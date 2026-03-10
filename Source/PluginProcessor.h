/*
    SignalScope — Real-time oscilloscope VST3 plugin

    PluginProcessor.h

    This is the "audio engine" of the plugin. The DAW (e.g. Ableton) creates an
    instance of this class when you load the plugin. It handles:
    - Receiving audio from the DAW (processBlock)
    - Storing recent samples in a circular buffer for the UI to read
    - Reporting what audio formats are supported (stereo, mono, etc.)
    - Saving/loading plugin state

    For SignalScope, this is an analysis-only plugin — we read the audio but
    never modify it. processBlock() passes audio through unchanged.
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

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
    //
    // The circular buffer is how audio data gets from the audio thread to
    // the UI thread. The audio thread writes samples here in processBlock(),
    // and the UI thread reads them in paint().
    //
    // We store 2 channels (left + right) in separate buffers so the UI can
    // choose which to display. Buffer size is 8192 samples, which at 44.1kHz
    // gives us ~186ms of audio history — more than enough for any oscilloscope
    // time window we'd want to show.

    static constexpr int kCircularBufferSize = 8192;

    // The buffers themselves. Written by the audio thread, read by the UI thread.
    std::vector<float> circularBufferL;
    std::vector<float> circularBufferR;

    // The write position — where the audio thread will write the next sample.
    // std::atomic makes this safe to read from the UI thread without locks.
    // memory_order_relaxed is fine because we only need the value to be
    // "approximately correct" — a frame of visual lag is invisible.
    std::atomic<int> writePosition { 0 };

    // Copies the most recent `numSamples` from the circular buffer into `dest`,
    // in chronological order (oldest first). The UI thread calls this.
    void getDisplaySamples (std::vector<float>& destL, std::vector<float>& destR,
                            int numSamples) const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalScopeAudioProcessor)
};
