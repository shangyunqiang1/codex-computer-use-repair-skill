#include "compat.h"
#include "frame_dispatch.h"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <new>
#include <cstdio>

namespace capture_compat {
namespace {
namespace dx = ABI::Windows::Graphics::DirectX;
using Device = dx::Direct3D11::IDirect3DDevice;
using Size = ABI::Windows::Graphics::SizeInt32;
using FactoryFn = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
using CreatePoolFn = HRESULT(STDMETHODCALLTYPE*)(void*, Device*, dx::DirectXPixelFormat,
                                                INT32, Size, capture::IDirect3D11CaptureFramePool**);
using CreateSessionFn = HRESULT(STDMETHODCALLTYPE*)(void*, capture::IGraphicsCaptureItem*,
                                                   capture::IGraphicsCaptureSession**);
using QueryFn = HRESULT(STDMETHODCALLTYPE*)(void*, REFIID, void**);
using AddFrameFn = HRESULT(STDMETHODCALLTYPE*)(void*, FrameHandler*, EventRegistrationToken*);
using InvokeFn = HRESULT(STDMETHODCALLTYPE*)(void*, capture::IDirect3D11CaptureFramePool*, IInspectable*);
using NextFrameFn = HRESULT(STDMETHODCALLTYPE*)(void*, capture::IDirect3D11CaptureFrame**);
using StartFn = HRESULT(STDMETHODCALLTYPE*)(void*);

void Trace(const char* operation, void* object, HRESULT hr = S_OK) noexcept {
#ifdef CAPTURE_COMPAT_TRACE
    wchar_t path[MAX_PATH]{};
    DWORD length = GetTempPathW(_countof(path), path);
    if (!length || length >= _countof(path)) return;
    swprintf_s(path + length, _countof(path) - length, L"codex-capture-compat-%lu.log", GetCurrentProcessId());
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[192]{};
    int count = sprintf_s(line, "%llu tid=%lu %s object=%p hr=%08lX\r\n",
        GetTickCount64(), GetCurrentThreadId(), operation, object, static_cast<unsigned long>(hr));
    DWORD written = 0;
    if (count > 0) WriteFile(file, line, static_cast<DWORD>(count), &written, nullptr);
    CloseHandle(file);
#else
    (void)operation; (void)object; (void)hr;
#endif
}

struct SlotRecord { void** slot; void* original; };
SRWLOCK slots_lock = SRWLOCK_INIT;
SlotRecord slots[64]{};
size_t slot_count = 0;
FactoryFn original_factory = nullptr;
volatile LONG iat_hooks = 0, factory_calls = 0, pool_hooks = 0, session_hooks = 0;
volatile LONG border_interfaces = 0, border_noops = 0, hook_errors = 0;
volatile LONG deferred_capture = 0;

struct Subscription {
    void* pool;
    EventRegistrationToken token;
    FrameHandler* handler;
    Subscription* next;
};
SRWLOCK subscriptions_lock = SRWLOCK_INIT;
Subscription* subscriptions = nullptr;

void CancelSubscriptions(void* pool, const EventRegistrationToken* token = nullptr, bool unsubscribe = false) noexcept {
    Subscription* removed = nullptr;
    AcquireSRWLockExclusive(&subscriptions_lock);
    auto link = &subscriptions;
    while (*link) {
        auto item = *link;
        if (item->pool == pool && (!token || item->token.value == token->value)) {
            *link = item->next; item->next = removed; removed = item;
            CancelDeferredHandler(item->handler);
        } else link = &item->next;
    }
    ReleaseSRWLockExclusive(&subscriptions_lock);
    // Release outside the registry lock: COM destructors may reenter.
    while (removed) {
        auto item = removed; removed = item->next;
        if (unsubscribe) static_cast<capture::IDirect3D11CaptureFramePool*>(pool)->remove_FrameArrived(item->token);
        item->handler->Release(); delete item;
    }
}

HRESULT STDMETHODCALLTYPE AddFrame(void*, FrameHandler*, EventRegistrationToken*) noexcept;
HRESULT STDMETHODCALLTYPE RemoveFrame(void*, EventRegistrationToken) noexcept;
HRESULT STDMETHODCALLTYPE ClosePool(void*) noexcept;

void Error() noexcept {
    InterlockedIncrement(&hook_errors);
    OutputDebugStringW(L"[CodexCaptureCompat] Hook installation failed; original operation preserved.\n");
}

void** Slot(void* object, size_t index) noexcept {
    return *reinterpret_cast<void***>(object) + index;
}

void* Original(void* object, size_t index) noexcept {
    void** target = Slot(object, index);
    void* result = nullptr;
    AcquireSRWLockShared(&slots_lock);
    for (size_t i = 0; i < slot_count; ++i) {
        if (slots[i].slot == target) { result = slots[i].original; break; }
    }
    ReleaseSRWLockShared(&slots_lock);
    return result;
}

bool PatchSlot(void* object, size_t index, void* replacement) noexcept {
    void** target = Slot(object, index);
    AcquireSRWLockExclusive(&slots_lock);
    for (size_t i = 0; i < slot_count; ++i) {
        if (slots[i].slot == target) {
            bool ok = *target == replacement;
            ReleaseSRWLockExclusive(&slots_lock);
            if (!ok) Error();
            return ok;
        }
    }
    if (slot_count == _countof(slots)) {
        ReleaseSRWLockExclusive(&slots_lock);
        Error();
        return false;
    }
    // A factory can be released while its shared vtable remains patched.
    // Pin both modules, so neither the vtable nor our hook becomes dangling.
    HMODULE ignored = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(*target), &ignored) ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(replacement), &ignored)) {
        ReleaseSRWLockExclusive(&slots_lock);
        Error();
        return false;
    }
    DWORD protection = 0;
    if (!VirtualProtect(target, sizeof(void*), PAGE_READWRITE, &protection)) {
        ReleaseSRWLockExclusive(&slots_lock);
        Error();
        return false;
    }
    slots[slot_count++] = { target, *target };
    InterlockedExchangePointer(target, replacement);
    DWORD ignored_protection = 0;
    bool restored = !!VirtualProtect(target, sizeof(void*), protection, &ignored_protection);
    ReleaseSRWLockExclusive(&slots_lock);
    if (!restored) Error();
    return true;
}

