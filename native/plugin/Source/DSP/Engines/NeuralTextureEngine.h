#pragma once
//==============================================================================
//  NeuralTextureEngine.h — AI Texture v0.1 (CACHED MODE).
//
//  >>> AI TEXTURE v0.1 — CACHED, NOT REALTIME NEURAL INFERENCE <<<
//
//  This engine is the plugin-side consumer of an offline AI texture pipeline:
//
//      DDSP  -> offline timbre/pitch analysis (native/ai-helper)
//      RAVE  -> future offline neural texture generation
//      v0.1  -> the PLUGIN only plays back a CACHED .wav texture file.
//
//  There is intentionally NO TensorFlow / PyTorch / RAVE / ONNX inside the
//  plugin. The audio thread only reads a pre-loaded sample buffer, so it stays
//  real-time-safe: no file IO, no allocation, no model inference in renderAdd().
//
//  Texture WAVs are loaded OFF the audio thread (during preset apply on the
//  message thread) and handed to the engine as a shared, immutable buffer.
//  When the texture file is missing the engine fails silent and exposes
//  isMissing()/isCached() so the preset-quality report can flag
//  MODEL_OR_TEXTURE_MISSING without crashing or muting the main sample.
//==============================================================================
#include "IEngineSource.h"
#include "../Layers/LayerEQCarver.h"
#include <memory>
#include <atomic>

namespace dida { namespace engines {

class NeuralTextureEngine : public IEngineSource
{
public:
    NeuralTextureEngine() = default;
    ~NeuralTextureEngine() override = default;

    EngineType type() const noexcept override { return EngineType::NeuralTextureCached; }

    void prepare(double sampleRate, int blockSize) override;
    void reset() override;

    void noteOn(int midiNote, float velocity) override;
    void noteOff() override;

    // Real-time safe: no allocation, no IO, no inference.
    void renderAdd(float* outL, float* outR, int numSamples,
                   float pitchHz, const ModSnapshot& mods) override;

    //========================================================================
    //  Off-thread configuration (called during preset apply, message thread).
    //========================================================================

    // Load a WAV/AIFF/FLAC texture file from disk. Returns false (and marks the
    // engine "missing") when the file is absent or unreadable. NEVER called from
    // the audio thread. The decoded buffer is stored as a shared immutable buffer
    // so multiple voices can share the same texture without re-decoding.
    bool loadTextureFile(const juce::File& file);

    // Share an already-decoded texture buffer between voices (preferred — load
    // once in the applier, share to every voice's engine instance).
    void setSharedTexture(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                          double fileSampleRate) noexcept;

    void setLoop(bool shouldLoop) noexcept            { loop = shouldLoop; }
    void setRootMidi(int midi) noexcept               { rootMidi = juce::jlimit(0, 127, midi); }
    void setPitchTracking(bool track) noexcept        { pitchTracking = track; }
    void setFollowMainEnvelope(bool follow) noexcept  { followMainEnvelope = follow; }
    void setReleaseMs(float ms) noexcept              { releaseMs = juce::jlimit(5.0f, 4000.0f, ms); }
    void setEqRole(const juce::String& role) noexcept { carverL.setRole(role); carverR.setRole(role); }
    void setDebugName(const juce::String& n) noexcept { debugName = n; }

    // Gain safety (AI Texture v0.1): the texture must sit QUIET under the main
    // sample. Level is specified in dB and clamped so it can never dominate.
    static constexpr float kDefaultLevelDb = -18.0f; // quiet by default
    static constexpr float kMaxLevelDb     =  -9.0f; // hard cap for any user value
    void setLevelDb(float db) noexcept
    {
        const float clamped = juce::jmin(kMaxLevelDb, db);
        levelLin = juce::Decibels::decibelsToGain(clamped, -120.0f);
    }

    //========================================================================
    //  Reporting (read off the audio thread).
    //========================================================================
    bool isCached()  const noexcept { return hasTexture.load(); }
    bool isMissing() const noexcept { return missing.load(); }
    float getLastPeak() const noexcept { return lastPeak.load(); }

private:
    inline float readInterp(int channel, double pos) const noexcept;

    std::shared_ptr<const juce::AudioBuffer<float>> texture; // immutable, shared
    double fileSampleRate = 44100.0;
    double engineSampleRate = 44100.0;

    std::atomic<bool>  hasTexture { false };
    std::atomic<bool>  missing    { false };
    std::atomic<float> lastPeak   { 0.0f };

    // Playback state (audio thread only).
    double readPos = 0.0;
    double playRatio = 1.0;
    bool   active = false;

    // Simple AR envelope so the texture follows note-off and never feeds the
    // reverb send forever.
    enum class EnvStage { Idle, Attack, Sustain, Release };
    EnvStage envStage = EnvStage::Idle;
    float    env = 0.0f;
    float    attackInc = 1.0f;
    float    releaseDec = 0.001f;

    // Parameters.
    bool  loop = true;
    int   rootMidi = 60;
    bool  pitchTracking = true;
    bool  followMainEnvelope = true;
    float releaseMs = 120.0f;
    float levelLin = juce::Decibels::decibelsToGain(kDefaultLevelDb, -120.0f);
    float velocity = 1.0f;

    LayerRoleCarver carverL, carverR;

    // Debug peak logging throttle.
    juce::String debugName;
    int    samplesSincePeakLog = 0;
    float  runningPeak = 0.0f;
};

}} // namespace dida::engines
