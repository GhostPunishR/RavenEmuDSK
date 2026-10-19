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
#elif defined(__APPLE__) || defined(RAVENEMUDSK_HAVE_LIBSECRET)
#include <algorithm>
#include <cstring>
#include <vector>
#ifdef __APPLE__
#include <Security/Security.h>
#else
#include <libsecret/secret.h>
#endif
namespace ravenemudsk_ra {
namespace {
bool valid_field(const std::string& text, std::size_t limit) {
    return !text.empty() && text.size() <= limit && text.find_first_of("\n\r") == std::string::npos &&
        text.find('\0') == std::string::npos;
}
std::optional<SessionCredentials> unpack(const char* data, std::size_t size) {
    if (!data || size > 4096) return std::nullopt;
    const std::string value(data, size);
    const auto split = value.find('\n');
    if (split == std::string::npos) return std::nullopt;
    SessionCredentials session{value.substr(0, split), value.substr(split + 1)};
    if (!valid_field(session.username, 256) || !valid_field(session.token, 2048)) return std::nullopt;
    return session;
}
class NativeSessionStore final : public SessionStore {
public:
    explicit NativeSessionStore(std::string target) : target_(std::move(target)) {}
#ifdef __APPLE__
    std::optional<SessionCredentials> load() override {
        auto query = attributes();
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFTypeRef result = nullptr;
        const auto status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        if (status != errSecSuccess || !result) return std::nullopt;
        std::optional<SessionCredentials> session;
        if (CFGetTypeID(result) == CFDataGetTypeID()) {
            const auto data = static_cast<CFDataRef>(result);
            session = unpack(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), CFDataGetLength(data));
        }
        CFRelease(result);
        return session;
    }
    bool save(const SessionCredentials& session) override {
        if (!valid_field(session.username, 256) || !valid_field(session.token, 2048)) return false;
        auto value = session.username + "\n" + session.token;
        auto data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(value.data()), value.size());
        auto query = attributes();
        auto update = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(update, kSecValueData, data);
        auto status = SecItemUpdate(query, update);
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query, kSecValueData, data);
            status = SecItemAdd(query, nullptr);
        }
        CFRelease(update); CFRelease(query); CFRelease(data);
        std::fill(value.begin(), value.end(), '\0');
        return status == errSecSuccess;
    }
    bool clear() override {
        auto query = attributes();
        const auto status = SecItemDelete(query);
        CFRelease(query);
        return status == errSecSuccess || status == errSecItemNotFound;
    }
private:
    CFMutableDictionaryRef attributes() const {
        auto query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        auto target = CFStringCreateWithBytes(kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(target_.data()), target_.size(), kCFStringEncodingUTF8, false);
        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, target);
        CFDictionarySetValue(query, kSecAttrAccount, CFSTR("RetroAchievements"));
        CFRelease(target);
        return query;
    }
#else
    std::optional<SessionCredentials> load() override {
        GError* error = nullptr;
        char* value = secret_password_lookup_sync(schema(), nullptr, &error, "target", target_.c_str(), nullptr);
        auto session = value ? unpack(value, std::strlen(value)) : std::nullopt;
        if (value) secret_password_free(value);
        if (error) g_error_free(error);
        return session;
    }
    bool save(const SessionCredentials& session) override {
        if (!valid_field(session.username, 256) || !valid_field(session.token, 2048)) return false;
        auto value = session.username + "\n" + session.token;
        GError* error = nullptr;
        const bool ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT,
            "RavenEmu - RetroAchievements", value.c_str(), nullptr, &error, "target", target_.c_str(), nullptr);
        std::fill(value.begin(), value.end(), '\0');
        if (error) g_error_free(error);
        return ok;
    }
    bool clear() override {
        GError* error = nullptr;
        secret_password_clear_sync(schema(), nullptr, &error, "target", target_.c_str(), nullptr);
        // libsecret returns false for an already absent item; this is successful logout.
        const bool ok = !error;
        if (error) g_error_free(error);
        return ok;
    }
private:
    static const SecretSchema* schema() {
        static const SecretSchema value = {"org.ravenemu.RetroAchievements", SECRET_SCHEMA_NONE,
            {{"target", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
            0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        return &value;
    }
#endif
    std::string target_;
};
}
std::shared_ptr<SessionStore> make_session_store(const std::string& target) {
    if (!valid_field(target, 256)) return nullptr;
#ifdef __APPLE__
    auto text = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(target.data()), target.size(), kCFStringEncodingUTF8, false);
    if (!text) return nullptr;
    CFRelease(text);
#else
    if (!g_utf8_validate(target.c_str(), -1, nullptr)) return nullptr;
#endif
    return std::make_shared<NativeSessionStore>(target);
}
}
#else
namespace ravenemudsk_ra {
std::shared_ptr<SessionStore> make_session_store(const std::string&) { return nullptr; }
}
#endif
