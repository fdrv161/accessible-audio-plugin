# Speech and focus

Contents: 1. Speech channels. 2. Title, description, help. 3. What to say.
4. Moving focus. 5. REAPER and NVDA specifics. 6. Rate and duplicates.

## 1. Speech channels

A screen reader learns about the plugin in three ways. Use the first one that fits;
use only one per event.

1. **Focus change.** The newly focused element is read automatically: name, role,
   value, description. Free and the most reliable.
2. **Value or state change of the focused element.** A JUCE Slider, ToggleButton,
   ComboBox, or a custom handler that raises
   `notifyAccessibilityEvent(AccessibilityEvent::valueChanged)`. The screen reader reads
   the new value (or "checked") by itself.
3. **Explicit announcement.** For what no focus or value change shows: results
   ("Bank loaded"), modes ("Preview On"), rows of a virtual list cell, errors.

### Explicit announcements on Windows

Use `a11y::announce` / `a11y::Announcer` from templates/ScreenReaderAnnouncer.

- It raises a UI Automation notification from a hidden 1x1 child window of the editor
  that exists only for that (the technique of OSARA and Surge XT). It does not depend on
  which element has focus, never changes a title or description, and the window never
  appears in navigation.
- NVDA and Narrator read UIA notifications. NVDA reads them only while the focus is in
  the same application (plugin window focused, or REAPER focused for an in-process
  plugin); a plugin running in a separate host process is silent while focus is in
  the host.
