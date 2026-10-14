#include "edgecontrol/metrics_server.hpp"

#include "edgecontrol/network.hpp"

#include <array>
#include <cerrno>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace edgecontrol {
namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;

int lastSocketError() noexcept {
    return WSAGetLastError();
}

void closeSocket(Socket socket) noexcept {
    static_cast<void>(closesocket(socket));
}

class SocketRuntime {
public:
    SocketRuntime() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            throw std::runtime_error("WSAStartup failed with error " + std::to_string(result));
        }
    }

    ~SocketRuntime() {
        static_cast<void>(WSACleanup());
    }
};
#else
using Socket = int;
constexpr Socket invalid_socket = -1;

int lastSocketError() noexcept {
    return errno;
}

void closeSocket(Socket socket) noexcept {
    static_cast<void>(close(socket));
}

class SocketRuntime {};
#endif

class SocketGuard {
public:
    explicit SocketGuard(Socket socket) noexcept : socket_(socket) {}

    ~SocketGuard() {
        if (socket_ != invalid_socket) {
            closeSocket(socket_);
        }
    }

    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

    [[nodiscard]] Socket get() const noexcept {
        return socket_;
    }

private:
    Socket socket_;
};

[[noreturn]] void throwSocketError(const std::string& operation) {
    throw std::system_error(lastSocketError(), std::system_category(), operation);
}

void sendAll(Socket socket, std::string_view response) {
    std::size_t sent = 0;
    while (sent < response.size()) {
#ifdef _WIN32
        const int result = send(socket, response.data() + sent,
                                static_cast<int>(response.size() - sent), 0);
#else
        const auto result =
            send(socket, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
#endif
        if (result < 0) {
            throwSocketError("send");
        }
        if (result == 0) {
            throw std::runtime_error("send returned zero before the response was complete.");
        }
        sent += static_cast<std::size_t>(result);
    }
}

bool isMetricsRequest(const char* request, std::size_t request_size) {
    const std::string_view request_text(request, request_size);
    const std::size_t line_end = request_text.find("\r\n");
    if (line_end == std::string_view::npos) {
        return false;
    }

    const std::string_view request_line = request_text.substr(0, line_end);
    const std::size_t method_end = request_line.find(' ');
    if (method_end == std::string_view::npos) {
        return false;
    }

    const std::size_t target_end = request_line.find(' ', method_end + 1U);
    if (target_end == std::string_view::npos ||
        request_line.find(' ', target_end + 1U) != std::string_view::npos) {
        return false;
    }

    const std::string_view method = request_line.substr(0, method_end);
    const std::string_view target =
        request_line.substr(method_end + 1U, target_end - method_end - 1U);
    const std::string_view version = request_line.substr(target_end + 1U);

    return method == "GET" && target == "/metrics" &&
           (version == "HTTP/1.0" || version == "HTTP/1.1");
}

std::optional<std::size_t> receiveRequest(Socket client, std::array<char, 2048>& request) {
    std::size_t request_size = 0;
    while (request_size < request.size()) {
#ifdef _WIN32
        const int received = recv(client, request.data() + request_size,
                                  static_cast<int>(request.size() - request_size), 0);
#else
        const auto received =
            recv(client, request.data() + request_size, request.size() - request_size, 0);
#endif
        if (received == 0) {
            return std::nullopt;
        }
        if (received < 0) {
            throwSocketError("recv");
        }

        request_size += static_cast<std::size_t>(received);
        if (std::string_view(request.data(), request_size).find("\r\n") !=
            std::string_view::npos) {
            return request_size;
        }
    }
    return request_size;
}

void sendHttpResponse(Socket client, std::string_view status,
                      std::string_view content_type, std::string_view body) {
    std::string response = "HTTP/1.1 ";
    response.append(status);
    response.append("\r\nContent-Type: ");
    response.append(content_type);
    response.append("\r\nContent-Length: ");
    response.append(std::to_string(body.size()));
    response.append("\r\nConnection: close\r\n\r\n");
    response.append(body);
    sendAll(client, response);
}

void handleClient(const Network& network, Socket client) {
    std::array<char, 2048> request{};
    const std::optional<std::size_t> request_size = receiveRequest(client, request);
    if (!request_size.has_value()) {
        return;
    }

    if (isMetricsRequest(request.data(), *request_size)) {
        const std::string body = network.prometheusMetrics();
        sendHttpResponse(client, "200 OK",
                         "text/plain; version=0.0.4; charset=utf-8", body);
        return;
    }

    sendHttpResponse(client, "404 Not Found", "text/plain; charset=utf-8",
                     "Not found. Try GET /metrics\n");
}

} // namespace

void servePrometheusMetrics(const Network& network, std::uint16_t port) {
    if (port == 0U) {
        throw std::invalid_argument("Metrics server port must be in range 1..65535.");
    }

    [[maybe_unused]] const SocketRuntime socket_runtime;
    const SocketGuard server(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (server.get() == invalid_socket) {
        throwSocketError("socket");
    }

    int reuse_address = 1;
    if (setsockopt(server.get(), SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
                   reinterpret_cast<const char*>(&reuse_address),
#else
                   &reuse_address,
#endif
                   static_cast<int>(sizeof(reuse_address))) != 0) {
        throwSocketError("setsockopt");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(server.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throwSocketError("bind");
    }
    if (listen(server.get(), 16) != 0) {
        throwSocketError("listen");
    }

    while (true) {
        const SocketGuard client(accept(server.get(), nullptr, nullptr));
        if (client.get() == invalid_socket) {
            throwSocketError("accept");
        }
        handleClient(network, client.get());
    }
}

} // namespace edgecontrol
