#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

NextGenKickAudioProcessor::NextGenKickAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, &undoManager, "Parameters", createParameterLayout())
{
    limBufferL.resize(limBufferSize, 0.0f);
    limBufferR.resize(limBufferSize, 0.0f);

    visualBuffer.resize(visualBufferSize, 0.0f);
    fullWaveAtk.resize(fullWaveSize, 0.0f);
    fullWaveBody.resize(fullWaveSize, 0.0f);
    fullWaveSub.resize(fullWaveSize, 0.0f);

    satStates.resize(2);

    initPresets();
}

NextGenKickAudioProcessor::~NextGenKickAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout NextGenKickAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    auto createFloat = [&](juce::String id, juce::String name, float min, float max, float def) {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(id, name, min, max, def));
        };

    juce::StringArray atkWaves{ "White", "Pink", "Brown", "Square", "Saw", "Triangle", "Pulse", "Ultra Sine" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>("atkWave", "Atk Waveform", atkWaves, 0));
    createFloat("atkDecay", "Atk Decay", 0.001f, 0.2f, 0.01f);
    createFloat("atkCurve", "Atk Decay Curve", 0.1f, 10.0f, 2.0f);
    createFloat("atkTone", "Atk Tone (Hi-Cut)", 100.0f, 20000.0f, 20000.0f);
    createFloat("atkLevel", "Atk Level", 0.0f, 1.0f, 0.4f);
    createFloat("atkPan", "Atk Panning", -1.0f, 1.0f, 0.0f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>("atkPitch", "Atk Click Freq", juce::NormalisableRange<float>(100.0f, 15000.0f, 1.0f, 0.25f), 3000.0f));
    createFloat("atkHPF", "Atk HighPass", 20.0f, 2000.0f, 200.0f);
    createFloat("atkPulseWidth", "Atk Pulse Width", 0.01f, 0.99f, 0.5f);

    params.push_back(std::make_unique<juce::AudioParameterChoice>("bodyWave", "Body Waveform", juce::StringArray{ "Ultra Sine", "Bessel", "Saw", "Square", "Triangle" }, 0));
    createFloat("pStart", "Body Pitch Start", 100.0f, 2000.0f, 350.0f);
    createFloat("pEnd", "Body Pitch End", 20.0f, 150.0f, 43.6f);
    createFloat("pDecay", "Body Pitch Decay", 0.01f, 0.5f, 0.07f);
    createFloat("pGlide", "Von Karman Tension", 0.0f, 5.0f, 0.8f);
    createFloat("pCurve", "Body Pitch Curve", 0.1f, 5.0f, 1.0f);
    createFloat("bodyDecay", "Body Amp Decay", 0.05f, 1.5f, 0.35f);
    createFloat("bodyCurve", "Body Amp Curve", 0.1f, 5.0f, 1.0f);
    createFloat("bodyLevel", "Body Level", 0.0f, 1.0f, 0.75f);
    createFloat("bodyPan", "Body Panning", -1.0f, 1.0f, 0.0f);
    createFloat("besselRatio", "Bessel Ratio", 1.0f, 3.0f, 1.593f);
    createFloat("bodyFilter", "Body LowPass", 100.0f, 12000.0f, 5000.0f);

    createFloat("subNote", "Sub Note (MIDI)", 24.0f, 48.0f, 29.0f);
    createFloat("subFine", "Sub Fine Tune (Hz)", -10.0f, 10.0f, 0.0f);
    params.push_back(std::make_unique<juce::AudioParameterBool>("subTrack", "Key Tracking", false));
    createFloat("subDecay", "Sub Amp Decay", 0.10f, 5.0f, 0.25f);
    createFloat("subCurve", "Sub Decay Curve", 3.0f, 10.0f, 4.0f);
    createFloat("subLevel", "Sub Level", 0.0f, 1.0f, 0.65f);
    createFloat("subPhase", "Sub Phase Offset", 0.0f, 360.0f, 0.0f);
    createFloat("subAntiClick", "Sub Anti-Click (ms)", 0.10f, 50.0f, 5.0f);
    createFloat("subPan", "Sub Panning", -1.0f, 1.0f, 0.0f);

    createFloat("masterDrive", "Master Drive", 1.0f, 25.0f, 1.0f);
    juce::StringArray satModes{ "Soft Tanh", "Hard Clip", "Triode", "Tape", "Transformer", "JFET", "BJT", "Wavefold", "Bitcrush", "Exciter", "Cubic" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>("satType", "Saturation Mode", satModes, 0));
    createFloat("masterOut", "Final Volume", 0.0f, 1.0f, 0.7f);
    createFloat("masterWidth", "Stereo Width", 0.0f, 1.0f, 1.0f);
    createFloat("masterRelease", "Note Safety Tail", 3.00f, 20.0f, 5.0f);
    createFloat("masterPhase", "Global Phase Reset", 0.0f, 360.0f, 0.0f);
    createFloat("limThreshold", "Limiter Threshold (dB)", -12.0f, 0.0f, 0.0f);
    createFloat("limLookahead", "Limiter Look-ahead (ms)", 0.0f, 5.0f, 1.0f);

    // CHANGED: Master LPF Range extended to 20000.0f
    createFloat("masterLPF", "Master LowPass", 60.0f, 20000.0f, 20000.0f);

    juce::StringArray osModes{ "Off", "2x (Standard)", "4x (High)", "8x (Ultra)" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osMode", "Oversampling", osModes, 1));

    return { params.begin(), params.end() };
}
void NextGenKickAudioProcessor::initPresets() {
    auto add = [&](juce::String n, int aw, float al, float ad, float ac, float at, float ah, float ap,
        int bw, float bl, float ps, float pe, float pd, float pc, float pg, float bd, float bc, float bf,
        bool st, float sn, float sl, float sd, float sc,
        int sat, float dr, float mo, float mw, float mlpf) {
            PresetData p;
            p.name = n;
            p.atkWave = aw; p.atkLevel = al; p.atkDecay = ad; p.atkCurve = ac; p.atkTone = at; p.atkHPF = ah; p.atkPan = 0;
            p.atkPitch = ap;
            p.atkPW = 0.5f;
            p.bodyWave = bw; p.bodyLevel = bl; p.pStart = ps; p.pEnd = pe; p.pDecay = pd; p.pCurve = pc; p.pGlide = pg; p.bDecay = bd; p.bCurve = bc; p.bRatio = 1.593f; p.bFilter = bf; p.bPan = 0;
            p.subTrack = st; p.subNote = sn; p.subFine = 0; p.subLevel = sl; p.subDecay = sd; p.subCurve = sc; p.subPhase = 0; p.subAntiClick = 5.0f; p.subPan = 0;
            p.satType = sat; p.osMode = 1;
            p.mDrive = dr; p.mOut = mo; p.mWidth = mw; p.mRelease = 5.0f; p.mPhase = 0; p.limThresh = 0; p.limLook = 1.0f;
            p.mLPF = mlpf;
            presetList.push_back(p);
        };

    // --- MODERN KICKS --- (All LPF defaults updated to 20000.0f)
    add("Init / Default", 0, 0.4f, 0.02f, 2.0f, 20000, 200, 3000, 0, 0.7f, 350, 43.6f, 0.07f, 1.0f, 0.8f, 0.35f, 1.0f, 5000, false, 29, 0.6f, 0.2f, 4.0f, 0, 1.0f, 0.6f, 1.0f, 20000.0f);
    add("TR-808 Pure", 0, 0.3f, 0.02f, 4.0f, 10000, 500, 3000, 0, 0.8f, 180, 48, 0.05f, 3.0f, 0.0f, 0.5f, 0.8f, 3000, false, 36, 0.5f, 0.3f, 3.0f, 0, 1.2f, 0.6f, 0.5f, 20000.0f);
    add("TR-909 Punch", 1, 0.5f, 0.02f, 2.5f, 12000, 100, 3000, 4, 0.7f, 300, 50, 0.06f, 1.5f, 0.2f, 0.25f, 2.0f, 8000, false, 38, 0.5f, 0.2f, 4.0f, 3, 2.0f, 0.5f, 0.7f, 20000.0f);
    add("TR-606 Box", 3, 0.3f, 0.01f, 5.0f, 8000, 300, 3000, 3, 0.6f, 200, 55, 0.04f, 1.0f, 0.0f, 0.2f, 1.5f, 4000, false, 43, 0.4f, 0.2f, 3.0f, 0, 1.2f, 0.6f, 0.3f, 20000.0f);
    add("Analog Fat", 7, 0.2f, 0.015f, 2.0f, 20000, 50, 398.0f, 0, 0.8f, 120, 35, 0.15f, 2.20f, 1.5f, 0.50f, 1.25f, 2500, false, 24, 0.7f, 0.25f, 8.40f, 0, 3.0f, 0.5f, 0.6f, 20000.0f);
    add("Clean Club", 0, 0.3f, 0.015f, 2.5f, 12000, 200, 3000, 0, 0.75f, 220, 48, 0.08f, 2.0f, 0.1f, 0.4f, 1.1f, 3000, false, 36, 0.4f, 0.2f, 2.5f, 0, 1.3f, 0.6f, 0.8f, 20000.0f);
    add("Tight Pop", 6, 0.4f, 0.01f, 3.0f, 15000, 150, 3000, 4, 0.7f, 300, 52, 0.06f, 3.0f, 0.0f, 0.25f, 1.8f, 6000, false, 40, 0.3f, 0.15f, 3.0f, 10, 1.0f, 0.6f, 0.7f, 20000.0f);
    add("Soft Layer", 1, 0.2f, 0.03f, 1.5f, 5000, 200, 3000, 1, 0.6f, 180, 45, 0.1f, 1.5f, 0.0f, 0.3f, 1.0f, 1500, false, 33, 0.5f, 0.2f, 2.0f, 0, 1.1f, 0.6f, 0.6f, 20000.0f);
    add("Digital Click", 3, 0.3f, 0.005f, 5.0f, 20000, 500, 3000, 0, 0.8f, 250, 55, 0.05f, 4.0f, 0.0f, 0.2f, 2.0f, 5000, false, 41, 0.2f, 0.1f, 4.0f, 1, 1.5f, 0.6f, 0.8f, 20000.0f);
    add("Solid Sub", 0, 0.1f, 0.01f, 2.0f, 3000, 100, 3000, 0, 0.8f, 150, 38, 0.05f, 2.0f, 0.0f, 0.4f, 0.8f, 1000, false, 28, 0.6f, 0.3f, 2.5f, 0, 1.5f, 0.6f, 0.5f, 20000.0f);
    add("Deep House", 1, 0.2f, 0.03f, 1.5f, 5000, 300, 3000, 0, 0.7f, 180, 48, 0.08f, 2.0f, 0.5f, 0.3f, 1.0f, 1500, false, 36, 0.5f, 0.25f, 3.0f, 3, 1.5f, 0.6f, 0.8f, 20000.0f);
    add("Tech House", 6, 0.4f, 0.01f, 3.0f, 18000, 150, 3000, 4, 0.8f, 400, 52, 0.05f, 4.0f, 0.1f, 0.2f, 3.0f, 5000, false, 40, 0.4f, 0.2f, 5.0f, 0, 2.0f, 0.5f, 0.9f, 20000.0f);
    add("Techno Rumble", 2, 0.2f, 0.05f, 1.0f, 2000, 500, 3000, 0, 0.6f, 250, 45, 0.06f, 2.5f, 0.0f, 0.2f, 1.5f, 800, false, 33, 0.7f, 0.3f, 2.0f, 3, 4.0f, 0.5f, 1.0f, 20000.0f);
    add("Warehouse", 4, 0.5f, 0.02f, 2.0f, 15000, 100, 3000, 1, 0.7f, 500, 48, 0.1f, 1.5f, 0.5f, 0.3f, 1.2f, 3000, false, 36, 0.5f, 0.25f, 2.0f, 6, 3.0f, 0.5f, 0.8f, 20000.0f);
    add("Minimal", 7, 0.6f, 0.01f, 8.0f, 20000, 1000, 3000, 0, 0.5f, 150, 50, 0.04f, 3.0f, 0.0f, 0.15f, 2.0f, 2000, false, 38, 0.3f, 0.15f, 3.0f, 10, 1.0f, 0.6f, 0.5f, 20000.0f);
    add("Dub Techno", 2, 0.1f, 0.05f, 1.0f, 800, 200, 3000, 0, 0.6f, 100, 38, 0.2f, 0.8f, 2.0f, 0.6f, 0.5f, 600, false, 26, 0.7f, 0.35f, 2.0f, 3, 2.0f, 0.5f, 1.0f, 20000.0f);
    add("Disco Pop", 1, 0.3f, 0.02f, 2.0f, 10000, 200, 3000, 1, 0.7f, 220, 55, 0.09f, 2.0f, 0.2f, 0.35f, 1.5f, 4000, false, 41, 0.4f, 0.25f, 3.0f, 4, 1.5f, 0.6f, 0.7f, 20000.0f);
    add("Future House", 3, 0.5f, 0.015f, 4.0f, 18000, 150, 3000, 0, 0.8f, 450, 58, 0.06f, 3.5f, 0.0f, 0.25f, 2.5f, 8000, false, 46, 0.3f, 0.2f, 4.0f, 1, 2.0f, 0.6f, 0.9f, 20000.0f);
    add("Lo-Fi HipHop", 2, 0.6f, 0.04f, 1.0f, 3000, 50, 3000, 0, 0.5f, 120, 45, 0.12f, 1.0f, 0.5f, 0.3f, 1.0f, 800, false, 33, 0.5f, 0.3f, 2.0f, 8, 1.0f, 0.7f, 0.4f, 20000.0f);
    add("Acid Kick", 4, 0.4f, 0.01f, 5.0f, 20000, 100, 3000, 2, 0.7f, 600, 50, 0.08f, 4.0f, 0.0f, 0.2f, 2.0f, 12000, false, 38, 0.4f, 0.2f, 3.0f, 5, 3.5f, 0.5f, 0.8f, 20000.0f);
    add("Big Room", 4, 0.5f, 0.01f, 6.0f, 20000, 200, 3000, 0, 0.8f, 800, 48, 0.15f, 3.0f, 0.0f, 0.4f, 1.0f, 12000, false, 36, 0.2f, 0.3f, 2.0f, 1, 2.5f, 0.5f, 1.0f, 20000.0f);
    add("Prog Trance", 0, 0.4f, 0.02f, 3.0f, 15000, 300, 3000, 0, 0.7f, 350, 50, 0.08f, 2.5f, 0.1f, 0.3f, 1.8f, 6000, false, 38, 0.4f, 0.25f, 3.0f, 0, 1.5f, 0.6f, 0.8f, 20000.0f);
    add("Psytrance", 7, 0.6f, 0.005f, 10.0f, 20000, 500, 3000, 4, 0.8f, 500, 55, 0.04f, 5.0f, 0.0f, 0.15f, 3.0f, 12000, false, 43, 0.1f, 0.15f, 5.0f, 5, 2.0f, 0.6f, 0.7f, 20000.0f);
    add("Uplifting", 0, 0.5f, 0.02f, 4.0f, 18000, 400, 3000, 0, 0.7f, 400, 52, 0.07f, 3.0f, 0.0f, 0.25f, 2.0f, 8000, false, 40, 0.3f, 0.25f, 3.5f, 9, 2.0f, 0.6f, 0.9f, 20000.0f);
    add("Melbourne", 3, 0.4f, 0.03f, 2.0f, 12000, 100, 3000, 2, 0.8f, 300, 50, 0.1f, 2.0f, 0.5f, 0.3f, 1.5f, 10000, false, 38, 0.3f, 0.25f, 2.0f, 4, 2.5f, 0.5f, 0.9f, 20000.0f);
    add("Complextro", 3, 0.5f, 0.02f, 3.0f, 15000, 200, 3000, 3, 0.7f, 400, 45, 0.08f, 2.5f, 0.2f, 0.25f, 2.0f, 12000, false, 33, 0.4f, 0.2f, 3.0f, 8, 6.0f, 0.4f, 0.8f, 20000.0f);
    add("Eurodance", 0, 0.4f, 0.02f, 3.0f, 20000, 300, 3000, 0, 0.8f, 300, 55, 0.06f, 2.0f, 0.0f, 0.2f, 1.5f, 8000, false, 41, 0.3f, 0.2f, 3.0f, 0, 1.2f, 0.6f, 0.7f, 20000.0f);
    add("Hard Trance", 2, 0.5f, 0.02f, 3.0f, 15000, 200, 3000, 2, 0.8f, 500, 50, 0.1f, 2.5f, 0.1f, 0.3f, 1.5f, 12000, false, 38, 0.3f, 0.25f, 3.0f, 5, 3.0f, 0.5f, 0.9f, 20000.0f);
    add("Tropical", 5, 0.3f, 0.02f, 2.0f, 8000, 100, 3000, 1, 0.7f, 180, 48, 0.06f, 1.5f, 0.0f, 0.2f, 1.2f, 4000, false, 36, 0.4f, 0.2f, 2.0f, 2, 1.2f, 0.6f, 0.7f, 20000.0f);
    add("Future Rave", 4, 0.6f, 0.01f, 5.0f, 20000, 200, 3000, 0, 0.8f, 600, 58, 0.08f, 3.0f, 0.0f, 0.25f, 1.8f, 10000, false, 46, 0.2f, 0.2f, 3.0f, 1, 2.5f, 0.5f, 0.8f, 20000.0f);
    add("Trap 808 Long", 0, 0.2f, 0.02f, 5.0f, 10000, 500, 3000, 0, 0.9f, 200, 40, 0.08f, 3.0f, 0.0f, 0.5f, 0.5f, 2000, true, 36, 0.0f, 0.3f, 1.0f, 0, 2.0f, 0.6f, 0.5f, 20000.0f);
    add("Trap 808 Hard", 4, 0.4f, 0.01f, 4.0f, 15000, 300, 3000, 0, 0.9f, 300, 45, 0.06f, 4.0f, 0.2f, 0.4f, 0.8f, 5000, true, 40, 0.0f, 0.3f, 2.0f, 1, 4.0f, 0.5f, 0.6f, 20000.0f);
    add("Drill 808", 0, 0.2f, 0.02f, 3.0f, 12000, 400, 3000, 0, 0.9f, 200, 48, 0.3f, 1.5f, 4.0f, 0.5f, 0.6f, 1500, true, 41, 0.0f, 0.3f, 1.0f, 3, 3.0f, 0.6f, 0.6f, 20000.0f);
    add("Dubstep", 6, 0.5f, 0.01f, 6.0f, 20000, 100, 3000, 3, 0.8f, 400, 50, 0.08f, 3.0f, 0.0f, 0.25f, 2.0f, 8000, false, 38, 0.4f, 0.2f, 3.0f, 6, 4.0f, 0.5f, 0.9f, 20000.0f);
    add("Hybrid Trap", 2, 0.4f, 0.02f, 3.0f, 10000, 200, 3000, 2, 0.8f, 350, 42, 0.12f, 2.5f, 0.5f, 0.3f, 1.0f, 5000, true, 30, 0.3f, 0.25f, 2.0f, 7, 3.0f, 0.5f, 0.8f, 20000.0f);
    add("Future Bass", 1, 0.3f, 0.03f, 1.5f, 8000, 200, 3000, 0, 0.7f, 200, 50, 0.1f, 2.0f, 0.0f, 0.2f, 1.2f, 3000, true, 38, 0.4f, 0.25f, 2.0f, 2, 1.5f, 0.6f, 0.7f, 20000.0f);
    add("Phonk", 2, 0.5f, 0.02f, 2.0f, 5000, 100, 3000, 3, 0.8f, 250, 45, 0.1f, 1.5f, 0.0f, 0.3f, 1.0f, 1000, false, 33, 0.3f, 0.3f, 1.5f, 8, 8.0f, 0.4f, 0.5f, 20000.0f);
    add("Glitch Hop", 3, 0.4f, 0.01f, 4.0f, 12000, 200, 3000, 1, 0.7f, 300, 50, 0.07f, 3.0f, 0.2f, 0.2f, 1.8f, 4000, false, 38, 0.4f, 0.2f, 3.0f, 7, 3.0f, 0.5f, 0.9f, 20000.0f);
    add("UK Garage", 5, 0.3f, 0.015f, 3.0f, 10000, 300, 3000, 0, 0.7f, 200, 55, 0.06f, 2.0f, 0.0f, 0.2f, 1.5f, 3000, false, 43, 0.4f, 0.2f, 3.0f, 0, 1.2f, 0.6f, 0.8f, 20000.0f);
    add("Bass House", 7, 0.5f, 0.01f, 5.0f, 15000, 200, 3000, 1, 0.8f, 400, 48, 0.08f, 3.0f, 0.0f, 0.25f, 1.5f, 8000, false, 36, 0.3f, 0.25f, 3.0f, 5, 4.0f, 0.5f, 0.9f, 20000.0f);
    add("Hardstyle", 4, 0.6f, 0.01f, 6.0f, 20000, 100, 3000, 2, 0.9f, 800, 55, 0.15f, 4.0f, 0.0f, 0.3f, 2.81f, 15000, false, 43, 0.1f, 0.3f, 4.0f, 7, 1.20f, 0.6f, 1.0f, 20000.0f);
    add("Rawstyle", 4, 0.7f, 0.01f, 8.0f, 20000, 50, 3000, 2, 0.9f, 900, 50, 0.2f, 5.0f, 0.0f, 0.4f, 0.8f, 15000, false, 38, 0.0f, 0.3f, 5.0f, 1, 10.0f, 0.4f, 1.0f, 20000.0f);
    add("Gabber", 3, 0.5f, 0.01f, 5.0f, 15000, 200, 3000, 3, 0.8f, 600, 55, 0.12f, 3.0f, 0.2f, 0.3f, 2.96f, 10000, false, 43, 0.2f, 0.25f, 3.0f, 6, 12.0f, 0.4f, 0.9f, 20000.0f);
    add("Frenchcore", 4, 0.5f, 0.005f, 6.0f, 18000, 300, 3000, 0, 0.8f, 500, 60, 0.08f, 4.0f, 0.0f, 0.2f, 1.5f, 8000, false, 48, 0.3f, 0.2f, 4.0f, 9, 3.0f, 0.5f, 0.8f, 20000.0f);
    add("UK Hardcore", 7, 0.4f, 0.01f, 4.0f, 15000, 400, 3000, 0, 0.8f, 400, 65, 0.07f, 3.0f, 0.0f, 0.2f, 1.8f, 6000, false, 53, 0.3f, 0.2f, 3.0f, 0, 2.0f, 0.6f, 0.8f, 20000.0f);
    add("Industrial", 2, 0.5f, 0.05f, 1.0f, 5000, 100, 3000, 3, 0.8f, 300, 35, 0.3f, 1.0f, 2.0f, 0.4f, 0.5f, 2000, false, 24, 0.4f, 0.3f, 1.0f, 8, 8.0f, 0.4f, 1.0f, 20000.0f);
    add("Neurofunk", 4, 0.5f, 0.01f, 5.0f, 18000, 200, 3000, 2, 0.8f, 500, 50, 0.08f, 3.5f, 0.5f, 0.2f, 1.8f, 10000, false, 38, 0.3f, 0.2f, 3.5f, 6, 4.0f, 0.5f, 0.9f, 20000.0f);
    add("DnB Roll", 6, 0.4f, 0.01f, 4.0f, 15000, 300, 3000, 0, 0.8f, 350, 55, 0.06f, 3.0f, 0.0f, 0.15f, 2.0f, 6000, false, 41, 0.3f, 0.2f, 3.0f, 4, 2.0f, 0.6f, 0.8f, 20000.0f);
    add("Jump Up", 3, 0.4f, 0.015f, 3.0f, 12000, 150, 3000, 3, 0.7f, 400, 58, 0.06f, 2.5f, 0.0f, 0.15f, 1.5f, 5000, false, 46, 0.4f, 0.2f, 3.0f, 1, 3.0f, 0.6f, 0.8f, 20000.0f);
    add("Liquid DnB", 1, 0.3f, 0.02f, 2.0f, 8000, 200, 3000, 0, 0.7f, 200, 52, 0.07f, 2.0f, 0.0f, 0.2f, 1.2f, 3000, false, 40, 0.4f, 0.2f, 2.0f, 2, 1.5f, 0.6f, 0.7f, 20000.0f);
    add("Default (Empty)", 0, 0.0f, 0.01f, 2.0f, 20000, 20, 3000, 0, 0.0f, 100, 20, 0.1f, 1.0f, 0.0f, 0.1f, 1.0f, 20000, false, 0, 0.0f, 0.1f, 3.0f, 0, 1.0f, 0.6f, 1.0f, 20000.0f);
}

