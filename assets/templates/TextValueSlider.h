#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Ползунок, значение которого скринридер читает текстом с единицами («+3 dB»,
// «1.2 kHz»), а не процентами диапазона. Клавиши: Вверх/Вправо +шаг, Вниз/Влево −шаг,
// Page Up/Down — крупный шаг, Home/End — пределы, Enter — точный ввод (onRequestExactEntry).
// Изменения от пользователя обрамлены жестами для автоматизации хоста; изменения от хоста
// (setFromHost) не отправляются обратно — нет «эха» и лишней записи огибающей.
class TextValueSlider : public juce::Slider
{
public:
    std::function<juce::String (double)> format;                  // текст значения с единицей
    std::function<bool (const juce::String&, double&)> parse;     // строгий разбор точного ввода
    std::function<void()> onGestureBegin, onGestureEnd;            // жесты хоста
    std::function<void (double)> onUserValue;                     // значение от пользователя
    std::function<void()> onRequestExactEntry;                    // открыть поле ввода

    double keyStep = 1.0;
    double pageStep = 6.0;
    // Стандарт ARIA для ползунков: Home — минимум, End — максимум. В сетке LSampler-24
    // Alt+Home — максимум. Выбери одно правило на весь плагин.
    bool homeIsMaximum = false;

    explicit TextValueSlider (const juce::String& accessibleName)
    {
        setName (accessibleName);
        setTitle (accessibleName);
        setWantsKeyboardFocus (true);
        setSliderStyle (juce::Slider::LinearHorizontal);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

        textFromValueFunction = [this] (double v) { return format ? format (v) : juce::String (v); };
        valueFromTextFunction = [this] (const juce::String& text)
        {
            double parsed = getValue();
            return (parse && parse (text, parsed)) ? parsed : getValue();
        };

        onDragStart = [this] { beginGesture (true); };
        onDragEnd   = [this] { endGesture (true); };
        onValueChange = [this]
        {
            if (syncingFromHost)
                return;
            // Клавиатура и колесо: жест на каждое изменение. Перетаскивание: один жест на всё.
            const bool single = ! dragGesture;
            if (single) beginGesture (false);
            if (onUserValue) onUserValue (getValue());
            if (single) endGesture (false);
        };
    }

    void setFromHost (double value)
    {
        const juce::ScopedValueSetter<bool> guard (syncingFromHost, true);
        setValue (value, juce::sendNotificationSync);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        if (mods.isAltDown() || mods.isCtrlDown() || mods.isCommandDown())
            return juce::Slider::keyPressed (key); // комбинации — родителю/по умолчанию

        const int code = key.getKeyCode();
        const double minimum = getMinimum(), maximum = getMaximum();

        if (code == juce::KeyPress::returnKey)
        {
            if (onRequestExactEntry) onRequestExactEntry();
            return true;
        }
        if (code == juce::KeyPress::upKey   || code == juce::KeyPress::rightKey) { stepBy (keyStep);   return true; }
        if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)  { stepBy (-keyStep);  return true; }
        if (code == juce::KeyPress::pageUpKey)   { stepBy (pageStep);  return true; }
        if (code == juce::KeyPress::pageDownKey) { stepBy (-pageStep); return true; }
        if (code == juce::KeyPress::homeKey) { setValue (homeIsMaximum ? maximum : minimum, juce::sendNotificationSync); return true; }
        if (code == juce::KeyPress::endKey)  { setValue (homeIsMaximum ? minimum : maximum, juce::sendNotificationSync); return true; }

        return juce::Slider::keyPressed (key);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        // Текстовый интерфейс значения вместо числового диапазона: UIA получает строку
        // «+3 dB», и скринридер не подставляет проценты.
        class TextValue final : public juce::AccessibilityTextValueInterface
        {
        public:
            explicit TextValue (TextValueSlider& s) : slider (s) {}
            bool isReadOnly() const override { return ! slider.isEnabled(); }
            juce::String getCurrentValueAsString() const override { return slider.getTextFromValue (slider.getValue()); }
            void setValueAsString (const juce::String& text) override
            {
                double parsed = slider.getValue();
                if (slider.parse && slider.parse (text, parsed))
                    slider.setValue (parsed, juce::sendNotificationSync);
            }
        private:
            TextValueSlider& slider;
        };

        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider,
            juce::AccessibilityActions{},
            juce::AccessibilityHandler::Interfaces { std::make_unique<TextValue> (*this) });
    }

private:
    void stepBy (double delta)
    {
        setValue (juce::jlimit (getMinimum(), getMaximum(), getValue() + delta), juce::sendNotificationSync);
    }

    void beginGesture (bool fromDrag)
    {
        if (fromDrag) dragGesture = true;
        if (onGestureBegin) onGestureBegin();
    }

    void endGesture (bool fromDrag)
    {
        if (fromDrag) dragGesture = false;
        if (onGestureEnd) onGestureEnd();
    }

    bool syncingFromHost = false;
    bool dragGesture = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextValueSlider)
};

// Пример строгого разбора: число целиком, конечное, в пределах; «dB» в конце допускается.
//   slider.parse = [&slider] (const juce::String& text, double& out)
//   {
//       auto t = text.trim().toLowerCase().replace ("db", "").trim();
//       if (! t.containsOnly ("+-0123456789.") || t.isEmpty()) return false;
//       const double v = t.getDoubleValue();
//       if (! std::isfinite (v) || v < slider.getMinimum() || v > slider.getMaximum()) return false;
//       out = v; return true;
//   };
