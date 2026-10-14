# Screen patterns

Contents: 1. Virtual list cell. 2. Parameter grid. 3. Individual controls.
4. Retrofitting an existing custom GUI. 5. File browser. 6. Properties and read-only
text. 7. Dialogs. 8. Popup menus. 9. Long operations. 10. Visual displays.
11. Help and About. 12. Page switcher. 13. Preview modes. 14. Accessibility settings.
15. WebView editors and other frameworks.

## 1. Virtual list cell

Task: 24 slots, hundreds of files, 128 sequencer steps. Hundreds of components are
expensive and noisy for screen readers.

Solution: ONE focusable component that keeps a cursor index and shows the current row.
Arrows change the index and speak the new row; the title is live. Template:
AccessibleListCell.h.

- The row says everything needed: "Slot 3, Kick"; "Folder Drums"; "Selected. Slot 4,
  Snare Soft"; "Step 5, Selected. Repeat, 2".
- State is words in the row, never only colour ("Selected", "Auto", "Mixed values").
- Empty list: a row such as "Empty folder" / "No recent paths".
- If each item needs its own role (object navigation), real components are possible,
  but only the current one is focusable (LSampler-24 slots: setWantsKeyboardFocus only
  on the selected slot).

## 2. Parameter grid

LSampler-24 reference: a ComboBox as the grid (items "Name, value") + a Slider as Value
+ a temporary TextEditor for exact entry. Simpler ready version without ComboBox:
AccessibleParameterList.h (one cell, live title, all AudioProcessor parameters).

- Parameter catalog as DATA, not code: key, name, category, type, min, max, default,
  step, coarse step, unit, decimals, option labels. Grid, formatting, host parameters
  and saving are built from one catalog.
- One formatting function `formatParameter(index, value)` used by grid, Value, entry
  parsing and the host parameter's getText.
- Action rows ("Zero Crossing: Apply"): Enter runs them, speech "Applied".
- Cursor memory per slot or global, in memory, not in presets.
- Update only the selected row, not a 150-item grid ten times a second (TEST58).
- Programmatic item change: dontSendNotification; user navigation: sendNotificationSync
  so the ComboBox reports the new item to the screen reader. While repopulating, no
  notifications (RT-05).

## 3. Individual controls

- Button: TextButton; title = action; description = shortcut. Image buttons must get a
  title (CTL-01).
- Toggle: ToggleButton (the screen reader says checked/not checked). Custom toggles:
  override getCurrentState with withCheckable/withChecked (CTL-07).
- Choice: ComboBox with setEditableText(false); add Home/End (CTL-08).
- Slider with units: TextValueSlider.h (text value "+3 dB", strict parsing, keyboard
  steps, gestures).
- Read-only field: TextEditor with setReadOnly(true), setCaretVisible(true),
  setTabKeyUsedAsCharacter(false) (CTL-05).
- Unique names: raise display precision until names differ (CTL-04).
- Host sync: syncingFromHost guard around setValue from the host (PAR-04).

## 4. Retrofitting an existing custom GUI

Most inaccessible plugins draw knobs and buttons in paint() and handle only the mouse.
Do not rewrite the GUI. Choose, from cheapest:

1. **Accessible parameter page.** Add AccessibleParameterList as a focusable element (or
   a page opened with a hotkey) that covers every host parameter. Hours, not days; the
   visual GUI stays as it is.
