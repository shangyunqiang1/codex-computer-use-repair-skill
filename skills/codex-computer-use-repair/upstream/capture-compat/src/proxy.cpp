#include "compat.h"
#include <cwchar>

namespace {
INIT_ONCE version_once = INIT_ONCE_STATIC_INIT;
HMODULE real_version = nullptr;
constexpr const char* export_names[] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",
    "GetFileVersionInfoExW", "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW", "GetFileVersionInfoW",
    "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
    "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"
};
FARPROC exports[_countof(export_names)]{};

BOOL CALLBACK LoadSystemVersion(PINIT_ONCE, PVOID, PVOID*) noexcept {
    wchar_t path[MAX_PATH + 32]{};
    UINT size = GetSystemDirectoryW(path, MAX_PATH);
    if (!size || size >= MAX_PATH) return FALSE;
    wcscat_s(path, L"\\version.dll");
    real_version = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!real_version) return FALSE;
    for (size_t i = 0; i < _countof(exports); ++i)
        exports[i] = GetProcAddress(real_version, export_names[i]);
    return TRUE;
}
}

extern "C" FARPROC ResolveVersionExport(unsigned index) noexcept {
    if (!InitOnceExecuteOnce(&version_once, LoadSystemVersion, nullptr, nullptr) ||
        index >= _countof(exports) || !exports[index]) {
        // Never guess an undocumented export's signature or return success.
        RaiseFailFastException(nullptr, nullptr, 0);
        return nullptr;
    }
    return exports[index];
}

extern "C" BOOL WINAPI CodexCaptureCompatGetStatus(capture_compat::Status* status, DWORD size) noexcept {
    if (!status || size != sizeof(*status)) return FALSE;
    capture_compat::GetStatus(*status);
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // /MT uses CRT thread-local state; leave thread notifications enabled.
        // Keep this stack frame small: DllMain also runs on graphics-driver
        // worker threads, which may reserve very small stacks.
        wchar_t path[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
        if (!length || length >= _countof(path)) return TRUE;
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        if (_wcsicmp(name, L"codex-computer-use.exe") == 0 || _wcsicmp(name, L"compat_probe.exe") == 0)
            capture_compat::Initialize(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
