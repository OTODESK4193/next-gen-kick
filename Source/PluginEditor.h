#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// --- Custom Components ---
class InfoBarSlider : public juce::Slider {
public:
    juce::String nameEN, nameJP, description, unit;
    bool isFreq = false;
    bool isNote = false;
    bool requiresKeyTrackInfo = false;

    std::function<void(const juce::String&, bool)> onInfoUpdate;
    std::function<void()> onInfoClear;
    std::function<void(InfoBarSlider*)> onHoverStart;
    std::function<void()> onHoverEnd;

    void mouseEnter(const juce::MouseEvent& e) override {
        if (onHoverStart) onHoverStart(this);
        updateInfo();
        juce::Slider::mouseEnter(e);
    }
    void mouseExit(const juce::MouseEvent& e) override {
        if (onHoverEnd) onHoverEnd();
        if (onInfoClear) onInfoClear();
        juce::Slider::mouseExit(e);
    }
    void valueChanged() override {
        if (isMouseOverOrDragging()) updateInfo();
        juce::Slider::valueChanged();
    }

    void updateInfo();
private:
    juce::String getNoteStr(double hz);
};

class InfoBarCombo : public juce::ComboBox {
public:
    juce::String nameEN, nameJP, description;
    juce::StringArray itemDescriptions;
    std::function<void(const juce::String&)> onInfoUpdate;
    std::function<void()> onInfoClear;

    void mouseEnter(const juce::MouseEvent& e) override { updateInfo(); juce::ComboBox::mouseEnter(e); }
    void mouseExit(const juce::MouseEvent& e) override { if (onInfoClear) onInfoClear(); juce::ComboBox::mouseExit(e); }
    void updateInfo();
};

class InfoBarButton : public juce::ToggleButton {
public:
    juce::String nameJP, description;
    std::function<void(const juce::String&)> onInfoUpdate;
    std::function<void()> onInfoClear;

    void mouseEnter(const juce::MouseEvent& e) override { if (onInfoUpdate) onInfoUpdate(nameJP + ": " + description); juce::ToggleButton::mouseEnter(e); }
    void mouseExit(const juce::MouseEvent& e) override { if (onInfoClear) onInfoClear(); juce::ToggleButton::mouseExit(e); }
};

// --- Main Editor Class ---
class NextGenKickAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    NextGenKickAudioProcessorEditor(NextGenKickAudioProcessor&);
    ~NextGenKickAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    void updateInfoBar(const juce::String& text, bool addKeyTrackInfo = false);
    void clearInfoBar();

    void createSlider(InfoBarSlider& slider, const juce::String& paramID, const juce::String& nameEN, const juce::String& nameJP, const juce::String& unit, const juce::String& desc, bool isFreq = false, bool isNote = false, bool reqKeyTrack = false);
    void createCombo(InfoBarCombo& combo, const juce::String& paramID, const juce::String& nameEN, const juce::String& nameJP, const juce::String& desc, const juce::StringArray& items, const juce::StringArray& itemDescs);
    void createButton(InfoBarButton& button, const juce::String& paramID, const juce::String& nameJP, const juce::String& desc);

    NextGenKickAudioProcessor& audioProcessor;

    // GUI Components
    juce::Label titleLabel;
    juce::ComboBox presetCombo;
    juce::TextButton randomButton;

    // New Buttons
    juce::TextButton undoButton;
    juce::TextButton redoButton;
    juce::TextButton saveButton;
    juce::TextButton loadButton;

    juce::Label infoBar;

    // Attack
    InfoBarCombo atkWaveCombo;
    InfoBarSlider atkDecaySlider, atkCurveSlider, atkToneSlider, atkHPFSlider;
    InfoBarSlider atkLevelSlider, atkPanSlider, atkPitchSlider, atkPWSlider;

    // Body
    InfoBarCombo bodyWaveCombo;
    InfoBarSlider pStartSlider, pEndSlider, pDecaySlider, pCurveSlider, pGlideSlider;
    InfoBarSlider bDecaySlider, bCurveSlider, bRatioSlider, bFilterSlider;
    InfoBarSlider bLevelSlider, bPanSlider;

    // Sub
    InfoBarSlider subNoteSlider, subFineSlider;
    InfoBarButton subTrackButton;
    // subModeCombo Removed
    InfoBarSlider subDecaySlider, subCurveSlider, subLevelSlider;
    InfoBarSlider subPhaseSlider, subAntiClickSlider, subPanSlider;

    // Master
    InfoBarCombo satTypeCombo;
    InfoBarCombo osCombo;
    InfoBarSlider mDriveSlider, mOutSlider, mWidthSlider;
    InfoBarSlider mReleaseSlider, mPhaseSlider, limThreshSlider, limLookSlider;
    InfoBarSlider masterLPFSlider;

    // Attachments
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::vector<std::unique_ptr<SliderAtt>> sliderAttachments;
    std::unique_ptr<ComboAtt> atkWaveAtt, bodyWaveAtt, satTypeAtt, osAtt;
    std::unique_ptr<ButtonAtt> subTrackAtt;

    // Visualization
    juce::Path oscPath;
    juce::Path pathAtk, pathBody, pathSub;
    juce::Rectangle<int> areaStaticScope, areaRealtimeScope, areaMeter;

    // Layout Areas
    juce::Rectangle<int> areaAtkSection, areaBodySection, areaSubSection, areaMasterSection;
    juce::Rectangle<int> areaOutputMeter;
    juce::Rectangle<int> areaLogo;

    float currentOutputLevel = 0.0f;
    std::vector<float> scopeData;
    int scopeWriteIndex = 0;

    InfoBarSlider* hoveringSlider = nullptr;
    juce::String defaultInfoText;

    // ▼▼▼ 画像用変数 ▼▼▼
    juce::Image logoImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NextGenKickAudioProcessorEditor)
};