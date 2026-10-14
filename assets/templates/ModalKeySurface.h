#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "ScreenReaderAnnouncer.h"

// База модального экрана внутри окна плагина (браузер, редактор, свойства, About).
// Что даёт:
//  - запоминает, где был фокус, и возвращает его при закрытии;
//  - Tab/Shift+Tab ходят только внутри экрана;
//  - Escape закрывает (асинхронно: нельзя удалять/прятать компонент в его же keyPressed);
//  - все прочие клавиши, включая отпускание, поглощаются и не уходят в DAW (REAPER).
// Наследник переопределяет handleSurfaceKey() и firstFocusTarget().
class ModalKeySurface : public juce::Component
{
public:
    std::function<void()> onClosed; // вызывается после закрытия и возврата фокуса

    ModalKeySurface()
    {
        setWantsKeyboardFocus (true);
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    }

    // Показать поверх parent. entryText — вводная фраза («Properties. Audio File. Name: Kick»);
    // если пусто, скринридер просто прочитает элемент, получивший фокус.
    void open (juce::Component& parent, const juce::String& entryText = {})
    {
        returnFocus = juce::Component::getCurrentlyFocusedComponent();
        parent.addAndMakeVisible (*this);
        setBounds (parent.getLocalBounds());
        toFront (false);
        isOpen = true;

        // Даём JUCE и хосту закончить текущую смену фокуса, потом ставим свой.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<ModalKeySurface> (this), entryText]
        {
            if (safeThis == nullptr || ! safeThis->isOpen)
                return;
            auto* target = safeThis->firstFocusTarget();
            if (target == nullptr)
                target = safeThis.getComponent();
            moveFocusTo (*target);
            if (entryText.isNotEmpty())
                a11y::announce (*target, entryText);
        });
    }

    void requestClose()
    {
        // Закрываем асинхронно: requestClose часто вызывается из keyPressed этого же экрана.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<ModalKeySurface> (this)]
        {
            if (safeThis != nullptr)
                safeThis->closeNow();
        });
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (handleSurfaceKey (key))
            return true;

        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            requestClose();
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::tabKey)
        {
            const bool forwards = ! key.getModifiers().isShiftDown();
            auto* focused = juce::Component::getCurrentlyFocusedComponent();
            if (focused != nullptr && (focused == this || isParentOf (focused)))
                focused->moveKeyboardFocusToSibling (forwards);
            else if (auto* target = firstFocusTarget())
                moveFocusTo (*target);
            return true;
        }

        // Alt+F4 оставляем системе: пользователь должен всегда мочь закрыть окно.
        if (key.getKeyCode() == juce::KeyPress::F4Key && key.getModifiers().isAltDown())
            return false;

        return true; // модальный экран владеет всей клавиатурой
    }

    // Отпускание клавиш тоже не должно доходить до DAW.
    bool keyStateChanged (bool) override { return isOpen; }

protected:
    // Клавиши конкретного экрана. Вернуть true, если клавиша обработана.
    virtual bool handleSurfaceKey (const juce::KeyPress&) { return false; }

    // Элемент, который получает фокус при открытии.
    virtual juce::Component* firstFocusTarget() { return nullptr; }

    static void moveFocusTo (juce::Component& target)
    {
        target.grabKeyboardFocus();
        // Явное событие фокуса UIA: в REAPER без него NVDA иногда не замечает переход.
        if (auto* handler = target.getAccessibilityHandler())
            handler->grabFocus();
    }

private:
    void closeNow()
    {
        if (! isOpen)
            return;
        isOpen = false;

        auto target = returnFocus;
        returnFocus = nullptr;
        setVisible (false);
        if (auto* parent = getParentComponent())
            parent->removeChildComponent (this);

        if (target != nullptr && target->isShowing() && target->isEnabled())
        {
            juce::AccessibilityHandler::clearCurrentlyFocusedHandler();
            moveFocusTo (*target);
        }

        if (onClosed)
            onClosed();
    }

    juce::Component::SafePointer<juce::Component> returnFocus;
    bool isOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModalKeySurface)
};
