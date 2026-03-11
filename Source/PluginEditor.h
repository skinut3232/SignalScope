/*
    SignalScope — PluginEditor.h

    Phase 3 complete:
    - Time scale slider (zoom in/out on time axis)
    - Grid with amplitude and time labels
    - Channel select combo box (L / R / Mid / Side / Sum)
    - Trigger detection for stable waveform display
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

    // Mixed display samples (single channel after L/R/Mid/Side/Sum mixing)
    std::vector<float> displaySamples;

    // ── Controls ───────────────────────────────────────────────────────
    juce::Slider   timeScaleSlider;
    juce::Label    timeScaleLabel;

    juce::ComboBox channelSelect;
    juce::Label    channelLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalScopeAudioProcessorEditor)
};
