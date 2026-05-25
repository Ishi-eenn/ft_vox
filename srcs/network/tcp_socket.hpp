#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// Thin POSIX TCP wrapper used by both server and client.
// Sockets are set non-blocking after accept/connect.
class TcpSocket {
public:
    TcpSocket() = default;
    explicit TcpSocket(int fd);
    ~TcpSocket() { close(); }

    TcpSocket(const TcpSocket&)            = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& o) noexcept;
    TcpSocket& operator=(TcpSocket&& o) noexcept;

    // Server side: create listening socket bound to port.
    bool listen(uint16_t port);
    // Non-blocking accept; returns invalid socket if no pending connection.
    TcpSocket accept() const;

    // Client side: blocking connect to host:port.
    bool connect(const char* host, uint16_t port);

    // Send all bytes (blocking, suitable for small packets).
    bool sendRaw(const void* data, size_t len) const;

    // Non-blocking recv into internal ring buffer.
    // Extracts all complete packets (header+payload) and appends them to out.
    // Each element of out is the raw bytes of one complete packet.
    // Returns false if the connection was closed or errored.
    bool poll(std::vector<std::vector<uint8_t>>& out);

    bool isValid() const { return fd_ >= 0; }
    void close();
    int  fd()    const  { return fd_; }

private:
    void makeNonBlocking() const;

    int                  fd_ = -1;
    std::vector<uint8_t> buf_;
};
