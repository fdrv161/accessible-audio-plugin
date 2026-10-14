#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Asynchronous JUCE AlertWindow dialogs that behave well with NVDA in DAW hosts
// (lessons from LSampler-24 TEST124/TEST125). Never use synchronous dialogs in a plugin
// (runModalLoop, showMessageBox): defect DLG-01.
//
// Problems fixed here:
//  - AlertWindow::addButton() gives buttons focus order 1, so "OK" gets the first focus
//    instead of the text field, and NVDA announces OK and then the field.
//  - AlertWindow exposes its prompt both as its own description and through internal
//    Labels: NVDA reads the prompt (and sometimes the preset name) twice.
//  - Escape/Return travelling from a focused TextEditor up to the AlertWindow is
//    unreliable in some hosts: the field handles them itself.
//  - A second grabKeyboardFocus() after enterModalState() causes a transient extra
//    focus announcement: rely on the modal's own first focus + explicit focus order.
// Focus return after the dialog is the caller's job: do it in the callback (it runs
// asynchronously, after the dialog is gone) and check that the target is still showing.
namespace a11y
{
// Ask for a name or other short text. Focus starts in the field with the text selected.
// Enter = OK, Escape = Cancel. onResult (true, text) or onResult (false, {}).
inline void askForText (const juce::String& title,       // "Save Slot", "Rename folder"
                        const juce::String& fieldLabel,  // "File name"
                        const juce::String& initialText,
                        std::function<void (bool, juce::String)> onResult)
{
    auto* dialog = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("text", initialText, fieldLabel);
    dialog->addButton ("OK", 1);
    dialog->addButton ("Cancel", 0);

    dialog->setTitle (title);
    dialog->setDescription ({}); // the title is enough; a description would be read again
    for (auto* child : dialog->getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            label->setAccessible (false); // still drawn for sighted users

    juce::Component::SafePointer<juce::AlertWindow> safeDialog (dialog);
    if (auto* field = dialog->getTextEditor ("text"))
    {
        field->setTitle (fieldLabel);
        field->setExplicitFocusOrder (1);
        field->setSelectAllWhenFocused (true);
        field->setEscapeAndReturnKeysConsumed (true);
        field->onReturnKey = [safeDialog] { if (safeDialog != nullptr) safeDialog->exitModalState (1); };
        field->onEscapeKey = [safeDialog] { if (safeDialog != nullptr) safeDialog->exitModalState (0); };
    }
    if (auto* ok = dialog->getButton ("OK"))         { ok->setExplicitFocusOrder (2);     ok->setDescription ({}); }
    if (auto* cancel = dialog->getButton ("Cancel")) { cancel->setExplicitFocusOrder (3); cancel->setDescription ({}); }

    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safeDialog, onResult] (int result)
    {
        // The dialog still exists while the callback runs; it is deleted right after.
        const auto text = safeDialog != nullptr ? safeDialog->getTextEditorContents ("text").trim() : juce::String();
        if (onResult)
            onResult (result == 1 && text.isNotEmpty(), result == 1 ? text : juce::String());
    }), true);
}

// Yes/No confirmation for destructive actions. Focus starts on "No" (the safe choice);
// Y, N and Escape work. The message must name what will be lost and end with
// "Press Y or N." ("Slot 3, Kick, is occupied. Replace it with Snare? Press Y or N.").
// If NVDA reads the message twice in your host, hide the internal Labels as in askForText.
inline void confirm (const juce::String& title, const juce::String& message, std::function<void (bool)> onResult)
{
    auto* dialog = new juce::AlertWindow (title, message, juce::MessageBoxIconType::WarningIcon);
    dialog->addButton ("Yes", 1, juce::KeyPress ('y'));
    dialog->addButton ("No", 0, juce::KeyPress ('n'), juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->setTitle (title);
    if (auto* no = dialog->getButton ("No"))   no->setExplicitFocusOrder (1);
    if (auto* yes = dialog->getButton ("Yes")) yes->setExplicitFocusOrder (2);

    // If focus does not land on "No" in your host, grab it explicitly right after
    // enterModalState (LSampler-24's confirm dialog does that and works in REAPER).
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([onResult] (int result)
    {
        if (onResult)
            onResult (result == 1);
    }), true);
}
} // namespace a11y
