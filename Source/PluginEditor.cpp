#include "PluginProcessor.h"
#include "PluginEditor.h"

// --- Helper Functions ---

static juce::String utf8(const char* text) {
    return juce::String::fromUTF8(text);
}

juce::String InfoBarSlider::getNoteStr(double hz) {
    if (hz <= 0) return "";
    double noteNum = 69.0 + 12.0 * std::log2(hz / 440.0);
    int noteInt = (int)(noteNum + 0.5);
    int cents = (int)((noteNum - noteInt) * 100.0);
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (noteInt / 12) - 1;
    int nameIdx = noteInt % 12;
    if (nameIdx < 0) nameIdx += 12;
    juce::String centStr = (cents >= 0 ? "+" : "") + juce::String(cents);
    return juce::String(noteNames[nameIdx]) + juce::String(octave) + " (" + centStr + " ct)";
}

void InfoBarSlider::updateInfo() {
    if (onInfoUpdate) {
        juce::String valStr;
        double val = getValue();
        if (isFreq) valStr = juce::String(val, 1) + " Hz [" + getNoteStr(val) + "]";
        else if (isNote) {
            double freq = 440.0 * std::pow(2.0, (val - 69.0) / 12.0);
            valStr = "MIDI " + juce::String((int)val) + " [" + getNoteStr(freq) + "]";
        }
        else valStr = juce::String(val, 2) + " " + unit;
        onInfoUpdate(nameJP + " : " + valStr + "  ---  " + description, requiresKeyTrackInfo);
    }
}

void InfoBarCombo::updateInfo() {
    if (onInfoUpdate) {
        int idx = getSelectedItemIndex();
        juce::String itemDesc = (idx >= 0 && idx < itemDescriptions.size()) ? itemDescriptions[idx] : "";
        onInfoUpdate(nameJP + " : " + getText() + "  ---  " + itemDesc);
    }
}

