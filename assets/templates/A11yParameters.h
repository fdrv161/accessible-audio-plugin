#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Host-parameter helpers shared by AccessibleParameterList and AccessibleOverlay.
// Rules behind them: references/parameters-automation.md.
namespace a11y
{
enum class StepSize { fine, normal, coarse };

// Value as the user should hear it: the parameter's own text plus its unit label
// ("-3.0 dB", "Round Robin"). The same text the host and OSARA show.
inline juce::String valueText (const juce::RangedAudioParameter& p)
{
    auto text = p.getCurrentValueAsText().trim();
    const auto unit = p.getLabel().trim();
    if (unit.isNotEmpty() && ! text.endsWithIgnoreCase (unit))
        text << " " << unit;
    return text;
}

// Next normalised value for one key press.
// "Few steps" (discrete, or a short list of values such as 0..100 % or semitones):
// step through real values. Otherwise step in the normalised scale, which follows the
// parameter's skew: a 0.01 Hz interval on a 20..20000 Hz cutoff would be inaudible.
inline float steppedValue (const juce::RangedAudioParameter& p, int direction, StepSize size)
{
    const auto& range = p.getNormalisableRange();
    const float now = p.getValue();
    const int numSteps = p.getNumSteps();
    const bool fewSteps = p.isDiscrete() || (numSteps > 1 && numSteps <= 201);

    if (fewSteps)
    {
        const float interval = range.interval > 0.0f ? range.interval : 1.0f;
        const int count = size == StepSize::coarse ? juce::jmax (1, (numSteps - 1) / 10) : 1;
        const float real = range.convertFrom0to1 (now) + (float) (direction * count) * interval;
        return range.convertTo0to1 (range.snapToLegalValue (juce::jlimit (range.start, range.end, real)));
    }

    const float delta = size == StepSize::fine ? 0.001f : (size == StepSize::coarse ? 0.1f : 0.01f);
    const float next = juce::jlimit (0.0f, 1.0f, now + (float) direction * delta);
    if (range.interval > 0.0f) // keep values legal and readable ("1200 Hz", not "1200.0371 Hz")
    {
        const float snapped = range.convertTo0to1 (range.snapToLegalValue (range.convertFrom0to1 (next)));
        if (snapped != now)
            return snapped;
    }
    return next;
}

// A change made by the user: one host gesture per key press, so the DAW records
// automation and knows the "last touched" parameter (defect PAR-03).
// Returns true if the spoken value text changed (edges stay silent).
inline bool setByUser (juce::RangedAudioParameter& p, float normalised)
{
    const auto before = valueText (p);
    p.beginChangeGesture();
    p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    p.endChangeGesture();
    return valueText (p) != before;
}

// getValueForText() silently returns 0 (or the first option) for rubbish text.
// Accept only a number (optionally followed by a unit) or an exact option name.
inline bool parseTypedValue (const juce::RangedAudioParameter& p, const juce::String& text, float& normalised)
{
    const auto t = text.trim();
    if (t.isEmpty())
        return false;
    const auto number = t.upToFirstOccurrenceOf (" ", false, false);
    const bool numeric = number.containsOnly ("+-0123456789.,") && number.containsAnyOf ("0123456789");
    const float candidate = p.getValueForText (t);
    const bool optionName = p.getText (candidate, 128).trim().equalsIgnoreCase (t);
    if (! numeric && ! optionName)
        return false;
    normalised = juce::jlimit (0.0f, 1.0f, candidate);
    return true;
}
} // namespace a11y
