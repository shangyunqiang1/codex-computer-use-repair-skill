#include "../src/frame_dispatch.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace capture_compat;
void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Context {
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE unblock = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE destroyed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<int> calls{0};
    std::atomic<DWORD> thread{0};
    std::atomic<int> apartment{-1};
    HRESULT result = S_OK;
    ~Context() { CloseHandle(entered); CloseHandle(unblock); CloseHandle(destroyed); }
};
class Handler final : public FrameHandler, public IAgileObject {
    std::atomic<ULONG> refs_{1};
    Context& context_;
    bool agile_;
public:
    Handler(Context& context, bool agile) : context_(context), agile_(agile) {}
    ~Handler() { SetEvent(context_.destroyed); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(FrameHandler)) *result = static_cast<FrameHandler*>(this);
        else if (agile_ && iid == __uuidof(IAgileObject)) *result = static_cast<IAgileObject*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(capture::IDirect3D11CaptureFramePool*, IInspectable*) override {
        ++context_.calls; context_.thread = GetCurrentThreadId();
        APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
        if (SUCCEEDED(CoGetApartmentType(&type, &qualifier))) context_.apartment = type;
        SetEvent(context_.entered);
        Check(WaitForSingleObject(context_.unblock, 3000) == WAIT_OBJECT_0, "callback release deadline");
        return context_.result;
    }
};

int main() {
    FrameHandler* deferred = nullptr;
    Check(MakeDeferredHandler(nullptr, &deferred) == E_POINTER && !deferred, "null original rejected");
    {
        Context context;
        auto original = new Handler(context, false);
        Check(MakeDeferredHandler(original, &deferred) == E_NOINTERFACE && !deferred, "non-agile delegate preserved");
        original->Release();
        Check(WaitForSingleObject(context.destroyed, 0) == WAIT_OBJECT_0, "rejected handler not retained");
    }
    for (bool fail : {false, true}) {
        Context context;
        context.result = fail ? E_FAIL : S_OK;
        auto original = new Handler(context, true);
        Check(MakeDeferredHandler(original, &deferred) == S_OK, "agile wrapper created");
        original->Release();
        Check(deferred->Invoke(nullptr, nullptr) == S_OK, "event producer returns before consumer unblocks");
        Check(WaitForSingleObject(context.entered, 3000) == WAIT_OBJECT_0, "worker entered");
        Check(context.thread != GetCurrentThreadId() && context.apartment == APTTYPE_MTA, "separate MTA worker");
        for (int i = 0; i < 100; ++i) Check(deferred->Invoke(nullptr, nullptr) == S_OK, "coalesced notifications");
        Check(context.calls == 1, "at most one running callback");
        CancelDeferredHandler(deferred);
        Check(deferred->Invoke(nullptr, nullptr) == S_OK && context.calls == 1, "cancelled delegate suppresses calls");
        deferred->Release(); deferred = nullptr;
        Check(WaitForSingleObject(context.destroyed, 0) == WAIT_TIMEOUT, "in-flight work retains original");
        SetEvent(context.unblock);
        Check(WaitForSingleObject(context.destroyed, 3000) == WAIT_OBJECT_0, "worker releases original exactly once");
    }
    {
        Context context;
        auto original = new Handler(context, true);
        Check(MakeDeferredHandler(original, &deferred) == S_OK, "cancel-before-invoke setup");
        original->Release();
        CancelDeferredHandler(deferred);
        Check(deferred->Invoke(nullptr, nullptr) == S_OK && context.calls == 0, "cancel before queueing");
        deferred->Release();
    }
    LONG live = 0, calls = 0, errors = 0;
    GetDispatchStatus(live, calls, errors);
    // Destructor signals before the live counter decrement; allow its epilogue.
    for (int i = 0; live && i < 100; ++i) { Sleep(10); GetDispatchStatus(live, calls, errors); }
    Check(live == 0 && calls == 2 && errors == 1, "lifetime and async failure diagnostics");
    std::puts("PASS: asynchronous MTA delivery, bounded queue, cancellation, lifetime, non-agile fallback, failure diagnostics");
}
