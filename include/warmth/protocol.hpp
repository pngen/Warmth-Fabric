#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <cstring>
#include <cmath>
#include <span>
#include <algorithm>
#include <iterator>

#include "warmth/transport.hpp"
#include "warmth/detail/hash.hpp"
#include "warmth/identity.hpp"
#include "warmth/detail/macros.hpp"

namespace warmth::proto {

// Message and framing constants.
constexpr std::uint32_t kFrameMagic = 0x5746524Du; // "WFRM"
constexpr std::uint8_t kFrameVersion = 1u;
constexpr std::uint32_t kMaxPayload = 16u * 1024u * 1024u; // 16 MiB

#ifdef ERROR
#undef ERROR
#endif
enum class MessageType : std::uint16_t {
    HELLO = 1,
    HELLO_ACK = 2,
    REPORT = 3,
    WARM_TOPIC = 4,
    WARM_RESULT = 5,
    PING = 6,
    PONG = 7,
    SHUTDOWN = 8,
    ERROR = 9,
    READY = 10
};

constexpr bool known_type(std::uint16_t t) noexcept {
    switch (static_cast<MessageType>(t)) {
        case MessageType::HELLO: case MessageType::HELLO_ACK: case MessageType::REPORT:
        case MessageType::WARM_TOPIC: case MessageType::WARM_RESULT: case MessageType::PING:
        case MessageType::PONG: case MessageType::SHUTDOWN: case MessageType::ERROR:
        case MessageType::READY: return true;
    }
    return false;
}

constexpr const char* message_type_name(MessageType t) noexcept {
    switch (t) {
        case MessageType::HELLO:      return "HELLO";
        case MessageType::HELLO_ACK:  return "HELLO_ACK";
        case MessageType::REPORT:     return "REPORT";
        case MessageType::WARM_TOPIC: return "WARM_TOPIC";
        case MessageType::WARM_RESULT:return "WARM_RESULT";
        case MessageType::PING:       return "PING";
        case MessageType::PONG:       return "PONG";
        case MessageType::SHUTDOWN:   return "SHUTDOWN";
        case MessageType::ERROR:      return "ERROR";
        case MessageType::READY:      return "READY";
    }
    return "UNKNOWN";
}

struct Frame {
    MessageType type = MessageType::PING;
    std::uint8_t flags = 0;
    std::vector<std::uint8_t> payload;
};

// FrameWriter wraps a connection and writes length-prefixed, checksummed frames.
class FrameWriter {
public:
    explicit FrameWriter(net::TcpConnection& conn) : conn_(conn) {}
    bool write(const Frame& frame) {
        const auto& p = frame.payload;
        if (p.size() > kMaxPayload) return false;
        std::uint8_t header[12];
        header[0] = static_cast<std::uint8_t>(kFrameMagic >> 24);
        header[1] = static_cast<std::uint8_t>(kFrameMagic >> 16);
        header[2] = static_cast<std::uint8_t>(kFrameMagic >> 8);
        header[3] = static_cast<std::uint8_t>(kFrameMagic >> 0);
        header[4] = kFrameVersion;
        header[5] = frame.flags;
        header[6] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(frame.type) >> 8);
        header[7] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(frame.type) >> 0);
        const std::uint32_t len = static_cast<std::uint32_t>(p.size());
        header[8] = static_cast<std::uint8_t>(len >> 24);
        header[9] = static_cast<std::uint8_t>(len >> 16);
        header[10] = static_cast<std::uint8_t>(len >> 8);
        header[11] = static_cast<std::uint8_t>(len >> 0);
        if (!conn_.write_all(header, 12)) return false;
        if (!p.empty() && !conn_.write_all(p.data(), p.size())) return false;
        const std::uint32_t crc = detail::crc32(p.data(), p.size());
        std::uint8_t cb[4];
        cb[0] = static_cast<std::uint8_t>(crc >> 24); cb[1] = static_cast<std::uint8_t>(crc >> 16);
        cb[2] = static_cast<std::uint8_t>(crc >> 8);  cb[3] = static_cast<std::uint8_t>(crc >> 0);
        return conn_.write_all(cb, 4);
    }

private:
    net::TcpConnection& conn_;
};

