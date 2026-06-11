#include "Voice.h"
#include "VoiceCard.h"

static thread_local juce::AudioBuffer<float>* currentFxSendRenderBuffer = nullptr;

// Monotonic block sequence so the reporter can pick the most-recently-rendered
// voice's live telemetry. Diagnostic only.
static std::atomic<unsigned long long> gLiveRenderSeq { 0 };

void SynthVoice::beginFxSendRender(juce::AudioBuffer<float>* fxSendBuffer) noexcept
{
    currentFxSendRenderBuffer = fxSendBuffer;
}

void SynthVoice::endFxSendRender() noexcept
{
    currentFxSendRenderBuffer = nullptr;
}

SynthVoice::LiveRenderSnapshot SynthVoice::getLiveRenderSnapshot() const noexcept
{
    LiveRenderSnapshot s;
    s.valid = liveTel_.valid.load();
    s.seq   = liveTel_.seq.load();
    s.playedMidiNote = liveTel_.playedMidiNote.load();
    s.playedVelocity = liveTel_.playedVelocity.load();
    s.selectedZoneRoot = liveTel_.selectedZoneRoot.load();
    s.selectedZoneDistanceSemitones = liveTel_.selectedZoneDistanceSemitones.load();
    s.sampleReaderSourceStartSample = liveTel_.sampleReaderSourceStartSample.load();
    s.sampleReaderSourceLengthSamples = liveTel_.sampleReaderSourceLengthSamples.load();
    s.sampleReaderPlayheadBeforeRender = liveTel_.sampleReaderPlayheadBeforeRender.load();
    s.sampleReaderPlayheadAfterRender = liveTel_.sampleReaderPlayheadAfterRender.load();
    s.sampleReaderRequestedNumSamples = liveTel_.sampleReaderRequestedNumSamples.load();
    s.sampleReaderActualSamplesRead = liveTel_.sampleReaderActualSamplesRead.load();
    s.sampleReaderLoopEnabled = liveTel_.sampleReaderLoopEnabled.load();
    s.sampleReaderAtEndBeforeRender = liveTel_.sampleReaderAtEndBeforeRender.load();
    s.sampleReaderAtEndAfterRender = liveTel_.sampleReaderAtEndAfterRender.load();
    s.zoneStartSample = liveTel_.zoneStartSample.load();
    s.zoneEndSample = liveTel_.zoneEndSample.load();
    s.zoneCropStartSample = liveTel_.zoneCropStartSample.load();
    s.zoneCropEndSample = liveTel_.zoneCropEndSample.load();
    s.effectivePlaybackStartSample = liveTel_.effectivePlaybackStartSample.load();
    s.effectivePlaybackEndSample = liveTel_.effectivePlaybackEndSample.load();
    s.liveReaderBufferPeakDbBeforeEnvelope = liveTel_.liveReaderBufferPeakDbBeforeEnvelope.load();
    s.liveReaderBufferPeakDbAfterEnvelope = liveTel_.liveReaderBufferPeakDbAfterEnvelope.load();
    s.liveReaderBufferPeakDbAfterGain = liveTel_.liveReaderBufferPeakDbAfterGain.load();
    s.liveReaderBufferNonZeroSampleCount = liveTel_.liveReaderBufferNonZeroSampleCount.load();
    s.ampEnvelopeStage = liveTel_.ampEnvelopeStage.load();
    s.ampEnvelopeStateName = ADSREnvelope::stageName((ADSREnvelope::Stage) s.ampEnvelopeStage);
    s.ampEnvelopeCurrentGain = liveTel_.ampEnvelopeCurrentGain.load();
    s.ampEnvelopeAttackMs = liveTel_.ampEnvelopeAttackMs.load();
    s.ampEnvelopeDecayMs = liveTel_.ampEnvelopeDecayMs.load();
    s.ampEnvelopeSustain = liveTel_.ampEnvelopeSustain.load();
    s.ampEnvelopeReleaseMs = liveTel_.ampEnvelopeReleaseMs.load();
    s.voiceGainDb = liveTel_.voiceGainDb.load();
    s.layerGainDb = liveTel_.layerGainDb.load();
    s.finalVoiceGainDb = liveTel_.finalVoiceGainDb.load();
    return s;
}



SynthVoice::SynthVoice()
{
    // Wire LegacyOscillatorStub callbacks into real voice state so the
    // OSC A / OSC B waveform, detune and pulse-width controls actually
    // affect the rendered sound.
    oscAStub.onWaveform   = [this](Oscillator::Waveform w) { oscAWave = w; };
    oscAStub.onDetune     = [this](float c)                { oscADetuneCents = c; };
    oscAStub.onPulseWidth = [this](float p)                { oscAPulseWidth  = juce::jlimit(0.01f, 0.99f, p); };

    oscBStub.onWaveform   = [this](Oscillator::Waveform w) { oscBWave = w; };
    oscBStub.onDetune     = [this](float c)                { oscBDetuneCents = c; };
    oscBStub.onPulseWidth = [this](float p)                { oscBPulseWidth  = juce::jlimit(0.01f, 0.99f, p); };
}

bool SynthVoice::canPlaySound(juce::SynthesiserSound*) { return true; }

void SynthVoice::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    preparedBlockSize = juce::jmax(16, blockSize);
    filter.prepare(sr);
    ampEnv.prepare(sr);
    filterEnv.prepare(sr);
    modEnv.prepare(sr);

    // Per-layer role-aware carving (HP+LP+trim) — picks the right band per
    // role so layers stop fighting and start sounding like one instrument.
    noiseCarverL.prepare(sr); noiseCarverR.prepare(sr);
    noiseCarverL.setRole("air"); noiseCarverR.setRole("air");
    subCarver.prepare(sr);  subCarver.setRole("sub");
    oscBCarver.prepare(sr); oscBCarver.setRole("warmth");


    unison.prepare(sr);
    unison.setConfig(unisonRenderVoices, unisonRenderDetune, unisonRenderSpread, unisonRenderDrift);
    exciter.prepare(sr);
    spreader.prepare(sr);

    partialScratch.setSize(2, preparedBlockSize, false, false, true);
    for (auto& slot : partials_)
        if (slot.engine) slot.engine->prepare(sampleRate, preparedBlockSize);

    recalcGlideCoeff();
    reset();
}

