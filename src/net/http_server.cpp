#include "net/http_server.hpp"

#include <esp_log.h>

#include <array>
#include <memory>

namespace tapwave::net {

namespace {

constexpr const char* kTag = "http";
constexpr std::size_t kMaxClients = 6;

struct Outgoing {
    httpd_handle_t server;
    int fd;  // -1: every WebSocket client
    std::string text;
};

void send_now(void* arg) {
    std::unique_ptr<Outgoing> out(static_cast<Outgoing*>(arg));
    httpd_ws_frame_t frame{};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<std::uint8_t*>(out->text.data());
    frame.len = out->text.size();
    frame.final = true;
    if (out->fd >= 0) {
        httpd_ws_send_frame_async(out->server, out->fd, &frame);
        return;
    }
    std::array<int, kMaxClients + 2> fds{};
    std::size_t count = fds.size();
    if (httpd_get_client_list(out->server, &count, fds.data()) != ESP_OK) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (httpd_ws_get_fd_info(out->server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(out->server, fds[i], &frame);
        }
    }
}

}  // namespace

bool HttpServer::start(std::uint16_t port, std::uint16_t max_handlers) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = max_handlers;
    config.max_open_sockets = kMaxClients;
    config.lru_purge_enable = true;  // a phone that leaves sockets open cannot lock others out
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "the HTTP server did not start");
        server_ = nullptr;
        return false;
    }
    return true;
}

esp_err_t HttpServer::dispatch(httpd_req_t* req) {
    return (*static_cast<Handler*>(req->user_ctx))(req);
}

bool HttpServer::on(const char* uri, httpd_method_t method, Handler handler) {
    handlers_.push_back(std::move(handler));
    httpd_uri_t route{};
    route.uri = uri;
    route.method = method;
    route.handler = &HttpServer::dispatch;
    route.user_ctx = &handlers_.back();
    if (httpd_register_uri_handler(server_, &route) != ESP_OK) {
        ESP_LOGE(kTag, "could not register %s", uri);
        return false;
    }
    return true;
}

esp_err_t HttpServer::websocket_dispatch(httpd_req_t* req) {
    auto* self = static_cast<HttpServer*>(req->user_ctx);
    if (req->method == HTTP_GET) {
        if (self->on_open_) {
            self->on_open_(httpd_req_to_sockfd(req));  // the handshake is done
        }
        return ESP_OK;
    }
    // Clients do not send anything Tapwave acts on; read and drop data frames.
    httpd_ws_frame_t frame{};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    if (frame.len > 0 && frame.len <= 512) {
        std::string buffer(frame.len, '\0');
        frame.payload = reinterpret_cast<std::uint8_t*>(buffer.data());
        return httpd_ws_recv_frame(req, &frame, frame.len);
    }
    return frame.len > 512 ? ESP_FAIL : ESP_OK;
}

bool HttpServer::websocket(const char* uri, std::function<void(int fd)> on_open) {
    on_open_ = std::move(on_open);
    httpd_uri_t route{};
    route.uri = uri;
    route.method = HTTP_GET;
    route.handler = &HttpServer::websocket_dispatch;
    route.user_ctx = this;
    route.is_websocket = true;
    return httpd_register_uri_handler(server_, &route) == ESP_OK;
}

void HttpServer::broadcast(std::string text) {
    send(-1, std::move(text));
}

void HttpServer::send(int fd, std::string text) {
    if (!server_) {
        return;
    }
    auto* out = new Outgoing{server_, fd, std::move(text)};
    if (httpd_queue_work(server_, &send_now, out) != ESP_OK) {
        delete out;
    }
}

std::optional<std::string> HttpServer::header(httpd_req_t* req, const char* name) {
    const std::size_t length = httpd_req_get_hdr_value_len(req, name);
    if (length == 0) {
        return std::nullopt;
    }
    std::string value(length + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) {
        return std::nullopt;
    }
    value.resize(length);
    return value;
}

std::expected<std::string, int> HttpServer::body(httpd_req_t* req, std::size_t max_size) {
    if (req->content_len > max_size) {
        return std::unexpected(413);
    }
    std::string out(req->content_len, '\0');
    std::size_t received = 0;
    while (received < out.size()) {
        const int n = httpd_req_recv(req, out.data() + received, out.size() - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return std::unexpected(400);
        }
        received += static_cast<std::size_t>(n);
    }
    return out;
}

const char* HttpServer::status_line(int status) {
    switch (status) {
        case 200:
            return "200 OK";
        case 202:
            return "202 Accepted";
        case 204:
            return "204 No Content";
        case 302:
            return "302 Found";
        case 400:
            return "400 Bad Request";
        case 401:
            return "401 Unauthorized";
        case 403:
            return "403 Forbidden";
        case 404:
            return "404 Not Found";
        case 405:
            return "405 Method Not Allowed";
        case 409:
            return "409 Conflict";
        case 413:
            return "413 Payload Too Large";
        case 415:
            return "415 Unsupported Media Type";
        case 422:
            return "422 Unprocessable Entity";
        case 503:
            return "503 Service Unavailable";
        default:
            return "500 Internal Server Error";
    }
}

esp_err_t HttpServer::json(httpd_req_t* req, int status, std::string_view body) {
    httpd_resp_set_status(req, status_line(status));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body.data(), static_cast<ssize_t>(body.size()));
}

esp_err_t HttpServer::error(httpd_req_t* req, int status, std::string_view code,
                            std::string_view message) {
    std::string body = R"({"error":{"code":")";
    body.append(code);
    body += R"(","message":")";
    for (char c : message) {
        if (c == '"' || c == '\\') {
            body.push_back('\\');
        }
        body.push_back(c);
    }
    body += R"("}})";
    return json(req, status, body);
}

esp_err_t HttpServer::redirect(httpd_req_t* req, const char* location) {
    httpd_resp_set_status(req, status_line(302));
    httpd_resp_set_hdr(req, "Location", location);
    return httpd_resp_send(req, nullptr, 0);
}

}  // namespace tapwave::net
