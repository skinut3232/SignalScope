/*
    SignalScope — PluginEditor.h

    Phase 4: CRT/phosphor visual treatment.
    - Trace persistence via accumulation buffer (off-screen image)
    - Phosphor glow via multi-pass rendering (thick dim + thin bright)
    - Color themes: green phosphor, amber, blue
    - Scanline overlay for CRT texture
    - Persistence amount slider
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

    // Draws the grid lines and labels onto the given Graphics context.
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> bounds, float timeMs);

    // Draws scanline overlay for CRT effect.
    void drawScanlines (juce::Graphics& g, juce::Rectangle<float> bounds);

    // Returns the trace colour for the current theme (bright / dim / glow).
    juce::Colour getTraceColour (float brightness = 1.0f) const;

    SignalScopeAudioProcessor& processorRef;

    // Mixed display samples (single channel after L/R/Mid/Side/Sum mixing)
    std::vector<float> displaySamples;

    // ── Accumulation Buffer (Phase 4) ──────────────────────────────────
    // An off-screen image that persists between frames. Each frame we:
    // 1. Fade the image slightly (darkening old traces)
    // 2. Draw the new waveform on top (bright)
    // 3. Blit the image to the screen
    // This creates the phosphor persistence / afterglow effect.
    juce::Image traceImage;

    // ── Controls ───────────────────────────────────────────────────────
    juce::Slider   timeScaleSlider;
    juce::Label    timeScaleLabel;

    juce::ComboBox channelSelect;
    juce::Label    channelLabel;

    juce::Slider   persistenceSlider;
    juce::Label    persistenceLabel;

    juce::ComboBox colorSelect;
    juce::Label    colorLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalScopeAudioProcessorEditor)
};
