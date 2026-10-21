// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/user_agent.hpp>
#include <ravenemu/bridge.h>
#include <rc_client.h>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#else
#include <sys/utsname.h>
#endif
#ifndef RAVENEMUDSK_VERSION
#error "RAVENEMUDSK_VERSION comes from project(VERSION) in CMakeLists.txt"
#endif
namespace ravenemudsk_ra {
namespace {
std::string os_version() {
#if defined(_WIN32)
    // GetVersionEx reports the version the manifest targets; ntdll reports the real one.
    using RtlGetVersion = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    const auto get = reinterpret_cast<RtlGetVersion>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
    if (!get || get(&info) != 0) return "Windows";
    return "Windows " + std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." +
           std::to_string(info.dwBuildNumber);
#elif defined(__APPLE__)
    char version[32]{};
    size_t size = sizeof(version) - 1;
    if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) != 0) return "macOS";
    return std::string("macOS ") + version;
#else
    utsname name{};
    if (uname(&name) != 0) return "Linux";
    // "6.8.0-45-generic": the kernel version, without the distribution's build suffix.
    std::string release = name.release;
    release = release.substr(0, release.find('-'));
    return std::string(name.sysname) + " " + release;
#endif
}
const char* architecture() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "";
#endif
}
} // namespace
std::string system_description() {
    std::string text = os_version();
    if (*architecture()) text += std::string("; ") + architecture();
    // Header-safe: printable ASCII only, without the parentheses around it.
    for (auto& c : text)
        if (c < 0x20 || c > 0x7e || c == '(' || c == ')') c = ' ';
    return text;
}
std::string user_agent() {
    static const std::string agent = [] {
        char sdk[64]{};
        rc_client_get_user_agent_clause(nullptr, sdk, sizeof(sdk));
        return std::string("RavenEmu/") + RAVENEMUDSK_VERSION + " (" + system_description() + ") RavenCore/" +
               ravenemu_version() + " " + sdk;
    }();
    return agent;
}
} // namespace ravenemudsk_ra
