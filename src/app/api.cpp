// The REST API and WebSocket (ARCHITECTURE.md section 13). The web UI uses nothing else.
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>

#include <string>

#include "app/app.hpp"
#include "core/rtttl.hpp"
#include "net/static_files.hpp"

namespace tapwave::app {

namespace {

using net::HttpServer;

constexpr std::size_t kMaxJsonBody = 4096;
constexpr const char* kJson = "application/json";
constexpr const char* kBinary = "application/octet-stream";
constexpr const char* kCookie = "tapwave_session";

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') {
        s.remove_prefix(1);
    }
    while (!s.empty() && s.back() == ' ') {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<std::string> cookie_token(httpd_req_t* req) {
    const auto cookies = HttpServer::header(req, "Cookie");
    if (!cookies) {
        return std::nullopt;
    }
    std::string_view rest = *cookies;
    while (!rest.empty()) {
        const auto semicolon = rest.find(';');
        const std::string_view pair = trim(rest.substr(0, semicolon));
        rest =
            semicolon == std::string_view::npos ? std::string_view{} : rest.substr(semicolon + 1);
        const auto equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == kCookie) {
            return std::string(pair.substr(equals + 1));
        }
    }
    return std::nullopt;
}

bool authorized(App& app, httpd_req_t* req) {
    if (!app.password_required()) {
        return true;
    }
    if (const auto token = cookie_token(req); token && app.valid_session(*token)) {
        return true;
    }
    // HTTP Basic, user "admin", for scripts and curl.
    const auto header = HttpServer::header(req, "Authorization");
    if (!header || !header->starts_with("Basic ")) {
        return false;
    }
    const std::string encoded = header->substr(6);
    unsigned char decoded[160];
    std::size_t length = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded), &length,
                              reinterpret_cast<const unsigned char*>(encoded.data()),
                              encoded.size()) != 0) {
        return false;
    }
    const std::string_view credentials(reinterpret_cast<const char*>(decoded), length);
    return credentials.starts_with("admin:") && app.check_password(credentials.substr(6));
}

// Wraps a handler with the access check and, for requests that change state, the content type
// check that keeps other websites from sending them (ARCHITECTURE.md section 13).
HttpServer::Handler guard(App& app, const char* content_type, HttpServer::Handler handler) {
    return [&app, content_type, handler = std::move(handler)](httpd_req_t* req) -> esp_err_t {
        if (!authorized(app, req)) {
            return HttpServer::error(req, 401, "unauthorized",
                                     "Sign in with the admin password first.");
        }
        if (content_type) {
            const auto type = HttpServer::header(req, "Content-Type");
            if (!type || !type->starts_with(content_type)) {
                return HttpServer::error(
                    req, 415, "unsupported_media_type",
                    std::string("Send this request with Content-Type: ") + content_type + ".");
            }
        }
        return handler(req);
    };
}

// Reads a JSON object body. On failure the response has already been sent.
std::optional<JsonDocument> json_body(httpd_req_t* req, bool allow_empty, esp_err_t& sent) {
    const auto body = HttpServer::body(req, kMaxJsonBody);
    if (!body) {
        sent = HttpServer::error(req, body.error(), "bad_body",
                                 body.error() == 413 ? "The request body is too large."
                                                     : "The request body could not be read.");
        return std::nullopt;
    }
    JsonDocument doc;
    if (body->empty() && allow_empty) {
        doc.to<JsonObject>();
        return doc;
    }
    if (deserializeJson(doc, *body) || !doc.is<JsonObject>()) {
        sent = HttpServer::error(req, 400, "bad_json", "The request body is not a JSON object.");
        return std::nullopt;
    }
    return doc;
}

esp_err_t accepted(httpd_req_t* req, std::string_view body = R"({"ok":true})") {
    return HttpServer::json(req, 202, body);
}

esp_err_t upload(App& app, httpd_req_t* req, net::UpdateTarget target) {
    if (req->content_len == 0) {
        return HttpServer::error(req, 400, "empty_upload", "Choose a .bin file to upload.");
    }
    auto result =
        app.updater().upload(target, req->content_len, [req](char* buffer, std::size_t size) {
            while (true) {
                const int n = httpd_req_recv(req, buffer, size);
                if (n != HTTPD_SOCK_ERR_TIMEOUT) {
                    return n;
                }
            }
        });
    if (!result) {
        return HttpServer::error(req, 422, "update_failed", result.error());
    }
    return HttpServer::json(req, 200, R"({"ok":true,"restarting":true})");
}

}  // namespace

