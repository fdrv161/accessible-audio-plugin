#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "A11yKeys.h"
#include "ScreenReaderAnnouncer.h"

// ONE focusable component that stands for a long "virtual" list: slots, browser files,
// sequencer steps, preset names. The owner keeps the data; the cell asks for row text.
// - The screen reader always reads the current row (live title: NVDA+Up Arrow).
// - Moving the cursor speaks only the new row. Edges are silent (no wrap, no "end of list").
// Keys: Up/Down, Home/End (also Ctrl+Home/End), Page Up/Down (pageSize rows),
// letter = search forward, Shift+letter = backward, Enter = onActivate,
// Shift+F10 / Applications key = onContextMenu.
// Keys the cell does not use return false, so the editor decides (Tab, Escape, Space,
// shortcuts). The editor must consume everything it does not pass to the DAW on purpose
// (references/keyboard-model.md, "Key routing").
class AccessibleListCell : public juce::Component
{
public:
    std::function<int()> getCount;                        // number of rows
    std::function<juce::String (int)> getLine;            // full row text, as spoken
    std::function<juce::String (int)> getSearchName;      // name for letter search (default: getLine)
    std::function<void (int)> onCursorChanged;            // e.g. preview the selected file
    std::function<void (int)> onActivate;                 // Enter
    std::function<void (int)> onContextMenu;              // Shift+F10 / Applications key
    std::function<bool (const juce::KeyPress&)> onExtraKey; // screen-specific keys, called first

    juce::String emptyText { "Empty" }; // spoken row of an empty list ("Empty folder")
    int pageSize = 10;

    explicit AccessibleListCell (const juce::String& listName,
                                 juce::AccessibilityRole roleToUse = juce::AccessibilityRole::list)
        : role (roleToUse)
    {
        setName (listName);
        setWantsKeyboardFocus (true);
    }

    int getCursor() const noexcept { return cursor; }

    juce::String currentLine() const
    {
        const int count = rowCount();
        if (count <= 0 || ! getLine)
            return emptyText;
        return getLine (juce::jlimit (0, count - 1, cursor));
    }

    // speak = true for user actions; false for programmatic changes (silent).
    void setCursor (int index, bool speak)
    {
        const int count = rowCount();
        const int next = count > 0 ? juce::jlimit (0, count - 1, index) : 0;
        const bool changed = next != cursor;
        cursor = next;
        repaint();

        if (changed && onCursorChanged)
            onCursorChanged (cursor);
        if (changed && speak)
            a11y::announce (*this, currentLine());
    }

    // The list content changed (new folder, item deleted): keep the cursor in range
    // and optionally speak the current row.
    void refresh (bool speak)
    {
        const int count = rowCount();
        cursor = count > 0 ? juce::jlimit (0, count - 1, cursor) : 0;
        repaint();
        if (speak)
            a11y::announce (*this, currentLine());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (onExtraKey && onExtraKey (key))
            return true;

        const int count = rowCount();
        using KP = juce::KeyPress;

        if (a11y::is (key, KP::upKey))       { if (cursor > 0) setCursor (cursor - 1, true); return true; }
        if (a11y::is (key, KP::downKey))     { if (cursor + 1 < count) setCursor (cursor + 1, true); return true; }
        if (a11y::is (key, KP::pageUpKey))   { setCursor (cursor - pageSize, true); return true; }
        if (a11y::is (key, KP::pageDownKey)) { setCursor (cursor + pageSize, true); return true; }
        if (a11y::is (key, KP::homeKey) || a11y::is (key, KP::homeKey, true)) { setCursor (0, true); return true; }
        if (a11y::is (key, KP::endKey)  || a11y::is (key, KP::endKey, true))  { setCursor (count - 1, true); return true; }

        if (a11y::is (key, KP::returnKey))
        {
            if (count > 0 && onActivate)
                onActivate (cursor);
            return true;
        }

        if (a11y::isContextMenuKey (key))
        {
            if (count > 0 && onContextMenu)
                onContextMenu (cursor);
            return true;
        }

        if (const auto initial = a11y::searchCharacter (key))
        {
            const int direction = key.getModifiers().isShiftDown() ? -1 : 1;
            const int found = a11y::findByInitial (count, cursor, direction, initial, [this] (int i)
            {
                return getSearchName ? getSearchName (i) : (getLine ? getLine (i) : juce::String());
            });
            if (found >= 0)
                setCursor (found, true);
            return true; // no match: silent, like an edge
        }

        return false;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (16.0f);
        g.drawText (currentLine(), getLocalBounds().reduced (6), juce::Justification::centredLeft, true);
        if (hasKeyboardFocus (false))
            g.drawRect (getLocalBounds(), 2); // focus ring for sighted keyboard users
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        // The title is computed on every request, so NVDA+Up Arrow always reads the
        // current row without any events or timers. Nothing is ever written into
        // setTitle/setDescription, so no old message can "stick" (defect SPK-04).
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
    int rowCount() const { return getCount ? juce::jmax (0, getCount()) : 0; }

    juce::AccessibilityRole role;
    int cursor = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccessibleListCell)
};