2. **Overlays** (Surge XT's approach). For each visual widget, an
   `a11y::ParameterOverlay` (bound to the same host parameter) or `a11y::ActionOverlay`
   (for drawn buttons) is laid over it: mouse goes to the widget, keyboard focus and
   screen readers get the overlay. Then:
   - `widget.setWantsKeyboardFocus(false)`, and `widget.setAccessible(false)` if it
     exposes something useless;
   - set the Tab order with setExplicitFocusOrder on overlays if the visual order is not
     logical;
   - give the containers titles (group role) so focus entering a section says
     "Filter".
3. **Real accessible components** for new screens (templates in section 3).

Things a retrofit must not change without asking: DSP, parameter IDs and order, state
format.

## 5. File browser (internal)

The Windows file dialog is accessible, but an internal browser gives the same key model,
preview, position memory and library limits. LSampler-24 pattern (Alt+O, Alt+S, Alt+B;
save browsers TEST123):

- List cell: folders first ("Folder Drums"), then files of the wanted extensions
  (filter by extension, do not open each file).
- Up/Down item; Right or Enter on a folder enters; Left or Backspace goes up (silent at
  the library root); Page Up/Down by 10; Home/End; letter search.
- Space: Preview On/Off as a mode (section 13); on folders silent, mode kept.
- Preview delayed by one 10 Hz timer tick during fast scrolling.
- Enter on a file loads it; if the target is occupied, confirm (section 7).
- Save browsers: Enter saves into the current folder (even an empty one); an existing
  file asks Y/N; N stays in the browser.
- Shift+F10 menu: New folder, Rename, Delete; F2 renames (keeping the extension); Delete
  asks Y/N (folders: warn about contents); deletion runs as an async task; file
  operations stay inside the browser's root.
- Escape: close without changes, focus back where it came from.
- Memory: each browser remembers its folder, index and selected path (user settings);
  a remembered path is accepted only inside the current library root.
- Multi-select (Shift+Space, Shift+arrows) speaks the destination ("Selected. Slot 4,
  Snare"); Ctrl+arrows choose the destination slot.

## 6. Properties and read-only text

- Properties (Alt+Enter): modal panel named "Properties", two read-only TextEditor
  columns. Up/Down by line "Key: value" with speech; Left/Right or Tab switch columns;
  Ctrl+arrows the neighbouring non-empty item; Escape closes.
- Everything graphics and meters show must be in properties as text: length, sample
  rate, channels, peak in dBFS, playback window.
- About: text in a read-only TextEditor (readable by line; Ctrl+A, Ctrl+C), buttons
  "Project", "Contact", "Close"; Tab cycles inside About; Escape and Alt+C close.

## 7. Dialogs

- Asynchronous AlertWindow only (enterModalState with a callback), never
  runModalLoop/showMessageBox (DLG-01).
- Text entry (save, rename, new folder): `a11y::askForText` in AccessibleDialog.h. Focus
  starts in the field with the text selected; Enter = OK, Escape = Cancel handled by the
  field itself; title says the action ("Save Slot", "Rename folder"); no description;
  internal Labels hidden from screen readers; explicit focus order field, OK, Cancel; no
  extra grabKeyboardFocus after enterModalState (TEST124, TEST125).
- Confirmation: `a11y::confirm`. Yes = Y, No = N and Escape; first focus on No; text names
  what will be lost and ends with "Press Y or N."; decide before starting background
  work (DLG-03).
- After the dialog, return focus in the callback (async) and speak the result once.
- The system save dialog does not warn if the final name changes later (prefix,
  extension): check the final path yourself (DLG-06).

## 8. Popup menus

- `PopupMenu::showMenuAsync` with `withTargetComponent` (menu at the element).
- Menu window title through LookAndFeel::preparePopupMenuWindow: NVDA says "Advanced,
  menu" (DLG-04).
- Cancel (result 0) returns focus to where the menu was opened.
- After a command that opened a dialog or browser, reopening the menu on the same item
  (`withInitiallySelectedItem(lastItem)`) lets the user continue.
- Do not open menus while focus is outside the plugin window (DLG-05).
- Disabled items stay visible, disabled, with a reason ("D:\Library (unavailable)").
- Context menus open with Shift+F10 / Applications key and expose the showMenu
  accessibility action (KEY-04).

## 9. Long operations and progress

- Bank load, import, export: background thread (one job at a time, low-priority
  ThreadPool); result back through MessageManager::callAsync with the editor's
  SafePointer.
- During the job: remember focus; disable all controls except a status element; make the
  status element focusable and focus it (only if focus was in the plugin); its title is
  "Loading bank, 42 percent".
- Speech: one phrase at start; the percent at most every 4 seconds and only while the
  status element has focus; at the end the result ("Bank loaded: Salamander" or the error
  and what to do) and focus back where it was.
- While the job runs, keys are consumed except Escape.
- Closing the plugin window does not wait for the job; destroying the processor does.
- Percent from real work (frames decoded, files done), not time.

## 10. Visual displays

Waveforms, meters, slot overviews, decorative frames:

```cpp
class VisualOnly : public juce::Component
{
public:
    VisualOnly() { setAccessible (false); setWantsKeyboardFocus (false); setInterceptsMouseClicks (false, false); }
};
```

Screen readers do not see them, the mouse goes through, real controls keep their focus
order. Adding a nicer GUI must not change any key handler or phrase (LSampler-24
"Vintage Rack GUI", TEST46).

## 11. Help and About

- HTML help embedded in the binary (juce_add_binary_data), written to a temp folder and
  opened in the default browser.
- In the HTML: `lang` on `<html>` and on each language section; headings in order
  (h1, h2); language navigation by links; focus to h1 when switching language; lists
  instead of tables where possible.
- Language menu with a title; cancel returns focus.
- Short spoken help (F1/F4) inside complex screens.

## 12. Page switcher (ToneTrace variant)

- Page buttons are a radio group; only the selected one is focusable
  (`setWantsKeyboardFocus(selected)`), so the row is one Tab stop.
- Left/Right, Home/End change the page and move focus to its button; title "Bands page
  selector", help with the keys.
- On change: `notifyAccessibilityEvent(valueChanged)`.
- Ctrl+Tab / Ctrl+Shift+Tab change the page from anywhere.

## 13. Preview modes

- Preview is an On/Off mode switched by one key, not "press again when the sound
  ended". The state is an explicit flag in the editor, never derived from whether a
  voice is playing (TEST96).
- Navigation with preview on does not restart a phrase that is already playing; it only
  remembers the new start point (TEST38).
- Preview through the same path as a MIDI note (slot settings) when the user listens
  "as it will sound"; raw for a browser file not yet loaded.
- Every preview stops when focus leaves the plugin and when the editor closes (FOC-08).

## 14. Accessibility settings

Some behaviours that help screen-reader users disturb sighted mouse users (keys that
follow focus, spoken announcements). Surge XT keeps them as user options and offers one
command, "Set recommended accessibility options", which turns on keyboard shortcuts,
menus and edit keys that follow keyboard focus, announcements, expanded menus, and
focus moves after actions. If your plugin needs such options:
- one menu item (and hotkey) to enable all recommended options at once, announced as a
  list of what changed;
- store them in user settings, not in presets;
- default for a plugin made for blind users: everything on.

## 15. WebView editors and other frameworks

- **JUCE 8 WebView UIs** (WebBrowserComponent with WebView2 on Windows): accessibility
  comes from the HTML (native buttons, labels, headings, aria-live), not from JUCE
  handlers. NVDA may treat the page as a document (browse mode) and take the arrow keys:
  for app-like screens use focus mode or `role="application"` deliberately and test with
  NVDA; announcements through an aria-live region; key routing to the DAW still needs
  care. These notes are general web knowledge, not tested in a plugin yet: verify.
- **Frameworks without screen-reader support:** say so plainly. The minimum that always
  works is excellent host parameters (OSARA and the DAW's generic editor read them);
  the full path is moving the editor to JUCE.
