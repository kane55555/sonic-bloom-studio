#include "Voice.h"
#include "VoiceCard.h"


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

void SynthVoice::prepare(double sr, int)
{
    sampleRate = sr;
    filter.prepare(sr);
    ampEnv.prepare(sr);
    filterEnv.prepare(sr);
    modEnv.prepare(sr);

    // Per-layer carving filters — frequency lanes so layers stop clashing.
    noiseHpL.prepare(sr); noiseHpR.prepare(sr);
    noiseHpL.setMode(OnePoleCarver::Mode::HighPass); noiseHpL.setCutoff(2200.0f);
    noiseHpR.setMode(OnePoleCarver::Mode::HighPass); noiseHpR.setCutoff(2200.0f);
    subLp.prepare(sr);  subLp.setMode (OnePoleCarver::Mode::LowPass);  subLp.setCutoff(260.0f);
    oscBHp.prepare(sr); oscBHp.setMode(OnePoleCarver::Mode::HighPass); oscBHp.setCutoff(110.0f);

    unison.prepare(sr);
    unison.setConfig(unisonRenderVoices, unisonRenderDetune, unisonRenderSpread, unisonRenderDrift);
    exciter.prepare(sr);
    spreader.prepare(sr);

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
    oscBPhase = subPhase = fmModPhase = sineFallbackPhase = 0.0;
    pinkB0 = pinkB1 = pinkB2 = 0.0f;
    filter.reset();
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

    noiseHpL.reset(); noiseHpR.reset();
    subLp.reset(); oscBHp.reset();
    // Per-note phase scramble for the unison stack — anti-machine-gun.
    unison.randomizePhasesAndDrift();


    ampEnv.noteOn();
    filterEnv.noteOn();
    modEnv.noteOn();
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    ampEnv.noteOff();
    filterEnv.noteOff();
    modEnv.noteOff();
    if (! allowTailOff) { clearCurrentNote(); reset(); }
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

float SynthVoice::renderOscShape(Oscillator::Waveform w, float p, float pw) const noexcept
{
    using W = Oscillator::Waveform;
    const float twoPi = juce::MathConstants<float>::twoPi;
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
            sampL += zl * w; sampR += zr * w;
            loReadPos += loStep;
            advanceLoop(*loZone, loReadPos, loFinished);
        }
        if (hiZone && ! hiFinished)
        {
            float zl, zr; readWithLoop(*hiZone, hiReadPos, hiFinished, zl, zr);
            sampL += zl * zoneXfade; sampR += zr * zoneXfade;
            hiReadPos += hiStep;
            advanceLoop(*hiZone, hiReadPos, hiFinished);
        }

        // Legacy synth fallback only for factory/pure-synth presets. Imported
        // hybrid presets disable this so a missing sample cannot masquerade as
        // the same cheap synth sound.
        if (! hasSampleSource && fallbackSynthesisEnabled)
        {
            // Card pitch + slow drift add 0..few cents of vintage life.
            const double totalCents = (double) oscADetuneCents + (double) extraCentsNow();
            const double f = midiToHzD((double) currentMidiNote + (double) pitchOffsetSemis
                                       + totalCents / 100.0);
            if (unisonRenderVoices > 1)
            {
                // True unison stack — supersaw-style. Uses Osc A waveform when saw/square/triangle.
                auto shape = dida::UnisonEngine::Shape::Saw;
                if (oscAWave == Oscillator::Waveform::Square || oscAWave == Oscillator::Waveform::Pulse)
                    shape = dida::UnisonEngine::Shape::Square;
                else if (oscAWave == Oscillator::Waveform::Triangle)
                    shape = dida::UnisonEngine::Shape::Triangle;
                float ul = 0.0f, ur = 0.0f;
                unison.renderSample((float) f, shape, ul, ur);
                sampL += ul * 0.5f; sampR += ur * 0.5f;
            }
            else
            {
                sineFallbackPhase += f / sampleRate;
                if (sineFallbackPhase > 1.0) sineFallbackPhase -= 1.0;
                const float v = renderOscShape(oscAWave, (float) sineFallbackPhase, oscAPulseWidth) * 0.5f;
                sampL += v; sampR += v;
            }
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

            oscBPhase += bHz / sampleRate;
            if (oscBPhase > 1.0) oscBPhase -= 1.0;
            const float ph = (float) oscBPhase + fmOffset;
            oscBOut = renderOscShape(oscBWave, ph - std::floor(ph), oscBPulseWidth) * oscBLevel;
            // Gentle HP carve so Osc B body does not muddy the sample low end.
            oscBOut = oscBHp.process(oscBOut);
        }

        // ---- Layer 3: Sub (one octave below current note, sine) ----
        float subOut = 0.0f;
        if (subLevel > 0.0001f && sampleTickCounter >= subStartOffsetSamples)
        {
            const double subHz = midiToHzD((double) currentMidiNote - 12.0);
            subPhase += subHz / sampleRate;
            if (subPhase > 1.0) subPhase -= 1.0;
            subOut = std::sin((float) subPhase * juce::MathConstants<float>::twoPi) * subLevel;
            // Sub stays in its lane (<~260 Hz) — keeps the low end mono-tight.
            subOut = subLp.process(subOut);
        }

        // ---- Layer 4: Noise / Air (stereo decorrelated, HP-carved) ----
        float noiseOutL = 0.0f, noiseOutR = 0.0f;
        if (noiseLevel > 0.0001f && sampleTickCounter >= noiseStartOffsetSamples)
        {
            // Decorrelate L/R so air sits wide; HP at ~2.2k keeps it above
            // the body of brass / guitars / pianos.
            noiseOutL = noiseHpL.process(nextNoiseSample()) * noiseLevel;
            noiseOutR = noiseHpR.process(nextNoiseSample()) * noiseLevel;
        }

        ++sampleTickCounter;

        // ---- Layer-aware stereo placement ----
        // - Sample (Layer 1): centred (already L/R).
        // - Osc B (body): mostly mono, tiny stereo bleed (10-25% width).
        // - Sub: hard mono.
        // - Noise (air): wide (25-40%).
        const float oscBSideGain = 0.18f;   // ~18% side
        const float noiseSideGain = 0.35f;  // ~35% side
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

        if (numCh >= 2) { outputBuffer.addSample(0, s, l); outputBuffer.addSample(1, s, r); }
        else            { outputBuffer.addSample(0, s, 0.5f * (l + r)); }


        const bool sampleSourceDone = (loZone == nullptr || loFinished)
                                   && (hiZone == nullptr || hiFinished);
        // End the voice only when amp env is idle. If a sample finishes but
        // synth layers are still active, keep rendering until release.
        const bool synthLayersSilent = (oscBLevel + subLevel + noiseLevel) < 0.0001f;
        if (! ampEnv.isActive()
            || (sampleSourceDone && multisample && hasSampleSource && synthLayersSilent))
        {
            clearCurrentNote(); reset(); break;
        }
    }
}
