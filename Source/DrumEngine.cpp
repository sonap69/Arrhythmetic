#include "DrumEngine.h"
#include <cmath>

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // Cheap white noise (xorshift), fine for drums and allocation-free.
    struct Noise
    {
        uint32_t s = 0x9e3779b9u;
        float next() noexcept
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) (int32_t) s * (1.0f / 2147483648.0f);
        }
    };

    // Zero-delay-feedback state variable filter (Simper / Cytomic form).
    struct SVF
    {
        float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;

        void set (double sr, float freq, float q) noexcept
        {
            const float g = std::tan (juce::MathConstants<float>::pi * juce::jmin (freq, (float) sr * 0.45f) / (float) sr);
            k  = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() noexcept { ic1 = ic2 = 0; }

        void process (float x, float& lp, float& bp, float& hp) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
        float bandpass (float x) noexcept { float l, b, h; process (x, l, b, h); return b; }
        float bandpassNorm (float x) noexcept { float l, b, h; process (x, l, b, h); return b * k; }   // unity gain at centre
        float lowpass (float x) noexcept { float l, b, h; process (x, l, b, h); return l; }
        float highpass (float x) noexcept { float l, b, h; process (x, l, b, h); return h; }
    };

    // 2 ms linear attack, then exponential decay reaching -80 dB at `decay` seconds.
    inline float env (float t, float decay) noexcept
    {
        constexpr float attack = 0.002f;
        if (t < attack) return t / attack;
        return std::exp (-(t - attack) * 9.21f / decay);
    }

    // Exponential pitch glide from f0 towards f1.
    inline float glide (float t, float f0, float f1, float time) noexcept
    {
        return f1 + (f0 - f1) * std::exp (-t * 3.0f / time);
    }

    inline float square (float phase) noexcept    { return phase < 0.5f ? 1.0f : -1.0f; }
    inline float triangle (float phase) noexcept  { return 4.0f * std::abs (phase - 0.5f) - 1.0f; }
    inline void  advance (float& phase, float freq, float invSr) noexcept { phase += freq * invSr; phase -= std::floor (phase); }

    // The 808 metal circuit: six square oscillators at inharmonic ratios.
    constexpr float metalFreqs[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
}

//==============================================================================
struct DrumEngine::Voice
{
    virtual ~Voice() = default;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        chokeCoef = std::exp (-1.0f / (0.01f * (float) sampleRate));   // ~10 ms fade when choked
        tiltCoef  = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 1500.0f / (float) sampleRate);
        setRate (1.0f);
    }

    // Pitch works like changing the playback speed: a synth voice runs its oscillators,
    // envelopes and filters as if the sample rate were sr / rate, a sample is read faster.
    void setRate (float rate)
    {
        currentRate = rate;
        fsr = sr / rate;
        invSr = (float) (rate / sr);
        setup();
    }

    void trigger (const Sample* s, float velocity, float rate)
    {
        if (std::abs (rate - currentRate) > 1.0e-4f)
            setRate (rate);

        // Velocity: normal (0.8, MIDI ~100) plays at unity. Loudness follows a 1.5 power
        // curve (half velocity = about -9 dB), and soft hits also lose some top end,
        // the way a drum struck gently sounds darker, not just quieter.
        const float v = juce::jlimit (0.05f, 1.0f, velocity) / DrumEngine::normalVelocity;
        velGain  = std::pow (v, 1.5f);
        highGain = juce::jmin (1.0f, std::pow (v, 0.7f));
        tiltLp   = 0.0f;
        sample = s;
        samplePos = 0.0;
        sampleStep = s != nullptr ? s->sampleRate / sr * rate : 0.0;
        t = 0.0f;
        chokeGain = 1.0f;
        choking = false;
        active = true;
        if (s == nullptr) start();
    }

    void choke() noexcept { if (active) choking = true; }

    float next() noexcept
    {
        if (! active) return 0.0f;

        float out;
        if (sample != nullptr)
        {
            const int n = sample->data.getNumSamples();
            const int i = (int) samplePos;
            if (i + 1 >= n) { active = false; return 0.0f; }
            const float frac = (float) (samplePos - i);
            const float* d = sample->data.getReadPointer (0);
            out = d[i] + (d[i + 1] - d[i]) * frac;     // linear interpolation handles any file sample rate
            samplePos += sampleStep;
        }
        else
        {
            out = generate();
            t += invSr;
            if (t > length) active = false;
        }

        if (highGain < 0.999f)   // tilt: keep the body, turn the top end down
        {
            tiltLp += tiltCoef * (out - tiltLp);
            out = tiltLp + (out - tiltLp) * highGain;
        }
        out *= velGain;

        if (choking)
        {
            chokeGain *= chokeCoef;
            out *= chokeGain;
            if (chokeGain < 1.0e-4f) active = false;
        }
        return out;
    }