void SynthVoice::clearPartials() noexcept
{
    for (auto& slot : partials_) { slot.engine.reset(); slot.enabled = false; }
}

void SynthVoice::setPartial(int idx,
                            std::unique_ptr<dida::engines::IEngineSource> engine,
                            bool enabled, float level, float pan,
                            int pitchSemis, float fineCents,
                            bool isNeuralTexture) noexcept
{
    if (idx < 0 || idx >= kMaxPartials) return;
    auto& slot = partials_[(size_t) idx];
    slot.engine          = std::move(engine);
    slot.enabled         = enabled && slot.engine != nullptr;
    slot.level           = juce::jlimit(0.0f, 4.0f, level);
    slot.pan             = juce::jlimit(-1.0f, 1.0f, pan);
    slot.pitchSemis      = pitchSemis;
    slot.fineCents       = fineCents;
    slot.isNeuralTexture = isNeuralTexture;
    if (slot.engine) slot.engine->prepare(sampleRate, preparedBlockSize);
}

bool SynthVoice::hasActivePartials() const noexcept
{
    for (auto& slot : partials_)
        if (slot.enabled && slot.engine) return true;
    return false;
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
    oscBPhase = subPhase = fmModPhase = sineFallbackPhase = 0.0;
    fmOp3Phase = fmOp4Phase = 0.0;
    fmFeedbackZ = 0.0f;
    fxSendLevel = 0.0f;
    fxSendTarget = 0.0f;
    fxSendReleaseStep = 0.0f;
    fxSendReleaseSamples = 0;
    fxSendReleaseCounter = 0;
    fxSendActive = false;
    noteReleasedForFxSend = false;
    pinkB0 = pinkB1 = pinkB2 = 0.0f;
    filter.reset();
    for (auto& slot : partials_)
        if (slot.engine) slot.engine->reset();
}

void SynthVoice::beginFxSendRelease(float releaseMs) noexcept
{
    fxSendTarget = 0.0f;
    noteReleasedForFxSend = true;
    fxSendReleaseSamples = juce::jmax(1, (int) std::ceil(juce::jlimit(1.0f, 500.0f, releaseMs)
                                                       * 0.001 * sampleRate));
    fxSendReleaseCounter = fxSendReleaseSamples;
    fxSendReleaseStep = fxSendLevel / (float) fxSendReleaseSamples;
    fxSendActive = fxSendLevel > 0.000001f;
}

void SynthVoice::chokeFxSend(float fadeMs) noexcept
{
    beginFxSendRelease(fadeMs);
}

float SynthVoice::nextFxSendGain() noexcept
{
    if (! fxSendActive) return 0.0f;

    if (noteReleasedForFxSend || fxSendTarget <= 0.0f)
    {
        if (fxSendReleaseCounter > 0)
        {
            fxSendLevel = juce::jmax(0.0f, fxSendLevel - fxSendReleaseStep);
            --fxSendReleaseCounter;
        }
        else
        {
            fxSendLevel = 0.0f;
            fxSendActive = false;
        }
    }

    return fxSendActive ? fxSendLevel : 0.0f;
}


static double midiToHzD(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }

static juce::String midiToNoteName(int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    midi = juce::jlimit(0, 127, midi);
    return juce::String(names[midi % 12]) + juce::String((midi / 12) - 1);
}

static juce::String signedSemitoneOffset(int semis)
{
    return semis > 0 ? (juce::String("+") + juce::String(semis)) : juce::String(semis);
}

static void multisampleDebugLog(const juce::String& message)
{
    const auto line = "[DIDITAGAIN multisample] " + message;
    DBG(line);
    juce::Logger::writeToLog(line);
}

