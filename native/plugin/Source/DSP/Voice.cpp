#include "Voice.h"

SynthVoice::SynthVoice() {}

bool SynthVoice::canPlaySound(juce::SynthesiserSound*) { return true; }

void SynthVoice::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRate = sr;
    filter.prepare(sr);
    ampEnv.prepare(sr);
    filterEnv.prepare(sr);
    modEnv.prepare(sr);
    recalcGlideCoeff();
    reset();
}

void SynthVoice::recalcGlideCoeff() noexcept
{
    if (glideSeconds <= 0.0f) { glideCoeff = 0.0f; return; }
    glideCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * glideSeconds));
}

void SynthVoice::reset() noexcept
{
    loZone = hiZone = nullptr;
    loReadPos = hiReadPos = 0.0;
    loFinished = hiFinished = true;
    isActive = false;
    filter.reset();
}

static double midiToHzD(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }

void SynthVoice::startNote(int midiNoteNumber, float vel,
                           juce::SynthesiserSound*, int /*pitchWheel*/)
{
    targetMidiNote = static_cast<float>(midiNoteNumber);

    if (! isActive || ampEnv.getStage() == ADSREnvelope::Stage::Idle)
        currentMidiNote = targetMidiNote;

    velocity = juce::jlimit(0.0f, 1.0f, vel);
    isActive = true;
    recalcGlideCoeff();

    // Pick zones from the current multisample for this note + velocity.
    loZone = hiZone = nullptr;
    zoneXfade = 0.0f;

    if (multisample && ! multisample->isEmpty())
    {
        const int playedMidi = juce::jlimit(0, 127,
            midiNoteNumber + pitchOffsetSemis);
        const int playedVel = juce::jlimit(1, 127, static_cast<int>(vel * 127.0f + 0.5f));
        multisample->pickZonesForNote(playedMidi, playedVel, &loZone, &hiZone, zoneXfade);
    }

    loReadPos = hiReadPos = 0.0;
    loFinished = (loZone == nullptr);
    hiFinished = (hiZone == nullptr);

    ampEnv.noteOn();
    filterEnv.noteOn();
    modEnv.noteOn();
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    ampEnv.noteOff();
    filterEnv.noteOff();
    modEnv.noteOff();

    if (! allowTailOff)
    {
        clearCurrentNote();
        reset();
    }
}

void SynthVoice::pitchWheelMoved(int) {}
void SynthVoice::controllerMoved(int, int) {}

void SynthVoice::readZone(const dida::SampleZone& z, double readPos,
                          float& outL, float& outR) const noexcept
{
    const auto* L = z.buffer.getReadPointer(0);
    const auto* R = z.buffer.getReadPointer(1);
    const int n = z.buffer.getNumSamples();

    const int i0 = static_cast<int>(readPos);
    if (i0 < 0 || i0 >= n - 1) { outL = outR = 0.0f; return; }

    const float frac = static_cast<float>(readPos - i0);
    outL = L[i0] + (L[i0 + 1] - L[i0]) * frac;
    outR = R[i0] + (R[i0 + 1] - R[i0]) * frac;
}

