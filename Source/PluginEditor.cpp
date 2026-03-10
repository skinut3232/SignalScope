/*
    SignalScope — PluginEditor.cpp

    Phase 2: draws a real-time waveform from the audio circular buffer.

    How the drawing works:
    1. Timer fires at ~60fps → calls repaint()
    2. repaint() schedules a paint() call on the message thread
    3. paint() grabs the latest samples from the processor's circular buffer
    4. Each sample maps to one horizontal pixel position (X = time)
    5. Each sample's value (-1.0 to +1.0) maps to a vertical position (Y = amplitude)
    6. We connect all the points with a polyline (juce::Path)

    The result: a scrolling waveform that shows what the audio looks like in
    real time — the core of any oscilloscope.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

SignalScopeAudioProcessorEditor::SignalScopeAudioProcessorEditor (SignalScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (700, 400);

    // Start the repaint timer at ~60fps (16ms interval).
    // juce::Timer runs on the message thread, so this is safe for UI work.
    startTimerHz (60);
}

SignalScopeAudioProcessorEditor::~SignalScopeAudioProcessorEditor()
{
    // Stop the timer before destruction to avoid callbacks on a dead object.
    stopTimer();
}

void SignalScopeAudioProcessorEditor::timerCallback()
{
    // This fires ~60 times per second. All it does is tell JUCE "hey, the
    // display needs updating." JUCE will then call paint() at the right time.
    repaint();
}

void SignalScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    // ── Background ─────────────────────────────────────────────────────
    // Deep near-black, not pure black — the foundation for the CRT look.
    g.fillAll (juce::Colour (12, 12, 15));

    // ── Define the display area ────────────────────────────────────────
    // Leave a small margin around the edges for future UI chrome.
    const auto bounds = getLocalBounds().toFloat().reduced (10.0f);
    const float width  = bounds.getWidth();
    const float height = bounds.getHeight();
    const float centreY = bounds.getCentreY();

    // ── Grab audio samples ─────────────────────────────────────────────
    // Request one sample per horizontal pixel — this maps each pixel column
    // to one audio sample, giving us a 1:1 time-to-pixel relationship.
    // At 700px wide, that's 700 samples ≈ 15.9ms of audio at 44.1kHz.
    const int numSamples = static_cast<int> (width);

    if (numSamples <= 0)
        return;

    processorRef.getDisplaySamples (displaySamplesL, displaySamplesR, numSamples);

    // ── Draw the waveform as a polyline ────────────────────────────────
    // A Path is JUCE's way of describing a shape made of lines and curves.
    // We move to the first sample's position, then draw a line to each
    // subsequent sample. The result is a connected waveform trace.
    //
    // Y mapping: sample value of +1.0 → top of display area
    //            sample value of  0.0 → vertical centre (centreY)
    //            sample value of -1.0 → bottom of display area
    //
    // We multiply by 0.5 * height because the range -1.0 to +1.0 spans
    // the full height, so each unit of amplitude = half the height.

    juce::Path waveformPath;

    // For now, draw the left channel. Phase 3 adds channel selection.
    for (int i = 0; i < numSamples; ++i)
    {
        // Map sample index to X position (left to right across the display)
        const float x = bounds.getX() + (static_cast<float> (i) / static_cast<float> (numSamples - 1)) * width;

        // Map sample value to Y position (note: Y is inverted in screen coords —
        // 0 is the top of the screen, so we subtract to make positive values go up)
        const float y = centreY - displaySamplesL[i] * (height * 0.5f);

        if (i == 0)
            waveformPath.startNewSubPath (x, y);
        else
            waveformPath.lineTo (x, y);
    }

    // Draw the path as a green line — the classic oscilloscope colour.
    // 2.0f pixel width gives it some presence without being chunky.
    g.setColour (juce::Colour (0, 255, 100));
    g.strokePath (waveformPath, juce::PathStrokeType (2.0f));

    // ── Centre line ────────────────────────────────────────────────────
    // A subtle horizontal line at 0.0 amplitude — helps read the signal.
    g.setColour (juce::Colour (40, 40, 45));
    g.drawHorizontalLine (static_cast<int> (centreY), bounds.getX(), bounds.getRight());

    // ── Plugin label ───────────────────────────────────────────────────
    g.setColour (juce::Colour (60, 60, 65));
    g.setFont (14.0f);
    g.drawText ("SignalScope", getLocalBounds().reduced (10),
                juce::Justification::bottomRight, false);
}

void SignalScopeAudioProcessorEditor::resized()
{
    // Phase 3+: layout oscilloscope display and controls here.
}
