/*
    SignalScope — PluginEditor.cpp

    Draws a real-time waveform from the audio circular buffer.

    Phase 3 complete:
    - Time scale slider with grid/axis labels
    - Channel select combo box (L / R / Mid / Side / Sum)
    - Trigger detection locks waveform to selected channel
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// ── Helper: pick a "nice" step size for grid divisions ─────────────────
static float niceStep (float roughStep)
{
    if (roughStep <= 0.0f)
        return 1.0f;

    float exponent = std::floor (std::log10 (roughStep));
    float fraction = roughStep / std::pow (10.0f, exponent);

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

    // ── Channel Select ─────────────────────────────────────────────────
    // ComboBox item IDs start at 1 (JUCE convention — 0 means "no selection").
    // The order matches the ChannelMode enum.
    channelSelect.addItem ("L",    1);
    channelSelect.addItem ("R",    2);
    channelSelect.addItem ("Mid",  3);
    channelSelect.addItem ("Side", 4);
    channelSelect.addItem ("Sum",  5);
    channelSelect.setSelectedId (1);  // Default: Left channel

    channelSelect.onChange = [this]()
    {
        // Map combo box selection (1-based) to ChannelMode enum (0-based)
        const int selected = channelSelect.getSelectedId() - 1;
        processorRef.channelMode.store (
            static_cast<ChannelMode> (selected),
            std::memory_order_relaxed);
    };

    channelSelect.setColour (juce::ComboBox::backgroundColourId, juce::Colour (25, 25, 30));
    channelSelect.setColour (juce::ComboBox::textColourId, juce::Colour (150, 150, 155));
    channelSelect.setColour (juce::ComboBox::outlineColourId, juce::Colour (40, 40, 45));
    channelSelect.setColour (juce::ComboBox::arrowColourId, juce::Colour (0, 220, 90));

    addAndMakeVisible (channelSelect);

    channelLabel.setText ("Ch", juce::dontSendNotification);
    channelLabel.setColour (juce::Label::textColourId, juce::Colour (120, 120, 125));
    channelLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (channelLabel);

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

    // ── Grid ───────────────────────────────────────────────────────────
    const juce::Colour majorGridColour (35, 35, 40);
    const juce::Colour minorGridColour (25, 25, 28);
    const juce::Colour labelColour (70, 70, 75);

    g.setFont (11.0f);

    // Horizontal grid lines (amplitude)
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

            if (isMajor)
            {
                g.setColour (labelColour);

                juce::String label;
                if (amp == 0.0f)
                    label = "0";
                else if (amp == 1.0f || amp == -1.0f)
                    label = juce::String (static_cast<int> (amp));
                else
                    label = juce::String (amp, 1);

                g.drawText (label,
                            static_cast<int> (displayBounds.getX() + 2),
                            static_cast<int> (y - 7),
                            30, 14,
                            juce::Justification::centredLeft, false);
            }
        }
    }

    // Vertical grid lines (time)
    {
        const float roughStep = timeMs / 6.0f;
        const float step = niceStep (roughStep);

        for (float t = step; t < timeMs; t += step)
        {
            const float x = displayBounds.getX() + (t / timeMs) * displayWidth;

            g.setColour (majorGridColour);
            g.drawVerticalLine (static_cast<int> (x),
                                displayBounds.getY(), displayBounds.getBottom());

            g.setColour (labelColour);

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
    // getDisplayMixed() applies the channel mode (L/R/Mid/Side/Sum) and
    // returns a single mixed buffer, with trigger detection applied to
    // the selected channel.
    processorRef.getDisplayMixed (displaySamples, numSamples);

    juce::Path waveformPath;
    const int pixelCount = static_cast<int> (displayWidth);

    for (int px = 0; px < pixelCount; ++px)
    {
        const float samplePos = (static_cast<float> (px) / static_cast<float> (pixelCount - 1))
                                * static_cast<float> (numSamples - 1);

        const int   idx0 = static_cast<int> (samplePos);
        const int   idx1 = juce::jmin (idx0 + 1, numSamples - 1);
        const float frac = samplePos - static_cast<float> (idx0);

        const float sampleValue = displaySamples[idx0] + frac * (displaySamples[idx1] - displaySamples[idx0]);

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

    // Channel select on the left
    auto chLabelArea = controlArea.removeFromLeft (25);
    channelLabel.setBounds (chLabelArea);
    auto chSelectArea = controlArea.removeFromLeft (70);
    channelSelect.setBounds (chSelectArea);

    // Small gap
    controlArea.removeFromLeft (15);

    // Time slider fills the rest
    auto timeLabelArea = controlArea.removeFromLeft (40);
    timeScaleLabel.setBounds (timeLabelArea);
    timeScaleSlider.setBounds (controlArea);
}
