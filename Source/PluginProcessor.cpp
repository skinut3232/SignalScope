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

void SignalScopeAudioProcessor::getDisplaySamples (std::vector<float>& destL,
                                                    std::vector<float>& destR,
                                                    int numSamples) const
{
    /*
        Called by the UI thread to grab samples for display.

        Phase 3 adds TRIGGER DETECTION:

        Without a trigger, the display starts at "the most recent N samples",
        which means the waveform's phase position changes every frame → chaos.

        With a trigger, we search backwards through recent audio history to
        find a "trigger point" — a place where the signal crosses a threshold
        in the chosen direction (rising or falling edge). We then start the
        display from that trigger point, so the waveform appears phase-locked
        and stable.

        The search works like this (for rising edge, threshold = 0.0):
          1. Start from the most recent data and look backwards
          2. Find a sample pair where: previous < 0.0 AND current >= 0.0
          3. That crossing point becomes the start of our display window
          4. If no crossing is found, fall back to showing the most recent data

        We search through a "search window" that's larger than the display
        window. This gives us enough history to find a trigger point even
        if the signal frequency is low.
    */

    destL.resize (numSamples);
    destR.resize (numSamples);

    const int pos = writePosition.load (std::memory_order_relaxed);
    const TriggerMode mode = triggerMode.load (std::memory_order_relaxed);
    const float threshold = triggerLevel.load (std::memory_order_relaxed);

    // How far back to search for a trigger point. We search up to half the
    // buffer, which gives plenty of room to find a crossing while still
    // leaving enough samples ahead of the trigger to fill the display.
    const int searchSize = kCircularBufferSize / 2;

    // Default: start reading from (pos - numSamples), i.e. most recent data.
    // If we find a trigger, we'll override this.
    int triggerIndex = (pos - numSamples + kCircularBufferSize) % kCircularBufferSize;

    if (mode != TriggerMode::None)
    {
        // Search backwards from the most recent data to find a trigger crossing.
        // We start the search at a point that ensures we have enough samples
        // AFTER the trigger to fill the display (numSamples worth).
        //
        // searchStart = the newest sample we'd consider as a trigger point
        //             = pos - numSamples (so there are numSamples after it)
        // searchEnd   = how far back we're willing to look
        //             = searchStart - searchSize

        bool found = false;

        for (int i = 0; i < searchSize; ++i)
        {
            // Current sample index (working backwards from the newest valid trigger point)
            int idx = (pos - numSamples - i + kCircularBufferSize) % kCircularBufferSize;
            // Previous sample (one step older)
            int prevIdx = (idx - 1 + kCircularBufferSize) % kCircularBufferSize;

            float current = circularBufferL[idx];
            float previous = circularBufferL[prevIdx];

            bool triggered = false;

            if (mode == TriggerMode::Rising)
                triggered = (previous < threshold) && (current >= threshold);
            else if (mode == TriggerMode::Falling)
                triggered = (previous > threshold) && (current <= threshold);

            if (triggered)
            {
                triggerIndex = idx;
                found = true;
                break;
            }
        }

        // If no trigger found, fall through to the default (most recent samples).
        // This happens during silence or when the signal doesn't cross the threshold.
        (void) found;
    }

    // Copy numSamples starting from the trigger point (or fallback position)
    int readPos = triggerIndex;
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
