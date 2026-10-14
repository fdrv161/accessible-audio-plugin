---
name: accessible-audio-plugin
description: >
  Build, fix and audit VST3/CLAP/AU audio plugins in JUCE (C++) so blind musicians can
  use them fully with NVDA, JAWS, Narrator, VoiceOver and REAPER + OSARA: keyboard-only
  control, focus that never gets lost, short spoken feedback, readable host parameters
  and automation. Use whenever a plugin, VST, VST3, CLAP, AU, JUCE editor, DAW or REAPER
  comes up together with accessibility, a11y, screen readers, NVDA, JAWS, Narrator,
  OSARA, blind users, keyboard navigation or speech, even indirectly: "NVDA is silent in
  my plugin", "focus jumps to REAPER", "a Windows voice speaks instead of NVDA", or in
  Russian «NVDA молчит в плагине», «сделай синт для незрячих», «фокус улетает в REAPER»,
  «сообщения читает только Экранный диктор», «доступность плагина», «скринридер».
  Also use to review JUCE code for accessibility and to retrofit an inaccessible
  custom-drawn GUI.
---

# Accessible audio plugins (JUCE, VST3/CLAP)

Goal: a blind musician does everything a sighted one does, with the keyboard only,
hears short useful speech, never loses focus, and never triggers DAW actions by accident.

Sources of this skill: LSampler-24 (JUCE sampler, 159 iterations with an NVDA user in
REAPER: the reference behaviour), ToneTrace (EQ, VST3 + CLAP), Surge XT (overlays,
announcement window, control groups) and OSARA (REAPER's screen-reader extension:
UIA notifications, JAWS fallback).

## Ground rules

1. **One message, one channel.** Focus changes and value changes of real controls speak
   by themselves; announce only what they do not say. Never two channels for one event
   (SPK-03).
2. **Announce through the notification window** (templates/ScreenReaderAnnouncer). Never
   `postAnnouncement` on Windows: a separate SAPI voice, NVDA and JAWS hear nothing
   (SPK-01).
