#!/usr/bin/env python3
"""Static accessibility and real-time audit for JUCE audio plugins (VST3, CLAP, AU).

Finds code patterns that make a plugin unusable or confusing with screen readers
(NVDA, JAWS, Narrator, VoiceOver) or unstable in a DAW. It is a heuristic: it points
at suspicious places; speech and host behaviour still need a manual check with a
screen reader. Rule IDs match references/audit-checklist.md (one section per ID).

Output is plain text without colours (screen-reader friendly) or JSON (--format json).
Exit codes: 0 no errors, 1 errors found (with --strict also warnings), 2 bad arguments.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path

SOURCE_EXTENSIONS = {".h", ".hpp", ".hh", ".hxx", ".cpp", ".cc", ".cxx", ".mm", ".inl"}
SKIP_DIRS = {
    ".git", "build", "builds", "_deps", "cmake-build-debug", "cmake-build-release", "out",
    "juce", "juce_modules", "modules", "juicelibrarycode", "clap-juce-extensions", "node_modules",
    "third_party", "thirdparty", "external", "externals", "vendor", "libs", "submodules",
}
LEVELS = {"error": 0, "warning": 1, "info": 2}
CATALOG = "references/audit-checklist.md"


@dataclass(frozen=True)
class Rule:
    level: str
    title: str
    problem: str
    fix: str


RULES: dict[str, Rule] = {
    "SPK-01": Rule("error", "postAnnouncement on Windows",
        "juce::AccessibilityHandler::postAnnouncement speaks through a separate SAPI voice on Windows; NVDA and JAWS never receive the text.",
        "Use a11y::announce from assets/templates/ScreenReaderAnnouncer (UIA notification). Keep postAnnouncement only inside a macOS-only branch."),
    "SPK-02": Rule("warning", "Announcement tied to a JUCE element provider",
        "The UIA notification is raised from the provider of a JUCE component (getNativeImplementation, an internal API). It is silent when that component is hidden, disabled or excluded from accessibility, and can break with any JUCE update.",
        "Use the notification window of assets/templates/ScreenReaderAnnouncer.cpp: it works regardless of focus and needs no JUCE internals."),
    "SPK-03": Rule("info", "Possible double or cut-off speech",
        "Two speech channels at once (focus change plus announcement, titleChanged plus announcement): NVDA speaks twice or cancels the announcement when focus moves.",
        "One channel per event. After a focus move let the focus speak; if an extra result is needed, use a11y::Announcer::say (delayed, queued)."),
    "SPK-04": Rule("warning", "Message written into title/description",
        "A temporary message is stored in the title, description or help text of a component: it 'sticks' and is read again every time the user returns to that element.",
        "Speak messages with a11y::announce/Announcer only. Keep description for the shortcut ('Alt+O') and title for the current state."),
    "SPK-08": Rule("warning", "Non-ASCII string literal without UTF-8",
        "A string literal with non-ASCII characters is not wrapped as UTF-8: it becomes garbage on screen and in speech (and juce::String asserts).",
        "Wrap it in juce::String::fromUTF8 (u8\"...\") or use ASCII (a plain hyphen instead of a dash)."),
    "FOC-04": Rule("warning", "Keyboard focus grabbed in a constructor",
        "grabKeyboardFocus() in a constructor runs before the component is on screen, so it is ignored: the window opens with no focus and keys go to the DAW.",
        "Grab focus once the editor is showing: MessageManager::callAsync with a SafePointer, check isShowing(), then grabKeyboardFocus() and getAccessibilityHandler()->grabFocus()."),
    "FOC-07": Rule("warning", "Focusable element hidden from screen readers",
        "setAccessible(false) next to setWantsKeyboardFocus(true): the element takes focus but the screen reader sees nothing (silence).",
        "Either make it accessible with a title, or remove its focus (purely visual displays)."),
    "KEY-01": Rule("error", "Ctrl test that never matches on Windows",
        "On Windows isCommandDown() is true whenever Ctrl is held, so 'isCtrlDown() && !isCommandDown()' is always false: the Ctrl shortcut never works.",
        "Remove '!isCommandDown()' from Ctrl tests, or use a11y::modsAre from assets/templates/A11yKeys.h."),
    "KEY-02": Rule("warning", "Modal screen lets key releases reach the DAW",
        "A modal-looking component handles keyPressed but not keyStateChanged: key releases (and unhandled keys) reach REAPER, which runs its own actions.",
        "In modal screens return true from keyStateChanged while open and consume unhandled keys (template ModalKeySurface.h)."),
    "KEY-03": Rule("warning", "Mouse-only control",
        "The component reacts to the mouse, but this file has no keyboard handling, focus or accessibility handler: the action is probably unreachable from the keyboard.",
        "Give it focus, a title and keys for the same action, or lay an a11y::ParameterOverlay/ActionOverlay over it (AccessibleOverlay.h)."),
    "KEY-04": Rule("warning", "Right-click menu without Shift+F10",
        "An action or menu opens on right-click, but Shift+F10 / the Applications key is not handled: it cannot be opened from the keyboard.",
        "Handle a11y::isContextMenuKey(key) on the focused element and open the same menu with PopupMenu::showMenuAsync(...withTargetComponent(this))."),
    "KEY-05": Rule("info", "Alt test without a Shift check",
        "A key branch tests isAltDown() but not Shift: Shift+Alt+key also enters it, so a more specific Shift+Alt shortcut is shadowed (LSampler-24 TEST157).",
        "Match modifiers exactly (a11y::is(key, code, cmd, alt, shift)) and test specific combinations before generic ones."),
    "KEY-06": Rule("warning", "First-letter search based on key codes",
        "Type-ahead compares getKeyCode() with the printable range: arrow, Page and F-key codes overlap ASCII, so arrows or F-keys start a letter search.",
        "Use getTextCharacter(), with a key-code fallback only for digits and punctuation: a11y::searchCharacter in A11yKeys.h."),
    "CTL-01": Rule("warning", "Focusable element without a name",
        "The component takes keyboard focus but never gets an accessible title: the screen reader says only the role ('button', 'unknown').",
        "setTitle(\"Action or name\") (setName is not read by screen readers), or override getTitle() in a custom AccessibilityHandler."),
    "CTL-02": Rule("info", "Slider value without text",
        "The slider has no text function: screen readers read a bare number (and some read the numeric range or a percentage).",
        "Set textFromValueFunction with units, or use TextValueSlider.h / ParameterOverlay (text-only value)."),
    "CTL-05": Rule("warning", "Read-only text that cannot be read line by line",
        "A read-only TextEditor without a visible caret: NVDA cannot move through it by line or word.",
        "setCaretVisible(true) on read-only TextEditors and keep arrows, Home/End, Page Up/Down inside the field."),
    "CTL-07": Rule("warning", "Toggle state never reported",
        "A custom handler uses a toggle/radio role but never reports a checked state: the screen reader always says 'not checked'.",
        "Override getCurrentState(): withCheckable(), plus withChecked() when on; raise valueChanged after a change."),
    "DLG-01": Rule("warning", "Synchronous modal dialog or menu",
        "A synchronous modal loop in a plugin (runModalLoop, showMessageBox, PopupMenu::show, FileChooser::browseFor...) can hang the host and breaks focus and speech.",
        "Use async APIs: AlertWindow::enterModalState with a callback, showMessageBoxAsync, PopupMenu::showMenuAsync, FileChooser::launchAsync."),
    "DLG-02": Rule("info", "AlertWindow focus order not set",
        "AlertWindow::addButton gives buttons focus order 1, so OK gets the first focus instead of the text field and NVDA announces both.",
        "setExplicitFocusOrder(1) on the field, 2 on OK, 3 on Cancel; hide internal Labels (template AccessibleDialog.h)."),
    "DLG-04": Rule("info", "Popup menus without a title",
        "PopupMenu is used, but no LookAndFeel sets a title on the menu window: NVDA says just 'menu'.",
        "Override LookAndFeel::preparePopupMenuWindow and call window.setTitle(\"Preset menu\")."),
    "PAR-01": Rule("error", "Parameter ID duplicated or hash collision",
        "Two parameters share an ID, or their IDs collide in the 31-bit hash that VST3 and CLAP use: the host sees one parameter for two, automation breaks.",
        "Make the IDs unique and keep them forever; check every ID list with --param-ids."),
    "PAR-03": Rule("warning", "Value sent to host without a gesture",
        "setValueNotifyingHost without beginChangeGesture/endChangeGesture: automation recording and 'last touched parameter' may not work.",
        "Wrap keyboard changes in begin/end per key press, mouse drags in one gesture (a11y::setByUser in A11yParameters.h)."),
    "PAR-07": Rule("warning", "Heavy work in get/setStateInformation",
        "File access or hashing inside getStateInformation/setStateInformation: the host calls them synchronously (save, close), REAPER and NVDA freeze.",
        "Cache expensive results (hash by path+size+date) and keep state calls fast."),
    "PAR-08": Rule("error", "DEF_CLASS_IID without DECLARE_CLASS_IID",
        "DEF_CLASS_IID(X) without DECLARE_CLASS_IID(X, ...): MSVC cannot find X_iid (LSampler-24 TEST117).",
        "Declare the interface IID with DECLARE_CLASS_IID before DEF_CLASS_IID."),
    "RT-01": Rule("error", "Forbidden call on the audio thread",
        "processBlock (or a voice render) contains speech, locks, allocation, files or messaging: audio dropouts or a frozen host.",
        "Move it to the message thread or a background thread; on the audio thread use only atomics and pre-allocated memory."),
    "RT-02": Rule("warning", "Async callback captures raw this",
        "An asynchronous callback captures 'this' without a SafePointer: if the plugin window closes before it runs, the DAW crashes.",
        "Capture juce::Component::SafePointer<...>(this) and return early if it is null."),
    "RT-03": Rule("warning", "Component deleted or rebuilt inside keyPressed",
        "keyPressed deletes, resets or removes components directly: the key event is still being dispatched (Windows, JUCE, NVDA), which crashes hosts (LSampler-24 TEST152).",
        "Defer it: MessageManager::callAsync with a SafePointer, after the key press has finished."),
    "RT-05": Rule("info", "ComboBox repopulated with notifications",
        "A ComboBox is cleared and refilled, and the selection is set with notifications: onChange re-enters while the list is half-built (LSampler-24 TEST152).",
        "Use juce::dontSendNotification while repopulating, or a re-entrancy guard."),
    "RT-06": Rule("warning", "LookAndFeel never reset",
        "setLookAndFeel(&x) without setLookAndFeel(nullptr): the LookAndFeel may be destroyed while components still use it (crash on close).",
        "Call setLookAndFeel(nullptr) in the destructor before the LookAndFeel object is destroyed."),
    "BLD-01": Rule("warning", "Editor does not ask for keyboard focus",
        "EDITOR_WANTS_KEYBOARD_FOCUS (CMake) / pluginEditorRequiresKeys (Projucer) is not set: the plugin window may never receive keys in the DAW.",
        "CMake: juce_add_plugin(... EDITOR_WANTS_KEYBOARD_FOCUS TRUE). Projucer: tick 'Plugin Editor Requires Keyboard Focus'."),
    "BLD-02": Rule("error", "JUCE without accessibility support",
        "JUCE older than 6.1 has no accessibility API at all: screen readers see an empty window.",
        "Upgrade JUCE (ask the user first: it can change behaviour and the build)."),
    "BLD-03": Rule("error", "UIA used without linking",
        "UiaRaiseNotificationEvent is called directly, but UIAutomationCore is not linked: link error (and the plugin fails to load on Windows older than 10 1709).",
        "Load the functions at run time like ScreenReaderAnnouncer.cpp, or link Uiautomationcore and OleAut32 under WIN32."),
}


@dataclass
class Finding:
    rule: str
    path: str
    line: int
    code: str = ""
    detail: str = ""
    level: str = field(default="")

    def __post_init__(self):
        if not self.level:
            self.level = RULES[self.rule].level


# ---------------------------------------------------------------- text preparation

def blank_code(text: str, keep_strings: bool) -> str:
    """Replace comments (and, unless keep_strings, string contents) with spaces.
    Newlines are kept, so line numbers and character offsets stay valid."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if c == "/" and nxt == "*":
            out.append("  ")
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append("  ")
                i += 2
            continue
        # Raw string R"delim( ... )delim" (may span lines).
        if c == "R" and nxt == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            open_paren = text.find("(", i + 2)
            if open_paren != -1 and open_paren - (i + 2) <= 16:
                delim = text[i + 2:open_paren]
                close = text.find(")" + delim + '"', open_paren)
                if close != -1:
                    end = close + len(delim) + 2
                    out.append('R"')
                    for ch in text[i + 2:end - 1]:
                        out.append(ch if keep_strings or ch == "\n" else " ")
                    out.append('"')
                    i = end
                    continue
        # Digit separator 1'000 is not a character literal.
        if c == "'" and i > 0 and text[i - 1].isalnum() and nxt.isalnum() and text[i - 1].isdigit():
            out.append(c)
            i += 1
            continue
        if c in "\"'":
            quote = c
            out.append(c)
            i += 1
            while i < n and text[i] != quote and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i:i + 2] if keep_strings else "  ")
                    i += 2
                    continue
                out.append(text[i] if keep_strings else " ")
                i += 1
            if i < n and text[i] == quote:
                out.append(quote)
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def code_line(lines: list[str], number: int) -> str:
    if 1 <= number <= len(lines):
        text = lines[number - 1].strip()
        return text if len(text) <= 160 else text[:157] + "..."
    return ""


