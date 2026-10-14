# Defect catalog

Every known accessibility defect of JUCE plugins, with one stable ID. The audit
script prints these IDs; reports and fixes should cite them.

Each entry has: severity (critical = unusable, major = confusing or costly,
minor = comfort), how it is found (script rule, uia_inspect.ps1, grep, manual),
what the user hears, the cause, the fix, and how to verify. "Seen in" names the
real project where it happened (LSampler-24 iteration TESTnnn, ToneTrace, Surge XT,
OSARA).

Areas: SPK speech, FOC focus, KEY keyboard, CTL controls, DLG dialogs and menus,
PAR host parameters, RT threads and lifetime, BLD build.

Quick audit order: BLD, then FOC, KEY, SPK, CTL, DLG, PAR, RT. Critical items of
every area first.

## SPK: speech

### SPK-01
**postAnnouncement on Windows.** Critical. Script.
- Hears: a separate Windows voice (sounds like Narrator) at its own speed and
  volume; NVDA and JAWS say nothing, nothing on braille.
- Cause: on Windows JUCE implements AccessibilityHandler::postAnnouncement with
  SAPI ISpVoice, not with UI Automation (still true on JUCE master, October 2026).
- Fix: a11y::announce (templates/ScreenReaderAnnouncer). Keep postAnnouncement only
  in a macOS branch, where VoiceOver handles it.
- Verify: NVDA Speech Viewer shows the message; no second voice.
- Seen in: ToneTrace.

### SPK-02
**Announcement raised from a JUCE element provider.** Major. Script.
- Hears: sometimes nothing, typically right after a screen closes or while controls
  are disabled.
- Cause: the UIA notification is raised from the provider of a component
  (getNativeImplementation, an internal JUCE API). A hidden, disabled or
  setAccessible(false) source gives no provider or one NVDA ignores.
- Fix: the hidden notification window of ScreenReaderAnnouncer.cpp (OSARA and
  Surge XT technique). One call from anywhere, no choice of source element.
- Seen in: LSampler-24 announcer.

### SPK-03
**Double or cut-off speech.** Major. Script (info) for some cases.
- Hears: the same phrase twice, or the start of a phrase cut by another.
- Cause: two channels for one event: focus change plus announcement,
  titleChanged plus announcement, valueChanged of a focused slider plus an
  announcement of the value.
- Fix: one channel per event. Focus moves speak by themselves; value changes of
  real sliders speak by themselves (JUCE raises valueChanged). Announce only what
  no focus or value change says. After a focus move use Announcer::say (delayed).
- Seen in: LSampler-24 TEST39, TEST55, TEST125.

### SPK-04
**Message stuck in title or description.** Major. Script.
- Hears: an old message ("Loop On", "Help file unavailable") every time the user
  returns to an element.
- Cause: the announcer writes the text into setDescription/setTitle/setHelpText
  of the source.
- Fix: never store messages in properties. Description = shortcut only ("Alt+O");
  title = current state. Live titles: override getTitle() in a handler.
- Seen in: LSampler-24 TEST103, TEST108, TEST146.

### SPK-05
**Results of actions are not announced.** Critical. Manual.
- Hears: nothing after load, save, delete, mode toggle or error.
- Fix: one short result phrase per action ("Bank loaded: Salamander",
  "Cannot load Kick.wav: file not found. Choose another file."). Errors say what
  happened and what to do.

### SPK-06
**Verbose or unclear phrases.** Minor. Manual.
- Hears: "Cutoff parameter button has value 1200.00 hertz", indexes instead of
  option names, "C#4".
- Fix: main word first; navigation "Name, value"; change "value" only; options
  by label; trim zeros ("10%"); sign for bipolar values ("+3 dB"); notes as
  "61 C sharp 4"; no role words (the screen reader adds them). Rules:
  announcements-and-focus.md section 3.

### SPK-07
**Noisy edges or unexplained impossible actions.** Minor. Manual.
- Hears: "end of list", wrap-around to the first item, or silence when an
  action is impossible.
- Fix: edges are silent and do not wrap (exception: page switchers). An
  impossible action says why once ("Only one loaded slot", "No sample loaded").

### SPK-08
**Garbage characters.** Major. Script.
- Hears and sees: mojibake instead of "—", "•", "×", or Cyrillic.
- Cause: non-ASCII literals without UTF-8; juce::String(const char*) expects ASCII.
- Fix: juce::String::fromUTF8 (u8"...") or plain ASCII ("-" instead of "—").
- Seen in: LSampler-24 TEST47.