// FrameReader reads and validates frames, rejecting malformed or truncated
// input. A false return indicates a protocol or transport violation.
class FrameReader {
public:
    explicit FrameReader(net::TcpConnection& conn) : conn_(conn) {}
    bool read(Frame& frame) {
        std::uint8_t header[12];
        if (!conn_.read_exact(header, 12)) return false;
        const std::uint32_t magic = (static_cast<std::uint32_t>(header[0]) << 24) |
                                    (static_cast<std::uint32_t>(header[1]) << 16) |
                                    (static_cast<std::uint32_t>(header[2]) << 8) |
                                    static_cast<std::uint32_t>(header[3]);
        if (magic != kFrameMagic) return false;
        if (header[4] != kFrameVersion) return false;
        const std::uint16_t type = static_cast<std::uint16_t>((static_cast<std::uint16_t>(header[6]) << 8) | header[7]);
        if (!known_type(type)) return false;
        const std::uint32_t len = (static_cast<std::uint32_t>(header[8]) << 24) |
                                  (static_cast<std::uint32_t>(header[9]) << 16) |
                                  (static_cast<std::uint32_t>(header[10]) << 8) |
                                  static_cast<std::uint32_t>(header[11]);
        if (len > kMaxPayload) return false;
        if (!conn_.read_exact(frame.payload, len)) return false;
        std::uint8_t cb[4];
        if (!conn_.read_exact(cb, 4)) return false;
        const std::uint32_t crc = (static_cast<std::uint32_t>(cb[0]) << 24) | (static_cast<std::uint32_t>(cb[1]) << 16) |
                                  (static_cast<std::uint32_t>(cb[2]) << 8) | static_cast<std::uint32_t>(cb[3]);
        const std::uint32_t actual = detail::crc32(frame.payload.data(), frame.payload.size());
        if (crc != actual) return false;
        frame.type = static_cast<MessageType>(type);
        frame.flags = header[5];
        return true;
    }

private:
    net::TcpConnection& conn_;
};

// ---------------------------------------------------------------------------
// WireValue codec for message payloads (big-endian, length-prefixed strings).
// ---------------------------------------------------------------------------
class WireEncoder {
public:
    void u8(std::uint8_t v) { out_.push_back(v); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v >> 8)); u8(static_cast<std::uint8_t>(v)); }
    void u32(std::uint32_t v) { u8(static_cast<std::uint8_t>(v >> 24)); u8(static_cast<std::uint8_t>(v >> 16)); u8(static_cast<std::uint8_t>(v >> 8)); u8(static_cast<std::uint8_t>(v)); }
    void u64(std::uint64_t v) { for (int i = 7; i >= 0; --i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
    void f64(double d) { std::uint64_t bits = 0; std::memcpy(&bits, &d, sizeof(bits)); u64(bits); }
    void id(const Id128& id) { const auto b = id.to_bytes(); std::copy(b.begin(), b.end(), std::back_inserter(out_)); }
    void bytes(const std::uint8_t* p, std::size_t n) { out_.insert(out_.end(), p, p + n); }
    void str(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); bytes(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }
    [[nodiscard]] const std::vector<std::uint8_t>& data() const { return out_; }
    [[nodiscard]] std::vector<std::uint8_t> done() { return std::move(out_); }
private:
    std::vector<std::uint8_t> out_;
};

class WireDecoder {
public:
    explicit WireDecoder(std::span<const std::uint8_t> s) : s_(s) {}
    bool ok() const { return ok_; }
    bool peek_u8(std::uint8_t& v) const { if (i_ >= s_.size()) return false; v = s_[i_]; return true; }
    bool u8(std::uint8_t& v) { if (!take(1)) return false; v = s_[i_ - 1]; return true; }
    bool u16(std::uint16_t& v) { std::uint8_t a, b; if (!u8(a) || !u8(b)) return false; v = static_cast<std::uint16_t>((a << 8) | b); return true; }
    bool u32(std::uint32_t& v) {
        std::uint8_t b[4]; if (!take(4)) return false;
        for (int k = 0; k < 4; ++k) b[k] = s_[i_ - 4 + k];
        v = (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
            (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
        return true;
    }
    bool u64(std::uint64_t& v) {
        std::uint8_t b[8]; if (!take(8)) return false;
        for (int k = 0; k < 8; ++k) b[k] = s_[i_ - 8 + k];
        v = 0; for (int k = 0; k < 8; ++k) v = (v << 8) | b[k];
        return true;
    }
    bool f64(double& d) { std::uint64_t bits = 0; if (!u64(bits)) return false; std::memcpy(&d, &bits, sizeof(d)); return true; }
    bool id(Id128& out) {
        std::uint8_t b[16]; if (!take(16)) return false;
        for (int k = 0; k < 16; ++k) b[k] = s_[i_ - 16 + k];
        out = Id128::from_bytes(b); return true;
    }
    bool str(std::string& out) {
        std::uint32_t len = 0; if (!u32(len)) return false;
        if (static_cast<std::size_t>(len) > remaining()) return false;
        out.assign(reinterpret_cast<const char*>(s_.data() + i_), len);
        i_ += len;
        return true;
    }
    [[nodiscard]] std::size_t remaining() const { return s_.size() - i_; }
private:
    bool take(std::size_t n) { if (n > remaining()) { ok_ = false; return false; } i_ += n; return true; }
    std::span<const std::uint8_t> s_;
    std::size_t i_ = 0;
    bool ok_ = true;
};

} // namespace warmth::proto