void SynthVoice::startNote(int midiNoteNumber, float vel,
                           juce::SynthesiserSound*, int)
{
    targetMidiNote = static_cast<float>(midiNoteNumber);
    if (! isActive || ampEnv.getStage() == ADSREnvelope::Stage::Idle)
        currentMidiNote = targetMidiNote;

    velocity = juce::jlimit(0.0f, 1.0f, vel);
    isActive = true;
    fxSendLevel = 1.0f;
    fxSendTarget = 1.0f;
    fxSendReleaseStep = 0.0f;
    fxSendReleaseSamples = 0;
    fxSendReleaseCounter = 0;
    fxSendActive = true;
    noteReleasedForFxSend = false;
    mainSamplePeakLin_.store(0.0f);
    recalcGlideCoeff();

    loZone = hiZone = nullptr;
    zoneXfade = 0.0f;

    if (multisample && ! multisample->isEmpty())
    {
        const int playedMidi = juce::jlimit(0, 127, midiNoteNumber + pitchOffsetSemis);
        const int playedVel  = juce::jlimit(1, 127, static_cast<int>(vel * 127.0f + 0.5f));
        multisample->pickZonesForNote(playedMidi, playedVel, &loZone, &hiZone, zoneXfade);

        if (loZone != nullptr)
        {
            const bool fallbackNearest = playedMidi < loZone->lowKey || playedMidi > loZone->highKey;
            juce::String message;
            message << "selected note " << midiToNoteName(midiNoteNumber)
                    << " uses " << loZone->fileName
                    << " root=" << midiToNoteName(loZone->rootMidi)
                    << " zone=" << midiToNoteName(loZone->lowKey) << "-" << midiToNoteName(loZone->highKey)
                    << " offset=" << signedSemitoneOffset(playedMidi - loZone->rootMidi);
            if (fallbackNearest)
                message << " WARNING no matching hard zone; nearest root fallback";
            multisampleDebugLog(message);
        }
        else
        {
            multisampleDebugLog("NoteOn " + midiToNoteName(midiNoteNumber) + " WARNING no zone selected");
        }
    }

    // Start at cropStart (in samples) so the user-trimmed region plays first.
    auto startPos = [this](const dida::SampleZone* z) -> double {
        if (z == nullptr) return 0.0;
        const double n = (double) z->buffer.getNumSamples();
        return juce::jlimit(0.0, n - 2.0, (double) cropStartFrac * n);
    };
    loReadPos = startPos(loZone);
    hiReadPos = startPos(hiZone);
    loFinished = (loZone == nullptr);
    hiFinished = (hiZone == nullptr);
    oscBPhase = subPhase = fmModPhase = 0.0;
    fmOp3Phase = fmOp4Phase = 0.0;
    fmFeedbackZ = 0.0f;

    // ---- Per-layer micro-timing offsets (0.5..8 ms): each note picks new
    //      small random delays per support layer. Reduces the "stacked WAVs"
    //      artifact and adds natural ensemble feel. ----
    std::uniform_real_distribution<float> jitter(0.0005f, 0.008f);
    oscBStartOffsetSamples   = (int) (jitter(noiseRng) * (float) sampleRate);
    subStartOffsetSamples    = (int) (jitter(noiseRng) * (float) sampleRate);
    noiseStartOffsetSamples  = (int) (jitter(noiseRng) * (float) sampleRate * 0.5f);
    sampleTickCounter = 0;

    // Slight random phase for stereo decorrelation on synth layers.
    std::uniform_real_distribution<double> phaseJ(0.0, 1.0);
    oscBPhase = phaseJ(noiseRng);
    subPhase  = phaseJ(noiseRng) * 0.5;

    noiseCarverL.reset(); noiseCarverR.reset();
    subCarver.reset(); oscBCarver.reset();

    // ---- followMainEnvelope fade-in init ----
    // Each layer fades in over its configured fadeMs window so reinforcement
    // sine/triangle/noise/sub layers ease in beneath the main sample attack
    // instead of beeping out in front. Also enforces minimum 8 ms so even
    // an attack of 0 cannot produce a click.
    auto initFade = [this](bool follow, float ms, int& remaining, int& total)
    {
        if (! follow) { remaining = total = 0; return; }
        const int samples = (int) std::ceil((double) juce::jmax(8.0f, ms) * 0.001 * sampleRate);
        total = samples; remaining = samples;
    };
    initFade(oscBFollowMain,  oscBFollowFadeMs,  oscBFadeSamplesRemaining,  oscBFadeSamplesTotal);
    initFade(noiseFollowMain, noiseFollowFadeMs, noiseFadeSamplesRemaining, noiseFadeSamplesTotal);
    initFade(subFollowMain,   subFollowFadeMs,   subFadeSamplesRemaining,   subFadeSamplesTotal);

    // Per-note phase scramble for the unison stack — anti-machine-gun.
    unison.randomizePhasesAndDrift();



    ampEnv.noteOn();
    filterEnv.noteOn();
    modEnv.noteOn();

    for (auto& slot : partials_)
        if (slot.enabled && slot.engine)
            slot.engine->noteOn(static_cast<int>(targetMidiNote), velocity);

    // BUG 4 diagnostic: VOICE_STARTED surfaces the per-voice state that decides
    // whether a note can make sound — sample/zone mapping, fallback synthesis,
    // partial engines, and the amp-envelope target. "Silent" presets (e.g.
    // Clean Tuned Piano) reveal their root cause here: no zone selected with
    // fallback synthesis off and no active partials means nothing can sound.
    {
        int activePartials = 0;
        for (auto& slot : partials_)
            if (slot.enabled && slot.engine) ++activePartials;
        const bool hasSample = multisample != nullptr && ! multisample->isEmpty();
        const bool sampleVoice = hasSample && loZone != nullptr;
        const bool willProduceSound = sampleVoice || fallbackSynthesisEnabled || activePartials > 0;
        juce::String vs;
        vs << "VOICE_STARTED note=" << midiToNoteName(midiNoteNumber)
           << " midi=" << midiNoteNumber
           << " velocity=" << juce::String(velocity, 3)
           << " hasMultisample=" << (hasSample ? "true" : "false")
           << " zoneSelected=" << (loZone != nullptr ? "true" : "false")
           << " zoneFile=" << (loZone != nullptr ? loZone->fileName : juce::String("none"))
           << " fallbackSynthesisEnabled=" << (fallbackSynthesisEnabled ? "true" : "false")
           << " activePartials=" << activePartials
           << " ampStage=" << (int) ampEnv.getStage()
           << " willProduceSound=" << (willProduceSound ? "true" : "false");
        multisampleDebugLog(vs);
    }
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    ampEnv.noteOff();
    filterEnv.noteOff();
    modEnv.noteOff();
    beginFxSendRelease(fxSendReleaseMs);
    for (auto& slot : partials_)
        if (slot.enabled && slot.engine) slot.engine->noteOff();
    if (! allowTailOff)
    {
        fxSendLevel = 0.0f;
        fxSendTarget = 0.0f;
        fxSendActive = false;
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

// PolyBLEP correction (after Välimäki/Huovilainen). Removes the worst of
// the aliasing on saw/square/pulse at high pitches with ~zero CPU cost.
// dt is the per-sample phase increment (freq / sampleRate), assumed > 0.
static inline float polyBLEP(float t, float dt) noexcept
{
    if (dt <= 0.0f) return 0.0f;
    if (t < dt)        { const float x = t / dt;       return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
    return 0.0f;
}

float SynthVoice::renderOscShape(Oscillator::Waveform w, float p, float pw) const noexcept
{
    using W = Oscillator::Waveform;
    const float twoPi = juce::MathConstants<float>::twoPi;
    // dt is unknown here (call sites that need anti-aliasing pass through
    // renderOscShapeAA below). This path keeps the naive shape for shapes
    // that don't alias (sine/triangle) or as a safe fallback.
    switch (w)
    {
        case W::Sine:      return std::sin(p * twoPi);
        case W::Triangle:  return 4.0f * std::abs(p - std::floor(p + 0.5f)) - 1.0f;
        case W::Saw:       return 2.0f * (p - std::floor(p + 0.5f));
        case W::Square:    return (p - std::floor(p)) < 0.5f ? 1.0f : -1.0f;
        case W::Pulse:     return (p - std::floor(p)) < pw   ? 1.0f : -1.0f;
        case W::SuperSaw:
        {
            float s = 0.0f;
            for (int i = 0; i < 5; ++i)
            {
                const float det = 1.0f + 0.005f * (i - 2);
                const float ph  = p * det;
                s += 2.0f * (ph - std::floor(ph + 0.5f));
            }
            return s * 0.2f;
        }
        case W::FmCarrier: return std::sin(p * twoPi);
        case W::Wavetable: return std::sin(p * twoPi) + 0.3f * std::sin(p * twoPi * 3.0f);
        default:           return std::sin(p * twoPi);
    }
}

// Band-limited variant used by hot oscillator paths (Osc A fallback, Osc B).
// Saw / Square / Pulse get polyBLEP correction; everything else falls through.
static inline float renderOscShapeAA(Oscillator::Waveform w, float p, float pw, float dt) noexcept
{
    using W = Oscillator::Waveform;
    const float twoPi = juce::MathConstants<float>::twoPi;
    auto wrap = [](float x) { return x - std::floor(x); };
    p = wrap(p);
    switch (w)
    {
        case W::Sine:      return std::sin(p * twoPi);
        case W::Triangle:  return 4.0f * std::abs(p - std::floor(p + 0.5f)) - 1.0f;
        case W::Saw:
        {
            float v = 2.0f * p - 1.0f;
            v -= polyBLEP(p, dt);
            return v;
        }
        case W::Square:
        {
            float v = (p < 0.5f) ? 1.0f : -1.0f;
            v += polyBLEP(p, dt);
            v -= polyBLEP(wrap(p + 0.5f), dt);
            return v;
        }
        case W::Pulse:
        {
            const float pwC = juce::jlimit(0.05f, 0.95f, pw);
            float v = (p < pwC) ? 1.0f : -1.0f;
            v += polyBLEP(p, dt);
            v -= polyBLEP(wrap(p + (1.0f - pwC)), dt);
            return v;
        }
        case W::SuperSaw:
        {
            // Mini 5-voice supersaw with polyBLEP on each saw. 1/sqrt(N)
            // normalisation keeps the stack at unity loudness.
            float s = 0.0f;
            for (int i = 0; i < 5; ++i)
            {
                const float det = 1.0f + 0.005f * (i - 2);
                const float ph  = wrap(p * det);
                float v = 2.0f * ph - 1.0f;
                v -= polyBLEP(ph, dt * det);
                s += v;
            }
            return s * 0.4472136f; // 1/sqrt(5)
        }
        case W::FmCarrier: return std::sin(p * twoPi);
        case W::Wavetable: return std::sin(p * twoPi) + 0.3f * std::sin(p * twoPi * 3.0f);
        default:           return std::sin(p * twoPi);
    }
}

float SynthVoice::nextNoiseSample() noexcept
{
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    const float white = d(noiseRng);
    if (noiseType == 0) return white;
    // Simple 3-pole pink filter (Paul Kellet style, simplified)
    pinkB0 = 0.99765f * pinkB0 + white * 0.0990460f;
    pinkB1 = 0.96300f * pinkB1 + white * 0.2965164f;
    pinkB2 = 0.57000f * pinkB2 + white * 1.0526913f;
    return (pinkB0 + pinkB1 + pinkB2 + white * 0.1848f) * 0.25f;
}

void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                 int startSample, int numSamples)
{
    if (! isActive) return;

    const int numCh = outputBuffer.getNumChannels();

    // ---- Voice-card calibration (Juno/Jupiter/Prophet-style) ----
    const auto& card = dida::VoiceCardBank::instance().get(voiceCardIndex);
    const float vAmt = vintageAmount;
    const float cardPitchCents = dida::vintageMix(card.pitchCents,     vAmt);
    const float cardVcaDb      = dida::vintageMix(card.vcaGainDb,      vAmt)
                               + dida::vintageMix(card.gainDb,         vAmt);
    const float cardPan        = dida::vintageMix(card.panOffset,      vAmt);
    const float cardCutoffHz   = dida::vintageMix(card.cutoffHz,       vAmt);
    const float vcaGainLin     = std::pow(10.0f, cardVcaDb / 20.0f);

    // Slow analog drift (0.03..0.25 Hz per card). Adds a couple cents of
    // wobble — the heart of the "alive" character vs static digital pitch.
    const double driftInc = (double) card.driftHz / sampleRate;


    // Capture card pitch + drift in a per-block "extra cents" closure used
    // by both the sample-rate computation and the synth fallback path.
    auto extraCentsNow = [&]() {
        const float driftCents = std::sin((float) driftPhase * juce::MathConstants<float>::twoPi)
                               * (2.5f * vAmt);   // up to ±2.5c at full vintage
        return cardPitchCents + driftCents;
    };

    auto rateFor = [&](const dida::SampleZone* z) -> double
    {
        if (z == nullptr) return 1.0;
        const double extraRatio = std::pow(2.0, (double) extraCentsNow() / 1200.0);
        const double playedHz = midiToHzD(currentMidiNote + (double) pitchOffsetSemis) * extraRatio;
        const double rootHz   = midiToHzD((double) z->rootMidi);
        const double srRatio  = z->sourceSampleRate / sampleRate;
        return pitchTracking ? srRatio * (playedHz / rootHz) : srRatio;
    };


    // Helper: read a zone with crop/loop crossfade respected.
    auto readWithLoop = [this](const dida::SampleZone& z, double& pos, bool& finished,
                               float& outL, float& outR)
    {
        const double n = (double) z.buffer.getNumSamples();
        const double cropEnd  = juce::jlimit(2.0, n - 1.0, (double) cropEndFrac  * n);
        const double loopStart = juce::jlimit(0.0, cropEnd - 2.0, (double) loopStartFrac * n);
        const double loopEnd   = juce::jlimit(loopStart + 2.0, cropEnd, (double) loopEndFrac * n);
        const double xfadeSamples = juce::jlimit(0.0, (loopEnd - loopStart) * 0.45,
                                                 (double) loopCrossfadeMs * 0.001 * sampleRate);

        readZone(z, pos, outL, outR);

        // Equal-power crossfade tail near loopEnd: blend with samples from loopStart
        if (sampleLooping && ! oneShotMode && xfadeSamples > 1.0
            && pos > loopEnd - xfadeSamples)
        {
            const double into = pos - (loopEnd - xfadeSamples);
            const double t = juce::jlimit(0.0, 1.0, into / xfadeSamples);
            const float gIn  = std::sin((float) t * juce::MathConstants<float>::halfPi);
            const float gOut = std::cos((float) t * juce::MathConstants<float>::halfPi);
            float bL, bR;
            readZone(z, loopStart + into, bL, bR);
            outL = outL * gOut + bL * gIn;
            outR = outR * gOut + bR * gIn;
        }
    };

    // ---- Multi-engine partials: render block-rate into scratch, then mix
    //      into the per-sample sample bus before the filter. PCM partials
    //      render nothing (handled by the legacy sample path above).
    const bool partialsActive = hasActivePartials();
    if (partialsActive)
    {
        if (partialScratch.getNumSamples() < numSamples)
            partialScratch.setSize(2, juce::jmax(numSamples, preparedBlockSize), false, false, true);
        partialScratch.clear(0, numSamples);
        auto* pL = partialScratch.getWritePointer(0);
        auto* pR = partialScratch.getWritePointer(1);
        dida::engines::ModSnapshot mod;
        mod.velocity = velocity;
        for (auto& slot : partials_)
        {
            if (! (slot.enabled && slot.engine)) continue;
            if (slot.engine->type() == dida::engines::EngineType::Pcm) continue;
            // AI Texture v0.2 — DEBUG solo: when soloing the neural texture, skip
            // every non-neural partial so only the cached texture is audible.
            if (soloNeuralTexture && ! slot.isNeuralTexture) continue;
            // AI Texture v0.1 — FX-send safety: when the live "Texture Amount"
            // is 0 or the panel toggle is off, neuralTextureLiveGain == 0. Skip
            // the neural render ENTIRELY so the cached texture contributes zero
            // signal and can never reach the reverb/delay/chorus sends.
            if (slot.isNeuralTexture && neuralTextureLiveGain <= 0.0f) continue;
            const double midi = (double) currentMidiNote + (double) slot.pitchSemis
                              + (double) slot.fineCents / 100.0;
            const float pitchHz = (float) midiToHzD(midi);
            // Render additively into a private temp range so we can apply
            // per-partial level/pan before summing into the shared scratch.
            std::array<float, 2048> tL{}, tR{};
            const int chunk = juce::jmin((int) tL.size(), numSamples);
            int done = 0;
            while (done < numSamples)
            {
                const int n = juce::jmin(chunk, numSamples - done);
                std::fill_n(tL.data(), n, 0.0f);
                std::fill_n(tR.data(), n, 0.0f);
                slot.engine->renderAdd(tL.data(), tR.data(), n, pitchHz, mod);
                // AI Texture v0.1: scale neural texture partials by the live
                // shaped "Texture Amount" (0..1). The texture's absolute level
                // (and the -9 dB safe ceiling) is owned by NeuralTextureEngine;
                // this multiplier can only attenuate further. amount=0 -> 0.
                const float liveGain = slot.isNeuralTexture ? neuralTextureLiveGain : 1.0f;
                const float panAng = (slot.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                const float gL = std::cos(panAng) * 1.41421356f * slot.level * liveGain;
                const float gR = std::sin(panAng) * 1.41421356f * slot.level * liveGain;
                for (int i = 0; i < n; ++i)
                {
                    pL[done + i] += tL[i] * gL;
                    pR[done + i] += tR[i] * gR;
                }
                done += n;
            }
        }
    }

    for (int s = startSample; s < startSample + numSamples; ++s)
    {

        if (glideCoeff > 0.0f)
            currentMidiNote = targetMidiNote + (currentMidiNote - targetMidiNote) * glideCoeff;
        else
            currentMidiNote = targetMidiNote;

        loStep = rateFor(loZone);
        hiStep = rateFor(hiZone);

        // ---- Layer 1: Sample (with oscALevel acting as sample volume) ----
        float sampL = 0.0f, sampR = 0.0f;
        const bool hasSampleSource = (loZone != nullptr) || (hiZone != nullptr);

        auto advanceLoop = [this](const dida::SampleZone& z, double& pos, bool& finished)
        {
            const double n = (double) z.buffer.getNumSamples();
            const double cropEnd  = juce::jlimit(2.0, n - 1.0, (double) cropEndFrac  * n);
            const double loopStart = juce::jlimit(0.0, cropEnd - 2.0, (double) loopStartFrac * n);
            const double loopEnd   = juce::jlimit(loopStart + 2.0, cropEnd, (double) loopEndFrac * n);
            if (pos >= cropEnd)
            {
                if (sampleLooping && ! oneShotMode && loopEnd > loopStart + 2.0)
                    pos = loopStart + (pos - loopEnd);
                else
                    finished = true;
            }
        };

        if (loZone && ! loFinished)
        {
            float zl, zr; readWithLoop(*loZone, loReadPos, loFinished, zl, zr);
            const float w = (hiZone ? (1.0f - zoneXfade) : 1.0f);
            if (! soloNeuralTexture) { sampL += zl * w; sampR += zr * w; }
            loReadPos += loStep;
            advanceLoop(*loZone, loReadPos, loFinished);
        }
        if (hiZone && ! hiFinished)
        {
            float zl, zr; readWithLoop(*hiZone, hiReadPos, hiFinished, zl, zr);
            if (! soloNeuralTexture) { sampL += zl * zoneXfade; sampR += zr * zoneXfade; }
            hiReadPos += hiStep;
            advanceLoop(*hiZone, hiReadPos, hiFinished);
        }

        if (partialsActive)
        {
            const int idx = s - startSample;
            sampL += partialScratch.getSample(0, idx);
            sampR += partialScratch.getSample(1, idx);
        }



        // Legacy synth fallback only for factory/pure-synth presets. Imported
        // hybrid presets disable this so a missing sample cannot masquerade as
        // the same cheap synth sound.
        if (! hasSampleSource && fallbackSynthesisEnabled && ! soloNeuralTexture)
        {
            // Card pitch + slow drift add 0..few cents of vintage life.
            const double totalCents = (double) oscADetuneCents + (double) extraCentsNow();
            const double f = midiToHzD((double) currentMidiNote + (double) pitchOffsetSemis
                                       + totalCents / 100.0);
            const float dt = (float) (f / sampleRate);
            float synL = 0.0f, synR = 0.0f;

            auto renderSubtractive = [&]() {
                if (unisonRenderVoices > 1)
                {
                    auto shape = dida::UnisonEngine::Shape::Saw;
                    if (oscAWave == Oscillator::Waveform::Square || oscAWave == Oscillator::Waveform::Pulse)
                        shape = dida::UnisonEngine::Shape::Square;
                    else if (oscAWave == Oscillator::Waveform::Triangle)
                        shape = dida::UnisonEngine::Shape::Triangle;
                    unison.renderSample((float) f, shape, synL, synR);
                    synL *= 0.5f; synR *= 0.5f;
                }
                else
                {
                    sineFallbackPhase += f / sampleRate;
                    if (sineFallbackPhase > 1.0) sineFallbackPhase -= 1.0;
                    const float v = renderOscShapeAA(oscAWave, (float) sineFallbackPhase, oscAPulseWidth, dt) * 0.5f;
                    synL = synR = v;
                }
            };

            // ---- FM engine (DX-style) ----
            //   ops=2  →  op2 (mod, with feedback) → op1 (sine carrier)
            //   ops=4  →  op4 → op3 → op2 (with feedback) → op1 (sine carrier)
            //
            // fmAmount (0..12) is treated as a master modulation index;
            // fmRatio is op2's ratio. op3 sits at 2x ratio, op4 at 3x.
            // Each modulator's index is tapered down the chain so the tone
            // stays musical instead of dissolving into noise, and op2 carries
            // self-feedback that gives FM its bell/edge character. Output is
            // gain-compensated so a high mod-index doesn't peak the bus.
            auto renderFM = [&](int ops) {
                const float twoPi = juce::MathConstants<float>::twoPi;
                const float masterIdx = juce::jlimit(0.0f, 8.0f, fmAmount * 0.6f);
                const float feedbackAmt = juce::jlimit(0.0f, 1.6f, masterIdx * 0.18f);

                // op4 (only used for FM4)
                float mod4 = 0.0f;
                if (ops >= 4)
                {
                    fmOp4Phase += (f * fmRatio * 3.0f) / sampleRate;
                    if (fmOp4Phase >= 1.0) fmOp4Phase -= 1.0;
                    mod4 = std::sin((float) fmOp4Phase * twoPi) * (masterIdx * 0.45f);
                }

                // op3 (only used for FM4), modulated by op4
                float mod3 = 0.0f;
                if (ops >= 4)
                {
                    fmOp3Phase += (f * fmRatio * 2.0f) / sampleRate;
                    if (fmOp3Phase >= 1.0) fmOp3Phase -= 1.0;
                    mod3 = std::sin((float) fmOp3Phase * twoPi + mod4)
                         * (masterIdx * 0.65f);
                }

                // op2 — main modulator with self-feedback (the DX edge).
                fmModPhase += (f * fmRatio) / sampleRate;
                if (fmModPhase >= 1.0) fmModPhase -= 1.0;
                const float mod2 = std::sin((float) fmModPhase * twoPi
                                            + mod3
                                            + fmFeedbackZ * feedbackAmt)
                                 * masterIdx;
                fmFeedbackZ = mod2 / juce::jmax(0.0001f, masterIdx); // normalised -1..1

                // op1 — sine carrier.
                sineFallbackPhase += f / sampleRate;
                if (sineFallbackPhase > 1.0) sineFallbackPhase -= 1.0;
                const float carrier = std::sin((float) sineFallbackPhase * twoPi + mod2);

                // Gain compensation: as mod index rises the harmonic spread
                // widens and peak amplitude stays ~1.0, so a fixed 0.5 trim
                // is enough; tame a touch more when feedback is heavy so the
                // attack transient doesn't bite.
                const float compensated = carrier * (0.50f / (1.0f + feedbackAmt * 0.25f));
                synL = synR = compensated;
            };

            // Wavetable: morph sine → triangle → saw → square by macro position
            // (use oscAPulseWidth as morph 0..1 if no dedicated parameter).
            auto renderWavetable = [&]() {
                sineFallbackPhase += f / sampleRate;
                if (sineFallbackPhase > 1.0) sineFallbackPhase -= 1.0;
                const float ph = (float) sineFallbackPhase;
                const float morph = juce::jlimit(0.0f, 1.0f, oscAPulseWidth);
                const float s1 = std::sin(ph * juce::MathConstants<float>::twoPi);
                const float s2 = 4.0f * std::abs(ph - std::floor(ph + 0.5f)) - 1.0f;
                const float s3 = 2.0f * ph - 1.0f - polyBLEP(ph, dt);
                const float s4 = (ph < 0.5f ? 1.0f : -1.0f) + polyBLEP(ph, dt)
                               - polyBLEP(ph + 0.5f - std::floor(ph + 0.5f), dt);
                const float seg = morph * 3.0f;
                const int   i0  = juce::jlimit(0, 2, (int) seg);
                const float f0  = seg - (float) i0;
                const float frames[4] = { s1, s2, s3, s4 };
                synL = synR = (frames[i0] * (1.0f - f0) + frames[i0 + 1] * f0) * 0.5f;
            };

            // Layered: subtractive + sine sub-octave + soft triangle on top.
            auto renderLayered = [&]() {
                renderSubtractive();
                subPhase += (f * 0.5) / sampleRate;
                if (subPhase >= 1.0) subPhase -= 1.0;
                const float subS = std::sin((float) subPhase * juce::MathConstants<float>::twoPi) * 0.25f;
                synL += subS; synR += subS;
            };

            switch (engineMode)
            {
                case EngineMode::FM2:      renderFM(2);       break;
                case EngineMode::FM4:      renderFM(4);       break;
                case EngineMode::Wavetable:renderWavetable(); break;
                case EngineMode::Layered:  renderLayered();   break;
                case EngineMode::Subtractive:
                default:                   renderSubtractive(); break;
            }
            sampL += synL; sampR += synR;
        }



        sampL *= oscALevel;
        sampR *= oscALevel;

        // ---- Layer 2: Osc B (with optional FM modulation) ----
        // Gated by micro-timing offset so support layers enter a few ms after
        // the sample attack — eliminates the "stacked" feel.
        float oscBOut = 0.0f;
        if (oscBLevel > 0.0001f && sampleTickCounter >= oscBStartOffsetSamples)
        {
            const double bMidi = (double) currentMidiNote + (double) oscBPitchOffsetSemis
                               + (double) oscBDetuneCents / 100.0;
            const double bHz = midiToHzD(bMidi);

            float fmOffset = 0.0f;
            if (fmAmount > 0.0001f)
            {
                fmModPhase += (bHz * fmRatio) / sampleRate;
                if (fmModPhase > 1.0) fmModPhase -= 1.0;
                fmOffset = std::sin((float) fmModPhase * juce::MathConstants<float>::twoPi)
                         * fmAmount * 0.1f;
            }

            const float bDt = (float) (bHz / sampleRate);
            oscBPhase += bHz / sampleRate;
            if (oscBPhase > 1.0) oscBPhase -= 1.0;
            const float ph = (float) oscBPhase + fmOffset;
            oscBOut = renderOscShapeAA(oscBWave, ph - std::floor(ph), oscBPulseWidth, bDt) * oscBLevel;
            // Role-aware HP+LP+trim carve so Osc B parks in its assigned band.
            oscBOut = oscBCarver.process(oscBOut);
            // followMainEnvelope fade-in (equal-power ramp 0..1).
            if (oscBFadeSamplesRemaining > 0 && oscBFadeSamplesTotal > 0)
            {
                const float t = 1.0f - (float) oscBFadeSamplesRemaining / (float) oscBFadeSamplesTotal;
                oscBOut *= std::sin(t * juce::MathConstants<float>::halfPi);
                --oscBFadeSamplesRemaining;
            }
        }

        // ---- Layer 3: Sub (one octave below current note, sine) ----
        float subOut = 0.0f;
        if (subLevel > 0.0001f && sampleTickCounter >= subStartOffsetSamples)
        {
            const double subHz = midiToHzD((double) currentMidiNote - 12.0);
            subPhase += subHz / sampleRate;
            if (subPhase > 1.0) subPhase -= 1.0;
            subOut = std::sin((float) subPhase * juce::MathConstants<float>::twoPi) * subLevel;
            subOut = subCarver.process(subOut);
            if (subFadeSamplesRemaining > 0 && subFadeSamplesTotal > 0)
            {
                const float t = 1.0f - (float) subFadeSamplesRemaining / (float) subFadeSamplesTotal;
                subOut *= std::sin(t * juce::MathConstants<float>::halfPi);
                --subFadeSamplesRemaining;
            }
        }

        // ---- Layer 4: Noise / Air (stereo decorrelated, role-carved) ----
        float noiseOutL = 0.0f, noiseOutR = 0.0f;
        if (noiseLevel > 0.0001f && sampleTickCounter >= noiseStartOffsetSamples)
        {
            noiseOutL = noiseCarverL.process(nextNoiseSample()) * noiseLevel;
            noiseOutR = noiseCarverR.process(nextNoiseSample()) * noiseLevel;
            if (noiseFadeSamplesRemaining > 0 && noiseFadeSamplesTotal > 0)
            {
                const float t = 1.0f - (float) noiseFadeSamplesRemaining / (float) noiseFadeSamplesTotal;
                const float g = std::sin(t * juce::MathConstants<float>::halfPi);
                noiseOutL *= g; noiseOutR *= g;
                --noiseFadeSamplesRemaining;
            }
        }


        ++sampleTickCounter;

        // ---- Layer-aware stereo placement ----
        // - Sample (Layer 1): centred (already L/R).
        // - Osc B (body): mostly mono, tiny stereo bleed (10-25% width).
        // - Sub: hard mono.
        // - Noise (air): wide (25-40%).
        const float oscBSideGain = 0.18f;   // ~18% side
        const float noiseSideGain = 0.22f;  // air stays present, not splashy
        constexpr float kAirLayerTrim = 0.38f;
        noiseOutL *= kAirLayerTrim;
        noiseOutR *= kAirLayerTrim;
        float l = sampL + subOut + oscBOut + noiseOutL * (1.0f + noiseSideGain);
        float r = sampR + subOut + oscBOut + noiseOutR * (1.0f - noiseSideGain);
        // Push a small portion of Osc B opposite-side for body width.
        l += oscBOut * oscBSideGain;
        r -= oscBOut * oscBSideGain;

        // ---- Filter (with card cutoff offset) ----
        const float fEnv = filterEnv.getNextSample();
        const float keyOffset = (currentMidiNote - 60.0f) * filterKeyTrack * 100.0f;
        float modCut = baseCutoff * std::pow(2.0f, filterEnvAmount * fEnv * 4.0f)
                       + keyOffset + cardCutoffHz;
        modCut = juce::jlimit(20.0f, 20000.0f, modCut);
        filter.setCutoff(modCut);

        const float mono = 0.5f * (l + r);
        const float filtered = filter.processSample(mono);
        l = 0.5f * (l + filtered);
        r = 0.5f * (r + filtered);

        // ---- Amp envelope + per-voice VCA card offset + pan offset ----
        const float amp = ampEnv.getNextSample();
        const float velCurve = 0.3f + 0.7f * velocity;
        const float gain = amp * velCurve * vcaGainLin;
        l *= gain; r *= gain;
        // Equal-power pan offset using card pan (-0.08..+0.08 at full vintage).
        // cardPan=0 → both sides ~0.707 (centre). Multiply by sqrt(2) to keep
        // unity gain in the centre position.
        const float panAngle = (cardPan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        const float panL = std::cos(panAngle) * 1.41421356f;
        const float panR = std::sin(panAngle) * 1.41421356f;
        l *= panL;
        r *= panR;


        // Premium tone stages: harmonic exciter + Haas/M-S stereo spread.
        // Both are no-ops at amount 0, so CPU cost stays near zero for
        // categories that don't request them (e.g. pianos, 808s).
        exciter.process(l, r);
        spreader.process(l, r);

        // Advance slow analog drift phase.
        driftPhase += driftInc;
        if (driftPhase >= 1.0) driftPhase -= 1.0;

        // Per-voice output trim (-6 dB). This is the single biggest contributor
        // to clean chord/poly playback: each voice contributes half the gain
        // so 4-8 simultaneous notes stay well below 0 dBFS before the FX
        // chain's master gain + limiter.
        constexpr float kVoiceTrim = 0.5f;
        l *= kVoiceTrim; r *= kVoiceTrim;

        // ---- Per-layer peak metering (cheap abs/max). Throttled log at end. ----
        const float absSamp  = juce::jmax(std::fabs(sampL),  std::fabs(sampR));
        const float absOscB  = std::fabs(oscBOut);
        const float absSub   = std::fabs(subOut);
        const float absNoise = juce::jmax(std::fabs(noiseOutL), std::fabs(noiseOutR));
        const float absOut   = juce::jmax(std::fabs(l), std::fabs(r));
        if (absSamp  > peakSamp)  peakSamp  = absSamp;
        if (absOscB  > peakOscB)  peakOscB  = absOscB;
        if (absSub   > peakSub)   peakSub   = absSub;
        if (absNoise > peakNoise) peakNoise = absNoise;
        if (absOut   > peakOut)   peakOut   = absOut;
        // Note-scoped main-sample peak for the preset-quality reporter (held
        // until the next noteOn instead of the per-second meter reset).
        if (absSamp > mainSamplePeakLin_.load())
            mainSamplePeakLin_.store(absSamp);
        if (++meterFrameCounter >= (int) sampleRate)
        {
            auto db = [](float v) { return 20.0f * std::log10(juce::jmax(1.0e-9f, v)); };
            if (peakOut > 0.001f) // skip idle voices
            {
                juce::Logger::writeToLog(juce::String("[DIDITAGAIN voice peaks] samp=")
                    + juce::String(db(peakSamp), 1)
                    + " oscB=" + juce::String(db(peakOscB), 1)
                    + " sub="  + juce::String(db(peakSub), 1)
                    + " noise="+ juce::String(db(peakNoise), 1)
                    + " out="  + juce::String(db(peakOut), 1) + " dBFS");
            }
            peakSamp = peakOscB = peakSub = peakNoise = peakOut = 0.0f;
            meterFrameCounter = 0;
        }

        const float fxSendGain = nextFxSendGain();
        if (auto* fxSendBuffer = currentFxSendRenderBuffer)
        {
            if (fxSendGain > 0.0f && s < fxSendBuffer->getNumSamples())
            {
                if (fxSendBuffer->getNumChannels() >= 2)
                {
                    fxSendBuffer->addSample(0, s, l * fxSendGain);
                    fxSendBuffer->addSample(1, s, r * fxSendGain);
                }
                else if (fxSendBuffer->getNumChannels() > 0)
                {
                    fxSendBuffer->addSample(0, s, 0.5f * (l + r) * fxSendGain);
                }
            }
        }

        if (numCh >= 2) { outputBuffer.addSample(0, s, l); outputBuffer.addSample(1, s, r); }
        else            { outputBuffer.addSample(0, s, 0.5f * (l + r)); }


        const bool sampleSourceDone = (loZone == nullptr || loFinished)
                                   && (hiZone == nullptr || hiFinished);
        // End the voice only when amp env is idle. If a sample finishes but
        // synth layers are still active, keep rendering until release.
        const bool synthLayersSilent = (oscBLevel + subLevel + noiseLevel) < 0.0001f
                                     && ! partialsActive;
        if (! ampEnv.isActive()
            || (sampleSourceDone && multisample && hasSampleSource && synthLayersSilent))
        {
            clearCurrentNote(); reset(); break;
        }
    }
}
