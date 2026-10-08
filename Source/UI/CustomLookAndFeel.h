#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ResonaPro
{
    /** Flat, quiet, cream-coloured styling.

        The interface stays deliberately plain: a warm off-white ground, one
        accent colour reserved for the gain-reduction display, and thin outlines
        everywhere else. Nothing competes with the spectrum, because the spectrum
        is the part a user actually reads.
    */
    class CustomLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        // --- palette -------------------------------------------------------------
        static juce::Colour ground()     { return juce::Colour (0xffede8de); }  // window
        static juce::Colour panel()      { return juce::Colour (0xfff6f2e9); }  // raised areas
        static juce::Colour well()       { return juce::Colour (0xffe5dfd2); }  // graph well
        static juce::Colour hairline()   { return juce::Colour (0xffd2caba); }  // 1px separators
        static juce::Colour ink()        { return juce::Colour (0xff3b3833); }  // primary text
        static juce::Colour inkSoft()    { return juce::Colour (0xff7f7867); }  // secondary text
        static juce::Colour accent()     { return juce::Colour (0xffc9762e); }  // amber
        static juce::Colour accentSoft() { return juce::Colour (0xffe2a76a); }
        static juce::Colour spectrum()   { return juce::Colour (0xff9b9384); }  // input curve
        static juce::Colour warn()       { return juce::Colour (0xffa85a4a); }

        CustomLookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, ground());
            setColour (juce::DocumentWindow::textColourId,         ink());

            setColour (juce::Slider::textBoxTextColourId,          ink());
            setColour (juce::Slider::textBoxBackgroundColourId,    juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxOutlineColourId,       juce::Colours::transparentBlack);
            setColour (juce::Slider::rotarySliderFillColourId,     accent());
            setColour (juce::Slider::rotarySliderOutlineColourId,  hairline());
            setColour (juce::Slider::thumbColourId,                ink());

            setColour (juce::Label::textColourId,                  inkSoft());

            setColour (juce::TextButton::buttonColourId,           panel());
            setColour (juce::TextButton::buttonOnColourId,         accent());
            setColour (juce::TextButton::textColourOffId,          inkSoft());
            setColour (juce::TextButton::textColourOnId,           juce::Colour (0xfffdfaf4));

            setColour (juce::ComboBox::backgroundColourId,         panel());
            setColour (juce::ComboBox::textColourId,               ink());
            setColour (juce::ComboBox::outlineColourId,            hairline());
            setColour (juce::ComboBox::arrowColourId,              inkSoft());

            setColour (juce::PopupMenu::backgroundColourId,        panel());
            setColour (juce::PopupMenu::textColourId,              ink());
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accentSoft());
            setColour (juce::PopupMenu::highlightedTextColourId,   ink());

            setColour (juce::ToggleButton::textColourId,           inkSoft());
            setColour (juce::ToggleButton::tickColourId,           accent());

            setColour (juce::TooltipWindow::backgroundColourId,    panel());
            setColour (juce::TooltipWindow::textColourId,          ink());
            setColour (juce::TooltipWindow::outlineColourId,       hairline());
        }

        // --- rotary knob ---------------------------------------------------------
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
            const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
            const auto centreX = bounds.getCentreX();
            const auto centreY = bounds.getCentreY();
            const auto angle = rotaryStartAngle
                             + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
            const auto lineW = juce::jmax (2.0f, radius * 0.16f);
            const auto arcR = radius - lineW * 0.5f;

            if (radius <= 2.0f)
                return;

            // knob face
            g.setColour (panel());
            g.fillEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
            g.setColour (hairline());
            g.drawEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.0f);

            // unfilled track
            juce::Path track;
            track.addCentredArc (centreX, centreY, arcR, arcR, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (hairline());
            g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            // filled portion
            if (sliderPosProportional > 0.001f)
            {
                juce::Path fill;
                fill.addCentredArc (centreX, centreY, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
                g.setColour (accent());
                g.strokePath (fill, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }

            // pointer
            juce::Path pointer;
            const auto pw = juce::jmax (1.6f, radius * 0.10f);
            pointer.addRoundedRectangle (-pw * 0.5f, -radius + lineW * 0.6f,
                                         pw, radius * 0.42f, pw * 0.5f);
            pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                        .translated (centreX, centreY));
            g.setColour (ink());
            g.fillPath (pointer);
        }

        // --- flat, outlined buttons ---------------------------------------------
        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                   const juce::Colour&,
                                   bool shouldDrawAsHighlighted, bool shouldDrawAsDown) override
        {
            const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
            const bool on = button.getToggleState();

            // A button can ask to be filled with the brand colour instead of the
            // panel colour. Without this the drawer handle was painted the same
            // cream as everything else and disappeared into the background.
            const bool brand = button.getProperties().contains ("brandFill");

            auto fill = brand ? accent() : (on ? accent() : panel());
            if (brand && on) fill = accent().darker (0.10f);
            if (shouldDrawAsDown)             fill = fill.darker (0.06f);
            else if (shouldDrawAsHighlighted) fill = fill.brighter (0.04f);

            g.setColour (fill);
            g.fillRoundedRectangle (bounds, 3.0f);
            g.setColour (on ? accent().darker (0.12f) : hairline());
            g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
            g.setColour (button.findColour (button.getToggleState()
                                                ? juce::TextButton::textColourOnId
                                                : juce::TextButton::textColourOffId));
            g.drawFittedText (button.getButtonText(), button.getLocalBounds(),
                              juce::Justification::centred, 1, 0.85f);
        }

        // --- combo boxes ---------------------------------------------------------
        void drawComboBox (juce::Graphics& g, int width, int height, bool,
                           int, int, int, int, juce::ComboBox& box) override
        {
            const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
            g.setColour (panel());
            g.fillRoundedRectangle (bounds, 3.0f);
            g.setColour (box.isMouseOver() ? accentSoft() : hairline());
            g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

            juce::Path arrow;
            const auto cx = static_cast<float> (width) - 12.0f;
            const auto cy = static_cast<float> (height) * 0.5f;
            arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
            arrow.lineTo (cx, cy + 2.5f);
            arrow.lineTo (cx + 4.0f, cy - 2.0f);
            g.setColour (inkSoft());
            g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override
        {
            return juce::Font (juce::FontOptions (12.0f));
        }

        juce::Font getPopupMenuFont() override
        {
            return juce::Font (juce::FontOptions (13.0f));
        }
    };
}