def classify_condition(cond: str) -> str:
    has_win = bool(re.search(r"JUCE_WINDOWS|_WIN32|_WIN64|JUCE_MSVC|\bWIN32\b", cond))
    has_other = bool(re.search(r"JUCE_MAC|__APPLE__|JUCE_IOS|JUCE_LINUX|JUCE_BSD|JUCE_ANDROID|__linux__", cond))
    negated = "!" in cond or cond.startswith("ifndef")
    if has_win and has_other:
        return "unknown"
    if has_win:
        return "other" if negated else "win"
    if has_other:
        return "unknown" if negated else "other"
    return "unknown"


def lines_not_built_on_windows(text: str) -> set[int]:
    """Line numbers inside preprocessor branches that are certainly not compiled on Windows."""
    excluded: set[int] = set()
    stack: list[dict] = []
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if line.startswith("#"):
            directive = line[1:].strip()
            if directive.startswith("if"):
                kind = classify_condition(directive)
                stack.append({"kind": kind, "had_win": kind == "win"})
                continue
            if directive.startswith("elif") and stack:
                kind = classify_condition(directive)
                stack[-1]["kind"] = kind
                stack[-1]["had_win"] = stack[-1]["had_win"] or kind == "win"
                continue
            if directive.startswith("else") and stack:
                stack[-1]["kind"] = "other" if stack[-1]["had_win"] else "unknown"
                continue
            if directive.startswith("endif") and stack:
                stack.pop()
                continue
        if any(frame["kind"] == "other" for frame in stack):
            excluded.add(number)
    return excluded


