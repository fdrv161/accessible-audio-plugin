#include "ScreenReaderAnnouncer.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <ole2.h>
 #include <oleauto.h>
 #include <oleacc.h>
 #include <UIAutomation.h>
 #include <atomic>
 #if JUCE_MSVC
  #pragma comment (lib, "ole32.lib")    // CoCreateInstance
  #pragma comment (lib, "oleaut32.lib") // SysAllocString, SysFreeString
  #pragma comment (lib, "user32.lib")   // windows, NotifyWinEvent
 #endif
#endif

namespace a11y
{
#if JUCE_WINDOWS
namespace
{
// Values from UIAutomationCore.h (Windows 10 1709+), declared here so the file builds
// with any Windows SDK and any _WIN32_WINNT setting.
constexpr int notificationKindOther  = 4; // NotificationKind_Other
constexpr int processingAll          = 2; // NotificationProcessing_All: queue after current speech
constexpr int processingMostRecent   = 3; // NotificationProcessing_MostRecent: replaces pending ones

// PROPID_ACC_NAME from oleacc.h, defined locally so no extra GUID library is needed.
constexpr GUID propIdAccName { 0x608d3df8, 0x8128, 0x4aa7, { 0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91 } };

using RaiseNotificationFn = HRESULT (WINAPI*) (IRawElementProviderSimple*, int, int, BSTR, BSTR);
using ReturnProviderFn    = LRESULT (WINAPI*) (HWND, WPARAM, LPARAM, IRawElementProviderSimple*);
using HostFromHwndFn      = HRESULT (WINAPI*) (HWND, IRawElementProviderSimple**);
using ClientsListeningFn  = BOOL    (WINAPI*) ();
using DisconnectFn        = HRESULT (WINAPI*) (IRawElementProviderSimple*);

// Loaded at run time, not linked: UiaRaiseNotificationEvent exists only on Windows 10
// 1709+, and on older systems the plugin must stay silent rather than fail to load.
struct UiaApi
{
    UiaApi()
    {
        if (auto* dll = ::LoadLibraryW (L"UIAutomationCore.dll"))
        {
            raise          = reinterpret_cast<RaiseNotificationFn> (::GetProcAddress (dll, "UiaRaiseNotificationEvent"));
            returnProvider = reinterpret_cast<ReturnProviderFn>    (::GetProcAddress (dll, "UiaReturnRawElementProvider"));
            hostFromHwnd   = reinterpret_cast<HostFromHwndFn>      (::GetProcAddress (dll, "UiaHostProviderFromHwnd"));
            listening      = reinterpret_cast<ClientsListeningFn>  (::GetProcAddress (dll, "UiaClientsAreListening"));
            disconnect     = reinterpret_cast<DisconnectFn>        (::GetProcAddress (dll, "UiaDisconnectProvider")); // optional
        }
    }

    bool usable() const { return raise != nullptr && returnProvider != nullptr && hostFromHwnd != nullptr && listening != nullptr; }