// Only supplied if the actual session returns E_NOINTERFACE for Session3.
// The OS continues showing its default capture border. No borderless access
// request, capture permission, input check, or screenshot state is changed.
class BorderFallback final : public capture::IGraphicsCaptureSession3 {
    std::atomic<ULONG> references_{1};
    capture::IGraphicsCaptureSession* owner_;
public:
    explicit BorderFallback(capture::IGraphicsCaptureSession* owner) noexcept : owner_(owner) {
        owner_->AddRef();
    }
    ~BorderFallback() { owner_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(capture::IGraphicsCaptureSession3)) {
            *result = static_cast<capture::IGraphicsCaptureSession3*>(this);
            AddRef();
            return S_OK;
        }
        // In particular, IUnknown identity belongs to the real session.
        return owner_->QueryInterface(iid, result);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** values) override {
        return owner_->GetIids(count, values);
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
        return owner_->GetRuntimeClassName(value);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override {
        return owner_->GetTrustLevel(value);
    }
    HRESULT STDMETHODCALLTYPE get_IsBorderRequired(boolean* value) override {
        if (!value) return E_POINTER;
        *value = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsBorderRequired(boolean) override {
        InterlockedIncrement(&border_noops);
        OutputDebugStringW(L"[CodexCaptureCompat] Optional border setter skipped; OS default border retained.\n");
        return S_OK;
    }
};

HRESULT STDMETHODCALLTYPE SessionQuery(void* self, REFIID iid, void** result) noexcept {
    auto original = reinterpret_cast<QueryFn>(Original(self, 0));
    if (!original) { Error(); return E_UNEXPECTED; }
    HRESULT hr = original(self, iid, result);
    if (hr != E_NOINTERFACE || iid != __uuidof(capture::IGraphicsCaptureSession3) || !result)
        return hr;
    *result = nullptr;
    auto fallback = new (std::nothrow) BorderFallback(static_cast<capture::IGraphicsCaptureSession*>(self));
    if (!fallback) return E_OUTOFMEMORY;
    *result = static_cast<capture::IGraphicsCaptureSession3*>(fallback);
    InterlockedIncrement(&border_interfaces);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CreateSession(void* self, capture::IGraphicsCaptureItem* item,
                                       capture::IGraphicsCaptureSession** result) noexcept {
    auto original = reinterpret_cast<CreateSessionFn>(Original(self, 10));
    if (!original) { Error(); return E_UNEXPECTED; }
    HRESULT hr = original(self, item, result);
    if (SUCCEEDED(hr) && result && *result) {
        // Always query the native QI, including when its shared vtable was
        // already patched by an earlier capture session.
        auto query = reinterpret_cast<QueryFn>(Original(*result, 0));
        if (!query) query = reinterpret_cast<QueryFn>(*Slot(*result, 0));
        capture::IGraphicsCaptureSession3* native = nullptr;
        HRESULT border_hr = query(*result, __uuidof(capture::IGraphicsCaptureSession3), reinterpret_cast<void**>(&native));
        if (native) native->Release();
        if (border_hr == E_NOINTERFACE) {
            InterlockedExchange(&deferred_capture, 1);
            PatchSlot(self, 8, reinterpret_cast<void*>(&AddFrame));
            PatchSlot(self, 9, reinterpret_cast<void*>(&RemoveFrame));
            ABI::Windows::Foundation::IClosable* closable = nullptr;
            if (SUCCEEDED(static_cast<IUnknown*>(self)->QueryInterface(IID_PPV_ARGS(&closable)))) {
                PatchSlot(closable, 6, reinterpret_cast<void*>(&ClosePool));
                closable->Release();
            }
        }
        HookSession(*result);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE FrameInvoke(void* self, capture::IDirect3D11CaptureFramePool* sender,
                                      IInspectable* args) noexcept {
    auto original = reinterpret_cast<InvokeFn>(Original(self, 3));
    if (!original) return E_UNEXPECTED;
    Trace("Invoke.enter", self);
    HRESULT hr = original(self, sender, args);
    Trace("Invoke.leave", self, hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE AddFrame(void* self, FrameHandler* handler, EventRegistrationToken* token) noexcept {
    auto original = reinterpret_cast<AddFrameFn>(Original(self, 8));
    if (!original) return E_UNEXPECTED;
    Trace("FrameArrived.add.enter", self);
#ifdef CAPTURE_COMPAT_TRACE
    if (handler) PatchSlot(handler, 3, reinterpret_cast<void*>(&FrameInvoke));
#endif
    ABI::Windows::System::IDispatcherQueue* queue = nullptr;
    auto pool = static_cast<capture::IDirect3D11CaptureFramePool*>(self);
    HRESULT queue_hr = pool->get_DispatcherQueue(&queue);
    bool free_threaded = SUCCEEDED(queue_hr) && !queue;
    if (queue) queue->Release();
    if (handler && token && free_threaded && InterlockedCompareExchange(&deferred_capture, 0, 0)) {
        FrameHandler* deferred = nullptr;
        HRESULT wrap_hr = MakeDeferredHandler(handler, &deferred);
        if (SUCCEEDED(wrap_hr)) {
            auto entry = new (std::nothrow) Subscription{self, {}, deferred, nullptr};
            if (!entry) { deferred->Release(); return E_OUTOFMEMORY; }
            HRESULT hr = original(self, deferred, token);
            if (FAILED(hr)) { deferred->Release(); delete entry; return hr; }
            entry->token = *token;
            AcquireSRWLockExclusive(&subscriptions_lock);
            entry->next = subscriptions; subscriptions = entry;
            ReleaseSRWLockExclusive(&subscriptions_lock);
            Trace("FrameArrived.add.deferred", self, hr);
            return hr;
        }
        Trace("FrameArrived.add.native_fallback", self, wrap_hr);
    }
    HRESULT hr = original(self, handler, token);
    Trace("FrameArrived.add.leave", self, hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE NextFrame(void* self, capture::IDirect3D11CaptureFrame** frame) noexcept {
    auto original = reinterpret_cast<NextFrameFn>(Original(self, 7));
    if (!original) return E_UNEXPECTED;
    Trace("TryGetNextFrame.enter", self);
    HRESULT hr = original(self, frame);
    Trace(SUCCEEDED(hr) && frame && *frame ? "TryGetNextFrame.frame" : "TryGetNextFrame.empty", self, hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE RemoveFrame(void* self, EventRegistrationToken token) noexcept {
    using Fn = HRESULT(STDMETHODCALLTYPE*)(void*, EventRegistrationToken);
    auto original = reinterpret_cast<Fn>(Original(self, 9));
    if (!original) return E_UNEXPECTED;
    HRESULT hr = original(self, token);
    if (SUCCEEDED(hr)) CancelSubscriptions(self, &token);
    return hr;
}

HRESULT STDMETHODCALLTYPE ClosePool(void* self) noexcept {
    auto original = reinterpret_cast<StartFn>(Original(self, 6));
    if (!original) return E_UNEXPECTED;
    capture::IDirect3D11CaptureFramePool* pool = nullptr;
    if (SUCCEEDED(static_cast<IUnknown*>(self)->QueryInterface(IID_PPV_ARGS(&pool)))) {
        CancelSubscriptions(pool, nullptr, true);
        pool->Release();
    }
    return original(self);
}

HRESULT STDMETHODCALLTYPE StartCapture(void* self) noexcept {
    auto original = reinterpret_cast<StartFn>(Original(self, 6));
    if (!original) return E_UNEXPECTED;
    APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
    HRESULT apartment_hr = CoGetApartmentType(&apartment, &qualifier);
    Trace(SUCCEEDED(apartment_hr) && apartment == APTTYPE_MTA ? "StartCapture.MTA" : "StartCapture.nonMTA", self, apartment_hr);
    HRESULT hr = original(self);
    Trace("StartCapture.leave", self, hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE CreatePool(void* self, Device* device, dx::DirectXPixelFormat format,
                                    INT32 buffers, Size size,
                                    capture::IDirect3D11CaptureFramePool** result) noexcept {
    auto original = reinterpret_cast<CreatePoolFn>(Original(self, 6));
    if (!original) { Error(); return E_UNEXPECTED; }
    HRESULT hr = original(self, device, format, buffers, size, result);
    if (SUCCEEDED(hr) && result && *result &&
        PatchSlot(*result, 10, reinterpret_cast<void*>(&CreateSession))) {
        InterlockedIncrement(&pool_hooks);
#ifdef CAPTURE_COMPAT_TRACE
        PatchSlot(*result, 8, reinterpret_cast<void*>(&AddFrame));
        PatchSlot(*result, 7, reinterpret_cast<void*>(&NextFrame));
#endif
    }
    return hr;
}

HRESULT WINAPI ActivationFactory(HSTRING class_id, REFIID iid, void** result) noexcept {
    InterlockedIncrement(&factory_calls);
    HRESULT hr = original_factory(class_id, iid, result);
    if (FAILED(hr) || !result || !*result) return hr;
    UINT32 length = 0;
    const wchar_t* name = WindowsGetStringRawBuffer(class_id, &length);
    constexpr wchar_t pool_name[] = L"Windows.Graphics.Capture.Direct3D11CaptureFramePool";
    if (length == _countof(pool_name) - 1 && wmemcmp(name, pool_name, length) == 0) {
        if (iid == __uuidof(capture::IDirect3D11CaptureFramePoolStatics) ||
            iid == __uuidof(capture::IDirect3D11CaptureFramePoolStatics2)) {
            PatchSlot(*result, 6, reinterpret_cast<void*>(&CreatePool));
        } else {
            // Also cover clients requesting an activation factory / IUnknown
            // before QueryInterface-ing to one of the two statics interfaces.
            auto unknown = static_cast<IUnknown*>(*result);
            capture::IDirect3D11CaptureFramePoolStatics* s1 = nullptr;
            capture::IDirect3D11CaptureFramePoolStatics2* s2 = nullptr;
            if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&s1)))) {
                PatchSlot(s1, 6, reinterpret_cast<void*>(&CreatePool));
                s1->Release();
            }
            if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&s2)))) {
                PatchSlot(s2, 6, reinterpret_cast<void*>(&CreatePool));
                s2->Release();
            }
        }
    }
    return hr;
}
}

bool HookSession(capture::IGraphicsCaptureSession* session) noexcept {
    if (!session) return false;
    if (!PatchSlot(session, 0, reinterpret_cast<void*>(&SessionQuery))) return false;
#ifdef CAPTURE_COMPAT_TRACE
    PatchSlot(session, 6, reinterpret_cast<void*>(&StartCapture));
#endif
    InterlockedIncrement(&session_hooks);
    return true;
}

bool Initialize(HMODULE executable) noexcept {
    // Runs under loader lock: no COM, LoadLibrary, workers, heap allocations,
    // or file logging here. Only the target EXE's already-resolved IAT changes.
    auto base = reinterpret_cast<BYTE*>(executable);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return false;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return false;
    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
        auto addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto entry = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(entry->Name, "RoGetActivationFactory") != 0) continue;
            auto target = reinterpret_cast<void**>(&addresses->u1.Function);
            DWORD protection = 0;
            if (!VirtualProtect(target, sizeof(void*), PAGE_READWRITE, &protection)) return false;
            original_factory = reinterpret_cast<FactoryFn>(*target);
            InterlockedExchangePointer(target, reinterpret_cast<void*>(&ActivationFactory));
            DWORD ignored = 0;
            if (!VirtualProtect(target, sizeof(void*), protection, &ignored)) InterlockedIncrement(&hook_errors);
            InterlockedIncrement(&iat_hooks);
            return true;
        }
    }
    return false;
}

void GetStatus(Status& result) noexcept {
    result = {sizeof(Status), InterlockedCompareExchange(&iat_hooks, 0, 0),
        InterlockedCompareExchange(&factory_calls, 0, 0), InterlockedCompareExchange(&pool_hooks, 0, 0),
        InterlockedCompareExchange(&session_hooks, 0, 0), InterlockedCompareExchange(&border_interfaces, 0, 0),
        InterlockedCompareExchange(&border_noops, 0, 0), InterlockedCompareExchange(&hook_errors, 0, 0)};
    GetDispatchStatus(result.live_handlers, result.dispatched_calls, result.dispatch_errors);
}
}
