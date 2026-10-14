#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "ScreenReaderAnnouncer.h"

// Доступный редактор ВСЕХ параметров процессора одним фокусируемым элементом
// (упрощённая модель «Сетка/Значение» из LSampler-24). Подходит как основа редактора
// нового плагина или как «доступная вкладка» поверх недоступного GUI.
//
// Клавиши:
//   Вверх/Вниз            — параметр в группе (края тихие)
//   Влево/Вправо          — предыдущая/следующая группа
//   Home/End              — первый/последний в группе; Ctrl+Home/End — во всём списке
//   Alt+Вверх/Вниз        — значение на шаг; Alt+Page Up/Down — крупный шаг
//   Alt+Home/End          — максимум/минимум
//   Backspace             — значение по умолчанию
//   Enter                 — точный ввод текстом (Enter — применить, Escape — отмена)
//   Буква / Shift+буква   — поиск параметра по первой букве вперёд/назад
// Tab, Escape и пробел уходят родителю (переход фокуса, закрытие, транспорт DAW).
//
// Речь: при навигации — «Имя, значение», при изменении — только значение,
// изменения от автоматизации не озвучиваются (их читает живой title по NVDA+Стрелка вверх).
class AccessibleParameterList : public juce::Component, private juce::Timer
{
public:
    struct Entry
    {
        juce::RangedAudioParameter* parameter = nullptr;
        juce::String group;
    };

    // Собрать параметры из дерева процессора; группа — имя ближайшей подгруппы.
    static std::vector<Entry> fromProcessor (juce::AudioProcessor& processor)
    {
        std::vector<Entry> result;
        collect (processor.getParameterTree(), "Parameters", result);
        return result;
    }

    explicit AccessibleParameterList (std::vector<Entry> entriesToUse)
        : entries (std::move (entriesToUse))
    {
        setWantsKeyboardFocus (true);
        setTitle ("Parameters");

        // Поле точного ввода: обычный TextEditor, его читает любой скринридер.
        addChildComponent (entry);
        entry.setMultiLine (false);
        entry.setSelectAllWhenFocused (true);
        entry.onReturnKey = [this] { finishEntry (true); };
        entry.onEscapeKey = [this] { finishEntry (false); };
        entry.onFocusLost = [this] { if (entry.isVisible()) finishEntry (false); };

        startTimerHz (10);
    }

