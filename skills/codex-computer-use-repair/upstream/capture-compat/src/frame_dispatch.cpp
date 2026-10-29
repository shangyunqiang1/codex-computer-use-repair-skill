#include "frame_dispatch.h"
#include <atomic>
#include <new>

namespace capture_compat {
namespace {
std::atomic<LONG> live_handlers{0}, dispatched_calls{0}, dispatch_errors{0};

class DeferredHandler final : public FrameHandler, public IAgileObject {
    std::atomic<ULONG> refs_{1};
    std::atomic<bool> cancelled_{false}, busy_{false};
    FrameHandler* original_;
    struct Work {
        DeferredHandler* owner;
        capture::IDirect3D11CaptureFramePool* sender;
        IInspectable* args;
    };
    static void Dispose(Work* work) noexcept {
        if (work->args) work->args->Release();
        if (work->sender) work->sender->Release();
        work->owner->busy_ = false;
        work->owner->Release();
        delete work;
    }
    static void CALLBACK Run(PTP_CALLBACK_INSTANCE instance, void* context) noexcept {
        CallbackMayRunLong(instance);
        auto work = static_cast<Work*>(context);
        HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
        if (SUCCEEDED(initialized)) {
            // Suppress queued calls after cancellation. An already running call
            // retains its own references until it finishes.
            if (!work->owner->cancelled_.load()) {
                ++dispatched_calls;
                if (FAILED(work->owner->original_->Invoke(work->sender, work->args))) ++dispatch_errors;
            }
        } else ++dispatch_errors;
        Dispose(work);
        if (SUCCEEDED(initialized)) RoUninitialize();
    }
public:
    explicit DeferredHandler(FrameHandler* original) noexcept : original_(original) {
        original_->AddRef(); ++live_handlers;
    }
    ~DeferredHandler() { original_->Release(); --live_handlers; }
    void Cancel() noexcept { cancelled_ = true; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(FrameHandler))
            *result = static_cast<FrameHandler*>(this);
        else if (iid == __uuidof(IAgileObject)) *result = static_cast<IAgileObject*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = --refs_; if (!count) delete this; return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(capture::IDirect3D11CaptureFramePool* sender, IInspectable* args) override {
        if (cancelled_.load()) return S_OK;
        // WGC currently supplies null args. Preserve native delivery if a
        // future implementation supplies apartment-bound event arguments.
        if (args) {
            IAgileObject* agile = nullptr;
            HRESULT hr = args->QueryInterface(IID_PPV_ARGS(&agile));
            if (FAILED(hr)) return original_->Invoke(sender, args);
            agile->Release();
        }
        bool expected = false;
        // Availability notifications can be coalesced. Bound work to one
        // queued/running callback per event subscription.
        if (!busy_.compare_exchange_strong(expected, true)) return S_OK;
        auto work = new (std::nothrow) Work{this, sender, args};
        if (!work) { busy_ = false; return E_OUTOFMEMORY; }
        AddRef();
        if (sender) sender->AddRef();
        if (args) args->AddRef();
        if (!TrySubmitThreadpoolCallback(&Run, work, nullptr)) {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            Dispose(work); ++dispatch_errors; return hr;
        }
        return S_OK;
    }
};
}

HRESULT MakeDeferredHandler(FrameHandler* original, FrameHandler** result) noexcept {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!original) return E_POINTER;
    IAgileObject* agile = nullptr;
    HRESULT hr = original->QueryInterface(IID_PPV_ARGS(&agile));
    if (FAILED(hr)) return hr;
    agile->Release();
    auto handler = new (std::nothrow) DeferredHandler(original);
    if (!handler) return E_OUTOFMEMORY;
    *result = handler; return S_OK;
}

void CancelDeferredHandler(FrameHandler* handler) noexcept {
    static_cast<DeferredHandler*>(handler)->Cancel();
}

void GetDispatchStatus(LONG& live, LONG& calls, LONG& errors) noexcept {
    live = live_handlers.load(); calls = dispatched_calls.load(); errors = dispatch_errors.load();
}
}
