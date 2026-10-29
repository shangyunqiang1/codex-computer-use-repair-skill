#pragma once
#include <windows.h>
#include <roapi.h>
#include <windows.graphics.capture.h>

namespace capture_compat {
namespace capture = ABI::Windows::Graphics::Capture;

struct Status {
    DWORD size;
    LONG iat_hooks;
    LONG factory_calls;
    LONG pool_hooks;
    LONG session_hooks;
    LONG border_interfaces;
    LONG border_noops;
    LONG hook_errors;
    LONG live_handlers;
    LONG dispatched_calls;
    LONG dispatch_errors;
};

// The DLL and patched system modules must remain loaded until process exit.
bool Initialize(HMODULE executable) noexcept;
bool HookSession(capture::IGraphicsCaptureSession* session) noexcept;
void GetStatus(Status& result) noexcept;
}
