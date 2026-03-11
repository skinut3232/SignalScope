/*
    SignalScope — PluginEditor.cpp

    Phase 4: CRT/phosphor visual treatment.

    The key technique is the ACCUMULATION BUFFER (traceImage):
    - It's an off-screen JUCE Image that persists between paint() calls
    - Each frame, we fade it slightly by drawing a semi-transparent dark
      rectangle over it (this dims the old traces)
    - Then we draw the new waveform on top (bright)
    - The result: old trace positions dim gradually over multiple frames,
      creating the phosphor persistence / afterglow effect

    The glow effect is achieved by drawing the waveform twice:
    1. First pass: thick, dim line (simulates the bloom/spread of light)
    2. Second pass: thin, bright line (the sharp core of the trace)
    This layered approach approximates a Gaussian glow without needing
    actual blur shaders.
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

// ── Color theme definitions ────────────────────────────────────────────
// Each theme has a bright trace colour and we derive dimmer versions
// for the glow and persistence trails.
struct ThemeColours
{
    juce::Colour bright;    // Core trace line
    juce::Colour mid;       // Glow layer
    juce::Colour dim;       // Faint outer glow
};

static const ThemeColours kThemes[] =
{
    // Green phosphor — classic oscilloscope look
    { juce::Colour (120, 255, 170), juce::Colour (0, 200, 80), juce::Colour (0, 100, 40) },
    // Amber — warm vintage feel
    { juce::Colour (255, 210, 100), juce::Colour (220, 150, 30), juce::Colour (130, 80, 10) },
    // Blue — cool modern aesthetic
    { juce::Colour (140, 200, 255), juce::Colour (50, 130, 230), juce::Colour (20, 60, 140) },
};

SignalScopeAudioProcessorEditor::SignalScopeAudioProcessorEditor (SignalScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (700, 400);

    // ── Time Scale Slider ──────────────────────────────────────────────
    timeScaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    timeScaleSlider.setRange (1.0, 100.0, 0.1);
    timeScaleSlider.setValue (processorRef.timeScaleMs.load(), juce::dontSendNotification);
    timeScaleSlider.setSkewFactorFromMidPoint (15.0);
    timeScaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 55, 20);
    timeScaleSlider.setTextValueSuffix (" ms");

    timeScaleSlider.onValueChange = [this]()
    {
        processorRef.timeScaleMs.store (
            static_cast<float> (timeScaleSlider.getValue()),
            std::memory_order_relaxed);
    };

    timeScaleSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (150, 150, 155));
    timeScaleSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (40, 40, 45));
    timeScaleSlider.setColour (juce::Slider::trackColourId, juce::Colour (60, 60, 65));
    timeScaleSlider.setColour (juce::Slider::thumbColourId, juce::Colour (120, 120, 125));

    addAndMakeVisible (timeScaleSlider);

    timeScaleLabel.setText ("Time", juce::dontSendNotification);
    timeScaleLabel.setColour (juce::Label::textColourId, juce::Colour (90, 90, 95));
    timeScaleLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (timeScaleLabel);

    // ── Channel Select ─────────────────────────────────────────────────
    channelSelect.addItem ("L",    1);
    channelSelect.addItem ("R",    2);
    channelSelect.addItem ("Mid",  3);
    channelSelect.addItem ("Side", 4);
    channelSelect.addItem ("Sum",  5);
    channelSelect.setSelectedId (static_cast<int> (processorRef.channelMode.load()) + 1,
                                  juce::dontSendNotification);

    channelSelect.onChange = [this]()
    {
        const int selected = channelSelect.getSelectedId() - 1;
        processorRef.channelMode.store (
            static_cast<ChannelMode> (selected),
            std::memory_order_relaxed);
    };

    channelSelect.setColour (juce::ComboBox::backgroundColourId, juce::Colour (25, 25, 30));
    channelSelect.setColour (juce::ComboBox::textColourId, juce::Colour (150, 150, 155));
    channelSelect.setColour (juce::ComboBox::outlineColourId, juce::Colour (40, 40, 45));
    channelSelect.setColour (juce::ComboBox::arrowColourId, juce::Colour (90, 90, 95));

    addAndMakeVisible (channelSelect);

    channelLabel.setText ("Ch", juce::dontSendNotification);
    channelLabel.setColour (juce::Label::textColourId, juce::Colour (90, 90, 95));
    channelLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (channelLabel);

    // ── Persistence Slider ─────────────────────────────────────────────
    persistenceSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    persistenceSlider.setRange (0.0, 0.95, 0.01);
    persistenceSlider.setValue (processorRef.persistence.load(), juce::dontSendNotification);
    persistenceSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);

    persistenceSlider.onValueChange = [this]()
    {
        processorRef.persistence.store (
            static_cast<float> (persistenceSlider.getValue()),
            std::memory_order_relaxed);
    };

    persistenceSlider.setColour (juce::Slider::trackColourId, juce::Colour (60, 60, 65));
    persistenceSlider.setColour (juce::Slider::thumbColourId, juce::Colour (120, 120, 125));

    addAndMakeVisible (persistenceSlider);

    persistenceLabel.setText ("Persist", juce::dontSendNotification);
    persistenceLabel.setColour (juce::Label::textColourId, juce::Colour (90, 90, 95));
    persistenceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (persistenceLabel);

    // ── Color Theme Select ─────────────────────────────────────────────
    colorSelect.addItem ("Green",  1);
    colorSelect.addItem ("Amber",  2);
    colorSelect.addItem ("Blue",   3);
    colorSelect.setSelectedId (processorRef.colorTheme.load() + 1,
                               juce::dontSendNotification);

    colorSelect.onChange = [this]()
    {
        processorRef.colorTheme.store (
            colorSelect.getSelectedId() - 1,
            std::memory_order_relaxed);
        // Clear the trace image when changing colour so old traces
        // don't linger in the previous colour.
        traceImage = juce::Image();
    };

    colorSelect.setColour (juce::ComboBox::backgroundColourId, juce::Colour (25, 25, 30));
    colorSelect.setColour (juce::ComboBox::textColourId, juce::Colour (150, 150, 155));
    colorSelect.setColour (juce::ComboBox::outlineColourId, juce::Colour (40, 40, 45));
    colorSelect.setColour (juce::ComboBox::arrowColourId, juce::Colour (90, 90, 95));

    addAndMakeVisible (colorSelect);

    colorLabel.setText ("Color", juce::dontSendNotification);
    colorLabel.setColour (juce::Label::textColourId, juce::Colour (90, 90, 95));
    colorLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (colorLabel);

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

juce::Colour SignalScopeAudioProcessorEditor::getTraceColour (float brightness) const
{
    const int theme = juce::jlimit (0, 2,
        processorRef.colorTheme.load (std::memory_order_relaxed));

    if (brightness >= 0.9f)
        return kThemes[theme].bright;
    else if (brightness >= 0.5f)
        return kThemes[theme].mid;
    else
        return kThemes[theme].dim;
}

void SignalScopeAudioProcessorEditor::drawGrid (juce::Graphics& g,
                                                  juce::Rectangle<float> bounds,
                                                  float timeMs)
{
    const float displayWidth  = bounds.getWidth();
    const float displayHeight = bounds.getHeight();
    const float centreY = bounds.getCentreY();

    const juce::Colour majorGridColour (30, 30, 35);
    const juce::Colour minorGridColour (22, 22, 25);
    const juce::Colour labelColour (55, 55, 60);

    g.setFont (11.0f);

    // Horizontal grid lines (amplitude)
    const float amplitudes[] = { -1.0f, -0.75f, -0.5f, -0.25f, 0.0f,
                                  0.25f, 0.5f, 0.75f, 1.0f };

    for (float amp : amplitudes)
    {
        const float y = centreY - amp * (displayHeight * 0.5f);
        const bool isMajor = (amp == 0.0f || amp == 0.5f || amp == -0.5f
                              || amp == 1.0f || amp == -1.0f);

        g.setColour (isMajor ? majorGridColour : minorGridColour);
        g.drawHorizontalLine (static_cast<int> (y), bounds.getX(), bounds.getRight());

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
                        static_cast<int> (bounds.getX() + 2),
                        static_cast<int> (y - 7),
                        30, 14,
                        juce::Justification::centredLeft, false);
        }
    }

    // Vertical grid lines (time)
    const float roughStep = timeMs / 6.0f;
    const float step = niceStep (roughStep);

    for (float t = step; t < timeMs; t += step)
    {
        const float x = bounds.getX() + (t / timeMs) * displayWidth;

        g.setColour (majorGridColour);
        g.drawVerticalLine (static_cast<int> (x), bounds.getY(), bounds.getBottom());

        g.setColour (labelColour);

        juce::String label;
        if (step >= 1.0f && std::fmod (t, 1.0f) < 0.01f)
            label = juce::String (static_cast<int> (t)) + "ms";
        else
            label = juce::String (t, 1) + "ms";

        g.drawText (label,
                    static_cast<int> (x - 20),
                    static_cast<int> (bounds.getBottom() - 14),
                    40, 14,
                    juce::Justification::centred, false);
    }
}

void SignalScopeAudioProcessorEditor::drawScanlines (juce::Graphics& g,
                                                      juce::Rectangle<float> bounds)
{
    // Draw subtle horizontal lines every 2 pixels to simulate CRT scanlines.
    // Very low opacity so they're felt more than seen.
    g.setColour (juce::Colour::fromRGBA (0, 0, 0, 18));

    const int top = static_cast<int> (bounds.getY());
    const int bottom = static_cast<int> (bounds.getBottom());

    for (int y = top; y < bottom; y += 2)
        g.drawHorizontalLine (y, bounds.getX(), bounds.getRight());
}

void SignalScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    // ── Background ─────────────────────────────────────────────────────
    g.fillAll (juce::Colour (8, 8, 10));

    // ── Define the display area ────────────────────────────────────────
    const int controlStripHeight = 35;
    const auto displayBounds = getLocalBounds()
                                   .withTrimmedBottom (controlStripHeight)
                                   .toFloat()
                                   .reduced (10.0f);
    const float displayWidth  = displayBounds.getWidth();
    const float displayHeight = displayBounds.getHeight();

    if (displayWidth <= 0 || displayHeight <= 0)
        return;

    const float timeMs = processorRef.timeScaleMs.load (std::memory_order_relaxed);
    const double sampleRate = processorRef.currentSampleRate.load (std::memory_order_relaxed);
    const float persist = processorRef.persistence.load (std::memory_order_relaxed);

    int numSamples = static_cast<int> (timeMs * sampleRate / 1000.0);
    numSamples = juce::jlimit (2, SignalScopeAudioProcessor::kCircularBufferSize / 2, numSamples);

    // ── Grid (drawn directly to screen, behind everything) ─────────────
    drawGrid (g, displayBounds, timeMs);

    // ── Accumulation buffer for trace persistence ──────────────────────
    //
    // The trace image is an ARGB off-screen image the same size as the
    // display area. We keep it between paint() calls — that's what
    // creates the persistence.
    //
    // Each frame:
    //   1. Fade the image by drawing a semi-transparent dark rect over it.
    //      The alpha of this rect controls how fast old traces dim.
    //      High persistence → low alpha → slow fade.
    //   2. Draw the new waveform onto the image (bright).
    //   3. Blit the image onto the screen.

    const int imgW = static_cast<int> (displayWidth);
    const int imgH = static_cast<int> (displayHeight);

    // Recreate the image if the size changed (e.g., window resize)
    if (traceImage.isNull() || traceImage.getWidth() != imgW || traceImage.getHeight() != imgH)
        traceImage = juce::Image (juce::Image::ARGB, imgW, imgH, true);

    {
        juce::Graphics ig (traceImage);

        // Step 1: Fade old traces. The fade alpha controls persistence speed.
        // persist=0.0 → fadeAlpha=255 → instant clear (no persistence)
        // persist=0.95 → fadeAlpha=~13 → very slow fade (long trails)
        const int fadeAlpha = static_cast<int> ((1.0f - persist) * 255.0f);
        ig.setColour (juce::Colour::fromRGBA (8, 8, 10, static_cast<juce::uint8> (fadeAlpha)));
        ig.fillAll();

        // Step 2: Draw the new waveform onto the accumulation buffer
        processorRef.getDisplayMixed (displaySamples, numSamples);

        juce::Path waveformPath;
        const int pixelCount = imgW;

        for (int px = 0; px < pixelCount; ++px)
        {
            const float samplePos = (static_cast<float> (px) / static_cast<float> (pixelCount - 1))
                                    * static_cast<float> (numSamples - 1);

            const int   idx0 = static_cast<int> (samplePos);
            const int   idx1 = juce::jmin (idx0 + 1, numSamples - 1);
            const float frac = samplePos - static_cast<float> (idx0);

            const float sampleValue = displaySamples[idx0] + frac * (displaySamples[idx1] - displaySamples[idx0]);

            const float x = static_cast<float> (px);
            const float y = (static_cast<float> (imgH) * 0.5f) - sampleValue * (static_cast<float> (imgH) * 0.5f);

            if (px == 0)
                waveformPath.startNewSubPath (x, y);
            else
                waveformPath.lineTo (x, y);
        }

        // Glow pass: thick, dim line (simulates light bloom/spread)
        ig.setColour (getTraceColour (0.3f).withAlpha (0.3f));
        ig.strokePath (waveformPath, juce::PathStrokeType (6.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Mid glow: medium thickness
        ig.setColour (getTraceColour (0.6f).withAlpha (0.5f));
        ig.strokePath (waveformPath, juce::PathStrokeType (3.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Core trace: thin, bright line (sharp center of the beam)
        ig.setColour (getTraceColour (1.0f));
        ig.strokePath (waveformPath, juce::PathStrokeType (1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Step 3: Blit the accumulation buffer onto the screen
    g.drawImageAt (traceImage,
                   static_cast<int> (displayBounds.getX()),
                   static_cast<int> (displayBounds.getY()));

    // ── Scanlines ──────────────────────────────────────────────────────
    drawScanlines (g, displayBounds);

    // ── Subtle vignette (darkened edges) ───────────────────────────────
    // Draws a gradient that darkens the corners/edges of the display,
    // simulating the curved glass of a CRT tube.
    {
        const auto b = displayBounds.toNearestInt();
        juce::ColourGradient vignette (juce::Colours::transparentBlack, displayBounds.getCentre(),
                                        juce::Colour::fromRGBA (0, 0, 0, 60),
                                        { displayBounds.getX(), displayBounds.getY() },
                                        true);  // radial gradient
        g.setGradientFill (vignette);
        g.fillRect (b);
    }

    // ── Plugin label ───────────────────────────────────────────────────
    g.setColour (juce::Colour (35, 35, 40));
    g.setFont (13.0f);
    g.drawText ("SignalScope", displayBounds.toNearestInt().reduced (6),
                juce::Justification::topRight, false);
}

void SignalScopeAudioProcessorEditor::resized()
{
    const int controlStripHeight = 35;
    auto controlArea = getLocalBounds()
                           .removeFromBottom (controlStripHeight)
                           .reduced (10, 5);

    // Channel select
    auto chLabelArea = controlArea.removeFromLeft (22);
    channelLabel.setBounds (chLabelArea);
    auto chSelectArea = controlArea.removeFromLeft (60);
    channelSelect.setBounds (chSelectArea);

    controlArea.removeFromLeft (10);

    // Color theme select
    auto colLabelArea = controlArea.removeFromLeft (35);
    colorLabel.setBounds (colLabelArea);
    auto colSelectArea = controlArea.removeFromLeft (65);
    colorSelect.setBounds (colSelectArea);

    controlArea.removeFromLeft (10);

    // Persistence slider
    auto persLabelArea = controlArea.removeFromLeft (42);
    persistenceLabel.setBounds (persLabelArea);
    auto persSliderArea = controlArea.removeFromLeft (80);
    persistenceSlider.setBounds (persSliderArea);

    controlArea.removeFromLeft (10);

    // Time slider fills the rest
    auto timeLabelArea = controlArea.removeFromLeft (32);
    timeScaleLabel.setBounds (timeLabelArea);
    timeScaleSlider.setBounds (controlArea);
}
