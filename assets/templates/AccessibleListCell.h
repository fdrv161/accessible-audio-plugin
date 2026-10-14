#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "ScreenReaderAnnouncer.h"

// Один фокусируемый элемент для длинного «виртуального» списка: слоты, файлы браузера,
// шаги секвенсора. Данные и курсор хранит владелец; ячейка спрашивает текст строки.
// Скринридер всегда читает текущую строку (живой title, NVDA+Стрелка вверх),
// а при движении курсора слышит только новую строку.
//
// Клавиши: Вверх/Вниз, Home/End, Page Up/Down (pageSize строк), буква — поиск вперёд,
// Shift+буква — назад, Enter — onActivate. Края тихие. Свои клавиши экрана
// подключаются через onExtraKey (вызывается первым).
class AccessibleListCell : public juce::Component
{
public:
    std::function<int()> getCount;                         // число строк
    std::function<juce::String (int)> getLine;             // полный текст строки для чтения
    std::function<juce::String (int)> getSearchName;       // имя для поиска по букве (если пусто — getLine)
    std::function<void (int)> onCursorChanged;             // например, прослушать выбранный файл
    std::function<void (int)> onActivate;                  // Enter
    std::function<bool (const juce::KeyPress&)> onExtraKey;

    juce::String emptyText { "Empty" };                    // строка для пустого списка
    int pageSize = 10;

    explicit AccessibleListCell (juce::AccessibilityRole roleToUse = juce::AccessibilityRole::list)
        : role (roleToUse)
    {
        setWantsKeyboardFocus (true);
    }

    int getCursor() const noexcept { return cursor; }

    juce::String currentLine() const
    {
        const int count = getCount ? getCount() : 0;
        if (count <= 0 || ! getLine)
            return emptyText;
        return getLine (juce::jlimit (0, count - 1, cursor));
    }

    // speak = true при действии пользователя; false — при программной смене (без речи).
    void setCursor (int index, bool speak)
    {
        const int count = getCount ? getCount() : 0;
        const int next = count > 0 ? juce::jlimit (0, count - 1, index) : 0;
        const bool changed = next != cursor;
        cursor = next;
        repaint();

        if (changed && onCursorChanged)
            onCursorChanged (cursor);
        if (changed && speak)
            a11y::announce (*this, currentLine());
    }

    // Список изменился (новая папка и т.п.): удержать курсор в границах и сказать строку.
    void refresh (bool speak)
    {
        const int count = getCount ? getCount() : 0;
        cursor = count > 0 ? juce::jlimit (0, count - 1, cursor) : 0;
        repaint();
        if (speak)
            a11y::announce (*this, currentLine());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (onExtraKey && onExtraKey (key))
            return true;

        const auto mods = key.getModifiers();
        const int code = key.getKeyCode();
        const int count = getCount ? getCount() : 0;
        // На Windows isCommandDown() истинно при Ctrl: проверяем оба только как «любой из».
        const bool plain = ! mods.isAnyModifierKeyDown();
        const bool ctrlOnly = mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isShiftDown();

        if (plain && code == juce::KeyPress::upKey)       { if (cursor > 0) setCursor (cursor - 1, true); return true; }
        if (plain && code == juce::KeyPress::downKey)     { if (cursor + 1 < count) setCursor (cursor + 1, true); return true; }
        if (plain && code == juce::KeyPress::pageUpKey)   { setCursor (cursor - pageSize, true); return true; }
        if (plain && code == juce::KeyPress::pageDownKey) { setCursor (cursor + pageSize, true); return true; }
        if ((plain || ctrlOnly) && code == juce::KeyPress::homeKey) { setCursor (0, true); return true; }
        if ((plain || ctrlOnly) && code == juce::KeyPress::endKey)  { setCursor (count - 1, true); return true; }

        if (plain && code == juce::KeyPress::returnKey)
        {
            if (count > 0 && onActivate)
                onActivate (cursor);
            return true;
        }

        if (! mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isCommandDown())
        {
            // Для поиска берём напечатанный символ: коды стрелок и Page Up не должны
            // попасть в «печатный» диапазон. Для Shift+цифра символ — «!», поэтому
            // при Shift берём букву/цифру из кода клавиши.
            auto typed = key.getTextCharacter();
            if (mods.isShiftDown() && ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')))
                typed = (juce::juce_wchar) code;

            if (typed >= 33 && typed != 127)
            {
                searchByInitial (typed, mods.isShiftDown() ? -1 : 1);
                return true;
            }
        }

        // Tab, Escape и пробел отдаём родителю: он решает, переходить ли фокус,
        // закрывать ли экран и отдавать ли пробел DAW.
        if (code == juce::KeyPress::tabKey || code == juce::KeyPress::escapeKey || code == juce::KeyPress::spaceKey)
            return false;

        return true; // остальное поглощаем, чтобы случайно не запустить действие DAW
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (16.0f);
        g.drawText (currentLine(), getLocalBounds().reduced (6), juce::Justification::centredLeft, true);
        if (hasKeyboardFocus (false))
            g.drawRect (getLocalBounds(), 2);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        // Заголовок вычисляется при каждом запросе: NVDA+Стрелка вверх всегда читает
        // актуальную строку, без событий и таймеров.
        class LiveTitleHandler final : public juce::AccessibilityHandler
        {
        public:
            LiveTitleHandler (AccessibleListCell& ownerToUse, juce::AccessibilityRole r)
                : juce::AccessibilityHandler (ownerToUse, r), owner (ownerToUse) {}
            juce::String getTitle() const override { return owner.currentLine(); }
        private:
            AccessibleListCell& owner;
        };
        return std::make_unique<LiveTitleHandler> (*this, role);
    }

private:
    void searchByInitial (juce::juce_wchar initial, int direction)
    {
        const int count = getCount ? getCount() : 0;
        if (count <= 0)
            return;
        const auto target = juce::CharacterFunctions::toLowerCase (initial);
        for (int distance = 1; distance <= count; ++distance)
        {
            int index = (cursor + direction * distance) % count;
            if (index < 0)
                index += count;
            const auto name = getSearchName ? getSearchName (index) : (getLine ? getLine (index) : juce::String());
            if (name.isNotEmpty() && juce::CharacterFunctions::toLowerCase (name[0]) == target)
            {
                setCursor (index, true);
                return;
            }
        }
        // Нет совпадений — тишина, как на краях списка.
    }

    juce::AccessibilityRole role;
    int cursor = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccessibleListCell)
};
