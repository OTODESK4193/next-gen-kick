#pragma once
#include <JuceHeader.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <atomic>

// --- Preset Structure ---
struct PresetData {
    juce::String name;
    // Attack
    int atkWave; float atkLevel, atkDecay, atkCurve, atkTone, atkHPF, atkPan, atkPitch, atkPW;
    // Body
    int bodyWave; float bodyLevel, pStart, pEnd, pDecay, pCurve, pGlide, bDecay, bCurve, bRatio, bFilter, bPan;
    // Sub
    bool subTrack; float subNote, subFine, subLevel, subDecay, subCurve, subPhase, subAntiClick, subPan;
    // Master
    int satType; bool hqMode; float mDrive, mOut, mWidth, mRelease, mPhase, limThresh, limLook;
};

class NextGenKickAudioProcessor : public juce::AudioProcessor
{
public:
    NextGenKickAudioProcessor();
    ~NextGenKickAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- 視覚化用バッファ ---
    static constexpr int visualBufferSize = 1024;
    std::vector<float> visualBuffer;
    juce::AbstractFifo visualFifo{ visualBufferSize };

    static constexpr int fullWaveSize = 22050;
    std::vector<float> fullWaveAtk;
    std::vector<float> fullWaveBody;
    std::vector<float> fullWaveSub;
    std::atomic<int> fullWaveWriteIdx{ 0 };
    std::atomic<bool> isRecordingFullWave{ false };

    // GUI参照用 (Public)
    int lastMidiNote = 29;

    // --- Preset System ---
    std::vector<PresetData> presetList;
    void loadPreset(int index);

private:
    float currentSampleRate = 44100.0f;
    std::atomic<bool> isNoteActive{ false };
    double noteOnTime = 0.0;

    double phaseAtk = 0.0;
    double phaseBody = 0.0;
    double phaseSub = 0.0;

    juce::Random random;
    int crCounter = 0;

    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, lastBrown = 0;
    float spareNoise = 0.0f;
    bool hasSpareNoise = false;

    // --- TPT Filters ---
    juce::dsp::StateVariableTPTFilter<float> filterAtkHP;
    juce::dsp::StateVariableTPTFilter<float> filterAtkLP;
    juce::dsp::StateVariableTPTFilter<float> filterBodyLP;

    // --- Oversampling & Saturation Buffer ---
    juce::dsp::Oversampling<float> oversampler{ 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    juce::AudioBuffer<float> satBuffer;

    float tapeHysteresisL = 0.0f;
    float tapeHysteresisR = 0.0f;

    int subAttackCounter = 0;
    float lastMixL = 0.0f, lastMixR = 0.0f;
    float dcLastInL = 0, dcLastOutL = 0, dcLastInR = 0, dcLastOutR = 0;

    // --- Limiter ---
    static constexpr int limBufferSize = 4096;
    static constexpr int limMask = limBufferSize - 1;
    std::vector<float> limBufferL;
    std::vector<float> limBufferR;
    int limWriteIdx = 0;
    float cachedPeak = 0.0f;
    int cachedPeakIdx = -1;

    // --- Smoothed Parameters ---
    juce::LinearSmoothedValue<float> s_atkDecay, s_atkCurve, s_atkTone, s_atkLevel, s_atkPan, s_atkPitch, s_atkHPF, s_atkPW;
    juce::LinearSmoothedValue<float> s_pStart, s_pEnd, s_pDecay, s_pGlide, s_pCurve, s_bodyDecay, s_bodyCurve, s_bodyLevel, s_bodyPan, s_besselRatio, s_bodyFilter;
    juce::LinearSmoothedValue<float> s_subNote, s_subFine, s_subDecay, s_subCurve, s_subLevel, s_subPhase, s_subAntiClick, s_subPan;
    juce::LinearSmoothedValue<float> s_masterDrive, s_masterOut, s_masterWidth, s_masterRelease, s_masterPhase, s_limThreshold;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void initPresets();

    inline double generateUltraPureSine(double phase) noexcept;
    inline double polyBlep(double t, double dt) noexcept;
    float getPinkNoise() noexcept;

    float processSaturationSample(float x, int type, float drive, float& tapeState);

    void updateParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NextGenKickAudioProcessor)
};