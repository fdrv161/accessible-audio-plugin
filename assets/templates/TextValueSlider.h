#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include "A11yKeys.h"

// Slider whose value screen readers read as text with units ("+3 dB", "1.2 kHz").
//
// Why: a plain juce::Slider exposes both a UIA Value (text from getTextFromValue) and a
// RangeValue (bare number). Current NVDA prefers the text; other screen readers and old
// NVDA versions may read the bare number or a percentage. This slider exposes ONLY the
// text value, so every screen reader gets "+3 dB". JUCE still raises the value-changed
// event, so the focused slider's new value is spoken without any announcement code
// (do not announce it again: double speech, defect SPK-03).
//
// Keys: Up/Right +step, Down/Left -step, Shift+arrows fine step, Page Up/Down coarse,
// Home/End limits (see homeIsMaximum), Backspace/Delete default value, Enter exact entry
// (onRequestExactEntry), Shift+F10 / Applications key onContextMenu.
// User changes are wrapped in host gestures; host changes (setFromHost) are not sent
// back ("echo", defect PAR-04).
class TextValueSlider : public juce::Slider
{
public:
    std::function<juce::String (double)> format;              // value text with unit
    std::function<bool (const juce::String&, double&)> parse; // strict parse of typed text
    std::function<void()> onGestureBegin, onGestureEnd;        // host gestures
    std::function<void (double)> onUserValue;                 // value changed by the user
    std::function<void()> onRequestExactEntry;                // open an entry field
    std::function<void()> onContextMenu;                      // automation, MIDI learn, reset...

    double keyStep = 1.0;
    double fineStep = 0.1;
    double pageStep = 6.0;
    double defaultValue = std::nan ("");
    // Platform convention (Windows trackbar, ARIA): Home = minimum. LSampler-24 and
    // Surge XT use Home = maximum. Pick ONE rule for the whole plugin.
    bool homeIsMaximum = false;

    explicit TextValueSlider (const juce::String& accessibleName)
    {
        setName (accessibleName);
        setTitle (accessibleName); // screen readers use the title, not the name
        setWantsKeyboardFocus (true);
        setSliderStyle (juce::Slider::LinearHorizontal);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

        textFromValueFunction = [this] (double v) { return format ? format (v) : juce::String (v); };
        valueFromTextFunction = [this] (const juce::String& text)
        {
            double parsed = getValue();
            return (parse && parse (text, parsed)) ? parsed : getValue();
        };

        onDragStart = [this] { dragGesture = true;  if (onGestureBegin) onGestureBegin(); };
        onDragEnd   = [this] { dragGesture = false; if (onGestureEnd)   onGestureEnd(); };
        onValueChange = [this]
        {
            if (syncingFromHost)
                return;
            // Keyboard and wheel: one gesture per change. Mouse drag: one gesture in total.
            const bool single = ! dragGesture;
            if (single && onGestureBegin) onGestureBegin();
            if (onUserValue) onUserValue (getValue());
            if (single && onGestureEnd) onGestureEnd();
        };
    }

    // Call from the host-parameter listener (on the message thread) to show a host change.
    void setFromHost (double value)
    {
        const juce::ScopedValueSetter<bool> guard (syncingFromHost, true);
        setValue (value, juce::sendNotificationSync);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        using KP = juce::KeyPress;

        if (a11y::isContextMenuKey (key))
        {
            if (onContextMenu) onContextMenu();
            return true;
        }

        if (a11y::is (key, KP::returnKey))
        {
            if (onRequestExactEntry) onRequestExactEntry();
            return true;
        }

        if (a11y::is (key, KP::upKey)   || a11y::is (key, KP::rightKey))              { stepBy (keyStep);   return true; }
        if (a11y::is (key, KP::downKey) || a11y::is (key, KP::leftKey))               { stepBy (-keyStep);  return true; }
        if (a11y::is (key, KP::upKey,   false, false, true) || a11y::is (key, KP::rightKey, false, false, true)) { stepBy (fineStep);  return true; }
        if (a11y::is (key, KP::downKey, false, false, true) || a11y::is (key, KP::leftKey,  false, false, true)) { stepBy (-fineStep); return true; }
        if (a11y::is (key, KP::pageUpKey))   { stepBy (pageStep);  return true; }
        if (a11y::is (key, KP::pageDownKey)) { stepBy (-pageStep); return true; }
        if (a11y::is (key, KP::homeKey))     { setValue (homeIsMaximum ? getMaximum() : getMinimum(), juce::sendNotificationSync); return true; }
        if (a11y::is (key, KP::endKey))      { setValue (homeIsMaximum ? getMinimum() : getMaximum(), juce::sendNotificationSync); return true; }

        if ((a11y::is (key, KP::backspaceKey) || a11y::is (key, KP::deleteKey)) && ! std::isnan (defaultValue))
        {
            setValue (defaultValue, juce::sendNotificationSync);
            return true;
        }

        return false; // other keys go to the parent/editor
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        // Text value interface instead of a numeric range: UIA gets only "+3 dB".
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

    bool syncingFromHost = false;
    bool dragGesture = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextValueSlider)
};

// Example of a strict parser: a whole finite number in range; a trailing "dB" is allowed.
//   slider.parse = [&slider] (const juce::String& text, double& out)
//   {
//       auto t = text.trim().toLowerCase().replace ("db", "").trim();
//       if (t.isEmpty() || ! t.containsOnly ("+-0123456789.")) return false;
//       const double v = t.getDoubleValue();
//       if (! std::isfinite (v) || v < slider.getMinimum() || v > slider.getMaximum()) return false;
//       out = v; return true;
//   };
