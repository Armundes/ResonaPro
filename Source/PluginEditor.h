#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UI/CustomLookAndFeel.h"
#include "UI/SpectralVisualizer.h"
#include "DSP/LearnAnalyzer.h"

namespace ResonaPro
{
    class ResonaProAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Timer
    {
    public:
        explicit ResonaProAudioProcessorEditor (ResonaProAudioProcessor&);
        ~ResonaProAudioProcessorEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void setupRotarySlider (juce::Slider& slider, juce::Label& label, const juce::String& text,
                                juce::Colour accent);
        void setupToggle (juce::TextButton& button, const juce::String& text);
        void setupCombo (juce::ComboBox& box, juce::Label& label, const juce::String& text,
                         const juce::StringArray& items);

        ResonaProAudioProcessor& processorRef;
        CustomLookAndFeel customLnF;

        /** Shows the tooltips attached to the controls below. */
        juce::TooltipWindow tooltipWindow { this, 700 };

        SpectralVisualizer visualizer;

        juce::ComboBox presetBox, qualityBox, responseBox, vocalProfileBox;
        juce::Label presetLabel, qualityLabel, responseLabel, vocalProfileLabel;
        juce::ComboBox deltaBandBox;
        juce::Label    deltaBandLabel;

        juce::TextButton bypassButton { "BYPASS" },
                         iso226Button { "ISO 226" },
                         modeHardButton { "HARD" },
                         midSideButton { "MID/SIDE" },
                         deltaButton { "DELTA" },
                         soloCutButton { "SOLO CUT" },
                         resetBandsButton { "RESET BANDS" },
                         matchButton { "MATCH" },
                         copyBandsButton { "COPY" },
                         pasteBandsButton { "PASTE" },
                         abButton { "A/B" },
                         keyButton { "EXT KEY" },
                         lowBandButton { "LOW DETAIL" },
                         learnButton { "LEARN" },
                         applyLearnButton { "APPLY" },
                         undoLearnButton { "UNDO" };
        juce::Slider motionSlider;
        juce::Label motionLabel, learnStatus;

        juce::TextButton drawerButton { "FINE TUNE ▸" };

        /** The drawer holds the controls a user reaches for now and then. It is
            closed by default so the first impression is the graph and the eight
            controls that matter, and its state carries across editor reopen
            because reopening a plugin should not cost the same two clicks twice.
        */
        static bool& drawerPreference() { static bool open = false; return open; }
        bool isDrawerOpen = drawerPreference();

        juce::Slider attackTiltSlider, releaseTiltSlider, detailTiltSlider, sibilanceSlider;
        juce::Label  attackTiltLabel, releaseTiltLabel, detailTiltLabel, sibilanceLabel;
        juce::Label  drawerTitleLabel;
        LearnAnalyzer learner;
        std::vector<LearnAnalyzer::Candidate> learnedCandidates;
        bool learning = false;
        uint64_t lastLearnSequence = 0;
        void timerCallback() override;
        void finishLearning();

        // Phase 8: what the take's own sibilance looked like. Kept until the
        // user presses APPLY, so nothing changes during capture.
        SibilanceDeEsser::Profile learnedSibilance;

        // The full read of the take, and the values LEARN wrote so they can be
        // taken back. See docs/LEARN-DESIGN.md.
        LearnAnalyzer::TakeProfile learnedProfile;
        bool  learnApplied = false;
        std::vector<std::pair<juce::String, float>> learnUndo;

        /** Runtime information the user cannot otherwise see: the true frequency
            resolution of the current transform and the latency it costs.
        */
        juce::Label infoLabel;

        /** A/B comparison. Two stored states, swapped by the button. */
        void swapAB();
        juce::MemoryBlock slotA, slotB;
        bool usingSlotA = true;
        bool slotsInitialised = false;

        /** Reports the transform's real frequency resolution and the latency it
            costs, which the Quality labels alone cannot convey at every sample rate.
        */
        void updateInfoLabel();

        static constexpr int NumKnobs = 10;
        juce::Slider knobs[NumKnobs];
        juce::Label  knobLabels[NumKnobs];

        using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        std::unique_ptr<SliderAttachment> depthAttachment, detailAttachment, selectivityAttachment,
                                          transientAttachment, maxCutAttachment, attackAttachment,
                                          releaseAttachment, mixAttachment, outputAttachment,
                                          stereoLinkAttachment;

        std::unique_ptr<ButtonAttachment> bypassAttachment, iso226Attachment, modeHardAttachment,
                                          midSideAttachment, deltaAttachment, soloCutAttachment,
                                          matchAttachment;
        std::unique_ptr<ButtonAttachment> keyAttachment, lowBandAttachment;
        std::unique_ptr<SliderAttachment> motionAttachment;
        std::unique_ptr<SliderAttachment> attackTiltAttachment, releaseTiltAttachment,
                                          detailTiltAttachment, sibilanceAttachment;

        std::unique_ptr<ComboBoxAttachment> qualityAttachment, responseAttachment, vocalProfileAttachment,
                                             deltaBandAttachment;

        /** Combo-box ID (1-based) of the preset whose values match the parameter
            defaults, i.e. "Lead Vocal - Gentle".
        */
        int lastPreset = 2;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonaProAudioProcessorEditor)
    };
}
