# Building VST3 and CLAP (CMake or Projucer + JUCE)

Contents: 1. CMake project. 2. Projucer project. 3. CLAP. 4. GitHub Actions.
5. Local build on Windows. 6. After the build.

Version facts below were checked in October 2026. Re-check them (SKILL.md, "Facts that
expire") before relying on them.

## 1. CMake project

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyPlugin VERSION 1.0.0 LANGUAGES C CXX)

if(MSVC)
    # Static runtime: the plugin does not depend on installed VC++ redistributables.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4          # pin a tag, never a branch; in an existing project keep its version
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MyPlugin
    COMPANY_NAME "My Company"
    PLUGIN_MANUFACTURER_CODE Myco      # 4 characters, at least one upper case
    PLUGIN_CODE Mypl                   # 4 characters, unique for the manufacturer
    FORMATS VST3                       # + AU, Standalone as needed (Standalone helps testing)
    PRODUCT_NAME "My Plugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE   # without it the window may never get keys (BLD-01)
    COPY_PLUGIN_AFTER_BUILD FALSE)     # never copy into system folders without asking

target_sources(MyPlugin PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ScreenReaderAnnouncer.cpp)  # the only template with a .cpp; the rest are headers

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

Notes:
- **JUCE version.** New project: the latest 9.0.x tag (9.0.4, 12 October 2026). Existing
  project: never change the JUCE version without the user's consent; an upgrade can
  break the build and behaviour. JUCE before 6.1 has no accessibility at all (BLD-02).
  From 8 to 9: `ThreadPool::addJob` takes only functions returning `void` or
  `ThreadPoolJob::JobStatus` (a lambda with `return true;` no longer compiles), and
  `Drawable` is no longer a `Component` (wrap it in `DrawableComponent`). The 9.x change
  lists mention no changes to the accessibility, keyboard or focus APIs; the rest is in
  JUCE's BREAKING_CHANGES.md. JUCE 8.0.9 made disabled components reachable by
  accessibility navigation by default.
- **UI Automation linking.** ScreenReaderAnnouncer.cpp loads the UIA functions from
  UIAutomationCore.dll at run time and links ole32/oleaut32/user32 through
  `#pragma comment` on MSVC, so no CMake change is needed. MinGW is untested; it would
  need `if(WIN32) target_link_libraries(MyPlugin PRIVATE ole32 oleaut32 uuid) endif()`. Calling
  `UiaRaiseNotificationEvent` directly instead needs `Uiautomationcore` linked and makes
  the plugin fail to load on Windows older than 10 1709 (BLD-03).
- **Help in the binary:** `juce_add_binary_data(MyPluginHelp SOURCES Help/Help.html)`, link
  `MyPluginHelp`, and include `<BinaryData.h>` in the file that uses it (without the include:
  errors C2653/C3861, TEST76).
- Never set `COPY_PLUGIN_AFTER_BUILD TRUE` or install into
  `C:\Program Files\Common Files\VST3` without the user's permission.

## 2. Projucer project (.jucer)

- "Plugin Editor Requires Keyboard Focus" must be ticked (stored as
  `pluginEditorRequiresKeys` in `pluginCharacteristicsValue`): BLD-01.
- Add the template files to the Source group; ScreenReaderAnnouncer.cpp must be compiled.
- Projucer cannot build CLAP; for CLAP the project has to move to CMake (plan that move
  with the user, it is a bigger change).
- The audit script reads .jucer files (keyboard-focus flag, Projucer version).

## 3. CLAP

JUCE 9.0.4 still has no built-in CLAP format (`FORMATS` accepts Standalone, Unity, VST3,
AU, AUv3, AAX, VST, LV2), although CLAP was announced for JUCE 9. The usual route is
free-audio/clap-juce-extensions. Its README lists JUCE 6, 7 and 8; before using it with
JUCE 9 check the repository's recent commits and issues, pin a known-good commit, and
build in CI. If your JUCE version gains `FORMATS CLAP`, prefer it and check that parameter
IDs stay compatible.

```cmake
# After JUCE. As a submodule:
#   git submodule add -b main https://github.com/free-audio/clap-juce-extensions.git libs/clap-juce-extensions
#   git submodule update --init --recursive    # pulls clap and clap-helpers
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

clap_juce_extensions_plugin(TARGET MyPlugin
    CLAP_ID "com.mycompany.myplugin"          # reverse domain, forever
    CLAP_FEATURES audio-effect equalizer)     # or instrument sampler, etc.
```

- It creates `MyPlugin_CLAP`; editor and processor are the same as for VST3, so all
  accessibility work applies unchanged.
- `CLAP_USE_JUCE_PARAMETER_RANGES` (`OFF` by default: the host sees 0..1; `DISCRETE`;
  `ALL`): choose once before the first release and never change it; it is part of the
  parameter contract. Value text always comes from the parameter's `getText`.
- CLAP parameter IDs use the same JUCE hash as VST3: one collision check covers both.
- Existing sources can be passed by variables (ToneTrace: `JUCE_DIR`,
  `CLAP_JUCE_EXTENSIONS_DIR`) to avoid downloads on every build.
- In REAPER the CLAP and VST3 builds are different FX with separate parameter lists: test
  both (BLD-04).

## 4. GitHub Actions (Windows)

LSampler-24 approach: no local builds; build and tests run in Actions on windows-latest;
the result is a ZIP with the plugin and a text file with version and commit.

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
        run: python tools/a11y_audit.py Source CMakeLists.txt
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

Copy scripts/a11y_audit.py into the plugin repository (tools/) for the audit step, or drop
the step. The CLAP artefact path may differ: check the build log. Use `--param-ids` with
the project's parameter manifest when IDs are built at run time.

## 5. Local build on Windows

Visual Studio Build Tools with "Desktop development with C++" (it includes the Windows
SDK), CMake 3.22+:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Install tools only with the user's permission.

## 6. After the build

- The plugin opens in REAPER (VST3 and CLAP); NVDA names the first element.
- scripts/uia_inspect.ps1 -Mode Tree on the open plugin window: no unnamed focusable
  elements, values with units.
- REAPER / OSARA parameter list: names and value texts.
- The manual plan in testing.md.