def match_brace(code: str, open_index: int, open_ch: str = "{", close_ch: str = "}") -> int:
    depth = 0
    for k in range(open_index, len(code)):
        if code[k] == open_ch:
            depth += 1
        elif code[k] == close_ch:
            depth -= 1
            if depth == 0:
                return k
    return len(code) - 1


CONTROL_WORDS = {
    "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "decltype",
    "static_assert", "noexcept", "throw", "new", "delete", "defined", "operator", "else",
    "do", "try", "case", "requires", "co_await", "co_return", "alignas",
}
FUNC_NAME_RE = re.compile(r"([A-Za-z_~]\w*(?:\s*::\s*~?[A-Za-z_]\w*)*)\s*\(")


@dataclass
class Function:
    name: str           # last name component
    qualified: str      # e.g. Editor::Editor
    start: int          # index of the name
    body_start: int     # index of '{'
    body_end: int       # index of matching '}'


def skip_ws(code: str, i: int) -> int:
    while i < len(code) and code[i].isspace():
        i += 1
    return i


def find_functions(code: str) -> list[Function]:
    """Function definitions (with bodies), including in-class members and constructors."""
    result: list[Function] = []
    for m in FUNC_NAME_RE.finditer(code):
        qualified = re.sub(r"\s+", "", m.group(1))
        name = qualified.split("::")[-1]
        if name in CONTROL_WORDS or not name:
            continue
        before = code[max(0, m.start() - 2):m.start()]
        if before.endswith(".") or before.endswith("->"):
            continue  # member call, not a definition
        close = match_brace(code, m.end() - 1, "(", ")")
        j = skip_ws(code, close + 1)
        # qualifiers after the parameter list
        while True:
            q = re.match(r"(const|override|final|noexcept(\s*\([^)]*\))?|mutable|volatile|&&|&|->\s*[\w:<>,\s*&]+?(?=\s*[{:]))", code[j:])
            if not q:
                break
            j = skip_ws(code, j + q.end())
        if j < len(code) and code[j] == ":" and code[j:j + 2] != "::":
            # constructor initializer list: name(args) or name{args}, separated by commas
            j = skip_ws(code, j + 1)
            while j < len(code):
                ident = re.match(r"[A-Za-z_][\w:<>]*", code[j:])
                if not ident:
                    break
                j = skip_ws(code, j + ident.end())
                if j < len(code) and code[j] in "({":
                    j = skip_ws(code, match_brace(code, j, code[j], ")" if code[j] == "(" else "}") + 1)
                else:
                    break
                if j < len(code) and code[j] == ",":
                    j = skip_ws(code, j + 1)
                    continue
                break
        if j < len(code) and code[j] == "{":
            result.append(Function(name, qualified, m.start(), j, match_brace(code, j)))
    return result


LAMBDA_RE = re.compile(r"\]\s*(\([^()]*(?:\([^()]*\)[^()]*)*\))?\s*(mutable\s*)?(noexcept\s*)?(->\s*[\w:<>]+\s*)?\{")


def lambda_ranges(code: str, start: int, end: int) -> list[tuple[int, int]]:
    ranges = []
    for m in LAMBDA_RE.finditer(code, start, end):
        open_index = m.end() - 1
        ranges.append((open_index, match_brace(code, open_index)))
    return ranges


def inside(index: int, ranges: list[tuple[int, int]]) -> bool:
    return any(a <= index <= b for a, b in ranges)


def class_ranges(code: str) -> list[tuple[str, int, int, str, int]]:
    """(name, body_start, body_end, base clause, keyword index) for class/struct definitions."""
    out = []
    for m in re.finditer(r"\b(class|struct)\s+(?:\w+\s+)*?([A-Za-z_]\w*)\s*(final\s*)?(:[^{;()]*)?\{", code):
        prefix = code[max(0, m.start() - 6):m.start()]
        if "enum" in prefix:
            continue
        open_index = m.end() - 1
        out.append((m.group(2), open_index, match_brace(code, open_index), m.group(4) or "", m.start()))
    return out


# ---------------------------------------------------------------- per-file checks

