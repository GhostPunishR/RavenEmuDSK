// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/session_store.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#include <utility>
#include <vector>

namespace ravenemudsk_ra {
namespace {
bool valid(const SessionCredentials& session) {
    return !session.username.empty() && session.username.size() <= 256 &&
           !session.token.empty() && session.token.size() <= CRED_MAX_CREDENTIAL_BLOB_SIZE &&
           session.username.find('\0') == std::string::npos &&
           session.token.find('\0') == std::string::npos;
}
std::wstring wide(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), out.data(), length)) return {};
    return out;
}
std::string utf8(const wchar_t* text) {
    if (!text) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                          nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string out(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                            out.data(), length, nullptr, nullptr)) return {};
    out.pop_back();
    return out;
}
class WindowsSessionStore final : public SessionStore {
public:
    explicit WindowsSessionStore(std::wstring target) : target_(std::move(target)) {}
    std::optional<SessionCredentials> load() override {
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target_.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return std::nullopt;
        SessionCredentials session;
        if (credential->CredentialBlob && credential->CredentialBlobSize > 0 &&
            credential->CredentialBlobSize <= CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            session.username = utf8(credential->UserName);
            session.token.assign(reinterpret_cast<const char*>(credential->CredentialBlob),
                                 credential->CredentialBlobSize);
            SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
        }
        CredFree(credential);
        return valid(session) ? std::optional<SessionCredentials>(std::move(session)) : std::nullopt;
    }
    bool save(const SessionCredentials& session) override {
        if (!valid(session)) return false;
        auto username = wide(session.username);
        if (username.empty()) return false;
        std::vector<BYTE> token(session.token.begin(), session.token.end());
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target_.data();
        credential.UserName = username.data();
        credential.CredentialBlob = token.data();
        credential.CredentialBlobSize = static_cast<DWORD>(token.size());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        const bool stored = CredWriteW(&credential, 0) != FALSE;
        SecureZeroMemory(token.data(), token.size());
        return stored;
    }
    bool clear() override {
        return CredDeleteW(target_.c_str(), CRED_TYPE_GENERIC, 0) != FALSE || GetLastError() == ERROR_NOT_FOUND;
    }
private:
    std::wstring target_;
};
}
std::shared_ptr<SessionStore> make_session_store(const std::string& target) {
    if (target.empty() || target.size() > 256 || target.find('\0') != std::string::npos) return nullptr;
    const auto name = wide(target);
    return name.empty() ? nullptr : std::make_shared<WindowsSessionStore>(name);
}
} // namespace ravenemudsk_ra
#else
namespace ravenemudsk_ra {
std::shared_ptr<SessionStore> make_session_store(const std::string&) { return nullptr; }
} // namespace ravenemudsk_ra
#endif
