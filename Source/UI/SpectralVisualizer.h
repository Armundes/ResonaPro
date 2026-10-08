#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>

namespace ResonaPro
{
    class ResonaProAudioProcessor;

    class SpectralVisualizer : public juce::Component, public juce::Timer
    {
    public:
        SpectralVisualizer(ResonaProAudioProcessor& proc, juce::AudioProcessorValueTreeState& vts)
            : processor(proc), apvts(vts)
        {
            startTimerHz(60);
            smoothedMag.fill(-100.0f);
            smoothedRed.fill(0.0f);
            smoothedBaseline.fill(-100.0f);
            smoothedWindowHz.fill(0.0f);
            numericEntry.setMultiLine (false);
            numericEntry.setTextToShowWhenEmpty ("Hz  dB  Q", juce::Colours::grey);
            numericEntry.onReturnKey = [this]
            {
                const auto tokens = juce::StringArray::fromTokens (numericEntry.getText(), " ,;", "");
                if (editingNode >= 0 && tokens.size() == 3)
                {
                    bool valid = true;
                    for (const auto& token : tokens)
                        valid &= token.containsOnly ("0123456789+-. ")
                                 && token.containsAnyOf ("0123456789");
                    if (! valid) { numericEntry.setVisible (false); return; }
                    const auto id = juce::String (editingNode + 1);
                    const float values[] = { tokens[0].getFloatValue(), tokens[1].getFloatValue(),
                                             tokens[2].getFloatValue() };
                    for (float value : values)
                        if (! std::isfinite (value)) { numericEntry.setVisible (false); return; }
                    const char* prefixes[] = { "eq_freq_", "eq_gain_", "eq_q_" };
                    const float minimum[] = { 20.0f, -24.0f, 0.1f };
                    const float maximum[] = { 20000.0f, 24.0f, 8.0f };
                    for (int i = 0; i < 3; ++i)
                        if (auto* param = apvts.getParameter (juce::String (prefixes[i]) + id))
                            param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 (
                                std::clamp (values[i], minimum[i], maximum[i])));
                }
                numericEntry.setVisible (false);
            };
            numericEntry.onEscapeKey = [this] { numericEntry.setVisible (false); };
            addChildComponent (numericEntry);
        }

        ~SpectralVisualizer() override
        {
            stopTimer();
        }

        void timerCallback() override
        {
            std::array<float, ResonaProAudioProcessor::ScopeSize> rawMag;
            std::array<float, ResonaProAudioProcessor::ScopeSize> rawRed;
            std::array<float, ResonaProAudioProcessor::ScopeSize> rawBase;
            std::array<float, ResonaProAudioProcessor::ScopeSize> rawWindowHz;
            float refProm = 0.0f;
            processor.getVisualizerData (rawMag, rawRed, rawBase, rawWindowHz, refProm);

            for (size_t i = 0; i < numPoints; ++i)
            {
                // Nothing non-finite gets past this point.
                //
                // These are recursive filters: each value is multiplied by a
                // fraction and added back in every frame. A single -inf or NaN
                // therefore never washes out -- it persists for the life of the
                // editor, and the display goes dead while the audio carries on
                // working normally. A gain of exactly zero converted to dB gives
                // -inf, which is how a solo monitor managed to freeze the graph
                // permanently. The engine no longer produces that value, and this
                // is the second line of defence so the next one cannot either.
                const float mIn = std::isfinite (rawMag[i])      ? rawMag[i]      : -100.0f;
                const float rIn = std::isfinite (rawRed[i])      ? rawRed[i]      :    0.0f;
                const float bIn = std::isfinite (rawBase[i])     ? rawBase[i]     : -100.0f;
                const float wIn = std::isfinite (rawWindowHz[i]) ? rawWindowHz[i] :    0.0f;

                // Asymmetric smoothing: fall fast, rise slowly, so the display reads
                // like a real analyser instead of flickering.
                const float aMag = mIn > smoothedMag[i] ? 0.55f : 0.20f;
                const float aRed = rIn < smoothedRed[i] ? 0.55f : 0.20f;
                smoothedMag[i]      = (1.0f - aMag) * smoothedMag[i] + aMag * mIn;
                smoothedRed[i]      = (1.0f - aRed) * smoothedRed[i] + aRed * rIn;
                smoothedBaseline[i] = 0.75f * smoothedBaseline[i] + 0.25f * bIn;
                smoothedWindowHz[i] = 0.80f * smoothedWindowHz[i] + 0.20f * wIn;
            }

            // Smooth across frequency as well as in time. Per-bin smoothing alone
            // leaves every harmonic peak as its own spike, which reads as a comb of
            // needles rather than the shape of a voice. Two passes of a binomial
            // kernel give a 9-point span: wide enough to read as a contour, narrow
            // enough to keep the formant structure a user needs to aim at.
            for (int pass = 0; pass < 2; ++pass)
            {
                // The reduction curve is included: the detector applies one gain
                // across a band and then jumps, so the raw curve is a staircase.
                // Smoothing it is what makes it read as a shape.
                for (auto* curve : { &smoothedMag, &smoothedBaseline, &smoothedRed })
                {
                    auto& v = *curve;
                    auto prev = v;
                    for (size_t i = 1; i + 1 < numPoints; ++i)
                        v[i] = 0.25f * prev[i - 1] + 0.5f * prev[i] + 0.25f * prev[i + 1];
                }
            }

            referencePromDb = refProm;

            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();

            // Soft Matte Slate Background
            juce::ColourGradient bgGrad(juce::Colour(0xffe9e3d7), 0, 0,
                                        juce::Colour(0xffe2dbcd), 0, bounds.getBottom(), false);
            g.setGradientFill(bgGrad);
            g.fillRoundedRectangle(bounds, 6.0f);

            // Subtle Soft Border
            g.setColour(juce::Colour(0xffd2caba));
            g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

            drawGrid(g, bounds);
            drawMagnitudeSpectrum(g, bounds);
            drawAnalysisWindow(g, bounds);
            drawReductionCurve(g, bounds);
            drawDetectedResonances(g, bounds);
            drawSidechainEQCurve(g, bounds);
            drawEQNodes(g, bounds);
            drawCursorReadout(g, bounds);
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            currentMousePos = e.getPosition();
            isMouseInside = true;

            hoveredWing = 0;
            hoveredNode = findNodeOrWingNear(e.getPosition(), hoveredWing);

            if (hoveredWing != 0)
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            else if (hoveredNode != -1)
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            else
                setMouseCursor(juce::MouseCursor::NormalCursor);

            repaint();
        }

