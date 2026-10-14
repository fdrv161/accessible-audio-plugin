#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "A11yKeys.h"
#include "A11yParameters.h"
#include "ScreenReaderAnnouncer.h"

// Retrofit accessibility onto custom-painted controls WITHOUT changing them (the overlay
// idea used by Surge XT). A transparent focusable component is laid exactly over the
// visual widget: the mouse goes through to the widget, while keyboard focus and screen
// readers get the overlay, bound to the same host parameter or action.
//
//   cutoffOverlay = std::make_unique<a11y::ParameterOverlay> (*apvts.getParameter ("cutoff"));
//   cutoffOverlay->attachTo (cutoffKnob);       // same parent, on top, follows its bounds
//   loadOverlay = std::make_unique<a11y::ActionOverlay> ("Load preset", "Alt+O");
//   loadOverlay->onPress = [this] { openPresetBrowser(); };
//   loadOverlay->attachTo (loadIcon);
//
// Also: make the visual widget stop taking focus (widget.setWantsKeyboardFocus (false)) and
// hide its own accessibility if it has a useless one (widget.setAccessible (false)).
// Tab order follows on-screen position; use setExplicitFocusOrder() on overlays to change it.
namespace a11y
{
class OverlayBase : public juce::Component, private juce::ComponentListener
{
public:
    ~OverlayBase() override { detach(); }

    // Put this overlay over `widget` (same parent, on top) and keep it there.
    void attachTo (juce::Component& widget)
    {
        detach();
        visual = &widget;
        visual->addComponentListener (this);
        if (auto* parent = widget.getParentComponent())
        {
            parent->addAndMakeVisible (*this);
            toFront (false);
        }
        syncToVisual();
    }

    void detach()
    {
        if (visual != nullptr)
            visual->removeComponentListener (this);
        visual = nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        if (hasKeyboardFocus (true)) // focus ring for sighted keyboard users
        {
            g.setColour (findColour (juce::TextEditor::focusedOutlineColourId));
            g.drawRect (getLocalBounds(), 2);
        }
    }

protected:
    OverlayBase()
    {
        setWantsKeyboardFocus (true);
        setInterceptsMouseClicks (false, true); // mouse reaches the widget; children (entry field) still get clicks
    }

    juce::Component::SafePointer<juce::Component> visual;

private:
    void syncToVisual()
    {
        if (visual == nullptr)
            return;
        setBounds (visual->getBounds());
        setVisible (visual->isVisible());
    }

    void componentMovedOrResized (juce::Component&, bool, bool) override { syncToVisual(); }
    void componentVisibilityChanged (juce::Component&) override { syncToVisual(); }
    void componentBeingDeleted (juce::Component&) override { visual = nullptr; setVisible (false); }
};

// Overlay bound to a host parameter. Numbers act as a slider, choices as an option list
// (also a slider: Up/Down change the option), booleans as a toggle button.
// Value text = the parameter's own text + unit, the same as OSARA and the host show.
// Value changes raise JUCE's value-changed event, so the screen reader speaks the new
// value of the focused overlay by itself: do not announce it again (SPK-03).
class ParameterOverlay : public OverlayBase
{
public:
    std::function<void()> onContextMenu; // Shift+F10 / Applications key / VoiceOver "show menu"
    bool homeIsMaximum = false;          // one rule for the whole plugin (keyboard-model.md)

    explicit ParameterOverlay (juce::RangedAudioParameter& parameterToUse, const juce::String& title = {})
        : param (parameterToUse)
    {
        setTitle (title.isNotEmpty() ? title : param.getName (128));
        addChildComponent (entryField);
        entryField.setMultiLine (false);
        entryField.setSelectAllWhenFocused (true);
        entryField.setEscapeAndReturnKeysConsumed (true);
        entryField.onReturnKey = [this] { finishEntry (true); };
        entryField.onEscapeKey = [this] { finishEntry (false); };
        entryField.onFocusLost = [this] { if (entryField.isVisible()) finishEntry (false); };
    }

