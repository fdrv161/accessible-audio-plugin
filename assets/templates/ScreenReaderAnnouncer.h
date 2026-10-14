#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>

// Spoken messages for NVDA, Narrator, JAWS (Windows) and VoiceOver (macOS).
// Rules for WHAT to say and WHEN: references/announcements-and-focus.md.
//
// Windows: the message is raised as a UI Automation notification from a hidden 1x1
// child window of the plugin editor that exists only for this purpose (the technique
// OSARA and Surge XT use). It does not depend on which component has focus, does not
// touch any title or description, and screen readers never land on that window.
// If JAWS or Dolphin is loaded (they ignore UIA notifications, per OSARA), the text is
// put into the MSAA name of the focused window for a moment instead (OSARA's fallback;
// not yet verified inside JUCE plugins, test with JAWS).
// juce::AccessibilityHandler::postAnnouncement is NOT used on Windows: JUCE speaks it
// with a separate SAPI voice, and NVDA/JAWS never receive it (defect SPK-01).
//
// macOS: juce::AccessibilityHandler::postAnnouncement (VoiceOver handles it).
namespace a11y
{
enum class Speech
{
    interrupt, // latest wins: navigation, values, quick feedback
    queue      // appended after current speech: results, errors (must not be lost)
};

// Speak `text` now. `componentInEditor` is any component that is on screen inside the
// plugin editor (used only to find the editor's native window). Message thread only.
// Returns false if nothing was sent (no screen reader running, editor not on screen).
bool announce (juce::Component& componentInEditor, const juce::String& text,
               Speech mode = Speech::interrupt);

// Timed announcements. Keep one per editor (member of the editor class).
//  - say():       results of actions ("Bank loaded", errors). Queued and spoken after a
//                 short delay, so that the speech for a focus change caused by the same
//                 action comes first and does not cancel the message (screen readers
//                 cut off speech when focus moves).
//  - sayLatest(): fast repeated feedback (key held down): only the newest text is spoken.
//  - sayIfChanged(): status/telemetry; silent if the text equals the previous one.
class Announcer : private juce::Timer
{
public:
    explicit Announcer (juce::Component& componentInEditor) : owner (&componentInEditor) {}
    ~Announcer() override { stopTimer(); }

    void say (const juce::String& text, int delayMs = 150)
    {
        if (text.isEmpty())
            return;
        pending.push_back ({ text, now() + (juce::uint32) juce::jmax (0, delayMs) });
        startTimer (15);
    }

    void sayLatest (const juce::String& text, int delayMs = 30)
    {
        if (text.isEmpty())
            return;
        latest = text;
        latestDue = now() + (juce::uint32) juce::jmax (0, delayMs);
        startTimer (15);
    }

    void sayIfChanged (const juce::String& text)
    {
        if (text == lastStatus)
            return;
        lastStatus = text;
        sayLatest (text);
    }

    void clear()
    {
        pending.clear();
        latest.clear();
        lastStatus.clear();
        stopTimer();
    }

private:
    struct Item { juce::String text; juce::uint32 due; };

    static juce::uint32 now() { return juce::Time::getMillisecondCounter(); }

    void timerCallback() override
    {
        if (owner == nullptr)
        {
            clear();
            return;
        }

        const auto t = now();
        if (latest.isNotEmpty() && t >= latestDue)
        {
            announce (*owner, latest, Speech::interrupt);
            latest.clear();
        }

        while (! pending.empty() && t >= pending.front().due)
        {
            announce (*owner, pending.front().text, Speech::queue);
            pending.pop_front();
        }

        if (pending.empty() && latest.isEmpty())
            stopTimer();
    }

    juce::Component::SafePointer<juce::Component> owner;
    std::deque<Item> pending;
    juce::String latest, lastStatus;
    juce::uint32 latestDue = 0;

    JUCE_DECLARE_NON_COPYABLE (Announcer)
};
} // namespace a11y
