// A thin C++ layer over ESP-IDF's esp_http_server: std::function routes, JSON helpers and a
// WebSocket broadcast that is safe from any task.
#pragma once

#include <esp_http_server.h>

#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace tapwave::net {

class HttpServer {
public:
    using Handler = std::function<esp_err_t(httpd_req_t* req)>;

    bool start(std::uint16_t port, std::uint16_t max_handlers);
    bool running() const { return server_ != nullptr; }

    // Routes are matched in registration order; "*" at the end of a URI matches any suffix.
    bool on(const char* uri, httpd_method_t method, Handler handler);

    // A WebSocket endpoint. After the handshake, `authorize` decides whether the client may
    // stay, and on_open runs on the server task for clients that may.
    bool websocket(const char* uri, std::function<bool(httpd_req_t* req)> authorize,
                   std::function<void(int fd)> on_open);

    // Sends a text frame to every WebSocket client, or to one. Safe from any task.
    void broadcast(std::string text);
    void send(int fd, std::string text);

    // ---- request and response helpers ----

    static std::optional<std::string> header(httpd_req_t* req, const char* name);

    // Reads the whole body. The error is the HTTP status to answer with.
    static std::expected<std::string, int> body(httpd_req_t* req, std::size_t max_size);

    static esp_err_t json(httpd_req_t* req, int status, std::string_view body);
    static esp_err_t error(httpd_req_t* req, int status, std::string_view code,
                           std::string_view message);
    static esp_err_t redirect(httpd_req_t* req, const char* location);
    static const char* status_line(int status);

private:
    static esp_err_t dispatch(httpd_req_t* req);
    static esp_err_t websocket_dispatch(httpd_req_t* req);

    httpd_handle_t server_ = nullptr;
    std::deque<Handler> handlers_;  // a deque keeps handler addresses stable
    std::function<bool(httpd_req_t* req)> authorize_;
    std::function<void(int fd)> on_open_;
};

}  // namespace tapwave::net
