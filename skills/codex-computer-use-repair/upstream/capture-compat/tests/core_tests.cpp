#include "../src/compat.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace capture_compat;
namespace {
void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
std::atomic<int> destroyed = 0;
class FakeSession final : public capture::IGraphicsCaptureSession, public capture::IGraphicsCaptureSession3 {
    std::atomic<ULONG> refs_{1};
public:
    HRESULT border_result;
    boolean border = true;
    int setter_calls = 0;
    explicit FakeSession(HRESULT result) : border_result(result) {}
    ~FakeSession() { ++destroyed; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) ||
            iid == __uuidof(capture::IGraphicsCaptureSession))
            *result = static_cast<capture::IGraphicsCaptureSession*>(this);
        else if (iid == __uuidof(capture::IGraphicsCaptureSession3)) {
            if (FAILED(border_result)) return border_result;
            *result = static_cast<capture::IGraphicsCaptureSession3*>(this);
        } else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** values) override {
        if (!count || !values) return E_POINTER;
        *values = nullptr; *count = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* result) override {
        return WindowsCreateString(L"FakeSession", 11, result);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* result) override {
        if (!result) return E_POINTER; *result = BaseTrust; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartCapture() override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_IsBorderRequired(boolean* result) override {
        if (!result) return E_POINTER; *result = border; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsBorderRequired(boolean value) override {
        border = value; ++setter_calls; return S_OK;
    }
};

// Force the same ABI vtable dispatch used by the Rust Windows projection.
__declspec(noinline) HRESULT Query(capture::IGraphicsCaptureSession* session, REFIID iid, void** out) {
    auto table = *reinterpret_cast<void***>(session);
    using Fn = HRESULT(STDMETHODCALLTYPE*)(void*, REFIID, void**);
    return reinterpret_cast<Fn>(table[0])(session, iid, out);
}
}

int main() {
    auto missing = new FakeSession(E_NOINTERFACE);
    auto base = static_cast<capture::IGraphicsCaptureSession*>(missing);
    Check(HookSession(base), "install session hook");
    Check(HookSession(base), "idempotent installation");
    capture::IGraphicsCaptureSession3* border = nullptr;
    Check(Query(base, __uuidof(capture::IGraphicsCaptureSession3), reinterpret_cast<void**>(&border)) == S_OK,
          "missing border interface supplied");
    Check(border->put_IsBorderRequired(false) == S_OK, "optional false setter succeeds");
    Check(border->put_IsBorderRequired(true) == S_OK, "optional true setter succeeds");
    boolean is_required = false;
    Check(border->get_IsBorderRequired(&is_required) == S_OK && is_required, "default border stays on");
    Check(missing->setter_calls == 0, "no unsupported OS setter called");
    Check(border->get_IsBorderRequired(nullptr) == E_POINTER, "null getter rejected");
    Check(Query(base, __uuidof(capture::IGraphicsCaptureSession3), nullptr) == E_POINTER, "null QI rejected");
    IUnknown* identity = nullptr;
    Check(border->QueryInterface(IID_PPV_ARGS(&identity)) == S_OK && identity == static_cast<IUnknown*>(base),
          "original COM IUnknown identity retained");
    identity->Release();
    void* other = nullptr;
    const GUID unrelated = {0x1a2b3c4d, 0x1111, 0x2222, {0x88,0x11,0x22,0x33,0x44,0x55,0x66,0x77}};
    Check(Query(base, unrelated, &other) == E_NOINTERFACE && !other, "unrelated missing IID unchanged");
    Check(base->StartCapture() == S_FALSE, "other method result unchanged");
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) threads.emplace_back([base] {
        for (int i = 0; i < 200; ++i) {
            capture::IGraphicsCaptureSession3* p = nullptr;
            Check(Query(base, __uuidof(capture::IGraphicsCaptureSession3), reinterpret_cast<void**>(&p)) == S_OK,
                  "concurrent QI");
            p->Release();
        }
    });
    for (auto& thread : threads) thread.join();
    base->Release();
    Check(destroyed == 0, "fallback keeps owner alive");
    border->Release();
    Check(destroyed == 1, "owner released with fallback");

    for (HRESULT preserved : {E_ACCESSDENIED, E_FAIL}) {
        auto item = new FakeSession(preserved);
        auto session = static_cast<capture::IGraphicsCaptureSession*>(item);
        border = nullptr;
        Check(Query(session, __uuidof(capture::IGraphicsCaptureSession3), reinterpret_cast<void**>(&border)) == preserved,
              "non-compatibility failures preserved");
        Check(!border, "failed query does not supply fake interface");
        session->Release();
    }
    auto supported = new FakeSession(S_OK);
    auto session = static_cast<capture::IGraphicsCaptureSession*>(supported);
    border = nullptr;
    Check(Query(session, __uuidof(capture::IGraphicsCaptureSession3), reinterpret_cast<void**>(&border)) == S_OK &&
          border == static_cast<capture::IGraphicsCaptureSession3*>(supported), "supported native interface unchanged");
    Check(border->put_IsBorderRequired(false) == S_OK && supported->setter_calls == 1 && !supported->border,
          "native setter delegated");
    border->Release();
    session->Release();
    Check(destroyed == 4, "all test owners destroyed exactly once");
    Status status{};
    GetStatus(status);
    Check(status.hook_errors == 0, "no hook errors");
    std::printf("PASS: missing/native/error QI, identity, lifetime, nulls, concurrency; fallbacks=%ld\n",
                status.border_interfaces);
}
