// Warmth Fabric - src/transport.cpp
// Copyright 2026 Summon Software Labs
// SPDX-License-Identifier: Apache-2.0
#include "warmth/transport.hpp"

#include <cstring>
#include <atomic>

namespace warmth::net {

namespace {
std::atomic<bool> g_ws_initialized{false};
}

bool socket_init() {
#ifdef _WIN32
    if (g_ws_initialized.load()) return true;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    g_ws_initialized.store(true);
    return true;
#else
    return true;
#endif
}

void socket_cleanup() {
#ifdef _WIN32
    if (g_ws_initialized.exchange(false)) WSACleanup();
#endif
}

TcpConnection::~TcpConnection() { close(); }

TcpConnection::TcpConnection(TcpConnection&& o) noexcept : sock_(o.sock_) { o.sock_ = invalid_socket; }

TcpConnection& TcpConnection::operator=(TcpConnection&& o) noexcept {
    if (this != &o) { close(); sock_ = o.sock_; o.sock_ = invalid_socket; }
    return *this;
}

bool TcpConnection::write_all(const std::uint8_t* data, std::size_t n) {
    if (!valid()) return false;
    std::size_t sent = 0;
    while (sent < n) {
        const std::size_t chunk = n - sent;
#ifdef _WIN32
        const int bytes = ::send(sock_, reinterpret_cast<const char*>(data + sent), static_cast<int>(chunk), 0);
#else
        const ssize_t bytes = ::send(sock_, data + sent, chunk, 0);
#endif
        if (bytes <= 0) return false;
        sent += static_cast<std::size_t>(bytes);
    }
    return true;
}

bool TcpConnection::write_all(const std::vector<std::uint8_t>& data) { return write_all(data.data(), data.size()); }

bool TcpConnection::read_exact(std::uint8_t* out, std::size_t n) {
    if (!valid()) return false;
    std::size_t got = 0;
    while (got < n) {
#ifdef _WIN32
        const int bytes = ::recv(sock_, reinterpret_cast<char*>(out + got), static_cast<int>(n - got), 0);
#else
        const ssize_t bytes = ::recv(sock_, out + got, n - got, 0);
#endif
        if (bytes == 0) return false;           // clean EOF
        if (bytes < 0) return false;            // error
        got += static_cast<std::size_t>(bytes);
    }
    return true;
}

bool TcpConnection::read_exact(std::vector<std::uint8_t>& out, std::size_t n) {
    out.resize(n);
    return read_exact(out.data(), n);
}

void TcpConnection::close() {
    if (!valid()) return;
#ifdef _WIN32
    // shutdown() first so a recv blocked on this socket from another thread
    // actually unblocks (closesocket alone may not interrupt a blocking recv).
    ::shutdown(sock_, SD_BOTH);
    ::closesocket(sock_);
#else
    ::shutdown(sock_, SHUT_RDWR);
    ::close(sock_);
#endif
    sock_ = invalid_socket;
}

TcpListener::~TcpListener() { close(); }

bool TcpListener::listen(const std::string& host, std::uint16_t port) {
    socket_init();
    close();
#ifdef _WIN32
    sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (sock_ == invalid_socket) return false;

    // Reuse a recently-used address port quickly.
    int yes = 1;
#ifdef _WIN32
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (host == "localhost" || host == "127.0.0.1" || host.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    }
    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close(); return false; }
    if (::listen(sock_, 16) != 0) { close(); return false; }

    sockaddr_in bound{};
#ifdef _WIN32
    int len = sizeof(bound);
    ::getsockname(sock_, reinterpret_cast<sockaddr*>(&bound), &len);
#else
    socklen_t len = sizeof(bound);
    ::getsockname(sock_, reinterpret_cast<sockaddr*>(&bound), &len);
#endif
    port_ = ntohs(bound.sin_port);
    return true;
}

std::optional<TcpConnection> TcpListener::accept() {
    if (sock_ == invalid_socket) return std::nullopt;
#ifdef _WIN32
    sockaddr_in cli{};
    int len = sizeof(cli);
    socket_type s = ::accept(sock_, reinterpret_cast<sockaddr*>(&cli), &len);
    if (s == invalid_socket) return std::nullopt;
    // Disable Nagle for low latency control messages.
    int yes = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
    return TcpConnection(s);
#else
    sockaddr_in cli{};
    socklen_t len = sizeof(cli);
    socket_type s = ::accept(sock_, reinterpret_cast<sockaddr*>(&cli), &len);
    if (s == invalid_socket) return std::nullopt;
    return TcpConnection(s);
#endif
}

std::uint16_t TcpListener::port() const noexcept { return port_; }

void TcpListener::close() {
    if (sock_ == invalid_socket) return;
#ifdef _WIN32
    ::closesocket(sock_);
#else
    ::close(sock_);
#endif
    sock_ = invalid_socket;
}

TcpConnection connect(const std::string& host, std::uint16_t port) {
    socket_init();
#ifdef _WIN32
    socket_type s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    socket_type s = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (s == invalid_socket) return TcpConnection();
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        ::closesocket(s);
#else
        ::close(s);
#endif
        return TcpConnection();
    }
    int yes = 1;
#ifdef _WIN32
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
    return TcpConnection(s);
}

} // namespace warmth::net