        void mouseExit(const juce::MouseEvent&) override
        {
            isMouseInside = false;
            if (hoveredNode != -1 || hoveredWing != 0)
            {
                hoveredNode = -1;
                hoveredWing = 0;
                setMouseCursor(juce::MouseCursor::NormalCursor);
            }
            repaint();
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            int wing = 0;
            int hitNode = findNodeOrWingNear(e.getPosition(), wing);

            if (e.mods.isRightButtonDown() && hitNode >= 0)
            {
                showNodeContextMenu (hitNode, e.getScreenPosition());
                return;
            }

            if (hitNode >= 0)
            {
                selectedNode = hitNode;
                isDragging = true;
                dragStartPos = e.getPosition();
                auto bStr = juce::String(selectedNode + 1);
                dragStartFreq = apvts.getRawParameterValue("eq_freq_" + bStr)->load();
                dragStartGain = apvts.getRawParameterValue("eq_gain_" + bStr)->load();
                dragStartQ    = apvts.getRawParameterValue("eq_q_" + bStr)->load();

                if (wing != 0 || e.mods.isAltDown() || e.mods.isCommandDown())
                    dragMode = DragMode::QOnly;
                else
                    dragMode = DragMode::Position;

                repaint();
            }
            else
            {
                selectedNode = -1;
                dragMode = DragMode::None;
                repaint();
            }
        }

        void mouseDoubleClick(const juce::MouseEvent& e) override
        {
            int wing = 0;
            int node = findNodeOrWingNear(e.getPosition(), wing);
            auto bounds = getLocalBounds().toFloat();

            if (node != -1)
            {
                // Double click on node toggles enable/disable
                auto bStr = juce::String(node + 1);
                if (auto* enParam = apvts.getParameter("eq_enable_" + bStr))
                {
                    bool cur = apvts.getRawParameterValue("eq_enable_" + bStr)->load() > 0.5f;
                    enParam->setValueNotifyingHost(cur ? 0.0f : 1.0f);
                }
            }
            else
            {
                // Double click in empty space: finds first disabled node and activates it at clicked position
                for (int b = 0; b < 8; ++b)
                {
                    auto bStr = juce::String(b + 1);
                    bool isEnabled = apvts.getRawParameterValue("eq_enable_" + bStr)->load() > 0.5f;
                    if (!isEnabled)
                    {
                        float freq = xToFreq(static_cast<float>(e.x), bounds.getWidth());
                        float gain = yToGain(static_cast<float>(e.y), bounds.getHeight());

                        if (auto* enParam = apvts.getParameter("eq_enable_" + bStr))
                            enParam->setValueNotifyingHost(1.0f);
                        if (auto* fParam = apvts.getParameter("eq_freq_" + bStr))
                            fParam->setValueNotifyingHost(fParam->getNormalisableRange().convertTo0to1(freq));
                        if (auto* gParam = apvts.getParameter("eq_gain_" + bStr))
                            gParam->setValueNotifyingHost(gParam->getNormalisableRange().convertTo0to1(gain));
                        selectedNode = b;
                        break;
                    }
                }
            }
            repaint();
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            currentMousePos = e.getPosition();
            isMouseInside = true;
            if (isDragging && selectedNode != -1)
            {
                auto bounds = getLocalBounds().toFloat();
                auto bStr = juce::String(selectedNode + 1);

                if (dragMode == DragMode::QOnly || e.mods.isAltDown() || e.mods.isCommandDown())
                {
                    // Dragging to adjust Q (horizontal drag changes bandwidth: right=narrower/higher Q, left=wider/lower Q)
                    float deltaX = static_cast<float>(e.x - dragStartPos.x);
                    float qFactor = std::pow(2.0f, deltaX * 0.015f);
                    float newQ = std::clamp(dragStartQ * qFactor, 0.1f, 8.0f);

                    if (auto* qParam = apvts.getParameter("eq_q_" + bStr))
                        qParam->setValueNotifyingHost(qParam->getNormalisableRange().convertTo0to1(newQ));
                }
                else
                {
                    // Regular frequency and gain drag
                    float freq = xToFreq(static_cast<float>(e.x), bounds.getWidth());
                    float gain = yToGain(static_cast<float>(e.y), bounds.getHeight());

                    if (e.mods.isShiftDown())
                    {
                        freq = 1000.0f * std::pow (2.0f, std::round (6.0f * std::log2 (freq / 1000.0f)) / 6.0f);
                        gain = std::round (gain);
                    }

                    if (auto* fParam = apvts.getParameter("eq_freq_" + bStr))
                        fParam->setValueNotifyingHost(fParam->getNormalisableRange().convertTo0to1(freq));
                    if (auto* gParam = apvts.getParameter("eq_gain_" + bStr))
                        gParam->setValueNotifyingHost(gParam->getNormalisableRange().convertTo0to1(gain));
                }

                repaint();
            }
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            isDragging = false;
            dragMode = DragMode::None;
            repaint();
        }

        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            int wing = 0;
            int targetNode = findNodeOrWingNear(e.getPosition(), wing);
            if (targetNode == -1)
                targetNode = selectedNode;

