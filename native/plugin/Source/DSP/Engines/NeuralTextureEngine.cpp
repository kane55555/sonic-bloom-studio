//==============================================================================
//  NeuralTextureEngine.cpp — AI Texture v0.1 (CACHED MODE).
//
//  See NeuralTextureEngine.h for the v0.1 contract. All file IO lives here and
//  is only ever invoked off the audio thread (preset apply / message thread).
//  renderAdd() does NOT allocate, read files, or run any model.
//==============================================================================
#include "NeuralTextureEngine.h"

namespace dida { namespace engines {

void NeuralTextureEngine::prepare(double sampleRate, int /*blockSize*/)
{
    engineSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    carverL.prepare(engineSampleRate);
    carverR.prepare(engineSampleRate);
    // Default neural-texture role keeps the cached layer out of the body band.
    if (carverL.getTrimLinear() == 1.0f) { carverL.setRole("neuralTexture"); carverR.setRole("neuralTexture"); }
    reset();
}

void NeuralTextureEngine::reset()
{
    readPos = 0.0;
    active = false;
    env = 0.0f;
    envStage = EnvStage::Idle;
    carverL.reset();
    carverR.reset();
    runningPeak = 0.0f;
    samplesSincePeakLog = 0;
}

void NeuralTextureEngine::setSharedTexture(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                                           double fileSr) noexcept
{
    texture = std::move(buffer);
    fileSampleRate = fileSr > 0.0 ? fileSr : 44100.0;
    const bool ok = texture != nullptr && texture->getNumSamples() > 0;
    hasTexture.store(ok);
    missing.store(! ok);
}

bool NeuralTextureEngine::loadTextureFile(const juce::File& file)
{
    // OFF the audio thread only. Fail-silent on any problem.
    if (! file.existsAsFile())
    {
        texture.reset();
        hasTexture.store(false);
        missing.store(true);
        return false;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        texture.reset();
        hasTexture.store(false);
        missing.store(true);
        return false;
    }

    const int numCh = juce::jlimit(1, 2, (int) reader->numChannels);
    const int len   = (int) juce::jmin<juce::int64>(reader->lengthInSamples, 60 * (juce::int64) reader->sampleRate);
    auto buf = std::make_shared<juce::AudioBuffer<float>>(2, len);
    buf->clear();
    reader->read(buf.get(), 0, len, 0, true, numCh > 1);
    if (numCh == 1)
        buf->copyFrom(1, 0, *buf, 0, 0, len); // mono -> dual mono

    setSharedTexture(std::move(buf), reader->sampleRate);
    return true;
}

void NeuralTextureEngine::noteOn(int midiNote, float vel)
{
    velocity = juce::jlimit(0.0f, 1.0f, vel);
    readPos = 0.0;
    active = hasTexture.load();

    // Playback rate. With pitch tracking the texture follows the played note
    // relative to its analysis root; otherwise it plays at native pitch.
    const double srRatio = fileSampleRate / engineSampleRate;
    if (pitchTracking)
    {
        const double rootHz = 440.0 * std::pow(2.0, ((double) rootMidi - 69.0) / 12.0);
        const double noteHz = 440.0 * std::pow(2.0, ((double) midiNote - 69.0) / 12.0);
        playRatio = srRatio * (rootHz > 0.0 ? noteHz / rootHz : 1.0);
    }
    else
    {
        playRatio = srRatio;
    }

    // Envelope: short attack so a cached texture eases in beneath the main
    // sample attack instead of clicking.
    const float attackMs = followMainEnvelope ? 30.0f : 5.0f;
    attackInc  = 1.0f / juce::jmax(1.0f, (float) (attackMs * 0.001 * engineSampleRate));
    releaseDec = 1.0f / juce::jmax(1.0f, (float) (releaseMs * 0.001 * engineSampleRate));
    env = 0.0f;
    envStage = active ? EnvStage::Attack : EnvStage::Idle;
}

void NeuralTextureEngine::noteOff()
{
    // Always release on note-off so the texture cannot feed the reverb send
    // forever (acceptance test: follows note-off).
    if (envStage != EnvStage::Idle)
        envStage = EnvStage::Release;
}

inline float NeuralTextureEngine::readInterp(int channel, double pos) const noexcept
{
    const auto* data = texture->getReadPointer(channel);
    const int n = texture->getNumSamples();
    const int i0 = (int) pos;
    if (i0 < 0 || i0 >= n) return 0.0f;
    const int i1 = (i0 + 1 < n) ? i0 + 1 : (loop ? 0 : i0);
    const float frac = (float) (pos - (double) i0);
    return data[i0] + (data[i1] - data[i0]) * frac;
}

void NeuralTextureEngine::renderAdd(float* outL, float* outR, int numSamples,
                                    float /*pitchHz*/, const ModSnapshot& mods)
{
    // Fail silent when there is no cached texture or the layer is muted.
    if (! active || ! hasTexture.load() || texture == nullptr || levelLin <= 0.0f)
        return;

    const int n = texture->getNumSamples();
    if (n <= 1) return;

    // Emergency limiter ceiling so a hot texture can never dominate the main
    // sample even if mis-tuned. Applied per-sample before the global FX bus.
    constexpr float kCeiling = 0.5f; // ~ -6 dBFS

    const float vGain = levelLin * (0.5f + 0.5f * velocity)
                      * (0.6f + 0.4f * juce::jlimit(0.0f, 1.0f, mods.velocity));
    float blockPeak = 0.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        // Envelope update (linear AR).
        switch (envStage)
        {
            case EnvStage::Attack:
                env += attackInc;
                if (env >= 1.0f) { env = 1.0f; envStage = EnvStage::Sustain; }
                break;
            case EnvStage::Release:
                env -= releaseDec;
                if (env <= 0.0f) { env = 0.0f; envStage = EnvStage::Idle; active = false; }
                break;
            case EnvStage::Sustain:
            case EnvStage::Idle:
            default:
                break;
        }
        if (! active) break;

        float l = readInterp(0, readPos);
        float r = readInterp(1, readPos);

        // Role-aware carve keeps the texture out of the main sample's body.
        l = carverL.process(l);
        r = carverR.process(r);

        const float g = vGain * env;
        l *= g; r *= g;

        // Emergency hard ceiling (gain safety).
        l = juce::jlimit(-kCeiling, kCeiling, l);
        r = juce::jlimit(-kCeiling, kCeiling, r);

        outL[s] += l;
        outR[s] += r;

        const float mag = juce::jmax(std::abs(l), std::abs(r));
        if (mag > blockPeak) blockPeak = mag;

        // Advance read head; loop or stop at end.
        readPos += playRatio;
        if (readPos >= (double) (n - 1))
        {
            if (loop) readPos -= (double) (n - 1);
            else { active = false; break; }
        }
    }

    lastPeak.store(blockPeak);

    // Debug peak logging (throttled to ~once per second of audio).
    runningPeak = juce::jmax(runningPeak, blockPeak);
    samplesSincePeakLog += numSamples;
    if (samplesSincePeakLog >= (int) engineSampleRate)
    {
        juce::Logger::writeToLog("[AI Texture v0.1] partial="
            + (debugName.isNotEmpty() ? debugName : juce::String("neuralTextureCached"))
            + " peak=" + juce::String(juce::Decibels::gainToDecibels(runningPeak, -120.0f), 2) + "dB"
            + " cached=" + (hasTexture.load() ? "true" : "false"));
        runningPeak = 0.0f;
        samplesSincePeakLog = 0;
    }
}

}} // namespace dida::engines