void NextGenKickAudioProcessor::loadPreset(int index) {
    if (index < 0 || index >= presetList.size()) return;
    const auto& p = presetList[index];

    auto setVal = [&](juce::String id, float val) {
        if (auto* param = apvts.getParameter(id)) param->setValueNotifyingHost(param->convertTo0to1(val));
        };

    setVal("atkWave", (float)p.atkWave); setVal("atkLevel", p.atkLevel); setVal("atkDecay", p.atkDecay);
    setVal("atkCurve", p.atkCurve); setVal("atkTone", p.atkTone); setVal("atkHPF", p.atkHPF);
    setVal("atkPan", p.atkPan); setVal("atkPitch", p.atkPitch); setVal("atkPulseWidth", p.atkPW);

    setVal("bodyWave", (float)p.bodyWave); setVal("bodyLevel", p.bodyLevel); setVal("pStart", p.pStart);
    setVal("pEnd", p.pEnd); setVal("pDecay", p.pDecay); setVal("pCurve", p.pCurve);
    setVal("pGlide", p.pGlide); setVal("bodyDecay", p.bDecay); setVal("bodyCurve", p.bCurve);
    setVal("besselRatio", p.bRatio); setVal("bodyFilter", p.bFilter); setVal("bodyPan", p.bPan);

    setVal("subTrack", p.subTrack ? 1.0f : 0.0f); setVal("subNote", p.subNote); setVal("subFine", p.subFine);
    setVal("subLevel", p.subLevel); setVal("subDecay", p.subDecay); setVal("subCurve", p.subCurve);
    setVal("subPhase", p.subPhase); setVal("subAntiClick", p.subAntiClick); setVal("subPan", p.subPan);
    // subMode removed

    setVal("satType", (float)p.satType); setVal("osMode", (float)p.osMode);
    setVal("masterDrive", p.mDrive); setVal("masterOut", p.mOut); setVal("masterWidth", p.mWidth);
    setVal("masterRelease", p.mRelease); setVal("masterPhase", p.mPhase);
    setVal("limThreshold", p.limThresh); setVal("limLookahead", p.limLook);
    setVal("masterLPF", p.mLPF);
}

