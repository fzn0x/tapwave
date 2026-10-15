// Serves the web UI from LittleFS: gzipped files with the right content type and caching, a
// fallback to index.html for page routes, and a built-in recovery page when the filesystem is
// empty or missing.
#pragma once

#include <FS.h>
#include <esp_http_server.h>

#include <string_view>

namespace tapwave::net {

// The content type for a file name.
const char* content_type(std::string_view path);

// Serves `path` (from the request URI, without the query) out of `root` on `fs`.
// Returns ESP_ERR_NOT_FOUND when neither the file nor index.html exists, so the caller can
// answer with the recovery page or a 404.
esp_err_t serve_file(httpd_req_t* req, fs::FS& fs, std::string_view root, std::string_view path);

// A small page built into the firmware that can upload the web UI and firmware images.
esp_err_t serve_recovery_page(httpd_req_t* req);

}  // namespace tapwave::net
