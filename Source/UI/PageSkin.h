// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>

namespace sj {

/** Optional "recording desk" skin that matches the FtheBand landing page (styles.css): near-black page, cream ink, red LEDs,
    square corners, 1 px borders, serif text and monospace controls. Fonts (Source Serif 4, JetBrains Mono; SIL OFL 1.1) are
    embedded by the caller as typefaces. The default look of the app is untouched unless this is switched on. */
struct PageSkin : juce::LookAndFeel_V4 {
    static inline const juce::Colour bg { 0xff070707 }, desk { 0xff101010 }, bar { 0xff0c0c0c }, border { 0xff2e2e2e },
                                     ink { 0xfff4eee4 }, mute { 0xff8a8276 }, red { 0xffff2a2a }, paper { 0xffc9b896 };

    PageSkin (juce::Typeface::Ptr serifTf, juce::Typeface::Ptr serifBoldTf, juce::Typeface::Ptr monoTf, juce::Typeface::Ptr monoBoldTf)
        : serif (std::move (serifTf)), serifBold (std::move (serifBoldTf)), mono (std::move (monoTf)), monoBold (std::move (monoBoldTf))
    {
        using namespace juce;
        const Colour ledOn (0xff2a1010), track (0xff2a2a2a);
        setColour (ResizableWindow::backgroundColourId, bg);
        setColour (TextButton::buttonColourId, desk);        setColour (TextButton::buttonOnColourId, ledOn);
        setColour (TextButton::textColourOffId, paper);      setColour (TextButton::textColourOnId, ink);
        setColour (ToggleButton::textColourId, ink);         setColour (ToggleButton::tickColourId, red);
        setColour (ToggleButton::tickDisabledColourId, mute);
        setColour (ComboBox::backgroundColourId, desk);      setColour (ComboBox::textColourId, ink);
        setColour (ComboBox::outlineColourId, border);       setColour (ComboBox::arrowColourId, red);
        setColour (ComboBox::buttonColourId, desk);
        setColour (PopupMenu::backgroundColourId, bar);      setColour (PopupMenu::textColourId, ink);
        setColour (PopupMenu::highlightedBackgroundColourId, ledOn); setColour (PopupMenu::highlightedTextColourId, ink);
        setColour (Slider::backgroundColourId, track);       setColour (Slider::trackColourId, red);
        setColour (Slider::thumbColourId, ink);              setColour (Slider::textBoxTextColourId, ink);
        setColour (Slider::textBoxBackgroundColourId, desk); setColour (Slider::textBoxOutlineColourId, border);
        setColour (Label::textColourId, ink);                setColour (Label::outlineColourId, Colours::transparentBlack);
        setColour (TextEditor::backgroundColourId, desk);    setColour (TextEditor::textColourId, ink);
        setColour (TextEditor::outlineColourId, border);     setColour (TextEditor::focusedOutlineColourId, red);
        setColour (TextEditor::highlightColourId, Colour (0xff5a1414));
        setColour (ListBox::backgroundColourId, desk);       setColour (ListBox::outlineColourId, border);
        setColour (ScrollBar::thumbColourId, Colour (0xff3a3a3a));
        setColour (TooltipWindow::backgroundColourId, bar);  setColour (TooltipWindow::textColourId, ink);
        setColour (TooltipWindow::outlineColourId, border);
        setColour (ProgressBar::foregroundColourId, red);    setColour (ProgressBar::backgroundColourId, track);
        setColour (AlertWindow::backgroundColourId, desk);   setColour (AlertWindow::textColourId, ink);
        setColour (AlertWindow::outlineColourId, border);
        setColour (HyperlinkButton::textColourId, red);
        setColour (TabbedComponent::outlineColourId, Colours::transparentBlack);
    }

