#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Version.h"

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

        setupCombo (qualityBox, qualityLabel, "QUALITY",
                    { "Low Latency (1k)", "Standard (2k)", "High (4k)", "Ultra (8k)" });

        setupCombo (responseBox, responseLabel, "RESPONSE",
                    { "Eco (2x)", "Standard (4x)", "Fine (8x)" });

        // ---- knobs --------------------------------------------------------------
        setupRotarySlider (knobs[0], knobLabels[0], "DEPTH", themePrimary);
        setupRotarySlider (knobs[1], knobLabels[1], "DETAIL", themePrimary);
        setupRotarySlider (knobs[2], knobLabels[2], "SELECT", themeSecondary);
        setupRotarySlider (knobs[3], knobLabels[3], "TRANSIENT", themeSecondary);
        setupRotarySlider (knobs[4], knobLabels[4], "MAX CUT", themeAccent);
        setupRotarySlider (knobs[5], knobLabels[5], "ATTACK", themeAccent);
        setupRotarySlider (knobs[6], knobLabels[6], "RELEASE", themeAccent);
        setupRotarySlider (knobs[7], knobLabels[7], "STEREO LINK", themeGreen);
        setupRotarySlider (knobs[8], knobLabels[8], "MIX", themeGreen);
        setupRotarySlider (knobs[9], knobLabels[9], "OUTPUT", themeRose);

        // ---- toggles ------------------------------------------------------------
        setupToggle (bypassButton, "BYPASS");
        setupToggle (iso226Button, "EAR GUARD");
        setupToggle (modeHardButton, "HARD");
        setupToggle (midSideButton, "MID/SIDE");
        setupToggle (deltaButton, "DELTA");
        setupToggle (keyButton, "EXT KEY");
        setupToggle (lowBandButton, "LOW DETAIL");
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
            applyLearnButton.setEnabled (false);
            learnStatus.setText ("Applied to " + juce::String (learnedCandidates.size()) + " focus bands",
                                 juce::dontSendNotification);
        };
        addAndMakeVisible (applyLearnButton);

        // ---- fine tuning drawer controls ----------------------------------------
        setupRotarySlider (attackTiltSlider, attackTiltLabel, "ATK TILT", themeAccent);
        setupRotarySlider (releaseTiltSlider, releaseTiltLabel, "REL TILT", themeAccent);
        setupRotarySlider (detailTiltSlider, detailTiltLabel, "DETAIL TILT", themePrimary);
        setupRotarySlider (sibilanceSlider, sibilanceLabel, "SIBILANCE", themeGreen);

        drawerTitleLabel.setText ("SURGICAL TILT & DE-ESS", juce::dontSendNotification);
        drawerTitleLabel.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        drawerTitleLabel.setColour (juce::Label::textColourId, themePrimary);
        drawerTitleLabel.setJustificationType (juce::Justification::centred);
        addChildComponent (drawerTitleLabel);

        drawerButton.setButtonText ("FINE TUNE ◂");
        drawerButton.setClickingTogglesState (true);
        drawerButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc9762e));
        drawerButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        drawerButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        drawerButton.onClick = [this]
        {
            isDrawerOpen = ! isDrawerOpen;
            drawerButton.setButtonText (isDrawerOpen ? "FINE TUNE ◂" : "FINE TUNE ▸");
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

        qualityAttachment      = std::make_unique<ComboBoxAttachment> (p.apvts, "quality", qualityBox);
        responseAttachment     = std::make_unique<ComboBoxAttachment> (p.apvts, "response", responseBox);
        vocalProfileAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, "vocalProfile", vocalProfileBox);

        bypassAttachment    = std::make_unique<ButtonAttachment> (p.apvts, "bypass", bypassButton);
        iso226Attachment    = std::make_unique<ButtonAttachment> (p.apvts, "iso226", iso226Button);
        modeHardAttachment  = std::make_unique<ButtonAttachment> (p.apvts, "modeHard", modeHardButton);
        midSideAttachment   = std::make_unique<ButtonAttachment> (p.apvts, "midSide", midSideButton);
        deltaAttachment     = std::make_unique<ButtonAttachment> (p.apvts, "deltaListen", deltaButton);
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
            { &deltaButton,     "Plays only what is being removed, at natural level and timing." },
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
        attackTiltSlider.setTooltip ("Attack Tilt: Fast sub-ms attack on high sibilance/transients, slower response on low boom.");
        releaseTiltSlider.setTooltip ("Release Tilt: Fast air recovery on top end (preserves breath), longer hold on low body.");
        detailTiltSlider.setTooltip ("Detail Tilt: Surgical narrow notches in highs, broad smooth notches in lows.");
        sibilanceSlider.setTooltip ("Dedicated musical sibilance de-esser: dynamically smooths harsh S and T consonants.");
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
    }

    ResonaProAudioProcessorEditor::~ResonaProAudioProcessorEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void ResonaProAudioProcessorEditor::timerCallback()
    {
        if (! learning) return;
        std::array<float, ResonaProAudioProcessor::ScopeSize> mag {}, reduction {}, baseline {};
        float reference = 0.0f;
        uint64_t sequence = 0;
        processorRef.getVisualizerData (mag, reduction, baseline, reference, &sequence);
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
        stopTimer();
        learning = false;
        learnButton.setButtonText ("LEARN");
        learnedCandidates = learner.suggestions();
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
        learnStatus.setText ("Suggested Hz: " + review + " — press APPLY to accept",
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
        g.drawText ("Drag cue nodes UP to focus suppression on problem areas. Scroll, Alt+Drag or drag side wings to adjust Width (Q). Double-click to toggle/add.",
                    22, getHeight() - 184, getWidth() - 44, 12, juce::Justification::left);
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

        // ---- second row: engines + character toggles ----------------------------
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

        row2.removeFromLeft (14);
        const int toggleWidth = juce::jlimit (58, 92, row2.getWidth() / 5);
        iso226Button.setBounds (row2.removeFromLeft (toggleWidth).reduced (2, 4));
        modeHardButton.setBounds (row2.removeFromLeft (toggleWidth).reduced (2, 4));
        midSideButton.setBounds (row2.removeFromLeft (toggleWidth).reduced (2, 4));
        deltaButton.setBounds (row2.removeFromLeft (toggleWidth).reduced (2, 4));
        resetBandsButton.setBounds (row2.removeFromLeft (juce::jmin (toggleWidth, row2.getWidth())).reduced (2, 4));

        // ---- processing and learning row ----------------------------------------
        auto row3 = area.removeFromTop (48).reduced (18, 5);
        drawerButton.setBounds (row3.removeFromRight (102).reduced (2, 4));
        row3.removeFromRight (8);
        keyButton.setBounds (row3.removeFromLeft (86).reduced (2, 4));
        row3.removeFromLeft (5);
        lowBandButton.setBounds (row3.removeFromLeft (103).reduced (2, 4));
        row3.removeFromLeft (4);
        motionLabel.setBounds (row3.removeFromLeft (82));
        motionSlider.setBounds (row3.removeFromLeft (95));
        row3.removeFromLeft (6);
        learnButton.setBounds (row3.removeFromLeft (84).reduced (2, 4));
        row3.removeFromLeft (4);
        applyLearnButton.setBounds (row3.removeFromLeft (84).reduced (2, 4));
        learnStatus.setBounds (row3.reduced (4, 0));

        // ---- bottom knob row ----------------------------------------------------
        auto bottomArea = area.removeFromBottom (186).reduced (20, 14);
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

        // ---- visualiser and collapsible side drawer ----------------------------
        if (isDrawerOpen)
        {
            auto drawerArea = area.removeFromRight (204).reduced (8, 8);
            visualizer.setBounds (area.reduced (14, 6));

            drawerTitleLabel.setVisible (true);
            drawerTitleLabel.setBounds (drawerArea.removeFromTop (16));

            drawerArea.removeFromTop (8);
            auto rowTop = drawerArea.removeFromTop (drawerArea.getHeight() / 2 - 4);
            auto rowBot = drawerArea;

            const int dKnobW = rowTop.getWidth() / 2;

            auto atkArea = rowTop.removeFromLeft (dKnobW);
            attackTiltLabel.setVisible (true);
            attackTiltSlider.setVisible (true);
            attackTiltLabel.setBounds (atkArea.removeFromTop (13));
            attackTiltSlider.setBounds (atkArea.reduced (2, 0));

            auto relArea = rowTop;
            releaseTiltLabel.setVisible (true);
            releaseTiltSlider.setVisible (true);
            releaseTiltLabel.setBounds (relArea.removeFromTop (13));
            releaseTiltSlider.setBounds (relArea.reduced (2, 0));

            auto detArea = rowBot.removeFromLeft (dKnobW);
            detailTiltLabel.setVisible (true);
            detailTiltSlider.setVisible (true);
            detailTiltLabel.setBounds (detArea.removeFromTop (13));
            detailTiltSlider.setBounds (detArea.reduced (2, 0));

            auto sibArea = rowBot;
            sibilanceLabel.setVisible (true);
            sibilanceSlider.setVisible (true);
            sibilanceLabel.setBounds (sibArea.removeFromTop (13));
            sibilanceSlider.setBounds (sibArea.reduced (2, 0));
        }
        else
        {
            visualizer.setBounds (area.reduced (16, 6));

            drawerTitleLabel.setVisible (false);
            attackTiltLabel.setVisible (false);
            attackTiltSlider.setVisible (false);
            releaseTiltLabel.setVisible (false);
            releaseTiltSlider.setVisible (false);
            detailTiltLabel.setVisible (false);
            detailTiltSlider.setVisible (false);
            sibilanceLabel.setVisible (false);
            sibilanceSlider.setVisible (false);
        }
    }
}
