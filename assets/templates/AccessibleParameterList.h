#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "A11yKeys.h"
#include "A11yParameters.h"
#include "ScreenReaderAnnouncer.h"

// Accessible editor for ALL processor parameters as one focusable element (a simplified
// "Grid / Value" model from LSampler-24). Use it as the main editor of a new plugin, or as
// an "accessible page" on top of an inaccessible GUI (fastest retrofit).
//
// Keys:
//   Up/Down              parameter within the group (edges are silent)
//   Left/Right           previous/next group, keeping the row when possible
//   Home/End             first/last in the group; Ctrl+Home/End: whole list
//   Page Up/Down         8 rows within the group
//   Alt+Up/Down          value by one step; Shift+Alt+Up/Down: fine step (continuous only)
//   Alt+Page Up/Down     coarse step
//   Alt+Home/End         maximum/minimum
//   Backspace            default value
//   Enter                exact entry (Enter applies, Escape cancels)
//   letter / Shift+letter  search by the name the user hears, forward/backward
//   Shift+F10 / Applications key  onContextMenu (e.g. host automation, MIDI learn)
// Unused keys return false: the editor decides and consumes them (keyboard-model.md).
//
// Speech: navigation says "Name, value"; a value change says only the value; host
// automation is never spoken (the live title answers NVDA+Up Arrow).
class AccessibleParameterList : public juce::Component, private juce::Timer
{
public:
    struct Entry
    {
        juce::RangedAudioParameter* parameter = nullptr;
        juce::String group;
        juce::String label; // spoken/searched name; empty = parameter->getName (128)
    };

    std::function<void (juce::RangedAudioParameter&)> onContextMenu;

    // Collect parameters from the processor tree; group = nearest sub-group name.
    static std::vector<Entry> fromProcessor (juce::AudioProcessor& processor)
    {
        std::vector<Entry> result;
        collect (processor.getParameterTree(), "Parameters", result);
        return result;
    }

    explicit AccessibleParameterList (std::vector<Entry> entriesToUse)
        : entries (std::move (entriesToUse))
    {
        setName ("Parameters");
        setWantsKeyboardFocus (true);

        // Exact entry field: a plain TextEditor, readable by every screen reader.
        addChildComponent (entryField);
        entryField.setMultiLine (false);
        entryField.setSelectAllWhenFocused (true);
        entryField.setEscapeAndReturnKeysConsumed (true);
        entryField.onReturnKey = [this] { finishEntry (true); };
        entryField.onEscapeKey = [this] { finishEntry (false); };
        entryField.onFocusLost = [this] { if (entryField.isVisible()) finishEntry (false); };

        startTimerHz (10);
    }

    juce::String nameOf (size_t index) const
    {
        const auto& e = entries[index];
        return e.label.isNotEmpty() ? e.label : e.parameter->getName (128);
    }