// --- Constructor ---
NextGenKickAudioProcessorEditor::NextGenKickAudioProcessorEditor(NextGenKickAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(960, 800);
    scopeData.resize(1024, 0.0f);

    auto& lf = getLookAndFeel();
    juce::Font jpFont = juce::Font("Meiryo UI", 16.0f, juce::Font::plain);
    if (!jpFont.getTypefaceName().contains("Meiryo")) {
        jpFont = juce::Font("Yu Gothic UI", 16.0f, juce::Font::plain);
    }
    if (auto* v4 = dynamic_cast<juce::LookAndFeel_V4*>(&lf)) {
        v4->setDefaultSansSerifTypefaceName(jpFont.getTypefaceName());
    }
    else {
        infoBar.setFont(jpFont);
    }

    addAndMakeVisible(titleLabel);
    titleLabel.setText("NEXT GEN KICK", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(26.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    // --- Buttons ---
    addAndMakeVisible(randomButton);
    randomButton.setButtonText("Random");
    randomButton.setTooltip(utf8("ランダマイズ"));
    randomButton.onClick = [this] { audioProcessor.performRandomization(); };

    addAndMakeVisible(undoButton);
    undoButton.setButtonText("Undo");
    undoButton.setTooltip(utf8("元に戻す"));
    undoButton.onClick = [this] { audioProcessor.undoManager.undo(); };

    addAndMakeVisible(redoButton);
    redoButton.setButtonText("Redo");
    redoButton.setTooltip(utf8("やり直す"));
    redoButton.onClick = [this] { audioProcessor.undoManager.redo(); };

    addAndMakeVisible(saveButton);
    saveButton.setButtonText("Save");
    saveButton.onClick = [this] {
        auto fileChooser = std::make_shared<juce::FileChooser>("Save Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xml");
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this, fileChooser](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File{}) audioProcessor.saveUserPreset(file);
            });
        };

    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load");
    loadButton.onClick = [this] {
        auto fileChooser = std::make_shared<juce::FileChooser>("Load Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xml");
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, fileChooser](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File{}) audioProcessor.loadUserPreset(file);
            });
        };

    // --- Preset Combo ---
    addAndMakeVisible(presetCombo);
    presetCombo.clear();

    for (int i = 0; i < audioProcessor.presetList.size(); ++i) {
        // インデックス番号を新しいプリセット数に合わせて修正
        if (i == 0)       presetCombo.addSectionHeading("--- MODERN KICKS ---");
        else if (i == 48) presetCombo.addSectionHeading("--- VINTAGE KICKS ---");
        else if (i == 82) presetCombo.addSectionHeading("--- SNARE & TOM ---");
        else if (i == 102) presetCombo.addSectionHeading("--- HAT ---");
        else if (i == 110) presetCombo.addSectionHeading("--- FX & OTHERS ---");

        presetCombo.addItem(audioProcessor.presetList[i].name, i + 1);
    }

    presetCombo.setSelectedId(1, juce::dontSendNotification);
    presetCombo.onChange = [this] {
        int idx = presetCombo.getSelectedItemIndex();
        if (idx >= 0) audioProcessor.loadPreset(idx);
        };

    addAndMakeVisible(infoBar);

    defaultInfoText = utf8("Ready. このプラグインはOTODESKが作成しました。気に入って頂けた方は無料で使う事ができますが、OTODESKのXアカウントをフォロー＆コメントをいただくと大変喜びます。https://x.com/kijyoumusic");
    infoBar.setText(defaultInfoText, juce::dontSendNotification);

    infoBar.setColour(juce::Label::backgroundColourId, juce::Colour(0xFF1A1A1A));
    infoBar.setColour(juce::Label::textColourId, juce::Colours::cyan);
    infoBar.setJustificationType(juce::Justification::centred);

    infoBar.addMouseListener(this, false);
    infoBar.setMouseCursor(juce::MouseCursor::PointingHandCursor);

    // --- Components Setup ---
    juce::StringArray atkWaves{
        utf8("White Noise (ホワイトノイズ)"), utf8("Pink Noise (ピンクノイズ)"), utf8("Brown Noise (ブラウンノイズ)"),
        utf8("Square (矩形波)"), utf8("Saw (ノコギリ波)"), utf8("Triangle (三角波)"), utf8("Pulse (パルス波)"), utf8("Ultra Sine (サイン波)")
    };
    juce::StringArray atkDescs{
        utf8("【ホワイトノイズ】全帯域均一。EDM等の鋭いアタックに最適です。"),
        utf8("【ピンクノイズ】聴感上フラット。自然で馴染みの良いクリック感です。"),
        utf8("【ブラウンノイズ】高域減衰。ローファイで有機的な汚れを加えます。"),
        utf8("【矩形波】奇数次倍音のみ。チップチューン的な硬いデジタル音です。"),
        utf8("【ノコギリ波】全倍音を含む。最も派手で攻撃的なアタックを作れます。"),
        utf8("【三角波】丸みがありつつ、サイン波より少しエッジがあります。"),
        utf8("【パルス波】幅を調整可能な矩形波。細くすると鋭いクリックになります。"),
        utf8("【サイン波】倍音なし。特定の周波数（Freq）をピンポイントで補強します。")
    };
    createCombo(atkWaveCombo, "atkWave", "Type", utf8("波形タイプ"), utf8("アタック成分の波形ソースを選択します。"), atkWaves, atkDescs);

    createSlider(atkDecaySlider, "atkDecay", "Decay", utf8("減衰時間"), "s", utf8("クリック音の長さを調整します。短くすると鋭く、長くすると太くなります。"));
    createSlider(atkCurveSlider, "atkCurve", "Curve", utf8("カーブ"), "", utf8("エンベロープの急峻さ。値を大きくするとアタックがより鋭角的になります。"));
    createSlider(atkToneSlider, "atkTone", "Tone", utf8("トーン(LPF)"), "Hz", utf8("ローパスフィルタ。高域のザラつきを削り、耳障りな成分を抑えます。"), true);
    createSlider(atkHPFSlider, "atkHPF", "Hi-Pass", utf8("ハイパス"), "Hz", utf8("ハイパスフィルタ。不要な低域をカットし、Bodyとの濁りを防ぎます。"), true);
    createSlider(atkLevelSlider, "atkLevel", "Level", utf8("音量"), "x", utf8("アタックレイヤーのミックス音量です。"));
    createSlider(atkPanSlider, "atkPan", "Pan", utf8("定位"), "LR", utf8("左右のバランスを調整します。"));
    createSlider(atkPitchSlider, "atkPitch", "Freq", utf8("周波数"), "Hz", utf8("幾何学波形（Sine/Saw/Pulse）選択時の基本ピッチです。"), true);
    createSlider(atkPWSlider, "atkPulseWidth", "Width", utf8("パルス幅"), "%", utf8("矩形波/パルス波の太さ。50%で完全な矩形波、小さくすると細くなります。"));

    juce::StringArray bodyWaves{
        utf8("Ultra Sine (サイン波)"), utf8("Bessel (ベッセル/FM)"), utf8("Saw (ノコギリ波)"), utf8("Square (矩形波)"), utf8("Triangle (三角波)")
    };
    juce::StringArray bodyDescs{
        utf8("【サイン波】歪みのない純粋な低音。TR-808系やTrapキックの基本です。"),
        utf8("【ベッセル】膜の物理振動を模した非調和倍音。アコースティックな響きです。"),
        utf8("【ノコギリ波】倍音豊富。フィルタと歪みを多用するHardstyle等に向きます。"),
        utf8("【矩形波】中域が空洞化した、独特のボックス感がある低音です。"),
        utf8("【三角波】サイン波に近いですが、わずかにエッジがあり存在感が出ます。")
    };
    createCombo(bodyWaveCombo, "bodyWave", "Type", utf8("波形タイプ"), utf8("キックの核（ボディ）となる波形を選択します。"), bodyWaves, bodyDescs);

    createSlider(pStartSlider, "pStart", "P.Start", utf8("ピッチ開始"), "Hz", utf8("スイープ開始周波数。高いほど「バチッ」というパンチ感が強まります。"), true);
    createSlider(pEndSlider, "pEnd", "P.End", utf8("ピッチ終了"), "Hz", utf8("スイープ到達点。キックの基音（音程）となります。"), true);
    createSlider(pDecaySlider, "pDecay", "P.Decay", utf8("ピッチ減衰"), "s", utf8("ピッチが下がりきるまでの時間。キックの「重さ」に関わります。"));
    createSlider(pCurveSlider, "pCurve", "P.Curve", utf8("ピッチカーブ"), "", utf8("下降の形状。大きくすると初期のアタック感が強調されます。"));
    createSlider(pGlideSlider, "pGlide", "Tension", utf8("張力"), "", utf8("膜の物理的な張力変化（Von Karman式）を再現し、独特の粘りを加えます。"));
    createSlider(bDecaySlider, "bodyDecay", "A.Decay", utf8("音量減衰"), "s", utf8("ボディの鳴っている長さ（余韻）を調整します。"));
    createSlider(bCurveSlider, "bodyCurve", "A.Curve", utf8("音量カーブ"), "", utf8("音量の減衰カーブ。大きくするとタイトに、小さくするとサステインが増します。"));
    createSlider(bRatioSlider, "besselRatio", "FM Ratio", utf8("FM比"), "", utf8("Bessel波形選択時の倍音比率。値を上げると金属的な響きになります。"));
    createSlider(bFilterSlider, "bodyFilter", "LPF", utf8("ローパス"), "Hz", utf8("ボディの高域を丸め、よりサブベースに近い質感にします。"), true);
    createSlider(bLevelSlider, "bodyLevel", "Level", utf8("音量"), "x", utf8("ボディレイヤーのミックス音量です。"));
    createSlider(bPanSlider, "bodyPan", "Pan", utf8("定位"), "LR", utf8("左右のバランスを調整します。"));

    createSlider(subNoteSlider, "subNote", "Note", utf8("ノート"), "", utf8("KeyTrackオフ時の固定ピッチです。"), false, true);
    createSlider(subFineSlider, "subFine", "Fine", utf8("微調整"), "Hz", utf8("周波数の微調整。Bodyとの位相干渉やうなり（Beat）を調整します。"));
    createButton(subTrackButton, "subTrack", utf8("キー追従"), utf8("オンにすると入力MIDIノートの音程で鳴ります。ベースライン専用。Levelにマウスを合わせると音程を確認できます。"));
    createSlider(subDecaySlider, "subDecay", "Decay", utf8("減衰時間"), "s", utf8("サブベースの長さ。Bodyより少し長くして余韻を作ると効果的です。"));
    createSlider(subCurveSlider, "subCurve", "Curve", utf8("カーブ"), "", utf8("減衰カーブ。サブベースは急峻（>3.0）にしてタイトにするのが定石です。"));
    createSlider(subLevelSlider, "subLevel", "Level", utf8("音量"), "x", utf8("サブレイヤーのミックス音量です。"), false, false, true);
    createSlider(subPhaseSlider, "subPhase", "Phase", utf8("位相"), "deg", utf8("Bodyに対する位相ズレ。低域の打ち消し合いを防ぐために調整します。"));
    createSlider(subAntiClickSlider, "subAntiClick", "Anti-Click", utf8("アンチクリック"), "ms", utf8("発音開始時の微小フェードイン。ゼロ交差ノイズを防ぎます。"));
    createSlider(subPanSlider, "subPan", "Pan", utf8("定位"), "LR", utf8("左右のバランス。低域はセンター（0）が推奨されます。"));

    juce::StringArray satTypes{
        utf8("Soft Tanh (ソフト/温かみ)"), utf8("Hard Clip (デジタル)"), utf8("Triode (真空管/三極管)"), utf8("Tape (テープ/粘り)"),
        utf8("Transformer (トランス/太さ)"), utf8("JFET (クランチ)"), utf8("BJT (ファズ/激歪み)"), utf8("Wavefold (ウェーブフォールド)"),
        utf8("Bitcrush (ビットクラッシュ)"), utf8("Exciter (エキサイター)"), utf8("Cubic (クリーン)")
    };
    juce::StringArray satDescs{
        utf8("【Soft Tanh】標準的なソフトクリップ。温かみがあり、最も扱いやすい歪みです。"),
        utf8("【Hard Clip】閾値で信号を切断。バリバリとしたデジタルで攻撃的な音です。"),
        utf8("【Triode】真空管（三極管）モデル。偶数次倍音を含み、太さと温かみを加えます。"),
        utf8("【Tape】磁気テープのヒステリシス（履歴）を再現。コンプ感と粘り気が出ます。"),
        utf8("【Transformer】低域の密度を上げるトランスフォーマー歪み。音の重心が下がります。"),
        utf8("【JFET】真空管に近い特性を持つトランジスタ。ジャリッとしたクランチ感です。"),
        utf8("【BJT】鋭い立ち上がりのトランジスタ。毛羽立った激しいファズサウンドです。"),
        utf8("【Wavefold】波形を折り返すことで倍音を増殖させます。金属的で変調感のある音。"),
        utf8("【Bitcrush】解像度を下げ、量子化ノイズを加えます。レトロで荒い質感。"),
        utf8("【Exciter】高域成分のみを歪ませて加算します。音の抜けときらびやかさを付加。"),
        utf8("【Cubic】3次多項式。原音のニュアンスを保ちつつ太くするクリーンな歪み。")
    };
    createCombo(satTypeCombo, "satType", "Distort", utf8("歪みタイプ"), utf8("サチュレーションのアルゴリズムを選択します。"), satTypes, satDescs);

    juce::StringArray osTypes{ "Off", "2x (Standard)", "4x (High)", "8x (Ultra)" };
    juce::StringArray osDescs{
        utf8("【Off】オーバーサンプリングなし。CPU負荷は最低ですが、折り返しノイズが発生する場合があります。"),
        utf8("【2x】標準モード。バランスの良い設定で、多くのエイリアシングを除去します。"),
        utf8("【4x】高音質モード。激しい歪みを加える場合に適していますが、CPU負荷が増加します。"),
        utf8("【8x】最高品質。ほぼ完全にエイリアシングを除去しますが、非常に高いCPU負荷がかかります。")
    };
    createCombo(osCombo, "osMode", "Quality", utf8("品質設定"), utf8("オーバーサンプリング倍率。高くすると高域の折り返しノイズが減り、よりクリアな歪みになります。"), osTypes, osDescs);

    createSlider(mDriveSlider, "masterDrive", "Drive", utf8("ドライブ"), "x", utf8("歪みの深さ。音量は自動補正されるため、質感の調整に集中できます。"));
    createSlider(mOutSlider, "masterOut", "Volume", utf8("出力音量"), "x", utf8("最終的な出力レベルです。"));
    createSlider(mWidthSlider, "masterWidth", "Width", utf8("ステレオ幅"), "x", utf8("0で完全モノラル、1でステレオ。キックは少し狭めるのが定石です。"));
    createSlider(limThreshSlider, "limThreshold", "Ceil", utf8("シーリング"), "dB", utf8("リミッターが作動する上限レベル。0dB推奨。"));
    createSlider(limLookSlider, "limLookahead", "Lookahead", "ms", utf8("先読み"), utf8("リミッターの反応速度。アタックのトランジェントを保護します。"));
    createSlider(mPhaseSlider, "masterPhase", "Phase", utf8("開始位相"), "deg", utf8("全レイヤー共通の波形開始位置。アタックの出音を安定させます。"));
    createSlider(mReleaseSlider, "masterRelease", "Gate", utf8("ゲート"), "s", utf8("ノートオフ後の強制音止め時間（安全装置）。"));

    // Added Master LPF Slider
    createSlider(masterLPFSlider, "masterLPF", "Hi-Cut", utf8("ハイカット"), "Hz", utf8("最終段のローパスフィルタ。クリックノイズや高域のザラつきを除去します。"), true);

    // CHANGED: Use logo_jpg
    logoImage = juce::ImageCache::getFromMemory(BinaryData::logo_jpg, BinaryData::logo_jpgSize);

    startTimerHz(60);
}