protected:
    virtual void setup() {}
    virtual void start() {}
    virtual float generate() noexcept = 0;

    double sr = 44100.0;
    double fsr = 44100.0;         // sample rate the filters are tuned for (sr / pitch rate)
    float currentRate = 1.0f;
    float invSr = 1.0f / 44100.0f;
    float t = 0.0f;
    float length = 1.0f;          // seconds until a synth voice is switched off
    bool active = false;
    bool choking = false;
    float chokeGain = 1.0f;
    float chokeCoef = 0.995f;
    float velGain = 1.0f, highGain = 1.0f, tiltLp = 0.0f, tiltCoef = 0.2f;
    Noise noise;

    const Sample* sample = nullptr;
    double samplePos = 0.0, sampleStep = 1.0;
};

namespace
{
    using Voice = DrumEngine::Voice;

    struct Kick : Voice
    {
        float phase = 0;
        Kick() { length = 0.6f; }
        void start() override { phase = 0; }
        float generate() noexcept override
        {
            advance (phase, glide (t, 150.0f, 42.0f, 0.09f), invSr);
            float body  = std::sin (twoPi * phase) * env (t, 0.55f);
            float click = t < 0.004f ? noise.next() * (1.0f - t / 0.004f) * 0.25f : 0.0f;
            return body + click;
        }
    };

    struct Snare : Voice
    {
        float phase = 0; SVF hp;
        Snare() { length = 0.25f; }
        void setup() override { hp.set (fsr, 1400.0f, 0.707f); }
        void start() override { phase = 0; hp.reset(); }
        float generate() noexcept override
        {
            advance (phase, glide (t, 220.0f, 170.0f, 0.05f), invSr);
            float tone  = triangle (phase) * env (t, 0.12f) * 0.5f;
            float snare = hp.highpass (noise.next()) * env (t, 0.2f) * 0.6f;
            return tone + snare;
        }
    };

    struct Clap : Voice
    {
        SVF bp;
        Clap() { length = 100.0f; }   // ends itself, in real time (below)
        void setup() override { bp.set (fsr, 1300.0f, 1.2f); }
        void start() override { bp.reset(); }
        float generate() noexcept override
        {
            // Three quick bursts, then a longer tail: what makes a clap sound like several
            // hands. The bursts are timed in real seconds, not in pitched time: slowed down
            // with the pitch they drift apart and are heard as separate claps. Pitch moves
            // only the filter (tuned via fsr), so a low clap sounds darker, still one clap.
            const float rt = t / currentRate;
            if (rt > 0.3f) { active = false; return 0.0f; }

            float e = 0.0f;
            for (float o : { 0.0f, 0.011f, 0.022f })
                if (rt >= o && rt < o + 0.011f)
                    e = juce::jmax (e, 0.9f * std::exp (-(rt - o) / 0.005f));
            if (rt >= 0.033f)
                e = juce::jmax (e, 0.7f * std::exp (-(rt - 0.033f) * 9.21f / 0.22f));
            return bp.bandpass (noise.next()) * e * 1.6f;
        }
    };

    struct Rim : Voice
    {
        float p1 = 0, p2 = 0; SVF hp;
        Rim() { length = 0.06f; }
        void setup() override { hp.set (fsr, 500.0f, 0.707f); }
        void start() override { p1 = p2 = 0; hp.reset(); }
        float generate() noexcept override
        {
            advance (p1, 1700.0f, invSr);
            advance (p2, 480.0f, invSr);
            float s = square (p1) * env (t, 0.03f) * 0.3f + triangle (p2) * env (t, 0.04f) * 0.5f;
            return hp.highpass (s);
        }
    };

    struct Tom : Voice
    {
        float phase = 0, f0, f1, dec, glideTime;
        Tom (float startHz, float endHz, float decay, float g) : f0 (startHz), f1 (endHz), dec (decay), glideTime (g) { length = decay + 0.05f; }
        void start() override { phase = 0; }
        float generate() noexcept override
        {
            advance (phase, glide (t, f0, f1, glideTime), invSr);
            return std::sin (twoPi * phase) * env (t, dec) * 0.8f;
        }
    };

