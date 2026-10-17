# Symptom index: from a user complaint to the defect

Users describe what they hear, not the cause. Find the closest symptom, then check
the defects in the listed order (most likely first). IDs are in audit-checklist.md.

## How to get a precise report

Ask the user (blind users can do all of this with the keyboard):
- Windows version, DAW and version, plugin version and format (VST3 or CLAP),
  plugin window docked in the FX chain or floating.
- Screen reader and version; OSARA version if REAPER.
- Exact steps, what they expected to hear, what they heard. NVDA's Speech Viewer
  (NVDA menu, Tools, Speech Viewer) shows the exact spoken text to copy.
- For a silent or wrong element: NVDA's command "Copies developer info for the
  navigator object to the clipboard" (NVDA menu, Preferences, Input gestures, Tools;
  no key by default, ask the user to assign one). Focus the element, make sure the
  navigator follows focus (NVDA+Numpad Minus moves it to focus), press the key, paste
  the text into the chat. Fallback: NVDA+F1 opens the same info in the NVDA log.
  How to read it: testing.md section 8.
- For keys: the plugin's keyboard diagnostic mode (testing.md), or NVDA's input help
  (NVDA+1) to confirm the key reaches the system.
- Where it happened: the plugin window or the host's parameter list (OSARA).

## Silence

- **NVDA says nothing when the plugin window opens, keys run REAPER actions:**
  FOC-01, BLD-01, FOC-05, KEY-02.
- **Silence after closing a screen, a dialog or a menu:** FOC-03, FOC-02, SPK-02.
- **Silence on one element while others speak:** FOC-07, CTL-01, CTL-03, FOC-05.
- **Results (loaded, saved, errors) never spoken:** SPK-05, SPK-02, SPK-10, SPK-11.
- **Messages spoken with NVDA, silent with JAWS:** SPK-11.
- **Silent after switching page or mode, old elements still read:** FOC-09, FOC-02.

## Wrong voice or wrong text

- **A different Windows voice (like Narrator) speaks, NVDA silent:** SPK-01.
- **Phrase spoken twice, or cut off:** SPK-03, SPK-10, DLG-02.
- **An old message is read again when returning to a button:** SPK-04.
- **Slider read as a number or percent without units:** CTL-02.
- **Garbage characters:** SPK-08.
- **Too much talking (automation, meters, progress):** SPK-09.
- **Two elements sound the same:** CTL-04.
- **"Button" or "unknown" with no name:** CTL-01.
- **Toggle always "not checked":** CTL-07.
- **Menu announced only as "menu":** DLG-04.

## Keyboard

- **Keys trigger REAPER (record, transport, undo):** KEY-02, FOC-02, FOC-01, DLG-05.
- **Ctrl shortcut does nothing on Windows (works on macOS):** KEY-01.
- **A Shift+Alt (or Shift+Ctrl) shortcut runs the plain Alt action:** KEY-05.
- **Arrows, Page keys or F-keys jump to random items; digits do not search:** KEY-06.
- **Space does not start the DAW transport while the plugin has focus:** KEY-07.
- **Context menu impossible without a mouse:** KEY-04, KEY-03.
- **Some feature works only with the mouse:** KEY-03, CTL-03.
- **A key acts twice:** KEY-11.
- **Menu visible but keys and NVDA stay in REAPER:** DLG-05.
- **Tab skips a button or escapes a dialog:** FOC-06.
- **Cannot read an About or help text by line:** CTL-05.

## Stability

- **REAPER crashes on Escape or when closing a screen:** RT-03, RT-04, RT-02.
- **Crash when closing the plugin window during an operation:** RT-02, RT-06.
- **REAPER and NVDA freeze for seconds when saving or closing:** PAR-07, RT-07.
- **Crash or wrong reading after leaving a special mode:** RT-05.
- **Clicks or dropouts while using the GUI:** RT-01.

## Host parameters and automation

- **Parameter list in REAPER or OSARA shows numbers or unclear names:** PAR-02.
- **Automation recorded wrongly or not at all from the keyboard:** PAR-03, PAR-04.
- **Old projects lose automation after an update:** PAR-01.
- **"Auto" announced but nothing is automated, or a hidden envelope still controls the
  value:** PAR-06, PAR-08.
- **Automation is spoken while the project plays:** PAR-04, SPK-09.

## When the cause is not in the list

1. Reproduce the path in code: key -> keyPressed entry point -> handler -> state
   change -> what is spoken (announce / focus / value event) -> where focus ends.
2. Run scripts/uia_inspect.ps1 -Mode Watch while the user (or you, with the
   standalone build) repeats the steps: it lists every focus, name and value change.
3. Add a new defect entry to audit-checklist.md once the cause is known.
