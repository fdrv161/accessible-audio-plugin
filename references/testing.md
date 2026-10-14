# Verification: automatic and manual

Contents: 1. Key routing tests. 2. Static checks. 3. Accessibility tree check.
4. Keyboard diagnostic mode. 5. Focus log. 6. Manual plan with NVDA and REAPER.
7. Iteration note. 8. Bug report from a user.

Be honest in reports: say which items were verified by a test, a script, reading code,
or not at all. Speech in a real host is verified only by a person with a screen reader.

## 1. Key routing tests

Speech cannot be tested in CI, key routing can: create the processor and editor in a
console test and call `keyPressed` directly.

```cpp
MyAudioProcessor p;
auto editor = std::make_unique<MyAudioProcessorEditor> (p);
auto key = [] (int code, int mods = 0, juce::juce_wchar ch = 0)
{ return juce::KeyPress (code, juce::ModifierKeys (mods), ch); };

require (! editor->keyPressed (key (juce::KeyPress::spaceKey), slotCell),
         "On the main page Space belongs to the host transport");
require (editor->keyPressed (key (juce::KeyPress::returnKey), slotCell), "Enter opens parameters");
require (editor->keyPressed (key ('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::commandModifier, 'z'), grid),
         "Grid consumes unrelated host Ctrl commands");
require (editor->keyPressed (key (juce::KeyPress::escapeKey), grid), "Escape returns to slots");
require (grid.getAccessibilityHandler()->getTitle() == "Cutoff, 1200 Hz", "Row text after navigation");
```

On Windows a real Ctrl press sets both ctrlModifier and commandModifier: build test key
presses the same way, or the test misses KEY-01 bugs.

What to test:
- which keys are consumed and which go to the DAW on purpose;
- navigation reaches the right element (index, category);
- Escape returns one level up;
- edges: a second press at the edge changes nothing;
- the element's title after an action equals the expected row (what NVDA+Up Arrow reads);
- specific shortcuts win over generic ones (Shift+Alt+Up vs Alt+Up, KEY-05).

## 2. Static checks

- `python scripts/a11y_audit.py Source CMakeLists.txt` (or `--format json` to process
  the findings). Rule IDs = audit-checklist.md. `--list-rules` prints all rules.
  Mark a reviewed false positive in code: `// a11y-ignore: RT-02 runs synchronously`.
- Small guard scripts for bugs that were fixed once (LSampler-24:
  `verify-test114-ctrl-shift-a.py` checks Ctrl+Shift+A conditions have no
  `!isCommandDown()`). Cheap, and the bug cannot come back.
- Parameter contract script: count, unique IDs and hashes, order against the manifest, no
  forbidden IDs (or `a11y_audit.py --param-ids manifest.json`).
- Audio regression when DSP was touched: bit-exact comparison with a frozen reference
  render (LSampler-24 keeps the TEST59 copy with SHA-256).

## 3. Accessibility tree check (Windows)

`scripts/uia_inspect.ps1` reads the same UI Automation data NVDA reads, with nothing to
install. Fastest with a Standalone build of the plugin (add `Standalone` to FORMATS), or
with the plugin open in REAPER.

```bash
powershell -ExecutionPolicy Bypass -File scripts/uia_inspect.ps1 -Mode List
```

```bash
powershell -ExecutionPolicy Bypass -File scripts/uia_inspect.ps1 -Mode Tree -Process reaper
```

```bash
powershell -ExecutionPolicy Bypass -File scripts/uia_inspect.ps1 -Mode Watch -Seconds 90 -Delay 5 -LogFile focus-log.txt
```

- Tree: every element as "role, name, value, states", with PROBLEM for focusable
  elements without a name (CTL-01) and NOTE for number-only values (CTL-02) and duplicate
  names (CTL-04). Exit code 1 if there are problems.
- Watch: every focus change and every change of the focused element's name or value with
  a time stamp, roughly what NVDA announces. Ask the user to run it while reproducing a
  problem and send the log.
- Not visible to it: JUCE descriptions (UIA FullDescription) and notifications (the
  announcements). Those need NVDA's Speech Viewer.

## 4. Keyboard diagnostic mode

When a key "does nothing" in some host, let the user tell you what JUCE receives
(LSampler-24 TEST136 to TEST138). Add temporarily, remove after the fix:

```cpp
// Temporary: F11 toggles a mode that speaks what JUCE receives for each key.
bool keyDiagnostic = false;

bool handleDiagnostic (const juce::KeyPress& key)
{
    if (a11y::is (key, juce::KeyPress::F11Key))
    {
        keyDiagnostic = ! keyDiagnostic;
        a11y::announce (*this, keyDiagnostic ? "Key diagnostic on" : "Key diagnostic off");
        return true;
    }
    if (! keyDiagnostic)
        return false;
    const auto m = key.getModifiers();
    const auto typed = key.getTextCharacter();
    a11y::announce (*this, "Code " + juce::String (key.getKeyCode())
        + ", text " + juce::String ((int) typed)
        + (typed >= 32 && typed < 127 ? " " + juce::String::charToString (typed) : juce::String())
        + ", search " + juce::String ((int) a11y::searchCharacter (key))
        + ", ctrl " + juce::String ((int) m.isCtrlDown()) + ", command " + juce::String ((int) m.isCommandDown())
        + ", alt " + juce::String ((int) m.isAltDown()) + ", shift " + juce::String ((int) m.isShiftDown()));
    return false; // let the normal handler run too, so the user hears both
}
```

Pick a toggle key that is known to arrive (LSampler-24 used F11 after Ctrl+Alt+Shift+D
was not delivered in some cases).

## 5. Focus log

For "focus jumps somewhere" bugs, log every focus change in a debug build (Surge XT has
the same switch):

```cpp
struct FocusLogger : juce::FocusChangeListener
{
    FocusLogger()  { juce::Desktop::getInstance().addFocusChangeListener (this); }
    ~FocusLogger() override { juce::Desktop::getInstance().removeFocusChangeListener (this); }
    void globalFocusChanged (juce::Component* c) override
    {
        DBG ("Focus: " << (c ? c->getTitle() + " [" + typeid (*c).name() + "]"
                                 + (c->getAccessibilityHandler() ? "" : " NOT ACCESSIBLE")
                             : juce::String ("none")));
    }
};
```

## 6. Manual plan with NVDA and REAPER

With NVDA (and if possible JAWS and Narrator), REAPER with OSARA. Each step has an
expected phrase; NVDA's Speech Viewer shows exactly what was said.

1. Insert the plugin, open the window (FX chain and floating). NVDA names the first
   element; focus is inside the plugin.
2. Tab/Shift+Tab through everything: each element has a name, role and shortcut; no
   "unknown" or unnamed elements.
3. Arrows in the main list: short clear rows; edges silent.
4. NVDA+Up Arrow on every screen: the current row, no old messages.
5. Change a value: only the new value, once.
6. Every modal screen: open by hotkey and by button, work, Escape: focus back (to the
   button after a button, to the main element after a hotkey).
7. Unused letters, Ctrl+Z, Ctrl+S, Space inside modal screens: REAPER does nothing.
8. Space on the main screen: REAPER transport starts (if that is the design).
9. Long operation: start phrase, percent by NVDA+Up Arrow, Alt+Tab away during the
   operation (the plugin does not talk from the background), back: result message and
   focus where it was.
10. Close the plugin window during an operation and open it again.
11. REAPER / OSARA parameter list: names, values, typing a value.
12. Ctrl+Shift+A (if present): envelope on/off, row says "Auto" or not; two instances on
    different tracks.
13. Leave the plugin during preview: the sound stops.
14. CLAP build: steps 1 to 5 and 11.
15. Save, close and reopen the project: values and automation intact.
16. Shift+F10 on every control that has a right-click menu.
17. Every dialog: first focus, Enter, Escape, no double reading.

## 7. Iteration note

Every change gets a short text file (LSampler-24: `TESTnnn_..._NOTE.txt`):

```text
<Plugin> TEST<number> - <one line>

Base: TEST<previous>.

Changes:
- <what changed, with keys and exact phrases>

Not changed:
- DSP, parameter IDs and order, preset format, other shortcuts.

Verified:
- <test/script: PASS> | Not compiled locally (build via GitHub Actions).

Test in <DAW> with <screen reader>:
1. <step> -> expected: "<phrase>"
2. ...
```

"Not changed" is not a formality: it tells the user what does not need re-testing.

## 8. Bug report from a user

Ask for (from ToneTrace, extended):
- Windows or macOS version, DAW and version, plugin version and format (VST3/CLAP),
  window docked or floating;
- screen reader and version, OSARA version;
- plugin window or host parameter list;
- steps, what they expected to hear, what they heard (Speech Viewer text);
- for a wrong element: NVDA+F1 developer info from the NVDA log;
- if possible, a uia_inspect.ps1 Watch log of the same steps.