    // Hi-hat: metal oscillators, band-passed then high-passed.
    struct Hat : Voice
    {
        std::array<float, 6> phases {};
        SVF bp, hp, noiseHp;
        float dec, level, noiseMix, bpHz, bpQ;
        Hat (float decay, float lvl, float noiseAmount, float bandHz, float bandQ)
            : dec (decay), level (lvl), noiseMix (noiseAmount), bpHz (bandHz), bpQ (bandQ) { length = decay + 0.05f; }
        void setup() override
        {
            bp.set (fsr, bpHz, bpQ);
            hp.set (fsr, 7000.0f, 0.707f);
            noiseHp.set (fsr, 7500.0f, 0.707f);
        }
        void start() override { phases = {}; bp.reset(); hp.reset(); noiseHp.reset(); }
        float generate() noexcept override
        {
            float s = 0.0f;
            for (size_t i = 0; i < 6; ++i)
            {
                advance (phases[i], metalFreqs[i] * 1.4f, invSr);
                s += square (phases[i]);
            }
            s = hp.highpass (bp.bandpass (s * (1.0f / 6.0f))) * 11.0f;   // filters remove most of the energy, so make it up here
            const float sizzle = noiseMix > 0.0f ? noiseHp.highpass (noise.next()) * noiseMix : 0.0f;
            return (s + sizzle) * env (t, dec) * level;
        }
    };

    // Cymbal: noise ringing through a bank of narrow resonators at inharmonic
    // frequencies, a ring-modulated "shimmer" and a wash of filtered noise.
    // Everything sits above ~2.5 kHz and no partial dominates, so it reads as
    // metal rather than as a pitched bell or cowbell.
    struct Cymbal : Voice
    {
        struct Params
        {
            float decay;      // seconds to -80 dB
            float burst;      // noise splash at the hit
            float wash;       // sustained noise
            float modes;      // resonator bank
            float shimmer;    // ring-modulated metal, sustained
            float ping;       // ring-modulated metal, short (the "tick" of a ride)
            float pingDecay;
            float lowCut, highCut;
            float modeScale;  // shifts the whole resonator bank
            float level;
            float modeQ = 18.0f;      // resonator sharpness: higher = more defined, less noisy
            float washDecay = 1.0f;   // wash and resonator sustain, as a fraction of decay
            float strike = 0.0f;      // extra resonator excitation right at the hit (the stick)
        };

        static constexpr int numModes = 14;
        static constexpr float modeFreqs[numModes] = { 3150, 3613, 4102, 4597, 5160, 5703, 6268,
                                                       6911, 7536, 8270, 9049, 9890, 10820, 11700 };
        std::array<SVF, numModes> modes;
        std::array<float, 6> phases {};
        SVF washHp, shimmerHp, outHp, outLp;
        Params p;

        explicit Cymbal (Params params) : p (params) { length = p.decay + 0.05f; }

        void setup() override
        {
            for (size_t i = 0; i < numModes; ++i)
                modes[i].set (fsr, modeFreqs[i] * p.modeScale, p.modeQ);
            washHp.set (fsr, 6000.0f, 0.707f);
            shimmerHp.set (fsr, 4500.0f, 0.707f);
            outHp.set (fsr, p.lowCut, 0.707f);
            outLp.set (fsr, p.highCut, 0.707f);
        }
        void start() override
        {
            phases = {};
            for (auto& m : modes) m.reset();
            washHp.reset(); shimmerHp.reset(); outHp.reset(); outLp.reset();
        }
        float generate() noexcept override
        {
            const float n = noise.next();

            const float body  = env (t, p.decay);
            const float sus   = env (t, p.decay * p.washDecay);
            const float burst = std::exp (-t / 0.04f);
            const float ping  = std::exp (-t / p.pingDecay);
            const float hit   = std::exp (-t / 0.006f);

            const float excite = n * (p.modes * sus + p.strike * hit);
            float modal = 0.0f;
            for (auto& m : modes)
                modal += m.bandpassNorm (excite);

            // three ring-modulated pairs of squares: sums and differences of all their
            // harmonics give a dense, clangy spectrum instead of a few clear pitches
            std::array<float, 6> sq;
            for (size_t i = 0; i < 6; ++i)
            {
                advance (phases[i], metalFreqs[i] * 3.1f, invSr);
                sq[i] = square (phases[i]);
            }
            const float ring = (sq[0] * sq[1] + sq[2] * sq[3] + sq[4] * sq[5]) * (1.0f / 3.0f);
            const float shimmer = shimmerHp.highpass (ring);
            const float wash = washHp.highpass (n);

            float x = wash * (p.wash * sus + p.burst * burst)
                    + modal
                    + shimmer * (p.shimmer * body + p.ping * ping);

            return outLp.lowpass (outHp.highpass (x)) * p.level;
        }
    };