            if (targetNode != -1)
            {
                auto bStr = juce::String(targetNode + 1);
                if (auto* qParam = apvts.getParameter("eq_q_" + bStr))
                {
                    float curQ = apvts.getRawParameterValue("eq_q_" + bStr)->load();
                    float delta = (std::abs(wheel.deltaY) > 0.0001f) ? wheel.deltaY : wheel.deltaX;
                    float factor = std::pow(2.0f, delta * 1.5f);
                    float newQ = std::clamp(curQ * factor, 0.1f, 8.0f);
                    qParam->setValueNotifyingHost(qParam->getNormalisableRange().convertTo0to1(newQ));
                    selectedNode = targetNode;
                    repaint();
                }
            }
        }

    public:
        void showNodeContextMenu (int nodeIndex, juce::Point<int> screenPos)
        {
            auto bStr = juce::String (nodeIndex + 1);
            int curType = static_cast<int> (apvts.getRawParameterValue ("eq_type_" + bStr) ? apvts.getRawParameterValue ("eq_type_" + bStr)->load() : 0.0f);
            bool isEnabled = apvts.getRawParameterValue ("eq_enable_" + bStr)->load() > 0.5f;

            juce::PopupMenu menu;
            juce::PopupMenu typeMenu;
            typeMenu.addItem (1, "Bell", true, curType == 0);
            typeMenu.addItem (2, "Low Cut (High Pass)", true, curType == 1);
            typeMenu.addItem (3, "High Cut (Low Pass)", true, curType == 2);
            typeMenu.addItem (4, "Low Shelf", true, curType == 3);
            typeMenu.addItem (5, "High Shelf", true, curType == 4);
            typeMenu.addItem (6, "Band Pass", true, curType == 5);
            menu.addSubMenu ("Filter Type", typeMenu);

            juce::PopupMenu slopeMenu;
            slopeMenu.addItem (10, "6 dB/oct  (Gentle, Q = 0.50)");
            slopeMenu.addItem (11, "12 dB/oct (Standard, Q = 0.71)");
            slopeMenu.addItem (12, "24 dB/oct (Steep, Q = 1.41)");
            slopeMenu.addItem (13, "48 dB/oct (Brickwall, Q = 2.83)");
            slopeMenu.addItem (14, "96 dB/oct (Ultra-Steep, Q = 5.66)");
            menu.addSubMenu ("Slope / Steepness Preset", slopeMenu);

            menu.addSeparator();
            menu.addItem (20, "Reset Sensitivity to 0.0 dB (Neutral)");
            menu.addItem (21, isEnabled ? "Disable Band" : "Enable Band");
            menu.addSeparator();
            menu.addItem (30, "Type Exact Values (Hz, dB, Q)...");

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)),
                [this, nodeIndex, bStr, isEnabled](int result)
                {
                    if (result >= 1 && result <= 6)
                    {
                        if (auto* tParam = apvts.getParameter ("eq_type_" + bStr))
                            tParam->setValueNotifyingHost (tParam->getNormalisableRange().convertTo0to1 (static_cast<float> (result - 1)));
                    }
                    else if (result >= 10 && result <= 14)
                    {
                        const float presetQs[] = { 0.50f, 0.7071f, 1.4142f, 2.8284f, 5.6568f };
                        float chosenQ = presetQs[result - 10];
                        if (auto* qParam = apvts.getParameter ("eq_q_" + bStr))
                            qParam->setValueNotifyingHost (qParam->getNormalisableRange().convertTo0to1 (chosenQ));
                    }
                    else if (result == 20)
                    {
                        if (auto* gParam = apvts.getParameter ("eq_gain_" + bStr))
                            gParam->setValueNotifyingHost (gParam->getNormalisableRange().convertTo0to1 (0.0f));
                    }
                    else if (result == 21)
                    {
                        if (auto* enParam = apvts.getParameter ("eq_enable_" + bStr))
                            enParam->setValueNotifyingHost (isEnabled ? 0.0f : 1.0f);
                    }
                    else if (result == 30)
                    {
                        editingNode = nodeIndex;
                        const auto id = juce::String (editingNode + 1);
                        const float f = apvts.getRawParameterValue ("eq_freq_" + id)->load();
                        const float g = apvts.getRawParameterValue ("eq_gain_" + id)->load();
                        const float q = apvts.getRawParameterValue ("eq_q_" + id)->load();
                        numericEntry.setText (juce::String (f, 0) + "  " + juce::String (g, 1) + "  " + juce::String (q, 2), false);
                        auto nodeX = freqToX (f, getLocalBounds().toFloat().getWidth());
                        auto nodeY = gainToY (g, getLocalBounds().toFloat().getHeight());
                        numericEntry.setBounds (std::clamp (static_cast<int>(nodeX) - 75, 0, std::max (0, getWidth() - 160)),
                                                std::clamp (static_cast<int>(nodeY) - 24, 0, std::max (0, getHeight() - 28)), 160, 28);
                        numericEntry.setVisible (true);
                        numericEntry.grabKeyboardFocus();
                        numericEntry.selectAll();
                    }
                    repaint();
                });
        }

    private:
        enum class DragMode { None, Position, QOnly };
        DragMode dragMode = DragMode::None;
        juce::Point<int> dragStartPos;
        float dragStartFreq = 1000.0f;
        float dragStartGain = 0.0f;
        float dragStartQ    = 1.0f;

        int hoveredNode = -1;
        int hoveredWing = 0; // -1 = left wing, +1 = right wing, 0 = none

        juce::TextEditor numericEntry;
        int editingNode = -1;
        juce::Point<int> currentMousePos;
        bool isMouseInside = false;

        void drawCursorReadout (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            if (! isMouseInside && ! isDragging)
                return;

            const float curX = static_cast<float> (currentMousePos.x);
            const float curY = static_cast<float> (currentMousePos.y);

            if (curX < bounds.getX() || curX > bounds.getRight() || curY < bounds.getY() || curY > bounds.getBottom())
                return;

            const float curFreq = xToFreq (curX, bounds.getWidth());
            const float curGain = yToGain (curY, bounds.getHeight());

            juce::String text;
            if (selectedNode >= 0 || hoveredNode >= 0)
            {
                const int b = (selectedNode >= 0) ? selectedNode : hoveredNode;
                const auto bStr = juce::String (b + 1);
                const float nodeF = apvts.getRawParameterValue ("eq_freq_" + bStr)->load();
                const float nodeG = apvts.getRawParameterValue ("eq_gain_" + bStr)->load();
                const float nodeQ = apvts.getRawParameterValue ("eq_q_" + bStr)->load();
                const int typeIdx = static_cast<int> (apvts.getRawParameterValue ("eq_type_" + bStr) ? apvts.getRawParameterValue ("eq_type_" + bStr)->load() : 0.0f);
                const char* typeNames[] = { "Bell", "Low Cut", "High Cut", "Low Shelf", "High Shelf", "Band Pass" };
                const char* tName = (typeIdx >= 0 && typeIdx < 6) ? typeNames[typeIdx] : "Bell";

                juce::String fStr = nodeF >= 1000.0f ? juce::String (nodeF / 1000.0f, (nodeF >= 10000.0f ? 1 : 2)) + " kHz"
                                                     : juce::String (std::round (nodeF), 0) + " Hz";
                juce::String gStr = (nodeG >= 0.0f ? "+" : "") + juce::String (nodeG, 1) + " dB";
                juce::String qStr = "Q " + juce::String (nodeQ, 2);

                text = "Band " + bStr + " (" + tName + ") • " + fStr + " • " + gStr + " • " + qStr;
            }
            else
            {
                juce::String fStr = curFreq >= 1000.0f ? juce::String (curFreq / 1000.0f, (curFreq >= 10000.0f ? 1 : 2)) + " kHz"
                                                       : juce::String (std::round (curFreq), 0) + " Hz";
                juce::String gStr = (curGain >= 0.0f ? "+" : "") + juce::String (curGain, 1) + " dB";
                text = fStr + "  •  " + gStr;
            }

            g.setFont (juce::FontOptions (10.5f).withStyle ("Bold"));
            juce::GlyphArrangement ga;
            ga.addLineOfText (juce::FontOptions (10.5f).withStyle ("Bold"), text, 0.0f, 0.0f);
            const int textWidth = static_cast<int> (ga.getBoundingBox (0, -1, true).getWidth() + 16.0f);
            const int badgeHeight = 20;

            int badgeX = static_cast<int> (curX) + 12;
            int badgeY = static_cast<int> (curY) - 24;

            if (badgeX + textWidth > bounds.getRight() - 6)
                badgeX = static_cast<int> (curX) - textWidth - 10;
            if (badgeY < bounds.getY() + 6)
                badgeY = static_cast<int> (curY) + 14;

            juce::Rectangle<float> badgeRect (static_cast<float> (badgeX), static_cast<float> (badgeY),
                                             static_cast<float> (textWidth), static_cast<float> (badgeHeight));

            g.setColour (juce::Colour (0xee211e1c));
            g.fillRoundedRectangle (badgeRect, 4.0f);
            g.setColour (juce::Colour (0x99c9762e));
            g.drawRoundedRectangle (badgeRect, 4.0f, 1.0f);

            g.setColour (juce::Colour (0xfff6f2e9));
            g.drawText (text, badgeRect, juce::Justification::centred, false);
        }

        void drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            g.setFont(juce::FontOptions(10.0f));

            const float freqs[] = { 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f };
            const char* labels[] = { "50", "100", "250", "500", "1k", "2.5k", "5k", "10k", "20k" };

            for (size_t i = 0; i < 9; ++i)
            {
                float x = freqToX(freqs[i], bounds.getWidth());
                g.setColour(juce::Colour(0xffcfc7b6));
                g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
                g.setColour(juce::Colour(0xff7f7867));
                const int labelX = std::clamp (static_cast<int> (x - 15), 1, std::max (1, getWidth() - 31));
                g.drawText(labels[i], labelX, static_cast<int>(bounds.getBottom() - 14), 30, 12, juce::Justification::centred);
            }

            const float dBs[] = { 18.0f, 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f };
            for (float db : dBs)
            {
                float y = gainToY(db, bounds.getHeight());
                g.setColour(db == 0.0f ? juce::Colour(0xffcdc5b4) : juce::Colour(0xffdcd4c5));
                g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
            }
        }

        /** Display-only tilt compensation.
            Music and speech fall by roughly 4.5 dB per octave, so an untilted
            spectrum is a slope down to the right and reads as if the top end were
            missing. Lifting each octave by a fixed amount flattens the display
            without touching a sample of the audio.

            The point-to-frequency mapping matches the processor: a log axis from
            20 Hz to 20 kHz across the scope points.
        */
        static float displayTiltDb (int i)
        {
            constexpr float dbPerOctave = 4.5f;
            const float norm = float (i) / float (numPoints - 1);
            const float freq = 20.0f * std::pow (1000.0f, norm);
            return dbPerOctave * std::log2 (juce::jmax (1.0f, freq) / 1000.0f);
        }

        /** Build a Catmull-Rom spline through the sampled curve.
            Straight segments between 256 points read as a staircase wherever the
            spectrum moves quickly, which is the part a user is looking at. A
            spline through the same samples reads as a shape, and costs one cubic
            per segment.
        */
        template <typename ValueFn>
        static juce::Path buildSmoothPath (int count, ValueFn valueAt,
                                           float x0, float dx,
                                           float yMin, float yMax)
        {
            juce::Path path;
            if (count < 2)
                return path;

            auto point = [&] (int i)
            {
                const int c = juce::jlimit (0, count - 1, i);
                return juce::Point<float> (x0 + float (c) * dx,
                                           juce::jlimit (yMin, yMax, valueAt (c)));
            };

            path.startNewSubPath (point (0));
            for (int i = 0; i < count - 1; ++i)
            {
                const auto p0 = point (i - 1), p1 = point (i);
                const auto p2 = point (i + 1), p3 = point (i + 2);
                path.cubicTo (p1.x + (p2.x - p0.x) / 6.0f,
                              p1.y + (p2.y - p0.y) / 6.0f,
                              p2.x - (p3.x - p1.x) / 6.0f,
                              p2.y - (p3.y - p1.y) / 6.0f,
                              p2.x, p2.y);
            }
            return path;
        }

        void drawMagnitudeSpectrum(juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const float dx = bounds.getWidth() / float (numPoints - 1);
            auto magY = [&] (int i)
            {
                return juce::jmap (std::clamp (smoothedMag[size_t (i)] + displayTiltDb (i),
                                               -80.0f, 20.0f),
                                   -80.0f, 20.0f, bounds.getBottom(), bounds.getY());
            };

            const juce::Path magPath = buildSmoothPath (int (numPoints), magY, bounds.getX(), dx,
                                                        bounds.getY(), bounds.getBottom());

            // Fill a closed copy, but stroke the open curve. Stroking the closed
            // path also strokes the two closing edges, and the edge running back to
            // the first sample drew a straight diagonal across the entire graph.
            juce::Path fillPath = magPath;
            fillPath.lineTo (bounds.getRight(), bounds.getBottom());
            fillPath.closeSubPath();

            juce::ColourGradient fillGrad(juce::Colour(0x2a9b9384), bounds.getCentreX(), bounds.getY(),
                                          juce::Colour(0x02000000), bounds.getCentreX(), bounds.getBottom(), false);
            g.setGradientFill(fillGrad);
            g.fillPath(fillPath);

            g.setColour(juce::Colour(0x559b9384));
            g.strokePath(magPath, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));

            auto baseY = [&] (int i)
            {
                return juce::jmap (std::clamp (smoothedBaseline[size_t (i)] + displayTiltDb (i),
                                               -80.0f, 20.0f),
                                   -80.0f, 20.0f, bounds.getBottom(), bounds.getY());
            };
            juce::Path basePath = buildSmoothPath (int (numPoints), baseY, bounds.getX(), dx,
                                                   bounds.getY(), bounds.getBottom());
            g.setColour (juce::Colour (0x44b3a892));
            g.strokePath (basePath, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
        }

        /** The span the detector looks over, drawn where the mouse is.
            This is what the DETAIL control sets. Without it the knob had nothing
            visible to move: it changed a window the user could not see, so it read
            as doing nothing at all.
        */
        void drawAnalysisWindow (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const int centre = isMouseInside
                                 ? juce::jlimit (0, getWidth() - 1, currentMousePos.x)
                                 : getWidth() / 2;

            // Map the pixel back to a scope point, then read the window there.
            const float frac = float (centre) / float (std::max (1, getWidth() - 1));
            const int idx = juce::jlimit (0, int (numPoints) - 1,
                                          int (frac * float (numPoints - 1) + 0.5f));

            const float halfHz = smoothedWindowHz[size_t (idx)];
            if (halfHz <= 0.0f)
                return;

            const float fNorm = float (idx) / float (numPoints - 1);
            const float fHz   = 20.0f * std::pow (1000.0f, fNorm);

            const float xL = freqToX (juce::jmax (20.0f, fHz - halfHz), bounds.getWidth());
            const float xR = freqToX (juce::jmin (20000.0f, fHz + halfHz), bounds.getWidth());

            const juce::Rectangle<float> band (bounds.getX() + xL, bounds.getY(),
                                               juce::jmax (2.0f, xR - xL), bounds.getHeight());

            g.setColour (juce::Colour (0x1e4a7fb5));
            g.fillRect (band);
            g.setColour (juce::Colour (0x665a8cc0));
            g.drawVerticalLine (int (band.getX()), band.getY(), band.getBottom());
            g.drawVerticalLine (int (band.getRight()), band.getY(), band.getBottom());

            // Say how wide it is, so the number moves with the band.
            juce::String txt;
            if (halfHz >= 1000.0f)
                txt = juce::String (halfHz * 2.0f / 1000.0f, 2) + " kHz window";
            else
                txt = juce::String (juce::roundToInt (halfHz * 2.0f)) + " Hz window";

            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.setColour (juce::Colour (0xff4a7fb5));
            g.drawText (txt, int (band.getX()) + 3, int (bounds.getY()) + 3, 130, 12,
                        juce::Justification::centredLeft);
        }

        void drawReductionCurve(juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const float dx = bounds.getWidth() / float (numPoints - 1);
            auto redY = [&] (int i)
            {
                return gainToY (smoothedRed[size_t (i)], bounds.getHeight());
            };
            juce::Path redPath = buildSmoothPath (int (numPoints), redY, bounds.getX(), dx,
                                                  bounds.getY(), bounds.getBottom());

            juce::ColourGradient redGrad(juce::Colour(0xffc9762e), bounds.getX(), bounds.getY(),
                                         juce::Colour(0xffdda05c), bounds.getRight(), bounds.getY(), false);
            g.setGradientFill(redGrad);
            g.strokePath(redPath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        /** Marks the frequencies the engine is acting on.
            Without this the graph shows a curve but not the decision behind it,
            and a user cannot tell whether the plugin found a resonance or missed
            one. Each mark is a short tick above the peak of a reduced region.
        */
        void drawDetectedResonances (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const float thresholdDb = 0.75f;   // below this the curve is the floor, not a decision
            const int   maxMarks    = 14;

            juce::Path marks;
            int count = 0;

            for (size_t i = 1; i + 1 < numPoints && count < maxMarks; )
            {
                if (smoothedRed[i] <= thresholdDb) { ++i; continue; }

                // Walk the run and remember where it peaks.
                size_t runStart = i;
                size_t peak = i;
                while (i + 1 < numPoints && smoothedRed[i + 1] > thresholdDb)
                {
                    ++i;
                    if (smoothedRed[i] > smoothedRed[peak]) peak = i;
                }
                const size_t runEnd = i;

                // Only mark a run that stands on its own rather than a wide shelf.
                const float width = float (runEnd - runStart + 1) / float (numPoints);
                if (width < 0.35f)
                {
                    const float x = bounds.getX()
                                  + (float (peak) / float (numPoints - 1)) * bounds.getWidth();
                    const float y = gainToY (smoothedRed[peak], bounds.getHeight());
                    marks.startNewSubPath (x, bounds.getY() + 2.0f);
                    marks.lineTo (x, juce::jmax (y - 3.0f, bounds.getY() + 4.0f));
                    ++count;
                }
                ++i;
            }

            if (count > 0)
            {
                g.setColour (juce::Colour (0xffc9762e).withAlpha (0.55f));
                g.strokePath (marks, juce::PathStrokeType (1.0f));
            }
        }

        void drawSidechainEQCurve(juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            juce::Path eqPath;
            bool started = false;

            for (int px = 0; px < static_cast<int>(bounds.getWidth()); px += 2)
            {
                float freq = xToFreq(static_cast<float>(px), bounds.getWidth());
                float totalGainDb = 0.0f;

                for (int b = 0; b < 8; ++b)
                {
                    auto bStr = juce::String(b + 1);
                    bool isEnabled = apvts.getRawParameterValue("eq_enable_" + bStr)->load() > 0.5f;
                    if (isEnabled)
                    {
                        int typeIdx = static_cast<int>(apvts.getRawParameterValue("eq_type_" + bStr) ? apvts.getRawParameterValue("eq_type_" + bStr)->load() : 0.0f);
                        float f = apvts.getRawParameterValue("eq_freq_" + bStr)->load();
                        float gain = apvts.getRawParameterValue("eq_gain_" + bStr)->load();
                        float q = apvts.getRawParameterValue("eq_q_" + bStr)->load();

                        FilterBand band { isEnabled, static_cast<FilterType>(typeIdx), f, gain, q };
                        totalGainDb += band.getWeightDbAt(freq);
                    }
                }

                float y = gainToY(totalGainDb, bounds.getHeight());
                if (!started)
                {
                    eqPath.startNewSubPath(static_cast<float>(px), y);
                    started = true;
                }
                else
                {
                    eqPath.lineTo(static_cast<float>(px), y);
                }
            }

            g.setColour(juce::Colour(0xd0c9762e));
            g.strokePath(eqPath, juce::PathStrokeType(1.8f));
        }

        void drawEQNodes(juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const int activeBand = (selectedNode >= 0) ? selectedNode : hoveredNode;

            // Draw highlighted focus curve under active/selected band
            if (activeBand >= 0)
            {
                auto bStr = juce::String(activeBand + 1);
                bool isEnabled = apvts.getRawParameterValue("eq_enable_" + bStr)->load() > 0.5f;
                if (isEnabled)
                {
                    float f0 = apvts.getRawParameterValue("eq_freq_" + bStr)->load();
                    float g0 = apvts.getRawParameterValue("eq_gain_" + bStr)->load();
                    float q0 = apvts.getRawParameterValue("eq_q_" + bStr)->load();

                    juce::Path bandShade;
                    bool startedShade = false;
                    const float zeroY = gainToY(0.0f, bounds.getHeight());

                    int typeIdx = static_cast<int>(apvts.getRawParameterValue("eq_type_" + bStr) ? apvts.getRawParameterValue("eq_type_" + bStr)->load() : 0.0f);
                    FilterBand activeBandObj { isEnabled, static_cast<FilterType>(typeIdx), f0, g0, q0 };

                    for (int px = 0; px < static_cast<int>(bounds.getWidth()); px += 2)
                    {
                        float freq = xToFreq(static_cast<float>(px), bounds.getWidth());
                        float wDb = activeBandObj.getWeightDbAt(freq);
                        float y = gainToY(wDb, bounds.getHeight());

                        if (!startedShade)
                        {
                            bandShade.startNewSubPath(static_cast<float>(px), zeroY);
                            bandShade.lineTo(static_cast<float>(px), y);
                            startedShade = true;
                        }
                        else
                        {
                            bandShade.lineTo(static_cast<float>(px), y);
                        }
                    }
                    bandShade.lineTo(bounds.getRight(), zeroY);
                    bandShade.closeSubPath();

                    juce::ColourGradient bandGrad(juce::Colour(0x35c9762e), freqToX(f0, bounds.getWidth()), gainToY(g0, bounds.getHeight()),
                                                  juce::Colour(0x05c9762e), freqToX(f0, bounds.getWidth()), zeroY, false);
                    g.setGradientFill(bandGrad);
                    g.fillPath(bandShade);
                }
            }

            for (int b = 0; b < 8; ++b)
            {
                auto bStr = juce::String(b + 1);
                bool isEnabled = apvts.getRawParameterValue("eq_enable_" + bStr)->load() > 0.5f;
                float f = apvts.getRawParameterValue("eq_freq_" + bStr)->load();
                float gain = apvts.getRawParameterValue("eq_gain_" + bStr)->load();
                float q = apvts.getRawParameterValue("eq_q_" + bStr)->load();

                float x = freqToX(f, bounds.getWidth());
                float y = gainToY(gain, bounds.getHeight());

                bool isSelected = (selectedNode == b);
                bool isHovered  = (hoveredNode == b);

                if (isEnabled)
                {
                    // Draw Q bandwidth wing handles when selected or hovered
                    if (isSelected || isHovered)
                    {
                        float bwOct = 1.0f / std::max(0.1f, q);
                        float fL = f * std::pow(2.0f, -0.5f * bwOct);
                        float fR = f * std::pow(2.0f, +0.5f * bwOct);
                        float xL = freqToX(fL, bounds.getWidth());
                        float xR = freqToX(fR, bounds.getWidth());

                        // Horizontal bandwidth guideline
                        g.setColour(juce::Colour(0x88c9762e));
                        g.drawHorizontalLine(static_cast<int>(y), xL, xR);

                        // Wing handles
                        g.setColour(juce::Colour(0xffc9762e));
                        g.fillEllipse(xL - 3.0f, y - 3.0f, 6.0f, 6.0f);
                        g.fillEllipse(xR - 3.0f, y - 3.0f, 6.0f, 6.0f);

                        // HUD badge info
                        int typeIdx = static_cast<int>(apvts.getRawParameterValue("eq_type_" + bStr) ? apvts.getRawParameterValue("eq_type_" + bStr)->load() : 0.0f);
                        juce::String typeName = getFilterTypeName(static_cast<FilterType>(typeIdx));
                        juce::String slopeInfo = (typeIdx == 1 || typeIdx == 2)
                            ? (juce::String(std::max(6, static_cast<int>(std::round(q * 16.97f) / 6.0f) * 6)) + " dB/oct")
                            : ("Q " + juce::String(q, 2));

                        juce::String text = "Band " + juce::String(b + 1) + " [" + typeName + "]: "
                                          + juce::String(std::round(f)) + " Hz | "
                                          + (gain >= 0.0f ? "+" : "") + juce::String(gain, 1) + " dB | "
                                          + slopeInfo + "  [Right-Click for Menu]";
                        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                        const int textW = 260;
                        const int badgeX = std::clamp(static_cast<int>(x - textW / 2), 10, std::max(10, static_cast<int>(bounds.getWidth() - textW - 10)));
                        const int badgeY = std::clamp(static_cast<int>(y - 28), 6, static_cast<int>(bounds.getHeight() - 24));

                        g.setColour(juce::Colour(0xd02a241b));
                        g.fillRoundedRectangle(static_cast<float>(badgeX), static_cast<float>(badgeY), static_cast<float>(textW), 18.0f, 4.0f);
                        g.setColour(juce::Colour(0xfffdfaf4));
                        g.drawText(text, badgeX, badgeY, textW, 18, juce::Justification::centred);
                    }

                    // Outer glow when selected or hovered
                    if (isSelected || isHovered)
                    {
                        g.setColour(juce::Colour(0x50c9762e));
                        g.fillEllipse(x - 13.0f, y - 13.0f, 26.0f, 26.0f);
                    }

                    // Node body circle
                    g.setColour(isSelected ? juce::Colour(0xff3d2e1e) : (isHovered ? juce::Colour(0xff332b22) : juce::Colour(0xff282520)));
                    g.fillEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f);

                    g.setColour(isSelected ? juce::Colour(0xffffa24a) : (isHovered ? juce::Colour(0xffe8903c) : juce::Colour(0xffc9762e)));
                    g.drawEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f, isSelected ? 2.2f : 1.6f);

                    // Number label 1..8 in center
                    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                    g.setColour(isSelected ? juce::Colour(0xfffffbfa) : juce::Colour(0xfff3eee4));
                    g.drawText(juce::String(b + 1), static_cast<int>(x - 9.0f), static_cast<int>(y - 9.0f), 18, 18, juce::Justification::centred, false);
                }
                else
                {
                    g.setColour(juce::Colour(0x35282520));
                    g.fillEllipse(x - 7.0f, y - 7.0f, 14.0f, 14.0f);
                    g.setColour(juce::Colour(0x709b9384));
                    g.drawEllipse(x - 7.0f, y - 7.0f, 14.0f, 14.0f, 1.0f);
                    g.setFont(juce::FontOptions(8.5f));
                    g.setColour(juce::Colour(0x709b9384));
                    g.drawText(juce::String(b + 1), static_cast<int>(x - 7.0f), static_cast<int>(y - 7.0f), 14, 14, juce::Justification::centred, false);
                }
            }
        }

        int findNodeOrWingNear(juce::Point<int> pos, int& outWing)
        {
            auto bounds = getLocalBounds().toFloat();
            outWing = 0;

            // Check wing handles of selected/hovered node first
            if (selectedNode >= 0)
            {
                auto bStr = juce::String(selectedNode + 1);
                float f = apvts.getRawParameterValue("eq_freq_" + bStr)->load();
                float gain = apvts.getRawParameterValue("eq_gain_" + bStr)->load();
                float q = apvts.getRawParameterValue("eq_q_" + bStr)->load();

                float y = gainToY(gain, bounds.getHeight());
                float bwOct = 1.0f / std::max(0.1f, q);
                float fL = f * std::pow(2.0f, -0.5f * bwOct);
                float fR = f * std::pow(2.0f, +0.5f * bwOct);
                float xL = freqToX(fL, bounds.getWidth());
                float xR = freqToX(fR, bounds.getWidth());

                if (pos.toFloat().getDistanceFrom(juce::Point<float>(xL, y)) < 12.0f)
                {
                    outWing = -1;
                    return selectedNode;
                }
                if (pos.toFloat().getDistanceFrom(juce::Point<float>(xR, y)) < 12.0f)
                {
                    outWing = +1;
                    return selectedNode;
                }
            }

            // Check node centers
            for (int b = 0; b < 8; ++b)
            {
                auto bStr = juce::String(b + 1);
                float f = apvts.getRawParameterValue("eq_freq_" + bStr)->load();
                float gain = apvts.getRawParameterValue("eq_gain_" + bStr)->load();

                float x = freqToX(f, bounds.getWidth());
                float y = gainToY(gain, bounds.getHeight());

                if (pos.toFloat().getDistanceFrom(juce::Point<float>(x, y)) < 20.0f)
                {
                    return b;
                }
            }
            return -1;
        }

        float freqToX(float freq, float width) const
        {
            float norm = std::log10(std::clamp(freq, 20.0f, 20000.0f) / 20.0f) / 3.0f;
            return norm * width;
        }

        float xToFreq(float x, float width) const
        {
            float norm = std::clamp(x / width, 0.0f, 1.0f);
            return 20.0f * std::pow(10.0f, norm * 3.0f);
        }

        float gainToY(float gainDb, float height) const
        {
            return juce::jmap(std::clamp(gainDb, -24.0f, 24.0f), 24.0f, -24.0f, 0.0f, height);
        }

        float yToGain(float y, float height) const
        {
            return juce::jmap(std::clamp(y, 0.0f, height), 0.0f, height, 24.0f, -24.0f);
        }

        ResonaProAudioProcessor& processor;
        juce::AudioProcessorValueTreeState& apvts;

        static constexpr size_t numPoints = static_cast<size_t> (ResonaProAudioProcessor::ScopeSize);
        std::array<float, numPoints> smoothedMag;
        std::array<float, numPoints> smoothedRed;
        std::array<float, numPoints> smoothedBaseline;
        std::array<float, numPoints> smoothedWindowHz;
        float referencePromDb = 0.0f;

        int selectedNode = -1;
        bool isDragging = false;
    };
}