AUDIO_FORBIDDEN = [
    ("error", r"\bpostAnnouncement\s*\(|\bannounce\w*\s*\(|\bUiaRaise\w+\s*\(", "speech"),
    ("error", r"\bMessageManager\b|\bcallAsync\s*\(", "message-thread call"),
    ("error", r"\bScoped(Read|Write)?Lock\b|\bstd::(lock_guard|unique_lock|scoped_lock)\b|\.lock\s*\(\s*\)", "lock"),
    ("error", r"\bnew\s+[A-Za-z_]|\bmake_unique\s*<|\bmake_shared\s*<|\.push_back\s*\(|\.emplace_back\s*\(|\.resize\s*\(", "memory allocation"),
    ("error", r"\bFile(Input|Output)Stream\b|\bloadFileAs\w+\s*\(|\bXmlDocument\b|\bValueTree::fromXml\b|\bjuce::File\b", "file or XML access"),
    ("error", r"\bset(Title|Description|HelpText)\s*\(|\bnotifyAccessibilityEvent\s*\(", "accessibility call"),
    ("warning", r"\brepaint\s*\(|\bsendChangeMessage\s*\(|\btriggerAsyncUpdate\s*\(", "GUI messaging (prefer an atomic polled by a timer)"),
    ("warning", r"\bDBG\s*\(|\bstd::cout\b|\bLogger::write", "logging"),
]

ASYNC_CALLS = r"\b(callAsync|callAfterDelay|launchAsync|showMenuAsync|showAsync|enterModalState|ModalCallbackFunction::create|showMessageBoxAsync|showOkCancelBox|showYesNoBox)\s*\("
ANNOUNCE_CALL = r"\bpostAnnouncement\s*\(|\bUiaRaiseNotificationEvent\s*\(|\bannounce(ToActiveScreenReader|ToScreenReader)?\s*\("
MODAL_CLASS = r"(Modal|Dialog|Browser|Popup|Properties|About|Surface)"
FRAMEWORK_WIDGET_BASE = r"public\s+(juce::)?(\w*Button|Slider|ComboBox|TextEditor|ListBox|TreeView|TableListBox|Label|Viewport|ScrollBar)\b"


