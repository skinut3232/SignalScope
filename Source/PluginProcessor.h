/*
    SignalScope — Real-time oscilloscope VST3 plugin

    PluginProcessor.h

    This is the "audio engine" of the plugin. The DAW (e.g. Ableton) creates an
    instance of this class when you load the plugin. It handles:
    - Receiving audio from the DAW (processBlock)
    - Storing recent samples in a circular buffer for the UI to read
    - Trigger detection to lock the waveform display
    - Reporting what audio formats are supported (stereo, mono, etc.)
    - Saving/loading plugin state

    For SignalScope, this is an analysis-only plugin — we read the audio but
    never modify it. processBlock() passes audio through unchanged.
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

// ── Trigger Mode ───────────────────────────────────────────────────────
// Determines how the oscilloscope finds a consistent "start point" for
// each frame of display.
//   Rising  = start where the signal crosses the threshold going UP
//   Falling = start where the signal crosses the threshold going DOWN
//   None    = no trigger, just show the most recent samples (free-running)
enum class TriggerMode
{
    Rising,
    Falling,
    None
};

// ── Channel Mode ───────────────────────────────────────────────────────
// Which signal to display on the oscilloscope.
//   Left  = left channel only
//   Right = right channel only
//   Mid   = (L + R) / 2  — the mono/center content of the stereo image
//   Side  = (L - R) / 2  — the stereo width content (reverb, panning, etc.)
//   Sum   = same as Mid  — common name for mono summing
enum class ChannelMode
{
    Left,
    Right,
    Mid,
    Side,
    Sum
};

class SignalScopeAudioProcessor : public juce::AudioProcessor
{
public:
    SignalScopeAudioProcessor();
    ~SignalScopeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── Circular Buffer (Phase 2) ──────────────────────────────────────

    static constexpr int kCircularBufferSize = 8192;

    std::vector<float> circularBufferL;
    std::vector<float> circularBufferR;

    std::atomic<int> writePosition { 0 };

    // Copies display samples into dest buffers. If a trigger is active,
    // searches for a trigger point and starts the display from there.
    // Falls back to free-running (most recent samples) if no trigger found.
    void getDisplaySamples (std::vector<float>& destL, std::vector<float>& destR,
                            int numSamples) const;

    // ── Trigger Settings (Phase 3) ─────────────────────────────────────
    //
    // These are atomic so the UI thread can change them while the
    // getDisplaySamples() method reads them — no locks needed.

    // Trigger threshold: the amplitude level the signal must cross.
    // Default 0.0 = zero crossing, which works well for most signals.
    std::atomic<float> triggerLevel { 0.0f };

    // Which edge to trigger on (rising, falling, or none/free-running).
    std::atomic<TriggerMode> triggerMode { TriggerMode::Rising };

    // ── Channel Select (Phase 3) ─────────────────────────────────────

    // Which channel(s) to display. The UI sets this via a combo box.
    std::atomic<ChannelMode> channelMode { ChannelMode::Left };

    // Returns the mixed display samples for the current channel mode.
    // This applies the L/R/Mid/Side/Sum math and writes the result
    // into a single output buffer.
    void getDisplayMixed (std::vector<float>& dest, int numSamples) const;

    // ── Time Scale (Phase 3) ───────────────────────────────────────────

    // How many milliseconds of audio to show on screen.
    // At 20ms default: a 500Hz wave shows one full cycle, which is a good
    // starting view. Range will be ~1ms (zoomed in) to ~100ms (zoomed out).
    std::atomic<float> timeScaleMs { 20.0f };

    // The DAW's sample rate, stored from prepareToPlay(). Needed to convert
    // the time scale (milliseconds) into a sample count.
    // Default 44100 is a safe fallback if prepareToPlay hasn't been called yet.
    std::atomic<double> currentSampleRate { 44100.0 };

    // Returns the sample at bufferIndex mixed according to the channel mode.
    float getSampleForChannel (int bufferIndex, ChannelMode mode) const;

    // ── Visual Settings (Phase 4) ──────────────────────────────────────

    // Persistence: how long the afterglow lasts (0.0 = no persistence,
    // 1.0 = infinite persistence / never fades). Controls the alpha of
    // the fade overlay drawn each frame.
    std::atomic<float> persistence { 0.6f };

    // Color theme: 0 = green phosphor, 1 = amber, 2 = blue
    std::atomic<int> colorTheme { 0 };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalScopeAudioProcessor)
};