    juce::String currentLine() const
    {
        if (entries.empty())
            return "No parameters";
        auto* p = current();
        return p->getName (128) + ", " + valueText (*p);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (entry.isVisible())
            return true; // клавиши, которые не принял TextEditor, никуда не отдаём

        const auto mods = key.getModifiers();
        const int code = key.getKeyCode();
        const bool alt = mods.isAltDown() && ! mods.isCtrlDown() && ! mods.isShiftDown();
        const bool ctrl = mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isShiftDown();
        const bool plain = ! mods.isAnyModifierKeyDown();

        if (code == juce::KeyPress::tabKey || code == juce::KeyPress::escapeKey || code == juce::KeyPress::spaceKey)
            return false;
        if (entries.empty())
            return true;

        // Изменение значения.
        if (alt)
        {
            if (code == juce::KeyPress::upKey)       { step (1, false);  return true; }
            if (code == juce::KeyPress::downKey)     { step (-1, false); return true; }
            if (code == juce::KeyPress::pageUpKey)   { step (1, true);   return true; }
            if (code == juce::KeyPress::pageDownKey) { step (-1, true);  return true; }
            if (code == juce::KeyPress::homeKey)     { setNormalised (1.0f); return true; }
            if (code == juce::KeyPress::endKey)      { setNormalised (0.0f); return true; }
            // Alt+F4 должен закрывать окно как обычно; прочие Alt-комбинации не отдаём DAW.
            return code != juce::KeyPress::F4Key;
        }

        if (ctrl && code == juce::KeyPress::homeKey) { select (0); return true; }
        if (ctrl && code == juce::KeyPress::endKey)  { select ((int) entries.size() - 1); return true; }

        if (plain)
        {
            const int begin = groupBegin (cursor), end = groupEnd (cursor);
            if (code == juce::KeyPress::upKey)       { if (cursor > begin) select (cursor - 1); return true; }
            if (code == juce::KeyPress::downKey)     { if (cursor < end)   select (cursor + 1); return true; }
            if (code == juce::KeyPress::homeKey)     { select (begin); return true; }
            if (code == juce::KeyPress::endKey)      { select (end);   return true; }
            if (code == juce::KeyPress::pageUpKey)   { select (juce::jmax (begin, cursor - 8)); return true; }
            if (code == juce::KeyPress::pageDownKey) { select (juce::jmin (end, cursor + 8));   return true; }
            if (code == juce::KeyPress::leftKey)     { moveGroup (-1); return true; }
            if (code == juce::KeyPress::rightKey)    { moveGroup (1);  return true; }
            if (code == juce::KeyPress::returnKey)   { beginEntry(); return true; }
            if (code == juce::KeyPress::backspaceKey){ setNormalised (current()->getDefaultValue()); return true; }
        }

        if (! mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isCommandDown())
        {
            auto typed = key.getTextCharacter();
            if (mods.isShiftDown() && ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')))
                typed = (juce::juce_wchar) code; // Shift+цифра даёт «!», берём саму цифру
            if (typed >= 33 && typed != 127)
            {
                searchByInitial (typed, mods.isShiftDown() ? -1 : 1);
                return true;
            }
        }

        return true; // список владеет клавиатурой: Ctrl+Z, Ctrl+S и пр. не уходят в DAW
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced (6);
        g.setColour (findColour (juce::Label::textColourId));
        if (! entries.empty())
        {
            g.setFont (13.0f);
            g.drawText (entries[(size_t) cursor].group, area.removeFromTop (18), juce::Justification::centredLeft, true);
        }
        g.setFont (16.0f);
        g.drawText (lastPaintedLine = currentLine(), area, juce::Justification::centredLeft, true);
        if (hasKeyboardFocus (true))
            g.drawRect (getLocalBounds(), 2);
    }

    void resized() override
    {
        entry.setBounds (getLocalBounds().reduced (6).removeFromBottom (28));
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        // Живой title: всегда «Имя, значение» на момент запроса, включая автоматизацию.
        class LiveTitleHandler final : public juce::AccessibilityHandler
        {
        public:
            explicit LiveTitleHandler (AccessibleParameterList& o)
                : juce::AccessibilityHandler (o, juce::AccessibilityRole::list), owner (o) {}
            juce::String getTitle() const override { return owner.currentLine(); }
        private:
            AccessibleParameterList& owner;
        };
        return std::make_unique<LiveTitleHandler> (*this);
    }

private:
    static void collect (const juce::AudioProcessorParameterGroup& group, const juce::String& groupName,
                         std::vector<Entry>& out)
    {
        for (auto* node : group)
        {
            if (auto* sub = node->getGroup())
                collect (*sub, sub->getName(), out);
            else if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (node->getParameter()))
                out.push_back ({ ranged, groupName });
        }
    }

    juce::RangedAudioParameter* current() const { return entries[(size_t) cursor].parameter; }

    static juce::String valueText (juce::RangedAudioParameter& p)
    {
        auto text = p.getCurrentValueAsText().trim();
        const auto unit = p.getLabel().trim();
        if (unit.isNotEmpty() && ! text.endsWithIgnoreCase (unit))
            text << " " << unit;
        return text;
    }

    int groupBegin (int index) const
    {
        while (index > 0 && entries[(size_t) index - 1].group == entries[(size_t) index].group) --index;
        return index;
    }

    int groupEnd (int index) const
    {
        const int last = (int) entries.size() - 1;
        while (index < last && entries[(size_t) index + 1].group == entries[(size_t) index].group) ++index;
        return index;
    }

    void select (int index)
    {
        index = juce::jlimit (0, (int) entries.size() - 1, index);
        if (index == cursor)
            return; // край — тишина
        cursor = index;
        repaint();
        a11y::announce (*this, currentLine());
    }

    void moveGroup (int direction)
    {
        // Переходим в соседнюю группу, сохраняя номер строки, если он там есть.
        const int begin = groupBegin (cursor), end = groupEnd (cursor);
        const int row = cursor - begin;
        if (direction < 0 && begin > 0)
        {
            const int previousBegin = groupBegin (begin - 1);
            selectWithGroup (juce::jmin (previousBegin + row, begin - 1));
        }
        else if (direction > 0 && end + 1 < (int) entries.size())
        {
            selectWithGroup (juce::jmin (end + 1 + row, groupEnd (end + 1)));
        }
    }

    void selectWithGroup (int index)
    {
        cursor = juce::jlimit (0, (int) entries.size() - 1, index);
        repaint();
        a11y::announce (*this, entries[(size_t) cursor].group + ". " + currentLine());
    }

