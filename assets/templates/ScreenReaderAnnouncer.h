#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Озвучка сообщений для NVDA, JAWS, Экранного диктора (Windows, через UI Automation)
// и VoiceOver (macOS). Подробности и правила — references/announcements-and-focus.md.
namespace a11y
{
// Как вести себя с очередью речи скринридера.
enum class SpeechQueue
{
    replacePrevious, // новое сообщение прерывает прежнее: значения, навигация
    keepAll          // сообщение нельзя потерять: итог операции, ошибка
};

// Произнести текст. source — видимый компонент окна плагина, который есть в дереве
// доступности: элемент в фокусе или постоянная метка статуса. Только поток сообщений.
// Функция НЕ меняет title/description источника: временный текст не «прилипает».
void announce (juce::Component& source, const juce::String& text,
               SpeechQueue queue = SpeechQueue::replacePrevious);

// Для телеметрии и статусов: не повторяет текст, который только что был сказан.
// Не используй для ответов на нажатия — пользователь вправе услышать фразу ещё раз.
class DedupAnnouncer
{
public:
    void say (juce::Component& source, const juce::String& text,
              SpeechQueue queue = SpeechQueue::replacePrevious)
    {
        if (text == lastText)
            return;
        lastText = text;
        announce (source, text, queue);
    }

    void reset() { lastText.clear(); }

private:
    juce::String lastText;
};
} // namespace a11y