### SPK-09
**Speech spam.** Major. Manual.
- Hears: every automation point, meter level or progress percent.
- Fix: host automation and meters are never announced; their values are readable on
  demand (live title, properties). Progress at most every 4 seconds and only while
  focus is in the plugin (Announcer::sayIfChanged plus a time check).

### SPK-10
**Announcement lost after a focus move.** Major. Manual.
- Hears: the focus phrase, but not the result message sent just before it.
- Cause: screen readers cancel speech when focus changes.
- Fix: send the message after the focus move: a11y::Announcer::say (default 150 ms
  delay, queued), the way Surge XT delays announcements by a few idle ticks.

### SPK-11
**JAWS or Dolphin silent.** Major. Manual (needs JAWS).
- Cause: per OSARA, these screen readers ignore UIA notification events.
- Fix: ScreenReaderAnnouncer detects jhook.dll / dolwinhk.dll and uses OSARA's MSAA
  name-change fallback (unverified inside JUCE windows: test with JAWS; if silent,
  report it honestly as a limitation).

## FOC: focus

### FOC-01
**No focus inside the plugin when the window opens.** Critical. Script (BLD-01) plus manual.
- Hears: nothing, or REAPER's FX chain; keys run REAPER actions.
- Fix: EDITOR_WANTS_KEYBOARD_FOCUS TRUE; after the editor is showing, focus the first
  working element with callAsync + SafePointer, grabKeyboardFocus() and
  getAccessibilityHandler()->grabFocus(); one entry phrase at most.
- Verify: open the plugin from the FX chain and as a floating window; NVDA names the
  first element.

### FOC-02
**Focus left on a hidden, disabled or removed element.** Critical. Manual, grep.
- Hears: silence; the next keys go to the DAW.
- Find: `rg -n "setVisible\s*\(\s*false|setEnabled\s*\(\s*false|removeChildComponent" Source`
  and check what had focus.
- Fix: move focus to the new place BEFORE hiding or disabling. Remember where focus came
  from; check isShowing() && isEnabled() of the return target.
- Seen in: LSampler-24 several iterations; template ModalKeySurface closes this way.

### FOC-03
**Focus not returned after closing a screen, dialog or menu.** Critical. Manual.
- Hears: "pane", the DAW, or nothing after Escape or menu cancel (result 0).
- Fix: return to the element that opened it (a button) or to the main element (when
  opened by a hotkey); do it asynchronously (callAsync / callAfterDelay 50 ms) with a
  SafePointer; handle menu result 0.
- Seen in: LSampler-24 TEST108.

### FOC-04
**grabKeyboardFocus in a constructor.** Major. Script.
- Cause: the component is not on screen yet, the call is ignored.
- Fix: defer until isShowing() (callAsync, or visibilityChanged/parentHierarchyChanged).

### FOC-05
**UIA focus event missing in REAPER.** Major. Manual.
- Hears: NVDA keeps reading the old element although JUCE moved focus.
- Fix: after grabKeyboardFocus() call getAccessibilityHandler()->grabFocus(); when
  returning to an element JUCE may think is still focused, call
  AccessibilityHandler::clearCurrentlyFocusedHandler() first. A 1 ms deferred
  grabFocus() in focusGained helped LSampler-24 when entering from REAPER.

### FOC-06
**Tab order wrong or broken.** Major. Manual, uia_inspect.ps1.
- Cases: order differs from the visual order; a modal screen lets Tab escape; a
  hand-written list of Tab stops misses newly added buttons (LSampler-24 TEST134).
- Fix: setExplicitFocusOrder or a focus container (FocusContainerType::
  keyboardFocusContainer) on modal screens; derive Tab cycles from one list of
  components; multi-cell widgets get one Tab stop (roving focus).

### FOC-07
**Focusable element hidden from screen readers.** Critical. Script.
- Cause: setAccessible(false) with setWantsKeyboardFocus(true).
- Fix: either accessible with a title, or not focusable (purely visual).

### FOC-08
**Preview keeps sounding after focus leaves the plugin.** Minor. Manual.
- Fix: in the editor timer watch hasKeyboardFocus(true) of the editor; stop all
  previews when it becomes false, and in the destructor.
