#include "Voice.h"

SynthVoice::SynthVoice() {}

bool SynthVoice::canPlaySound(juce::SynthesiserSound*) { return true; }

void SynthVoice::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRate = sr;
    oscA.prepare(sr);
    oscB.prepare(sr);
    subOsc.prepare(sr);
    filter.prepare(sr);
    ampEnv.prepare(sr);
    filterEnv.prepare(sr);
    modEnv.prepare(sr);

    subOsc.setWaveform(Oscillator::Waveform::Sine);
    recalcGlideCoeff();
}

void SynthVoice::recalcGlideCoeff() noexcept
{
    if (glideSeconds <= 0.0f)
    {
        glideCoeff = 0.0f; // instant
        return;
    }
    // one-pole towards target, ~63% in glideSeconds.
    glideCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * glideSeconds));
}

void SynthVoice::startNote(int midiNoteNumber, float vel,
                           juce::SynthesiserSound*, int /*pitchWheel*/)
{
    targetMidiNote = static_cast<float>(midiNoteNumber);

    // If no note was held (idle), snap to target so we don't glide from 60.
    if (!isActive || ampEnv.getStage() == ADSREnvelope::Stage::Idle)
        currentMidiNote = targetMidiNote;

    velocity = juce::jlimit(0.0f, 1.0f, vel);
    isActive = true;

    recalcGlideCoeff();

    ampEnv.noteOn();
    filterEnv.noteOn();
    modEnv.noteOn();
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    ampEnv.noteOff();
    filterEnv.noteOff();
    modEnv.noteOff();

    if (!allowTailOff)
    {
        clearCurrentNote();
        isActive = false;
    }
}

void SynthVoice::pitchWheelMoved(int) {}
void SynthVoice::controllerMoved(int, int) {}

float SynthVoice::renderSample()
{
    // ---- Glide / portamento ----
    if (glideCoeff > 0.0f)
        currentMidiNote = targetMidiNote + (currentMidiNote - targetMidiNote) * glideCoeff;
    else
        currentMidiNote = targetMidiNote;

    currentFrequency = dida::midiToHz(currentMidiNote);

    // ---- Update oscillator pitches ----
    oscA.setFrequency(currentFrequency);

    if (engineMode == EngineMode::FM2)
        oscB.setFrequency(currentFrequency * fmRatio);
    else
        oscB.setFrequency(currentFrequency);

    subOsc.setFrequency(currentFrequency * 0.5f); // -1 octave

    // ---- Generate sources ----
    float oscBSample = oscB.getNextSample();
    float oscASample;

    if (engineMode == EngineMode::FM2)
    {
        // Phase-mod the carrier (oscA) with modulator (oscB).
        // Modulation index in radians; capped so it can't explode.
        const float pm = oscBSample * fmAmount;
        oscA.setPhaseOffset(pm);   // see Oscillator.h: per-sample phase offset
        oscASample = oscA.getNextSample();
    }
    else
    {
        oscA.setPhaseOffset(0.0f);
        oscASample = oscA.getNextSample();
    }

    const float subSample   = subOsc.getNextSample();
    const float noiseSample = noiseGen.next();

    float mix = oscASample * oscALevel
              + oscBSample * oscBLevel
              + subSample  * subLevel
              + noiseSample * noiseLevel;

    // Soft pre-filter clip to keep the filter input bounded.
    mix = std::tanh(mix * 0.9f);

    // ---- Filter modulation: env amount + key tracking ----
    const float filtEnvVal = filterEnv.getNextSample();
    const float keyOffset  = (currentMidiNote - 60.0f) * filterKeyTrack * 100.0f; // ~1 oct per 12 semitones * keyTrack
    float modulatedCutoff  = baseCutoff
                             * std::pow(2.0f, filterEnvAmount * filtEnvVal * 4.0f) // ±4 octaves at full env
                             + keyOffset;
    modulatedCutoff = juce::jlimit(20.0f, 20000.0f, modulatedCutoff);
    filter.setCutoff(modulatedCutoff);

    const float filtered = filter.processSample(mix);

    // ---- Amp envelope + velocity response ----
    const float amp = ampEnv.getNextSample();
    if (!ampEnv.isActive())
    {
        clearCurrentNote();
        isActive = false;
        return 0.0f;
    }

    const float velCurve = 0.3f + 0.7f * velocity; // never fully silent at low vel
    return filtered * amp * velCurve;
}

void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                 int startSample, int numSamples)
{
    if (!isActive) return;

    const int numCh = outputBuffer.getNumChannels();

    for (int s = startSample; s < startSample + numSamples; ++s)
    {
        const float v = renderSample();
        if (!isActive) break;

        for (int ch = 0; ch < numCh; ++ch)
            outputBuffer.addSample(ch, s, v);
    }
}