    const char* names[DrumEngine::numVoices] =
        { "BASS DRUM", "SNARE", "HANDCLAP", "RIMSHOT", "LOW TOM", "HI TOM", "CLOSED HAT", "OPEN HAT", "CRASH", "RIDE" };

    // b = bass drum, s = snare, c = clap, r = rim, l = low tom, t = (hi) tom,
    // h = closed hat, o = open hat, x = crash (cymbals are written as x in drum notation), d = ride
    const char keys[DrumEngine::numVoices] = { 'b', 's', 'c', 'r', 'l', 't', 'h', 'o', 'x', 'd' };

    const char* ids[DrumEngine::numVoices] =
        { "kick", "snare", "clap", "rim", "lowtom", "hitom", "closedhat", "openhat", "crash", "ride" };

    // General MIDI drum notes, used for MIDI export
    const int midiNotes[DrumEngine::numVoices] = { 36, 38, 39, 37, 45, 48, 42, 46, 49, 51 };

    constexpr int closedHat = 6, openHat = 7;
}

//==============================================================================
namespace
{
    const char* fxNames[DrumEngine::numFx]  = { "REVERB", "DELAY", "COMPRESS", "DISTORT", "STUTTER" };
    const char* fxShorts[DrumEngine::numFx] = { "RV", "DL", "CP", "DS", "ST" };
    const char* fxIds[DrumEngine::numFx]    = { "reverb", "delay", "comp", "dist", "stutter" };

    // delay times in quarter notes
    const double delayBeats[DrumEngine::numDelayTimes]     = { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5 };
    const char*  delayNames[DrumEngine::numDelayTimes]     = { "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4." };
    // stutter slice lengths in 16th steps
    const double stutterSteps[DrumEngine::numStutterRates] = { 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5 };
    const char*  stutterNames[DrumEngine::numStutterRates] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };

    constexpr int blockSize = 256;
    constexpr float maxDelaySeconds = 4.6f;     // 1/4 dotted at 20 BPM
    constexpr float maxStutterSeconds = 3.05f;  // 1/4 at 20 BPM
}

DrumEngine::DrumEngine()
{
    voices[0] = std::make_unique<Kick>();
    voices[1] = std::make_unique<Snare>();
    voices[2] = std::make_unique<Clap>();
    voices[3] = std::make_unique<Rim>();
    voices[4] = std::make_unique<Tom> (140.0f, 85.0f, 0.4f, 0.25f);
    voices[5] = std::make_unique<Tom> (230.0f, 150.0f, 0.32f, 0.2f);
    //                               decay  level  noise  band-pass Hz  Q
    voices[6] = std::make_unique<Hat> (0.05f, 0.35f, 0.0f,  10000.0f, 0.9f);   // closed
    voices[7] = std::make_unique<Hat> (0.9f, 0.30f, 0.8f, 8000.0f, 0.7f);   // open
    //                                       decay burst wash modes shim ping  pDec   lowCut  highCut  scale level
    voices[8] = std::make_unique<Cymbal> (Cymbal::Params { 2.4f, 0.8f, 0.45f, 2.0f, 0.35f, 0.0f, 0.10f, 3500.0f, 14000.0f, 1.0f, 0.6f }); // crash
    voices[9] = std::make_unique<Cymbal> (Cymbal::Params { 2.4f, 0.3f, 0.06f, 0.9f, 0.25f, 0.5f, 0.12f, 2800.0f, 11000.0f, 0.9f, 0.7f, 25.0f, 0.6f, 3.0f }); // ride

    for (auto& row : pattern)
        for (auto& s : row) s.store (false);
    for (auto& row : velocity)
        for (auto& s : row) s.store (normalVelocity);
    for (auto& m : muted) m.store (false);
    for (auto& p : previewRequest) p.store (false);
    for (auto& h : liveHit) h.store (0.0f);
    for (auto& s : samples) s.store (nullptr);
    for (auto& v : voiceVolume) v.store (1.0f);
    for (auto& p : voicePitch) p.store (0.0f);
    for (auto& f : fxOn) f.store (false);
    skipOnce.fill (-1);

    // Starting routing: the usual suspects for each effect.
    //                                       kick   snare  clap   rim    ltom   htom   chh    ohh    crash  ride
    const bool defaults[numFx][numVoices] = { { false, true,  true,  true,  true,  true,  false, true,  true,  true  },   // reverb
                                              { false, false, true,  true,  false, false, false, false, false, false },   // delay
                                              { true,  true,  true,  true,  true,  true,  true,  true,  true,  true  },   // compressor
                                              { true,  true,  false, false, false, false, false, false, false, false },   // distortion
                                              { true,  true,  true,  true,  true,  true,  true,  true,  true,  true  } }; // stutter
    for (size_t f = 0; f < (size_t) numFx; ++f)
        for (size_t v = 0; v < (size_t) numVoices; ++v)
            fxSend[f][v].store (defaults[f][v]);
}