- Seen in: LSampler-24 TEST70.

### FOC-09
**Accessibility tree not refreshed after rebuilding the UI.** Major. Manual.
- Hears: old elements, or NVDA cannot find new ones after a page or mode switch.
- Fix: after rebuilding children call
  getAccessibilityHandler()->notifyAccessibilityEvent(AccessibilityEvent::structureChanged)
  on the container, deferred by a tick (Surge XT does this after layout changes).

## KEY: keyboard

### KEY-01
**Ctrl test that never matches on Windows.** Critical. Script.
- Cause: on Windows isCommandDown() is true whenever Ctrl is held, so
  `isCtrlDown() && !isCommandDown()` is always false.
- Fix: drop `!isCommandDown()`, or a11y::modsAre / a11y::is from A11yKeys.h.
- Seen in: LSampler-24 TEST114 (Ctrl+Shift+A); the same pattern survived in other
  handlers, so search the whole project, not only the reported shortcut.

### KEY-02
**Keys leak to the DAW.** Critical. Script (modal classes) plus manual.
- Hears: REAPER actions start (R records, Space plays, Ctrl+Z undoes the project).
- Cause: keyPressed returns false for keys the plugin screen owns; modal screens do
  not consume key releases (keyStateChanged).
- Fix: route keys through one entry point; the editor consumes everything it does not
  pass to the DAW on purpose; modal screens return true from keyStateChanged while open
  (ModalKeySurface). Tell users about REAPER's "Send all keyboard input to plug-in".

### KEY-03
**Mouse-only control.** Critical. Script.
- Fix: keyboard focus, title and keys for the same action; for custom-painted
  widgets without touching them: a11y::ParameterOverlay / ActionOverlay.

### KEY-04
**Right-click menu without Shift+F10.** Critical. Script.
- Fix: a11y::isContextMenuKey (Shift+F10 and the Applications key) on the focused
  element opens the same menu via showMenuAsync(...withTargetComponent(element)); also
  add AccessibilityActionType::showMenu (VoiceOver VO+Shift+M). If REAPER also opens its
  own menu, consume the F10 release in keyStateChanged.

### KEY-05
**Incomplete modifier checks, wrong precedence.** Major. Script (info).
- Hears: Shift+Alt+Up changes the value instead of switching the sub-loop.
- Cause: a generic `isAltDown() && key == up` branch runs before the specific
  Shift+Alt branch and does not exclude Shift.
- Fix: match modifiers exactly (a11y::is(key, code, cmd, alt, shift)); test specific
  combinations first.
- Seen in: LSampler-24 TEST155 to TEST157.

### KEY-06
**First-letter search broken.** Major. Script.
- Cases: arrows, Page keys or F-keys start a search (codes overlap ASCII: Up is '&',
  F1 is 'p'); digits and punctuation never match because the host sends no text
  character; Shift+1 searches for "!".
- Fix: a11y::searchCharacter (typed character first, key-code fallback only for digits
  and punctuation, F-keys excluded). Search the name the user hears, without repeated
  prefixes.
- Seen in: LSampler-24 TEST40, TEST129, TEST134, TEST135.

### KEY-07
**Space taken from the DAW.** Major. Manual.
- Cases: the plugin eats Space so transport cannot start; a focused ComboBox opens its
  popup on Space/Enter.
- Fix: on the main screen Space goes to the DAW (return false) unless a preview mode
  is switched on (Alt+P "Slot preview On" / "DAW space On"); a ComboBox that must not
  open on Space gets a key handler that returns false for Space.
- Seen in: LSampler-24 TEST50, TEST51, TEST146.

### KEY-08
**System or screen-reader keys swallowed.** Critical. Manual.
- Never consume Alt+F4, Alt+Tab, Insert/CapsLock combinations, or bind actions to Ctrl
  alone (it stops speech).

### KEY-09
**Long lists without fast navigation.** Minor. Manual.
- Fix: Home/End, Ctrl+Home/End, Page Up/Down, letter search forward, Shift+letter
  backward (AccessibleListCell).

### KEY-10
**Shortcuts not discoverable.** Minor. Manual.
- Fix: shortcut in the button's description ("Alt+O"), in the help (F1), and in a
  spoken short help inside complex screens.

### KEY-11
**Key handled twice.** Major. Manual.
- Cause: addKeyListener(editor) on children plus their own keyPressed.
- Fix: one entry point (handleKeyPress(key, source)) called first by every control.