void NextGenKickAudioProcessor::performRandomization() {
    auto setVal = [&](juce::String id, float val) {
        if (auto* param = apvts.getParameter(id)) param->setValueNotifyingHost(param->convertTo0to1(val));
        };
    auto randF = [&](float min, float max) { return min + random.nextFloat() * (max - min); };
    auto randI = [&](int min, int max) { return random.nextInt(max - min + 1) + min; };

    setVal("atkWave", (float)randI(0, 7));
    setVal("atkLevel", randF(0.2f, 0.8f));
    setVal("atkDecay", randF(0.005f, 0.08f));
    setVal("atkCurve", randF(0.5f, 4.0f));
    setVal("atkTone", randF(5000.0f, 20000.0f));
    setVal("atkHPF", randF(20.0f, 500.0f));
    setVal("atkPitch", randF(500.0f, 8000.0f));

    setVal("bodyWave", (float)randI(0, 4));
    setVal("pStart", randF(200.0f, 1000.0f));
    setVal("pEnd", randF(30.0f, 60.0f));
    setVal("pDecay", randF(0.05f, 0.3f));
    setVal("pCurve", randF(0.5f, 3.0f));
    setVal("bodyDecay", randF(0.2f, 0.8f));
    setVal("bodyLevel", randF(0.6f, 0.9f));
    setVal("besselRatio", randF(1.0f, 2.5f));
    setVal("bodyFilter", randF(2000.0f, 12000.0f));

    setVal("subLevel", randF(0.4f, 0.8f));
    setVal("subDecay", randF(0.2f, 0.6f));
    setVal("subAntiClick", randF(1.0f, 10.0f));

    setVal("satType", (float)randI(0, 10));
    setVal("masterDrive", randF(1.0f, 5.0f));
    setVal("masterLPF", randF(800.0f, 20000.0f));
}