DrumEngine::~DrumEngine() = default;

const char* DrumEngine::voiceName (int index) { return names[index]; }
char DrumEngine::voiceKey (int index)         { return keys[index]; }
const char* DrumEngine::voiceId (int index)   { return ids[index]; }
int DrumEngine::voiceMidiNote (int index)     { return midiNotes[index]; }
const char* DrumEngine::fxName (int fx)       { return fxNames[fx]; }
const char* DrumEngine::fxShort (int fx)      { return fxShorts[fx]; }
const char* DrumEngine::fxId (int fx)         { return fxIds[fx]; }
const char* DrumEngine::delayTimeName (int i)   { return delayNames[juce::jlimit (0, numDelayTimes - 1, i)]; }
const char* DrumEngine::stutterRateName (int i) { return stutterNames[juce::jlimit (0, numStutterRates - 1, i)]; }

int DrumEngine::voiceForMidiNote (int note)
{
    // General MIDI drum map, with the neighbouring notes most pads and kits also send
    switch (note)
    {
        case 35: case 36:                     return 0;   // bass drum
        case 38: case 40:                     return 1;   // snare
        case 39:                              return 2;   // hand clap
        case 37:                              return 3;   // side stick / rim
        case 41: case 43: case 45:            return 4;   // low / floor toms
        case 47: case 48: case 50:            return 5;   // mid / high toms
        case 42: case 44:                     return 6;   // closed / pedal hi-hat
        case 46:                              return 7;   // open hi-hat
        case 49: case 52: case 55: case 57:   return 8;   // crash, china, splash
        case 51: case 53: case 59:            return 9;   // ride, ride bell
        default:                              return -1;
    }
}

void DrumEngine::copySettingsFrom (const DrumEngine& o)
{
    for (size_t v = 0; v < (size_t) numVoices; ++v)
    {
        for (size_t s = 0; s < (size_t) maxSteps; ++s)
        {
            pattern[v][s].store (o.pattern[v][s].load());
            velocity[v][s].store (o.velocity[v][s].load());
        }
        muted[v].store (o.muted[v].load());
        samples[v].store (o.samples[v].load());
        voiceVolume[v].store (o.voiceVolume[v].load());
        voicePitch[v].store (o.voicePitch[v].load());
    }
    for (size_t f = 0; f < (size_t) numFx; ++f)
    {
        fxOn[f].store (o.fxOn[f].load());
        for (size_t v = 0; v < (size_t) numVoices; ++v)
            fxSend[f][v].store (o.fxSend[f][v].load());
    }
    numSteps.store (o.numSteps.load());
    tempo.store (o.tempo.load());
    swing.store (o.swing.load());
    volume.store (o.volume.load());
    reverbAmount.store (o.reverbAmount.load());   reverbSize.store (o.reverbSize.load());
    delayAmount.store (o.delayAmount.load());     delayFeedback.store (o.delayFeedback.load());
    delayTime.store (o.delayTime.load());
    compAmount.store (o.compAmount.load());       compMix.store (o.compMix.load());
    distDrive.store (o.distDrive.load());         distTone.store (o.distTone.load());
    stutterRate.store (o.stutterRate.load());
}

void DrumEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& v : voices) v->prepare (sampleRate);
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (volume.load());

    for (auto& b : stutterBuf)
        b.assign ((size_t) (sampleRate * maxStutterSeconds), 0.0f);
    stutterActive = stutterFilled = stutterPending = false;

    reverb.setSampleRate (sampleRate);
    reverb.reset();
    lastRevSize = -1.0f;

    delayL.assign ((size_t) (sampleRate * maxDelaySeconds) + 4, 0.0f);
    delayR.assign (delayL.size(), 0.0f);
    delayWrite = 0;
    delaySamples.reset (sampleRate, 0.15);
    delaySamples.setCurrentAndTargetValue ((float) (sampleRate * 0.375));

    mainBuf.assign (blockSize, 0.0f);
    revL.assign (blockSize, 0.0f);
    revR.assign (blockSize, 0.0f);
    dlySend.assign (blockSize, 0.0f);
    distLp.fill (0.0f);
    compEnv = 0.0f;
    compGain = 1.0f;
    compDelay.assign ((size_t) juce::jmax (1, (int) std::round (sampleRate * 0.001)), 0.0f);
    compDelayPos = 0;
}

double DrumEngine::stepSamples() const
{
    return sr * 60.0 / juce::jlimit (20.0, 400.0, (double) tempo.load()) / 4.0;
}

void DrumEngine::triggerVoice (int v, float vel)
{
    if (v == closedHat) voices[openHat]->choke();
    const float rate = std::pow (2.0f, juce::jlimit (-24.0f, 24.0f, voicePitch[(size_t) v].load()) / 12.0f);
    voices[(size_t) v]->trigger (samples[(size_t) v].load(), vel, rate);
}

void DrumEngine::startStutter()
{
    const double len = stepSamples() * stutterSteps[juce::jlimit (0, numStutterRates - 1, stutterRate.load())];
    stutterLen = juce::jlimit (64, (int) stutterBuf[0].size(), (int) std::round (len));
    stutterPos = 0;
    stutterFilled = false;
    stutterActive = true;
    stutterPending = false;
}

void DrumEngine::fireStep()
{
    const int n = juce::jlimit (1, maxSteps, numSteps.load());
    if (step >= n) step = 0;

    // a stutter switched on mid-step starts exactly on the next step, so it stays in time
    if (stutterPending) startStutter();

    for (int v = 0; v < numVoices; ++v)
    {
        if (skipOnce[(size_t) v] == step)
        {
            skipOnce[(size_t) v] = -1;   // this hit was already played live, just before the step
            continue;
        }
        if (pattern[(size_t) v][(size_t) step].load() && ! muted[(size_t) v].load())
            triggerVoice (v, velocity[(size_t) v][(size_t) step].load());
    }

    currentStep.store (step);

    // Swing: even steps get longer, odd steps shorter, so off-beat 16ths land late.
    const double base = stepSamples();
    const double sw   = swingDelay (swing.load());
    lastStepLength = (step % 2 == 0) ? base * (1.0 + sw) : base * (1.0 - sw);
    samplesToNextStep += lastStepLength;

    step = (step + 1) % n;
}

// Write a live hit into the step it is closest to.
void DrumEngine::recordHit (int v, float vel)
{
    const int n = juce::jlimit (1, maxSteps, numSteps.load());
    const int upcoming = step % n;
    const int previous = (step - 1 + n) % n;

    int target;
    if (lastStepLength <= 0.0 || samplesToNextStep <= lastStepLength * 0.5)
    {
        target = upcoming;
        skipOnce[(size_t) v] = upcoming;   // don't play it a second time when that step fires
    }
    else
    {
        target = previous;
    }

    velocity[(size_t) v][(size_t) target].store (vel);
    pattern[(size_t) v][(size_t) target].store (true);
    patternVersion.fetch_add (1);
}

void DrumEngine::render (float* left, float* right, int numSamples)
{
    const bool isPlaying = playing.load();
    if (isPlaying && ! wasPlaying)
    {
        step = 0;
        samplesToNextStep = 0.0;
        lastStepLength = 0.0;
        skipOnce.fill (-1);
    }
    if (! isPlaying && wasPlaying)
        currentStep.store (-1);
    wasPlaying = isPlaying;

    for (int v = 0; v < numVoices; ++v)
    {
        if (previewRequest[(size_t) v].exchange (false))
            triggerVoice (v);

        const float vel = liveHit[(size_t) v].exchange (0.0f);
        if (vel > 0.0f)
        {
            triggerVoice (v, vel);
            if (isPlaying && recording.load())
                recordHit (v, vel);
        }
    }

    // stutter on/off
    const bool wantStutter = fxOn[fxStutter].load();
    if (! wantStutter)
        stutterActive = stutterPending = false;
    else if (! stutterActive && ! stutterPending)
    {
        if (isPlaying) stutterPending = true;   // start on the next step
        else           startStutter();
    }

    gain.setTargetValue (volume.load());

    for (int pos = 0; pos < numSamples; pos += blockSize)
        renderBlock (left + pos, right != nullptr ? right + pos : nullptr,
                     juce::jmin (blockSize, numSamples - pos), isPlaying);
}

