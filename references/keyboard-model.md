# Keyboard model

Contents: 1. Principles. 2. Reference layout: parameter grid. 3. Simple editors.
4. Main screen and lists. 5. Modal screens and pages. 6. Letter search.
7. Conflicts with REAPER and screen readers. 8. JUCE key quirks. 9. Key routing.

## 1. Principles

- Same action, same key on every screen. Users learn the model once (KEY-12).
- Every function works from the keyboard; the mouse is an extra (KEY-03).
- Everything a sighted user opens with a right-click (MIDI learn, reset, presets,
  automation) opens with **Shift+F10** and the **Applications key** on the focused
  element (a11y::isContextMenuKey). Blind users try these first. Open the menu at the
  element (`withTargetComponent`); cancel returns focus (KEY-04).
- Shortcuts are discoverable: in the button description ("Alt+O"), in the spoken short
  help (F1) and in the manual (KEY-10).
- Keys not used by a plugin screen are consumed so they cannot run DAW actions.
  Deliberate exceptions: Space on the main screen (DAW transport), Alt+F4, Alt+Tab, and
  screen-reader keys (Insert/CapsLock combinations, NVDA+Up Arrow).
- Edges are silent, no wrap-around; exception: page switchers and modes where cycling is
  expected (Tab through pages).
- Navigation is separate from editing: plain arrows explore, so values never change by
  accident while the user listens around.

## 2. Reference layout: parameter grid ("Grid / Value", LSampler-24)

The grid is one focusable element with rows "Name, value", grouped in categories
(pages), up to 8 rows per column.

In Grid:
- Up/Down: previous/next parameter in the category.
- Left/Right: previous/next category, keeping the row when possible.
- Alt+Shift+Left/Right: first parameter of the previous/next category (TEST129).
- Home/End: first/last in the category; Ctrl+Home/End: first/last in the whole grid.
- Page Up/Down: 8 rows (or to the category edge).
- Letter, digit or punctuation: next parameter starting with it; Shift+letter: previous.
- Alt+Up/Down: value by one step.
- Alt+Page Up/Down: coarse step (LSampler-24: step x 40; choose for your ranges).
- Alt+Left/Right: step multiplier (1, 5, 10, 15, 20), announced "Step 5".
- Alt+Home/End: maximum/minimum.
- Backspace: default value.
- Shift+Up/Down/Left/Right: next parameter with an active host envelope (TEST134).
- Ctrl+Shift+A: toggle host automation of this parameter; Alt+Shift+A: deactivate all
  active envelopes of this plugin instance (TEST158). See parameters-automation.md.
- Enter (or Tab, or Alt+V): go to Value. Escape: leave the grid one level up.

In Value:
- Up/Down: value +/- step; Left/Right: step multiplier; Page Up/Down: coarse;
  Home/End: maximum/minimum. Alt variants work the same.
- Tab: exact entry field (TextEditor accepting "-0123456789."). Enter applies and
  returns to Grid; Escape cancels.
- Enter or Shift+Tab: back to Grid. Escape: Grid.

AccessibleParameterList.h implements a simplified version of this model.

## 3. Simple editors (few controls, each a separate element)

Allowed alternative (ToneTrace, Surge XT): arrows change the value of the focused
control directly.
- Up/Right +step, Down/Left -step; Shift+arrows fine step (Surge XT); Page Up/Down
  coarse step.
- Home/End: limits. Platform convention (Windows trackbar, ARIA) is Home = minimum;
  LSampler-24 and Surge XT use Home = maximum. Pick one rule for the whole plugin.