    // ---- fonts: serif for text, monospace for controls (like the page) ----
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override { return f.isBold() ? serifBold : serif; }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::Font (juce::FontOptions (mono).withHeight (juce::jmin ((float) h * 0.55f, 14.0f))); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (mono).withHeight (13.5f)); }
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (serif).withHeight (15.0f)); }
    juce::Font getLabelFont (juce::Label& l) override {
        const auto f = l.getFont();
        return juce::Font (juce::FontOptions (f.isBold() ? serifBold : serif).withHeight (f.getHeight()));
    }

    // ---- square, 1 px, red on hover (the page has no rounded corners) ----
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool highlighted, bool down) override {
        auto r = b.getLocalBounds();
        g.setColour (down ? c.brighter (0.15f) : highlighted ? c.brighter (0.08f) : c);
        g.fillRect (r);
        g.setColour (highlighted || down ? red : border);
        g.drawRect (r, 1);
    }

    /** Toggle = an LED (like .led / .led.on on the page) + monospace text. */
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override {
        const auto r = b.getLocalBounds().toFloat();
        const float d = 12.0f, cx = 4.0f + d * 0.5f, cy = r.getCentreY();
        if (b.getToggleState()) {
            for (int i = 3; i >= 1; --i) { g.setColour (red.withAlpha (0.10f)); const float e = d * 0.5f + (float) i * 2.0f; g.fillEllipse (cx - e, cy - e, 2 * e, 2 * e); }
            g.setColour (red); g.fillEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d);
        } else {
            g.setColour (juce::Colour (0xff2a1010)); g.fillEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d);
            g.setColour (juce::Colour (0xff3a1a1a)); g.drawEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d, 1.0f);
        }
        g.setColour (b.isEnabled() ? ink : mute);
        g.setFont (juce::Font (juce::FontOptions (mono).withHeight (13.5f)));
        g.drawFittedText (b.getButtonText(), juce::Rectangle<int> (24, 0, b.getWidth() - 24, b.getHeight()), juce::Justification::centredLeft, 1, 0.8f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override {
        g.setColour (desk); g.fillRect (0, 0, w, h);
        g.setColour (border); g.drawRect (0, 0, w, h, 1);
        juce::Path p; const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        p.addTriangle (cx - 4.5f, cy - 2.5f, cx + 4.5f, cy - 2.5f, cx, cy + 3.0f);
        g.setColour (red); g.fillPath (p);
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    { label.setBounds (6, 1, box.getWidth() - 28, box.getHeight() - 2); label.setFont (getComboBoxFont (box)); }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override {
        if (style != juce::Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s); return; }
        const float ty = (float) y + ((float) h - 4.0f) * 0.5f;
        g.setColour (s.findColour (juce::Slider::backgroundColourId)); g.fillRect ((float) x, ty, (float) w, 4.0f);
        g.setColour (red); g.fillRect ((float) x, ty, juce::jmax (0.0f, pos - (float) x), 4.0f);
        g.setColour (ink); g.fillRect (pos - 4.0f, (float) y + ((float) h - 18.0f) * 0.5f, 8.0f, 18.0f);
    }

    // ---- tabs: mono caps-ish labels, red 3 px rule on the front tab (like .soon's top border) ----
    void drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool) override {
        const auto r = b.getLocalBounds(); const bool front = b.isFrontTab();
        g.setColour (front ? desk : over ? juce::Colour (0xff141414) : bar); g.fillRect (r);
        g.setColour (front ? ink : mute);
        g.setFont (juce::Font (juce::FontOptions (front ? monoBold : mono).withHeight (14.0f)));
        g.drawFittedText (b.getButtonText(), r.reduced (6, 2), juce::Justification::centred, 1, 0.8f);
        if (front) { g.setColour (red); g.fillRect (r.getX(), 0, r.getWidth(), 3); }
        g.setColour (border); g.fillRect (r.getRight() - 1, 0, 1, r.getHeight());
    }
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics& g) override { g.fillAll (bar); }
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g, int w, int h) override { g.setColour (border); g.fillRect (0, h - 1, w, 1); }

    juce::Typeface::Ptr serif, serifBold, mono, monoBold;
};

} // namespace sj