void register_api(App& app, net::HttpServer& http, bool filesystem_ok) {
    http.on("/api/status", HTTP_GET,
            [&app](httpd_req_t* req) { return HttpServer::json(req, 200, app.status_json()); });

    http.on("/api/scans", HTTP_GET, guard(app, nullptr, [&app](httpd_req_t* req) {
                return HttpServer::json(req, 200, app.scans_json());
            }));

    http.on("/api/config", HTTP_GET, guard(app, nullptr, [&app](httpd_req_t* req) {
                return HttpServer::json(req, 200, app.config_json());
            }));

    http.on("/api/config", HTTP_PATCH, guard(app, kJson, [&app](httpd_req_t* req) {
                esp_err_t sent = ESP_OK;
                auto doc = json_body(req, false, sent);
                if (!doc) {
                    return sent;
                }
                const PatchResult result = app.patch_config(doc->as<JsonObjectConst>());
                JsonDocument out;
                JsonArray problems;
                if (!result.saved) {
                    JsonObject error = out["error"].to<JsonObject>();
                    error["code"] = "invalid_config";
                    error["message"] = "Some settings are not valid. Nothing was saved.";
                    problems = error["details"].to<JsonArray>();
                } else {
                    out["restart_required"] = result.restart_required;
                    problems = out["warnings"].to<JsonArray>();
                }
                for (const auto& issue : result.issues) {
                    JsonObject item = problems.add<JsonObject>();
                    item["path"] = issue.path;
                    item["message"] = issue.message;
                    item["warning"] = issue.warning;
                }
                std::string body;
                serializeJson(out, body);
                return HttpServer::json(req, result.saved ? 200 : 422, body);
            }));

    http.on("/api/config/reset", HTTP_POST, guard(app, kJson, [&app](httpd_req_t* req) {
                esp_err_t sent = ESP_OK;
                auto doc = json_body(req, false, sent);
                if (!doc) {
                    return sent;
                }
                if ((*doc)["confirm"] != true) {
                    return HttpServer::error(req, 400, "not_confirmed",
                                             "Send {\"confirm\": true} to erase all settings.");
                }
                app.factory_reset();
                return accepted(req, R"({"ok":true,"restarting":true})");
            }));

    http.on("/api/wifi/networks", HTTP_GET, guard(app, nullptr, [&app](httpd_req_t* req) {
                return HttpServer::json(req, 200, app.networks_json());
            }));

    http.on("/api/wifi", HTTP_POST, guard(app, kJson, [&app](httpd_req_t* req) {
                esp_err_t sent = ESP_OK;
                auto doc = json_body(req, false, sent);
                if (!doc) {
                    return sent;
                }
                const std::string ssid = (*doc)["ssid"] | "";
                const std::string password = (*doc)["password"] | "";
                if (ssid.empty() || ssid.size() > 32) {
                    return HttpServer::error(req, 422, "bad_ssid", "Choose a Wi-Fi network.");
                }
                if (!password.empty() && (password.size() < 8 || password.size() > 64)) {
                    return HttpServer::error(req, 422, "bad_password",
                                             "Wi-Fi passwords are 8 to 63 characters.");
                }
                app.try_wifi(ssid, password);
                return accepted(req);
            }));

    http.on("/api/reader", HTTP_GET, guard(app, nullptr, [&app](httpd_req_t* req) {
                return HttpServer::json(req, 200, app.reader_json());
            }));

    http.on("/api/reader/detect", HTTP_POST, guard(app, kJson, [&app](httpd_req_t* req) {
                app.scans().redetect();
                return accepted(req);
            }));

    http.on("/api/device/restart", HTTP_POST, guard(app, kJson, [&app](httpd_req_t* req) {
                app.post(RestartRequested{1000});
                return accepted(req, R"({"ok":true,"restarting":true})");
            }));

    http.on("/api/device/identify", HTTP_POST, guard(app, kJson, [&app](httpd_req_t* req) {
                app.post(Feedback{FeedbackKind::Identify});
                return accepted(req);
            }));

    http.on("/api/feedback", HTTP_POST, guard(app, kJson, [&app](httpd_req_t* req) {
                esp_err_t sent = ESP_OK;
                auto doc = json_body(req, false, sent);
                if (!doc) {
                    return sent;
                }
                const std::string result = (*doc)["result"] | "";
                if (result != "ok" && result != "error") {
                    return HttpServer::error(
                        req, 422, "bad_result",
                        "Send {\"result\": \"ok\"} or {\"result\": \"error\"}.");
                }
                app.post(Feedback{result == "ok" ? FeedbackKind::Ok : FeedbackKind::Error});
                return accepted(req);
            }));

    http.on("/api/buzzer/rtttl", HTTP_POST, guard(app, kJson, [&app](httpd_req_t* req) {
                esp_err_t sent = ESP_OK;
                auto doc = json_body(req, false, sent);
                if (!doc) {
                    return sent;
                }
                const std::string melody = (*doc)["melody"] | "";
                if (!rtttl::parse(melody)) {
                    return HttpServer::error(req, 422, "bad_melody",
                                             "The melody is not valid RTTTL, for example "
                                             "\"beep:d=16,o=5,b=100:c,g,b\".");
                }
                app.post(PlayMelody{melody});
                return accepted(req);
            }));

    http.on("/api/session", HTTP_POST, [&app](httpd_req_t* req) {
        const auto type = HttpServer::header(req, "Content-Type");
        if (!type || !type->starts_with(kJson)) {
            return HttpServer::error(req, 415, "unsupported_media_type",
                                     "Send this request with Content-Type: application/json.");
        }
        esp_err_t sent = ESP_OK;
        auto doc = json_body(req, false, sent);
        if (!doc) {
            return sent;
        }
        if (!app.password_required()) {
            return HttpServer::json(req, 200, R"({"ok":true,"required":false})");
        }
        const std::string password = (*doc)["password"] | "";
        if (!app.check_password(password)) {
            return HttpServer::error(req, 401, "wrong_password", "That password is not right.");
        }
        const std::string cookie = std::string(kCookie) + "=" + app.create_session() +
                                   "; Path=/; HttpOnly; SameSite=Strict; Max-Age=2592000";
        httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
        return HttpServer::json(req, 200, R"({"ok":true,"required":true})");
    });

    http.on("/api/session", HTTP_DELETE, [&app](httpd_req_t* req) {
        if (const auto token = cookie_token(req)) {
            app.end_session(*token);
        }
        httpd_resp_set_hdr(req, "Set-Cookie",
                           "tapwave_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
        return HttpServer::json(req, 200, R"({"ok":true})");
    });

    http.on("/api/update", HTTP_GET, guard(app, nullptr, [&app](httpd_req_t* req) {
                return HttpServer::json(req, 200, app.update_json());
            }));

    auto start_update = [&app](bool install) {
        return guard(app, kJson, [&app, install](httpd_req_t* req) {
            if (!app.updater().start(app.config().updates.manifest_url, install)) {
                return HttpServer::error(req, 409, "busy", "An update is already running.");
            }
            return accepted(req);
        });
    };
    http.on("/api/update/check", HTTP_POST, start_update(false));
    http.on("/api/update/install", HTTP_POST, start_update(true));

    http.on("/api/update/firmware", HTTP_POST, guard(app, kBinary, [&app](httpd_req_t* req) {
                return upload(app, req, net::UpdateTarget::Firmware);
            }));
    http.on("/api/update/filesystem", HTTP_POST, guard(app, kBinary, [&app](httpd_req_t* req) {
                return upload(app, req, net::UpdateTarget::Filesystem);
            }));

    http.websocket(
        "/ws", [&app](httpd_req_t* req) { return authorized(app, req); },
        [&app, &http](int fd) {
            http.send(fd, R"({"type":"hello","version":")" + app.device().version + R"(","id":")" +
                              app.device().id + "\"}");
            http.send(fd, R"({"type":"status","status":)" + app.status_json() + "}");
        });

    http.on("/api/*", HTTP_GET, [](httpd_req_t* req) {
        return HttpServer::error(req, 404, "not_found", "There is no such API endpoint.");
    });

    http.on("/*", HTTP_GET, [&app, filesystem_ok](httpd_req_t* req) {
        const auto host = HttpServer::header(req, "Host");
        if (const auto target = app.captive_portal_redirect(host.value_or(""))) {
            return HttpServer::redirect(req, target->c_str());
        }
        std::string_view path = req->uri;
        if (const auto query = path.find('?'); query != std::string_view::npos) {
            path = path.substr(0, query);
        }
        if (filesystem_ok) {
            const esp_err_t served = net::serve_file(req, LittleFS, "/www", path);
            if (served != ESP_ERR_NOT_FOUND) {
                return served;
            }
            if (LittleFS.exists("/www/index.html.gz") || LittleFS.exists("/www/index.html")) {
                httpd_resp_set_status(req, HttpServer::status_line(404));
                return httpd_resp_sendstr(req, "Not found");
            }
        }
        return net::serve_recovery_page(req);
    });
}

}  // namespace tapwave::app
