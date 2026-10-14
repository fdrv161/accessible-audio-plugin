#include "ScreenReaderAnnouncer.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <oleauto.h>
 #include <UIAutomation.h>
 #if JUCE_MSVC
  #pragma comment (lib, "oleaut32.lib") // SysAllocString / SysFreeString
 #endif
#endif

namespace a11y
{
#if JUCE_WINDOWS
namespace
{
// Значения перечислений из UIAutomationCore.h (Windows 10 1709+). Объявлены здесь,
// чтобы не зависеть от версии Windows SDK и настройки _WIN32_WINNT.
constexpr int notificationKindOther          = 4; // NotificationKind_Other
constexpr int processingImportantAll         = 0; // NotificationProcessing_ImportantAll
constexpr int processingImportantMostRecent  = 1; // NotificationProcessing_ImportantMostRecent

using RaiseNotificationFn = HRESULT (WINAPI*) (IRawElementProviderSimple*, int, int, BSTR, BSTR);
using ClientsListeningFn  = BOOL (WINAPI*)();

struct UiaFunctions
{
    UiaFunctions()
    {
        // Загружаем во время работы, а не линкуем: на Windows без этой функции плагин
        // должен просто молчать, а не отказываться загружаться в DAW.
        if (auto* module = ::LoadLibraryW (L"UIAutomationCore.dll"))
        {
            raise     = reinterpret_cast<RaiseNotificationFn> (::GetProcAddress (module, "UiaRaiseNotificationEvent"));
            listening = reinterpret_cast<ClientsListeningFn>  (::GetProcAddress (module, "UiaClientsAreListening"));
        }
    }

    RaiseNotificationFn raise = nullptr;
    ClientsListeningFn listening = nullptr;
};

const UiaFunctions& uia()
{
    static const UiaFunctions instance;
    return instance;
}
} // namespace
#endif

void announce (juce::Component& source, const juce::String& text, SpeechQueue queue)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (text.isEmpty())
        return;

   #if JUCE_WINDOWS
    // Не используем juce::AccessibilityHandler::postAnnouncement: на Windows JUCE
    // произносит его синтезатором SAPI (голос как у Экранного диктора), а NVDA и JAWS
    // это сообщение не получают вовсе.
    const auto& functions = uia();
    if (functions.raise == nullptr)
        return;
    if (functions.listening != nullptr && ! functions.listening())
        return; // скринридер не запущен — некому говорить

    // nullptr, если компонент исключён из дерева доступности (setAccessible(false))
    // или ещё не на экране: такое событие NVDA не сможет связать с окном.
    auto* handler = source.getAccessibilityHandler();
    if (handler == nullptr)
        return;

    // На Windows нативная реализация JUCE — COM-объект провайдера UI Automation.
    auto* unknown = reinterpret_cast<IUnknown*> (handler->getNativeImplementation());
    if (unknown == nullptr)
        return;

    IRawElementProviderSimple* provider = nullptr;
    if (FAILED (unknown->QueryInterface (__uuidof (IRawElementProviderSimple),
                                         reinterpret_cast<void**> (&provider)))
        || provider == nullptr)
        return;

    auto* message  = ::SysAllocString (text.toWideCharPointer());
    auto* activity = ::SysAllocString (L"PluginAnnouncement");

    functions.raise (provider,
                     notificationKindOther,
                     queue == SpeechQueue::replacePrevious ? processingImportantMostRecent
                                                           : processingImportantAll,
                     message,
                     activity);

    ::SysFreeString (message);
    ::SysFreeString (activity);
    provider->Release();
   #else
    juce::ignoreUnused (source);
    // На macOS JUCE передаёт это VoiceOver штатным уведомлением.
    juce::AccessibilityHandler::postAnnouncement (text,
        queue == SpeechQueue::replacePrevious ? juce::AccessibilityHandler::AnnouncementPriority::high
                                              : juce::AccessibilityHandler::AnnouncementPriority::medium);
   #endif
}
} // namespace a11y
