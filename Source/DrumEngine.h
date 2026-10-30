#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// A user-loaded sound, already mixed down to mono.
struct Sample
{
    juce::AudioBuffer<float> data;
    double sampleRate = 44100.0;
    juce::String name;   // shown in the UI
    juce::String path;   // full path, saved with patterns
};

// All sound generation, step sequencing and effects. Runs on the audio thread;
// the UI talks to it only through the atomics below, so there are no locks.
//
// Signal flow, per sound:   synth/sample -> volume & pitch -> distortion -> stutter
// then on the buses:        compressor (sounds sent to it) + everything else = main
//                           main + reverb return + delay return -> master
class DrumEngine
{
public:
    static constexpr int numVoices = 10;
    static constexpr int maxSteps  = 32;

    enum Fx { fxReverb, fxDelay, fxComp, fxDist, fxStutter, numFx };

    DrumEngine();
    ~DrumEngine();

    void prepare (double sampleRate);
    void render (float* left, float* right, int numSamples);

    // Copy the pattern, settings, sounds and effects of another engine (used for offline export).
    void copySettingsFrom (const DrumEngine& other);

    // ---- shared state (UI writes, audio thread reads) ----
    std::array<std::array<std::atomic<bool>, maxSteps>, numVoices> pattern;
    std::array<std::atomic<bool>, numVoices> muted;
    std::array<std::atomic<bool>, numVoices> previewRequest;  // play once, never recorded
    std::array<std::atomic<float>, numVoices> liveHit;        // velocity 0..1 (0 = none); recorded when REC is on
    std::array<std::atomic<const Sample*>, numVoices> samples; // nullptr = built-in synth sound.
                                                               // The UI keeps every Sample alive, the
                                                               // audio thread never frees anything.
    std::array<std::atomic<float>, numVoices> voiceVolume;     // gain, 0 .. 1.5
    std::array<std::atomic<float>, numVoices> voicePitch;      // semitones, -12 .. +12

    std::atomic<int>   numSteps  { 16 };
    std::atomic<float> tempo     { 104.0f };  // quarter-note BPM
    std::atomic<float> swing     { 0.12f };   // 0 .. 0.6
    std::atomic<float> volume    { 0.8f };    // 0 .. 1
    std::atomic<bool>  playing   { false };
    std::atomic<bool>  recording { false };

    // ---- effects ----
    std::array<std::atomic<bool>, numFx> fxOn;                               // effect switched on (stutter: repeating)
    std::array<std::array<std::atomic<bool>, numVoices>, numFx> fxSend;      // which sounds go through each effect

    std::atomic<float> reverbAmount  { 0.35f }, reverbSize   { 0.55f };
    std::atomic<float> delayAmount   { 0.30f }, delayFeedback { 0.40f };
    std::atomic<int>   delayTime     { 3 };    // index into delay times (default 1/8 dotted)
    std::atomic<float> compAmount    { 0.45f }, compMix      { 1.0f };
    std::atomic<float> distDrive     { 0.40f }, distTone     { 0.65f };
    std::atomic<int>   stutterRate   { 3 };    // index into stutter rates (default 1/16)

    // ---- audio thread writes, UI reads ----
    std::atomic<int> currentStep { -1 };
    std::atomic<uint32_t> patternVersion { 0 };   // bumped when a live hit is recorded

    static const char* voiceName (int index);
    static const char* voiceId (int index);       // short stable id used in pattern files
    static char        voiceKey (int index);      // computer-keyboard key, lowercase
    static int         voiceMidiNote (int index); // General MIDI drum note used for export
    static int         voiceForMidiNote (int note); // incoming MIDI note -> voice, or -1

    static const char* fxName (int fx);           // "REVERB"
    static const char* fxShort (int fx);          // "RV", used as column header
    static const char* fxId (int fx);             // id used in pattern files

    static constexpr int numDelayTimes = 6;
    static const char* delayTimeName (int index);
    static constexpr int numStutterRates = 6;
    static const char* stutterRateName (int index);

    // Swing as the fraction of a 16th that odd steps are delayed by.
    static double swingDelay (float swing) { return juce::jlimit (0.0, 0.6, (double) swing) * 0.66; }

    struct Voice;   // defined in the .cpp

private:
    std::array<std::unique_ptr<Voice>, numVoices> voices;
    std::array<int, numVoices> skipOnce;      // step to skip once after a recorded early hit
    double sr = 44100.0;
    bool wasPlaying = false;
    int step = 0;
    double samplesToNextStep = 0.0;
    double lastStepLength = 0.0;
    juce::SmoothedValue<float> gain;

    // distortion: per-sound tone filter state
    std::array<float, numVoices> distLp {};

    // stutter: every sound records into its own slice so only the chosen ones repeat
    std::array<std::vector<float>, numVoices> stutterBuf;
    int stutterLen = 0, stutterPos = 0;
    bool stutterActive = false, stutterFilled = false, stutterPending = false;

    // compressor
    float compEnv = 0.0f, compGain = 1.0f;
    std::vector<float> compDelay;   // 1 ms look-ahead
    int compDelayPos = 0;

    // reverb + delay
    juce::Reverb reverb;
    float lastRevSize = -1.0f;
    std::vector<float> delayL, delayR;
    int delayWrite = 0;
    juce::SmoothedValue<float> delaySamples;
    float delayLpL = 0.0f, delayLpR = 0.0f;
    bool delayWasOn = false, delayDirty = false;

    // per-block scratch buffers
    std::vector<float> mainBuf, revL, revR, dlySend;

    void fireStep();
    void triggerVoice (int v, float velocity = 1.0f);
    void recordHit (int v);
    void startStutter();
    double stepSamples() const;   // length of a 16th without swing
    void renderBlock (float* left, float* right, int numSamples, bool isPlaying);
};
