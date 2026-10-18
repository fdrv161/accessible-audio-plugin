# accessible-audio-plugin

A skill for Claude (Claude Code or claude.ai) that helps build, fix and audit JUCE audio plugins (VST3, CLAP, AU) so blind musicians can use them with NVDA, JAWS, Narrator, VoiceOver and REAPER with OSARA.

It grew out of real work with a blind REAPER user: focus that jumps away, silent controls, keys that trigger the DAW, a Windows voice speaking instead of NVDA, and so on.

## What is inside

- `SKILL.md`: the main instructions and ground rules.
- `references/`: what usually goes wrong and how to fix it. Start with `pitfalls.md` (symptom to cause) and `audit-checklist.md` (all defects with fixes).
- `assets/templates/`: drop-in JUCE helpers: screen reader announcements that NVDA actually hears, an accessible parameter list, overlays for custom-drawn knobs, modal screens, dialogs.
- `scripts/a11y_audit.py`: static check of plugin sources for common accessibility mistakes.
- `scripts/uia_inspect.ps1`: shows what a screen reader sees in a running plugin window (Windows, nothing to install).

## How to use

With Claude Code: copy this folder to `~/.claude/skills/accessible-audio-plugin/` and ask, for example, "NVDA is silent in my plugin, check the code".

Without Claude: read `references/pitfalls.md`, then run the audit on your sources:

```
python scripts/a11y_audit.py Source CMakeLists.txt
```

## Status

The templates follow the JUCE 8/9 API but were not compiled in this repository. Expect small fixes for your JUCE version. Always check the result with a real screen reader.

## License

MIT, see `LICENSE`. Use the templates in free or commercial plugins.

---

Навык для Claude, который помогает делать плагины на JUCE доступными для незрячих: NVDA, JAWS, Экранный диктор, REAPER с OSARA. Лицензия MIT, шаблоны можно брать в любые проекты.