    juce::String currentLine() const
    {
        if (entries.empty())
            return "No parameters";
        return nameOf ((size_t) cursor) + ", " + a11y::valueText (*current());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (entryField.isVisible())
            return true; // keys the TextEditor did not take go nowhere

        if (entries.empty())
            return false;

        using KP = juce::KeyPress;

        // Value changes (Alt). Checked before plain navigation; each test matches the
        // exact modifiers, so Shift+Alt+Up never runs the Alt+Up branch (TEST157).
        if (a11y::is (key, KP::upKey,   false, true, true)) { step (1,  a11y::StepSize::fine);   return true; }
        if (a11y::is (key, KP::downKey, false, true, true)) { step (-1, a11y::StepSize::fine);   return true; }
        if (a11y::is (key, KP::upKey,       false, true))   { step (1,  a11y::StepSize::normal); return true; }
        if (a11y::is (key, KP::downKey,     false, true))   { step (-1, a11y::StepSize::normal); return true; }
        if (a11y::is (key, KP::pageUpKey,   false, true))   { step (1,  a11y::StepSize::coarse); return true; }
        if (a11y::is (key, KP::pageDownKey, false, true))   { step (-1, a11y::StepSize::coarse); return true; }
        if (a11y::is (key, KP::homeKey,     false, true))   { setNormalised (1.0f);    return true; }
        if (a11y::is (key, KP::endKey,      false, true))   { setNormalised (0.0f);    return true; }

        if (a11y::is (key, KP::homeKey, true)) { select (0); return true; }
        if (a11y::is (key, KP::endKey,  true)) { select ((int) entries.size() - 1); return true; }

        const int begin = groupBegin (cursor), end = groupEnd (cursor);
        if (a11y::is (key, KP::upKey))        { if (cursor > begin) select (cursor - 1); return true; }
        if (a11y::is (key, KP::downKey))      { if (cursor < end)   select (cursor + 1); return true; }
        if (a11y::is (key, KP::homeKey))      { select (begin); return true; }
        if (a11y::is (key, KP::endKey))       { select (end);   return true; }
        if (a11y::is (key, KP::pageUpKey))    { select (juce::jmax (begin, cursor - 8)); return true; }
        if (a11y::is (key, KP::pageDownKey))  { select (juce::jmin (end, cursor + 8));   return true; }
        if (a11y::is (key, KP::leftKey))      { moveGroup (-1); return true; }
        if (a11y::is (key, KP::rightKey))     { moveGroup (1);  return true; }
        if (a11y::is (key, KP::returnKey))    { beginEntry(); return true; }
        if (a11y::is (key, KP::backspaceKey)) { setNormalised (current()->getDefaultValue()); return true; }

        if (a11y::isContextMenuKey (key))
        {
            if (onContextMenu)
                onContextMenu (*current());
            return true;
        }

        if (const auto initial = a11y::searchCharacter (key))
        {
            const int direction = key.getModifiers().isShiftDown() ? -1 : 1;
            const int found = a11y::findByInitial ((int) entries.size(), cursor, direction, initial,
                                                   [this] (int i) { return nameOf ((size_t) i); });
            if (found >= 0)
            {
                if (entries[(size_t) found].group != entries[(size_t) cursor].group)
                    selectWithGroup (found);
                else
                    select (found);
            }
            return true;
        }

        return false;
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
        entryField.setBounds (getLocalBounds().reduced (6).removeFromBottom (28));
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        // Live title: always "Name, value" at the moment of the request, including automation.
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
                out.push_back ({ ranged, groupName, {} });
        }
    }

    juce::RangedAudioParameter* current() const { return entries[(size_t) cursor].parameter; }

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
            return; // edge: silent
        cursor = index;
        repaint();
        a11y::announce (*this, currentLine());
    }

    void moveGroup (int direction)
    {
        // Into the neighbouring group, keeping the row number when that group has it.
        const int begin = groupBegin (cursor), end = groupEnd (cursor);
        const int row = cursor - begin;
        if (direction < 0 && begin > 0)
            selectWithGroup (juce::jmin (groupBegin (begin - 1) + row, begin - 1));
        else if (direction > 0 && end + 1 < (int) entries.size())
            selectWithGroup (juce::jmin (end + 1 + row, groupEnd (end + 1)));
    }

    void selectWithGroup (int index)
    {
        cursor = juce::jlimit (0, (int) entries.size() - 1, index);
        repaint();
        a11y::announce (*this, entries[(size_t) cursor].group + ". " + currentLine());
    }

    void step (int direction, a11y::StepSize size)
    {
        setNormalised (a11y::steppedValue (*current(), direction, size));
    }

    void setNormalised (float normalised)
    {
        const bool changed = a11y::setByUser (*current(), normalised);
        repaint();
        if (changed)
            a11y::announce (*this, a11y::valueText (*current())); // only the value; edge: silent
    }

    void beginEntry()
    {
        auto* p = current();
        entryField.setTitle (nameOf ((size_t) cursor) + ", type a value");
        entryField.setText (p->getCurrentValueAsText(), false);
        entryField.setVisible (true);
        entryField.grabKeyboardFocus();
        entryField.selectAll();
    }

    void finishEntry (bool apply)
    {
        const auto text = entryField.getText();
        // Hide the field and return focus asynchronously: this runs inside the field's own
        // key handling, and hiding a focused component there can crash hosts (RT-04).
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<AccessibleParameterList> (this), text, apply]
        {
            if (safeThis == nullptr || ! safeThis->entryField.isVisible())
                return;

            bool valid = true;
            if (apply)
            {
                float normalised = 0.0f;
                valid = a11y::parseTypedValue (*safeThis->current(), text, normalised);
                if (valid) // silent: the focus speech below already reads "Name, new value"
                    a11y::setByUser (*safeThis->current(), normalised);
            }

            // Focus first, then hide the field: never hide the focused component (FOC-02).
            safeThis->grabKeyboardFocus();
            if (auto* handler = safeThis->getAccessibilityHandler())
                handler->grabFocus(); // force the UIA focus event (REAPER sometimes misses it)
            safeThis->entryField.setVisible (false);
            safeThis->repaint();

            if (! valid)
                safeThis->announcer.say ("Invalid value: " + text.trim() + ". Type a number or an option name.");
        });
    }

    // Host automation changes values: repaint, but never speak.
    void timerCallback() override
    {
        if (isShowing() && currentLine() != lastPaintedLine)
            repaint();
    }

    std::vector<Entry> entries;
    int cursor = 0;
    juce::String lastPaintedLine;
    juce::TextEditor entryField;
    a11y::Announcer announcer { *this }; // delayed: speaks after the focus-change speech

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccessibleParameterList)
};

// Use in the plugin editor:
//   list = std::make_unique<AccessibleParameterList> (AccessibleParameterList::fromProcessor (processor));
//   addAndMakeVisible (*list);
//   // after setSize() in the editor constructor, focus the list once the window is on screen:
//   juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Editor> (this)]
//   {
//       if (safe != nullptr && safe->list->isShowing())
//       {
//           safe->list->grabKeyboardFocus();
//           if (auto* h = safe->list->getAccessibilityHandler()) h->grabFocus();
//       }
//   });