NextGenKickAudioProcessorEditor::~NextGenKickAudioProcessorEditor() { stopTimer(); }

void NextGenKickAudioProcessorEditor::mouseUp(const juce::MouseEvent& e) {
    if (e.eventComponent == &infoBar) {
        juce::URL("https://x.com/kijyoumusic").launchInDefaultBrowser();
    }
}

void NextGenKickAudioProcessorEditor::createSlider(InfoBarSlider& slider, const juce::String& paramID, const juce::String& nameEN, const juce::String& nameJP, const juce::String& unit, const juce::String& desc, bool isFreq, bool isNote, bool reqKeyTrack) {
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    slider.nameEN = nameEN; slider.nameJP = nameJP; slider.unit = unit; slider.description = desc; slider.isFreq = isFreq; slider.isNote = isNote;
    slider.requiresKeyTrackInfo = reqKeyTrack;

    slider.onHoverStart = [this](InfoBarSlider* s) { hoveringSlider = s; };
    slider.onHoverEnd = [this]() { hoveringSlider = nullptr; };

    slider.onInfoUpdate = [this](const juce::String& s, bool b) { updateInfoBar(s, b); };
    slider.onInfoClear = [this]() { clearInfoBar(); };
    sliderAttachments.push_back(std::make_unique<SliderAtt>(audioProcessor.apvts, paramID, slider));
}