- JAWS and Dolphin ignore UIA notifications (OSARA's finding). The template detects them
  and uses OSARA's fallback: the text becomes the MSAA name of the focused window for
  a moment. Not yet verified inside JUCE windows: test with JAWS before promising it.
- `Speech::interrupt` (NotificationProcessing_MostRecent): new text replaces pending
  text; for navigation and values. `Speech::queue` (NotificationProcessing_All): queued
  after current speech; for results and errors.
- `Announcer::say` waits ~150 ms and queues: use it for a result that comes together
  with a focus move (screen readers cancel speech on focus change, so an earlier
  message would be lost). `sayLatest` coalesces fast repeats (key held down).

**Never on Windows:** `juce::AccessibilityHandler::postAnnouncement`. JUCE speaks it with
SAPI (ISpVoice): a separate voice with its own speed and volume, invisible to NVDA, JAWS
and braille (defect SPK-01). On macOS it is the right call (VoiceOver), and the template
uses it there. JUCE has no screen reader support on Linux.

**Avoid:** raising the notification from a JUCE component's own provider
(`getNativeImplementation`), the older approach: silent when that component is hidden or
not accessible, and it relies on JUCE internals (SPK-02).

## 2. Title, description, help

- **title** (`setTitle`, or `getTitle()` in a custom handler): what NVDA calls the
  name, read on focus and by NVDA+Up Arrow. Short, current state: "Slot 3, Kick",
  "Cutoff, 1200 Hz, Auto".
- **name** (`setName`): internal JUCE name, not read by screen readers (buttons fall back
  to their button text). Do not rely on it for accessibility.
- **description** (`setDescription`): read after name and role. Use it for the
  shortcut only: "Load Sample, button, Alt+O". Never for messages: they would be read
  every time focus returns (SPK-04).
- **help** (`setHelpText`): long key help; NVDA does not always read it. Duplicate
  important keys in the spoken short help (F1) and the HTML manual.

### Live title

When the text changes on its own (host automation, playback position), do not update
the title on a timer. Give the component its own AccessibilityHandler whose
`getTitle()` computes the text on every request (AccessibleListCell,
AccessibleParameterList). NVDA+Up Arrow then always reads the current value, with no
extra events.

### Slider values

A plain `juce::Slider` exposes a UIA Value (text from `getTextFromValue`) and a
RangeValue (number). Current NVDA prefers the text; other screen readers and old NVDA
versions may read the number or a percentage. For values with units ("+3 dB",
"1.2 kHz", "C sharp 4") expose a text-only value: TextValueSlider.h or
ParameterOverlay. At minimum set `textFromValueFunction` with units.

## 3. What to say

- **Order:** the important word first. "Cutoff, 1200 Hz", not "The filter parameter
  Cutoff has the value of 1200 hertz".
- **Navigation:** "Name, value". **Value change:** only the value.
  **Context change** (slot, page, mode): context + current row ("Slot 4, Snare. Level,
  -3 dB"; "F2, Sample Parameters. Velocity Low, 1").
- **Entering a screen:** focus speaks the element; add at most one short context phrase
  that the element does not say ("5 loaded slots").
- **Numbers:** no trailing zeros ("10%"); a sign for bipolar values ("+3 dB"); units
  that sound right; notes as "60 C 4", "61 C sharp 4" (never "#"); tempo sync as
  "1 bar", "eighth triplet".
- **Options:** by label ("Round Robin"), never by index.
- **Mixed state:** honestly, "Mixed values, increase by 3 dB".
- **No role words:** never "button", "checkbox", "selected item"; the screen reader adds
  roles.
- **ASCII or explicit UTF-8:** no "—", "•", "×" without `juce::String::fromUTF8`
  (SPK-08). A hyphen and commas sound better anyway.
- **Errors:** what happened and what to do. "Library unavailable: D:\Library. Connect
  the drive, or choose the Library Folder in Advanced."
- **Dangerous confirmations** name what will be lost: "Slot 3, Kick, is occupied.
  Replace it with Snare? Press Y or N."
- **Plugin language:** spoken strings in English unless the plugin is localised; keep
  them short and consistent across screens.

## 4. Moving focus

Recipe proven in REAPER:

```cpp
juce::AccessibilityHandler::clearCurrentlyFocusedHandler(); // only if focus may already "be" there
target.grabKeyboardFocus();
if (auto* h = target.getAccessibilityHandler())
    h->grabFocus(); // guarantees the UIA focus event even if JUCE thinks nothing changed
```

Rules:
- Move focus to the new place BEFORE hiding or disabling the focused component
  (FOC-02).
- Remember where focus came from (`SafePointer<Component> returnFocus`). Return to the
  button if a button opened the screen, to the main element (current slot) if a hotkey
  did (LSampler-24 TEST108). Check `isShowing() && isEnabled()`; otherwise use a fallback.
- Move focus after opening or closing a screen asynchronously
  (`MessageManager::callAsync` or `Timer::callAfterDelay(50, ...)`), always with a
  SafePointer, so JUCE and the host finish their own focus change first.
- Initial focus when the editor opens: after it is showing, never in the constructor
  (FOC-04).
- When focus leaves the plugin window (user back in the DAW), stop all previews: watch
  `hasKeyboardFocus(true)` of the editor in its timer (FOC-08).
- After rebuilding a container's children, raise `structureChanged` on it one tick
  later (FOC-09).
- Debugging: register a `juce::FocusChangeListener` with
  `juce::Desktop::getInstance().addFocusChangeListener(this)` and log every change
  (component title, type, whether it has an accessibility handler), as Surge XT does in
  its debug mode; or use scripts/uia_inspect.ps1 -Mode Watch.

## 5. REAPER and NVDA specifics

- **Deferred grabFocus.** In `focusGained` of the main grid, a 1 ms deferred
  `getAccessibilityHandler()->grabFocus()` made REAPER pass UIA focus into the plugin
  reliably (LSampler-24).
- **Mode name on entry (hack).** LSampler-24's grid briefly sets the title "Grid" (or
  "Value. Cutoff") on focus so NVDA announces the mode, then about 200 ms later the
  compact title "Cutoff, 1200 Hz" for row reading. Use only if users want the mode name.
- **Menus from the FX chain window.** A PopupMenu opened while REAPER's FX chain list has
  keyboard focus is visible, but keys and NVDA stay in REAPER. Defer opening until the
  editor `hasKeyboardFocus(true)` (DLG-05).
- **Menu window name.** Override `preparePopupMenuWindow` in your LookAndFeel and call
  `window.setTitle("Choose Help language")`; otherwise NVDA says just "menu".
- **Space.** On the main screen return false for Space so the DAW transport works
  (KEY-07).
- **"Send all keyboard input to plug-in"** (REAPER FX window option) helps when REAPER
  still grabs keys; mention it in the plugin help.

## 6. Rate and duplicates

- Do not repeat the same text twice in a row for status and telemetry
  (`Announcer::sayIfChanged`). A user's repeated key press may repeat the phrase.
- Progress of long operations: at most every 4 seconds, only while focus is in the
  plugin; the current percent is always in the status element's title (NVDA+Up Arrow).
- Host automation and meters are never announced; their values are available on
  demand (live title, properties).
- Telemetry shown as text (levels, analysis) is updated with a rate limit while focused,
  never announced per change.
