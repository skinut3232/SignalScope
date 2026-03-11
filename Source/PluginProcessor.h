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

    // ── Time Scale (Phase 3) ───────────────────────────────────────────

    // How many milliseconds of audio to show on screen.
    // At 20ms default: a 500Hz wave shows one full cycle, which is a good
    // starting view. Range will be ~1ms (zoomed in) to ~100ms (zoomed out).
    std::atomic<float> timeScaleMs { 20.0f };

    // The DAW's sample rate, stored from prepareToPlay(). Needed to convert
    // the time scale (milliseconds) into a sample count.
    // Default 44100 is a safe fallback if prepareToPlay hasn't been called yet.
    std::atomic<double> currentSampleRate { 44100.0 };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalScopeAudioProcessor)
};