### KEY-12
**Different key models on different screens.** Major. Manual.
- Fix: one model for the whole plugin (keyboard-model.md): navigation separate from
  editing, Escape always goes one level up, same keys for the same actions.

## CTL: controls and names

### CTL-01
**Focusable element without a name.** Critical. Script, uia_inspect.ps1.
- Hears: "button", "unknown", "pane".
- Cause: no setTitle (setName is not read by screen readers; buttons fall back to
  their text, image buttons have none).
- Fix: setTitle("action or name"); live titles via a handler's getTitle().

### CTL-02
**Slider value without text or units.** Major. Script (info), uia_inspect.ps1.
- Hears: "0.53" or a percentage instead of "-3 dB".
- Cause: no textFromValueFunction; or a screen reader that reads the UIA RangeValue
  number (current NVDA prefers the text Value, older versions and other readers may
  not).
- Fix: TextValueSlider.h or ParameterOverlay (text-only value); at least a
  textFromValueFunction with units.

### CTL-03
**Custom-painted control not exposed at all.** Critical. Script (KEY-03), uia_inspect.ps1.
- Fix (retrofit without touching the GUI): ParameterOverlay / ActionOverlay over each
  widget, or an AccessibleParameterList page with all parameters.

### CTL-04
**Elements that sound the same.** Minor. uia_inspect.ps1.
- Fix: unique names; raise display precision until names differ ("1.02 kHz",
  "1.05 kHz"; ToneTrace).

### CTL-05
**Read-only text that cannot be read by line.** Major. Script.
- Fix: setReadOnly(true) + setCaretVisible(true); arrows, Home/End, Page keys stay in
  the field; Tab moves focus (setTabKeyUsedAsCharacter(false)); Ctrl+A and Ctrl+C work
  (LSampler-24 TEST159).

### CTL-06
**Graphics exposed, or information only graphical.** Major. Manual, uia_inspect.ps1.
- Fix: VisualOnly components (setAccessible(false), no focus, no mouse); everything
  they show is available as text (properties, status line).

### CTL-07
**Toggle state never reported.** Major. Script.
- Fix: in a custom handler override getCurrentState() with withCheckable() and
  withChecked(); raise valueChanged after toggling (JUCE then sends the toggle-state
  event). Surge XT hit this.

### CTL-08
**ComboBox without Home/End.** Minor. Manual.
- Fix: handle Home/End (first/last option) in a ComboBox subclass or key listener.

## DLG: dialogs and menus

### DLG-01
**Synchronous dialog or menu.** Critical. Script.
- Fix: AlertWindow::enterModalState with a callback, showMessageBoxAsync,
  PopupMenu::showMenuAsync, FileChooser::launchAsync.

### DLG-02
**AlertWindow: wrong first focus or prompt read twice.** Major. Script (info).
- Cause: addButton gives buttons focus order 1 (OK gets focus first); the prompt is
  exposed by the window description and by internal Labels; an extra
  grabKeyboardFocus after enterModalState adds a second focus announcement.
- Fix: templates/AccessibleDialog.h (askForText, confirm).
- Seen in: LSampler-24 TEST124, TEST125.

### DLG-03
**Unsafe confirmations.** Major. Manual.
- Fix: Y/N/Escape keys; first focus on "No"; text names what will be lost and ends
  with "Press Y or N."; decide before any background work starts.

### DLG-04
**Menus without a title.** Minor. Script (info).
- Fix: LookAndFeel::preparePopupMenuWindow sets window.setTitle("Preset menu"); menus
  open at the element (withTargetComponent); cancel returns focus; disabled items stay
  visible with a reason ("D:\Library (unavailable)").

### DLG-05
**Menu opened while the host owns focus.** Major. Manual.
- Hears: the menu is visible but keys and NVDA stay in REAPER's FX chain.
- Fix: open menus only when the editor hasKeyboardFocus(true); otherwise defer.

### DLG-06
**Save dialog overwrites silently.** Minor. Manual.
- Cause: the final path changes after the system dialog (prefix, extension).
- Fix: check the final path yourself and confirm.

## PAR: host parameters and automation

### PAR-01
**Parameter IDs unstable, duplicated or colliding.** Critical. Script (+ --param-ids).
- Fix: string IDs independent of UI, language and order; new parameters only at the
  end; never reuse or re-range an ID; check the 31-bit hash (VST3 and CLAP) for
  collisions; freeze a manifest.

