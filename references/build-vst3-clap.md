# Сборка VST3 и CLAP (CMake + JUCE)

## 1. Базовый CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyPlugin VERSION 1.0.0 LANGUAGES C CXX)

if(MSVC)
    # Статический рантайм: плагин не зависит от установленных VC++ redist.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3          # фиксируй конкретный тег, не ветку; в чужом проекте — его версия
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MyPlugin
    COMPANY_NAME "My Company"
    PLUGIN_MANUFACTURER_CODE Myco      # 4 символа, хотя бы одна заглавная
    PLUGIN_CODE Mypl                   # 4 символа, уникально у производителя
    FORMATS VST3                       # + AU, Standalone при необходимости
    PRODUCT_NAME "My Plugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE   # важно: без этого окно плагина может не получать клавиши
    COPY_PLUGIN_AFTER_BUILD FALSE)     # не копировать в системные папки без спроса

target_sources(MyPlugin PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ScreenReaderAnnouncer.cpp)

target_compile_features(MyPlugin PRIVATE cxx_std_20)
target_compile_definitions(MyPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MyPlugin PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
```

Заметки:
- Версия JUCE. Новый проект — последний тег 9.0.x (в октябре 2026 —
  9.0.3). В существующем проекте не меняй версию JUCE без просьбы
  пользователя: обновление может сломать сборку и поведение. Что важно при
  переходе с 8 на 9: `ThreadPool::addJob` принимает только функцию,
  возвращающую `void` или `ThreadPoolJob::JobStatus` (лямбда с
  `return true;` не соберётся), а `Drawable` больше не `Component`
  (оборачивай в `DrawableComponent`). Ломающих изменений в API
  доступности, клавиш и фокуса в 9.x нет; остальное — в `BREAKING_CHANGES.md`
  JUCE.
- Шаблон анонсера сам загружает функции UI Automation из
  `UIAutomationCore.dll` во время работы, поэтому дополнительная линковка
  не нужна. Если пишешь прямой вызов `UiaRaiseNotificationEvent`, добавь
  `if(WIN32) target_link_libraries(MyPlugin PRIVATE Uiautomationcore OleAut32) endif()`
  (так сделано в LSampler-24) — но тогда плагин не загрузится на Windows
  старше 10 версии 1709.
- Справка в бинарнике: `juce_add_binary_data(MyPluginHelp SOURCES
  Help/Help.html)`, затем линковать `MyPluginHelp` и подключать
  `<BinaryData.h>` в файле, где она используется (без include — ошибки
  C2653/C3861, TEST76).
- Не вызывай `COPY_PLUGIN_AFTER_BUILD TRUE` и установку в
  `C:\Program Files\Common Files\VST3` без разрешения пользователя.

## 2. CLAP через clap-juce-extensions

В JUCE 8 и 9 (проверено на 9.0.3) штатного формата CLAP нет, хотя его
обещали в 9-й версии. Стандартный путь — проект
free-audio/clap-juce-extensions: он поддерживает JUCE 6–8, а JUCE 9 — с
июля 2026 года. Для JUCE 9 бери свежий коммит `main` и фиксируй его в
подмодуле. Если в твоей версии JUCE появился `FORMATS CLAP`, используй
штатный путь и проверь совместимость ID параметров.

```cmake
# После JUCE. Вариант с подмодулем:
#   git submodule add -b main https://github.com/free-audio/clap-juce-extensions.git libs/clap-juce-extensions
#   git submodule update --init --recursive    # тянет clap и clap-helpers
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

clap_juce_extensions_plugin(TARGET MyPlugin
    CLAP_ID "com.mycompany.myplugin"          # обратный домен, навсегда
    CLAP_FEATURES audio-effect equalizer)     # или instrument sampler и т.п.
```

- Создаётся цель `MyPlugin_CLAP`, редактор и процессор — те же самые, что у
  VST3. Вся доступность работает одинаково: окно — тот же JUCE-редактор.
- `CLAP_USE_JUCE_PARAMETER_RANGES` (`OFF` по умолчанию: хост видит 0..1;
  `DISCRETE`; `ALL`) — выбери один раз до первого релиза и не меняй: это
  часть контракта параметров. Текст значений в любом случае берётся из
  `getText` параметра.
- ID параметров CLAP вычисляются тем же хешем JUCE, что и у VST3: проверка
  коллизий одна на оба формата.
- Можно указать путь к существующим исходникам через переменные
  (как в ToneTrace: `JUCE_DIR`, `CLAP_JUCE_EXTENSIONS_DIR`), чтобы не
  скачивать при каждой сборке.
- В REAPER CLAP и VST3 одного плагина — разные FX с разными списками
  параметров; проверяй оба.

## 3. Сборка в GitHub Actions (Windows)

Подход LSampler-24: локально не собирают, сборка и тесты — в Actions на
`windows-latest`, результат — ZIP с `.vst3` и текстом о версии и коммите.

```yaml
name: Build
on: { push: { branches: [ main ] }, workflow_dispatch: {} }
permissions: { contents: read }
jobs:
  windows:
    runs-on: windows-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - name: Static accessibility audit
        run: python .claude/skills/accessible-audio-plugin/scripts/a11y_audit.py Source CMakeLists.txt
      - name: Configure
        run: cmake -S . -B build -G "Visual Studio 17 2022" -A x64
      - name: Build
        run: cmake --build build --config Release
      - name: Tests
        run: ctest --test-dir build -C Release --output-on-failure
      - uses: actions/upload-artifact@v4
        with:
          name: MyPlugin-Windows
          path: |
            build/MyPlugin_artefacts/Release/VST3/
            build/MyPlugin_artefacts/Release/CLAP/
```

Шаг аудита можно убрать, если навык не лежит в репозитории плагина.
Путь к артефакту CLAP у clap-juce-extensions может отличаться — сверься с
выводом сборки.

## 4. Локальная сборка на Windows

Visual Studio Build Tools с компонентом «Desktop development with C++»,
CMake 3.22+. Пример команд:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Устанавливать инструменты глобально — только с разрешения пользователя.

## 5. Проверка после сборки

- Плагин открывается в REAPER (VST3 и CLAP), NVDA объявляет первый элемент.
- Список параметров в REAPER/OSARA: имена и тексты значений.
- Ручной план из `testing.md`.