class FileChecker:
    def __init__(self, path: Path, display: str, text: str, companion: str, project: "ProjectFacts", out: list[Finding]):
        self.path, self.display, self.text = path, display, text
        self.lines = text.splitlines()
        self.code = blank_code(text, keep_strings=False)
        self.code_with_strings = blank_code(text, keep_strings=True)
        self.companion = companion
        self.project = project
        self.out = out
        self.not_windows = lines_not_built_on_windows(text)
        self.functions = find_functions(self.code)
        self.classes = class_ranges(self.code)

    def add(self, rule: str, index: int, detail: str = "", level: str = "") -> None:
        number = line_of(self.code, index)
        self.out.append(Finding(rule, self.display, number, code_line(self.lines, number), detail, level))

    def body(self, f: Function) -> str:
        return self.code[f.body_start:f.body_end]

    def functions_named(self, pattern: str):
        return [f for f in self.functions if re.fullmatch(pattern, f.name)]

    def is_constructor(self, f: Function) -> bool:
        parts = f.qualified.split("::")
        if len(parts) >= 2 and parts[-1] == parts[-2]:
            return True
        return any(name == f.name and start < f.start < end for name, start, end, _, _ in self.classes)

    def run(self) -> None:
        code, comp = self.code, self.companion

        # SPK-01 postAnnouncement outside macOS-only branches
        for m in re.finditer(r"\bpostAnnouncement\s*\(", code):
            if line_of(code, m.start()) not in self.not_windows:
                self.add("SPK-01", m.start())

        # SPK-02 notification raised from a JUCE element provider
        for m in re.finditer(r"\bgetNativeImplementation\s*\(", code):
            self.add("SPK-02", m.start())

        # SPK-03 titleChanged events; focus move + announcement in one function
        for m in re.finditer(r"notifyAccessibilityEvent\s*\(\s*(juce::)?AccessibilityEvent::titleChanged", code):
            if line_of(code, m.start()) not in self.not_windows:
                self.add("SPK-03", m.start(), "titleChanged event: make sure no announcement says the same text.")
        for f in self.functions:
            body = self.body(f)
            focus = re.search(r"\bgrabKeyboardFocus\s*\(|(->|\.)\s*grabFocus\s*\(", body)
            speak = re.search(ANNOUNCE_CALL, body)
            if focus and speak and f.name not in ("announce", "announceToActiveScreenReader"):
                self.add("SPK-03", f.body_start + speak.start(),
                         f"Function '{f.name}' moves focus and announces: the focus speech may cut the announcement off.")

        # SPK-04 messages stored in title/description/help
        for m in re.finditer(r"\bset(Description|HelpText|Title)\s*\(\s*(message|msg|announcement|notification|text|spoken|speech)\s*\)", code):
            if m.group(1) == "Title" and m.group(2) in ("text",):
                continue
            self.add("SPK-04", m.start())
        for f in self.functions:
            body = self.body(f)
            if re.search(ANNOUNCE_CALL, body):
                for m in re.finditer(r"\bset(Description|HelpText)\s*\(\s*(?!\)|\{\s*\}|juce::String\s*\(\s*\)|\")[A-Za-z_]", body):
                    already = any(x.rule == "SPK-04" and x.line == line_of(code, f.body_start + m.start()) for x in self.out)
                    if not already:
                        self.add("SPK-04", f.body_start + m.start(), "The same function also announces: the message probably stays in the description.")

        # SPK-08 non-ASCII literals
        for m in re.finditer(r'(u8|L|u|U)?"((?:[^"\\\n]|\\.)*)"', self.code_with_strings):
            literal = m.group(2)
            if m.group(1) == "u8" or all(ord(ch) < 128 for ch in literal):
                continue
            before = self.code_with_strings[max(0, m.start() - 60):m.start()]
            if re.search(r"fromUTF8|CharPointer_UTF8|CharPointer_UTF16|\bTRANS\b|NEEDS_TRANS", before):
                continue
            self.add("SPK-08", m.start())

        # FOC-04 focus in constructors (not inside deferred lambdas)
        for f in self.functions:
            if not self.is_constructor(f):
                continue
            lambdas = lambda_ranges(code, f.body_start + 1, f.body_end)
            for m in re.finditer(r"\bgrabKeyboardFocus\s*\(", code[f.body_start:f.body_end]):
                index = f.body_start + m.start()
                if not inside(index, lambdas):
                    self.add("FOC-04", index)

        # FOC-07 focusable but excluded from accessibility (within 3 lines)
        hidden = [line_of(code, m.start()) for m in re.finditer(r"setAccessible\s*\(\s*false\s*\)", code)]
        focus_lines = [line_of(code, m.start()) for m in re.finditer(r"setWantsKeyboardFocus\s*\(\s*true\s*\)", code)]
        for h in hidden:
            if any(abs(h - f) <= 3 for f in focus_lines):
                self.out.append(Finding("FOC-07", self.display, h, code_line(self.lines, h)))

        # KEY-01 Ctrl and not Command
        reported = set()
        for m in re.finditer(r"(!\s*)?[\w.>\-]*isCtrlDown\s*\(\s*\)", code):
            if m.group(1):
                continue
            left = max(code.rfind(";", 0, m.start()), code.rfind("{", 0, m.start()), code.rfind("}", 0, m.start()))
            rights = [i for i in (code.find(";", m.end()), code.find("{", m.end()), code.find("}", m.end())) if i >= 0]
            right = min(rights) if rights else len(code)
            if left in reported:
                continue
            if re.search(r"!\s*[\w.>\-]*isCommandDown\s*\(", code[left + 1:right]):
                reported.add(left)
                self.add("KEY-01", m.start())

        # KEY-02 modal-looking class without keyStateChanged
        for name, start, end, bases, keyword in self.classes:
            if not re.search(MODAL_CLASS, name) or re.search(r"\bModalKeySurface\b", bases):
                continue
            class_code = code[start:end]
            if re.search(r"\bkeyPressed\s*\(", class_code) and not re.search(r"\bkeyStateChanged\s*\(", comp):
                self.add("KEY-02", keyword)

        # KEY-03 mouse without keyboard
        mouse = re.search(r"\bvoid\s+mouse(Down|Up|DoubleClick|Drag)\s*\(", code)
        if mouse and not re.search(r"\bkeyPressed\s*\(|setWantsKeyboardFocus\s*\(\s*true|addKeyListener\s*\(|createAccessibilityHandler|"
                                   + FRAMEWORK_WIDGET_BASE, comp):
            self.add("KEY-03", mouse.start())

        # KEY-04 right-click without Shift+F10
        m = re.search(r"\bisPopupMenu\s*\(|\bisRightButtonDown\s*\(", code)
        if m and not re.search(r"\bF10Key\b|isContextMenuKey", comp):
            if self.project.has_shift_f10:
                self.add("KEY-04", m.start(), "Shift+F10 is handled elsewhere in the project: check that it reaches this control's menu.", level="info")
            else:
                self.add("KEY-04", m.start())

        # KEY-05 Alt test without Shift check inside key handlers
        for f in self.functions_named(r"keyPressed|handleKey\w*|\w*KeyPress\w*"):
            body = self.body(f)
            for cond in re.finditer(r"\bif\s*\(", body):
                close = match_brace(body, cond.end() - 1, "(", ")")
                condition = body[cond.end():close]
                positive_key = re.search(r"==\s*(juce::)?KeyPress::\w+|\bisKeyCode\s*\(|\b\w*Key\s*==", condition)
                alt_tests = re.finditer(r"(!\s*)?(?:[\w:]+\s*(?:\(\s*\))?\s*(?:\.|->)\s*)*isAltDown\s*\(\s*\)", condition)
                positive_alt = any(not t.group(1) for t in alt_tests)
                if (positive_alt and positive_key
                        and not re.search(r"isShiftDown|isAnyModifierKeyDown|modsAre|getRawFlags|ModifierKeys\s*\(", condition)):
                    self.add("KEY-05", f.body_start + cond.start())

        # KEY-06 type-ahead from key codes
        for f in self.functions:
            body = self.body(f)
            if "getKeyCode" in body and "getTextCharacter" not in body and re.search(r">=?\s*33\b|<=\s*126\b|<\s*127\b", body):
                at = re.search(r">=?\s*33\b|<=\s*126\b|<\s*127\b", body)
                self.add("KEY-06", f.body_start + at.start())

        # CTL-01 focusable custom component without a title; icon buttons without a title
        has_name = re.search(r"\bsetTitle\s*\(|\bgetTitle\s*\(\s*\)\s*const|createAccessibilityHandler", comp)
        for m in re.finditer(r"(?<![.>\w])setWantsKeyboardFocus\s*\(\s*true\s*\)", code):
            if not has_name:
                self.add("CTL-01", m.start())
                break
        m = re.search(r"\b(ImageButton|DrawableButton|ShapeButton)\b", code)
        if m and not re.search(r"\bsetTitle\s*\(", comp):
            self.add("CTL-01", m.start(), "Icon button: the screen reader has no text to read.")

        # CTL-02 slider without text
        m = re.search(r"\b(juce::)?Slider\s+\w+\s*[;{(]|public\s+(juce::)?Slider\b|<\s*(juce::)?Slider\s*>", code)
        if m and not re.search(r"SliderAttachment|SliderParameterAttachment|textFromValueFunction|setTextValueSuffix|"
                               r"AccessibilityTextValueInterface|getTextFromValue|TextValueSlider", comp):
            self.add("CTL-02", m.start())

        # CTL-05 read-only text without caret
        m = re.search(r"\bsetReadOnly\s*\(\s*true\s*\)", code)
        if m and not re.search(r"setCaretVisible\s*\(\s*true\s*\)", comp):
            self.add("CTL-05", m.start())

        # CTL-07 toggle role without checked state
        m = re.search(r"AccessibilityRole::(toggleButton|radioButton)", code)
        if m and re.search(r"AccessibilityHandler\s*\(|public\s+(juce::)?AccessibilityHandler", code) and "withChecked" not in comp:
            self.add("CTL-07", m.start())

        # DLG-01 synchronous dialogs and menus
        sync = (r"\brunModalLoop\s*\(|\bshowMessageBox\s*\(|\bshowModalDialog\s*\(|\bbrowseFor(FileToOpen|FileToSave|Directory|MultipleFilesToOpen)\s*\("
                r"|\b\w*[mM]enu\w*\s*(\.|->)\s*show(At)?\s*\(|PopupMenu\s*\(\s*\)\s*\.\s*show\s*\(")
        for m in re.finditer(sync, code):
            self.add("DLG-01", m.start())

        # DLG-02 AlertWindow text field without focus order
        m = re.search(r"\baddTextEditor\s*\(", code)
        if m and not re.search(r"setExplicitFocusOrder\s*\(", comp):
            self.add("DLG-02", m.start())

        # PAR-03 value without gesture
        m = re.search(r"\bsetValueNotifyingHost\s*\(", code)
        if m and not re.search(r"\bbeginChangeGesture\s*\(|\bsetByUser\s*\(", comp):
            self.add("PAR-03", m.start())

        # PAR-07 heavy state calls
        for f in self.functions_named(r"getStateInformation|setStateInformation|getCurrentProgramStateInformation|setCurrentProgramStateInformation"):
            heavy = re.search(r"\bFile(Input|Output)Stream\b|\bloadFileAs\w+\s*\(|\bSHA256\b|\bMD5\b|\bfindChildFiles\s*\(|\bjuce::File\b|\bFile\s+\w+\s*[({=]", self.body(f))
            if heavy:
                self.add("PAR-07", f.body_start + heavy.start())

        # RT-01 audio thread
        for f in self.functions_named(r"processBlock|processBlockBypassed|renderNextBlock|process|processSample|processStereo"):
            if f.name in ("process", "processSample", "processStereo") and "AudioBuffer" not in self.code[f.start:f.body_start] \
                    and "ProcessContext" not in self.code[f.start:f.body_start]:
                continue
            body = self.body(f)
            for level, pattern, what in AUDIO_FORBIDDEN:
                for m in re.finditer(pattern, body):
                    self.add("RT-01", f.body_start + m.start(), f"Found on the audio thread: {what}.", level=level)

        # RT-02 async lambdas with raw this
        for m in re.finditer(ASYNC_CALLS, code):
            window = code[m.end():m.end() + 400]
            bracket = window.find("[")
            if bracket < 0 or window[:bracket].count(";") > 0:
                continue
            close = window.find("]", bracket)
            if close < 0:
                continue
            captures = [c.strip() for c in window[bracket + 1:close].split(",")]
            if any("SafePointer" in c or c.lower().startswith(("safe", "weak")) for c in captures):
                continue
            if any(c in ("this", "=", "&", "*this") for c in captures):
                self.add("RT-02", m.start())

        # RT-03 deleting or rebuilding inside keyPressed (not deferred)
        owned = set(re.findall(r"(?:unique_ptr|shared_ptr|ScopedPointer|OptionalScopedPointer)\s*<[^;{}]*?>\s+(\w+)\s*[;{=]", comp))
        for f in self.functions_named(r"keyPressed"):
            lambdas = lambda_ranges(code, f.body_start + 1, f.body_end)
            for m in re.finditer(r"\bdelete\s+\w|(\w+)\s*(?:\.|->)\s*reset\s*\(\s*\)|\bremoveChildComponent\s*\(|\bdeleteAllChildren\s*\(|\bremoveAllChildren\s*\(",
                                 code[f.body_start:f.body_end]):
                if m.group(1) and m.group(1) not in owned:
                    continue  # reset() of a bitset/selection, not of an owned component
                index = f.body_start + m.start()
                if not inside(index, lambdas):
                    self.add("RT-03", index)

        # RT-05 ComboBox repopulation with notifications
        for f in self.functions:
            body = self.body(f)
            if re.search(r"\.clear\s*\(", body) and re.search(r"\baddItem\s*\(", body):
                for m in re.finditer(r"\bsetSelected(Id|ItemIndex)\s*\(", body):
                    close = match_brace(body, m.end() - 1, "(", ")")
                    if "dontSendNotification" not in body[m.end():close]:
                        self.add("RT-05", f.body_start + m.start())
                        break

        # RT-06 LookAndFeel never reset
        m = re.search(r"\bsetLookAndFeel\s*\(\s*&|\bsetDefaultLookAndFeel\s*\(\s*&", code)
        if m and not re.search(r"\bset(Default)?LookAndFeel\s*\(\s*nullptr", comp):
            self.add("RT-06", m.start())

        # PAR-08 IID macros (project-level facts collected here)
        for m in re.finditer(r"\bDEF_CLASS_IID\s*\(\s*(\w+)\s*\)", code):
            if m.group(1) not in self.project.declared_iids:
                self.add("PAR-08", m.start())

        # DLG-04 menus without a title (reported once per project)
        if not self.project.has_menu_title and not self.project.reported_menu_title:
            m = re.search(r"\bPopupMenu\b", code)
            if m:
                self.project.reported_menu_title = True
                self.add("DLG-04", m.start())


