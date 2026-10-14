#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Keyboard helpers shared by all templates. Rules behind them: references/keyboard-model.md.
namespace a11y
{
// Exact modifier match. "cmd" means Ctrl on Windows/Linux and Command on macOS.
// JUCE reports isCommandDown() == true whenever Ctrl is held on Windows, so the
// classic "isCtrlDown() && ! isCommandDown()" is ALWAYS false there (defect KEY-01).
inline bool modsAre (juce::ModifierKeys m, bool cmd, bool alt, bool shift)
{
    return m.isCommandDown() == cmd && m.isAltDown() == alt && m.isShiftDown() == shift
          #if JUCE_MAC
           && ! m.isCtrlDown() // physical Control is a separate key on macOS
          #endif
           ;
}

inline bool isPlain (const juce::KeyPress& k) { return modsAre (k.getModifiers(), false, false, false); }

// Key code + exact modifiers. Letters compare case-insensitively.
inline bool is (const juce::KeyPress& k, int code, bool cmd = false, bool alt = false, bool shift = false)
{
    const auto a = k.getKeyCode(), b = code;
    const bool sameKey = a == b
        || (a < 0x10000 && b < 0x10000
            && juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) a)
               == juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) b));
    return sameKey && modsAre (k.getModifiers(), cmd, alt, shift);
}

// Shift+F10, or the Applications (context menu) key. JUCE on Windows reports the
// Applications key as code 0x5D with no text character; ']' has the same code but
// types a character, so the text check tells them apart. Verify on your JUCE version
// with the keyboard diagnostic from references/testing.md.
inline bool isContextMenuKey (const juce::KeyPress& k)
{
    if (is (k, juce::KeyPress::F10Key, false, false, true))
        return true;
    return k.getKeyCode() == 0x5D && k.getTextCharacter() == 0 && isPlain (k);
}

// Lower-case character for first-letter search, or 0 if the key is not a search key.
// - Uses the typed character first: navigation and F-key codes overlap printable ASCII
//   on some backends ('&' == Up, 'p' == F1), so "code >= 33 && code <= 126" breaks
//   arrows and F-keys (LSampler-24 TEST40, TEST134).
// - Some hosts deliver digits and punctuation with no text character: fall back to the
//   key code for those keys only (TEST135).
// - Shift+digit types '!': for backward search (Shift+key) take the letter/digit from the code.
inline juce::juce_wchar searchCharacter (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (m.isCtrlDown() || m.isAltDown() || m.isCommandDown())
        return 0;

    const int code = k.getKeyCode();
    if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F12Key)
        return 0;
    if (isContextMenuKey (k))
        return 0;

    const bool letterOrDigit = (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z') || (code >= '0' && code <= '9');
    if (m.isShiftDown() && letterOrDigit)
        return juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);

    const auto typed = k.getTextCharacter();
    if (typed >= 33 && typed != 127 && typed < 0x110000)
        return juce::CharacterFunctions::toLowerCase (typed);

    if (typed == 0 && code < 0x10000
        && juce::String (".,-_/\\;:[=+'`0123456789").containsChar ((juce::juce_wchar) code))
        return (juce::juce_wchar) code;

    return 0;
}

// Index of the next item whose name starts with `initial`, searching from `current`
// in `direction` (+1 forward, -1 backward) with wrap-around, or -1 if none.
// `nameOf` must return the name the user HEARS (not a host or internal name, TEST129),
// without repeated prefixes such as "Loop 3, " or "Slice ".
template <typename NameFn>
int findByInitial (int count, int current, int direction, juce::juce_wchar initial, NameFn&& nameOf)
{
    if (count <= 0 || initial == 0)
        return -1;
    const auto target = juce::CharacterFunctions::toLowerCase (initial);
    for (int distance = 1; distance <= count; ++distance)
    {
        int index = (current + direction * distance) % count;
        if (index < 0)
            index += count;
        const juce::String name = nameOf (index);
        if (name.isNotEmpty() && juce::CharacterFunctions::toLowerCase (name[0]) == target)
            return index;
    }
    return -1;
}
} // namespace a11y