### PAR-02
**Host parameters unreadable.** Major. Manual (REAPER parameter list or OSARA).
- Fix: names understandable out of context ("Slot 2 - Filter Cutoff"); getText =
  editor text; getValueForText parses option names and numbers; correct
  getNumSteps/isBoolean; units in the label.

### PAR-03
**No gestures.** Major. Script.
- Fix: begin/end gesture per key press, one gesture per mouse drag
  (a11y::setByUser).

### PAR-04
**Echo and spoken automation.** Major. Manual.
- Cause: a host change updates the control, whose onValueChange sends the value back
  with a gesture (and may announce it).
- Fix: syncingFromHost guard (TextValueSlider::setFromHost); never announce host
  changes.

### PAR-05
**UI state exposed as automatable parameters.** Major. Manual.
- Fix: no parameters for selection, page, cursor, focus, file operations or
  destructive commands; per-slot parameters are tied to the slot, not "current".

### PAR-06
**Invented host state.** Major. Manual.
- Hears: "Auto" while no envelope is active, or "automation on" without effect.
- Fix: say only what the host confirmed (REAPER ACTIVE=1 or one unambiguous menu item).

### PAR-07
**Heavy work in state calls.** Critical. Script.
- Hears: REAPER and NVDA freeze for many seconds when closing or saving.
- Fix: cache hashes by path+size+date; keep get/setStateInformation fast.
- Seen in: LSampler-24 TEST104.

### PAR-08
**REAPER API misuse.** Major. Script (IID macros) plus manual.
- Cases: DEF_CLASS_IID without DECLARE_CLASS_IID (TEST117 compile error); acting on
  another plugin instance; toggling envelope visibility instead of ACTIVE (TEST116).
- Fix: parameters-automation.md section 8.

## RT: threads and lifetime

### RT-01
**Forbidden call on the audio thread.** Critical. Script.
- Fix: only atomics and pre-allocated memory in processBlock; commands to the audio
  thread through atomics; results back through a timer that polls atomics.

### RT-02
**Async callback with raw this.** Critical. Script.
- Fix: capture juce::Component::SafePointer and return if null.

### RT-03
**Component deleted or rebuilt inside keyPressed.** Critical. Script.
- Fix: defer with callAsync + SafePointer.
- Seen in: LSampler-24 slice editor Escape crash; TEST152 (Bank Macro view rebuilt
  during key dispatch).

### RT-04
**Owner hidden while its TextEditor has focus.** Critical. Manual.
- Hears: REAPER crashes on Escape from an entry field.
- Fix: apply text, move focus away, hide the text box, then callAsync the next step.

### RT-05
**ComboBox repopulated with notifications.** Major. Script (info).
- Fix: dontSendNotification while rebuilding; a re-entrancy guard; reset auxiliary
  indexes before leaving a mode and bounds-check them (TEST152).

### RT-06
**LookAndFeel not reset.** Major. Script.
- Fix: setLookAndFeel(nullptr) in destructors before the LookAndFeel dies.

### RT-07
**Heavy work on the message thread.** Major. Manual.
- Fix: decoding, hashing, copying in a background ThreadPool job with text progress;
  one job at a time.

### RT-08
**UI-only state saved in presets or projects.** Minor. Manual.
- Fix: cursor positions, preview modes and pages live in processor memory or user
  settings, not in the plugin state (unless the user asks).

## BLD: build

### BLD-01
**Editor does not ask for keyboard focus.** Major. Script.
- Fix: CMake `EDITOR_WANTS_KEYBOARD_FOCUS TRUE`; Projucer "Plugin Editor Requires
  Keyboard Focus" (pluginEditorRequiresKeys).

### BLD-02
**JUCE without accessibility.** Critical. Script.
- JUCE before 6.1 has no accessibility API. Upgrading changes behaviour: ask first.

### BLD-03
**UIA called without linking.** Critical. Script.
- Fix: run-time loading as in ScreenReaderAnnouncer.cpp (also keeps the plugin
  loadable on Windows older than 10 1709).

### BLD-04
**VST3 and CLAP behave differently.** Minor. Manual.
- Fix: one editor and one parameter list for both; test both formats in REAPER (they
  are separate FX with separate parameter lists).
