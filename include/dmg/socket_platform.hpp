#pragma once

// Private socket portability layer. The protocol and reliability window do not
// depend on OS handles, Winsock initialization, or truncated-receive conventions.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dmg::netplay::socket_platform {
#ifdef _WIN32
using Handle = SOCKET;
using AddressLength = int;
inline constexpr Handle invalid_handle = INVALID_SOCKET;
inline int last_error() noexcept { return WSAGetLastError(); }
inline std::string describe_error(int error) { return "Winsock error " + std::to_string(error); }
inline bool temporary_error(int error) noexcept {
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}
#else
using Handle = int;
using AddressLength = socklen_t;
inline constexpr Handle invalid_handle = -1;
inline int last_error() noexcept { return errno; }
inline std::string describe_error(int error) { return std::strerror(error); }
inline bool temporary_error(int error) noexcept {
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}
#endif

class Runtime {
public:
    Runtime() {
#ifdef _WIN32
        WSADATA data{};
        const int status = WSAStartup(MAKEWORD(2, 2), &data);
        if (status != 0) throw std::runtime_error("UDP WSAStartup: " + describe_error(status));
        if (data.wVersion != MAKEWORD(2, 2)) {
            WSACleanup();
            throw std::runtime_error("UDP requires Winsock 2.2");
        }
#endif
    }
    ~Runtime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
};

inline Handle create_datagram() noexcept {
#ifdef _WIN32
    // SOCKET is pointer-width on Win64; never narrow it to an int. Prevent
    // inherited handles just as FD_CLOEXEC does for the POSIX implementation.
    return WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
#else
    return socket(AF_INET, SOCK_DGRAM, 0);
#endif
}
inline void close_handle(Handle handle) noexcept {
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
}

class Socket {
    Handle handle_{invalid_handle};
public:
    explicit Socket(Handle handle) noexcept : handle_(handle) {}
    ~Socket() { if (handle_ != invalid_handle) close_handle(handle_); }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Handle get() const noexcept { return handle_; }
    bool valid() const noexcept { return handle_ != invalid_handle; }
};

inline bool nonblocking(Handle handle) noexcept {
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(handle, FIONBIO, &enabled) == 0;
#else
    return fcntl(handle, F_SETFL, O_NONBLOCK) == 0 && fcntl(handle, F_SETFD, FD_CLOEXEC) == 0;
#endif
}

inline std::ptrdiff_t send_datagram(Handle handle, const std::uint8_t *bytes, std::size_t size,
                                   const sockaddr_in &address) noexcept {
#ifdef _WIN32
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        WSASetLastError(WSAEMSGSIZE); return -1;
    }
    return sendto(handle, reinterpret_cast<const char *>(bytes), static_cast<int>(size), 0,
                  reinterpret_cast<const sockaddr *>(&address), static_cast<int>(sizeof(address)));
#else
    return sendto(handle, bytes, size, 0, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#endif
}

struct ReceiveResult { std::ptrdiff_t size; bool truncated; };
inline ReceiveResult receive_datagram(Handle handle, std::uint8_t *bytes, std::size_t capacity,
                                      sockaddr_in &address, AddressLength &length) noexcept {
#ifdef _WIN32
    if (capacity > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        WSASetLastError(WSAEMSGSIZE); return {-1, false};
    }
    const auto count = recvfrom(handle, reinterpret_cast<char *>(bytes), static_cast<int>(capacity), 0,
                                reinterpret_cast<sockaddr *>(&address), &length);
    // Winsock consumes an oversized UDP datagram but reports SOCKET_ERROR,
    // unlike POSIX recvfrom, which returns the copied prefix. Preserve that
    // distinction so a valid prefix can never be accepted as a complete packet.
    if (count == SOCKET_ERROR && last_error() == WSAEMSGSIZE)
        return {static_cast<std::ptrdiff_t>(capacity), true};
    return {count, false};
#else
    return {recvfrom(handle, bytes, capacity, 0, reinterpret_cast<sockaddr *>(&address), &length), false};
#endif
}
} // namespace dmg::netplay::socket_platform
