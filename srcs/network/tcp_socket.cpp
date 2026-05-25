#include "network/tcp_socket.hpp"
#include "network/packet.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <algorithm>

TcpSocket::TcpSocket(int fd) : fd_(fd) {
    makeNonBlocking();
}

TcpSocket::TcpSocket(TcpSocket&& o) noexcept
    : fd_(o.fd_), buf_(std::move(o.buf_)) {
    o.fd_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept {
    if (this != &o) {
        close();
        fd_  = o.fd_;
        buf_ = std::move(o.buf_);
        o.fd_ = -1;
    }
    return *this;
}

void TcpSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        buf_.clear();
    }
}

void TcpSocket::makeNonBlocking() const {
    if (fd_ < 0) return;
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) return;
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
}

bool TcpSocket::listen(uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        perror("[TcpSocket] socket");
        return false;
    }
    int yes = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("[TcpSocket] bind");
        close();
        return false;
    }
    if (::listen(fd_, 8) < 0) {
        perror("[TcpSocket] listen");
        close();
        return false;
    }
    makeNonBlocking();
    fprintf(stderr, "[Server] listening on port %u\n", port);
    return true;
}

TcpSocket TcpSocket::accept() const {
    sockaddr_in addr{};
    socklen_t   len = sizeof(addr);
    int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    if (cfd < 0) return TcpSocket{};   // EAGAIN / EWOULDBLOCK is expected

    // Disable Nagle's algorithm for low-latency small packets.
    int yes = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    fprintf(stderr, "[Server] accepted %s\n", inet_ntoa(addr.sin_addr));
    return TcpSocket{cfd};
}

bool TcpSocket::connect(const char* host, uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) { perror("[TcpSocket] socket"); return false; }

    int yes = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "[TcpSocket] invalid address: %s\n", host);
        close();
        return false;
    }
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("[TcpSocket] connect");
        close();
        return false;
    }
    makeNonBlocking();
    fprintf(stderr, "[Client] connected to %s:%u\n", host, port);
    return true;
}

bool TcpSocket::sendRaw(const void* data, size_t len) const {
    if (fd_ < 0) return false;
    const auto* p = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd_, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool TcpSocket::poll(std::vector<std::vector<uint8_t>>& out) {
    if (fd_ < 0) return false;

    // Read whatever is available.
    uint8_t tmp[4096];
    for (;;) {
        ssize_t n = ::recv(fd_, tmp, sizeof(tmp), 0);
        if (n > 0) {
            buf_.insert(buf_.end(), tmp, tmp + n);
        } else if (n == 0) {
            // Peer closed connection.
            close();
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            close();
            return false;
        }
    }

    // Extract complete packets from buf_.
    constexpr size_t HDR = sizeof(PacketHeader);
    while (buf_.size() >= HDR) {
        PacketHeader hdr;
        std::memcpy(&hdr, buf_.data(), HDR);
        size_t total = HDR + hdr.size;
        if (buf_.size() < total) break;

        out.emplace_back(buf_.begin(), buf_.begin() + total);
        buf_.erase(buf_.begin(), buf_.begin() + total);
    }
    return true;
}