void NextGenKickAudioProcessor::saveUserPreset(const juce::File& file) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    xml->writeTo(file);
}

void NextGenKickAudioProcessor::loadUserPreset(const juce::File& file) {
    std::unique_ptr<juce::XmlElement> xmlState(juce::XmlDocument::parse(file));
    if (xmlState.get() != nullptr)
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

inline double NextGenKickAudioProcessor::generateUltraPureSine(double phase) noexcept {
    double x = phase - juce::MathConstants<double>::pi;
    if (x < -juce::MathConstants<double>::pi) x += juce::MathConstants<double>::twoPi;
    else if (x > juce::MathConstants<double>::pi) x -= juce::MathConstants<double>::twoPi;
    const double x2 = x * x;
    return x * (1.0 - x2 * (0.1666666667 - x2 * (0.0083333333 - x2 * (0.0001984127 - x2 * (0.0000027557 - x2 * 0.0000000209)))));
}

inline double NextGenKickAudioProcessor::polyBlep(double t, double dt) noexcept {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    else if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

float NextGenKickAudioProcessor::getPinkNoise() noexcept {
    const float w = random.nextFloat() * 2.0f - 1.0f;
    b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750312f; b2 = 0.96900f * b2 + w * 0.1538520f;
    b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 + w * 0.0168980f;
    const float p = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f; b6 = w * 0.11592f; return p;
}

// --- ADAA (Antiderivative Antialiasing) Calculation ---
inline float NextGenKickAudioProcessor::calcADAAFunc(float x, int type) noexcept {
    switch (type) {
    case 0: // Soft Tanh
        if (std::abs(x) > 10.0f) return std::abs(x) - 0.693147f;
        return std::log(std::cosh(x));

    case 1: // Hard Clip
        if (x < -1.0f) return -x - 0.5f;
        if (x > 1.0f) return x - 0.5f;
        return 0.5f * x * x;

    case 6: // BJT (Atan based)
    {
        const float k = 2.2f;
        const float scale = 0.58f;
        float term1 = x * std::atan(k * x);
        float term2 = (0.5f / k) * std::log(1.0f + k * k * x * x);
        return scale * (term1 - term2);
    }

    case 7: // Wavefold
    {
        return -1.0f / juce::MathConstants<float>::pi * std::cos(x * juce::MathConstants<float>::pi);
    }

    case 10: // Cubic
        return (0.5f * x * x) - (x * x * x * x * 0.08333333f);

    default: return 0.0f;
    }
}

float NextGenKickAudioProcessor::processSaturationSampleADAA(float x, int type, float drive, SaturationState& state) {
    if (drive <= 1.001f) {
        state.active = false;
        state.lastX = x;
        return x;
    }

    if (type == 3) {
        float g = x * drive;
        float y = 0.92f * std::tanh(g + 0.08f * state.tapeHysteresis);
        state.tapeHysteresis = y;
        return y;
    }

    if (type == 2 || type == 4 || type == 5 || type == 8 || type == 9) {
        float g = x * drive;
        switch (type) {
        case 2: { float v = g + 0.15f; return (v > 0) ? (v / (1.0f + std::abs(v * 0.35f))) - 0.1f : v * 0.85f; }
        case 4: return g / (1.0f + 0.45f * std::abs(g));
        case 5: return (std::abs(g) < 1.0f) ? g - (g * g * g) / 3.0f : (g > 0 ? 0.67f : -0.67f);
        case 8: { float step = 1.0f / (1.0f + (25.0f - drive)); return std::round(g / step) * step; }
        case 9: { float hpf = g - 0.96f * g; return g + 0.45f * std::tanh(hpf * 9.0f); }
        default: return g;
        }
    }

    float g = x * drive;

    if (!state.active) {
        state.active = true;
        state.lastX = g;
        state.lastF = calcADAAFunc(g, type);

        switch (type) {
        case 0: return std::tanh(g);
        case 1: return juce::jlimit(-1.0f, 1.0f, g);
        case 6: return std::atan(g * 2.2f) * 0.58f;
        case 7: return std::sin(g * juce::MathConstants<float>::pi);
        case 10: return g - (g * g * g) / 3.1f;
        }
    }

    float Fx = calcADAAFunc(g, type);
    float output = 0.0f;
    float delta = g - state.lastX;

    if (std::abs(delta) < 1.0e-5f) {
        switch (type) {
        case 0: output = std::tanh(g); break;
        case 1: output = juce::jlimit(-1.0f, 1.0f, g); break;
        case 6: output = std::atan(g * 2.2f) * 0.58f; break;
        case 7: output = std::sin(g * juce::MathConstants<float>::pi); break;
        case 10: output = g - (g * g * g) / 3.1f; break;
        }
    }
    else {
        output = (Fx - state.lastF) / delta;
    }

    state.lastX = g;
    state.lastF = Fx;

    return output;
}

void NextGenKickAudioProcessor::updateOversampler(int mode, int samplesPerBlock) {
    std::lock_guard<std::mutex> lock(oversamplerMutex);

    if (mode <= 0) {
        oversampler.reset();
        setLatencySamples(0);
    }
    else {
        // mode 1=2x(factor=1), 2=4x(factor=2), 3=8x(factor=3)
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(2, mode, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampler->initProcessing(samplesPerBlock);
        oversampler->reset();
        setLatencySamples(oversampler->getLatencyInSamples());
    }
}

bool NextGenKickAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono() || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void NextGenKickAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = (float)sampleRate;
    isNoteActive = false;
    std::fill(limBufferL.begin(), limBufferL.end(), 0.0f);
    std::fill(limBufferR.begin(), limBufferR.end(), 0.0f);
    dcLastInL = dcLastOutL = dcLastInR = dcLastOutR = 0;
    cachedPeak = 0.0f; cachedPeakIdx = -1;
    crCounter = 0;

    for (auto& s : satStates) s.reset();

    satBuffer.setSize(2, samplesPerBlock);

    // Initial OS setup
    currentOsMode = (int)apvts.getRawParameterValue("osMode")->load();
    updateOversampler(currentOsMode, samplesPerBlock);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;

    filterAtkHP.prepare(spec); filterAtkHP.setType(juce::dsp::StateVariableTPTFilterType::highpass); filterAtkHP.setResonance(0.5f);
    filterAtkLP.prepare(spec); filterAtkLP.setType(juce::dsp::StateVariableTPTFilterType::lowpass); filterAtkLP.setResonance(0.5f);
    filterBodyLP.prepare(spec); filterBodyLP.setType(juce::dsp::StateVariableTPTFilterType::lowpass); filterBodyLP.setResonance(0.5f);

    // Master LPF Init (4-stage cascade)
    // CHANGED: Q = 0.5 (Critically Damped) for smoother response
    for (auto& f : filterMasterLP_L) { f.prepare(spec); f.setType(juce::dsp::StateVariableTPTFilterType::lowpass); f.setResonance(0.5f); }
    for (auto& f : filterMasterLP_R) { f.prepare(spec); f.setType(juce::dsp::StateVariableTPTFilterType::lowpass); f.setResonance(0.5f); }

    double smoothTime = 0.02;
    s_atkDecay.reset(sampleRate, smoothTime); s_atkCurve.reset(sampleRate, smoothTime); s_atkTone.reset(sampleRate, smoothTime);
    s_atkLevel.reset(sampleRate, smoothTime); s_atkPan.reset(sampleRate, smoothTime); s_atkPitch.reset(sampleRate, smoothTime);
    s_atkHPF.reset(sampleRate, smoothTime); s_atkPW.reset(sampleRate, smoothTime);
    s_pStart.reset(sampleRate, smoothTime); s_pEnd.reset(sampleRate, smoothTime); s_pDecay.reset(sampleRate, smoothTime);
    s_pGlide.reset(sampleRate, smoothTime); s_pCurve.reset(sampleRate, smoothTime); s_bodyDecay.reset(sampleRate, smoothTime);
    s_bodyCurve.reset(sampleRate, smoothTime); s_bodyLevel.reset(sampleRate, smoothTime); s_bodyPan.reset(sampleRate, smoothTime);
    s_besselRatio.reset(sampleRate, smoothTime); s_bodyFilter.reset(sampleRate, smoothTime);
    s_subNote.reset(sampleRate, smoothTime); s_subFine.reset(sampleRate, smoothTime); s_subDecay.reset(sampleRate, smoothTime);
    s_subCurve.reset(sampleRate, smoothTime); s_subLevel.reset(sampleRate, smoothTime); s_subPhase.reset(sampleRate, smoothTime);
    s_subAntiClick.reset(sampleRate, smoothTime); s_subPan.reset(sampleRate, smoothTime);
    s_masterDrive.reset(sampleRate, smoothTime); s_masterOut.reset(sampleRate, smoothTime); s_masterWidth.reset(sampleRate, smoothTime);
    s_masterRelease.reset(sampleRate, smoothTime); s_masterPhase.reset(sampleRate, smoothTime); s_limThreshold.reset(sampleRate, smoothTime);
    s_masterLPF.reset(sampleRate, smoothTime);

    updateParameters();
}

void NextGenKickAudioProcessor::updateParameters() {
    auto getRaw = [this](juce::String id) { return apvts.getRawParameterValue(id)->load(); };
    s_atkDecay.setTargetValue(getRaw("atkDecay")); s_atkCurve.setTargetValue(getRaw("atkCurve")); s_atkTone.setTargetValue(getRaw("atkTone"));
    s_atkLevel.setTargetValue(getRaw("atkLevel")); s_atkPan.setTargetValue(getRaw("atkPan")); s_atkPitch.setTargetValue(getRaw("atkPitch"));
    s_atkHPF.setTargetValue(getRaw("atkHPF")); s_atkPW.setTargetValue(getRaw("atkPulseWidth"));
    s_pStart.setTargetValue(getRaw("pStart")); s_pEnd.setTargetValue(getRaw("pEnd")); s_pDecay.setTargetValue(getRaw("pDecay"));
    s_pGlide.setTargetValue(getRaw("pGlide")); s_pCurve.setTargetValue(getRaw("pCurve")); s_bodyDecay.setTargetValue(getRaw("bodyDecay"));
    s_bodyCurve.setTargetValue(getRaw("bodyCurve")); s_bodyLevel.setTargetValue(getRaw("bodyLevel")); s_bodyPan.setTargetValue(getRaw("bodyPan"));
    s_besselRatio.setTargetValue(getRaw("besselRatio")); s_bodyFilter.setTargetValue(getRaw("bodyFilter"));
    s_subNote.setTargetValue(getRaw("subNote")); s_subFine.setTargetValue(getRaw("subFine")); s_subDecay.setTargetValue(getRaw("subDecay"));
    s_subCurve.setTargetValue(getRaw("subCurve")); s_subLevel.setTargetValue(getRaw("subLevel")); s_subPhase.setTargetValue(getRaw("subPhase"));
    s_subAntiClick.setTargetValue(getRaw("subAntiClick")); s_subPan.setTargetValue(getRaw("subPan"));
    s_masterDrive.setTargetValue(getRaw("masterDrive")); s_masterOut.setTargetValue(getRaw("masterOut")); s_masterWidth.setTargetValue(getRaw("masterWidth"));
    s_masterRelease.setTargetValue(getRaw("masterRelease")); s_masterPhase.setTargetValue(getRaw("masterPhase")); s_limThreshold.setTargetValue(getRaw("limThreshold"));
    s_masterLPF.setTargetValue(getRaw("masterLPF"));
}

void NextGenKickAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto numSamples = buffer.getNumSamples();
    updateParameters();

    const double invSR = 1.0 / (double)currentSampleRate;
    const double twoPi = juce::MathConstants<double>::twoPi;
    const double invTwoPi = 1.0 / twoPi;

    const int sMod = (int)apvts.getRawParameterValue("satType")->load();
    const int aWav = (int)apvts.getRawParameterValue("atkWave")->load();
    const int bWav = (int)apvts.getRawParameterValue("bodyWave")->load();
    const bool sTra = apvts.getRawParameterValue("subTrack")->load() > 0.5f;
    const int newOsMode = (int)apvts.getRawParameterValue("osMode")->load();

    if (newOsMode != currentOsMode) {
        currentOsMode = newOsMode;
        updateOversampler(currentOsMode, numSamples);
    }

    const float dcAlpha = std::exp(-(float)invSR * (1.0f / 0.075f));

    float lookaheadMs = apvts.getRawParameterValue("limLookahead")->load();
    int lookaheadSamples = std::max(0, (int)(lookaheadMs * 0.001f * currentSampleRate));
    lookaheadSamples = std::min(lookaheadSamples, limBufferSize - 2);

    for (const auto metadata : midiMessages) {
        if (metadata.getMessage().isNoteOn()) {
            lastMidiNote = metadata.getMessage().getNoteNumber();
            isNoteActive = true;
            noteOnTime = 0.0;
            subAttackCounter = 0;

            filterAtkHP.reset(); filterAtkLP.reset(); filterBodyLP.reset();
            // CHANGED: REMOVED Master LPF Reset to prevent clicks
            // for(auto& f : filterMasterLP_L) f.reset();
            // for(auto& f : filterMasterLP_R) f.reset();

            double sPh = s_masterPhase.getTargetValue() / 360.0 * twoPi;
            phaseAtk = phaseBody = sPh;
            phaseSub = ((s_subPhase.getTargetValue() / 360.0) * twoPi) + sPh;

            fullWaveWriteIdx = 0;
            isRecordingFullWave = true;

            {
                std::lock_guard<std::mutex> lock(oversamplerMutex);
                if (oversampler) oversampler->reset();
            }

            for (auto& s : satStates) s.reset();
        }
    }

    if (!isNoteActive) { buffer.clear(); return; }

    auto* satL = satBuffer.getWritePointer(0);
    auto* satR = satBuffer.getWritePointer(1);

    float aPitVal = 0, aDecVal = 0, aCurVal = 0, aHPFVal = 0, aToneVal = 0, aLevVal = 0, aPanVal = 0, aPWVal = 0;
    float pStaVal = 0, pEndVal = 0, pDecVal = 0, pGliVal = 0, pCurVal = 0, bRatVal = 0, bLevVal = 0, bDecVal = 0, bCurVal = 0, bPanVal = 0, bFiltVal = 0;
    float sNotVal = 0, sDecV = 0, sCurV = 0, sLevV = 0, sPanV = 0, mDriVal = 0, mOutVal = 0, mRelVal = 0, lThrDB = 0, mLPFVal = 0;
    double sBaseHz = 0, sFinalHz = 0;

    for (int i = 0; i < numSamples; ++i) {
        aPitVal = s_atkPitch.getNextValue(); aDecVal = s_atkDecay.getNextValue(); aCurVal = s_atkCurve.getNextValue();
        aHPFVal = s_atkHPF.getNextValue(); aToneVal = s_atkTone.getNextValue(); aLevVal = s_atkLevel.getNextValue();
        aPanVal = s_atkPan.getNextValue(); aPWVal = s_atkPW.getNextValue();
        pStaVal = s_pStart.getNextValue(); pEndVal = s_pEnd.getNextValue(); pDecVal = s_pDecay.getNextValue();
        pGliVal = s_pGlide.getNextValue(); pCurVal = s_pCurve.getNextValue(); bRatVal = s_besselRatio.getNextValue();
        bLevVal = s_bodyLevel.getNextValue(); bDecVal = s_bodyDecay.getNextValue(); bCurVal = s_bodyCurve.getNextValue();
        bPanVal = s_bodyPan.getNextValue(); bFiltVal = s_bodyFilter.getNextValue();
        sNotVal = sTra ? (float)lastMidiNote : s_subNote.getNextValue();

        sBaseHz = 440.0 * std::pow(2.0, ((double)sNotVal - 69.0) / 12.0);
        sFinalHz = sBaseHz + (double)s_subFine.getNextValue();

        sDecV = s_subDecay.getNextValue(); sCurV = s_subCurve.getNextValue(); sLevV = s_subLevel.getNextValue();
        sPanV = s_subPan.getNextValue();
        mDriVal = s_masterDrive.getNextValue(); mOutVal = s_masterOut.getNextValue(); mRelVal = s_masterRelease.getNextValue();
        lThrDB = s_limThreshold.getNextValue();
        mLPFVal = s_masterLPF.getNextValue();

        if (++crCounter >= 8) {
            crCounter = 0;
            filterAtkHP.setCutoffFrequency(aHPFVal); filterAtkLP.setCutoffFrequency(aToneVal); filterBodyLP.setCutoffFrequency(bFiltVal);
            // Update all 4 stages of Master LPF
            for (auto& f : filterMasterLP_L) f.setCutoffFrequency(mLPFVal);
            for (auto& f : filterMasterLP_R) f.setCutoffFrequency(mLPFVal);
        }

        double atkRaw = 0.0; double dtA = (double)aPitVal * invSR;
        if (aWav == 0) {
            if (hasSpareNoise) { atkRaw = spareNoise; hasSpareNoise = false; }
            else {
                float u1 = random.nextFloat(), u2 = random.nextFloat();
                float mag = std::sqrt(-2.0f * std::log(u1 + 1e-9f));
                atkRaw = mag * std::cos(twoPi * u2) * 0.4f; spareNoise = mag * std::sin(twoPi * u2) * 0.4f; hasSpareNoise = true;
            }
        }
        else if (aWav == 1) atkRaw = getPinkNoise();
        else if (aWav == 2) { float w = random.nextFloat() * 2.0f - 1.0f; lastBrown = (lastBrown + 0.02f * w) / 1.02f; atkRaw = lastBrown * 3.5f; }
        else if (aWav == 3) { double t = phaseAtk * invTwoPi; atkRaw = (t < 0.5 ? 1.0 : -1.0) + polyBlep(t, dtA) - polyBlep(std::fmod(t + 0.5, 1.0), dtA); }
        else if (aWav == 4) { double t = phaseAtk * invTwoPi; atkRaw = (2.0 * t - 1.0) - polyBlep(t, dtA); }
        else if (aWav == 5) { double t = phaseAtk * invTwoPi; atkRaw = std::abs(t - 0.5) * 4.0 - 1.0; }
        else if (aWav == 6) { double t = phaseAtk * invTwoPi; atkRaw = (t < aPWVal ? 1.0 : -1.0) + polyBlep(t, dtA) - polyBlep(std::fmod(t + (1.0 - aPWVal), 1.0), dtA); }
        else atkRaw = generateUltraPureSine(phaseAtk);

        float atkEnv = std::pow(std::max(0.0f, 1.0f - (float)noteOnTime / (aDecVal + 0.0001f)), aCurVal);
        float atkCalc = (float)atkRaw * atkEnv * aLevVal;

        float atkFilt = filterAtkHP.processSample(0, atkCalc);
        atkFilt = filterAtkLP.processSample(0, atkFilt);

        float pE = std::pow(std::exp(-(float)noteOnTime / (pDecVal + 0.0001f)), pCurVal);
        double fBody = (double)pEndVal + ((double)pStaVal - (double)pEndVal) * (pE + pGliVal * (pE * pE * pE));
        double dtB = fBody * invSR; double bodyRaw = 0.0;

        if (bWav == 0) bodyRaw = generateUltraPureSine(phaseBody);
        else if (bWav == 1) bodyRaw = (generateUltraPureSine(phaseBody) + 0.4 * generateUltraPureSine(phaseBody * bRatVal) + 0.2 * generateUltraPureSine(phaseBody * 2.135)) / 1.7;
        else if (bWav == 2) { double t = phaseBody * invTwoPi; bodyRaw = (2.0 * t - 1.0) - polyBlep(t, dtB); }
        else if (bWav == 3) { double t = phaseBody * invTwoPi; bodyRaw = (t < 0.5 ? 1.0 : -1.0) + polyBlep(t, dtB) - polyBlep(std::fmod(t + 0.5, 1.0), dtB); }
        else { double t = phaseBody * invTwoPi; bodyRaw = std::abs(t - 0.5) * 4.0 - 1.0; }

        float bodyEnv = std::pow(std::exp(-(float)noteOnTime / (bDecVal + 0.0001f)), bCurVal);
        float bodySig = (float)bodyRaw * bodyEnv * bLevVal;

        float bodyFilt = filterBodyLP.processSample(0, bodySig);

        float subEnvBase = std::pow(std::exp(-(float)noteOnTime / (sDecV + 0.0001f)), sCurV);
        float sAnt = s_subAntiClick.getNextValue();
        float subFadeInv = 1.0f / ((sAnt / 1000.0f) * (float)currentSampleRate + 0.001f);
        float antiC = (subAttackCounter * subFadeInv < 1.0f) ? 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * (float)subAttackCounter++ * subFadeInv)) : 1.0f;
        float subFinal = (float)generateUltraPureSine(phaseSub) * subEnvBase * antiC * sLevV;

        float mixL = (atkFilt * (1.0f - aPanVal)) + (bodyFilt * (1.0f - bPanVal)) + (subFinal * (1.0f - sPanV));
        float mixR = (atkFilt * (1.0f + aPanVal)) + (bodyFilt * (1.0f + bPanVal)) + (subFinal * (1.0f + sPanV));

        mixL *= 0.6f;
        mixR *= 0.6f;

        satL[i] = (mixL + lastMixL) * 0.5f;
        satR[i] = (mixR + lastMixR) * 0.5f;
        lastMixL = mixL; lastMixR = mixR;

        if (isRecordingFullWave) {
            int fIdx = fullWaveWriteIdx.load();
            if (fIdx < fullWaveSize) {
                fullWaveAtk[fIdx] = atkFilt;
                fullWaveBody[fIdx] = bodyFilt;
                fullWaveSub[fIdx] = subFinal;
                fullWaveWriteIdx.store(fIdx + 1);
            }
            else { isRecordingFullWave = false; }
        }

        phaseBody += twoPi * dtB; if (phaseBody >= twoPi) phaseBody -= twoPi;
        phaseSub += twoPi * (sFinalHz * invSR); if (phaseSub >= twoPi) phaseSub -= twoPi;
        phaseAtk += twoPi * dtA; if (phaseAtk >= twoPi) phaseAtk -= twoPi;
        noteOnTime += (double)invSR;

        if (subEnvBase < 0.0001f && bodyEnv < 0.0001f && noteOnTime >(double)mRelVal) { isNoteActive = false; break; }
    }

    juce::dsp::AudioBlock<float> satBlock(satBuffer);

    // Locked Oversampling Process
    {
        std::lock_guard<std::mutex> lock(oversamplerMutex);
        if (oversampler) {
            auto upsampledBlock = oversampler->processSamplesUp(satBlock);
            for (int ch = 0; ch < 2; ++ch) {
                auto* p = upsampledBlock.getChannelPointer(ch);
                auto& state = satStates[ch];
                for (int i = 0; i < (int)upsampledBlock.getNumSamples(); ++i) {
                    p[i] = processSaturationSampleADAA(p[i], sMod, mDriVal, state);
                }
            }
            oversampler->processSamplesDown(satBlock);
        }
        else {
            for (int ch = 0; ch < 2; ++ch) {
                auto* p = satBlock.getChannelPointer(ch);
                auto& state = satStates[ch];
                for (int i = 0; i < numSamples; ++i) {
                    p[i] = processSaturationSampleADAA(p[i], sMod, mDriVal, state);
                }
            }
        }
    }

    auto* outL = buffer.getWritePointer(0); auto* outR = buffer.getWritePointer(1);
    const float* srcL = satBuffer.getReadPointer(0);
    const float* srcR = satBuffer.getReadPointer(1);

    int visStart1 = 0, visSize1 = 0, visStart2 = 0, visSize2 = 0;
    std::vector<float> tempVisBuffer(numSamples);
    visualFifo.prepareToWrite(numSamples, visStart1, visSize1, visStart2, visSize2);

    float driveComp = 1.0f / std::sqrt(std::max(1.0f, mDriVal));

    for (int i = 0; i < numSamples; ++i) {
        float driveL = srcL[i] * driveComp * mOutVal;
        float driveR = srcR[i] * driveComp * mOutVal;

        // Apply Master LPF (Stereo, 4-stage cascade = 48dB/oct)
        // Bypass if frequency is very high (near 20k)
        if (mLPFVal < 19950.0f) {
            for (auto& f : filterMasterLP_L) driveL = f.processSample(0, driveL);
            for (auto& f : filterMasterLP_R) driveR = f.processSample(0, driveR);
        }

        float mWidthVal = s_masterWidth.getNextValue();
        float mid = (driveL + driveR) * 0.5f;
        float side = (driveL - driveR) * 0.5f * mWidthVal;
        driveL = mid + side;
        driveR = mid - side;

        limBufferL[limWriteIdx] = driveL;
        limBufferR[limWriteIdx] = driveR;

        float currentInputAbs = std::max(std::abs(driveL), std::abs(driveR));
        int windowTailIdx = (limWriteIdx - lookaheadSamples + limBufferSize) & limMask;

        if (currentInputAbs >= cachedPeak) {
            cachedPeak = currentInputAbs;
            cachedPeakIdx = limWriteIdx;
        }
        else if (cachedPeakIdx == windowTailIdx) {
            cachedPeak = 0.0f;
            for (int k = 0; k < lookaheadSamples; ++k) {
                int idx = (limWriteIdx - k + limBufferSize) & limMask;
                float p = std::max({ cachedPeak, std::abs(limBufferL[idx]), std::abs(limBufferR[idx]) });
                if (p > cachedPeak) {
                    cachedPeak = p;
                    cachedPeakIdx = idx;
                }
            }
        }

        float lThr = juce::Decibels::decibelsToGain(lThrDB);
        float gain = (cachedPeak > lThr) ? (lThr / cachedPeak) : 1.0f;

        float outRawL = limBufferL[windowTailIdx] * gain;
        float outRawR = limBufferR[windowTailIdx] * gain;
        limWriteIdx = (limWriteIdx + 1) & limMask;

        dcLastOutL = outRawL - dcLastInL + dcAlpha * dcLastOutL; dcLastInL = outRawL;
        dcLastOutR = outRawR - dcLastInR + dcAlpha * dcLastOutR; dcLastInR = outRawR;
        outL[i] = dcLastOutL; outR[i] = dcLastOutR;

        tempVisBuffer[i] = (dcLastOutL + dcLastOutR) * 0.5f;
    }

    if (visSize1 > 0) {
        for (int i = 0; i < visSize1; ++i) visualBuffer[visStart1 + i] = tempVisBuffer[i];
    }
    if (visSize2 > 0) {
        for (int i = 0; i < visSize2; ++i) visualBuffer[visStart2 + i] = tempVisBuffer[visSize1 + i];
    }
    visualFifo.finishedWrite(visSize1 + visSize2);
}

