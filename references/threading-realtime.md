# Threads, real time, editor lifetime

Why this is part of accessibility: a 20-second freeze of REAPER and NVDA when closing the
plugin window (LSampler-24 TEST104), or crackling audio, makes a plugin as unusable as a
silent screen reader. A crash after closing a dialog costs a blind user a project they
could not even see.

## 1. Audio thread (processBlock), RT-01

Forbidden: speech and any accessibility call, MessageManager, callAsync, locks
(ScopedLock), allocation (new, make_shared, growing a std::vector, creating a
juce::String), files, XML/ValueTree parsing, host notifications, logging.

Allowed: atomics, pre-allocated buffers, swapping ready-made state snapshots.

LSampler-24 patterns:
- **Triple-buffered snapshots.** The control thread builds the state in a "write" buffer
  and atomically swaps it with the "middle" one (the "new" flag lives in the same atomic
  as the index). The audio thread takes the middle buffer when it sees the flag. Old
  samples are freed by the control thread, never by the audio thread.
- **Commands through atomics:** `requestPreviewStart(slot)`, `requestPreviewStop()`. The
  GUI stores a command, the audio thread runs it. Pack related fields into one atomic so
  they cannot tear (slot + kind + item).
- **Idle:** no voices, MIDI or commands: clear the output and return. Preview commands and
  MIDI wake the block up. Skip unchanged state sync during idle (TEST140 to TEST142).
- **Observation for the GUI** (meters): separate atomics the audio thread only writes and
  the GUI reads and resets.

## 2. Control thread and background jobs, RT-07

- Heavy work (decoding, SHA-256, copying, archives) in a `juce::ThreadPool` with one
  low-priority thread; one job at a time (`compare_exchange` on a "busy" flag). In JUCE 9
  the function given to `addJob` must return `void` or `ThreadPoolJob::JobStatus`.
- The result returns via `MessageManager::callAsync`, checking that the processor is still
  alive (shutdown flag) and holding the editor's SafePointer.
- The processor destructor sets the shutdown flag and waits for the job. Closing the
  editor does not wait: when it opens again it sees the job and shows progress.
- Cache expensive results the host may request synchronously (`getStateInformation` on
  save or close): recomputing SHA-256 of 384 WAV files froze REAPER and NVDA (TEST104); a
  cache by path + size + date fixed it (PAR-07).

## 3. Safe asynchronous GUI calls, RT-02 to RT-05

- Always `juce::Component::SafePointer` in lambdas for `callAsync`,
  `Timer::callAfterDelay`, `launchAsync`, `showMenuAsync`, `enterModalState`. A raw
  `this` crashes the DAW if the user closed the plugin window while a dialog was open.
- **Never delete, reset or rebuild components inside their own keyPressed (RT-03).**
  Closing a modal editor with Escape: only `MessageManager::callAsync(... close ...)` in the
  handler. Opening or closing a view that recreates a ComboBox during the key dispatch
  crashed REAPER (TEST152): defer it until the key press has finished.
- **Close a temporary TextEditor before hiding or refilling its owner (RT-04).** The
  entry field of a Slider is a child TextEditor; hiding or repopulating the slider while
  the field has focus crashed REAPER. Order: apply the text, move focus,
  `hideTextBox(discard)`, then `callAsync(next step)`.
- **Repopulating lists (RT-05):** `dontSendNotification`, a re-entrancy guard against
  onChange/onValueChange, reset auxiliary indexes before leaving a mode, explicit bounds
  checks (TEST152).
- Host callbacks (`getContextMenuForParameter`) run on the message thread, and their
  results are used while the menu object is alive.

## 4. Editor lifetime

- Constructor: set up components, restore cursor memory from the processor (never
  overwrite it with defaults during initialisation, TEST45), defer warnings and the first
  focus (callAsync) so modal windows have a parent and focus works (FOC-04).
- 10 Hz timer: check the state revision and update only what changed; check window focus
  and stop previews when it is lost; show background job progress.
- Destructor: stop all previews, close modal screens, `setLookAndFeel(nullptr)` on every
  component before the LookAndFeel objects are destroyed (RT-06).
- UI-only state (cursor position, preview mode) lives in processor memory or user
  settings, not in presets or the project, unless the user asks otherwise (RT-08).
