/*
    SignalScope — PluginProcessor.cpp

    Implementation of the audio processor. Key concept:

    The DAW's audio engine runs on a dedicated "audio thread" that has strict
    real-time requirements. processBlock() is called on this thread, so we
    must NEVER do anything slow inside it — no memory allocation, no file I/O,
    no locking mutexes. Violating this causes audio glitches (clicks, dropouts).

    Phase 2 adds a circular buffer: processBlock() copies each incoming sample
    into the buffer, and the UI thread reads the buffer to draw the waveform.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

SignalScopeAudioProcessor::SignalScopeAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Pre-allocate the circular buffers NOW, in the constructor (message thread).
    // We must never allocate memory on the audio thread — that would cause
    // the OS memory allocator to lock, stalling audio and causing glitches.
    // By allocating here and never resizing, processBlock() just writes into
    // existing memory, which is safe.
    circularBufferL.resize (kCircularBufferSize, 0.0f);
    circularBufferR.resize (kCircularBufferSize, 0.0f);
}

SignalScopeAudioProcessor::~SignalScopeAudioProcessor()
{
}

const juce::String SignalScopeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SignalScopeAudioProcessor::acceptsMidi() const    { return false; }
bool SignalScopeAudioProcessor::producesMidi() const   { return false; }
bool SignalScopeAudioProcessor::isMidiEffect() const   { return false; }
double SignalScopeAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int SignalScopeAudioProcessor::getNumPrograms()    { return 1; }
int SignalScopeAudioProcessor::getCurrentProgram() { return 0; }
void SignalScopeAudioProcessor::setCurrentProgram (int) {}
const juce::String SignalScopeAudioProcessor::getProgramName (int) { return {}; }
void SignalScopeAudioProcessor::changeProgramName (int, const juce::String&) {}

juce::AudioProcessorEditor* SignalScopeAudioProcessor::createEditor()
{
    return new SignalScopeAudioProcessorEditor (*this);
}

bool SignalScopeAudioProcessor::hasEditor() const { return true; }

void SignalScopeAudioProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Reset the circular buffer state when playback starts.
    // This clears any stale data from a previous play session.
    std::fill (circularBufferL.begin(), circularBufferL.end(), 0.0f);
    std::fill (circularBufferR.begin(), circularBufferR.end(), 0.0f);
    writePosition.store (0, std::memory_order_relaxed);
}

void SignalScopeAudioProcessor::releaseResources()
{
    // Nothing to release — the circular buffers are managed by std::vector
    // and will be cleaned up when the processor is destroyed.
}

bool SignalScopeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainInput  = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput)
        return false;

    if (mainInput == juce::AudioChannelSet::mono()
        || mainInput == juce::AudioChannelSet::stereo())
        return true;

    return false;
}

void SignalScopeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& /*midiMessages*/)
{
    /*
        The DAW hands us a buffer of audio samples every few milliseconds.
        A "buffer" is just an array of floats between -1.0 and +1.0, where
        each float represents the air pressure at one instant in time (a "sample").

        For stereo audio at 44100 Hz with a 512-sample buffer:
        - We get called ~86 times per second (44100 / 512)
        - Each call gives us 512 samples per channel
        - That's about 11.6 milliseconds of audio per call

        Our job: copy these samples into the circular buffer so the UI can
        read them, then let the audio pass through unmodified.
    */

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // ── Copy samples into the circular buffer ──────────────────────────
    //
    // We grab the raw float* pointers to the left and right channel data.
    // getReadPointer() gives us a C-style array of floats — one float per
    // sample. We iterate through and write each sample into our circular
    // buffer at the current write position, then advance the position.
    //
    // The modulo operator (%) makes the position wrap around to 0 when it
    // reaches the end of the buffer — that's the "circular" part.

    const int numSamples = buffer.getNumSamples();
    const float* channelL = buffer.getReadPointer (0);

    // Handle mono input: if there's only 1 channel, use it for both L and R
    const float* channelR = (totalNumInputChannels >= 2)
                                ? buffer.getReadPointer (1)
                                : buffer.getReadPointer (0);

    int pos = writePosition.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        circularBufferL[pos] = channelL[i];
        circularBufferR[pos] = channelR[i];
        pos = (pos + 1) % kCircularBufferSize;
    }

    // Publish the new write position so the UI thread can see it.
    // memory_order_relaxed is fine — we don't need strict ordering guarantees
    // because a frame of visual lag (16ms) is completely invisible.
    writePosition.store (pos, std::memory_order_relaxed);
}

void SignalScopeAudioProcessor::getDisplaySamples (std::vector<float>& destL,
                                                    std::vector<float>& destR,
                                                    int numSamples) const
{
    /*
        Called by the UI thread to grab the most recent N samples in
        chronological order (oldest → newest, left to right on screen).

        The trick: the write position tells us where the NEXT sample will go,
        so the most recent sample is at (writePos - 1), and the oldest sample
        we want is at (writePos - numSamples). We use modulo arithmetic to
        handle the wrap-around.

        Example with buffer size 8, writePos = 3, requesting 5 samples:
          Buffer: [5] [6] [7] [ ] [ ] [ ] [3] [4]
                        ^writePos
          We want indices: 6, 7, 0, 1, 2 → samples [3][4][5][6][7]
    */

    destL.resize (numSamples);
    destR.resize (numSamples);

    const int pos = writePosition.load (std::memory_order_relaxed);

    // Calculate where the oldest requested sample lives in the circular buffer
    int readPos = (pos - numSamples + kCircularBufferSize) % kCircularBufferSize;

    for (int i = 0; i < numSamples; ++i)
    {
        destL[i] = circularBufferL[readPos];
        destR[i] = circularBufferR[readPos];
        readPos = (readPos + 1) % kCircularBufferSize;
    }
}

void SignalScopeAudioProcessor::getStateInformation (juce::MemoryBlock& /*destData*/)
{
    // Phase 3+: save oscilloscope settings (time scale, trigger level, etc.)
}

void SignalScopeAudioProcessor::setStateInformation (const void* /*data*/, int /*sizeInBytes*/)
{
    // Phase 3+: restore oscilloscope settings
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SignalScopeAudioProcessor();
}