    bool isToggle() const { return param.isBoolean(); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        using KP = juce::KeyPress;
        if (entryField.isVisible())
            return true;

        if (isContextMenuKey (key))
        {
            if (onContextMenu) onContextMenu();
            return true;
        }

        if (isToggle())
        {
            if (is (key, KP::spaceKey) || is (key, KP::returnKey))
            {
                change (param.getValue() >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
            return false;
        }

        if (is (key, KP::upKey)   || is (key, KP::rightKey)) { change (steppedValue (param, 1,  StepSize::normal)); return true; }
        if (is (key, KP::downKey) || is (key, KP::leftKey))  { change (steppedValue (param, -1, StepSize::normal)); return true; }
        if (is (key, KP::upKey,   false, false, true) || is (key, KP::rightKey, false, false, true)) { change (steppedValue (param, 1,  StepSize::fine)); return true; }
        if (is (key, KP::downKey, false, false, true) || is (key, KP::leftKey,  false, false, true)) { change (steppedValue (param, -1, StepSize::fine)); return true; }
        if (is (key, KP::pageUpKey))   { change (steppedValue (param, 1,  StepSize::coarse)); return true; }
        if (is (key, KP::pageDownKey)) { change (steppedValue (param, -1, StepSize::coarse)); return true; }
        if (is (key, KP::homeKey))     { change (homeIsMaximum ? 1.0f : 0.0f); return true; }
        if (is (key, KP::endKey))      { change (homeIsMaximum ? 0.0f : 1.0f); return true; }
        if (is (key, KP::backspaceKey) || is (key, KP::deleteKey)) { change (param.getDefaultValue()); return true; }
        if (is (key, KP::returnKey))   { beginEntry(); return true; }

        return false; // the editor decides the rest
    }

    void resized() override { entryField.setBounds (getLocalBounds()); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        auto actions = juce::AccessibilityActions().addAction (juce::AccessibilityActionType::showMenu,
                                                               [this] { if (onContextMenu) onContextMenu(); });

        if (isToggle())
        {
            // Without overriding getCurrentState the checked state is always reported as
            // "not checked" (Surge XT hit this). JUCE then sends the toggle-state change
            // event itself when we raise valueChanged.
            class ToggleHandler final : public juce::AccessibilityHandler
            {
            public:
                ToggleHandler (ParameterOverlay& o, juce::AccessibilityActions a)
                    : juce::AccessibilityHandler (o, juce::AccessibilityRole::toggleButton, std::move (a)), owner (o) {}
                juce::AccessibleState getCurrentState() const override
                {
                    auto state = juce::AccessibilityHandler::getCurrentState().withCheckable();
                    return owner.param.getValue() >= 0.5f ? state.withChecked() : state;
                }
            private:
                ParameterOverlay& owner;
            };
            auto toggle = [this] { change (param.getValue() >= 0.5f ? 0.0f : 1.0f); };
            actions.addAction (juce::AccessibilityActionType::press, toggle)
                   .addAction (juce::AccessibilityActionType::toggle, toggle);
            return std::make_unique<ToggleHandler> (*this, std::move (actions));
        }

        // Text value only (no numeric range), so every screen reader reads "-3.0 dB".
        class TextValue final : public juce::AccessibilityTextValueInterface
        {
        public:
            explicit TextValue (ParameterOverlay& o) : owner (o) {}
            bool isReadOnly() const override { return false; }
            juce::String getCurrentValueAsString() const override { return valueText (owner.param); }
            void setValueAsString (const juce::String& text) override
            {
                float normalised = 0.0f;
                if (parseTypedValue (owner.param, text, normalised))
                    owner.change (normalised);
            }
        private:
            ParameterOverlay& owner;
        };
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, std::move (actions),
            juce::AccessibilityHandler::Interfaces { std::make_unique<TextValue> (*this) });
    }

private:
    void change (float normalised)
    {
        if (setByUser (param, normalised))
            if (auto* handler = getAccessibilityHandler())
                handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
        if (visual != nullptr)
            visual->repaint();
    }

    void beginEntry()
    {
        entryField.setTitle (getTitle() + ", type a value");
        entryField.setText (param.getCurrentValueAsText(), false);
        entryField.setVisible (true);
        entryField.grabKeyboardFocus();
        entryField.selectAll();
    }

    void finishEntry (bool apply)
    {
        const auto text = entryField.getText();
        // Asynchronous: this runs inside the field's own key handling (RT-03, RT-04).
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<ParameterOverlay> (this), text, apply]
        {
            if (safeThis == nullptr || ! safeThis->entryField.isVisible())
                return;
            bool valid = true;
            if (apply)
            {
                float normalised = 0.0f;
                valid = parseTypedValue (safeThis->param, text, normalised);
                if (valid) // silent: the focus speech below reads the new value
                    setByUser (safeThis->param, normalised);
            }
            safeThis->grabKeyboardFocus(); // focus first, then hide the field (FOC-02)
            if (auto* handler = safeThis->getAccessibilityHandler())
                handler->grabFocus();
            safeThis->entryField.setVisible (false);
            if (safeThis->visual != nullptr)
                safeThis->visual->repaint();
            if (! valid)
                safeThis->announcer.say ("Invalid value: " + text.trim() + ". Type a number or an option name.");
        });
    }

