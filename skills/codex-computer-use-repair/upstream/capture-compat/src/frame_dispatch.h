#pragma once
#include "compat.h"

namespace capture_compat {
using FrameHandler = ABI::Windows::Foundation::ITypedEventHandler<capture::Direct3D11CaptureFramePool*, IInspectable*>;
// E_NOINTERFACE means the caller must retain native delegate delivery.
HRESULT MakeDeferredHandler(FrameHandler* original, FrameHandler** result) noexcept;
void CancelDeferredHandler(FrameHandler* handler) noexcept;
void GetDispatchStatus(LONG& live, LONG& calls, LONG& errors) noexcept;
}
