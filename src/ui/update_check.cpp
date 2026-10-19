// SPDX-License-Identifier: AGPL-3.0-only
// HTTPS request behind UpdateCheck: WinHTTP on Windows, libcurl elsewhere
// when available. Only GET /repos/GhostPunishR/RavenEmuDSK/releases/latest.
#include <ravenemudsk/ui/updates.hpp>
#include <cstddef>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#elif defined(RAVENEMUDSK_HAVE_CURL)
#include <curl/curl.h>
#endif

namespace ravenemudsk_ui {
namespace {
constexpr std::size_t max_response = 1u << 20;
struct Response {
    long status = 0;
    std::string body;
    std::string error;
};
#ifdef _WIN32
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};
Response fetch_latest() {
    Response r;
    Handle session{WinHttpOpen(L"RavenEmuDSK", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) { r.error = "WinHTTP indisponible"; return r; }
    WinHttpSetTimeouts(session.h, 5000, 5000, 5000, 8000);
    Handle connection{WinHttpConnect(session.h, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    Handle request{connection.h ? WinHttpOpenRequest(connection.h, L"GET",
        L"/repos/GhostPunishR/RavenEmuDSK/releases/latest", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr};
    if (!request.h ||
        !WinHttpSendRequest(request.h, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = "Connexion à GitHub impossible (erreur " + std::to_string(GetLastError()) + ")";
        return r;
    }
    DWORD code = 0, size = sizeof(code);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<long>(code);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available) || available == 0) break;
        if (r.body.size() + available > max_response) { r.error = "Réponse trop volumineuse"; break; }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read) || read == 0) break;
        r.body.append(chunk, 0, read);
    }
    return r;
}
#elif defined(RAVENEMUDSK_HAVE_CURL)
size_t collect(char* data, size_t size, size_t count, void* user) {
    auto* body = static_cast<std::string*>(user);
    if (body->size() + size * count > max_response) return 0;  // aborts the transfer
    body->append(data, size * count);
    return size * count;
}
Response fetch_latest() {
    Response r;
    CURL* curl = curl_easy_init();
    if (!curl) { r.error = "libcurl indisponible"; return r; }
    curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_URL,
                     "https://api.github.com/repos/GhostPunishR/RavenEmuDSK/releases/latest");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RavenEmuDSK");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) r.error = std::string("Connexion à GitHub impossible : ") + curl_easy_strerror(result);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return r;
}
#endif
} // namespace

bool UpdateCheck::supported() {
#if defined(_WIN32) || defined(RAVENEMUDSK_HAVE_CURL)
    return true;
#else
    return false;
#endif
}
void UpdateCheck::start(const std::string& current_version) {
    std::lock_guard lock(shared_->mutex);
    if (shared_->status == Status::Checking) return;
    if (!supported()) {
        shared_->status = Status::Failed;
        shared_->error = "Vérification non disponible dans cette version";
        return;
    }
#if defined(RAVENEMUDSK_HAVE_CURL) && !defined(_WIN32)
    static const bool curl_ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!curl_ready) { shared_->status = Status::Failed; shared_->error = "libcurl indisponible"; return; }
#endif
    shared_->status = Status::Checking;
    shared_->error.clear();
    std::thread([shared = shared_, current_version] {
        Status status = Status::Failed;
        std::optional<Version> latest;
        std::string error;
#if defined(_WIN32) || defined(RAVENEMUDSK_HAVE_CURL)
        const Response r = fetch_latest();
        const auto current = parse_version(current_version);
        if (!r.error.empty()) {
            error = r.error;
        } else if (r.status == 404) {
            status = Status::NoRelease;  // nothing published yet
        } else if (r.status != 200) {
            error = "GitHub a répondu " + std::to_string(r.status);
        } else if (const auto tag = latest_tag(r.body); tag && (latest = parse_version(*tag))) {
            status = current && is_newer(*latest, *current) ? Status::Available : Status::UpToDate;
        } else {
            error = "Réponse de GitHub illisible";
        }
#endif
        std::lock_guard done(shared->mutex);
        shared->status = status;
        shared->latest = latest;
        shared->error = error;
    }).detach();
}
UpdateCheck::Status UpdateCheck::status() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->status;
}
std::optional<Version> UpdateCheck::latest() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->latest;
}
std::string UpdateCheck::error() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->error;
}
} // namespace ravenemudsk_ui
