/*
    SignalScope — PluginEditor.cpp

    Draws a real-time waveform from the audio circular buffer.

    How the drawing works:
    1. Timer fires at ~60fps → calls repaint()
    2. paint() converts timeScaleMs → sample count using the sample rate
    3. Grabs that many samples from the processor's circular buffer
    4. Maps samples to pixels using linear interpolation
    5. Connects all points with a polyline (juce::Path)

    Phase 3 adds grid/axis labels and a time scale slider.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// ── Helper: pick a "nice" step size for grid divisions ─────────────────
//
// Given a rough step size, round it to the nearest "nice" number from
// the sequence: ...0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50...
//
// This is the standard algorithm used by graphing libraries to choose
// axis tick spacing. It ensures grid lines land on round numbers like
// "5 ms" or "20 ms" rather than awkward values like "7.3 ms".
static float niceStep (float roughStep)
{
    if (roughStep <= 0.0f)
        return 1.0f;

    // Find the order of magnitude (power of 10)
    float exponent = std::floor (std::log10 (roughStep));
    float fraction = roughStep / std::pow (10.0f, exponent);

    // Snap to the nearest value in the 1-2-5 sequence
    float niceFraction;
    if (fraction < 1.5f)
        niceFraction = 1.0f;
    else if (fraction < 3.5f)
        niceFraction = 2.0f;
    else if (fraction < 7.5f)
        niceFraction = 5.0f;
    else
        niceFraction = 10.0f;

    return niceFraction * std::pow (10.0f, exponent);
}

SignalScopeAudioProcessorEditor::SignalScopeAudioProcessorEditor (SignalScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (700, 400);

    // ── Time Scale Slider ──────────────────────────────────────────────
    timeScaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    timeScaleSlider.setRange (1.0, 100.0, 0.1);
    timeScaleSlider.setValue (20.0);
    timeScaleSlider.setSkewFactorFromMidPoint (15.0);
    timeScaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    timeScaleSlider.setTextValueSuffix (" ms");

    timeScaleSlider.onValueChange = [this]()
    {
        processorRef.timeScaleMs.store (
            static_cast<float> (timeScaleSlider.getValue()),
            std::memory_order_relaxed);
    };

    timeScaleSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (150, 150, 155));
    timeScaleSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (40, 40, 45));
    timeScaleSlider.setColour (juce::Slider::trackColourId, juce::Colour (0, 180, 70));
    timeScaleSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0, 220, 90));

    addAndMakeVisible (timeScaleSlider);

    timeScaleLabel.setText ("Time", juce::dontSendNotification);
    timeScaleLabel.setColour (juce::Label::textColourId, juce::Colour (120, 120, 125));
    timeScaleLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (timeScaleLabel);

    startTimerHz (60);
}

SignalScopeAudioProcessorEditor::~SignalScopeAudioProcessorEditor()
{
    stopTimer();
}

void SignalScopeAudioProcessorEditor::timerCallback()
{
    repaint();
}

void SignalScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    // ── Background ─────────────────────────────────────────────────────
    g.fillAll (juce::Colour (12, 12, 15));

    // ── Define the display area ────────────────────────────────────────
    const int controlStripHeight = 35;
    const auto displayBounds = getLocalBounds()
                                   .withTrimmedBottom (controlStripHeight)
                                   .toFloat()
                                   .reduced (10.0f);
    const float displayWidth  = displayBounds.getWidth();
    const float displayHeight = displayBounds.getHeight();
    const float centreY = displayBounds.getCentreY();

    if (displayWidth <= 0 || displayHeight <= 0)
        return;

    const float timeMs = processorRef.timeScaleMs.load (std::memory_order_relaxed);
    const double sampleRate = processorRef.currentSampleRate.load (std::memory_order_relaxed);

    int numSamples = static_cast<int> (timeMs * sampleRate / 1000.0);
    numSamples = juce::jlimit (2, SignalScopeAudioProcessor::kCircularBufferSize / 2, numSamples);

    // ── Grid (drawn BEHIND the waveform) ───────────────────────────────
    //
    // The grid provides visual reference for reading the signal.
    // Horizontal lines = amplitude divisions (how loud)
    // Vertical lines = time divisions (when)
    //
    // We draw it before the waveform so the trace sits on top.

    const juce::Colour majorGridColour (35, 35, 40);
    const juce::Colour minorGridColour (25, 25, 28);
    const juce::Colour labelColour (70, 70, 75);
    const float labelFontSize = 11.0f;

    g.setFont (labelFontSize);

    // ── Horizontal grid lines (amplitude) ──────────────────────────────
    // Major lines at 0.0, ±0.5, ±1.0
    // Minor lines at ±0.25, ±0.75
    {
        const float amplitudes[] = { -1.0f, -0.75f, -0.5f, -0.25f, 0.0f,
                                      0.25f, 0.5f, 0.75f, 1.0f };

        for (float amp : amplitudes)
        {
            const float y = centreY - amp * (displayHeight * 0.5f);
            const bool isMajor = (amp == 0.0f || amp == 0.5f || amp == -0.5f
                                  || amp == 1.0f || amp == -1.0f);

            g.setColour (isMajor ? majorGridColour : minorGridColour);
            g.drawHorizontalLine (static_cast<int> (y),
                                  displayBounds.getX(), displayBounds.getRight());

            // Labels on major lines only, on the left edge
            if (isMajor)
            {
                g.setColour (labelColour);

                // Format: show "0" for zero, otherwise show with one decimal
                juce::String label;
                if (amp == 0.0f)
                    label = "0";
                else if (amp == 1.0f || amp == -1.0f)
                    label = juce::String (static_cast<int> (amp));
                else
                    label = juce::String (amp, 1);

                // Position the label just to the right of the left edge
                g.drawText (label,
                            static_cast<int> (displayBounds.getX() + 2),
                            static_cast<int> (y - 7),
                            30, 14,
                            juce::Justification::centredLeft, false);
            }
        }
    }

    // ── Vertical grid lines (time) ─────────────────────────────────────
    // Use the "nice step" algorithm to pick round-number intervals.
    // Aim for roughly 5-8 divisions across the display.
    {
        const float roughStep = timeMs / 6.0f;
        const float step = niceStep (roughStep);

        // Draw lines at each step interval: step, 2*step, 3*step, ...
        for (float t = step; t < timeMs; t += step)
        {
            // Map time position to pixel X
            const float x = displayBounds.getX() + (t / timeMs) * displayWidth;

            g.setColour (majorGridColour);
            g.drawVerticalLine (static_cast<int> (x),
                                displayBounds.getY(), displayBounds.getBottom());

            // Time label at the bottom of the grid
            g.setColour (labelColour);

            // Format: show as integer if whole number, otherwise 1 decimal
            juce::String label;
            if (step >= 1.0f && std::fmod (t, 1.0f) < 0.01f)
                label = juce::String (static_cast<int> (t)) + "ms";
            else
                label = juce::String (t, 1) + "ms";

            g.drawText (label,
                        static_cast<int> (x - 20),
                        static_cast<int> (displayBounds.getBottom() - 14),
                        40, 14,
                        juce::Justification::centred, false);
        }
    }

    // ── Grab audio samples and draw waveform ───────────────────────────
    processorRef.getDisplaySamples (displaySamplesL, displaySamplesR, numSamples);

    juce::Path waveformPath;
    const int pixelCount = static_cast<int> (displayWidth);

    for (int px = 0; px < pixelCount; ++px)
    {
        const float samplePos = (static_cast<float> (px) / static_cast<float> (pixelCount - 1))
                                * static_cast<float> (numSamples - 1);

        const int   idx0 = static_cast<int> (samplePos);
        const int   idx1 = juce::jmin (idx0 + 1, numSamples - 1);
        const float frac = samplePos - static_cast<float> (idx0);

        const float sampleValue = displaySamplesL[idx0] + frac * (displaySamplesL[idx1] - displaySamplesL[idx0]);

        const float x = displayBounds.getX() + static_cast<float> (px);
        const float y = centreY - sampleValue * (displayHeight * 0.5f);

        if (px == 0)
            waveformPath.startNewSubPath (x, y);
        else
            waveformPath.lineTo (x, y);
    }

    g.setColour (juce::Colour (0, 255, 100));
    g.strokePath (waveformPath, juce::PathStrokeType (2.0f));

    // ── Plugin label ───────────────────────────────────────────────────
    g.setColour (juce::Colour (45, 45, 50));
    g.setFont (14.0f);
    g.drawText ("SignalScope", displayBounds.toNearestInt().reduced (4),
                juce::Justification::topRight, false);
}

void SignalScopeAudioProcessorEditor::resized()
{
    const int controlStripHeight = 35;
    auto controlArea = getLocalBounds()
                           .removeFromBottom (controlStripHeight)
                           .reduced (10, 5);

    auto labelArea = controlArea.removeFromLeft (40);
    timeScaleLabel.setBounds (labelArea);
    timeScaleSlider.setBounds (controlArea);
}
