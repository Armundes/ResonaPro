#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Version.h"
#include <cstdio>

namespace ResonaPro
{
    namespace
    {
        const juce::Colour themePrimary   { 0xffc9762e };
        const juce::Colour themeSecondary { 0xffb3a892 };
        const juce::Colour themeAccent  { 0xffc9762e };
        const juce::Colour themeGreen  { 0xff8a9a6b };
        const juce::Colour themeRose   { 0xffa85a4a };
        const juce::Colour themeDim      { 0xff7f7867 };
    }

    //==============================================================================
    ResonaProAudioProcessorEditor::ResonaProAudioProcessorEditor (ResonaProAudioProcessor& p)
        : AudioProcessorEditor (&p),
          processorRef (p),
          visualizer (p, p.apvts)
    {
        setLookAndFeel (&customLnF);
        addAndMakeVisible (visualizer);

        // ---- preset selector ----------------------------------------------------
        setupCombo (presetBox, presetLabel, "PRESET", ResonaProAudioProcessor::getPresetNames());
        presetBox.clear (juce::dontSendNotification);
        const auto names = ResonaProAudioProcessor::getPresetNames();
        presetBox.addSectionHeading ("START");
        presetBox.addItem (names[0], 1);
        presetBox.addSectionHeading ("LEAD VOCALS");
        for (int i : { 1, 2, 3, 4 }) presetBox.addItem (names[i], i + 1);
        presetBox.addSectionHeading ("VOCAL TONE & CLARITY");
        for (int i : { 5, 6, 9 }) presetBox.addItem (names[i], i + 1);
        presetBox.addSectionHeading ("SIBILANCE & AIR");
        for (int i : { 7, 8, 10 }) presetBox.addItem (names[i], i + 1);
        presetBox.addSectionHeading ("SPACE, BROADCAST & BUS");
        for (int i : { 11, 12, 13, 14 }) presetBox.addItem (names[i], i + 1);
        // Show which preset the defaults correspond to, but never *apply* one here:
        // reopening the editor must not overwrite whatever the user has dialled in.
        presetBox.setSelectedId (lastPreset, juce::dontSendNotification);
        presetBox.onChange = [this]
        {
            const int idx = presetBox.getSelectedId() - 1;
            if (idx >= 0)
            {
                lastPreset = idx;
                processorRef.loadPreset (idx);
            }
        };

        setupCombo (vocalProfileBox, vocalProfileLabel, "VOCAL PROFILE",
                    { "Lead Vocal (All-Round)", "De-Ess / Sibilance",
                      "Warm Body & De-Mud", "Air & Silk" });

        setupCombo (qualityBox, qualityLabel, "RESOLUTION",
                    { "Low Latency (1k)", "Standard (2k)", "High (4k)", "Ultra (8k)" });

        setupCombo (responseBox, responseLabel, "OVERLAP",
                    { "Eco (2x)", "Standard (4x)", "Fine (8x)" });

        // ---- knobs --------------------------------------------------------------
        // Labels say what the control does rather than what it is like.
        setupRotarySlider (knobs[0], knobLabels[0], "DEPTH", themePrimary);
        setupRotarySlider (knobs[1], knobLabels[1], "DETAIL", themePrimary);
        knobs[1].setTooltip ("Detail: how far the detector looks either side of a peak when "
                             "judging it. Low finds broader resonances and cuts harder. "
                             "High only reaches for the narrowest ringing.");
        setupRotarySlider (knobs[2], knobLabels[2], "HOW PICKY", themeSecondary);
        knobs[2].setTooltip ("How picky: how far a peak must rise above the sound around it "
                             "before the plugin acts. Low cuts anything it finds. High only "
                             "cuts the obvious offenders.");
        setupRotarySlider (knobs[3], knobLabels[3], "TRANSIENT", themeSecondary);
        setupRotarySlider (knobs[4], knobLabels[4], "MAX CUT", themeAccent);
        setupRotarySlider (knobs[5], knobLabels[5], "ATTACK", themeAccent);
        setupRotarySlider (knobs[6], knobLabels[6], "RELEASE", themeAccent);
        setupRotarySlider (knobs[7], knobLabels[7], "STEREO LINK", themeGreen);
        setupRotarySlider (knobs[8], knobLabels[8], "MIX", themeGreen);
        setupRotarySlider (knobs[9], knobLabels[9], "OUTPUT", themeRose);

        // ---- toggles ------------------------------------------------------------
        setupToggle (bypassButton, "BYPASS");
        setupToggle (iso226Button, "WEIGHTING");
        setupToggle (modeHardButton, "HARD");
        setupToggle (midSideButton, "MID/SIDE");
        setupToggle (deltaButton, "DELTA");
        setupToggle (soloCutButton, "SOLO CUT");
        setupToggle (keyButton, "EXT KEY");
        setupToggle (lowBandButton, "LOW BAND DETAIL");
        setupRotarySlider (motionSlider, motionLabel, "NOTE MOTION", themePrimary);
        motionSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        motionSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
        motionAttachment = std::make_unique<SliderAttachment> (p.apvts, "motionProtect", motionSlider);

        learnButton.setButtonText ("LEARN");
        learnButton.onClick = [this]
        {
            if (learning) { finishLearning(); return; }
            if (processorRef.apvts.getRawParameterValue ("externalKey")->load() > 0.5f)
            {
                learnStatus.setText ("Turn EXT KEY off before learning the vocal", juce::dontSendNotification);
                return;
            }
            learner.reset();
            learnedCandidates.clear();
            learnedSibilance = SibilanceDeEsser::Profile {};
            learnedProfile = LearnAnalyzer::TakeProfile {};
            processorRef.startSibilanceLearn();
            lastLearnSequence = 0;
            learning = true;
            applyLearnButton.setEnabled (false);
            learnButton.setButtonText ("STOP LEARN");
            learnStatus.setText ("Play the take; press STOP to review", juce::dontSendNotification);
            startTimerHz (20);
        };
        addAndMakeVisible (learnButton);
        applyLearnButton.setButtonText ("APPLY LEARN");
        applyLearnButton.setEnabled (false);
        applyLearnButton.onClick = [this]
        {
            for (size_t i = 0; i < learnedCandidates.size(); ++i)
            {
                const auto s = juce::String (static_cast<int> (i) + 1);
                processorRef.setParameterValue ("eq_enable_" + s, 1.0f);
                processorRef.setParameterValue ("eq_freq_" + s, learnedCandidates[i].frequency);
                processorRef.setParameterValue ("eq_gain_" + s, learnedCandidates[i].sensitivityDb);
                processorRef.setParameterValue ("eq_q_" + s, 2.0f);
            }
            learnUndo.clear();

            // Everything LEARN writes goes through one helper that refuses any id
            // outside the fence. The fence is checked again here, in the UI path,
            // not only where the values are chosen -- defence in depth, and the
            // reason the guarantee is worth stating.
            auto propose = [this] (const juce::String& id, float value)
            {
                if (! ResonaProAudioProcessor::isLearnEditable (id))
                    return;
                if (auto* prm = processorRef.apvts.getParameter (id))
                    learnUndo.emplace_back (id, prm->getValue());
                processorRef.setParameterValue (id, value);
            };

            if (learnedProfile.valid)
            {
                propose ("depth",       learnedProfile.depth);
                propose ("sharpness",   learnedProfile.sharpness);
                propose ("selectivity", learnedProfile.selectivity);
                propose ("transientPreserve", learnedProfile.transient);
                propose ("maxReduction", learnedProfile.maxCutDb);
                propose ("attack",      learnedProfile.attackMs);
                propose ("release",     learnedProfile.releaseMs);
                propose ("detailTilt",  learnedProfile.detailTilt);
                propose ("releaseTilt", learnedProfile.releaseTilt);
                propose ("attackTilt",  learnedProfile.attackTilt);

                for (int b = 0; b < learnedProfile.bandsUsed && b < 8; ++b)
                {
                    const auto n = juce::String (b + 1);
                    const auto& bd = learnedProfile.bands[b];
                    propose ("eq_enable_" + n, bd.on ? 1.0f : 0.0f);
                    propose ("eq_freq_"   + n, bd.hz);
                    propose ("eq_gain_"   + n, bd.gainDb);
                    propose ("eq_q_"      + n, bd.q);
                    propose ("eq_type_"   + n, float (bd.type));
                }
            }

            // Phase 8: the sibilance band and amount come from the take rather
            // than from my assumption about where sibilance lives.
            if (learnedSibilance.valid)
            {
                propose ("sibilanceLow",  learnedSibilance.lowHz);
                propose ("sibilanceHigh", learnedSibilance.highHz);
                propose ("sibilanceSmooth", learnedSibilance.amount);
            }

            learnApplied = ! learnUndo.empty();
            undoLearnButton.setEnabled (learnApplied);
            applyLearnButton.setEnabled (false);
            learnStatus.setText (learnedSibilance.valid
                ? "Applied. Sibilance at " + juce::String (learnedSibilance.peakHz / 1000.0f, 1)
                      + "k, de-ess " + juce::String (learnedSibilance.amount, 2)
                : "Applied to " + juce::String (learnedCandidates.size()) + " focus bands",
                                 juce::dontSendNotification);
        };
        addAndMakeVisible (applyLearnButton);

        undoLearnButton.setButtonText ("UNDO LEARN");
        undoLearnButton.setEnabled (false);
        undoLearnButton.onClick = [this]
        {
            // LEARN is the one action here that can move a dozen controls at
            // once, so it is the one action that must be reversible in a click.
            const int n = int (learnUndo.size());
            for (const auto& kv : learnUndo)
                processorRef.setParameterValue (kv.first, kv.second);
            learnUndo.clear();
            learnApplied = false;
            undoLearnButton.setEnabled (false);
            learnStatus.setText ("Reverted " + juce::String (n) + " values",
                                 juce::dontSendNotification);
        };
        addAndMakeVisible (undoLearnButton);

        // ---- fine tuning drawer controls ----------------------------------------
        setupRotarySlider (attackTiltSlider, attackTiltLabel, "ATK TILT", themeAccent);
        setupRotarySlider (releaseTiltSlider, releaseTiltLabel, "REL TILT", themeAccent);
        setupRotarySlider (detailTiltSlider, detailTiltLabel, "DETAIL TILT", themePrimary);
        // "SIBILANCE" named the problem; this control is the tool that fixes it.
        setupRotarySlider (sibilanceSlider, sibilanceLabel, "DE-ESS", themeGreen);

        drawerTitleLabel.setText ("FINE TUNE: TILTS & DE-ESS", juce::dontSendNotification);
        drawerTitleLabel.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        drawerTitleLabel.setColour (juce::Label::textColourId, themePrimary);
        drawerTitleLabel.setJustificationType (juce::Justification::centred);
        addChildComponent (drawerTitleLabel);

        drawerButton.setButtonText ("FINE TUNE <<");
        drawerButton.setClickingTogglesState (true);
        drawerButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc9762e));
        // Ask the look-and-feel to fill this one with the brand colour. Setting
        // buttonColourId alone did nothing: the shared painter ignored it, which
        // is why the button blended into the background.
        drawerButton.getProperties().set ("brandFill", true);
        drawerButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        drawerButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        drawerButton.onClick = [this]
        {
            isDrawerOpen = ! isDrawerOpen;
            drawerPreference() = isDrawerOpen;
            drawerButton.setButtonText (isDrawerOpen ? "FINE TUNE <<" : "FINE TUNE >>");
            drawerButton.setToggleState (isDrawerOpen, juce::dontSendNotification);
            resized();
            repaint();
        };
        addAndMakeVisible (drawerButton);
        learnStatus.setColour (juce::Label::textColourId, CustomLookAndFeel::inkSoft());
        learnStatus.setFont (juce::Font (juce::FontOptions (10.5f)));
        learnStatus.setText ("Learn a played take; nothing changes until Apply", juce::dontSendNotification);
        addAndMakeVisible (learnStatus);

        resetBandsButton.setButtonText ("RESET BANDS");
        resetBandsButton.onClick = [this]
        {
            for (int b = 1; b <= 8; ++b)
            {
                const auto s = juce::String (b);
                processorRef.setParameterValue ("eq_enable_" + s, 1.0f);
                processorRef.setParameterValue ("eq_gain_" + s, 0.0f);
            }
        };
        addAndMakeVisible (resetBandsButton);

        // ---- level match, delta band, A/B and band clipboard --------------------
        setupToggle (matchButton, "MATCH");
        setupToggle (abButton, "A/B");

        setupCombo (deltaBandBox, deltaBandLabel, "DELTA BAND",
                    { "All", "1", "2", "3", "4", "5", "6", "7", "8" });

        copyBandsButton.setButtonText ("COPY");
        copyBandsButton.onClick = [this]
        {
            juce::String payload = "ResonaProBandsV1";
            for (int b = 1; b <= 8; ++b)
            {
                const auto s = juce::String (b);
                for (const char* prefix : { "eq_freq_", "eq_gain_", "eq_q_", "eq_enable_" })
                    payload += ";" + juce::String (
                        processorRef.apvts.getRawParameterValue (juce::String (prefix) + s)->load(), 5);
            }
            juce::SystemClipboard::copyTextToClipboard (payload);
        };
        addAndMakeVisible (copyBandsButton);

        pasteBandsButton.setButtonText ("PASTE");
        pasteBandsButton.onClick = [this]
        {
            const auto fields = juce::StringArray::fromTokens (
                juce::SystemClipboard::getTextFromClipboard(), ";", "");
            if (fields.size() != 33 || fields[0] != "ResonaProBandsV1")
                return;
            for (int i = 1; i < fields.size(); ++i)
                if (fields[i].isEmpty() || ! fields[i].containsOnly ("0123456789+-."))
                    return;
            for (int b = 0; b < 8; ++b)
            {
                const auto s = juce::String (b + 1);
                processorRef.setParameterValue ("eq_freq_" + s,
                                                juce::jlimit (20.0f, 20000.0f, fields[1 + b * 4].getFloatValue()));
                processorRef.setParameterValue ("eq_gain_" + s,
                                                juce::jlimit (-24.0f, 24.0f, fields[2 + b * 4].getFloatValue()));
                processorRef.setParameterValue ("eq_q_" + s,
                                                juce::jlimit (0.1f, 8.0f, fields[3 + b * 4].getFloatValue()));
                processorRef.setParameterValue ("eq_enable_" + s,
                                                fields[4 + b * 4].getFloatValue() > 0.5f ? 1.0f : 0.0f);
            }
        };
        addAndMakeVisible (pasteBandsButton);

        abButton.onClick = [this] { swapAB(); };

        infoLabel.setJustificationType (juce::Justification::centredRight);
        infoLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::inkSoft());
        infoLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
        addAndMakeVisible (infoLabel);

        // ---- attachments --------------------------------------------------------
        depthAttachment      = std::make_unique<SliderAttachment> (p.apvts, "depth", knobs[0]);
        detailAttachment     = std::make_unique<SliderAttachment> (p.apvts, "sharpness", knobs[1]);
        selectivityAttachment= std::make_unique<SliderAttachment> (p.apvts, "selectivity", knobs[2]);
        transientAttachment  = std::make_unique<SliderAttachment> (p.apvts, "transientPreserve", knobs[3]);
        maxCutAttachment     = std::make_unique<SliderAttachment> (p.apvts, "maxReduction", knobs[4]);
        attackAttachment     = std::make_unique<SliderAttachment> (p.apvts, "attack", knobs[5]);
        releaseAttachment    = std::make_unique<SliderAttachment> (p.apvts, "release", knobs[6]);
        stereoLinkAttachment = std::make_unique<SliderAttachment> (p.apvts, "stereoLink", knobs[7]);
        mixAttachment        = std::make_unique<SliderAttachment> (p.apvts, "mix", knobs[8]);
        outputAttachment     = std::make_unique<SliderAttachment> (p.apvts, "outGain", knobs[9]);

        // Units on the readouts. A bare 24.0 could be anything; 24.0 dB is a
        // measurement. The three controls whose range is a fraction carry a
        // formatter instead of a suffix so the percentage is right.
        knobs[4].setTextValueSuffix (" dB");
        knobs[5].setTextValueSuffix (" ms");
        knobs[6].setTextValueSuffix (" ms");
        knobs[9].setTextValueSuffix (" dB");
        knobs[8].setTextValueSuffix (" %");   // range is 0..100, so the sign reads right
        // Transient and Stereo Link run 0..1 and JUCE ignores a text formatter set
        // after the attachment, so they show the fraction. Worth fixing later; a
        // wrong percentage would be worse than a plain number.

        qualityAttachment      = std::make_unique<ComboBoxAttachment> (p.apvts, "quality", qualityBox);
        responseAttachment     = std::make_unique<ComboBoxAttachment> (p.apvts, "response", responseBox);
        vocalProfileAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, "vocalProfile", vocalProfileBox);

        bypassAttachment    = std::make_unique<ButtonAttachment> (p.apvts, "bypass", bypassButton);
        iso226Attachment    = std::make_unique<ButtonAttachment> (p.apvts, "iso226", iso226Button);
        modeHardAttachment  = std::make_unique<ButtonAttachment> (p.apvts, "modeHard", modeHardButton);
        midSideAttachment   = std::make_unique<ButtonAttachment> (p.apvts, "midSide", midSideButton);
        deltaAttachment     = std::make_unique<ButtonAttachment> (p.apvts, "deltaListen", deltaButton);
        soloCutAttachment   = std::make_unique<ButtonAttachment> (p.apvts, "soloDeEss", soloCutButton);
        matchAttachment     = std::make_unique<ButtonAttachment> (p.apvts, "autoGain", matchButton);
        keyAttachment       = std::make_unique<ButtonAttachment> (p.apvts, "externalKey", keyButton);
        lowBandAttachment   = std::make_unique<ButtonAttachment> (p.apvts, "multiResolution", lowBandButton);
        deltaBandAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, "deltaBand", deltaBandBox);

        attackTiltAttachment  = std::make_unique<SliderAttachment> (p.apvts, "attackTilt", attackTiltSlider);
        releaseTiltAttachment = std::make_unique<SliderAttachment> (p.apvts, "releaseTilt", releaseTiltSlider);
        detailTiltAttachment  = std::make_unique<SliderAttachment> (p.apvts, "detailTilt", detailTiltSlider);
        sibilanceAttachment   = std::make_unique<SliderAttachment> (p.apvts, "sibilanceSmooth", sibilanceSlider);

        // Keep the readout in step with the two controls that change it.
        qualityBox.onChange  = [this] { updateInfoLabel(); };
        responseBox.onChange = [this] { updateInfoLabel(); };
        updateInfoLabel();

        // ---- tooltips -----------------------------------------------------------
        const std::pair<juce::Component*, const char*> tips[] = {
            { &knobs[0],  "How much reduction is applied. 0 is a true bypass-grade passthrough." },
            { &knobs[1],  "Analysis bandwidth. Low acts like a dynamic EQ, high like a surgical notch." },
            { &knobs[2],  "How far a peak must stand above the material's own peakiness before it is touched." },
            { &knobs[3],  "Raises the threshold during attacks so plosives and consonants keep their edge." },
            { &knobs[4],  "Ceiling on the reduction of any single frequency. Lower it if the result sounds hollow." },
            { &knobs[5],  "Per-bin envelope attack. The low end releases more slowly than the top." },
            { &knobs[6],  "Per-bin envelope release." },
            { &knobs[7],  "0 is dual mono, 1 is one shared decision for both channels." },
            { &knobs[8],  "Dry/wet. The dry path is delay-matched, so mixing never combs." },
            { &knobs[9],  "Output trim." },
            { &bypassButton,    "Latency-compensated bypass, so A/B switching does not shift the track." },
            { &iso226Button,    "Approximate ear-sensitive threshold bias; NOT an ISO 226 equal-loudness calculation." },
            { &modeHardButton,  "Reacts to absolute levels instead of relative ones. Much stronger." },
            { &midSideButton,   "Process the centre and the edges separately." },
            { &deltaButton,     "Plays everything the plug-in removes, at natural level and timing." },
            { &soloCutButton,   "Plays ONLY what the de-esser removes. Silent when it is cutting nothing -- "
                                "so if you hear a vowel in here, that vowel is being cut." },
            { &matchButton,     "Trims the output to the input's perceived loudness (ITU-R BS.1770), never raising the peak above the input." },
            { &deltaBandBox,    "Restrict DELTA to one focus band." },
            { &copyBandsButton, "Copy all eight focus band settings to paste into another instance." },
            { &pasteBandsButton,"Paste focus band settings copied from another instance." },
            { &abButton,        "Store the current settings and switch to the other slot." },
            { &resetBandsButton,"Return all eight focus bands to neutral." },
            { &qualityBox,      "Transform length. This buys real frequency resolution, and costs latency." },
            { &responseBox,     "How often the gain curve is recomputed." },
            { &vocalProfileBox, "Aims the detector at the region that needs work." },
            { &presetBox,       "Factory starting points." }
        };

        for (const auto& t : tips)
            if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (t.first))
                client->setTooltip (t.second);
        keyButton.setTooltip ("Use the host's optional sidechain input to detect resonances; process the main vocal.");
        lowBandButton.setTooltip ("Adds a 4k analysis window below 1.2 kHz while retaining current synthesis latency.");
        drawerButton.setTooltip ("Open/close the surgical fine-tuning drawer housing frequency-dependent tilt & de-essing controls.");
        attackTiltSlider.setTooltip ("Attack Tilt: how attack speed is split across the spectrum. "
                                     "Up = highs grab fast, lows move slowly. Measured: 2 frames at 6 kHz, 8 at 300 Hz.");
        releaseTiltSlider.setTooltip ("Release Tilt: how release speed is split across the spectrum. "
                                      "Up = highs let go first and keep the air, lows hold longer.");
        detailTiltSlider.setTooltip ("Detail Tilt: how narrow the detector's search window is, and how it "
                                     "changes with frequency. Up = surgical in the highs, broad in the lows.");
        sibilanceSlider.setTooltip ("De-esser. Measures the 4-11 kHz band against the 1-4 kHz body and cuts "
                                    "broadly when a consonant jumps out. Measured: -11 dB in band, 0.00 dB on the body.");
        motionSlider.setTooltip ("Protect moving harmonics and give a slight preference to anchored low-frequency rings.");
        learnButton.setTooltip ("Play the vocal take while learning; press Stop to review proposed focus frequencies.");
        applyLearnButton.setTooltip ("Apply the proposed focus nodes. The audio thread is never modified during capture.");

        // Double-clicking a knob must return it to its own default, not to zero.
        // (Zero is wrong for Mix, which should snap back to 100 %.)
        const char* knobParamIDs[NumKnobs] = { "depth", "sharpness", "selectivity", "transientPreserve",
                                              "maxReduction", "attack", "release", "stereoLink",
                                              "mix", "outGain" };
        for (int i = 0; i < NumKnobs; ++i)
            if (auto* param = p.apvts.getParameter (knobParamIDs[i]))
                knobs[i].setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

        setSize (960, 700);
        setResizable (true, true);
        setResizeLimits (820, 610, 1500, 980);

        // The timer drives the knob readouts as well as take learning, so it runs
        // for the life of the editor rather than only while learning.
        startTimerHz (12);
    }

    ResonaProAudioProcessorEditor::~ResonaProAudioProcessorEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void ResonaProAudioProcessorEditor::timerCallback()
    {
        // Runs whether or not a take is being learned, so the readouts stay live
        // while the user turns a knob.
        if (! learning) return;
        std::array<float, ResonaProAudioProcessor::ScopeSize> mag {}, reduction {}, baseline {};
        std::array<float, ResonaProAudioProcessor::ScopeSize> window;
        float reference = 0.0f;
        uint64_t sequence = 0;
        processorRef.getVisualizerData (mag, reduction, baseline, window, reference, &sequence);
        if (sequence == 0 || sequence == lastLearnSequence) return;
        lastLearnSequence = sequence;
        learner.addFrame (mag, baseline, reference);
        if (learner.frameCount() % 20 == 0)
            learnStatus.setText ("Listening: " + juce::String (learner.frameCount() / 20)
                                 + " s  —  press STOP to review", juce::dontSendNotification);
        if (learner.frameCount() >= 20 * 600) finishLearning();
    }

    void ResonaProAudioProcessorEditor::finishLearning()
    {
        learning = false;
        learnButton.setButtonText ("LEARN");
        learnedCandidates = learner.suggestions();
        learnedSibilance = processorRef.finishSibilanceLearn();
        learnedProfile   = learner.profile();

        // Onset and release come from the engine, which sees every transform
        // frame. The scope this learner reads runs at 20 frames a second, so one
        // frame is 50 ms, and these two controls are millisecond-scale. Every
        // value the learner produced for them landed on a ceiling: the attack
        // read 8 ms on every take because the measurement was dead, and once
        // that was fixed it read 30 ms on every take because 50 ms frames cannot
        // express a 30 ms window. The engine measures both properly.
        if (learnedProfile.valid)
        {
            // processor is the juce::AudioProcessor base reference the editor
            // holds, so the concrete type has to be named to reach these.
            const auto& proc = static_cast<const ResonaProAudioProcessor&> (processor);
            const float onsetMs = proc.getEngineLearnOnsetMs();
            const float ringMs  = proc.getEngineLearnRingMs();

            if (onsetMs > 0.0f)
                learnedProfile.attackMs = juce::jlimit (1.0f, 30.0f, onsetMs * 0.75f);

            if (ringMs > 0.0f)
                learnedProfile.releaseMs = juce::jlimit (15.0f, 300.0f, ringMs * 0.6f);
        }
        if (learnedCandidates.empty())
        {
            learnStatus.setText ("No recurring peaks; play at least a second of audible vocal",
                                 juce::dontSendNotification);
            return;
        }
        juce::String review;
        for (const auto& c : learnedCandidates)
        {
            if (review.isNotEmpty()) review += ", ";
            review += c.frequency >= 1000.0f
                ? juce::String (c.frequency / 1000.0f, 1) + "k"
                : juce::String (c.frequency, 0);
        }
        juce::String extra;
        if (learnedProfile.valid)
            extra = "  |  needs depth " + juce::String (learnedProfile.depth, 1)
                  + ", detail " + juce::String (learnedProfile.sharpness, 1)
                  + ", tilt " + juce::String (learnedProfile.detailTilt, 2)
                  + (learnedSibilance.valid
                       ? ", sibilance at " + juce::String (learnedSibilance.peakHz / 1000.0f, 1) + "k"
                       : juce::String());
        learnStatus.setText ("Found: " + review + extra,
                             juce::dontSendNotification);
        applyLearnButton.setEnabled (true);
    }

    void ResonaProAudioProcessorEditor::swapAB()
    {
        auto capture = [this]
        {
            juce::MemoryBlock state;
            processorRef.getStateInformation (state);
            return state;
        };

        auto restore = [this] (const juce::MemoryBlock& state)
        {
            if (state.getSize() > 0)
                processorRef.setStateInformation (state.getData(),
                                                  static_cast<int> (state.getSize()));
        };

        if (! slotsInitialised)
        {
            // First press: the current settings become slot A, and we move to B
            // which starts from the same values.
            slotA = capture();
            slotB = slotA;
            slotsInitialised = true;
        }
        else
        {
            if (usingSlotA) slotA = capture();
            else            slotB = capture();
        }

        usingSlotA = ! usingSlotA;
        restore (usingSlotA ? slotA : slotB);

        // The combo boxes do not refresh themselves after a bulk state change.
        qualityBox.setSelectedId (static_cast<int> (processorRef.apvts.getRawParameterValue ("quality")->load()) + 1,
                                  juce::dontSendNotification);
        responseBox.setSelectedId (static_cast<int> (processorRef.apvts.getRawParameterValue ("response")->load()) + 1,
                                   juce::dontSendNotification);
        abButton.setButtonText (usingSlotA ? "A/B  A" : "A/B  B");
        updateInfoLabel();
    }

    void ResonaProAudioProcessorEditor::updateInfoLabel()
    {
        const int    qualityIdx = static_cast<int> (processorRef.apvts.getRawParameterValue ("quality")->load());
        const int    fftSize    = 1024 << juce::jlimit (0, 3, qualityIdx);
        const double sr         = processorRef.getSampleRate() > 0.0
                                    ? processorRef.getSampleRate() : 48000.0;
        const int    bins       = fftSize / 2 + 1;
        const double binHz      = (sr * 0.5) / static_cast<double> (bins - 1);
        const double latencyMs  = 1000.0 * fftSize / sr;

        const juce::String text =
              juce::String (fftSize / 1024) + "k / "
            + juce::String (bins) + " bins / "
            + juce::String (binHz, 1) + " Hz / "
            + juce::String (std::lround (latencyMs)) + " ms";

        infoLabel.setText (text, juce::dontSendNotification);
    }

    //==============================================================================
    void ResonaProAudioProcessorEditor::setupRotarySlider (juce::Slider& slider, juce::Label& label,
                                                           const juce::String& text, juce::Colour accent)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 17);
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xff3b3833));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        addAndMakeVisible (slider);

        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, themeDim);
        addAndMakeVisible (label);
    }

    void ResonaProAudioProcessorEditor::setupToggle (juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        addAndMakeVisible (button);
    }

    void ResonaProAudioProcessorEditor::setupCombo (juce::ComboBox& box, juce::Label& label,
                                                    const juce::String& text, const juce::StringArray& items)
    {
        box.addItemList (items, 1);
        box.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (box);

        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, themeDim);
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    }

    //==============================================================================
    void ResonaProAudioProcessorEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xffede8de));

        // ---- header -------------------------------------------------------------
        auto headerArea = getLocalBounds().removeFromTop (56);
        juce::ColourGradient headerGrad (juce::Colour (0xffe9e3d7), 0, 0,
                                         juce::Colour (0xffede8de), 0, 56, false);
        g.setGradientFill (headerGrad);
        g.fillRect (headerArea);

        g.setFont (juce::FontOptions ("Helvetica Neue", 20.0f, juce::Font::bold));
        g.setColour (themePrimary);
        g.drawText ("RESONA", 18, 12, 90, 26, juce::Justification::left);

        g.setFont (juce::FontOptions ("Helvetica Neue", 20.0f, juce::Font::plain));
        g.setColour (themeAccent);
        g.drawText ("VOCAL", 106, 12, 70, 26, juce::Justification::left);

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (themeDim);
        g.drawText ("SPECTRAL VOCAL DE-RESONATOR  /  DE-ESSER  /  TONE SHAPER",
                    180, 17, 420, 18, juce::Justification::left);
        // The version of the binary that is actually loaded. Worth being able to
        // read at a glance: the first question with any plug-in problem is which
        // build you are looking at, and a label too dim to read is no answer.
        // It comes from the build rather than a literal here, because a literal
        // drifted a release behind the bundle once and made a correct install look
        // like a stale one.
        g.setColour (juce::Colour (0xff5f5949));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText ("v" RESONAPRO_VERSION_STRING "  -  adaptive resonance detection", 180, 32, 420, 15,
                    juce::Justification::left);

        g.setColour (juce::Colour (0xffe5dfd2));
        g.drawHorizontalLine (56, 0.0f, static_cast<float> (getWidth()));

        // ---- bottom control panel ----------------------------------------------
        auto bottomArea = getLocalBounds().removeFromBottom (186);
        g.setColour (juce::Colour (0xfff0ece2));
        g.fillRoundedRectangle (bottomArea.reduced (10, 6).toFloat(), 8.0f);
        g.setColour (juce::Colour (0xffdcd4c5));
        g.drawRoundedRectangle (bottomArea.reduced (10, 6).toFloat(), 8.0f, 1.0f);

        // ---- band hint ----------------------------------------------------------
        g.setFont (juce::FontOptions (9.0f));
        g.setColour (themeDim.withAlpha (0.85f));
    }

    void ResonaProAudioProcessorEditor::resized()
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (56).reduced (14, 8);

        // header right side: preset + bypass
        bypassButton.setBounds (header.removeFromRight (74).reduced (0, 2));
        header.removeFromRight (8);
        auto presetArea = header.removeFromRight (210);
        presetLabel.setBounds (presetArea.removeFromTop (11));
        presetBox.setBounds (presetArea.reduced (0, 1));

        // ---- second row: what the plugin is analysing ---------------------------
        auto row2 = area.removeFromTop (48).reduced (16, 6);
        auto profileArea = row2.removeFromLeft (200);
        vocalProfileLabel.setBounds (profileArea.removeFromTop (11));
        vocalProfileBox.setBounds (profileArea.reduced (0, 1));

        row2.removeFromLeft (10);
        auto qualityArea = row2.removeFromLeft (150);
        qualityLabel.setBounds (qualityArea.removeFromTop (11));
        qualityBox.setBounds (qualityArea.reduced (0, 1));

        row2.removeFromLeft (8);
        auto responseArea = row2.removeFromLeft (130);
        responseLabel.setBounds (responseArea.removeFromTop (11));
        responseBox.setBounds (responseArea.reduced (0, 1));

        // Four toggles. SOLO CUT sits beside DELTA because they answer the two
        // halves of the same question: DELTA is "what is the whole plug-in
        // taking?", SOLO CUT is "what is the de-esser taking, on its own?".
        row2.removeFromLeft (14);
        const int toggleWidth = juce::jlimit (56, 104, row2.getWidth() / 4);
        modeHardButton.setBounds (row2.removeFromLeft (toggleWidth).reduced (2, 4));
        midSideButton.setBounds (row2.removeFromLeft (toggleWidth).reduced (2, 4));
        deltaButton.setBounds (row2.removeFromLeft (toggleWidth).reduced (2, 4));
        soloCutButton.setBounds (row2.removeFromLeft (juce::jmin (toggleWidth, row2.getWidth())).reduced (2, 4));

        // ---- third row: learning, and the drawer handle -------------------------
        auto row3 = area.removeFromTop (48).reduced (18, 5);
        drawerButton.setBounds (row3.removeFromRight (112).reduced (2, 4));
        row3.removeFromRight (10);
        learnButton.setBounds (row3.removeFromLeft (86).reduced (2, 4));
        row3.removeFromLeft (5);
        applyLearnButton.setBounds (row3.removeFromLeft (100).reduced (2, 4));
        row3.removeFromLeft (5);
        undoLearnButton.setBounds (row3.removeFromLeft (96).reduced (2, 4));
        learnStatus.setBounds (row3.reduced (6, 0));

        // ---- knob row -----------------------------------------------------------
        auto bottomArea = area.removeFromBottom (196).reduced (20, 12);
        auto hintRow = bottomArea.removeFromBottom (26);

        matchButton.setBounds (hintRow.removeFromLeft (62).reduced (2, 3));
        hintRow.removeFromLeft (6);
        abButton.setBounds (hintRow.removeFromLeft (62).reduced (2, 3));
        hintRow.removeFromLeft (12);
        {
            auto dbArea = hintRow.removeFromLeft (118);
            deltaBandLabel.setBounds (dbArea.removeFromLeft (68));
            deltaBandBox.setBounds (dbArea.reduced (0, 2));
        }
        hintRow.removeFromLeft (12);
        copyBandsButton.setBounds (hintRow.removeFromLeft (54).reduced (2, 3));
        hintRow.removeFromLeft (4);
        pasteBandsButton.setBounds (hintRow.removeFromLeft (58).reduced (2, 3));
        infoLabel.setBounds (hintRow.reduced (4, 0));

        const int knobWidth = bottomArea.getWidth() / NumKnobs;
        for (int i = 0; i < NumKnobs; ++i)
        {
            auto colArea = bottomArea.removeFromLeft (knobWidth);
            knobLabels[i].setBounds (colArea.removeFromTop (15));
                knobs[i].setBounds (colArea.reduced (2, 0));
        }

        // ---- graph, and the drawer ----------------------------------------------
        // The graph takes the whole width unless the drawer is open. It is the
        // reason to use this plugin, so it gets the room.
        if (isDrawerOpen)
        {
            auto drawerArea = area.removeFromRight (214).reduced (8, 8);
            visualizer.setBounds (area.reduced (14, 6));

            drawerTitleLabel.setVisible (true);
            // Two kinds of control live in here: three frequency tilts and a
            // de-esser. The title says so, because nothing else did.
            drawerTitleLabel.setText ("FINE TUNE: TILTS & DE-ESS", juce::dontSendNotification);
            drawerTitleLabel.setBounds (drawerArea.removeFromTop (14));
            drawerArea.removeFromTop (6);

            const int knobRowH = 66;
            auto r1 = drawerArea.removeFromTop (knobRowH);
            auto r2 = drawerArea.removeFromTop (knobRowH);

            auto placePair = [] (juce::Rectangle<int> r, juce::Slider& s1, juce::Label& l1,
                                 juce::Slider& s2, juce::Label& l2)
            {
                const int w = r.getWidth() / 2;
                auto a = r.removeFromLeft (w);
                l1.setVisible (true); s1.setVisible (true);
                l1.setBounds (a.removeFromTop (13));
                s1.setBounds (a.reduced (2, 0));
                l2.setVisible (true); s2.setVisible (true);
                l2.setBounds (r.removeFromTop (13));
                s2.setBounds (r.reduced (2, 0));
            };
            placePair (r1, attackTiltSlider, attackTiltLabel, releaseTiltSlider, releaseTiltLabel);
            placePair (r2, detailTiltSlider, detailTiltLabel, sibilanceSlider, sibilanceLabel);

            // Note Motion used to sit here. It was removed: see the note in
            // Tests/DspTests.cpp. The drawer keeps only controls that have a
            // measurement behind them.
            drawerArea.removeFromTop (14);

            auto placeButtons = [&drawerArea] (juce::TextButton& b1, juce::TextButton& b2)
            {
                auto row = drawerArea.removeFromTop (28);
                const int w = row.getWidth() / 2;
                b1.setVisible (true);
                b2.setVisible (true);
                b1.setBounds (row.removeFromLeft (w).reduced (2, 1));
                b2.setBounds (row.reduced (2, 1));
            };
            placeButtons (iso226Button, lowBandButton);
            placeButtons (keyButton, resetBandsButton);
        }
        else
        {
            visualizer.setBounds (area.reduced (16, 6));

            drawerTitleLabel.setVisible (false);
            for (auto* s : { &attackTiltSlider, &releaseTiltSlider, &detailTiltSlider, &sibilanceSlider })
                s->setVisible (false);
            for (auto* l : { &attackTiltLabel, &releaseTiltLabel, &detailTiltLabel, &sibilanceLabel })
                l->setVisible (false);
            motionSlider.setVisible (false);
            motionLabel.setVisible (false);

            // These belong to the drawer, so they leave with it. Closing the
            // drawer hides controls; it never changes their values.
            iso226Button.setVisible (false);
            lowBandButton.setVisible (false);
            keyButton.setVisible (false);
            resetBandsButton.setVisible (false);
        }
    }
}