void DrumEngine::renderBlock (float* left, float* right, int n, bool isPlaying)
{
    // ---- read the settings once per block ----
    const bool revOn  = fxOn[fxReverb].load(),  dlyOn  = fxOn[fxDelay].load();
    const bool compOn = fxOn[fxComp].load(),    distOn = fxOn[fxDist].load();

    std::array<float, numVoices> vol;
    std::array<bool, numVoices> toRev, toDly, toComp, toDist, toStut;
    for (size_t v = 0; v < (size_t) numVoices; ++v)
    {
        vol[v]    = juce::jlimit (0.0f, 2.0f, voiceVolume[v].load());
        toRev[v]  = revOn  && fxSend[fxReverb][v].load();
        toDly[v]  = dlyOn  && fxSend[fxDelay][v].load();
        toComp[v] = compOn && fxSend[fxComp][v].load();
        toDist[v] = distOn && fxSend[fxDist][v].load();
        toStut[v] = stutterActive && fxSend[fxStutter][v].load();
    }

    // distortion: drive 1x..25x into tanh, normalised so full-scale peaks stay put;
    // tone is a low-pass from 1 kHz to 18 kHz
    const float drive   = 1.0f + 24.0f * std::pow (juce::jlimit (0.0f, 1.0f, distDrive.load()), 2.0f);
    const float driveNorm = 1.0f / std::tanh (drive) / std::pow (drive, 0.4f);   // keeps the loudness roughly level as drive goes up
    const float toneHz  = 1000.0f * std::pow (18.0f, juce::jlimit (0.0f, 1.0f, distTone.load()));
    const float toneCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * toneHz / (float) sr);

    // compressor: one knob sets threshold, ratio and make-up together
    const float ca = juce::jlimit (0.0f, 1.0f, compAmount.load());
    const float threshDb = -6.0f - 24.0f * ca;
    const float ratio    = 2.0f + 8.0f * ca;
    // make-up gain is what a loud hit (-3 dBFS) loses, plus up to 2 dB, so
    // turning it up keeps the loudness and brings the body of each sound forward
    const float makeup   = juce::Decibels::decibelsToGain (juce::jmax (0.0f, -3.0f - threshDb) * (1.0f - 1.0f / ratio) + 2.0f * ca);
    const float cMix     = juce::jlimit (0.0f, 1.0f, compMix.load());
    const float release  = std::exp (-1.0f / (0.100f * (float) sr));
    const float recover  = std::exp (-1.0f / (0.0008f * (float) sr));
    const int lookahead  = (int) compDelay.size();

    // delay time follows the tempo
    const int dTime = juce::jlimit (0, numDelayTimes - 1, delayTime.load());
    const float wantDelay = (float) (stepSamples() * 4.0 * delayBeats[dTime]);
    const float delayTarget = juce::jlimit (1.0f, (float) delayL.size() - 2.0f, wantDelay);
    if (dlyOn && ! delayWasOn) delaySamples.setCurrentAndTargetValue (delayTarget);   // switching on: jump, don't glide
    else                       delaySamples.setTargetValue (delayTarget);              // tempo/time change: glide like tape
    delayWasOn = dlyOn;
    const float fb = juce::jlimit (0.0f, 0.9f, delayFeedback.load());
    const float dWet = juce::jlimit (0.0f, 1.0f, delayAmount.load());
    const float fbCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 4500.0f / (float) sr);  // repeats get darker

    for (int i = 0; i < n; ++i)
    {
        if (isPlaying)
        {
            while (samplesToNextStep <= 0.0)
                fireStep();
            samplesToNextStep -= 1.0;
        }

        // stutter fades its loop edges over 1.5 ms so the repeats don't click
        float stutterGain = 1.0f;
        if (stutterActive && stutterFilled)
        {
            const float fade = 0.0015f * (float) sr;
            stutterGain = juce::jmin (1.0f, (float) stutterPos / fade, (float) (stutterLen - stutterPos) / fade);
        }

        float dry = 0.0f, compIn = 0.0f, rev = 0.0f, dly = 0.0f;
        for (size_t v = 0; v < (size_t) numVoices; ++v)
        {
            float x = voices[v]->next() * vol[v];

            if (toDist[v])
            {
                x = std::tanh (x * drive) * driveNorm;
                distLp[v] += toneCoef * (x - distLp[v]);
                x = distLp[v];
            }

            if (stutterActive && toStut[v])
            {
                auto& buf = stutterBuf[v];
                if (! stutterFilled) buf[(size_t) stutterPos] = x;          // first pass: record and play live
                else                 x = buf[(size_t) stutterPos] * stutterGain;
            }

            (toComp[v] ? compIn : dry) += x;
            if (toRev[v]) rev += x;
            if (toDly[v]) dly += x;
        }

        if (stutterActive && ++stutterPos >= stutterLen)
        {
            stutterPos = 0;
            stutterFilled = true;
        }

        if (compOn)
        {
            // Peak detector with 1 ms look-ahead: the gain is already down when a hit
            // arrives, so transients can't overshoot. The bus is delayed by that 1 ms.
            compEnv = juce::jmax (std::abs (compIn), compEnv * release);
            const float overDb = juce::Decibels::gainToDecibels (compEnv, -100.0f) - threshDb;
            const float target = juce::Decibels::decibelsToGain (overDb > 0.0f ? -overDb * (1.0f - 1.0f / ratio) : 0.0f);
            compGain = target < compGain ? target : recover * compGain + (1.0f - recover) * target;

            const float delayed = compDelay[(size_t) compDelayPos];
            compDelay[(size_t) compDelayPos] = compIn;
            compDelayPos = (compDelayPos + 1) % lookahead;

            dry += delayed + (delayed * compGain * makeup - delayed) * cMix;
        }
        else
        {
            dry += compIn;
        }

        mainBuf[(size_t) i] = dry;
        revL[(size_t) i] = revR[(size_t) i] = rev;
        dlySend[(size_t) i] = dly;
    }

    // ---- reverb (stereo, wet only) ----
    if (revOn)
    {
        const float size = juce::jlimit (0.0f, 1.0f, reverbSize.load());
        if (std::abs (size - lastRevSize) > 1.0e-3f)
        {
            juce::Reverb::Parameters p;
            p.roomSize = 0.3f + 0.67f * size;
            p.damping  = 0.45f - 0.2f * size;
            p.wetLevel = 1.0f;
            p.dryLevel = 0.0f;
            p.width    = 1.0f;
            reverb.setParameters (p);
            lastRevSize = size;
        }
        reverb.processStereo (revL.data(), revR.data(), n);
    }
    const float revWet = revOn ? juce::jlimit (0.0f, 1.0f, reverbAmount.load()) * 0.7f : 0.0f;

    // ---- delay (ping-pong) and output ----
    const int dSize = (int) delayL.size();
    for (int i = 0; i < n; ++i)
    {
        float dL = 0.0f, dR = 0.0f;
        if (dlyOn)
        {
            const float d = delaySamples.getNextValue();
            float readPos = (float) delayWrite - d;
            if (readPos < 0.0f) readPos += (float) dSize;
            const int r0 = (int) readPos;
            const int r1 = (r0 + 1) % dSize;
            const float frac = readPos - (float) r0;
            dL = delayL[(size_t) r0] + (delayL[(size_t) r1] - delayL[(size_t) r0]) * frac;
            dR = delayR[(size_t) r0] + (delayR[(size_t) r1] - delayR[(size_t) r0]) * frac;

            delayLpL += fbCoef * (dL - delayLpL);
            delayLpR += fbCoef * (dR - delayLpR);
            // the dry hit goes into the left line, each repeat crosses to the other side
            delayL[(size_t) delayWrite] = dlySend[(size_t) i] + delayLpR * fb;
            delayR[(size_t) delayWrite] = delayLpL * fb;
            delayWrite = (delayWrite + 1) % dSize;
            delayDirty = true;
        }

        const float m = mainBuf[(size_t) i];
        const float outL = m + revL[(size_t) i] * revWet + dL * dWet;
        const float outR = m + revR[(size_t) i] * revWet + dR * dWet;

        // gentle saturation instead of hard clipping when many voices hit at once
        const float g = gain.getNextValue();
        left[i] = std::tanh (outL * 0.9f) * g;
        if (right != nullptr) right[i] = std::tanh (outR * 0.9f) * g;
    }

    // clear the delay lines when it is switched off, so turning it back on starts clean
    if (! dlyOn && delayDirty)
    {
        delayDirty = false;
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        delayLpL = delayLpR = 0.0f;
    }
    if (! revOn && lastRevSize >= 0.0f)
    {
        reverb.reset();
        lastRevSize = -1.0f;
    }
}