3. **State in titles, messages in speech.** Title = short current state ("Cutoff,
   1200 Hz"), live via `getTitle()`. Description = shortcut only. Never store messages
   in properties (SPK-04).
4. **Say little.** Navigation "Name, value"; change: value only; no role words; options by
   label; edges silent; impossible actions say why.
5. **Focus is always meaningful.** On open: the first working element. On close: back
   where the user came from. Move focus before hiding anything (FOC-01 to FOC-03).
6. **The plugin owns its keys.** Every key a plugin screen uses is consumed, modal screens
   consume everything including key releases; Space on the main screen, Alt+F4, Alt+Tab
   and screen-reader keys go through (KEY-02, KEY-07, KEY-08).
7. **One key model everywhere**, exact modifier matching, Shift+F10 for every context
   menu (keyboard-model.md).
8. **Graphics are for eyes only.** Displays are excluded from accessibility; everything
   they show exists as text.
9. **Host parameters are a second UI.** Clear names, units, option labels, stable IDs,
   gestures (parameters-automation.md).
10. **The audio thread is untouchable.** No speech, locks, allocation, files or messages in
    processBlock; long work in the background with text progress.
11. **Never invent host state.** Say only what the DAW confirmed.
12. **Honest report.** What was verified by test, by script, by reading code, and what
    needs a person with a screen reader.

## Workflow

Pick the mode. A user complaint ("NVDA is silent when...") is mode B starting from
references/pitfalls.md.

### Step 0 for existing code: map the project

1. Build system and versions: CMakeLists.txt or .jucer, formats, JUCE version (the audit
   prints it; below 6.1 there is no accessibility at all, BLD-02).
2. Find the places that matter (ripgrep, from the project root):
   - keys: `rg -n "keyPressed|keyStateChanged|addKeyListener|isCtrlDown|isCommandDown" Source`
   - focus: `rg -n "grabKeyboardFocus|grabFocus\(|setWantsKeyboardFocus|setVisible\s*\(\s*false|removeChildComponent" Source`
   - speech: `rg -n "postAnnouncement|UiaRaise|announce|setTitle|setDescription|notifyAccessibilityEvent" Source`
   - custom widgets: `rg -n "void paint|mouseDown|mouseDrag|createAccessibilityHandler|setAccessible" Source`
   - dialogs and menus: `rg -n "AlertWindow|PopupMenu|FileChooser|showMenuAsync|enterModalState|runModalLoop" Source`
   - parameters: `rg -n "ParameterID|AudioParameter(Float|Int|Bool|Choice)|createParameterLayout|setValueNotifyingHost|beginChangeGesture" Source`
   - threads: `rg -n "processBlock|callAsync|callAfterDelay|ThreadPool|getStateInformation" Source`
3. Note the editor class, its key entry point, every screen (main, modal, browsers,
   dialogs) and how each opens and closes.

### B. Fix an existing plugin

1. **Script:** `python <skill>/scripts/a11y_audit.py Source CMakeLists.txt --format json`
   (add `--param-ids <manifest>` if IDs are built at run time). Findings carry defect
   IDs; details in references/audit-checklist.md.
2. **Symptom:** if the user reported something, take the defect list for it from
   references/pitfalls.md and check those first.
3. **Manual hunt**, area by area in this order: BLD, FOC, KEY, SPK, CTL, DLG, PAR, RT
   (audit-checklist.md). Then walk every screen as a user: open it, where does focus
   land, what is spoken, each key (handler, result, speech), close, where does focus
   return. The script misses most focus and speech logic; this walk finds it.
4. **Prioritise:** critical (unusable: no focus, silence, keys to the DAW, mouse-only,
   crashes), then major (double speech, lost focus after dialogs, unclear values), then
   minor.
5. **Fix minimally**, with the templates. Do not change without asking: DSP, parameter
   IDs and order, state format, JUCE version, shortcuts users already know. Warn if a fix
   touches them.
6. **Verify:** re-run the audit (the fixed findings are gone, no new ones); build if
   possible; key routing tests; `scripts/uia_inspect.ps1` on a running build if
   available; write the manual steps for the user (testing.md).
7. **Iteration note** (testing.md section 7): changed, deliberately not changed,
   verified, steps to test by ear.

### A. New plugin

1. Clarify: instrument or effect, formats (VST3, CLAP, AU), target DAWs (REAPER almost
   always), features, file handling.
2. Design the **screen and key map** before code: main screen, parameter grid, modal
   screens, browsers, global hotkeys; check conflicts with REAPER and NVDA
   (keyboard-model.md section 7). Show the map to the user.
3. Parameter catalog with permanent IDs, names, units, ranges, labels
   (parameters-automation.md).
4. Project: CMake + JUCE (+ CLAP), build-vst3-clap.md. Add Standalone for testing.
5. Copy templates (see below) and adapt; build screens from ui-patterns.md, speech and
   focus from announcements-and-focus.md.
6. Key routing tests and the audit script (testing.md); uia_inspect.ps1 on the
   Standalone build.
7. Build note and a manual test plan for REAPER with NVDA.

### C. Audit only

List findings by severity. For each: defect ID, file and line, what the user hears or
does now, why it is a problem, how to fix. No tables: one finding per paragraph, easy
to listen to.

### Other frameworks

Built for JUCE (bridges to UI Automation on Windows and NSAccessibility on macOS). For
WebView editors and frameworks without screen-reader support see ui-patterns.md
section 15. Never promise accessibility a framework cannot give.

## Templates (assets/templates/)

Copy the helpers together with any template that includes them: `A11yKeys.h`,
`A11yParameters.h`, `ScreenReaderAnnouncer.h` + `.cpp` (the only .cpp).

- `ScreenReaderAnnouncer.h/.cpp`: `a11y::announce` and `a11y::Announcer` (delayed queue,
  latest-wins, only-if-changed). UIA notification window on Windows (NVDA, Narrator),
  MSAA fallback for JAWS/Dolphin, VoiceOver on macOS.
- `A11yKeys.h`: exact modifier matching, Shift+F10 / Applications key, letter-search
  character, search helper.
- `A11yParameters.h`: value text with units, sensible key steps (no inaudible
  0.01 Hz steps), gestures, strict parsing of typed values.
- `AccessibleListCell.h`: one focusable element for a long virtual list (slots, files,
  steps) with a live title.
- `AccessibleParameterList.h`: accessible editor of all processor parameters (grid model,
  exact entry, letter search, gestures). Fastest retrofit for any plugin.
- `AccessibleOverlay.h`: `ParameterOverlay` and `ActionOverlay`, transparent focusable
  overlays over custom-drawn knobs and buttons (retrofit without touching the GUI).
- `TextValueSlider.h`: slider read as "+3 dB" by every screen reader.
- `ModalKeySurface.h`: base for modal screens: consumes all keys, Escape closes
  asynchronously, focus returns.
- `AccessibleDialog.h`: `askForText` and `confirm` AlertWindows without double reading
  and with the right first focus.

The templates follow the JUCE 8/9 API but were not compiled in this repository (no
compiler here): build them in the project and fix small differences for its JUCE version.

## Scripts (scripts/)

- `a11y_audit.py`: static audit of C++ sources, CMakeLists.txt and .jucer. 32 rules with
  defect IDs, JUCE version detection, parameter ID and hash collision check
  (`--param-ids`), `--format json`, `--list-rules`, `--skip`/`--only`, suppression comments
  (`// a11y-ignore: RT-02 reason`). Exit 0 no errors, 1 errors, 2 bad arguments. Plain text
  without colours. Heuristic: it does not replace a check with a screen reader.
- `uia_inspect.ps1` (Windows PowerShell, nothing to install): `-Mode List` finds JUCE
  windows, `-Mode Tree` prints what screen readers get (role, name, value, states) and
  flags unnamed focusable elements and number-only values, `-Mode Watch` logs every focus,
  name and value change while the plugin is used. Run with
  `powershell -ExecutionPolicy Bypass -File <skill>/scripts/uia_inspect.ps1 -Mode Tree -Process reaper`.

## References (references/)

- `pitfalls.md`: symptom index: what the user hears, which defects to check. Start here
  for complaints.
- `audit-checklist.md`: the defect catalog (SPK, FOC, KEY, CTL, DLG, PAR, RT, BLD): what the
  user hears, cause, fix, verification, where it was seen.
- `announcements-and-focus.md`: speech channels, titles, what to say, moving focus,
  REAPER and NVDA specifics. Read before touching speech or focus.
- `keyboard-model.md`: the reference key layout, simple editors, modal screens, letter
  search, conflicts, JUCE key quirks, key routing.
- `ui-patterns.md`: list cell, parameter grid, controls, retrofit, browsers, dialogs,
  menus, progress, displays, help, preview, accessibility settings, WebView.
- `parameters-automation.md`: IDs, texts, gestures, echo, keyboard automation, REAPER API.
- `threading-realtime.md`: audio thread, background jobs, safe async calls, lifetime.
- `build-vst3-clap.md`: CMake, Projucer, CLAP, CI, local build.
- `testing.md`: key routing tests, static checks, uia_inspect, keyboard diagnostic,
  focus log, manual plan, note and bug report templates.

## Facts that expire

Checked in October 2026. Re-check when much time has passed or before a release:
- JUCE `postAnnouncement` on Windows uses SAPI: JUCE repo,
  `modules/juce_gui_basics/native/accessibility/juce_Accessibility_windows.cpp`, search
  `postAnnouncement`.
- No native CLAP in JUCE (latest tag 9.0.4, 12 October 2026): `docs/CMake API.md`, the
  FORMATS list; tags at github.com/juce-framework/JUCE/tags.
- clap-juce-extensions officially lists JUCE 6 to 8: its README and recent commits.
- NVDA prefers the UIA Value text over RangeValue, and reads UIA notifications only from
  the focused application: nvaccess/nvda, `source/NVDAObjects/UIA/__init__.py`
  (`_get_value`, `event_UIA_notification`).
- JAWS and Dolphin ignore UIA notifications: OSARA, `src/uia.cpp`
  (`shouldUseUiaNotifications`).

## Final message to the user

Write in the user's language, short, as a list; no tables, no ASCII art (the user most
likely listens with a screen reader). Always three blocks: what was done; what was
verified for accessibility and how (test, script, reading code); what to check by ear
in the DAW, as concrete steps with expected phrases ("open Alt+M, press F2, then
Ctrl+Down Arrow: NVDA should say 'Sample 2, Velocity Low, 1'"). Name anything not
verified (templates not compiled, JAWS fallback untested).
