#include "net/static_files.hpp"

#include <array>
#include <string>

namespace tapwave::net {

namespace {

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

constexpr char kRecoveryPage[] = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tapwave recovery</title>
<style>
body{font:16px/1.5 system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#1b1b1f;background:#f6f5f2}
h1{font-size:1.4rem}section{background:#fff;border:1px solid #ddd;border-radius:12px;padding:1rem;margin:1rem 0}
button{font:inherit;padding:.6rem 1rem;border-radius:8px;border:0;background:#1b1b1f;color:#fff}
#log{white-space:pre-wrap;font-family:ui-monospace,monospace;font-size:.9rem}
</style></head><body>
<h1>Tapwave needs its web interface</h1>
<p>The firmware is running, but the files for the web interface are missing. This happens when
only the firmware was flashed. Upload the web interface file from the release page
(<code>tapwave-&lt;board&gt;-littlefs.bin</code>) and the device restarts with the full interface.</p>
<section><label>Web interface file<br><input type="file" id="fs" accept=".bin"></label>
<p><button onclick="send('fs','filesystem')">Upload web interface</button></p></section>
<section><label>Firmware file<br><input type="file" id="fw" accept=".bin"></label>
<p><button onclick="send('fw','firmware')">Upload firmware</button></p></section>
<p id="log"></p>
<p>Help: https://github.com/fzn0x/tapwave/wiki/Troubleshooting</p>
<script>
async function send(id, target) {
  const file = document.getElementById(id).files[0];
  const log = document.getElementById('log');
  if (!file) { log.textContent = 'Choose a file first.'; return; }
  log.textContent = 'Uploading ' + file.name + '...';
  try {
    const r = await fetch('/api/update/' + target, {method: 'POST',
      headers: {'Content-Type': 'application/octet-stream'}, body: file});
    const j = await r.json().catch(() => ({}));
    log.textContent = r.ok ? 'Done. The device is restarting; reload this page in a minute.'
                           : 'The upload failed: ' + ((j.error && j.error.message) || r.status);
  } catch (e) { log.textContent = 'The upload failed: ' + e; }
}
</script></body></html>)html";

}  // namespace

const char* content_type(std::string_view path) {
    static constexpr std::array<std::pair<std::string_view, const char*>, 11> kTypes{{
        {".html", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".mjs", "text/javascript; charset=utf-8"},
        {".svg", "image/svg+xml"},
        {".json", "application/json"},
        {".webmanifest", "application/manifest+json"},
        {".png", "image/png"},
        {".ico", "image/x-icon"},
        {".woff2", "font/woff2"},
        {".txt", "text/plain; charset=utf-8"},
    }};
    for (const auto& [suffix, type] : kTypes) {
        if (ends_with(path, suffix)) {
            return type;
        }
    }
    return "application/octet-stream";
}

esp_err_t serve_file(httpd_req_t* req, fs::FS& fs, std::string_view root, std::string_view path) {
    std::string wanted{path};
    if (wanted.empty() || wanted == "/") {
        wanted = "/index.html";
    }
    if (wanted.find("..") != std::string::npos || wanted.front() != '/') {
        return ESP_ERR_NOT_FOUND;
    }
    const std::size_t slash = wanted.rfind('/');
    const bool page_route = wanted.find('.', slash) == std::string::npos;

    // Gzipped first, then plain; page routes ("/settings") fall back to the app shell.
    std::array<std::string, 4> candidates{
        std::string(root) + wanted + ".gz",
        std::string(root) + wanted,
        page_route ? std::string(root) + "/index.html.gz" : std::string{},
        page_route ? std::string(root) + "/index.html" : std::string{},
    };
    for (const std::string& candidate : candidates) {
        if (candidate.empty() || !fs.exists(candidate.c_str())) {
            continue;
        }
        File file = fs.open(candidate.c_str(), "r");
        if (!file || file.isDirectory()) {
            continue;
        }
        const bool gzipped = ends_with(candidate, ".gz");
        const std::string_view served =
            gzipped ? std::string_view(candidate).substr(0, candidate.size() - 3)
                    : std::string_view(candidate);
        httpd_resp_set_type(req, content_type(served));
        if (gzipped) {
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        }
        // index.html is always revalidated; it references assets with ?v=<version>.
        httpd_resp_set_hdr(
            req, "Cache-Control",
            ends_with(served, ".html") ? "no-cache" : "public, max-age=604800, immutable");
        std::array<char, 1024> buffer;
        while (true) {
            const std::size_t n =
                file.read(reinterpret_cast<std::uint8_t*>(buffer.data()), buffer.size());
            if (n == 0) {
                break;
            }
            if (httpd_resp_send_chunk(req, buffer.data(), static_cast<ssize_t>(n)) != ESP_OK) {
                file.close();
                return ESP_FAIL;
            }
        }
        file.close();
        return httpd_resp_send_chunk(req, nullptr, 0);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t serve_recovery_page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kRecoveryPage, sizeof(kRecoveryPage) - 1);
}

}  // namespace tapwave::net