void NextGenKickAudioProcessorEditor::createCombo(InfoBarCombo& combo, const juce::String& paramID, const juce::String& nameEN, const juce::String& nameJP, const juce::String& desc, const juce::StringArray& items, const juce::StringArray& itemDescs) {
    addAndMakeVisible(combo);
    combo.addItemList(items, 1);
    combo.nameEN = nameEN; combo.nameJP = nameJP; combo.description = desc; combo.itemDescriptions = itemDescs;
    combo.onInfoUpdate = [this](const juce::String& s) { updateInfoBar(s); };
    combo.onInfoClear = [this]() { clearInfoBar(); };

    combo.getRootMenu()->setLookAndFeel(&getLookAndFeel());
    combo.onChange = [&combo] { if (combo.isMouseOver()) combo.updateInfo(); };

    if (paramID == "atkWave") atkWaveAtt = std::make_unique<ComboAtt>(audioProcessor.apvts, paramID, combo);
    else if (paramID == "bodyWave") bodyWaveAtt = std::make_unique<ComboAtt>(audioProcessor.apvts, paramID, combo);
    else if (paramID == "satType") satTypeAtt = std::make_unique<ComboAtt>(audioProcessor.apvts, paramID, combo);
    else if (paramID == "osMode") osAtt = std::make_unique<ComboAtt>(audioProcessor.apvts, paramID, combo);
}