    void step (int direction, bool coarse)
    {
        auto* p = current();
        const auto& range = p->getNormalisableRange();
        float next = p->getValue();

        if (p->isDiscrete() || range.interval > 0.0f)
        {
            // Дискретные и с явным интервалом — шагаем в реальных единицах.
            const float interval = range.interval > 0.0f ? range.interval : 1.0f;
            const int steps = coarse ? juce::jmax (1, p->getNumSteps() / 10) : 1;
            const float real = range.convertFrom0to1 (p->getValue()) + (float) (direction * steps) * interval;
            next = range.convertTo0to1 (range.snapToLegalValue (juce::jlimit (range.start, range.end, real)));
        }
        else
        {
            // Непрерывные — в нормализованной шкале: учитывает логарифмический skew
            // (у частоты среза шаг 1% одинаково удобен внизу и вверху диапазона).
            next = juce::jlimit (0.0f, 1.0f, p->getValue() + (float) direction * (coarse ? 0.1f : 0.01f));
        }
        setNormalised (next);
    }

    void setNormalised (float normalised)
    {
        auto* p = current();
        const auto before = valueText (*p);
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        p->endChangeGesture();
        repaint();

        const auto after = valueText (*p);
        if (after != before)
            a11y::announce (*this, after); // при изменении — только значение
    }

    void beginEntry()
    {
        auto* p = current();
        entry.setTitle (p->getName (128) + ", type value");
        entry.setText (p->getCurrentValueAsText(), false);
        entry.setVisible (true);
        entry.grabKeyboardFocus();
        entry.selectAll();
    }

    void finishEntry (bool apply)
    {
        const auto text = entry.getText();
        // Прячем поле и возвращаем фокус асинхронно: этот вызов приходит из keyPressed поля.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<AccessibleParameterList> (this), text, apply]
        {
            if (safeThis == nullptr || ! safeThis->entry.isVisible())
                return;
            safeThis->entry.setVisible (false);
            safeThis->grabKeyboardFocus();
            if (auto* handler = safeThis->getAccessibilityHandler())
                handler->grabFocus();
            if (! apply)
                return;
            float normalised = 0.0f;
            if (parseEntry (*safeThis->current(), text, normalised))
                safeThis->setNormalised (normalised);
            else
                a11y::announce (*safeThis, "Invalid value: " + text.trim(), a11y::SpeechQueue::keepAll);
        });
    }

    // getValueForText на мусорном тексте молча вернёт 0 (или первый вариант списка).
    // Принимаем только число или точную подпись варианта.
    static bool parseEntry (juce::RangedAudioParameter& p, const juce::String& text, float& normalised)
    {
        const auto t = text.trim();
        if (t.isEmpty())
            return false;
        const float candidate = p.getValueForText (t);
        const bool numeric = t.containsAnyOf ("0123456789");
        const bool label = p.getText (candidate, 128).trim().equalsIgnoreCase (t);
        if (! numeric && ! label)
            return false;
        normalised = juce::jlimit (0.0f, 1.0f, candidate);
        return true;
    }

    void searchByInitial (juce::juce_wchar initial, int direction)
    {
        const int count = (int) entries.size();
        const auto target = juce::CharacterFunctions::toLowerCase (initial);
        for (int distance = 1; distance <= count; ++distance)
        {
            int index = (cursor + direction * distance) % count;
            if (index < 0)
                index += count;
            const auto name = entries[(size_t) index].parameter->getName (128);
            if (name.isNotEmpty() && juce::CharacterFunctions::toLowerCase (name[0]) == target)
            {
                if (entries[(size_t) index].group != entries[(size_t) cursor].group)
                    selectWithGroup (index);
                else
                    select (index);
                return;
            }
        }
    }

    // Автоматизация хоста меняет значения: перерисовываем, но не говорим.
    void timerCallback() override
    {
        if (isShowing() && currentLine() != lastPaintedLine)
            repaint();
    }

    std::vector<Entry> entries;
    int cursor = 0;
    juce::String lastPaintedLine;
    juce::TextEditor entry;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccessibleParameterList)
};

// Использование в редакторе плагина:
//   list = std::make_unique<AccessibleParameterList> (AccessibleParameterList::fromProcessor (processor));
//   addAndMakeVisible (*list);
//   // в конструкторе редактора после setSize — фокус на список:
//   juce::MessageManager::callAsync ([safe = SafePointer<Editor> (this)] { if (safe) safe->list->grabKeyboardFocus(); });
