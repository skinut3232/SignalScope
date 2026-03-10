/*
    SignalScope — PluginEditor.h

    The editor is the plugin's GUI window. JUCE separates audio processing
    (Processor) from visual display (Editor) because they run on different
    threads:
    - The Processor runs on the audio thread (~86 calls/sec at 44.1kHz/512)
    - The Editor runs on the message thread (UI thread, ~60fps)

    Phase 2 adds:
    - A juce::Timer that fires ~60 times per second, calling repaint()
    - paint() reads the latest audio samples and draws them as a polyline
    - The waveform shows amplitude (Y axis) over time (X axis)
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
    // Called by the Timer at ~60fps. Triggers a repaint of the waveform.
    void timerCallback() override;

    SignalScopeAudioProcessor& processorRef;

    // Local copy of audio samples for the UI to draw.
    // We copy from the processor's circular buffer each frame so we have
    // a stable snapshot that won't change mid-draw.
    std::vector<float> displaySamplesL;
    std::vector<float> displaySamplesR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalScopeAudioProcessorEditor)
};
