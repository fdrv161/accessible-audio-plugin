# Host parameters and automation

Contents: 1. Why this matters. 2. IDs and order. 3. Names, texts, units. 4. What to
expose. 5. Threads and gestures. 6. Sync without echo. 7. Automation from the
keyboard. 8. REAPER API from VST3. 9. "Auto" indicator. 10. Macro parameters.
11. Checks.

## 1. Why this matters

REAPER + OSARA users often control a plugin without its window: through the FX
parameter list (OSARA reads each parameter's name and value text) and through
automation envelopes. Host parameters are a second user interface and must be as clear
as the editor.

## 2. IDs and order (PAR-01)

- Every parameter has a string ID independent of UI selection, language, visibility,
  loaded files and grid order. LSampler-24 scheme: `global_<key>`, `slot01_<key>`,
  `slot01_loop03_<key>`, `slot01_slice_<key>`, `slot01_sample05_velocity_low`.
- New parameters only at the END of the registration list. Never insert, reorder,
  delete, reuse an old ID, or change meaning or range: old projects would lose their
  automation. If a range must change, migrate stored state and warn the user (TEST145
  moved Decay from percent to ms with a schema version).
- JUCE's VST3 wrapper turns the string ID into a 31-bit hash; clap-juce-extensions uses
  the same hash for CLAP. Hash collision = two parameters with one host ID. The audit
  script checks literal IDs in the source; IDs built at run time need a manifest:
  `python scripts/a11y_audit.py Source --param-ids manifest.json` (JUCE hash: h = 31*h +
  code point over the string, uint32, then & 0x7fffffff).
- Freeze the list: a JSON manifest (id, name, min, max, default) in the repository and a
  script that checks count, uniqueness and order on every build.
- `ParameterID{ id, versionHint }`: the version hint is for AU; new parameters in a new
  version get a new hint.

## 3. Names, texts, units (PAR-02)

- Host names make sense outside the window: "Slot 2 - Filter Cutoff", "Bank Macro -
  Level". Hosts truncate long names: main part first.
- `getText(value)` returns what the editor shows: option labels ("Round Robin"), numbers
  with sensible precision; the unit in `getLabel()` / `withLabel("dB")`.
- `getValueForText(text)` parses option labels and numbers (OSARA and REAPER let users
  type a value).
- `isBoolean()` true only for real Off/On.
- Discrete parameters: correct `getNumSteps()` (max - min + 1), or hosts and OSARA step by
  1% and land between options.
- Do not reshape ranges for looks: if DSP stores cutoff as 0..1 with a log map, the host
  sees 0..1 and text in Hz, not a new 20..22000 range.

## 4. What to expose (PAR-05)

Expose musical and DSP parameters. Do not expose:
- file operations, browsers, load/save;
- current selection, page, focus, cursor position;
- destructive commands (trim, normalize into the buffer), copy, undo;
- edits that trigger heavy whole-sample analysis (thresholds that scan audio): keep
  them editor operations;
- a "parameter of the selected control": its meaning changes.

Multi-slot plugins: a parameter belongs to a fixed slot, never to "the current one", or
automation jumps when the selection changes.

## 5. Threads and gestures (PAR-03)

- A parameter value is an atomic (preferably the canonical `std::atomic<double>`). A host
  `setValue` only stores the value and bumps a revision counter: no locks, messages, host
  notifications, files or audio data.
- The audio thread compares revisions once per block and rebuilds only what changed, in
  pre-allocated memory.
- A change from the UI is a gesture: `beginChangeGesture()`, value, `endChangeGesture()`.
  Keyboard: one gesture per key press. Mouse drag: one gesture for the whole drag.
  `a11y::setByUser` does the keyboard case.
- Notify the host only from the message thread. If a value changed on a background thread
  (preset load), mark "needs notify" and use an AsyncUpdater; send the LATEST value then,
  not a stored old one (an outdated value would overwrite automation).
- Restoring state (setStateInformation, preset load) without gestures: values are
  published without "touching", so the DAW does not record them as automation.

## 6. Sync without echo (PAR-04)

When the host changes a parameter, the control on screen updates. If the control then
fires `onValueChange`, it sends the value back with a gesture: an echo, an extra
automation write and extra speech. Fix (ToneTrace):

```cpp
void setFromHost (double v)
{
    const juce::ScopedValueSetter<bool> guard (syncingFromHost, true);
    setValue (v, juce::sendNotificationSync);
}
// in onValueChange: if (syncingFromHost) return;
```

Host and automation changes are never announced: the user gets the current value on
request (live title, NVDA+Up Arrow).

## 7. Automation from the keyboard

A sighted user moves a knob with the mouse and REAPER knows the "last touched
parameter". From the keyboard an explicit command is needed. LSampler-24:

**Ctrl+Shift+A: toggle the envelope of the current parameter.**
1. Map the current grid row to its host parameter (one mapping function for all screens,
   so "touch" and "show envelope" point to the same ID).
2. If the REAPER API is available (section 8): toggle the envelope (ACTIVE, VISIBLE,
   ARM).
3. Otherwise ask the host for the parameter's context menu
   (`getHostContext()->getContextMenuForParameter(param)`, `getEquivalentPopupMenu()`)
   and find EXACTLY ONE item that activates or deactivates the envelope; run its
   `item.action()` while the menu object is alive. "Show/Hide" items do not count: a
   hidden but active envelope still controls the parameter, and the user "cannot" change
   it. Ignore "write", "record", "learn", "mode", "arm", "all automation".
4. If nothing reliable exists, send a touch: `beginChangeGesture()`,
   `sendValueChangedMessageToListeners(getValue())`, `endChangeGesture()` (value
   unchanged) and say "Direct automation toggle unavailable. Last touched: Slot 1 -
   Volume". In REAPER users can bind a key to "Show/hide envelope for last touched FX
   parameter".
5. For "Set All" modes and other non-automatable rows, say what to do instead ("Use
   Alt+U for Relative macro automation").

**Alt+Shift+A: deactivate all active envelopes of this instance (TEST158).**
- Only this plugin instance (verified as in section 8); if the instance cannot be
  identified safely, change nothing and say so.
- Only ACTIVE envelopes; points are kept; never create or delete lanes.
- One result message: "No active automations", "Disabled 1 automation", "Disabled 5
  automations", plus the number of failures if any.

**Shift+arrows in the grid: jump between parameters with an active envelope (TEST134).**

## 8. REAPER API from VST3 (PAR-08)

REAPER gives VST3 plugins the `IReaperHostApplication` interface (IID
`79655E36-77EE-4267-A573-FEF74912C27C`, from `reaper_vst3_interfaces.h` in the REAPER
SDK). In JUCE get it through `VST3ClientExtensions`: override `getVST3ClientExtensions()`
in the processor and `queryInterface` that IID in `setIHostApplication(FUnknown*)`.

- C++ needs both lines: `DECLARE_CLASS_IID(IReaperHostApplication, ...)` and then
  `DEF_CLASS_IID(IReaperHostApplication)`; without DECLARE, MSVC cannot find
  `IReaperHostApplication_iid` (TEST117).
- Functions by name: `getReaperApi("GetFXEnvelope")` and so on.
- **Own track, own instance.** Before any action make sure it is this FX:
  `getReaperParent(1)` gives this instance's track; `GetTouchedOrFocusedFX` gives the
  focused track/FX; the tracks must match; `TrackFX_GetParamName` at the index must match
  your parameter name. Otherwise do nothing: with several instances it is easy to change
  someone else's.
- Envelope: `GetFXEnvelope(track, fx, param, create)`;
  `GetSetEnvelopeInfo_String(env, "ACTIVE"/"VISIBLE"/"ARM", buf, set)`. On: ACTIVE=1,
  VISIBLE=1, ARM=1. Off: ACTIVE=0, ARM=0, VISIBLE=0; points are kept. ACTIVE first: an
  invisible active envelope is not off (TEST116).
- Afterwards: `TrackList_AdjustWindows(false)`, `UpdateArrange()`.
- CLAP: REAPER has its own host extension for API access; check the REAPER SDK, not tested
  in the source projects.

## 9. "Auto" indicator (PAR-06)

- Append ", Auto" to a grid row only if the host confirmed the envelope is active (REAPER
  ACTIVE=1, or one unambiguous "Deactivate ..." menu item). Unknown: add nothing.
- Poll only the selected parameter, at most once a second, only while the plugin window
  has focus.
- The indicator is part of the row (title), not a separate message.

## 10. Macro parameters

LSampler-24 Bank Macro: one knob offsets a parameter in every non-empty slot.
- Separate host parameters at the end of the list; the value is an offset (0 = neutral)
  applied to the slot value on the audio thread without overwriting the slots.
- Speech shows the result, not the raw offset: same result everywhere: "On, next option
  by 1" / "-3 dB, decrease by 3 dB"; different: "Mixed values, increase by 3 dB"
  (TEST120).
- "Set All" (write one value to all) is a one-off operation, not automation; explain
  this on Ctrl+Shift+A.

## 11. Checks

- Script: count, unique IDs and hashes, prefixes, no forbidden parameters
  (selection/focus/thresholds), the `setValue` callback without locks, `new`, files,
  `callAsync`, host notifications. `a11y_audit.py --param-ids` checks IDs and hashes.
- Manual: REAPER parameter list (or OSARA "FX parameters"): names and value texts; type a
  value; record and play an envelope; Ctrl+Shift+A twice; two instances on different
  tracks; CLAP and VST3 separately.
