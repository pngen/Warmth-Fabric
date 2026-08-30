#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <netdb.h>
#endif

namespace warmth::net {

#ifdef _WIN32
using socket_type = SOCKET;
constexpr socket_type invalid_socket = INVALID_SOCKET;
#else
using socket_type = int;
constexpr socket_type invalid_socket = -1;
#endif

// Initialize the socket layer (WSAStartup on Windows; no-op elsewhere).
// Returns false if initialization fails. Idempotent.
bool socket_init();
void socket_cleanup();

// A blocking TCP connection. Not thread-safe; use one connection per thread or
// externally synchronize.
class TcpConnection {
public:
    TcpConnection() = default;
    explicit TcpConnection(socket_type s) : sock_(s) {}
    ~TcpConnection();
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& o) noexcept;
    TcpConnection& operator=(TcpConnection&& o) noexcept;

    [[nodiscard]] bool valid() const noexcept { return sock_ != invalid_socket; }
    [[nodiscard]] socket_type native() const noexcept { return sock_; }

    // Write exactly n bytes; returns false on any error.
    bool write_all(const std::uint8_t* data, std::size_t n);
    bool write_all(const std::vector<std::uint8_t>& data);
    // Read exactly n bytes; returns false on EOF or error.
    bool read_exact(std::uint8_t* out, std::size_t n);
    bool read_exact(std::vector<std::uint8_t>& out, std::size_t n);
    void close();

private:
    socket_type sock_ = invalid_socket;
};

class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // Bind and listen on host:port. If port==0 the OS assigns a free port.
    bool listen(const std::string& host, std::uint16_t port);
    // Block until a connection arrives. Returns std::nullopt if the listener
    // was closed or an error occurred.
    std::optional<TcpConnection> accept();
    [[nodiscard]] std::uint16_t port() const noexcept;
    void close();

private:
    socket_type sock_ = invalid_socket;
    std::uint16_t port_ = 0;
};

// Convenience: connect to host:port. Returns an invalid connection on failure.
TcpConnection connect(const std::string& host, std::uint16_t port);

} // namespace warmth::net