- Backspace or Delete: default value (Surge XT uses Delete).
- Enter: exact entry. Shift+F10: context menu.
- Tab/Shift+Tab: next/previous control. Control groups (oscillator, filter, effects):
  keys to jump to the next/previous group (Surge XT has "Move Focus to Next/Previous
  Control Group"); a multi-cell widget is one Tab stop.
TextValueSlider.h and ParameterOverlay implement this model.

## 4. Main screen and lists

- Grid of items (24 slots as 3 columns of 8): arrows by rows and columns, Home/End
  start/end of the column, Ctrl+Home/End first/last. Enter opens the item; on an empty
  one say "Empty slot".
- Operations on the current item: Alt+C/X/V copy, cut, paste; Alt+R rename (F2 in
  browsers); Delete clear (with confirmation if it loses data); Alt+Delete clear all;
  Alt+Enter properties.
- Ctrl+Up/Down inside nested screens: previous/next non-empty item, keeping the current
  parameter and screen. If there is none: "Only one loaded slot".
- Alt+L ("where am I"): always back to the main list or the grid of the current screen.
- A "state" hotkey that speaks the current context (Surge XT: "Announce Editor State";
  it speaks the patch, scene, selected oscillator and its type, modulator and FX slot).
- Rows of action buttons: Tab/Shift+Tab cycle between the buttons and the main list;
  arrows, Home/End, Page keys on buttons are consumed.

## 5. Modal screens and pages

- A modal screen (file browser, slice editor, properties, About) receives every key,
  including key releases (`keyStateChanged` returns true), and passes nothing to the DAW
  (KEY-02, ModalKeySurface.h).
- Escape closes the screen or leaves a nested mode one level up.
- Opening hotkeys: either a toggle (Alt+M opens, Alt+M closes) or "go there" from
  anywhere (LSampler-24 TEST145 changed Alt+F to always open the FX screen, with Escape
  to leave). Choose one behaviour for all screen hotkeys.
- Pages inside a screen: F1, F2, F3 go directly; Tab/Shift+Tab cycle. An unavailable
  page does not open and says why ("Slice Sequencer inactive"). F4 (or F1 without pages):
  short spoken help.
- Page switcher alternative (ToneTrace): one Tab stop (roving focus: only the selected
  page button is focusable), Left/Right change the page, Ctrl+Tab/Ctrl+Shift+Tab from
  anywhere.
- Screens with selection: Shift+arrows range, Shift+Space toggle item, Ctrl+Delete clear
  selection, Ctrl+C/V copy/paste, Ctrl+Z/Y undo/redo (inside editors with their own
  history only).
- Read-only text (About, properties): arrows, Home/End, Page keys move inside the text,
  Ctrl+A selects all, Ctrl+C copies (TEST159).

## 6. Letter search

- Plain key: forward with wrap; Shift+key: backward. Letters, digits and punctuation.
- Search the name the user HEARS, not a host or internal name, and skip repeated
  prefixes ("Loop 3, ", "Slice ") and values (TEST129).
- Use `a11y::searchCharacter(key)`: the typed character first; key codes only as a
  fallback for digits and punctuation (some hosts send no text character for them,
  TEST135); never F-keys or navigation keys, whose codes overlap ASCII (Up is '&', F1 is
  'p', TEST40, TEST134); Shift+digit gives the digit, not "!".
- No match: silent, like an edge.

## 7. Conflicts with REAPER and screen readers

- REAPER receives every key the plugin does not consume and runs its action (Space
  transport, R record, Ctrl+S save project, Ctrl+Z undo project). The plugin must
  return true for everything that belongs to it. REAPER's FX window option "Send all
  keyboard input to plug-in" helps users.
- Never use screen-reader keys: NVDA+..., Insert/CapsLock combinations, NVDA+Up Arrow.
  Ctrl alone stops speech: never bind an action to Ctrl alone.
- Space: main screen = DAW. Preview by an explicit toggle (LSampler-24 Alt+P: "Slot
  preview On" / "DAW space On"). A ComboBox opens its popup on Space and Enter: make
  sure that does not steal Space from the DAW (TEST146).
- Ctrl+S, Ctrl+Z and similar on a normal plugin screen: consume them (otherwise REAPER
  saves or undoes the project); inside editors with their own history use them.
- F1 in REAPER opens REAPER help: a plugin's modal screen may take it.
- Alt+F4 and Alt+Tab are never consumed (KEY-08).

## 8. JUCE key quirks

- **Windows: Ctrl = Command.** `ModifierKeys::isCommandDown()` is true while Ctrl is held
  on Windows and Linux. `isCtrlDown() && !isCommandDown()` never matches (KEY-01). Use
  `a11y::modsAre` / `a11y::is` (exact modifier match).
- **Exact modifiers.** Every branch tests all modifiers, and specific combinations come
  before generic ones; otherwise Shift+Alt+Up runs the Alt+Up branch (KEY-05, TEST157).
- **Ctrl+letter / Alt+letter:** `getTextCharacter()` may be a control character or 0;
  compare `CharacterFunctions::toLowerCase((juce_wchar) key.getKeyCode())` (a11y::is does).
- **Applications key:** JUCE on Windows reports key code 0x5D with no text character
  (Surge XT handles it); ']' has the same code but types a character.
- **Layouts:** JUCE letter codes are Latin. A Russian layout does not break Alt+O, but
  letter search in non-Latin names depends on `getTextCharacter()`.
- **Listeners:** `addKeyListener(editor)` on children plus their own `keyPressed` handle
  a key twice (KEY-11). Use one entry point.
- **Unknown host behaviour:** when a key does not arrive or arrives strangely, add the
  temporary keyboard diagnostic from testing.md and let the user report what JUCE
  receives (LSampler-24 TEST136 to TEST138).

## 9. Key routing

Structure that worked well (one entry point `handleKeyPress(key, source)`, called first
by every control):

1. A background operation is running: consume everything except Escape.
2. A modal screen is open (About, properties, browser, editor): give it the key and
   return true.
3. Global commands of the current context (Alt+letters), each with an explicit list of
   screens where it is allowed.
4. Value mode / entry field keys.
5. Grid / list navigation and letter search.
6. At the end of a screen that owns the keyboard: `return true` (consume). On the main
   screen, `return false` only for keys deliberately given to the DAW (Space).

Templates (AccessibleListCell, AccessibleParameterList, TextValueSlider, overlays) return
false for keys they do not use, so this editor-level routing decides; the editor must
then consume them (step 6).