    juce::RangedAudioParameter& param;
    juce::TextEditor entryField;
    Announcer announcer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterOverlay)
};

// Overlay for a custom-painted button (icon, clickable label, drawn switch).
// Set isOn to make it a toggle with a checked state.
class ActionOverlay : public OverlayBase
{
public:
    std::function<void()> onPress;
    std::function<void()> onContextMenu;
    std::function<bool()> isOn; // optional: present = toggle button

    // title = the action ("Load preset"); shortcut = spoken after the role ("Alt+O").
    explicit ActionOverlay (const juce::String& title, const juce::String& shortcut = {})
    {
        setTitle (title);
        setDescription (shortcut); // only the shortcut: never put messages here (SPK-04)
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (isContextMenuKey (key))
        {
            if (onContextMenu) onContextMenu();
            return true;
        }
        if (is (key, juce::KeyPress::spaceKey) || is (key, juce::KeyPress::returnKey))
        {
            press();
            return true;
        }
        return false;
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        class ButtonHandler final : public juce::AccessibilityHandler
        {
        public:
            ButtonHandler (ActionOverlay& o, juce::AccessibilityRole r, juce::AccessibilityActions a)
                : juce::AccessibilityHandler (o, r, std::move (a)), owner (o) {}
            juce::AccessibleState getCurrentState() const override
            {
                auto state = juce::AccessibilityHandler::getCurrentState();
                if (! owner.isOn)
                    return state;
                state = state.withCheckable();
                return owner.isOn() ? state.withChecked() : state;
            }
        private:
            ActionOverlay& owner;
        };

        auto actions = juce::AccessibilityActions()
                           .addAction (juce::AccessibilityActionType::press, [this] { press(); })
                           .addAction (juce::AccessibilityActionType::showMenu, [this] { if (onContextMenu) onContextMenu(); });
        if (isOn)
            actions.addAction (juce::AccessibilityActionType::toggle, [this] { press(); });
        return std::make_unique<ButtonHandler> (*this, isOn ? juce::AccessibilityRole::toggleButton
                                                            : juce::AccessibilityRole::button, std::move (actions));
    }

private:
    void press()
    {
        if (onPress)
            onPress();
        if (isOn) // let the screen reader say "checked"/"not checked"
            if (auto* handler = getAccessibilityHandler())
                handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
        if (visual != nullptr)
            visual->repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActionOverlay)
};
} // namespace a11y
