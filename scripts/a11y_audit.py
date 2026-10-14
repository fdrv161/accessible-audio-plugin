#!/usr/bin/env python3
"""Статический аудит доступности и реального времени для плагинов на JUCE (VST3/CLAP).

Ищет типичные ошибки, из-за которых плагин неудобен или непригоден со скринридером
(NVDA, JAWS, Экранный диктор, VoiceOver), а также опасные вызовы в аудиопотоке.
Это эвристика: она показывает подозрительные места, но не заменяет проверку
со скринридером в DAW.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SOURCE_EXTENSIONS = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".mm"}
SKIP_DIRS = {
    ".git", "build", "builds", "_deps", "cmake-build-debug", "cmake-build-release",
    "juce", "clap-juce-extensions", "node_modules", "third_party", "thirdparty",
    "external", "vendor", "libs",
}
LEVELS = {"error": 0, "warning": 1, "info": 2}
LEVEL_NAMES = {"error": "ОШИБКА", "warning": "ПРЕДУПРЕЖДЕНИЕ", "info": "ЗАМЕТКА"}


@dataclass
class Finding:
    level: str
    rule: str
    path: Path
    line: int
    what: str
    fix: str
    code: str


# ---------------------------------------------------------------- разбор текста

def blank_comments(text: str, keep_strings: bool) -> str:
    """Заменяет комментарии (и при keep_strings=False — содержимое строк) пробелами.
    Переводы строк сохраняются, поэтому номера строк остаются верными."""
    out = []
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
        if c in "\"'":
            # Сырые строки R"( ... )" встречаются редко; обрабатываем как обычные.
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


def windows_excluded_lines(text: str) -> set[int]:
    """Номера строк, которые точно не компилируются под Windows
    (ветка #else после #if JUCE_WINDOWS, блоки #if JUCE_MAC и т.п.)."""
    excluded: set[int] = set()
    stack: list[dict] = []
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if line.startswith("#"):
            directive = line[1:].strip()
            if directive.startswith("if"):
                cond = directive
                is_windows = bool(re.search(r"JUCE_WINDOWS|_WIN32|_WIN64|JUCE_MSVC", cond))
                is_other = bool(re.search(r"JUCE_MAC|__APPLE__|JUCE_IOS|JUCE_LINUX|JUCE_BSD|JUCE_ANDROID", cond))
                negated = "!" in cond or directive.startswith("ifndef")
                if negated:
                    is_windows, is_other = is_other, is_windows
                stack.append({"win": is_windows, "other": is_other, "else": False})
                continue
            if directive.startswith("el") and stack:
                if directive.startswith("else"):
                    stack[-1]["else"] = True
                else:  # elif: считаем новую ветку неизвестной
                    stack[-1] = {"win": False, "other": False, "else": False}
                continue
            if directive.startswith("endif") and stack:
                stack.pop()
                continue
        for frame in stack:
            if (frame["win"] and frame["else"]) or (frame["other"] and not frame["else"]):
                excluded.add(number)
                break
    return excluded


def find_function_bodies(code: str, name_pattern: str):
    """Тела функций с заданным именем (определения, не объявления).
    Возвращает пары (начало тела, конец тела) в индексах code."""
    for match in re.finditer(name_pattern, code):
        i = match.end()
        depth = 1
        # пропустить список параметров
        while i < len(code) and depth > 0:
            if code[i] == "(":
                depth += 1
            elif code[i] == ")":
                depth -= 1
            i += 1
        # дальше либо ';' (объявление), либо '{' (определение), возможно через override/noexcept/const
        j = i
        while j < len(code) and code[j] not in "{;":
            j += 1
        if j >= len(code) or code[j] == ";":
            continue
        start = j
        depth = 0
        k = j
        while k < len(code):
            if code[k] == "{":
                depth += 1
            elif code[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        yield start, k


def code_line(lines: list[str], number: int) -> str:
    if 1 <= number <= len(lines):
        text = lines[number - 1].strip()
        return text if len(text) <= 140 else text[:137] + "..."
    return ""


# ---------------------------------------------------------------- правила по файлам

AUDIO_FORBIDDEN = [
    ("error", r"\bpostAnnouncement\s*\(|\bannounce\w*\s*\(", "озвучка из аудиопотока"),
    ("error", r"\bMessageManager\b|\bcallAsync\s*\(", "обращение к потоку сообщений из аудиопотока"),
    ("error", r"\bScoped(Read|Write)?Lock\b|\bstd::(lock_guard|unique_lock|scoped_lock)\b|\.lock\s*\(\s*\)",
     "блокировка в аудиопотоке"),
    ("error", r"\bnew\s+[A-Za-z_]|\bmake_unique\s*<|\bmake_shared\s*<", "выделение памяти в аудиопотоке"),
    ("error", r"\bFile(Input|Output)Stream\b|\bloadFileAs\w+\s*\(|\bXmlDocument\b|\bValueTree::fromXml\b",
     "работа с файлами или XML в аудиопотоке"),
    ("error", r"\bset(Title|Description|HelpText)\s*\(|\bnotifyAccessibilityEvent\s*\(",
     "вызов доступности из аудиопотока"),
    ("warning", r"\brepaint\s*\(|\bsendChangeMessage\s*\(|\btriggerAsyncUpdate\s*\(",
     "сообщения GUI из аудиопотока (лучше атомик и опрос таймером)"),
    ("warning", r"\bDBG\s*\(|\bstd::cout\b|\bLogger::write", "логирование в аудиопотоке"),
]

ASYNC_CALLS = r"\b(callAsync|callAfterDelay|launchAsync|showMenuAsync|showAsync|enterModalState|ModalCallbackFunction::create)\s*\("


def check_source(path: Path, text: str, findings: list[Finding], companion: str) -> dict:
    """companion — код всех файлов с тем же именем (Foo.h + Foo.cpp): проверки
    «чего-то не хватает» смотрят на пару файлов, а не на один."""
    lines = text.splitlines()
    code = blank_comments(text, keep_strings=False)       # для поиска вызовов
    code_with_strings = blank_comments(text, keep_strings=True)
    not_windows = windows_excluded_lines(text)
    facts = {"direct_uia": False}

    def add(level, rule, index, what, fix):
        number = line_of(code, index)
        findings.append(Finding(level, rule, path, number, what, fix, code_line(lines, number)))

    # E1: postAnnouncement на Windows
    for m in re.finditer(r"\bpostAnnouncement\s*\(", code):
        if line_of(code, m.start()) in not_windows:
            continue
        add("error", "E1 postAnnouncement", m.start(),
            "На Windows JUCE произносит postAnnouncement голосом SAPI (как Экранный диктор); NVDA и JAWS его не слышат.",
            "Озвучивать через UiaRaiseNotificationEvent на элементе окна: шаблон assets/templates/ScreenReaderAnnouncer.")

    # E2: «Ctrl нажат И Command не нажат» на Windows никогда не выполняется.
    # Ищем положительный isCtrlDown() (без «!» перед выражением) и отрицание
    # isCommandDown() в пределах одного оператора (между ; { }).
    reported_statements = set()
    for m in re.finditer(r"(!\s*)?[\w.>\-]*isCtrlDown\s*\(\s*\)", code):
        if m.group(1):
            continue
        left = max(code.rfind(";", 0, m.start()), code.rfind("{", 0, m.start()), code.rfind("}", 0, m.start()))
        rights = [i for i in (code.find(";", m.end()), code.find("{", m.end()), code.find("}", m.end())) if i >= 0]
        right = min(rights) if rights else len(code)
        if left in reported_statements:
            continue
        if re.search(r"!\s*[\w.>\-]*isCommandDown\s*\(", code[left + 1:right]):
            reported_statements.add(left)
            add("error", "E2 Ctrl без Command", m.start(),
                "На Windows isCommandDown() истинно при нажатом Ctrl, поэтому условие «Ctrl нажат и Command не нажат» никогда не выполнится: сочетание с Ctrl не работает.",
                "Убрать !isCommandDown() из условий, где проверяется нажатый Ctrl.")

    # E3: запрещённое в аудиопотоке
    for start, end in find_function_bodies(code, r"\b(processBlock|renderNextBlock|processBlockBypassed)\s*\("):
        body = code[start:end]
        for level, pattern, what in AUDIO_FORBIDDEN:
            for m in re.finditer(pattern, body):
                add(level, "E3 аудиопоток", start + m.start(), f"Внутри processBlock найдено: {what}.",
                    "Перенести в поток сообщений или фоновый поток; в аудиопотоке только атомики и заранее выделенная память.")

    # E4: прямой вызов UiaRaiseNotificationEvent (нужна линковка)
    if re.search(r"\bUiaRaiseNotificationEvent\s*\(", code):
        facts["direct_uia"] = True

    # W1: асинхронные лямбды с голым this
    for m in re.finditer(ASYNC_CALLS, code):
        window = code[m.end():m.end() + 400]
        bracket = window.find("[")
        if bracket < 0:
            continue
        close = window.find("]", bracket)
        if close < 0:
            continue
        captures = [c.strip() for c in window[bracket + 1:close].split(",")]
        if any(c in ("this", "=", "&", "*this") for c in captures):
            add("warning", "W1 голый this", m.start(),
                "Асинхронная лямбда захватывает this без SafePointer: если окно плагина закроют до вызова — падение DAW.",
                "Захватывать juce::Component::SafePointer<...> (this) и проверять на nullptr в начале лямбды.")

    # W2: мышь без клавиатуры
    has_mouse = re.search(r"\bvoid\s+mouse(Down|Up|DoubleClick|Drag)\s*\(", code)
    if has_mouse and not re.search(r"\bkeyPressed\s*\(|setWantsKeyboardFocus\s*\(\s*true|addKeyListener\s*\(|Button\b", companion):
        add("warning", "W2 только мышь", has_mouse.start(),
            "Компонент реагирует на мышь, но в файле нет обработки клавиш и фокуса: действие может быть недоступно с клавиатуры.",
            "Дать компоненту фокус (setWantsKeyboardFocus(true)), имя (setTitle) и клавиши для того же действия.")

    # W3: синхронные модальные окна
    for m in re.finditer(r"\brunModalLoop\s*\(|\bshowMessageBox\s*\(|NativeMessageBox::show(MessageBox|OkCancelBox|YesNoBox|YesNoCancelBox)\s*\(", code):
        add("warning", "W3 синхронный диалог", m.start(),
            "Синхронный модальный цикл в плагине: может зависнуть хост, фокус и озвучка ведут себя непредсказуемо.",
            "Асинхронный AlertWindow (enterModalState с колбэком) или showMessageBoxAsync/showAsync.")

    # W4: не-ASCII в строковых литералах без UTF-8
    for m in re.finditer(r'(u8|L|u|U)?"((?:[^"\\\n]|\\.)*)"', code_with_strings):
        literal = m.group(2)
        if m.group(1) == "u8" or all(ord(ch) < 128 for ch in literal):
            continue
        before = code_with_strings[max(0, m.start() - 60):m.start()]
        if re.search(r"fromUTF8|CharPointer_UTF8|CharPointer_UTF16|TRANS|NEEDS_TRANS", before):
            continue
        add("warning", "W4 кодировка строки", m.start(),
            "Не-ASCII символы в строке без явного UTF-8: после сборки могут стать «мусором» и в интерфейсе, и в речи.",
            "Обернуть в juce::String::fromUTF8(\"...\") или заменить на ASCII (обычный дефис вместо тире).")

    # W5: элемент с фокусом, скрытый от скринридера
    hidden = [line_of(code, m.start()) for m in re.finditer(r"setAccessible\s*\(\s*false\s*\)", code)]
    focus = [line_of(code, m.start()) for m in re.finditer(r"setWantsKeyboardFocus\s*\(\s*true\s*\)", code)]
    for h in hidden:
        if any(abs(h - f) <= 3 for f in focus):
            findings.append(Finding("warning", "W5 фокус без доступности", path, h,
                "Рядом setAccessible(false) и setWantsKeyboardFocus(true): элемент получает фокус, но скринридер его не видит — тишина.",
                "Либо сделать элемент доступным с именем, либо убрать у него фокус (для чисто визуальных дисплеев).",
                code_line(lines, h)))

    # W6: description как место для сообщений
    for m in re.finditer(r"\bsetDescription\s*\(\s*(message|text|msg|announcement|notification)\b", code):
        add("warning", "W6 сообщение в description", m.start(),
            "Временное сообщение записывается в description: оно «прилипнет» и будет звучать при каждом возврате на элемент.",
            "Озвучивать через анонсер, не меняя title/description; в description держать только горячую клавишу.")

    # W7: иконки-кнопки без имени
    m = re.search(r"\b(ImageButton|DrawableButton|ShapeButton)\b", code)
    if m and not re.search(r"\bsetTitle\s*\(", companion):
        add("warning", "W7 кнопка без имени", m.start(),
            "Кнопка-картинка без setTitle: скринридер скажет только «кнопка».",
            "Задать setTitle(\"действие\") и setDescription(\"горячая клавиша\").")

    # W9: меню по правому клику без Shift+F10. W2 этого не ловит, если в файле есть хоть какой-то keyPressed.
    m = re.search(r"\bisPopupMenu\s*\(|\bisRightButtonDown\s*\(", code)
    if m and not re.search(r"\bF10Key\b", companion):
        add("warning", "W9 меню только мышью", m.start(),
            "Действие или меню по правому клику, а Shift+F10 не обрабатывается: с клавиатуры его не открыть.",
            "В keyPressed элемента обрабатывать Shift+F10 (KeyPress::F10Key с Shift) и открывать то же меню через PopupMenu::showMenuAsync с withTargetComponent.")

    # I1: жесты автоматизации
    m = re.search(r"\bsetValueNotifyingHost\s*\(", code)
    if m and not re.search(r"\bbeginChangeGesture\s*\(", companion):
        add("info", "I1 жесты", m.start(),
            "Значение отправляется хосту без beginChangeGesture/endChangeGesture: запись автоматизации и «последний тронутый параметр» могут не работать.",
            "Обрамлять изменения с клавиатуры жестом begin/end, перетаскивание — одним жестом.")

    # I2: ползунки без текстового значения
    # Объявление ползунка: член класса, указатель или наследник (не juce::Slider::someColourId).
    m = re.search(r"\b(juce::)?Slider\s+\w+\s*[;{(]|public\s+(juce::)?Slider\b|<\s*(juce::)?Slider\s*>", code)
    if m and not re.search(r"SliderAttachment|SliderParameterAttachment|textFromValueFunction|setTextValueSuffix|"
                           r"AccessibilityTextValueInterface|getTextFromValue", companion):
        add("info", "I2 значение ползунка", m.start(),
            "Ползунок без текстового представления значения: скринридер может читать голое число или проценты.",
            "Задать textFromValueFunction с единицами или текстовый интерфейс значения (шаблон TextValueSlider.h).")

    # I3: titleChanged рядом с явной озвучкой — риск двойной речи
    for m in re.finditer(r"notifyAccessibilityEvent\s*\(\s*(juce::)?AccessibilityEvent::titleChanged", code):
        if line_of(code, m.start()) in not_windows:
            continue
        add("info", "I3 titleChanged", m.start(),
            "titleChanged вместе с явным анонсом даёт двойную или обрезанную фразу.",
            "Оставить один канал: либо анонс, либо смена фокуса/заголовка.")

    return facts


def check_cmake(path: Path, text: str, findings: list[Finding], any_direct_uia: bool) -> None:
    code = re.sub(r"#[^\n]*", lambda m: " " * len(m.group(0)), text)
    lines = text.splitlines()
    for m in re.finditer(r"\bjuce_add_plugin\s*\(", code):
        end = code.find(")", m.end())
        block = code[m.end():end if end > 0 else len(code)]
        if not re.search(r"EDITOR_WANTS_KEYBOARD_FOCUS\s+(TRUE|ON|1)", block, re.IGNORECASE):
            number = line_of(code, m.start())
            findings.append(Finding("warning", "W8 клавиатура окна", path, number,
                "В juce_add_plugin нет EDITOR_WANTS_KEYBOARD_FOCUS TRUE: окно плагина может не получать клавиши в DAW.",
                "Добавить EDITOR_WANTS_KEYBOARD_FOCUS TRUE.", code_line(lines, number)))
    if any_direct_uia and not re.search(r"uiautomationcore", code, re.IGNORECASE):
        findings.append(Finding("error", "E4 линковка UIA", path, 1,
            "UiaRaiseNotificationEvent вызывается напрямую, а библиотека Uiautomationcore не подключена: ошибка линковки.",
            "target_link_libraries(... Uiautomationcore OleAut32) под WIN32, или загружать функцию через GetProcAddress (как в шаблоне).",
            ""))


# ---------------------------------------------------------------- обход и вывод

def iter_files(paths: list[Path], include_third_party: bool):
    for root in paths:
        if root.is_file():
            yield root
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            if not include_third_party and any(part.lower() in SKIP_DIRS for part in path.relative_to(root).parts[:-1]):
                continue
            if path.suffix.lower() in SOURCE_EXTENSIONS or path.name == "CMakeLists.txt" or path.suffix.lower() == ".cmake":
                yield path


def main(argv: list[str]) -> int:
    # До argparse: справка --help тоже должна выводиться в UTF-8.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

    parser = argparse.ArgumentParser(
        prog="a11y_audit.py",
        description="Статический аудит доступности (NVDA, JAWS, Экранный диктор) и реального времени "
                    "для плагинов на JUCE. Вывод — простые строки без цвета.",
        epilog="Примеры:\n"
               "  python a11y_audit.py Source\n"
               "  python a11y_audit.py Source CMakeLists.txt --min-level warning\n"
               "  python a11y_audit.py . --strict\n\n"
               "Коды выхода: 0 — ошибок нет; 1 — найдены ошибки (с --strict также предупреждения);\n"
               "2 — неверные аргументы или путь не найден.\n"
               "Папки build, .git, JUCE, libs, external и подобные пропускаются (см. --include-third-party).",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="файлы или папки с исходниками и CMakeLists.txt")
    parser.add_argument("--min-level", choices=["error", "warning", "info"], default="info",
                        help="минимальный уровень вывода (по умолчанию info — всё)")
    parser.add_argument("--strict", action="store_true", help="считать предупреждения причиной кода выхода 1")
    parser.add_argument("--include-third-party", action="store_true", help="проверять и сторонние папки (JUCE, libs и т.п.)")
    parser.add_argument("--no-color", action="store_true", help="оставлено для совместимости: цвета не используются")
    args = parser.parse_args(argv)

    roots = [Path(p) for p in args.paths]
    missing = [str(p) for p in roots if not p.exists()]
    if missing:
        print("Путь не найден: " + ", ".join(missing) + ". Укажите существующую папку или файл.")
        return 2

    findings: list[Finding] = []
    sources, cmakes = [], []
    for path in iter_files(roots, args.include_third_party):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            print(f"Не удалось прочитать {path}: {error}")
            continue
        (cmakes if path.name == "CMakeLists.txt" or path.suffix.lower() == ".cmake" else sources).append((path, text))

    # Пары Foo.h + Foo.cpp в одной папке проверяются вместе на «отсутствие» чего-либо.
    companions: dict[tuple[Path, str], str] = {}
    for path, text in sources:
        key = (path.parent, path.stem.lower())
        companions[key] = companions.get(key, "") + "\n" + blank_comments(text, keep_strings=False)

    any_direct_uia = False
    for path, text in sources:
        facts = check_source(path, text, findings, companions[(path.parent, path.stem.lower())])
        any_direct_uia = any_direct_uia or facts["direct_uia"]
    for path, text in cmakes:
        check_cmake(path, text, findings, any_direct_uia)

    limit = LEVELS[args.min_level]
    shown = [f for f in findings if LEVELS[f.level] <= limit]
    shown.sort(key=lambda f: (LEVELS[f.level], str(f.path), f.line))

    def short(path: Path) -> str:
        # Короткий путь удобнее слушать скринридером.
        try:
            return str(path.resolve().relative_to(Path.cwd().resolve()))
        except ValueError:
            return str(path)

    for f in shown:
        print(f"{LEVEL_NAMES[f.level]} [{f.rule}] {short(f.path)}, строка {f.line}")
        print(f"  Что: {f.what}")
        print(f"  Исправить: {f.fix}")
        if f.code:
            print(f"  Код: {f.code}")
        print()

    counts = {level: sum(1 for f in findings if f.level == level) for level in LEVELS}
    print(f"Итого: ошибок {counts['error']}, предупреждений {counts['warning']}, заметок {counts['info']}. "
          f"Проверено файлов: {len(sources) + len(cmakes)}.")
    if not sources:
        print("Исходники C++ не найдены: проверьте путь.")
    print("Напоминание: это эвристика. Речь NVDA и поведение в DAW проверяются вручную.")

    if counts["error"] or (args.strict and counts["warning"]):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