void NextGenKickAudioProcessorEditor::createButton(InfoBarButton& button, const juce::String& paramID, const juce::String& nameJP, const juce::String& desc) {
    addAndMakeVisible(button);
    button.setButtonText(paramID == "subTrack" ? "Key Track" : "HQ Mode");
    button.nameJP = nameJP; button.description = desc;
    button.onInfoUpdate = [this](const juce::String& s) { updateInfoBar(s); };
    button.onInfoClear = [this]() { clearInfoBar(); };
    if (paramID == "subTrack") subTrackAtt = std::make_unique<ButtonAtt>(audioProcessor.apvts, paramID, button);
}

void NextGenKickAudioProcessorEditor::updateInfoBar(const juce::String& text, bool addKeyTrackInfo) {
    juce::String finalText = text;

    if (addKeyTrackInfo && audioProcessor.apvts.getRawParameterValue("subTrack")->load() > 0.5f) {
        int note = audioProcessor.lastMidiNote;
        double freq = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);

        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        int octave = (note / 12) - 1;
        int nameIdx = note % 12;
        juce::String noteName = juce::String(noteNames[nameIdx]) + juce::String(octave);

        finalText += utf8("  [受信MIDI: ") + noteName + " / " + juce::String(freq, 1) + " Hz]";
    }

    infoBar.setText(finalText, juce::dontSendNotification);
}

void NextGenKickAudioProcessorEditor::clearInfoBar() { infoBar.setText(defaultInfoText, juce::dontSendNotification); }

