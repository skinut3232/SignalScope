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

void SignalScopeAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // Store the sample rate so the UI can convert milliseconds → samples.
    // Common values: 44100, 48000, 88200, 96000 Hz.
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);

    // Reset the circular buffer state when playback starts.
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

// ── Helper: get a sample value for a given channel mode ────────────────
//
// This applies the channel mixing math at a single buffer index:
//   Left  = L
//   Right = R
//   Mid   = (L + R) / 2  — the "center" of the stereo image
//   Side  = (L - R) / 2  — the "width" of the stereo image
//   Sum   = (L + R) / 2  — same as Mid (common alias)
//
// Mid/Side encoding is how mastering engineers analyze stereo content.
// Mid contains everything panned center (vocals, kick, bass).
// Side contains everything panned wide (reverb, stereo effects).
float SignalScopeAudioProcessor::getSampleForChannel (int bufferIndex, ChannelMode mode) const
{
    const float l = circularBufferL[bufferIndex];
    const float r = circularBufferR[bufferIndex];

    switch (mode)
    {
        case ChannelMode::Left:   return l;
        case ChannelMode::Right:  return r;
        case ChannelMode::Mid:    return (l + r) * 0.5f;
        case ChannelMode::Side:   return (l - r) * 0.5f;
        case ChannelMode::Sum:    return (l + r) * 0.5f;
        default:                  return l;
    }
}

void SignalScopeAudioProcessor::getDisplaySamples (std::vector<float>& destL,
                                                    std::vector<float>& destR,
                                                    int numSamples) const
{
    /*
        Called by the UI thread to grab samples for display.

        Trigger detection searches backwards through recent audio for a
        threshold crossing on the SELECTED channel (not always left).
        This ensures the trigger locks to the signal you're actually viewing.
    */

    destL.resize (numSamples);
    destR.resize (numSamples);

    const int pos = writePosition.load (std::memory_order_relaxed);
    const TriggerMode mode = triggerMode.load (std::memory_order_relaxed);
    const float threshold = triggerLevel.load (std::memory_order_relaxed);
    const ChannelMode channel = channelMode.load (std::memory_order_relaxed);

    const int searchSize = kCircularBufferSize / 2;

    int triggerIndex = (pos - numSamples + kCircularBufferSize) % kCircularBufferSize;

    if (mode != TriggerMode::None)
    {
        for (int i = 0; i < searchSize; ++i)
        {
            int idx = (pos - numSamples - i + kCircularBufferSize) % kCircularBufferSize;
            int prevIdx = (idx - 1 + kCircularBufferSize) % kCircularBufferSize;

            // Use the selected channel for trigger detection so the
            // trigger locks to the signal being displayed.
            float current  = getSampleForChannel (idx, channel);
            float previous = getSampleForChannel (prevIdx, channel);

            bool triggered = false;

            if (mode == TriggerMode::Rising)
                triggered = (previous < threshold) && (current >= threshold);
            else if (mode == TriggerMode::Falling)
                triggered = (previous > threshold) && (current <= threshold);

            if (triggered)
            {
                triggerIndex = idx;
                break;
            }
        }
    }

    int readPos = triggerIndex;
    for (int i = 0; i < numSamples; ++i)
    {
        destL[i] = circularBufferL[readPos];
        destR[i] = circularBufferR[readPos];
        readPos = (readPos + 1) % kCircularBufferSize;
    }
}

void SignalScopeAudioProcessor::getDisplayMixed (std::vector<float>& dest,
                                                  int numSamples) const
{
    // First grab L+R with trigger detection, then mix down to one channel.
    std::vector<float> tempL, tempR;
    getDisplaySamples (tempL, tempR, numSamples);

    dest.resize (numSamples);
    const ChannelMode mode = channelMode.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        switch (mode)
        {
            case ChannelMode::Left:   dest[i] = tempL[i]; break;
            case ChannelMode::Right:  dest[i] = tempR[i]; break;
            case ChannelMode::Mid:    dest[i] = (tempL[i] + tempR[i]) * 0.5f; break;
            case ChannelMode::Side:   dest[i] = (tempL[i] - tempR[i]) * 0.5f; break;
            case ChannelMode::Sum:    dest[i] = (tempL[i] + tempR[i]) * 0.5f; break;
            default:                  dest[i] = tempL[i]; break;
        }
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