# ---------------------------------------------------------------- project-level facts

@dataclass
class ProjectFacts:
    has_shift_f10: bool = False
    has_menu_title: bool = False
    reported_menu_title: bool = False
    declared_iids: set = field(default_factory=set)
    direct_uia: bool = False
    juce_version: tuple | None = None
    juce_version_source: str = ""


def juce_hash(identifier: str) -> int:
    """JUCE String::hashCode over code points (uint32, multiplier 31), masked to 31 bits
    as JUCE's VST3 wrapper does by default (JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS).
    clap-juce-extensions uses the same IDs for CLAP."""
    h = 0
    for ch in identifier:
        h = (31 * h + ord(ch)) & 0xFFFFFFFF
    return h & 0x7FFFFFFF


PARAM_ID_PATTERNS = [
    r"\bParameterID\s*[{(]\s*\"([^\"]+)\"",
    r"\bAudioParameter(?:Float|Int|Bool|Choice)\s*>\s*\(\s*\"([^\"]+)\"",
    r"\bnew\s+(?:juce::)?AudioParameter(?:Float|Int|Bool|Choice)\s*\(\s*\"([^\"]+)\"",
]


def collect_param_ids(display: str, text: str, ids: list[tuple[str, str, int]]) -> None:
    code = blank_code(text, keep_strings=True)
    seen_positions = set()
    for pattern in PARAM_ID_PATTERNS:
        for m in re.finditer(pattern, code):
            # make_unique<AudioParameterFloat> (ParameterID {"x", 1}, ...) matches twice: keep one
            pos = m.start(1)
            if pos in seen_positions:
                continue
            seen_positions.add(pos)
            ids.append((m.group(1), display, line_of(code, m.start())))


def check_param_ids(ids: list[tuple[str, str, int]], out: list[Finding], source_note: str = "") -> None:
    first_by_id: dict[str, tuple[str, int]] = {}
    first_by_hash: dict[int, tuple[str, str, int]] = {}
    for pid, path, line in ids:
        if pid in first_by_id:
            p0, l0 = first_by_id[pid]
            out.append(Finding("PAR-01", path, line, "", f"ID '{pid}' is also created at {p0}:{l0}{source_note}."))
            continue
        first_by_id[pid] = (path, line)
        h = juce_hash(pid)
        if h in first_by_hash and first_by_hash[h][0] != pid:
            other, p0, l0 = first_by_hash[h]
            out.append(Finding("PAR-01", path, line, "", f"IDs '{pid}' and '{other}' ({p0}:{l0}) have the same VST3/CLAP hash {h}{source_note}."))
        else:
            first_by_hash.setdefault(h, (pid, path, line))