void NextGenKickAudioProcessorEditor::timerCallback() {
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    audioProcessor.visualFifo.prepareToRead(1024, start1, size1, start2, size2);

    if (size1 + size2 > 0) {
        float maxVal = 0.0f;

        if (size1 > 0) {
            for (int i = 0; i < size1; ++i) {
                float v = audioProcessor.visualBuffer[start1 + i];
                scopeData[scopeWriteIndex] = v;
                scopeWriteIndex = (scopeWriteIndex + 1) % 1024;
                maxVal = std::max(maxVal, std::abs(v));
            }
        }
        if (size2 > 0) {
            for (int i = 0; i < size2; ++i) {
                float v = audioProcessor.visualBuffer[start2 + i];
                scopeData[scopeWriteIndex] = v;
                scopeWriteIndex = (scopeWriteIndex + 1) % 1024;
                maxVal = std::max(maxVal, std::abs(v));
            }
        }
        audioProcessor.visualFifo.finishedRead(size1 + size2);

        currentOutputLevel *= 0.9f;
        if (maxVal > currentOutputLevel) currentOutputLevel = maxVal;

        repaint();
    }

    if (hoveringSlider && hoveringSlider->requiresKeyTrackInfo) {
        hoveringSlider->updateInfo();
    }
}

void NextGenKickAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xFF121212));

    // Meter
    g.setColour(juce::Colours::black);
    g.fillRect((float)areaOutputMeter.getX(), (float)areaOutputMeter.getY(), (float)areaOutputMeter.getWidth(), (float)areaOutputMeter.getHeight());
    g.setColour(juce::Colours::darkgrey);
    g.drawRect((float)areaOutputMeter.getX(), (float)areaOutputMeter.getY(), (float)areaOutputMeter.getWidth(), (float)areaOutputMeter.getHeight());

    float level = std::min(1.0f, currentOutputLevel);
    float barW = level * (float)areaOutputMeter.getWidth();
    juce::Colour col = level > 0.9f ? juce::Colours::red : (level > 0.7f ? juce::Colours::yellow : juce::Colours::green);
    g.setColour(col.withAlpha(0.8f));
    g.fillRect((float)areaOutputMeter.getX(), (float)areaOutputMeter.getY(), barW, (float)areaOutputMeter.getHeight());

    g.setColour(juce::Colours::white); g.setFont(12.0f);
    g.drawText("OUTPUT LEVEL", areaOutputMeter, juce::Justification::centred);

    // Headers
    auto drawHeader = [&](juce::Rectangle<int> area, juce::Colour bgCol, juce::Colour txtCol, juce::String text) {
        g.setColour(bgCol); g.fillRect(area);
        g.setColour(txtCol); g.setFont(18.0f);
        g.drawText(text, area.removeFromTop(30), juce::Justification::centred);
        };

    drawHeader(areaAtkSection, juce::Colours::darkred.withAlpha(0.1f), juce::Colours::red, "ATTACK");
    drawHeader(areaBodySection, juce::Colours::darkgreen.withAlpha(0.1f), juce::Colours::green, "BODY");
    drawHeader(areaSubSection, juce::Colours::darkorange.withAlpha(0.1f), juce::Colours::orange, "SUB");
    drawHeader(areaMasterSection, juce::Colours::darkblue.withAlpha(0.1f), juce::Colours::cyan, "MASTER");

    // Labels
    g.setFont(15.0f); g.setColour(juce::Colours::white.withAlpha(0.9f));
    auto drawLabel = [&](juce::Slider& s, juce::String text) {
        g.drawText(text, s.getX(), s.getY() + 55, s.getWidth(), 20, juce::Justification::centred);
        };
    for (auto* s : { &atkDecaySlider, &atkCurveSlider, &atkToneSlider, &atkHPFSlider, &atkLevelSlider, &atkPanSlider, &atkPitchSlider, &atkPWSlider }) drawLabel(*s, ((InfoBarSlider*)s)->nameEN);
    for (auto* s : { &pStartSlider, &pEndSlider, &pDecaySlider, &pCurveSlider, &pGlideSlider, &bDecaySlider, &bCurveSlider, &bRatioSlider, &bFilterSlider, &bLevelSlider, &bPanSlider }) drawLabel(*s, ((InfoBarSlider*)s)->nameEN);
    for (auto* s : { &subNoteSlider, &subFineSlider, &subDecaySlider, &subCurveSlider, &subLevelSlider, &subPhaseSlider, &subAntiClickSlider, &subPanSlider }) drawLabel(*s, ((InfoBarSlider*)s)->nameEN);
    for (auto* s : { &mDriveSlider, &mOutSlider, &mWidthSlider, &mReleaseSlider, &mPhaseSlider, &limThreshSlider, &limLookSlider, &masterLPFSlider }) drawLabel(*s, ((InfoBarSlider*)s)->nameEN);

    // Scopes
    g.setColour(juce::Colours::black); g.fillRect(areaStaticScope); g.fillRect(areaRealtimeScope);
    g.setColour(juce::Colours::grey); g.drawRect(areaStaticScope, 1.0f); g.drawRect(areaRealtimeScope, 1.0f);

    pathAtk.clear(); pathBody.clear(); pathSub.clear();
    float fullH = (float)areaStaticScope.getHeight(); float fullY = (float)areaStaticScope.getCentreY();
    float fullW = (float)areaStaticScope.getWidth(); float fullX = (float)areaStaticScope.getX();
    pathAtk.startNewSubPath(fullX, fullY); pathBody.startNewSubPath(fullX, fullY); pathSub.startNewSubPath(fullX, fullY);
    if (!audioProcessor.isRecordingFullWave) {
        for (int i = 0; i < audioProcessor.fullWaveSize; i += 40) {
            float x = juce::jmap((float)i, 0.0f, (float)audioProcessor.fullWaveSize, fullX, fullX + fullW);
            pathAtk.lineTo(x, fullY - (audioProcessor.fullWaveAtk[i] * fullH * 0.45f));
            pathBody.lineTo(x, fullY - (audioProcessor.fullWaveBody[i] * fullH * 0.45f));
            pathSub.lineTo(x, fullY - (audioProcessor.fullWaveSub[i] * fullH * 0.45f));
        }
    }
    g.setColour(juce::Colours::orange.withAlpha(0.6f)); g.strokePath(pathSub, juce::PathStrokeType(1.5f));
    g.setColour(juce::Colours::green.withAlpha(0.7f)); g.strokePath(pathBody, juce::PathStrokeType(1.2f));
    g.setColour(juce::Colours::red.withAlpha(0.9f)); g.strokePath(pathAtk, juce::PathStrokeType(1.2f));

    oscPath.clear();
    float oscH = (float)areaRealtimeScope.getHeight(); float oscY = (float)areaRealtimeScope.getCentreY();
    float oscW = (float)areaRealtimeScope.getWidth(); float oscX = (float)areaRealtimeScope.getX();
    oscPath.startNewSubPath(oscX, oscY);
    int readIdx = scopeWriteIndex;
    for (int i = 0; i < 1024; i += 4) {
        float val = scopeData[(readIdx + i) % 1024];
        float x = juce::jmap((float)i, 0.0f, 1024.0f, oscX, oscX + oscW);
        float y = oscY - (val * oscH * 0.45f);
        oscPath.lineTo(x, y);
    }
    g.setColour(juce::Colours::cyan); g.strokePath(oscPath, juce::PathStrokeType(1.5f));

    g.setColour(juce::Colours::white); g.setFont(12.0f);
    g.drawText("STATIC PREVIEW", areaStaticScope.getX() + 5, areaStaticScope.getY() + 5, 100, 20, juce::Justification::left);
    g.drawText("REALTIME OUT", areaRealtimeScope.getX() + 5, areaRealtimeScope.getY() + 5, 100, 20, juce::Justification::left);

    // --- Draw Logo (Image) ---
    // CHANGED: Maximize logo in remaining space
    g.setColour(juce::Colours::black);
    g.fillRect(areaLogo);

    if (logoImage.isValid())
    {
        g.drawImage(logoImage, areaLogo.toFloat(), juce::RectanglePlacement::centred);
    }
    else
    {
        g.setColour(juce::Colours::cyan.withAlpha(0.8f));
        g.setFont(juce::Font("Impact", 24.0f, juce::Font::bold));
        g.drawText("NEXT GEN KICK", areaLogo, juce::Justification::centred);
    }
}

void NextGenKickAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds();

    areaOutputMeter = bounds.removeFromBottom(30).reduced(5, 2);

    auto headerArea = bounds.removeFromTop(40);
    titleLabel.setBounds(headerArea.removeFromLeft(200).reduced(5));

    // Top Right Controls
    loadButton.setBounds(headerArea.removeFromRight(50).reduced(5));
    saveButton.setBounds(headerArea.removeFromRight(50).reduced(5));
    presetCombo.setBounds(headerArea.removeFromRight(150).reduced(5));
    randomButton.setBounds(headerArea.removeFromRight(80).reduced(5));
    redoButton.setBounds(headerArea.removeFromRight(60).reduced(5));
    undoButton.setBounds(headerArea.removeFromRight(60).reduced(5));

    auto infoArea = bounds.removeFromTop(30);
    infoBar.setBounds(infoArea.reduced(2));

    auto scopeArea = bounds.removeFromTop(200);
    areaStaticScope = scopeArea.removeFromLeft(getWidth() / 2).reduced(5);
    areaRealtimeScope = scopeArea.reduced(5);

    auto controlsArea = bounds.reduced(5);
    int colWidth = controlsArea.getWidth() / 4;
    int pad = 2;

    areaAtkSection = controlsArea.removeFromLeft(colWidth).reduced(pad);
    areaBodySection = controlsArea.removeFromLeft(colWidth).reduced(pad);
    areaSubSection = controlsArea.removeFromLeft(colWidth).reduced(pad);
    areaMasterSection = controlsArea.reduced(pad);

    auto atkPlace = areaAtkSection; atkPlace.removeFromTop(30);
    auto bodyPlace = areaBodySection; bodyPlace.removeFromTop(30);
    auto subPlace = areaSubSection; subPlace.removeFromTop(30);
    auto masterPlace = areaMasterSection; masterPlace.removeFromTop(30);

    int knobW = 70;
    int knobH = 95;

    auto layoutKnob = [&](InfoBarSlider& s, juce::Rectangle<int>& area, int x, int y) {
        s.setBounds(area.getX() + x * knobW + 5, area.getY() + y * knobH + 5, knobW, knobH);
        };

    atkWaveCombo.setBounds(atkPlace.removeFromTop(25).reduced(2)); atkPlace.removeFromTop(5);
    layoutKnob(atkLevelSlider, atkPlace, 0, 0); layoutKnob(atkDecaySlider, atkPlace, 1, 0); layoutKnob(atkCurveSlider, atkPlace, 2, 0);
    layoutKnob(atkToneSlider, atkPlace, 0, 1); layoutKnob(atkHPFSlider, atkPlace, 1, 1); layoutKnob(atkPanSlider, atkPlace, 2, 1);
    layoutKnob(atkPitchSlider, atkPlace, 0, 2); layoutKnob(atkPWSlider, atkPlace, 1, 2);

    bodyWaveCombo.setBounds(bodyPlace.removeFromTop(25).reduced(2)); bodyPlace.removeFromTop(5);
    layoutKnob(bLevelSlider, bodyPlace, 0, 0); layoutKnob(pDecaySlider, bodyPlace, 1, 0); layoutKnob(pCurveSlider, bodyPlace, 2, 0);
    layoutKnob(pStartSlider, bodyPlace, 0, 1); layoutKnob(pEndSlider, bodyPlace, 1, 1); layoutKnob(pGlideSlider, bodyPlace, 2, 1);
    layoutKnob(bDecaySlider, bodyPlace, 0, 2); layoutKnob(bCurveSlider, bodyPlace, 1, 2); layoutKnob(bFilterSlider, bodyPlace, 2, 2);
    layoutKnob(bRatioSlider, bodyPlace, 0, 3); layoutKnob(bPanSlider, bodyPlace, 1, 3);

    // Sub Section Layout
    auto subTopRow = subPlace.removeFromTop(25);
    subTrackButton.setBounds(subTopRow.removeFromLeft(70).reduced(2));
    subPlace.removeFromTop(5);

    layoutKnob(subLevelSlider, subPlace, 0, 0); layoutKnob(subDecaySlider, subPlace, 1, 0); layoutKnob(subCurveSlider, subPlace, 2, 0);
    layoutKnob(subNoteSlider, subPlace, 0, 1); layoutKnob(subFineSlider, subPlace, 1, 1); layoutKnob(subPhaseSlider, subPlace, 2, 1);
    layoutKnob(subAntiClickSlider, subPlace, 0, 2); layoutKnob(subPanSlider, subPlace, 1, 2);

    satTypeCombo.setBounds(masterPlace.removeFromTop(25).reduced(2));
    osCombo.setBounds(masterPlace.removeFromTop(25).reduced(2));
    layoutKnob(mDriveSlider, masterPlace, 0, 0); layoutKnob(mOutSlider, masterPlace, 1, 0); layoutKnob(mWidthSlider, masterPlace, 2, 0);
    layoutKnob(limThreshSlider, masterPlace, 0, 1); layoutKnob(limLookSlider, masterPlace, 1, 1); layoutKnob(mPhaseSlider, masterPlace, 2, 1);
    layoutKnob(mReleaseSlider, masterPlace, 0, 2); layoutKnob(masterLPFSlider, masterPlace, 1, 2);

    // CHANGED: Maximize Logo Area (Fill remaining space below knobs)
    int knobsHeight = 3 * 95;
    auto logoSpace = masterPlace;
    logoSpace.removeFromTop(knobsHeight + 10); // Skip knobs + padding
    areaLogo = logoSpace.reduced(5); // Use all remaining space
}