    RaiseNotificationFn raise = nullptr;
    ReturnProviderFn returnProvider = nullptr;
    HostFromHwndFn hostFromHwnd = nullptr;
    ClientsListeningFn listening = nullptr;
    DisconnectFn disconnect = nullptr;
};

const UiaApi& uia()
{
    static const UiaApi api;
    return api;
}

// The UIA element of the notification window. It is not a control, not content and not
// focusable, so screen readers never show it in object or keyboard navigation.
class NotifyProvider final : public IRawElementProviderSimple
{
public:
    explicit NotifyProvider (HWND windowToUse) : hwnd (windowToUse) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** out) override
    {
        if (out == nullptr)
            return E_POINTER;
        if (riid == __uuidof (IUnknown) || riid == __uuidof (IRawElementProviderSimple))
        {
            *out = static_cast<IRawElementProviderSimple*> (this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto remaining = --refCount;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions (ProviderOptions* result) override
    {
        if (result == nullptr)
            return E_POINTER;
        *result = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider (int, IUnknown** result) override
    {
        if (result == nullptr)
            return E_POINTER;
        *result = nullptr; // no patterns: this element only raises notifications
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue (int propertyId, VARIANT* result) override
    {
        if (result == nullptr)
            return E_POINTER;
        result->vt = VT_EMPTY;

        switch (propertyId)
        {
            case UIA_ControlTypePropertyId:
                result->vt = VT_I4;
                result->lVal = UIA_CustomControlTypeId;
                break;
            case UIA_NamePropertyId:
                result->vt = VT_BSTR;
                result->bstrVal = ::SysAllocString (L"Plugin announcements");
                break;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
            case UIA_IsKeyboardFocusablePropertyId:
                result->vt = VT_BOOL;
                result->boolVal = VARIANT_FALSE;
                break;
            default:
                break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider (IRawElementProviderSimple** result) override
    {
        if (result == nullptr)
            return E_POINTER;
        *result = nullptr;
        return uia().hostFromHwnd (hwnd, result);
    }

private:
    ~NotifyProvider() = default;

    HWND hwnd;
    std::atomic<ULONG> refCount { 1 };
};

LRESULT CALLBACK notifyWindowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* provider = reinterpret_cast<NotifyProvider*> (::GetWindowLongPtrW (hwnd, GWLP_USERDATA));
    const auto& api = uia();

    switch (message)
    {
        case WM_GETOBJECT:
            if (provider != nullptr && static_cast<LONG> (lParam) == UiaRootObjectId)
                return api.returnProvider (hwnd, wParam, lParam, provider);
            break;

        case WM_DESTROY:
            // Documented UIA rule: tell UIA the window's provider is going away.
            api.returnProvider (hwnd, 0, 0, nullptr);
            break;

        case WM_NCDESTROY:
            // The editor window was closed: the child window dies with it, so the provider
            // is released here and nothing else has to track its lifetime.
            if (provider != nullptr)
            {
                ::SetWindowLongPtrW (hwnd, GWLP_USERDATA, 0);
                if (api.disconnect != nullptr)
                    api.disconnect (provider);
                provider->Release();
            }
            break;

        default:
            break;
    }
    return ::DefWindowProcW (hwnd, message, wParam, lParam);
}

HINSTANCE moduleHandle()
{
    return static_cast<HINSTANCE> (juce::Process::getCurrentModuleInstanceHandle());
}

// The class name is unique for every loaded copy of the plugin DLL. A host may unload
// and reload the plugin; a class registered by the old copy would point into unloaded
// code, so it must never be reused. (It is not unregistered at unload on purpose:
// calling user32 during DLL detach is risky, and a leaked class name is harmless.)
struct WindowClass
{
    WindowClass()
        : name ("JuceA11yNotify_" + juce::String::toHexString ((juce::int64) (juce::pointer_sized_int) moduleHandle())
                + "_" + juce::String::toHexString (juce::Time::getHighResolutionTicks()))
    {
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof (wc);
        wc.lpfnWndProc = notifyWindowProc;
        wc.hInstance = moduleHandle();
        wc.lpszClassName = name.toWideCharPointer();
        atom = ::RegisterClassExW (&wc);
    }

    juce::String name;
    ATOM atom = 0;
};

const WindowClass& windowClass()
{
    static const WindowClass instance;
    return instance;
}

// One hidden 1x1 notification window per editor window, created on first use.
// It is hidden like in Surge XT. OSARA instead shows it (SW_SHOWNA, WS_EX_TRANSPARENT,
// never 0x0 in size); if a screen reader ever stays silent, try that variant.
NotifyProvider* providerForEditorWindow (HWND editorWindow)
{
    const auto& cls = windowClass();
    if (cls.atom == 0 || editorWindow == nullptr)
        return nullptr;

    HWND hwnd = ::FindWindowExW (editorWindow, nullptr, cls.name.toWideCharPointer(), nullptr);
    if (hwnd == nullptr)
    {
        hwnd = ::CreateWindowExW (0, cls.name.toWideCharPointer(), L"", WS_CHILD | WS_DISABLED,
                                  0, 0, 1, 1, editorWindow, nullptr, moduleHandle(), nullptr);
        if (hwnd == nullptr)
            return nullptr;
        ::SetWindowLongPtrW (hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (new NotifyProvider (hwnd)));
    }
    return reinterpret_cast<NotifyProvider*> (::GetWindowLongPtrW (hwnd, GWLP_USERDATA));
}

// JAWS and Dolphin load these DLLs into every process they watch. OSARA found that they
// ignore UIA notification events, so for them it uses the MSAA name trick below.
bool screenReaderIgnoresUiaNotifications()
{
    return ::GetModuleHandleW (L"jhook.dll") != nullptr || ::GetModuleHandleW (L"dolwinhk.dll") != nullptr;
}

IAccPropServices* createAccPropServices()
{
    IAccPropServices* services = nullptr;
    if (FAILED (::CoCreateInstance (__uuidof (CAccPropServices), nullptr, CLSCTX_INPROC_SERVER,
                                    __uuidof (IAccPropServices), reinterpret_cast<void**> (&services))))
        return nullptr;
    return services;
}

void clearMsaaName (HWND hwnd)
{
    if (hwnd == nullptr || ! ::IsWindow (hwnd))
        return;
    if (auto* services = createAccPropServices())
    {
        services->ClearHwndProps (hwnd, (DWORD) OBJID_CLIENT, CHILDID_SELF, &propIdAccName, 1);
        services->Release();
    }
}

// OSARA's fallback: give the focused window this text as its MSAA name for a moment and
// fire a name-change event; the screen reader reads the new name of the focus.
// UNVERIFIED in JUCE plugins (JUCE windows are UIA-native): test with JAWS.
bool announceViaMsaaName (const juce::String& text)
{
    static juce::String lastText; // message thread only
    static HWND lastWindow = nullptr;

    HWND focus = ::GetFocus();
    if (focus == nullptr)
        return false;

    auto services = createAccPropServices();
    if (services == nullptr)
        return false;

    if (lastWindow != nullptr && lastWindow != focus)
        clearMsaaName (lastWindow);

    // Clients may ignore a name change to the same text: make it different.
    const auto spoken = text == lastText ? text + " " : text;
    services->SetHwndPropStr (focus, (DWORD) OBJID_CLIENT, CHILDID_SELF, propIdAccName, spoken.toWideCharPointer());
    services->Release();
    ::NotifyWinEvent (EVENT_OBJECT_NAMECHANGE, focus, OBJID_CLIENT, CHILDID_SELF);

    lastText = spoken;
    lastWindow = focus;

    // Remove the override soon, or the window would later be read with this old message.
    juce::Timer::callAfterDelay (2000, [focus, spoken]
    {
        if (lastWindow == focus && lastText == spoken)
        {
            clearMsaaName (focus);
            lastWindow = nullptr;
        }
    });
    return true;
}
} // namespace
#endif

bool announce (juce::Component& componentInEditor, const juce::String& text, Speech mode)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (text.isEmpty())
        return false;

   #if JUCE_WINDOWS
    auto* peer = componentInEditor.getPeer();
    if (peer == nullptr)
        return false; // editor not on screen yet

    if (screenReaderIgnoresUiaNotifications())
        return announceViaMsaaName (text);

    const auto& api = uia();
    if (! api.usable() || ! api.listening())
        return false; // Windows older than 10 1709, or no screen reader running

    auto* provider = providerForEditorWindow (static_cast<HWND> (peer->getNativeHandle()));
    if (provider == nullptr)
        return false;

    auto* message  = ::SysAllocString (text.toWideCharPointer());
    auto* activity = ::SysAllocString (L"JucePluginAnnouncement");
    const auto result = api.raise (provider, notificationKindOther,
                                   mode == Speech::interrupt ? processingMostRecent : processingAll,
                                   message, activity);
    ::SysFreeString (message);
    ::SysFreeString (activity);
    return SUCCEEDED (result);

   #elif JUCE_MAC
    juce::ignoreUnused (componentInEditor);
    juce::AccessibilityHandler::postAnnouncement (text, mode == Speech::interrupt
                                                            ? juce::AccessibilityHandler::AnnouncementPriority::high
                                                            : juce::AccessibilityHandler::AnnouncementPriority::medium);
    return true;

   #else
    juce::ignoreUnused (componentInEditor, mode);
    return false; // JUCE has no screen reader support on Linux
   #endif
}
} // namespace a11y