void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                 int startSample, int numSamples)
{
    if (! isActive) return;

    const int numCh = outputBuffer.getNumChannels();

    // Compute playback rate: source must be resampled by srcSR/dstSR, then
    // scaled by the pitch ratio between the played note and the zone root.
    auto rateFor = [this](const dida::SampleZone* z) -> double
    {
        if (z == nullptr) return 1.0;
        const double playedHz = midiToHzD(currentMidiNote + (double) pitchOffsetSemis);
        const double rootHz   = midiToHzD((double) z->rootMidi);
        const double pitchRatio = playedHz / rootHz;
        return (z->sourceSampleRate / sampleRate) * pitchRatio;
    };

    for (int s = startSample; s < startSample + numSamples; ++s)
    {
        // Glide
        if (glideCoeff > 0.0f)
            currentMidiNote = targetMidiNote + (currentMidiNote - targetMidiNote) * glideCoeff;
        else
            currentMidiNote = targetMidiNote;

        loStep = rateFor(loZone);
        hiStep = rateFor(hiZone);

        float l = 0.0f, r = 0.0f;
        bool hasSampleSource = (loZone != nullptr) || (hiZone != nullptr);

        if (loZone && ! loFinished)
        {
            float zl = 0.0f, zr = 0.0f;
            readZone(*loZone, loReadPos, zl, zr);
            const float w = (hiZone ? (1.0f - zoneXfade) : 1.0f);
            l += zl * w;
            r += zr * w;
            loReadPos += loStep;
            if (loReadPos >= (double) (loZone->buffer.getNumSamples() - 1))
                loFinished = true;
        }
        if (hiZone && ! hiFinished)
        {
            float zl = 0.0f, zr = 0.0f;
            readZone(*hiZone, hiReadPos, zl, zr);
            const float w = zoneXfade;
            l += zl * w;
            r += zr * w;
            hiReadPos += hiStep;
            if (hiReadPos >= (double) (hiZone->buffer.getNumSamples() - 1))
                hiFinished = true;
        }

        // Fallback synth: when the active preset has no multisample loaded
        // (or the chosen zone is null), produce a simple sine + soft-saw
        // tone so non-sample presets are audible. The full 4-layer
        // oscillator/noise renderer is wired separately in HybridPresetGenerator
        // and SampleLayer; this fallback exists so the engine never goes
        // silent during preset switching.
        if (! hasSampleSource)
        {
            const double freq = 440.0 * std::pow(2.0,
                ((double) currentMidiNote + (double) pitchOffsetSemis - 69.0) / 12.0);
            sineFallbackPhase += freq / sampleRate;
            if (sineFallbackPhase > 1.0) sineFallbackPhase -= 1.0;
            const float twoPi = juce::MathConstants<float>::twoPi;
            const float sineV = std::sin((float) sineFallbackPhase * twoPi);
            // Detuned saw-ish overtone for body
            const float over  = std::sin((float) sineFallbackPhase * twoPi * 2.0f) * 0.25f;
            l += (sineV + over) * 0.35f;
            r += (sineV + over) * 0.35f;
        }

        // Filter (mono-summed cutoff modulation, processed per channel)
        const float fEnv = filterEnv.getNextSample();
        const float keyOffset = (currentMidiNote - 60.0f) * filterKeyTrack * 100.0f;
        float modCut = baseCutoff
                     * std::pow(2.0f, filterEnvAmount * fEnv * 4.0f)
                     + keyOffset;
        modCut = juce::jlimit(20.0f, 20000.0f, modCut);
        filter.setCutoff(modCut);

        const float mono = 0.5f * (l + r);
        const float filtered = filter.processSample(mono);
        l = 0.5f * (l + filtered);
        r = 0.5f * (r + filtered);

        // Amp envelope
        const float amp = ampEnv.getNextSample();
        const float velCurve = 0.3f + 0.7f * velocity;
        const float gain = amp * velCurve;
        l *= gain; r *= gain;

        if (numCh >= 2)
        {
            outputBuffer.addSample(0, s, l);
            outputBuffer.addSample(1, s, r);
        }
        else
        {
            outputBuffer.addSample(0, s, 0.5f * (l + r));
        }

        // Note ends when amp env idle, OR (for sample voices) when both
        // sources have fully played out. Pure synth-fallback voices end
        // only when the amp envelope releases.
        const bool sampleSourceDone = (loZone == nullptr || loFinished)
                                   && (hiZone == nullptr || hiFinished);
        if (! ampEnv.isActive() || (sampleSourceDone && multisample && hasSampleSource))
        {
            clearCurrentNote();
            reset();
            break;
        }
    }
}