const juce::String NextGenKickAudioProcessor::getName() const { return JucePlugin_Name; }
bool NextGenKickAudioProcessor::acceptsMidi() const { return true; }
bool NextGenKickAudioProcessor::producesMidi() const { return false; }
bool NextGenKickAudioProcessor::isMidiEffect() const { return false; }
double NextGenKickAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int NextGenKickAudioProcessor::getNumPrograms() { return 1; }
int NextGenKickAudioProcessor::getCurrentProgram() { return 0; }
void NextGenKickAudioProcessor::setCurrentProgram(int index) {}
const juce::String NextGenKickAudioProcessor::getProgramName(int index) { return {}; }
void NextGenKickAudioProcessor::changeProgramName(int index, const juce::String& newName) {}
bool NextGenKickAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* NextGenKickAudioProcessor::createEditor() { return new NextGenKickAudioProcessorEditor(*this); }
void NextGenKickAudioProcessor::getStateInformation(juce::MemoryBlock& destData) { auto state = apvts.copyState(); std::unique_ptr<juce::XmlElement> xml(state.createXml()); copyXmlToBinary(*xml, destData); }
void NextGenKickAudioProcessor::setStateInformation(const void* data, int sizeInBytes) { std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes)); if (xmlState.get() != nullptr) apvts.replaceState(juce::ValueTree::fromXml(*xmlState)); }
void NextGenKickAudioProcessor::releaseResources() {}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NextGenKickAudioProcessor(); }