def read_param_id_file(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    try:
        data = json.loads(text)
    except ValueError:
        return [line.strip() for line in text.splitlines() if line.strip() and not line.strip().startswith("#")]
    found: list[str] = []

    def walk(node):
        if isinstance(node, str):
            found.append(node)
        elif isinstance(node, list):
            for item in node:
                walk(item)
        elif isinstance(node, dict):
            for key in ("id", "paramID", "parameterID", "ID"):
                if isinstance(node.get(key), str):
                    found.append(node[key])
                    return
            for value in node.values():
                if isinstance(value, (list, dict)):
                    walk(value)
    walk(data)
    return found


def detect_juce_version(path: Path, display: str, text: str, facts: ProjectFacts) -> None:
    if facts.juce_version:
        return
    patterns = [
        r"FetchContent_Declare\s*\(\s*JUCE[^)]*?GIT_TAG\s+v?(\d+)\.(\d+)",
        r"juce-framework/JUCE[#@]v?(\d+)\.(\d+)",
        r"find_package\s*\(\s*JUCE\s+(\d+)\.(\d+)",
        r"jucerVersion=\"(\d+)\.(\d+)",
        r"#define\s+JUCE_MAJOR_VERSION\s+(\d+)[\s\S]*?#define\s+JUCE_MINOR_VERSION\s+(\d+)",
    ]
    for pattern in patterns:
        m = re.search(pattern, text, re.IGNORECASE)
        if m:
            facts.juce_version = (int(m.group(1)), int(m.group(2)))
            facts.juce_version_source = display
            return


def check_build_file(display: str, text: str, out: list[Finding], facts: ProjectFacts, is_jucer: bool) -> None:
    lines = text.splitlines()
    if is_jucer:
        if "JUCERPROJECT" in text and re.search(r"projectType=\"audioplug\"", text) and not re.search(r"pluginEditorRequiresKeys(?!=\"0\")", text):
            out.append(Finding("BLD-01", display, 1, "", "Projucer project: 'Plugin Editor Requires Keyboard Focus' is off."))
        return
    code = re.sub(r"#[^\n]*", lambda m: " " * len(m.group(0)), text)
    for m in re.finditer(r"\bjuce_add_plugin\s*\(", code):
        end = match_brace(code, m.end() - 1, "(", ")")
        block = code[m.end():end]
        if not re.search(r"EDITOR_WANTS_KEYBOARD_FOCUS\s+(TRUE|ON|1|YES)\b", block, re.IGNORECASE):
            number = line_of(code, m.start())
            out.append(Finding("BLD-01", display, number, code_line(lines, number)))
    if facts.direct_uia and re.search(r"\btarget_link_libraries\b", code) and not re.search(r"uiautomationcore", code, re.IGNORECASE):
        out.append(Finding("BLD-03", display, 1))


# ---------------------------------------------------------------- driver

def iter_files(roots: list[Path], include_third_party: bool):
    for root in roots:
        if root.is_file():
            yield root
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            parts = [p.lower() for p in path.relative_to(root).parts[:-1]]
            if not include_third_party and any(p in SKIP_DIRS for p in parts):
                continue
            suffix = path.suffix.lower()
            if suffix in SOURCE_EXTENSIONS or path.name == "CMakeLists.txt" or suffix in (".cmake", ".jucer"):
                yield path


def juce_header_version(roots: list[Path]) -> tuple[tuple[int, int] | None, str]:
    """Look for JUCE's own version header even inside skipped third-party folders."""
    for root in roots:
        if root.is_file():
            continue
        for header in root.rglob("juce_StandardHeader.h"):
            text = header.read_text(encoding="utf-8", errors="replace")
            major = re.search(r"#define\s+JUCE_MAJOR_VERSION\s+(\d+)", text)
            minor = re.search(r"#define\s+JUCE_MINOR_VERSION\s+(\d+)", text)
            if major and minor:
                return (int(major.group(1)), int(minor.group(1))), str(header)
    return None, ""


SUPPRESS_RE = re.compile(r"a11y-ignore:\s*([A-Z]{2,3}-\d{2}(?:\s*,\s*[A-Z]{2,3}-\d{2})*)")
SUPPRESS_FILE_RE = re.compile(r"a11y-ignore-file:\s*([A-Z]{2,3}-\d{2}(?:\s*,\s*[A-Z]{2,3}-\d{2})*)")


def is_suppressed(finding: Finding, texts: dict[str, list[str]]) -> bool:
    lines = texts.get(finding.path)
    if not lines:
        return False
    for line in lines:
        m = SUPPRESS_FILE_RE.search(line)
        if m and finding.rule in [x.strip() for x in m.group(1).split(",")]:
            return True
    for number in (finding.line, finding.line - 1):
        if 1 <= number <= len(lines):
            m = SUPPRESS_RE.search(lines[number - 1])
            if m and finding.rule in [x.strip() for x in m.group(1).split(",")]:
                return True
    return False


def list_rules() -> None:
    for rule_id, rule in RULES.items():
        print(f"{rule_id} ({rule.level}): {rule.title}")
        print(f"  Problem: {rule.problem}")
        print(f"  Fix: {rule.fix}")
        print(f"  Details: {CATALOG}#{rule_id.lower()}")
        print()


def main(argv: list[str]) -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

    parser = argparse.ArgumentParser(
        prog="a11y_audit.py",
        description="Static accessibility (NVDA, JAWS, Narrator, VoiceOver) and real-time audit for JUCE plugins. "
                    "Plain text output without colours, or JSON.",
        epilog="Examples:\n"
               "  python a11y_audit.py Source CMakeLists.txt\n"
               "  python a11y_audit.py . --format json > audit.json\n"
               "  python a11y_audit.py Source --min-level warning --skip CTL-02,SPK-03\n"
               "  python a11y_audit.py . --param-ids VST3_AUTOMATION_MANIFEST.json\n"
               "  python a11y_audit.py --list-rules\n\n"
               "Suppress a reviewed false positive with a comment on the same or previous line:\n"
               "  // a11y-ignore: RT-02 lambda runs synchronously\n"
               "or for a whole file: // a11y-ignore-file: CTL-02\n\n"
               "Exit codes: 0 no errors; 1 errors found (with --strict also warnings); 2 bad arguments.\n"
               "Folders such as build, .git, JUCE, libs, modules, external are skipped (see --include-third-party).",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", help="source files or folders, CMakeLists.txt, .jucer")
    parser.add_argument("--format", choices=["text", "json"], default="text", help="output format (default text)")
    parser.add_argument("--min-level", choices=["error", "warning", "info"], default="info", help="lowest level to show (default info)")
    parser.add_argument("--skip", default="", help="comma-separated rule IDs to skip")
    parser.add_argument("--only", default="", help="comma-separated rule IDs to run (others are skipped)")
    parser.add_argument("--param-ids", action="append", default=[], metavar="FILE",
                        help="also check IDs from a file (one per line, or JSON with 'id' fields) for duplicates and hash collisions")
    parser.add_argument("--strict", action="store_true", help="warnings also make the exit code 1")
    parser.add_argument("--include-third-party", action="store_true", help="also scan JUCE, libs, external and similar folders")
    parser.add_argument("--list-rules", action="store_true", help="print all rules and exit")
    parser.add_argument("--no-color", action="store_true", help="accepted for compatibility; output never has colours")
    args = parser.parse_args(argv)

    if args.list_rules:
        list_rules()
        return 0
    if not args.paths:
        print("No paths given. Example: python a11y_audit.py Source CMakeLists.txt (see --help).")
        return 2

    roots = [Path(p) for p in args.paths]
    missing = [str(p) for p in roots if not p.exists()]
    if missing:
        print("Path not found: " + ", ".join(missing) + ". Give an existing folder or file.")
        return 2
    unknown = [r for r in (args.skip + "," + args.only).split(",") if r.strip() and r.strip() not in RULES]
    if unknown:
        print("Unknown rule ID: " + ", ".join(unknown) + ". See --list-rules.")
        return 2

    cwd = Path.cwd().resolve()

    def display(path: Path) -> str:
        try:
            return path.resolve().relative_to(cwd).as_posix()
        except ValueError:
            return path.as_posix()

    sources, builds = [], []
    for path in iter_files(roots, args.include_third_party):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            print(f"Cannot read {path}: {error}")
            continue
        is_build = path.name == "CMakeLists.txt" or path.suffix.lower() in (".cmake", ".jucer")
        (builds if is_build else sources).append((path, display(path), text))

    facts = ProjectFacts()
    blanked = {}
    for path, shown, text in sources:
        code = blank_code(text, keep_strings=False)
        blanked[shown] = code
        facts.has_shift_f10 |= bool(re.search(r"\bF10Key\b|isContextMenuKey", code))
        facts.has_menu_title |= "preparePopupMenuWindow" in code
        facts.direct_uia |= bool(re.search(r"(?<!->)(?<!\.)\bUiaRaiseNotificationEvent\s*\(", code)) and "GetProcAddress" not in code
        facts.declared_iids |= set(re.findall(r"\bDECLARE_CLASS_IID\s*\(\s*(\w+)", code))
        detect_juce_version(path, shown, text, facts)
    for path, shown, text in builds:
        detect_juce_version(path, shown, text, facts)
    if not facts.juce_version:
        version, source = juce_header_version(roots)
        if version:
            facts.juce_version, facts.juce_version_source = version, display(Path(source))

    companions: dict[tuple[Path, str], str] = {}
    for path, shown, text in sources:
        key = (path.parent, path.stem.lower())
        companions[key] = companions.get(key, "") + "\n" + blanked[shown]

    findings: list[Finding] = []
    for path, shown, text in sources:
        FileChecker(path, shown, text, companions[(path.parent, path.stem.lower())], facts, findings).run()
    for path, shown, text in builds:
        check_build_file(shown, text, findings, facts, path.suffix.lower() == ".jucer")

    if facts.juce_version and facts.juce_version < (6, 1):
        findings.append(Finding("BLD-02", facts.juce_version_source, 1, "",
                                f"Detected JUCE {facts.juce_version[0]}.{facts.juce_version[1]}."))

    param_ids: list[tuple[str, str, int]] = []
    for path, shown, text in sources:
        collect_param_ids(shown, text, param_ids)
    check_param_ids(param_ids, findings)
    extra_ids_count = 0
    for id_file in args.param_ids:
        p = Path(id_file)
        if not p.exists():
            print(f"Parameter ID file not found: {id_file}.")
            return 2
        ids = read_param_id_file(p)
        extra_ids_count += len(ids)
        check_param_ids([(pid, display(p), n + 1) for n, pid in enumerate(ids)], findings, " (from --param-ids)")

    skip = {r.strip() for r in args.skip.split(",") if r.strip()}
    only = {r.strip() for r in args.only.split(",") if r.strip()}
    findings = [f for f in findings if f.rule not in skip and (not only or f.rule in only)]

    line_cache = {shown: text.splitlines() for _, shown, text in sources + builds}
    shown_findings, suppressed = [], []
    for f in findings:
        (suppressed if is_suppressed(f, line_cache) else shown_findings).append(f)

    # de-duplicate identical rule+place
    unique, seen = [], set()
    for f in shown_findings:
        key = (f.rule, f.path, f.line, f.detail)
        if key not in seen:
            seen.add(key)
            unique.append(f)
    shown_findings = unique

    limit = LEVELS[args.min_level]
    visible = sorted((f for f in shown_findings if LEVELS[f.level] <= limit),
                     key=lambda f: (LEVELS[f.level], f.path, f.line, f.rule))
    counts = {level: sum(1 for f in shown_findings if f.level == level) for level in LEVELS}
    version_text = (f"{facts.juce_version[0]}.{facts.juce_version[1]} (from {facts.juce_version_source})"
                    if facts.juce_version else "not detected")

    if args.format == "json":
        report = {
            "files_checked": len(sources) + len(builds),
            "juce_version": version_text,
            "parameter_ids_found_in_source": len(param_ids),
            "parameter_ids_from_files": extra_ids_count,
            "summary": {"errors": counts["error"], "warnings": counts["warning"], "infos": counts["info"],
                        "suppressed": len(suppressed)},
            "findings": [dict(asdict(f), title=RULES[f.rule].title, problem=RULES[f.rule].problem,
                              fix=RULES[f.rule].fix, details=f"{CATALOG}#{f.rule.lower()}") for f in visible],
        }
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        for f in visible:
            rule = RULES[f.rule]
            print(f"{f.level.upper()} {f.rule} {rule.title}. {f.path}, line {f.line}")
            print(f"  Problem: {rule.problem}")
            if f.detail:
                print(f"  Note: {f.detail}")
            print(f"  Fix: {rule.fix}")
            if f.code:
                print(f"  Code: {f.code}")
            print(f"  Details: {CATALOG}#{f.rule.lower()}")
            print()
        print(f"Summary: {counts['error']} errors, {counts['warning']} warnings, {counts['info']} notes, "
              f"{len(suppressed)} suppressed. Files checked: {len(sources) + len(builds)}. JUCE version: {version_text}. "
              f"Literal parameter IDs found: {len(param_ids)}"
              + (f", plus {extra_ids_count} from ID files." if extra_ids_count else "."))
        if not sources:
            print("No C++ sources found: check the path.")
        if not param_ids and not extra_ids_count:
            print("Parameter IDs built at run time are not checked: pass a manifest with --param-ids.")
        print("Reminder: static heuristics only. Speech and DAW behaviour need a manual check with a screen reader.")

    if counts["error"] or (args.strict and counts["warning"]):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
