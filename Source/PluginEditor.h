/*
    SignalScope — PluginEditor.h

    The editor is the plugin's GUI window. JUCE separates audio processing
    (Processor) from visual display (Editor) because they run on different
    threads:
    - The Processor runs on the audio thread (~86 calls/sec at 44.1kHz/512)
    - The Editor runs on the message thread (UI thread, ~60fps)

    Phase 3 adds:
    - Time scale slider to zoom in/out on the time axis
    - Sample-to-pixel mapping with linear interpolation
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>

class SignalScopeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit SignalScopeAudioProcessorEditor (SignalScopeAudioProcessor&);
    ~SignalScopeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SignalScopeAudioProcessor& processorRef;

    // Local copy of audio samples for the UI to draw.
    std::vector<float> displaySamplesL;
    std::vector<float> displaySamplesR;

    // ── Time Scale Control ─────────────────────────────────────────────
    // A horizontal slider at the bottom of the window that controls how
    // many milliseconds of audio are visible. Dragging right = zoom out
    // (more time visible), dragging left = zoom in (less time visible).
    juce::Slider timeScaleSlider;
    juce::Label  timeScaleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalScopeAudioProcessorEditor)
};
