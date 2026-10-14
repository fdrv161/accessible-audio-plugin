#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "A11yKeys.h"
#include "ScreenReaderAnnouncer.h"

// Base for a modal screen inside the plugin window (browser, editor, properties, About).
// - Remembers where focus was and returns it on close (to the opening button if opened
//   by a button, or to a target you pass, e.g. the main list when opened by a hotkey).
// - Tab/Shift+Tab cycle only inside the screen.
// - Escape closes asynchronously (never delete or hide a component inside its own
//   keyPressed: defect RT-03).
// - Every other key, including key releases, is consumed: nothing leaks to the DAW
//   (REAPER would run its own actions). Alt+F4 is left to the system.
// - Exposed as a named group: when focus enters, NVDA says the screen name once
//   ("Properties grouping"), so usually no extra entry announcement is needed.
// Subclasses override handleSurfaceKey() and firstFocusTarget().
class ModalKeySurface : public juce::Component
{
public:
    std::function<void()> onClosed; // after close and focus return

    explicit ModalKeySurface (const juce::String& screenName)
    {
        setTitle (screenName);
        setWantsKeyboardFocus (true);
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    }

    // Show on top of `parent`. `returnTo` = where focus goes on close (default: the
    // component focused right now). `entryText` = optional extra phrase spoken after the
    // focus lands ("5 loaded slots"); do not repeat what the focused element says itself.
    void open (juce::Component& parent, juce::Component* returnTo = nullptr, const juce::String& entryText = {})
    {
        returnFocus = returnTo != nullptr ? returnTo : juce::Component::getCurrentlyFocusedComponent();
        parent.addAndMakeVisible (*this);
        setBounds (parent.getLocalBounds());
        toFront (false);
        isOpen = true;

        // Let JUCE and the host finish their own focus change first, then set ours.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<ModalKeySurface> (this), entryText]
        {
            if (safeThis == nullptr || ! safeThis->isOpen)
                return;
            auto* target = safeThis->firstFocusTarget();
            moveFocusTo (target != nullptr ? *target : *safeThis);
            if (entryText.isNotEmpty())
                a11y::announce (*safeThis, entryText, a11y::Speech::queue); // after the focus speech
        });
    }

    void requestClose()
    {
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<ModalKeySurface> (this)]
        {
            if (safeThis != nullptr)
                safeThis->closeNow();
        });
    }

    bool isShowingModal() const noexcept { return isOpen; }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (handleSurfaceKey (key))
            return true;

        if (a11y::is (key, juce::KeyPress::escapeKey))
        {
            requestClose();
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::tabKey && ! key.getModifiers().isCtrlDown() && ! key.getModifiers().isAltDown())
        {
            const bool forwards = ! key.getModifiers().isShiftDown();
            auto* focused = juce::Component::getCurrentlyFocusedComponent();
            if (focused != nullptr && (focused == this || isParentOf (focused)))
                focused->moveKeyboardFocusToSibling (forwards); // wraps inside this focus container
            else if (auto* target = firstFocusTarget())
                moveFocusTo (*target);
            return true;
        }

        if (a11y::is (key, juce::KeyPress::F4Key, false, true))
            return false; // Alt+F4 must always close the window

        return true; // the modal screen owns the whole keyboard
    }

    // Key releases must not reach the DAW either.
    bool keyStateChanged (bool) override { return isOpen; }

protected:
    // Screen-specific keys. Return true if handled.
    virtual bool handleSurfaceKey (const juce::KeyPress&) { return false; }

    // Component that receives focus on open (first useful control).
    virtual juce::Component* firstFocusTarget() { return nullptr; }

    static void moveFocusTo (juce::Component& target)
    {
        target.grabKeyboardFocus();
        // Explicit UIA focus event: in REAPER NVDA sometimes misses the move without it.
        if (auto* handler = target.getAccessibilityHandler())
            handler->grabFocus();
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
    }

private:
    void closeNow()
    {
        if (! isOpen)
            return;
        isOpen = false;

        auto target = returnFocus;
        returnFocus = nullptr;

        // Move focus BEFORE hiding: a hidden focused component means silence and keys
        // going to the DAW (defect FOC-02).
        const bool canReturn = target != nullptr && target->isShowing() && target->isEnabled()
                               && target != this && ! isParentOf (target);
        if (canReturn)
        {
            // JUCE may think focus "did not change" and skip the UIA event; clear it first.
            juce::AccessibilityHandler::clearCurrentlyFocusedHandler();
            moveFocusTo (*target);
        }

        auto* parent = getParentComponent();
        setVisible (false);
        if (parent != nullptr)
            parent->removeChildComponent (this);

        // Fallback: JUCE gives focus to the parent's first focusable child.
        // Better: pass a valid returnTo to open().
        if (! canReturn && parent != nullptr)
            parent->grabKeyboardFocus();

        if (onClosed)
            onClosed();
    }

    juce::Component::SafePointer<juce::Component> returnFocus;
    bool isOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModalKeySurface)
};
