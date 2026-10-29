// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#include "MainComponent.h"
#include <set>
#include "Util/ProcStats.h"
#include "Net/SamplePacks.h"
#include "Setup/FirstRunSetup.h"
using namespace sj;

#ifndef FTHEBAND_DISPLAY_VERSION
 #define FTHEBAND_DISPLAY_VERSION "0.1.0-beta"
#endif
#ifndef FTHEBAND_DONATE_URL
 #define FTHEBAND_DONATE_URL ""
#endif
#ifndef FTHEBAND_DISPLAY_VERSION
 #define FTHEBAND_DISPLAY_VERSION "0.1.0-beta"
#endif
#ifndef FTHEBAND_COMMIT
 #define FTHEBAND_COMMIT ""
#endif
#ifndef FTHEBAND_SOURCE_URL
 #define FTHEBAND_SOURCE_URL ""
#endif

namespace {
// Palette. Default = the original dark-blue look; the optional "FtheBand page" skin (UI/PageSkin.h) swaps these (setSkin).
juce::Colour bg (0xff15171c), panel (0xff1f232a), panel2 (0xff2a2e36), accent (0xffe8a33d), textDim (0xffa0a6b0), ink (0xffffffff),
             barCol (0xff5aa9e6), okCol (juce::Colours::limegreen);   // chroma bars / "armed" text, and the good-level meter / "PLAYING" text
float panelCorner = 10.0f, fontScale = 1.0f;   // the serif looks smaller than the default sans at the same height
juce::Typeface::Ptr skinSerif, skinSerifBold;   // set only while the page skin is on
juce::Font font (float h, bool bold = false) {
    if (skinSerif != nullptr) return juce::Font (juce::FontOptions (bold ? skinSerifBold : skinSerif).withHeight (h * fontScale));
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}
struct Pal { juce::Colour bg, panel, panel2, accent, textDim, ink; float scale; };
Pal currentPal() { return { bg, panel, panel2, accent, textDim, ink, fontScale }; }
void applyPalette (bool page) {
    if (page) { bg = juce::Colour (0xff070707); panel = juce::Colour (0xff101010); panel2 = juce::Colour (0xff1c1c1c);
                accent = juce::Colour (0xffff2a2a); textDim = juce::Colour (0xff8a8276); ink = juce::Colour (0xfff4eee4); panelCorner = 0.0f;
                barCol = juce::Colour (0xffc9b896); okCol = juce::Colour (0xffffb020); fontScale = 1.12f; }
    else      { bg = juce::Colour (0xff15171c); panel = juce::Colour (0xff1f232a); panel2 = juce::Colour (0xff2a2e36);
                accent = juce::Colour (0xffe8a33d); textDim = juce::Colour (0xffa0a6b0); ink = juce::Colour (0xffffffff); panelCorner = 10.0f;
                barCol = juce::Colour (0xff5aa9e6); okCol = juce::Colours::limegreen; fontScale = 1.0f; }
}
/** After a live skin switch: labels / links keep their explicit colours and fonts, so re-map the palette ones. */
void restyleTree (juce::Component& c, const Pal& a, const Pal& b, std::set<juce::Component*>& done) {
    auto remap = [&] (juce::Colour x) { return x == a.ink ? b.ink : x == a.textDim ? b.textDim : x == a.accent ? b.accent : x; };
    for (auto* ch : c.getChildren()) {
        if (! done.insert (ch).second) continue;   // the showing tab is reachable twice
        if (auto* l = dynamic_cast<juce::Label*> (ch)) {
            { const auto old = l->findColour (juce::Label::textColourId); if (remap (old) != old) l->setColour (juce::Label::textColourId, remap (old)); }   // only explicit palette colours; tabs not showing have no LookAndFeel to ask
            const auto f = l->getFont(); l->setFont (font (f.getHeight() / a.scale, f.isBold()));
        }
        if (auto* h = dynamic_cast<juce::HyperlinkButton*> (ch)) { { const auto old = h->findColour (juce::HyperlinkButton::textColourId); if (remap (old) != old) h->setColour (juce::HyperlinkButton::textColourId, remap (old)); } 
            const double fh = (double) h->getProperties().getWithDefault ("sjLinkSize", 0.0);   // set where a link has its own fixed font; 0 = About-page style (underlined, resizes)
            if (fh > 0) h->setFont (font ((float) fh), false, juce::Justification::centredLeft);
            else        { auto lf = b.scale > 1.0f ? font (14.0f) : juce::Font (juce::FontOptions { 14.0f, juce::Font::underlined });
                          if (b.scale > 1.0f) lf.setUnderline (true);
                          h->setFont (lf, true, juce::Justification::left); } }
        restyleTree (*ch, a, b, done);
    }
}
const juce::StringArray styleNames { "Rock", "Funk", "Blues", "Ballad" };
const char* bandStateName (int s) { return s == 1 ? "Playing" : s == 2 ? "Fading out" : s == 3 ? "Counting in" : s == 4 ? "Ending (ringing out)" : "Out"; }

struct Content : juce::Component { virtual void tick() {} };
}

/** A tab: a Viewport whose content never shrinks below its design size (scrolls instead of clipping). */
struct MainComponent::Page : juce::Viewport {
    Page (std::unique_ptr<Content> c, int minW, int minH) : content (std::move (c)), mw (minW), mh (minH)
    { setViewedComponent (content.get(), false); setScrollBarsShown (true, true, false, false); }
    void resized() override {
        int w = getWidth(), h = getHeight(); const int sb = getScrollBarThickness();
        const bool needH = w < mw, needV = h < mh;
        if (needH) h -= sb; if (needV) w -= sb;
        content->setSize (juce::jmax (mw, w), juce::jmax (mh, h));
        Viewport::resized();
    }
    std::unique_ptr<Content> content; int mw, mh;
};

static void styleLabel (juce::Label& l, float h = 15.0f, juce::Colour c = ink)
{ l.setFont (font (h)); l.setColour (juce::Label::textColourId, c); }

// ================================================================================================ Jam
struct JamPage : Content {
    MainComponent& m; AudioEngine& e;
    juce::TextButton startBtn { "Start session" }, learnBtn { "Learn noise" }, recBtn { "Record stems" }, takesBtn { "Open takes" };
    double learnProg = 0; juce::ProgressBar learnBar { learnProg };
    juce::Label instL { {}, "Instrument" }, tunL { {}, "Tuning" }, centsL { {}, "Cents" }, info;
    juce::ComboBox inst, tun;
    juce::Slider cents { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton strangle { "Strangle" }, muteBass { "Mute bass chair" }, bassUp { "Bass chair octave up" }, monitor { "Monitor input" };
    juce::Label micL { {}, "Mic gain" }, latency;
    juce::Slider micGain { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton micMon { "Monitor mic" };
    juce::ComboBox startMode; juce::ToggleButton clickIn { "1-bar click" }; juce::Label bandLamp;
    juce::Rectangle<int> chordArea, chromaArea, meterArea;

    JamPage (MainComponent& mc, AudioEngine& en) : m (mc), e (en) {
        for (auto* c : std::initializer_list<juce::Component*> { &startBtn, &learnBtn, &learnBar, &recBtn, &takesBtn, &instL, &tunL, &centsL,
                                                                  &inst, &tun, &cents, &strangle, &muteBass, &bassUp, &monitor, &info,
                                                                  &micL, &micGain, &micMon, &latency, &startMode, &clickIn, &bandLamp }) addAndMakeVisible (c);
        startMode.addItemList ({ "Band: start when I play", "Band: start on button" }, 1);
        startMode.setTooltip ("Start when I play: the band waits (armed) and comes in on the downbeat after you start, and re-arms after it fades out.\n"
                              "Start on button: the band starts only when you press Start (Space), optionally after a 1-bar click.");
        startMode.onChange = [this] { e.startMode = startMode.getSelectedId() - 1; updateEnables(); };
        clickIn.setTooltip ("Start on button: count in with one bar of clicks before the band enters");
        clickIn.onClick = [this] { e.countInClick = clickIn.getToggleState(); };
        styleLabel (bandLamp, 15); bandLamp.setJustificationType (juce::Justification::centredLeft);
        for (auto* l : { &instL, &tunL, &centsL, &micL }) styleLabel (*l, 14, textDim);
        styleLabel (latency, 14);
        micGain.setRange (0, 4, 0.01); micGain.setSkewFactorFromMidPoint (1.0); micGain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        micGain.onValueChange = [this] { e.micGain = (float) micGain.getValue(); };
        micMon.onClick = [this] { e.micMonitor = micMon.getToggleState(); };
        micMon.setTooltip ("Hear the mic in the mix (off by default: use headphones to avoid feedback)");
        styleLabel (info, 14, textDim); info.setJustificationType (juce::Justification::topLeft);
        inst.addItemList (inputInstrumentNames(), 1); tun.addItemList (tuningNames(), 1);
        cents.setRange (-300, 300, 1); cents.setTextValueSuffix (" c"); cents.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        learnBar.setTextToDisplay ("noise learnt"); learnBar.setPercentageDisplay (false);
        inst.onChange = [this] { e.instrument = inst.getSelectedId() - 1; updateEnables(); };
        tun.onChange = [this] { e.tuning = tun.getSelectedId() - 1; updateEnables(); };
        cents.onValueChange = [this] { e.customCents = (float) cents.getValue(); };
        strangle.onClick = [this] { e.strangle = strangle.getToggleState(); };
        muteBass.onClick = [this] { e.muteBassChair = muteBass.getToggleState(); updateEnables(); };
        bassUp.onClick = [this] { e.bassGiveWay = bassUp.getToggleState(); };
        monitor.onClick = [this] { e.monitorGuitar = monitor.getToggleState(); };
        startBtn.onClick = [this] { startStop(); };
        learnBtn.onClick = [this] { e.learnNoiseNow(); };
        recBtn.onClick = [this] {
            if (e.recorder.isRecording()) e.recorder.stop();
            else e.startRecording (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ftheband Takes"));
        };
        takesBtn.onClick = [] { auto f = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ftheband Takes"); f.createDirectory(); f.startAsProcess(); };
        syncFromEngine();
    }
    /** Start button and Space. Start-when-I-play: toggles the session (armed / stopped). Start-on-button: starts the
        session + band; while listening with the band out, starts the band; otherwise stops (musically). */
    void startStop() {
        const auto ph = e.phase(); const bool button = e.startMode.load() == 1;
        if (ph == AudioEngine::Phase::Idle) e.startSession();
        else if (button && e.bandState.load() == 0 && ph == AudioEngine::Phase::Listening && ! e.bandStartReq.load()) e.bandStart();
        else e.stopSession();
    }
    juce::String startLabel() {
        const auto ph = e.phase();
        if (ph == AudioEngine::Phase::Idle) return "Start (Space)";
        if (e.startMode.load() == 1 && e.bandState.load() == 0 && ph == AudioEngine::Phase::Listening && ! e.bandStartReq.load()) return "Start band (Space)";
        return "Stop (Space)";
    }
    juce::String bandLampText (juce::Colour& col) {
        const int bs = m.usePreview ? 0 : e.bandState.load(); const auto ph = m.usePreview ? AudioEngine::Phase::Listening : e.phase();
        const bool button = e.startMode.load() == 1;
        col = juce::Colour (0xff9aa3ad);
        if (bs == 1) { col = okCol; return "Band: PLAYING"; }
        if (bs == 2) { col = juce::Colours::orange; return "Band: fading out - play to bring it back"; }
        if (bs == 3) { col = juce::Colours::yellow; return "Band: counting in..."; }
        if (bs == 4) { col = juce::Colours::orange; return "Band: ending"; }
        if (ph == AudioEngine::Phase::Idle) return "Band: stopped";
        if (ph == AudioEngine::Phase::Learning) return "Band: learning noise - don't play";
        if (button) { if (e.bandStartReq.load()) { col = juce::Colours::yellow; return "Band: starting..."; } return "Band: ready - press Start band"; }
        col = barCol; return "Band: ARMED - waiting for you";
    }
    void syncFromEngine() {
        startMode.setSelectedId (e.startMode.load() + 1, juce::dontSendNotification); clickIn.setToggleState (e.countInClick.load(), juce::dontSendNotification);
        inst.setSelectedId (e.instrument.load() + 1, juce::dontSendNotification); tun.setSelectedId (e.tuning.load() + 1, juce::dontSendNotification);
        cents.setValue (e.customCents.load(), juce::dontSendNotification);
        strangle.setToggleState (e.strangle.load(), juce::dontSendNotification); muteBass.setToggleState (e.muteBassChair.load(), juce::dontSendNotification); bassUp.setToggleState (e.bassGiveWay.load(), juce::dontSendNotification);
        monitor.setToggleState (e.monitorGuitar.load(), juce::dontSendNotification);
        micGain.setValue (e.micGain.load(), juce::dontSendNotification); micMon.setToggleState (e.micMonitor.load(), juce::dontSendNotification); updateEnables();
    }
    void updateEnables() {
        cents.setEnabled (e.tuning.load() == (int) Tuning::Custom); clickIn.setEnabled (e.startMode.load() == 1);
        const bool bassvi = e.instrument.load() == (int) InputInstrument::BassVI;
        strangle.setEnabled (bassvi); bassUp.setEnabled (! e.muteBassChair.load());
        const bool mic = m.usePreview || e.isMicInputActive(); micGain.setEnabled (mic); micMon.setEnabled (mic);
    }
    const EngineStatus& st() { return m.usePreview ? m.preview : e.status(); }

    void resized() override {
        auto r = getLocalBounds().reduced (16);
        auto row = r.removeFromTop (36);
        for (auto* b : { &startBtn, &learnBtn }) { b->setBounds (row.removeFromLeft (150)); row.removeFromLeft (8); }
        learnBar.setBounds (row.removeFromLeft (170).reduced (0, 6)); row.removeFromLeft (24);
        for (auto* b : { &recBtn, &takesBtn }) { b->setBounds (row.removeFromLeft (150)); row.removeFromLeft (8); }
        row.removeFromLeft (16); startMode.setBounds (row.removeFromLeft (210).reduced (0, 3)); row.removeFromLeft (8); clickIn.setBounds (row.removeFromLeft (100));
        r.removeFromTop (6); bandLamp.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        row = r.removeFromTop (30);
        instL.setBounds (row.removeFromLeft (80)); inst.setBounds (row.removeFromLeft (150)); row.removeFromLeft (16);
        tunL.setBounds (row.removeFromLeft (56)); tun.setBounds (row.removeFromLeft (150)); row.removeFromLeft (16);
        centsL.setBounds (row.removeFromLeft (48)); cents.setBounds (row.removeFromLeft (150)); row.removeFromLeft (16);
        strangle.setBounds (row.removeFromLeft (100)); muteBass.setBounds (row.removeFromLeft (140)); bassUp.setBounds (row.removeFromLeft (170)); monitor.setBounds (row.removeFromLeft (120));
        r.removeFromTop (8);
        row = r.removeFromTop (28);
        micL.setBounds (row.removeFromLeft (64)); micGain.setBounds (row.removeFromLeft (200)); row.removeFromLeft (8);
        micMon.setBounds (row.removeFromLeft (120)); row.removeFromLeft (16); latency.setBounds (row);
        r.removeFromTop (10);
        info.setBounds (r.removeFromBottom (64));
        r.removeFromBottom (10);
        chordArea = r.removeFromLeft (juce::jmin (620, r.getWidth() / 2));
        r.removeFromLeft (16);
        meterArea = r.removeFromBottom (64);
        r.removeFromBottom (12);
        chromaArea = r;
    }
    void paint (juce::Graphics& g) override {
        g.fillAll (bg);
        const auto& s = st();
        // ---- chord panel
        g.setColour (panel); g.fillRoundedRectangle (chordArea.toFloat(), panelCorner);
        auto a = chordArea.reduced (20, 14);
        const auto t = (Tuning) e.tuning.load(); const float c = e.customCents.load();
        g.setColour (textDim); g.setFont (font (15)); g.drawText ("CHORD (sounding)", a.removeFromTop (20), juce::Justification::left);
        g.setColour (s.chord.valid() ? accent : textDim); g.setFont (font (88, true));
        g.drawText (s.chord.valid() ? juce::String (s.chord.name()) : juce::String ("--"), a.removeFromTop (100), juce::Justification::left);
        g.setColour (ink); g.setFont (font (18));
        juce::String shape = "--";
        if (s.chord.valid()) { shape = noteName (fingeredPc (s.chord.root, t, c)); shape << chordSuffix (s.chord.type); }
        g.drawText ("Shape as fingered (" + tuningNames()[(int) t] + "): " + shape, a.removeFromTop (28), juce::Justification::left);
        g.drawText ("Key: " + juce::String (keyName (s.key)), a.removeFromTop (28), juce::Justification::left);
        g.drawText ("Tempo: " + (s.beatLocked ? juce::String (s.bpm, 1) + " BPM (locked to count-in)" : juce::String (e.fallbackBpm.load(), 0) + " BPM (fallback, no count-in yet)"),
                    a.removeFromTop (28), juce::Justification::left);
        const auto ph = m.usePreview ? AudioEngine::Phase::Listening : e.phase();
        juce::String phase = ph == AudioEngine::Phase::Idle ? "Idle - press Start session" : ph == AudioEngine::Phase::Learning ? "Learning noise - don't play"
                           : s.countIn ? "Listening - count in (4 hits) or just play" : "Jamming";
        g.setColour (textDim); g.drawText ("Session: " + phase + "    Band: " + bandStateName (m.usePreview ? 1 : e.bandState.load()), a.removeFromTop (28), juce::Justification::left);
        // ---- chroma
        g.setColour (panel); g.fillRoundedRectangle (chromaArea.toFloat(), panelCorner);
        auto ca = chromaArea.reduced (14, 12);
        g.setColour (textDim); g.setFont (font (14)); g.drawText ("Pitch classes (sounding)", ca.removeFromTop (18), juce::Justification::left);
        auto lab = ca.removeFromBottom (18); const float w = (float) ca.getWidth() / 12.0f;
        for (int i = 0; i < 12; ++i) {
            auto bar = juce::Rectangle<float> ((float) ca.getX() + (float) i * w + 3, (float) ca.getY() + 4, w - 6, (float) ca.getHeight() - 4);
            g.setColour (panel2); g.fillRect (bar);
            g.setColour (i == s.chord.root ? accent : barCol);
            g.fillRect (bar.removeFromBottom (bar.getHeight() * juce::jlimit (0.0f, 1.0f, s.chroma[(size_t) i])));
            g.setColour (juce::Colours::lightgrey); g.drawText (noteName (i), juce::Rectangle<float> ((float) lab.getX() + (float) i * w, (float) lab.getY(), w, 18), juce::Justification::centred);
        }
        // ---- input meter with gate marker, mic meter under it
        g.setColour (panel); g.fillRoundedRectangle (meterArea.toFloat(), panelCorner * 0.8f);
        auto all = meterArea.reduced (10, 6);
        auto meter = [&] (juce::Rectangle<int> ma, const juce::String& name, float db, bool on, float gate) {
            auto lbl = ma.removeFromLeft (150);
            auto x = [&] (float d) { return (float) ma.getX() + (float) ma.getWidth() * juce::jlimit (0.0f, 1.0f, (d + 72.0f) / 72.0f); };
            g.setColour (panel2); g.fillRect (ma);
            if (on) {
                g.setColour (db > -3 ? juce::Colours::red : db > -12 ? juce::Colours::orange : okCol);
                g.fillRect (juce::Rectangle<float> ((float) ma.getX(), (float) ma.getY(), x (db) - (float) ma.getX(), (float) ma.getHeight()));
                if (gate > -100) { g.setColour (juce::Colours::white); g.fillRect (x (gate) - 1, (float) ma.getY() - 2, 2.0f, (float) ma.getHeight() + 4); }
            }
            g.setColour (on ? ink : textDim); g.setFont (font (14));
            g.drawText (on ? name + " " + juce::String (db, 1) + " dB" : name + " (no device)", lbl, juce::Justification::left);
        };
        const float db = m.usePreview ? -18.0f : juce::Decibels::gainToDecibels (e.inputPeak.load(), -72.0f);
        const float mdb = m.usePreview ? -24.0f : juce::Decibels::gainToDecibels (e.micPeak.load(), -72.0f);
        meter (all.removeFromTop (all.getHeight() / 2 - 2), "Input", db, true, s.gateDb);
        all.removeFromTop (4);
        meter (all, "Mic", mdb, m.usePreview || e.isMicInputActive(), -1000.0f);
    }
    void tick() override {
        const auto& s = st();
        const auto ph = e.phase();
        juce::ignoreUnused (ph); startBtn.setButtonText (startLabel());
        juce::Colour lc; bandLamp.setText (bandLampText (lc), juce::dontSendNotification); bandLamp.setColour (juce::Label::textColourId, lc);
        learnProg = m.usePreview ? 1.0 : e.learnProgress();
        recBtn.setButtonText (e.recorder.isRecording() ? "Stop recording" : "Record stems");
        juce::String t;
        t << "Noise: floor " << juce::String (s.floorDb, 1) << " dBFS, gate " << juce::String (s.gateDb, 1) << " dB, hum notches ";
        if (s.nNotches == 0) t << (s.noiseLearned ? "none" : "(not learned yet)");
        for (int k = 0; k < s.nNotches; ++k) t << juce::String (s.notchHz[k], 1) << (k + 1 < s.nNotches ? ", " : " Hz");
        t << "    Strangle auto-detect: " << (s.strangleDetected ? "ON" : "off") << " (" << juce::String (s.lowRatioDb, 1) << " dB)\n";
        t << "Audio glitches: " << e.glitches() << " (late " << e.cbLate.load() << ", gaps " << e.cbGaps.load() << ", NaN " << e.nanEvents.load() << ")";
        if (e.clipSamples.load() > 0) t << "  clipped " << e.clipSamples.load();
        if (auto* d = m.deviceManager.getCurrentAudioDevice())
            if (d->getName().containsIgnoreCase ("ASIO4ALL") && d->getCurrentBufferSizeSamples() < 128) t << "  WARNING: ASIO4ALL buffer " << d->getCurrentBufferSizeSamples() << " < 128 - use 256";
        t << "    Analysis dropped " << juce::String ((double) e.analysisDropped.load() / e.getSampleRate(), 2) << " s    Recorder dropped blocks: " << e.recorder.droppedBlocks.load();
        if (e.recorder.isRecording()) t << "    REC " << juce::String ((double) e.recorder.samplesWritten() / e.getSampleRate(), 1) << " s -> " << e.recorder.lastFolder.getFullPathName();
        info.setText (t, juce::dontSendNotification);
        latency.setText (m.latencyText (true), juce::dontSendNotification);
        updateEnables();
        repaint();
    }
};

// ================================================================================================ Band
struct BandPage : Content {
    MainComponent& m; AudioEngine& e;
    juce::Label holdL { {}, "Keep playing through silence" }, holdHelp { {}, "bars, then a 2-bar fade-out (held notes and their decay count as playing)" };
    juce::Slider hold { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label styleL { {}, "Style" }, bpmL { {}, "Fallback tempo" }, masterL { {}, "Master" }, gtrL { {}, "Input level" };
    juce::ComboBox style;
    juce::Slider bpm { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight }, master { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 gtr { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton addBtn { "+ Add chair" };
    struct Card : juce::Component {
        juce::Label title; juce::ComboBox type, role; juce::Slider gain { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::ToggleButton mute { "Mute" }, solo { "Solo" }; juce::TextButton remove { "Remove" };
        Card() { for (auto* c : std::initializer_list<juce::Component*> { &title, &type, &role, &gain, &mute, &solo, &remove }) addAndMakeVisible (c);
                 type.addItemList (chairTypeNames(), 1); role.addItemList (chairRoleNames(), 1); gain.setRange (0, 1.5, 0.01);
                 gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20); gain.setTextValueSuffix (" gain"); gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20); styleLabel (title, 17); title.setFont (font (17, true)); }
        void paint (juce::Graphics& g) override { g.setColour (panel); g.fillRoundedRectangle (getLocalBounds().toFloat(), panelCorner); }
        void resized() override {
            auto r = getLocalBounds().reduced (12, 10);
            title.setBounds (r.removeFromTop (26)); r.removeFromTop (4);
            type.setBounds (r.removeFromTop (26)); r.removeFromTop (6); role.setBounds (r.removeFromTop (26)); r.removeFromTop (6);
            gain.setBounds (r.removeFromTop (26)); r.removeFromTop (6);
            auto row = r.removeFromTop (26); mute.setBounds (row.removeFromLeft (70)); solo.setBounds (row.removeFromLeft (70)); remove.setBounds (row.removeFromRight (80));
        }
    };
    juce::OwnedArray<Card> cards;

    BandPage (MainComponent& mc, AudioEngine& en) : m (mc), e (en) {
        for (auto* c : std::initializer_list<juce::Component*> { &styleL, &bpmL, &masterL, &gtrL, &style, &bpm, &master, &gtr, &addBtn }) addAndMakeVisible (c);
        for (auto* l : { &styleL, &bpmL, &masterL, &gtrL, &holdL }) styleLabel (*l, 14, textDim);
        styleLabel (holdHelp, 13, textDim);
        for (auto* c : std::initializer_list<juce::Component*> { &holdL, &hold, &holdHelp }) addAndMakeVisible (c);
        hold.setRange (1, 16, 1); hold.setTextValueSuffix (" bars"); hold.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
        hold.onValueChange = [this] { e.holdBars = (int) hold.getValue(); };
        style.addItemList (styleNames, 1);
        bpm.setRange (50, 220, 1); bpm.setTextValueSuffix (" BPM"); master.setRange (0, 1.5, 0.01); gtr.setRange (0, 2, 0.01);
        for (auto* s : { &bpm, &master, &gtr }) s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
        style.onChange = [this] { e.style = style.getSelectedId() - 1; };
        bpm.onValueChange = [this] { e.fallbackBpm = (float) bpm.getValue(); };
        master.onValueChange = [this] { e.masterGain = (float) master.getValue(); };
        gtr.onValueChange = [this] { e.guitarGain = (float) gtr.getValue(); };
        addBtn.onClick = [this] { auto c = e.getChairs(); if (c.size() < (size_t) MaxChairs) { c.push_back ({ ChairType::Keys, 0, 0.7f }); e.setChairs (c); rebuild(); } };
        syncFromEngine();
    }
    void syncFromEngine() {
        style.setSelectedId (e.style.load() + 1, juce::dontSendNotification); bpm.setValue (e.fallbackBpm.load(), juce::dontSendNotification);
        hold.setValue (e.holdBars.load(), juce::dontSendNotification);
        master.setValue (e.masterGain.load(), juce::dontSendNotification); gtr.setValue (e.guitarGain.load(), juce::dontSendNotification); rebuild();
    }
    /** Rebuild after the current click handler returns (the clicked card is deleted by rebuild()). A plain member
        function, not a SafePointer built inside a nested lambda capture: MSVC /W4 rejected that pattern. */
    void requestRebuild() {
        juce::Component::SafePointer<juce::Component> sp (this);
        juce::MessageManager::callAsync ([sp] { if (auto* p = dynamic_cast<BandPage*> (sp.getComponent())) p->rebuild(); });
    }
    void rebuild() {
        cards.clear(); auto specs = e.getChairs();
        for (int i = 0; i < (int) specs.size(); ++i) {
            auto* c = cards.add (new Card()); addAndMakeVisible (c); const auto& s = specs[(size_t) i];
            c->title.setText ("Chair " + juce::String (i + 1), juce::dontSendNotification);
            c->type.setSelectedId ((int) s.type + 1, juce::dontSendNotification); c->role.setSelectedId (s.role + 1, juce::dontSendNotification);
            c->gain.setValue (s.gain, juce::dontSendNotification); c->mute.setToggleState (s.mute, juce::dontSendNotification); c->solo.setToggleState (s.solo, juce::dontSendNotification);
            auto edit = [this, i] (std::function<void (ChairSpec&)> f) { auto v = e.getChairs(); if (i < (int) v.size()) { f (v[(size_t) i]); e.setChairs (v); } };
            c->type.onChange = [edit, c] { edit ([c] (ChairSpec& cs) { cs.type = (ChairType) (c->type.getSelectedId() - 1); }); };
            c->role.onChange = [edit, c] { edit ([c] (ChairSpec& cs) { cs.role = c->role.getSelectedId() - 1; }); };
            c->gain.onValueChange = [this, i, c] { e.setChairGain (i, (float) c->gain.getValue()); };
            c->mute.onClick = [this, i, c] { e.setChairMute (i, c->mute.getToggleState()); };
            c->solo.onClick = [this, i, c] { e.setChairSolo (i, c->solo.getToggleState()); };
            c->remove.onClick = [this, i] { auto v = e.getChairs(); if (i < (int) v.size()) { v.erase (v.begin() + i); e.setChairs (v);
                requestRebuild(); } };
        }
        addBtn.setEnabled (specs.size() < (size_t) MaxChairs);
        resized();
    }
    void resized() override {
        auto r = getLocalBounds().reduced (16);
        auto row = r.removeFromTop (30);
        styleL.setBounds (row.removeFromLeft (44)); style.setBounds (row.removeFromLeft (120)); row.removeFromLeft (20);
        bpmL.setBounds (row.removeFromLeft (104)); bpm.setBounds (row.removeFromLeft (230)); row.removeFromLeft (20);
        masterL.setBounds (row.removeFromLeft (56)); master.setBounds (row.removeFromLeft (190)); row.removeFromLeft (20);
        gtrL.setBounds (row.removeFromLeft (80)); gtr.setBounds (row.removeFromLeft (190));
        r.removeFromTop (6); row = r.removeFromTop (30);
        holdL.setBounds (row.removeFromLeft (200)); hold.setBounds (row.removeFromLeft (230)); row.removeFromLeft (12); holdHelp.setBounds (row.removeFromLeft (520));
        r.removeFromTop (12);
        const int cw = 290, ch = 196, gap = 14; const int perRow = juce::jmax (1, (r.getWidth() + gap) / (cw + gap));
        for (int i = 0; i <= cards.size(); ++i) {
            auto cell = juce::Rectangle<int> (r.getX() + (i % perRow) * (cw + gap), r.getY() + (i / perRow) * (ch + gap), cw, ch);
            if (i < cards.size()) cards[i]->setBounds (cell); else addBtn.setBounds (cell.withSizeKeepingCentre (160, 40));
        }
    }
    void paint (juce::Graphics& g) override { g.fillAll (bg); }
};

// ================================================================================================ Inserts
struct InsertsPage : Content {
    AudioEngine& e; PluginLibrary& lib;
    PluginSlotsPanel slots; juce::TextButton scan { "Plug-ins..." }; juce::ToggleButton drive { "Placeholder drive (distortion)" }; juce::Label help, help2, latency;
    std::function<void()> onChanged;
    InsertsPage (AudioEngine& en, PluginLibrary& l) : e (en), lib (l), slots (en.plugins) {
        for (auto* c : std::initializer_list<juce::Component*> { &slots, &scan, &drive, &help, &help2, &latency }) addAndMakeVisible (c);
        drive.setToggleState (e.placeholderDrive.load(), juce::dontSendNotification);
        drive.setTooltip ("Soft-clip distortion ahead of the inserts. Off = clean guitar; turn on only if you have no amp sim loaded.");
        drive.onClick = [this] { e.placeholderDrive = drive.getToggleState(); };
        styleLabel (help, 14, textDim); styleLabel (help2, 14, textDim); styleLabel (latency, 15);
        help2.setJustificationType (juce::Justification::topLeft);
        help2.setText ("Inserts run inside the audio callback on the same samples (no extra buffer of delay); only a plug-in's own reported\n"
                       "latency adds to what you hear. Recorded stems are delay-compensated: DI, band and mic are shifted to line up with GuitarFX.",
                       juce::dontSendNotification);
        help.setText ("Four VST3 inserts on the input (after the optional placeholder drive - off by default - before the GuitarFX stem). The DI stem is always recorded dry.",
                      juce::dontSendNotification);
        scan.onClick = [this] { lib.showScanner(); };
        slots.refresh();
    }
    void resized() override {
        auto r = getLocalBounds().reduced (16);
        auto row = r.removeFromTop (32); scan.setBounds (row.removeFromLeft (140)); row.removeFromLeft (16); drive.setBounds (row.removeFromLeft (250)); row.removeFromLeft (16); latency.setBounds (row);
        r.removeFromTop (8); help.setBounds (r.removeFromTop (24)); r.removeFromTop (8);
        slots.setBounds (r.removeFromTop (4 * 30 + 8).removeFromLeft (720));
        r.removeFromTop (8); help2.setBounds (r.removeFromTop (40).removeFromLeft (900));
    }
    void paint (juce::Graphics& g) override { g.fillAll (bg); }
    void tick() override {
        const int lat = e.plugins.getTotalLatencySamples(); const double sr = e.getSampleRate();
        juce::String t; t << "Insert latency: " << lat << " smp (" << juce::String (1000.0 * lat / sr, 1) << " ms, compensated in the stems)  |";
        for (int i = 0; i < PluginChain::NumSlots; ++i) {
            t << "  " << (i + 1) << ": ";
            if (e.plugins.getInstance (i) == nullptr) t << "-";
            else t << e.plugins.slotLatencySamples (i) << (e.plugins.isBypassed (i) ? " (byp)" : "");
        }
        latency.setText (t, juce::dontSendNotification);
    }
};

// ================================================================================================ Audio
struct AudioPage : Content {
    MainComponent& m;
    juce::AudioDeviceSelectorComponent sel; juce::Label dev, splitL { {}, "Guitar input on its own clock (split input)" }, splitStat;
    juce::ComboBox splitBox; juce::Label autoSplit { {}, "Optional, off by default. If your guitar input and output are different physical devices inside ASIO4ALL, their clocks drift (static after a while) unless you use split input." };
    juce::TextButton panelBtn { "Driver control panel" };
    juce::TextButton rescan { "Rescan" };
    juce::Label inputWhy; juce::TextButton fixBtn;   // why the guitar has no input + one-click fix (or "input OK")
    int fixId = 0;
    juce::ToggleButton builtin { "Low latency (built-in, no ASIO needed)" };
    juce::Label builtinStat, asioHelp, asioTm;
    juce::TextButton installFlex { "Setup check (ASIO drivers, sounds)..." };
    juce::HyperlinkButton asio4allLink { "Get ASIO4ALL (asio4all.org)", juce::URL ("https://asio4all.org") },
                          flexLink { "FlexASIO on GitHub", juce::URL ("https://github.com/dechamps/FlexASIO") };
    juce::String builtinMsg;
    juce::ToggleButton lowLat { "Low-latency input mode (WASAPI IAudioClient3, smallest period)" };
    juce::Label bufL { {}, "Output buffer" }, micL { {}, "Microphone (own clock, recorded as the Mic stem)" }, trimL { {}, "Recording trim (ms)" }, bufHint, micStat;
    juce::ComboBox bufBox, micBox;
    juce::Slider trim { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    explicit AudioPage (MainComponent& mc) : m (mc), sel (mc.deviceManager, 1, 1, 2, 2, false, false, false, false) {
        for (auto* c : std::initializer_list<juce::Component*> { &sel, &dev, &inputWhy, &fixBtn, &splitL, &splitStat, &splitBox, &autoSplit, &rescan, &panelBtn,
                                                                  &builtin, &builtinStat, &asioHelp, &asioTm, &installFlex, &asio4allLink, &flexLink, &lowLat, &bufL, &micL, &trimL, &bufHint, &micStat, &bufBox, &micBox, &trim }) addAndMakeVisible (c);
        styleLabel (inputWhy, 13); inputWhy.setJustificationType (juce::Justification::topLeft);
        fixBtn.onClick = [this] { if (fixId != 0) { m.applyInputFix (fixId); m.saveSession(); refresh(); tick(); } };
        styleLabel (dev, 14, textDim); styleLabel (splitL, 15); styleLabel (splitStat, 14, textDim);
        styleLabel (bufL, 14); styleLabel (micL, 15); styleLabel (trimL, 14); styleLabel (bufHint, 13, textDim); styleLabel (micStat, 14, textDim);
        micStat.setJustificationType (juce::Justification::topLeft);
        styleLabel (builtinStat, 13, textDim); builtinStat.setJustificationType (juce::Justification::topLeft);
        styleLabel (asioHelp, 13, textDim); asioHelp.setJustificationType (juce::Justification::topLeft);
        styleLabel (asioTm, 12, textDim); asioTm.setJustificationType (juce::Justification::topLeft);
        asioHelp.setText ("No ASIO driver? Tick \"Low latency (built-in)\" on the right: it uses Windows' own exclusive audio mode, nothing to install.\n"
                          "Prefer a real ASIO driver? ASIO4ALL is free from https://asio4all.org (FtheBand only opens the page; it never installs it). FlexASIO (free, MIT licence) "
                          "can be downloaded from its official GitHub release by the button below, only after you confirm. Restart FtheBand after installing.", juce::dontSendNotification);
        asioTm.setText ("ASIO is a trademark of Steinberg Media Technologies GmbH. FtheBand is not ASIO-certified and is not endorsed by or affiliated with Steinberg.", juce::dontSendNotification);
        for (auto* l : { &asio4allLink, &flexLink }) { l->setColour (juce::HyperlinkButton::textColourId, accent); l->setFont (font (13), false, juce::Justification::centredLeft); l->getProperties().set ("sjLinkSize", 13.0); }
        installFlex.setTooltip ("Checks what this PC has (ASIO drivers, band sounds, guitar cable) and lists it. Nothing is downloaded or installed unless you click a button in that window.");
        installFlex.onClick = [this] { m.showSetupDialog(); };
        builtin.onClick = [this] {
            const bool on = builtin.getToggleState();
            builtinStat.setText ("Opening...", juce::dontSendNotification);
            builtinMsg = m.setBuiltInLowLatency (on); refresh();
        };
        lowLat.onClick = [this] { m.preferLowLatency = lowLat.getToggleState(); if (m.splitDevice.isNotEmpty()) m.openSplitInput (m.splitType, m.splitDevice);
                                  if (m.micDevice.isNotEmpty()) m.openMicInput (m.micDevice); m.saveSession(); };
        bufBox.onChange = [this] {
            const int bs = bufBox.getSelectedId(); if (bs <= 0) return;
            auto s = m.deviceManager.getAudioDeviceSetup(); if (s.bufferSize == bs) return;
            s.bufferSize = bs; m.deviceManager.setAudioDeviceSetup (s, true);   // change listener reopens the split inputs
        };
        bufHint.setText ("128-256 for low latency (ASIO4ALL: never below 128); raise it if the Jam tab shows audio glitches", juce::dontSendNotification);
        micBox.onChange = [this] {
            const int id = micBox.getSelectedId(); if (id <= 0) return;
            if (id == 1) m.closeMicInput(); else m.openMicInput (micBox.getText());
            m.saveSession(); refresh();
        };
        trim.setRange (-50, 50, 0.1); trim.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
        trim.setTooltip ("Shifts the band stem against your recorded guitar/mic. + if your playing lands late in the takes");
        trim.onValueChange = [this] { m.trimMs = (float) trim.getValue(); m.updateLatency (true); };
        trim.onDragEnd = [this] { m.saveSession(); };
        splitStat.setJustificationType (juce::Justification::topLeft);
        splitBox.onChange = [this] { choose(); };
        styleLabel (autoSplit, 13, textDim); autoSplit.setJustificationType (juce::Justification::topLeft);
        panelBtn.onClick = [this] {
            auto* d = m.deviceManager.getCurrentAudioDevice(); if (d == nullptr || ! d->hasControlPanel()) return;
            if (d->showControlPanel()) {   // driver changed its routing: reopen so channels/buffer sizes are re-scanned
                m.deviceManager.closeAudioDevice(); m.deviceManager.restartLastAudioDevice();
                if (auto* t = m.deviceManager.getCurrentDeviceTypeObject()) t->scanForDevices();
            }
            m.saveSession(); refresh(); };
        rescan.onClick = [this] { refresh(); };
        refresh();
    }
    void refresh() {
        splitBox.clear (juce::dontSendNotification);
        splitBox.addItem ("(off: use the input selected on the left)", 1);
        const auto names = m.splitCandidates();
        for (int i = 0; i < names.size(); ++i) splitBox.addItem (names[i], i + 2);
        const int idx = names.indexOf (m.splitDevice);
        splitBox.setSelectedId (m.splitDevice.isEmpty() || idx < 0 ? 1 : idx + 2, juce::dontSendNotification);
        { auto* d = m.deviceManager.getCurrentAudioDevice(); panelBtn.setEnabled (d != nullptr && d->hasControlPanel()); }
        lowLat.setToggleState (m.preferLowLatency, juce::dontSendNotification);
        builtin.setToggleState (m.isBuiltInLowLatency(), juce::dontSendNotification);
        builtinStat.setText (builtinMsg.isNotEmpty() ? builtinMsg
            : juce::String ("WASAPI exclusive mode at the smallest stable buffer (128-256 smp at 48 kHz). The output device is used exclusively, so other apps are silent on it. "
                            "The guitar input keeps its own clock (split input, drift-corrected). Falls back to shared WASAPI if it cannot open."), juce::dontSendNotification);
        trim.setValue (m.trimMs, juce::dontSendNotification);
        micBox.clear (juce::dontSendNotification);
        micBox.addItem ("(no microphone)", 1);
        const auto mics = m.micCandidates();
        for (int i = 0; i < mics.size(); ++i) micBox.addItem (mics[i], i + 2);
        const int mi = mics.indexOf (m.micDevice);
        micBox.setSelectedId (m.micDevice.isEmpty() || mi < 0 ? 1 : mi + 2, juce::dontSendNotification);
        bufBox.clear (juce::dontSendNotification);
        if (auto* d = m.deviceManager.getCurrentAudioDevice()) {
            for (int b : d->getAvailableBufferSizes()) if (b <= 2048)
                bufBox.addItem (juce::String (b) + " smp (" + juce::String (1000.0 * b / d->getCurrentSampleRate(), 1) + " ms)", b);
            bufBox.setSelectedId (d->getCurrentBufferSizeSamples(), juce::dontSendNotification);
        }
    }
    /** JUCE's own "Audio device type" box only follows change messages; make it always show the type of the device that is really open. */
    void syncTypeCombo() {
        const auto type = m.deviceManager.getCurrentAudioDeviceType();
        if (type.isEmpty() || m.deviceManager.getCurrentAudioDevice() == nullptr) return;
        for (auto* c : sel.getChildren())
            if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
                if (cb->getText() != type) cb->setText (type, juce::dontSendNotification);
    }
    void choose() {
        const int id = splitBox.getSelectedId(); if (id <= 0) return;
        if (id == 1) m.closeSplitInput();
        else m.openSplitInput (m.splitType, splitBox.getText());
        m.saveSession(); refresh();
    }
    void tick() override {
        syncTypeCombo();
        { auto is = m.inputIssue();
          inputWhy.setText (is.text, juce::dontSendNotification);
          inputWhy.setColour (juce::Label::textColourId, is.ok ? textDim : juce::Colour (0xffe8a23c));
          fixId = is.fix; fixBtn.setButtonText (is.action); fixBtn.setVisible (is.fix != 0); }
        splitStat.setText (m.splitStatus + "\n" + m.latencyText (false), juce::dontSendNotification);
        juce::String t;
        if (m.micDevice.isEmpty()) t = "Microphone: off";
        else if (m.micError.isNotEmpty()) t = "Microphone ERROR: " + m.micError;
        else if (m.mic != nullptr) {
            auto& tl = m.engine.driftMic.tel;
            t << "Mic: " << m.mic->typeName << " / " << m.micDevice << "  block " << m.mic->inputBlock() << "  FIFO " << juce::String (1000.0 * tl.target.load() / m.engine.getSampleRate(), 1)
              << " ms  drift " << juce::String (tl.ppm.load(), 0) << " ppm  xfades " << (tl.underruns.load() + tl.overruns.load())
              << "\nThe mic is recorded as its own stem and is never used for chord/tempo following. Monitoring is on the Jam tab (default off).";
        }
        micStat.setText (t, juce::dontSendNotification);
    }
    void resized() override {
        auto r = getLocalBounds().reduced (16); dev.setBounds (r.removeFromTop (24)); r.removeFromTop (8);
        auto left = r.removeFromLeft (640); r.removeFromLeft (20);
        asioTm.setBounds (left.removeFromBottom (30)); left.removeFromBottom (4);
        auto lrow = left.removeFromBottom (26); installFlex.setBounds (lrow.removeFromLeft (250)); lrow.removeFromLeft (10);
        asio4allLink.setBounds (lrow.removeFromLeft (190)); flexLink.setBounds (lrow.removeFromLeft (150)); left.removeFromBottom (6);
        asioHelp.setBounds (left.removeFromBottom (58)); left.removeFromBottom (6);
        { auto strip = left.removeFromBottom (64); fixBtn.setBounds (strip.removeFromRight (190).removeFromTop (28)); strip.removeFromRight (8);
          inputWhy.setBounds (strip); left.removeFromBottom (4); }
        sel.setBounds (left);
        builtin.setBounds (r.removeFromTop (26)); builtinStat.setBounds (r.removeFromTop (52)); r.removeFromTop (10);
        splitL.setBounds (r.removeFromTop (24)); r.removeFromTop (6);
        auto row = r.removeFromTop (28); splitBox.setBounds (row.removeFromLeft (juce::jmin (420, row.getWidth() - 270))); row.removeFromLeft (8); rescan.setBounds (row.removeFromLeft (80)); row.removeFromLeft (8); panelBtn.setBounds (row.removeFromLeft (juce::jmin (170, row.getWidth())));
        r.removeFromTop (4); autoSplit.setBounds (r.removeFromTop (34));
        lowLat.setBounds (r.removeFromTop (24)); r.removeFromTop (6);
        row = r.removeFromTop (26); bufL.setBounds (row.removeFromLeft (100)); bufBox.setBounds (row.removeFromLeft (150)); row.removeFromLeft (8); bufHint.setBounds (row);
        r.removeFromTop (4);
        row = r.removeFromTop (26); trimL.setBounds (row.removeFromLeft (130)); trim.setBounds (row.removeFromLeft (juce::jmin (300, row.getWidth())));
        r.removeFromTop (6);
        splitStat.setBounds (r.removeFromTop (150)); r.removeFromTop (6);
        micL.setBounds (r.removeFromTop (24)); r.removeFromTop (4);
        row = r.removeFromTop (28); micBox.setBounds (row.removeFromLeft (juce::jmin (420, row.getWidth()))); r.removeFromTop (4);
        micStat.setBounds (r.removeFromTop (64));
    }
    void paint (juce::Graphics& g) override { g.fillAll (bg); }
};

// ================================================================================================ Get more sounds
/** Browse the online pack catalog, download/verify/install a pack, hot-load it. Designed for 1200x600 (fits 1280x720).
    All network + disk + decoding happens on CatalogFetcher / PackInstaller threads; the audio thread only ever sees a
    finished BandConfig swapped in lock-free. Offline = a message here, bundled sounds untouched. */
struct SoundsPage : Content, juce::ListBoxModel {
    AudioEngine& e;
    juce::Label title, status, detail, help; juce::ListBox list { "packs", this };
    juce::TextButton refresh { "Refresh catalog" }, get { "Download" }, remove { "Remove" }, cancel { "Cancel" }, openFolder { "Open folder" };
    double prog = 0; juce::ProgressBar bar { prog };
    CatalogFetcher fetcher; PackInstaller installer;
    std::vector<PackInfo> packs; bool fetchedOnce = false;

    explicit SoundsPage (AudioEngine& en) : e (en) {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &status, &detail, &help, &list, &refresh, &get, &remove, &cancel, &openFolder, &bar })
            addAndMakeVisible (c);
        styleLabel (title, 22); title.setFont (font (22, true)); title.setText ("Get more sounds", juce::dontSendNotification);
        styleLabel (status, 14, textDim); styleLabel (help, 13, textDim);
        styleLabel (detail, 15); detail.setJustificationType (juce::Justification::topLeft); detail.setMinimumHorizontalScale (1.0f);
        help.setJustificationType (juce::Justification::topLeft);
        help.setText ("Packs are listed from the FtheBand catalog on GitHub. Only licence-checked CC0 / CC-BY packs are offered.\n"
                      "Packs marked [installed, bundled] ship with the app (samples/packs next to FtheBand.exe): no download needed.\n"
                      "Other downloads go to " + SampleLibrary::userSamplesDir().getFullPathName() + " and are sha256-verified. The built-in sounds always work, online or not.", juce::dontSendNotification);
        list.setRowHeight (30); list.setColour (juce::ListBox::backgroundColourId, panel2);
        refresh.onClick = [this] { startFetch(); };
        get.onClick = [this] { if (auto* p = selected()) installer.start (*p, [this] (bool ok, juce::String m, std::shared_ptr<SampleLibrary> lib) { finished (ok, m, lib); }); };
        remove.onClick = [this] { if (auto* p = selected()) installer.startRemove (p->id, [this] (bool ok, juce::String m, std::shared_ptr<SampleLibrary> lib) { finished (ok, m, lib); }); };
        cancel.onClick = [this] { installer.cancel(); };
        openFolder.onClick = [] { auto d = SampleLibrary::userSamplesDir(); d.createDirectory(); d.startAsProcess(); };
        // installed-but-offline packs still show (and can be removed) before any catalog fetch
        packs = SamplePacks::installed(); list.updateContent();
        status.setText (packs.empty() ? "Press Refresh catalog to see available packs." : "Installed packs shown. Refresh to see the catalog.", juce::dontSendNotification);
        updateButtons();
    }
    const PackInfo* selected() const { const int r = list.getSelectedRow(); return r >= 0 && r < (int) packs.size() ? &packs[(size_t) r] : nullptr; }
    void startFetch() {
        if (fetcher.busy()) return;
        status.setText ("Fetching catalog...", juce::dontSendNotification); fetchedOnce = true;
        fetcher.fetch ([this] (bool ok, std::vector<PackInfo> p, juce::String err) {
            if (! ok) { status.setText (err, juce::dontSendNotification); return; }   // keep whatever was listed
            auto inst = SamplePacks::installed();
            for (auto& i : inst) if (std::none_of (p.begin(), p.end(), [&] (auto& c) { return c.id == i.id; })) p.push_back (i);   // retired from catalog
            packs = std::move (p); list.updateContent(); list.repaint();
            status.setText (juce::String ((int) packs.size()) + " pack(s)" + (err.isNotEmpty() ? "  -  " + err : juce::String()), juce::dontSendNotification);
            selectedRowsChanged (list.getSelectedRow());
        });
    }
    void finished (bool ok, const juce::String& msg, std::shared_ptr<SampleLibrary> lib) {
        if (ok && lib != nullptr) e.installLibrary (lib);   // message thread; lock-free swap into the band
        status.setText (msg, juce::dontSendNotification);
        list.repaint(); selectedRowsChanged (list.getSelectedRow());
    }
    void updateButtons() {
        auto* p = selected(); const bool busy = installer.busy();
        const bool bundled = p != nullptr && SamplePacks::isBundled (p->id);
        const bool inst = p != nullptr && SamplePacks::isInstalled (p->id);
        get.setEnabled (! busy && p != nullptr && ! p->entries.empty() && ! bundled); get.setButtonText (bundled ? "Bundled" : inst ? "Re-download" : "Download");
        remove.setEnabled (! busy && inst && ! bundled);   // bundled packs live beside the exe: nothing to remove or re-download cancel.setEnabled (busy); refresh.setEnabled (! busy);
    }
    // ListBoxModel
    int getNumRows() override { return (int) packs.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override {
        if (row < 0 || row >= (int) packs.size()) return;
        auto& p = packs[(size_t) row];
        if (sel) g.fillAll (accent.withAlpha (0.35f));
        g.setColour (ink); g.setFont (font (15, true));
        g.drawText (p.name, 10, 0, w - 260, h, juce::Justification::centredLeft, true);
        g.setColour (textDim); g.setFont (font (13));
        g.drawText (p.licence + "  " + SamplePacks::formatSize (p.size) + (SamplePacks::isBundled (p.id) ? "  [installed, bundled]" : SamplePacks::isInstalled (p.id) ? "  [installed]" : ""),
                    w - 250, 0, 240, h, juce::Justification::centredRight, true);
    }
    void selectedRowsChanged (int) override {
        if (auto* p = selected())
            detail.setText (p->name + "\n\nLicence: " + p->licence + "\nCredit: " + (p->credit.isNotEmpty() ? p->credit : juce::String ("-"))
                            + "\nSize: " + SamplePacks::formatSize (p->size) + (p->entries.empty() ? juce::String() : "   Files: " + juce::String ((int) p->entries.size()))
                            + (SamplePacks::isBundled (p->id) ? "\nBundled with the app (installed, loaded on every start)." : juce::String())
                            + "\n\n" + p->description, juce::dontSendNotification);
        else detail.setText ({}, juce::dontSendNotification);
        updateButtons();
    }
    void tick() override {
        if (! fetchedOnce && isShowing()) startFetch();   // first visit fetches automatically
        prog = installer.busy() ? (double) installer.progress() : 0.0;
        if (installer.busy()) status.setText (installer.status(), juce::dontSendNotification);
        updateButtons();
    }
    void resized() override {
        auto r = getLocalBounds().reduced (20);
        auto top = r.removeFromTop (34); title.setBounds (top.removeFromLeft (260));
        refresh.setBounds (top.removeFromRight (150)); top.removeFromRight (8); openFolder.setBounds (top.removeFromRight (120));
        r.removeFromTop (8);
        help.setBounds (r.removeFromBottom (60)); r.removeFromBottom (6);
        auto st = r.removeFromBottom (26); status.setBounds (st);
        r.removeFromBottom (6);
        auto bottom = r.removeFromBottom (32);
        get.setBounds (bottom.removeFromLeft (130)); bottom.removeFromLeft (8); remove.setBounds (bottom.removeFromLeft (100));
        bottom.removeFromLeft (8); cancel.setBounds (bottom.removeFromLeft (90)); bottom.removeFromLeft (12); bar.setBounds (bottom);
        r.removeFromBottom (8);
        list.setBounds (r.removeFromLeft (juce::jmin (640, r.getWidth() / 2 + 60))); r.removeFromLeft (16);
        detail.setBounds (r); list.updateContent();
    }
    void paint (juce::Graphics& g) override { g.fillAll (bg); }
};

// ================================================================================================ About
struct AboutPage : Content {
    juce::Label title, body, packCredits, skinLabel; juce::ComboBox skinBox; juce::HyperlinkButton licence, source, donate; juce::String lastCredits = "-";
    MainComponent& owner;
    explicit AboutPage (MainComponent& mc, const juce::String& donateUrl) : owner (mc) {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &body, &licence, &packCredits, &skinLabel, &skinBox }) addAndMakeVisible (c);
        // Look (default / FtheBand page skin): the app has no Settings tab, so the switch lives here. Applies at once and is saved.
        styleLabel (skinLabel, 14, textDim); skinLabel.setText ("Look:", juce::dontSendNotification); skinLabel.setJustificationType (juce::Justification::centredRight);
        skinBox.addItem ("Default", 1); skinBox.addItem ("FtheBand page (optional skin)", 2); skinBox.setSelectedId (mc.getSkin() == 1 ? 2 : 1, juce::dontSendNotification);
        skinBox.onChange = [this, &mc] { mc.setSkin (skinBox.getSelectedId() == 2 ? 1 : 0); };
        styleLabel (packCredits, 13); packCredits.setJustificationType (juce::Justification::topLeft); packCredits.setMinimumHorizontalScale (1.0f);
        styleLabel (title, 28); title.setFont (font (28, true));
        title.setText (juce::String ("FtheBand ") + FTHEBAND_DISPLAY_VERSION, juce::dontSendNotification);
        styleLabel (body, 15); body.setMinimumHorizontalScale (1.0f); body.setJustificationType (juce::Justification::topLeft);
        body.setText ("A band that follows you: play, and drums, bass and keys follow your chords, tempo and intensity.\n\n"
                      "Copyright (C) 2026 FtheBand contributors.\n"
                      "This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public\n"
                      "License, version 3. It comes with ABSOLUTELY NO WARRANTY. If you run a modified version for others over a network,\n"
                      "you must offer them its source. Built with JUCE (AGPLv3). Corresponding source: the link below and SOURCE.txt in the download.\n\n"
                      "Wurlitzer EP200 samples by Greg Sullivan (sullivang.net), SFZ mapping by kinwie, licensed under CC BY 3.0\n"
                      "(creativecommons.org/licenses/by/3.0). Unmodified; resampled and re-pitched on playback.\n"
                      "Drum, cymbal and bass samples: Karoryfer Samples (Gogodze Phu Vol. II, Black And Blue Basses) and\n"
                      "Versilian Studios (VCSL), CC0. Full list: CREDITS.md. Sound packs bundled in samples/packs are listed below.\n\n"
                      "FtheBand contains no third-party installers. The optional FlexASIO driver (MIT licence, github.com/dechamps/FlexASIO) is only downloaded\n"
                      "from its official GitHub release if you ask for it in the Setup check. Licences of FlexASIO and its libraries: THIRD-PARTY-NOTICES.md.\n"
                      "ASIO is a trademark and software of Steinberg Media Technologies GmbH. FtheBand is not ASIO-certified and is not endorsed by Steinberg.",
                      juce::dontSendNotification);
        licence.setButtonText ("GNU AGPL v3 licence text"); licence.setURL (juce::URL ("https://www.gnu.org/licenses/agpl-3.0.html"));
        const juce::String src (FTHEBAND_SOURCE_URL);
        if (src.isNotEmpty()) { source.setButtonText ("Source code (AGPL-3.0): " + src + (juce::String (FTHEBAND_COMMIT).isNotEmpty() ? "   [version " + juce::String (FTHEBAND_DISPLAY_VERSION) + ", commit " + juce::String (FTHEBAND_COMMIT) + "]" : juce::String())); source.setURL (juce::URL (src)); addAndMakeVisible (source); }
        if (donateUrl.isNotEmpty()) { donate.setButtonText ("Donate"); donate.setURL (juce::URL (donateUrl)); addAndMakeVisible (donate); }  // hidden unless configured
        for (auto* h : { &licence, &source, &donate }) h->setJustificationType (juce::Justification::left);
    }
    void resized() override {
        auto r = getLocalBounds().reduced (24);
        auto row = r.removeFromTop (40); skinBox.setBounds (row.removeFromRight (260).reduced (0, 6)); skinLabel.setBounds (row.removeFromRight (60)); title.setBounds (row);
        r.removeFromTop (8); body.setBounds (r.removeFromTop (345));
        licence.setBounds (r.removeFromTop (26).removeFromLeft (400)); source.setBounds (r.removeFromTop (26).removeFromLeft (700));
        r.removeFromTop (10); donate.setBounds (r.removeFromTop (30).removeFromLeft (120));
        r.removeFromTop (6); packCredits.setBounds (r);
    }
    int ticks = 0;
    void tick() override {   // CC-BY credits for downloaded packs (re-read ~1 Hz while visible)
        const int want = owner.getSkin() == 1 ? 2 : 1; if (skinBox.getSelectedId() != want) skinBox.setSelectedId (want, juce::dontSendNotification);
        if (ticks++ % 30 != 0) return;
        auto c = SamplePacks::attributionText();
        if (c != lastCredits) { lastCredits = c; packCredits.setText (c.isEmpty() ? juce::String() : "Sound packs, bundled and downloaded (CC-BY credits are required; CC0 credited as a courtesy):\n" + c, juce::dontSendNotification); }
    }
    void paint (juce::Graphics& g) override { g.fillAll (bg); }
};

// ================================================================================================ MainComponent
juce::File MainComponent::settingsFile()
{ return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ftheband").getChildFile ("session.xml"); }
juce::File MainComponent::recordingsFolder()
{ return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ftheband Takes"); }

float MainComponent::autoScaleFor (juce::Rectangle<int> area)
{ return juce::jlimit (0.5f, 1.0f, juce::jmin ((float) area.getWidth() / (float) LogicalW, (float) area.getHeight() / (float) LogicalH)); }

MainComponent::MainComponent (bool openAudio) : audioOpen (openAudio)
{
    donateUrl = FTHEBAND_DONATE_URL;
    if (audioOpen) loadSession();
    if (skin == 1) enableSkinGlobals (true);   // before the pages exist, so they are built with the skin's fonts and colours
    tabs.setTabBarDepth (34); tabs.setOutline (0);
    auto add = [this] (const juce::String& name, Content* c, int mw, int mh) {
        auto* p = pages.add (new Page (std::unique_ptr<Content> (c), mw, mh)); tabs.addTab (name, panel, p, false); };
    auto* jam = new JamPage (*this, engine); onStartStop = [jam] { jam->startStop(); };
    add ("Jam", jam, 1200, 600);
    setWantsKeyboardFocus (true);
    add ("Band", new BandPage (*this, engine), 1200, 560);
    auto* ins = new InsertsPage (engine, pluginLibrary); add ("Inserts", ins, 800, 260);
    add ("Audio", new AudioPage (*this), 1150, 690);
    add ("Get more sounds", new SoundsPage (engine), 1200, 600);
    add ("About", new AboutPage (*this, donateUrl), 900, 640);
    addAndMakeVisible (tabs); addAndMakeVisible (statusBar); styleLabel (statusBar, 13, textDim);

    if (audioOpen) {
        setupAudioDevices();
        deviceManager.addAudioCallback (&engine);
        if (savedSession != nullptr)
            if (auto* pc = savedSession->getChildByName ("PluginChain")) {
                juce::StringArray errs; engine.plugins.restoreFromXml (*pc, errs);
                if (! errs.isEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Some plug-ins failed to load", errs.joinIntoString ("\n"));
            }
        ins->slots.refresh();
        ins->slots.onChainChanged = [this] { saveSession(); };
        deviceManager.addChangeListener (this);
        changeListenerCallback (nullptr);
        juce::Timer::callAfterDelay (1500, [sp = juce::Component::SafePointer<MainComponent> (this)] { if (sp != nullptr) sp->maybeShowSetupAtStart(); });
    }
    if (skin == 1) { setLookAndFeel (skinLnf.get()); for (int i = 0; i < tabs.getNumTabs(); ++i) tabs.setTabBackgroundColour (i, panel);
                     { std::set<juce::Component*> done; for (auto* pg : pages) { done.insert (pg); restyleTree (*pg, currentPal(), currentPal(), done); } restyleTree (*this, currentPal(), currentPal(), done); } }   // hyperlinks keep a plain default font otherwise
    setSize (LogicalW, LogicalH);
    startTimerHz (30);
}

/** Palette + embedded OFL fonts (Source Serif 4, JetBrains Mono; see THIRD-PARTY-NOTICES.md) for the page skin, or back to the defaults. */
void MainComponent::enableSkinGlobals (bool page)
{
    applyPalette (page);
    if (page) {
        auto tf = [] (const char* d, int n) { return juce::Typeface::createSystemTypefaceFor (d, (size_t) n); };
        skinSerif = tf (BinaryData::SourceSerif4Regular_ttf, BinaryData::SourceSerif4Regular_ttfSize);
        skinSerifBold = tf (BinaryData::SourceSerif4Bold_ttf, BinaryData::SourceSerif4Bold_ttfSize);
        if (skinLnf == nullptr)
            skinLnf = std::make_unique<sj::PageSkin> (skinSerif, skinSerifBold, tf (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize),
                                                      tf (BinaryData::JetBrainsMonoBold_ttf, BinaryData::JetBrainsMonoBold_ttfSize));
    } else { skinSerif = nullptr; skinSerifBold = nullptr; }
}

void MainComponent::setSkin (int newSkin)
{
    newSkin = newSkin == 1 ? 1 : 0;
    if (newSkin == skin) return;
    const auto before = currentPal();
    skin = newSkin; enableSkinGlobals (skin == 1);
    setLookAndFeel (skin == 1 ? skinLnf.get() : nullptr);
    for (int i = 0; i < tabs.getNumTabs(); ++i) tabs.setTabBackgroundColour (i, panel);
    { std::set<juce::Component*> done; for (auto* pg : pages) { done.insert (pg); restyleTree (*pg, before, currentPal(), done); } restyleTree (*this, before, currentPal(), done); }   // tabs that are not showing are not children yet
    if (auto* w = getTopLevelComponent()) if (auto* dw = dynamic_cast<juce::DocumentWindow*> (w)) dw->setBackgroundColour (windowBackground (skin));
    sendLookAndFeelChange(); resized(); repaint();
    if (audioOpen) saveSession();
}

MainComponent::~MainComponent()
{
    stopTimer(); setLookAndFeel (nullptr);
    if (audioOpen) { saveSession(); deviceManager.removeChangeListener (this); split.reset(); mic.reset(); engine.setSplitInputActive (false); engine.setMicInputActive (false);
                     deviceManager.removeAudioCallback (&engine); }
}

/** Startup device policy:
    1) restore saved DEVICESETUP (+ <SplitInput type device/>) if present;
    2) Windows, ASIO4ALL present and a USB guitar interface visible to WASAPI: ASIO4ALL OUTPUT-ONLY (enable only the Realtek in
       its panel) @ 48 kHz / 128 (64 if available) + split input: the interface on its own WASAPI stream, drift-corrected;
    3) Windows, ASIO4ALL without a separate cable: ASIO4ALL in+out (one clock domain only if ASIO4ALL is set up that way);
    4) no ASIO4ALL: built-in low latency = WASAPI exclusive output (48 kHz, smallest stable buffer >= 128, stepped up if it glitches) + the
       default capture device as a split input; if exclusive fails, the low-latency shared type, then plain shared WASAPI. Plain shared
       tries the real Windows default output first and then EVERY other output in turn (display/HDMI audio last) until one opens;
    5) else the platform default (ALSA/JACK on Linux, CoreAudio on macOS). Input = mono channel 0.
    Afterwards, maybeAutoSplit() turns any "input device != output device" setup into split-input mode. */
void MainComponent::setupAudioDevices()
{
    auto* saved = savedSession ? savedSession->getChildByName ("DEVICESETUP") : nullptr;
    if (auto* si = savedSession ? savedSession->getChildByName ("SplitInput") : nullptr) {
        // v2 = split input is a manual opt-in; older sessions (auto-adopted cable) migrate to off
        if (si->getIntAttribute ("version", 1) >= 2) { splitType = si->getStringAttribute ("type"); splitDevice = si->getStringAttribute ("device"); }
        else { splitType = splitDevice = {}; }
    }
    if (saved != nullptr && deviceManager.initialise (1, 2, saved, false).isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr) {
        auto cur = deviceManager.getAudioDeviceSetup(); bool change = false;
        // no split input: the guitar must come from the selected device itself (old split-era sessions saved 0 input channels)
        if (splitDevice.isEmpty() && cur.inputChannels.isZero()) {
            if (cur.inputDeviceName.isEmpty()) cur.inputDeviceName = cur.outputDeviceName;   // ASIO: one driver for in+out
            cur.inputChannels.clear(); cur.inputChannels.setBit (0); cur.useDefaultInputChannels = false; change = true;
            logEvent ("input: enabled input channel 1 of the selected device (split input is off)"); }
        if (auto* d = deviceManager.getCurrentAudioDevice())
            if (d->getName().containsIgnoreCase ("ASIO4ALL") && d->getCurrentBufferSizeSamples() < 128) {
                int want = 0; for (int bsz : d->getAvailableBufferSizes()) if (bsz >= 256) { want = bsz; break; }
                if (want > 0) { logEvent ("buffer: saved " + juce::String (d->getCurrentBufferSizeSamples()) + " smp is below 128 on ASIO4ALL; using " + juce::String (want));
                                cur.bufferSize = want; change = true; } }
        if (change) { const auto err = deviceManager.setAudioDeviceSetup (cur, true); if (err.isNotEmpty()) logEvent ("device reopen failed: " + err); }
        if (auto* d = deviceManager.getCurrentAudioDevice()) logEvent ("opened: " + d->getName() + " buffer " + juce::String (d->getCurrentBufferSizeSamples()) + " smp, input chans " + juce::String (d->getActiveInputChannels().countNumberOfSetBits()) + " of " + juce::String (d->getInputChannelNames().size()) + " offered by the driver");
        if (splitDevice.isNotEmpty()) openSplitInput (splitType, splitDevice);
        if (micDevice.isNotEmpty()) openMicInput (micDevice);
        return;
    }
    splitType = splitDevice = {};
    deviceManager.initialise (1, 2, nullptr, true);
    auto& types = deviceManager.getAvailableDeviceTypes();
    auto findType = [&] (const juce::String& name) -> juce::AudioIODeviceType* { for (auto* t : types) if (t->getTypeName() == name) return t; return nullptr; };
    auto findCable = [] (juce::AudioIODeviceType* t) -> juce::String {
        t->scanForDevices();
        for (auto& n : t->getDeviceNames (true)) if (n.containsIgnoreCase ("Rocksmith")) return n;
        return {};
    };
    auto pickInput = [&] (juce::AudioIODeviceType* t) { return t->getDeviceNames (true)[t->getDefaultDeviceIndex (true)]; };
    auto tryApply = [&] (const juce::String& typeName, juce::AudioDeviceManager::AudioDeviceSetup s, bool withInput) {
        deviceManager.setCurrentAudioDeviceType (typeName, true);
        if (! withInput) s.inputDeviceName = {};
        s.inputChannels.clear(); if (withInput) s.inputChannels.setBit (0); s.useDefaultInputChannels = false;
        s.outputChannels.clear(); s.outputChannels.setRange (0, 2, true); s.useDefaultOutputChannels = false;
        return deviceManager.setAudioDeviceSetup (s, true).isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr;
    };
   #if JUCE_WINDOWS
    juce::ignoreUnused (findCable, tryApply);   // no name-based adoption of the cable: inputs come from the driver (ASIO4ALL panel) / device settings
    const auto lg = [this] (const juce::String& m) { logEvent (m); };
    if (auto r = sj::devpolicy::openAsio4All (deviceManager, lg); r.ok) { logEvent ("startup audio: " + r.how); return; }
    // No ASIO4ALL: the built-in low-latency mode (WASAPI exclusive -> low-latency shared -> plain shared, each only if the one before failed).
    // The guitar (default capture device) then runs as a split input on its own stream, drift-corrected against the output clock.
    juce::Array<int> ladder;
    // sharedWithInput = false: the guitar cable and the speakers are different devices on different clocks, so the guitar is ALWAYS a
    // drift-corrected split input here (a combined shared device, or an output-only fallback with no input at all, left a fresh laptop deaf).
    if (auto r = sj::devpolicy::openBuiltIn (deviceManager, true, false, lg, &ladder); r.ok) {
        logEvent ("startup audio: " + r.how);
        if (r.input.isEmpty()) {
            if (auto* sh = findType (sj::devpolicy::SharedType)) {
                sh->scanForDevices();
                logEvent ("capture devices: " + sh->getDeviceNames (true).joinIntoString (" | "));
                if (auto in = sj::devpolicy::defaultInputName (*sh); in.isNotEmpty()) { logEvent ("guitar input picked: " + in); openSplitInput (sj::devpolicy::SharedType, in); }
                else logEvent ("guitar input: no capture device found");
            }
            if (! ladder.isEmpty()) startBufferTuning (ladder);
        }
        return;
    }
    logEvent ("startup audio: no WASAPI output could be opened; trying the platform default");
   #else
    juce::ignoreUnused (findType);
   #endif
    if (auto* t = deviceManager.getCurrentDeviceTypeObject()) {
        auto s = deviceManager.getAudioDeviceSetup();
        s.inputDeviceName = pickInput (t); s.sampleRate = 48000;
        if (! tryApply (t->getTypeName(), s, true)) deviceManager.initialiseWithDefaultDevices (1, 2);
    }
}

// ------------------------------------------------------------------------------------------------ built-in low latency
bool MainComponent::isBuiltInLowLatency() const
{
    const auto t = deviceManager.getCurrentAudioDeviceType();
    return t == sj::devpolicy::ExclusiveType || t == sj::devpolicy::LowLatType;
}

/** Audio tab "Low latency (built-in, no ASIO needed)". on = exclusive (else low-latency shared, else shared) output + split input;
    off = plain shared WASAPI (the Windows default output first, then the others). Returns a one-line result for the UI. */
juce::String MainComponent::setBuiltInLowLatency (bool on)
{
   #if JUCE_WINDOWS
    const auto lg = [this] (const juce::String& m) { logEvent (m); };
    juce::String in = splitDevice;
    if (in.isEmpty()) {
        const auto cur = deviceManager.getAudioDeviceSetup();
        if (deviceManager.getCurrentAudioDeviceType().startsWith ("Windows Audio")) in = cur.inputChannels.isZero() ? juce::String() : cur.inputDeviceName;
        if (in.isEmpty() && on) if (auto* sh = findType (sj::devpolicy::SharedType)) in = sj::devpolicy::defaultInputName (*sh);
    }
    logEvent (juce::String ("built-in low latency: user switched ") + (on ? "ON" : "OFF") + " (guitar input: " + (in.isEmpty() ? juce::String ("combined device") : in) + ")");
    tuneActive = false;
    juce::Array<int> ladder; sj::devpolicy::Opened r;
    {
        const juce::ScopedValueSetter<bool> svs (reconfiguring, true);
        if (split != nullptr) split->close();
        r = sj::devpolicy::openBuiltIn (deviceManager, on, ! on && splitDevice.isEmpty(), lg, &ladder);
    }
    juce::String msg = r.ok ? r.how : "Could not open any audio output: " + r.how;
    if (r.ok) {
        const bool lowLat = r.type != sj::devpolicy::SharedType;
        if (lowLat || splitDevice.isNotEmpty() || r.input.isEmpty()) {   // output-only device: the guitar is a split input
            if (in.isNotEmpty()) openSplitInput (sj::devpolicy::SharedType, in);
            if (lowLat) startBufferTuning (ladder);
        }
        if (on && ! lowLat) msg = "Low latency modes could not be opened - using " + msg;
    }
    lastOutBlock = 0;
    updateLatency (true); saveSession();
    logEvent ("built-in low latency result: " + msg);
    return msg;
   #else
    juce::ignoreUnused (on); return "Windows only";
   #endif
}

/** Smallest stable buffer: for ~30 s after the built-in low-latency mode opens, any new audio glitch / driver xrun steps the buffer up one
    size (128 -> 160/192 -> ... as the device offers, never above 1024); a clean 30 s settles it. The final size is what session.xml keeps. */
void MainComponent::startBufferTuning (const juce::Array<int>& ladder)
{
    tuneLadder = ladder; tuneActive = ! ladder.isEmpty(); tuneStart = secs; tuneBase = -1;
    if (auto* d = deviceManager.getCurrentAudioDevice())
        logEvent ("buffer tuning: started at " + juce::String (d->getCurrentBufferSizeSamples()) + " smp, larger sizes available: " + juce::String (ladder.size()));
}

void MainComponent::tuneTick()
{
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr || ! isBuiltInLowLatency()) { tuneActive = false; return; }
    const int64_t el = secs - tuneStart;
    const int64_t g = engine.glitches() + d->getXRunCount();
    if (el < 4 || tuneBase < 0) { tuneBase = (int) g; return; }   // ignore the start-up (first seconds after a reopen)
    if (g > tuneBase) {
        const int cur = d->getCurrentBufferSizeSamples();
        int next = 0; for (int b : tuneLadder) if (b > cur) { next = b; break; }
        if (next == 0) { tuneActive = false; logEvent ("buffer tuning: glitch at " + juce::String (cur) + " smp and no larger size offered; staying"); return; }
        logEvent ("buffer tuning: glitch at " + juce::String (cur) + " smp -> stepping up to " + juce::String (next) + " smp");
        auto s = deviceManager.getAudioDeviceSetup(); s.bufferSize = next;
        const auto err = deviceManager.setAudioDeviceSetup (s, true);
        if (err.isNotEmpty()) { tuneActive = false; logEvent ("buffer tuning: reopen failed: " + err); return; }
        tuneStart = secs; tuneBase = -1;
    } else if (el >= 30) {
        tuneActive = false;
        logEvent ("buffer tuning: settled at " + juce::String (d->getCurrentBufferSizeSamples()) + " smp (no glitches for " + juce::String (el - 4) + " s)");
    }
}

/** Setup check window: lists what this PC has / lacks; every download or install is a separate button the player must click (see Setup/FirstRunSetup.h). */
void MainComponent::showSetupDialog()
{
    if (setupWindow != nullptr) { setupWindow->toFront (true); return; }
    auto names = [this] {
        juce::StringArray n;
        for (auto* t : deviceManager.getAvailableDeviceTypes())
            if (t != nullptr && ! t->getTypeName().containsIgnoreCase ("ASIO")) n.addArray (t->getDeviceNames (true));
        return n;
    };
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "FtheBand setup check";
    o.dialogBackgroundColour = juce::Colour (0xff1c1c22);
    o.componentToCentreAround = this;
    o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false; o.useBottomRightCornerResizer = false;
    o.content.setOwned (new sj::setup::SetupPanel (names, [this] { if (setupWindow != nullptr) setupWindow->exitModalState (0); }));
    setupWindow = o.launchAsync();
}

/** Once, shortly after the very first start: if anything optional is missing, show the setup check (it only lists; nothing is installed). */
void MainComponent::maybeShowSetupAtStart()
{
    if (sj::setup::alreadySeen()) return;
    juce::StringArray n;
    for (auto* t : deviceManager.getAvailableDeviceTypes()) if (t != nullptr && ! t->getTypeName().containsIgnoreCase ("ASIO")) n.addArray (t->getDeviceNames (true));
    const auto items = sj::setup::buildItems (sj::setup::detect (n));
    sj::setup::markSeen (true);
    logEvent ("setup check: " + juce::String (sj::setup::needsAttention (items) ? "items missing, showing the window" : "all fine"));
    if (sj::setup::needsAttention (items)) showSetupDialog();
}

// ------------------------------------------------------------------------------------------------ split input
juce::StringArray MainComponent::splitCandidates() const
{
    auto& types = const_cast<juce::AudioDeviceManager&> (deviceManager).getAvailableDeviceTypes();
    juce::AudioIODeviceType* t = nullptr;
    for (auto* x : types) if (x->getTypeName() == "Windows Audio") t = x;
    if (t == nullptr) t = const_cast<juce::AudioDeviceManager&> (deviceManager).getCurrentDeviceTypeObject();
    if (t == nullptr || t->getTypeName() == "ASIO") return {};
    t->scanForDevices();
    return t->getDeviceNames (true);
}

bool MainComponent::openSplitInput (const juce::String& typeNameIn, const juce::String& inName)
{
    auto* out = deviceManager.getCurrentAudioDevice();
    juce::AudioIODeviceType* type = nullptr;
    juce::String typeName = typeNameIn;
   #if JUCE_WINDOWS
    if (typeName.isEmpty() || typeName.startsWith ("Windows Audio (")) typeName = "Windows Audio";   // Low Latency / Exclusive output types: the input stream is always plain WASAPI (its low-latency twin is chosen in SplitInput)
   #else
    if (typeName.isEmpty()) typeName = deviceManager.getCurrentAudioDeviceType();
   #endif
    type = findType (typeName);
    if (out == nullptr || type == nullptr || type->getTypeName() == "ASIO") { splitError = "split input: no output device / unsupported type " + typeName; return false; }
    if (split == nullptr) split = std::make_unique<sj::SplitInput> (engine);
    // the output must not also read the cable: drop its input channels
    auto s = deviceManager.getAudioDeviceSetup();
    if (s.inputDeviceName.isNotEmpty() || ! s.inputChannels.isZero()) {
        const juce::ScopedValueSetter<bool> svs (reconfiguring, true);
        s.inputDeviceName = {}; s.inputChannels.clear(); s.useDefaultInputChannels = false;
        deviceManager.setAudioDeviceSetup (s, true);
        out = deviceManager.getCurrentAudioDevice();
        if (out == nullptr) { splitError = "output device failed to reopen"; return false; }
    }
    split->errored = false;
    splitError = split->open (*type, inName, out->getCurrentSampleRate(), out->getCurrentBufferSizeSamples(),
                              preferLowLatency ? lowLatencyTypeFor (*type) : nullptr);
    lastOutBlock = out->getCurrentBufferSizeSamples(); lastOutRate = out->getCurrentSampleRate();
    splitType = typeName; splitDevice = inName;
    if (splitError.contains ("input runs at") && ! inRateRetry) {
        // cable and output device run at different rates (e.g. headphones at 44.1 kHz, cable at 48 kHz). Move the OUTPUT (FtheBand's own
        // stream, not the Windows setting) to the cable's rate when it supports it, then open the cable again.
        const double inRate = splitError.fromFirstOccurrenceOf ("input runs at ", false, false).getDoubleValue();
        bool can = false;
        for (double r : out->getAvailableSampleRates()) if (std::abs (r - inRate) < 1.0) can = true;
        if (can && inRate > 0) {
            logEvent ("rate mismatch: cable " + juce::String (inRate, 0) + " Hz, output " + juce::String (out->getCurrentSampleRate(), 0) + " Hz -> moving the output to " + juce::String (inRate, 0) + " Hz");
            { const juce::ScopedValueSetter<bool> svs (reconfiguring, true);
              auto s2 = deviceManager.getAudioDeviceSetup(); s2.sampleRate = inRate;
              deviceManager.setAudioDeviceSetup (s2, true); }
            const juce::ScopedValueSetter<bool> rr (inRateRetry, true);
            return openSplitInput (typeNameIn, inName);
        }
    }
    logEvent ("open split input " + inName + " | output " + deviceSummary()
              + (splitError.isEmpty() ? " | OK as " + split->typeName
                                        + (preferLowLatency ? (split->lowLatencyFallback.isEmpty() ? (split->typeName.contains ("Low Latency") ? " (low-latency mode)" : " (low-latency mode not offered)")
                                                                                                  : " (FALLBACK from low-latency: " + split->lowLatencyFallback + ")")
                                                            : " (low-latency mode off in settings)")
                                        + " in-block " + juce::String (split->inputBlock()) + " out-block " + juce::String (out->getCurrentBufferSizeSamples())
                                        + " target " + juce::String (engine.driftIn.latencyMs(), 2) + " ms"
                                      : " | ERROR " + splitError
                                        + (deviceManager.getCurrentAudioDeviceType() == "ASIO" ? " (hint: if the cable is enabled in the ASIO4ALL panel, ASIO4ALL holds it exclusively - untick it there so only the Realtek output is on)" : "")));
    updateLatency (true);
    return splitError.isEmpty();
}

juce::AudioIODeviceType* MainComponent::findType (const juce::String& name) const
{
    for (auto* t : const_cast<juce::AudioDeviceManager&> (deviceManager).getAvailableDeviceTypes()) if (t->getTypeName() == name) return t;
    return nullptr;
}

juce::AudioIODeviceType* MainComponent::lowLatencyTypeFor (const juce::AudioIODeviceType& t) const
{
    return t.getTypeName() == "Windows Audio" ? findType ("Windows Audio (Low Latency Mode)") : nullptr;
}

juce::StringArray MainComponent::micCandidates() const
{
    auto names = splitCandidates();
    if (splitDevice.isNotEmpty()) names.removeString (splitDevice);
    else names.removeString (deviceManager.getAudioDeviceSetup().inputDeviceName);   // combined-device guitar input
    return names;
}

bool MainComponent::openMicInput (const juce::String& inName)
{
    auto* out = deviceManager.getCurrentAudioDevice();
   #if JUCE_WINDOWS
    auto* type = findType ("Windows Audio");
   #else
    auto* type = findType (deviceManager.getCurrentAudioDeviceType());
   #endif
    micDevice = inName;
    if (out == nullptr || type == nullptr || type->getTypeName() == "ASIO") { micError = "no output device / unsupported device type"; return false; }
    if (inName == splitDevice) { micError = "that device is already the guitar input"; return false; }
    if (mic == nullptr) mic = std::make_unique<sj::SplitInput> (engine, sj::SplitInput::Target::Mic);
    mic->errored = false;
    micError = mic->open (*type, inName, out->getCurrentSampleRate(), out->getCurrentBufferSizeSamples(),
                          preferLowLatency ? lowLatencyTypeFor (*type) : nullptr);
    logEvent ("open mic " + inName + (micError.isEmpty() ? " | OK as " + mic->typeName + (mic->lowLatencyFallback.isNotEmpty() ? " (FALLBACK: " + mic->lowLatencyFallback + ")" : "")
                                                          + " in-block " + juce::String (mic->inputBlock()) : " | ERROR " + micError));
    updateLatency (true);
    return micError.isEmpty();
}

void MainComponent::closeMicInput()
{
    if (mic != nullptr) mic->close();
    micDevice = {}; micError = {};
    updateLatency (true);
}

/** Path estimates in samples at the output rate. Guitar path = (split device input latency + its FIFO target) or the
    combined device's input latency. Re-applied when anything moved by > 1 ms (the adaptive FIFO target settles ~10 s
    after opening) so the stem delays do not chase every sample of target change. */
void MainComponent::updateLatency (bool force)
{
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr) return;
    const double sr = d->getCurrentSampleRate();
    const int outLat = d->getOutputLatencyInSamples();
    const bool sp = split != nullptr && splitDevice.isNotEmpty() && engine.isSplitInputActive();
    const int gIn = sp ? split->inputLatency() + (int) std::lround (engine.driftIn.tel.target.load()) : d->getInputLatencyInSamples();
    const bool mo = mic != nullptr && micDevice.isNotEmpty() && engine.isMicInputActive();
    const int mIn = mo ? mic->inputLatency() + (int) std::lround (engine.driftMic.tel.target.load()) : 0;
    engine.latencyTrimSamples = (int) std::lround (trimMs * sr / 1000.0);
    const int tol = (int) (sr / 1000.0);
    if (! force && std::abs (outLat - lastOutLat) <= tol && std::abs (gIn - lastGtrIn) <= tol && std::abs (mIn - lastMicIn) <= tol) return;
    lastOutLat = outLat; lastGtrIn = gIn; lastMicIn = mIn;
    engine.setLatencyPaths (outLat, gIn, mIn);
}

juce::String MainComponent::latencyText (bool shortForm) const
{
    if (usePreview) return shortForm ? "Latency: input FIFO 2.7 + driver in 3.0 + out 4.0 + inserts 0.0 = 9.7 ms (guitar to ears)"
                                     : "Latency (guitar to ears): FIFO 2.7 ms + driver in 3.0 + out 4.0 + inserts 0.0 = 9.7 ms\nRecorded stems: compensated";
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr) return "Latency: no device";
    const double ms = 1000.0 / d->getCurrentSampleRate();
    const bool sp = split != nullptr && splitDevice.isNotEmpty() && engine.isSplitInputActive();
    const double fifo = sp ? engine.driftIn.tel.target.load() * ms : 0.0;
    const double din = (sp ? split->inputLatency() : d->getInputLatencyInSamples()) * ms;
    const double dout = d->getOutputLatencyInSamples() * ms;
    const double ins = engine.plugins.getTotalLatencySamples() * ms;
    const double tot = fifo + din + dout + ins;
    auto f = [] (double v) { return juce::String (v, 1); };
    juce::String t;
    if (shortForm) t << "Latency: input FIFO " << f (fifo) << " + driver in " << f (din) << " + out " << f (dout) << " + inserts " << f (ins) << " = " << f (tot) << " ms (guitar to ears)";
    else {
        t << "Latency (guitar to ears): FIFO " << f (fifo) << " ms + driver in " << f (din) << " + out " << f (dout) << " + inserts " << f (ins) << " = " << f (tot) << " ms";
        t << "\nRecorded stems compensated: PDC " << engine.appliedPdcSamples.load() << " smp, round trip " << engine.appliedRtSamples.load()
          << " smp, mic skew " << engine.appliedMicSkew.load() << " smp (driver figures are what the driver reports)";
    }
    return t;
}

void MainComponent::closeSplitInput()
{
    if (split != nullptr) split->close();
    splitDevice = {}; splitError = {};
    updateLatency (true);
}

void MainComponent::reopenSplitInputForOutput()
{
    if (splitDevice.isEmpty()) return;
    if (auto* out = deviceManager.getCurrentAudioDevice()) {
        if (split != nullptr && split->isOpen() && engine.isSplitInputActive() && ! split->errored.load()
            && lastOutBlock == out->getCurrentBufferSizeSamples() && std::abs (lastOutRate - out->getCurrentSampleRate()) < 1) return;
        lastOutBlock = out->getCurrentBufferSizeSamples(); lastOutRate = out->getCurrentSampleRate();
        openSplitInput (splitType, splitDevice);
    } else if (split != nullptr) split->close();
}

void MainComponent::maybeAutoSplit()
{
    // auto split removed: split input is a manual setting only
}

juce::File MainComponent::driftLogFile()
{ return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ftheband").getChildFile ("drift.log"); }

MainComponent::InputIssue MainComponent::inputIssue() const
{
    InputIssue r;
    auto* d = deviceManager.getCurrentAudioDevice();
    if (usePreview && previewIssue > 0) {   // --screens: show each message
        r.fix = 1; r.action = "Use the cable through ASIO4ALL";
        r.text = "No guitar input is open. The cable (" + juce::String ("Microphone (Rocksmith USB Guitar Adapter)") + ") can't open on its own because ASIO4ALL holds it exclusively (it is enabled in the ASIO4ALL panel). "
                 "Greyed entries in \"Active input channels\" are just unticked: click the box. Or untick the cable in the ASIO4ALL panel so FtheBand can open it separately.";
        return r;
    }
    if (d == nullptr) { r.text = "No audio device is open. Pick one in \"Audio device type\" and \"Output\" on the left."; return r; }
    const auto type = deviceManager.getCurrentAudioDeviceType();
    const bool asio = type == "ASIO";
    const auto chans = d->getInputChannelNames();
    const bool splitWanted = splitDevice.isNotEmpty();
    const bool splitOk = splitWanted && split != nullptr && split->isOpen() && engine.isSplitInputActive() && ! split->errored.load();
    const bool driverIn = ! deviceManager.getAudioDeviceSetup().inputChannels.isZero() && ! d->getActiveInputChannels().isZero();
    if (splitOk) { r.ok = true; r.text = "Guitar input OK: " + splitDevice + " (own clock, drift-corrected). Headphones or speakers can be the output: the cable stays the input."; return r; }
    if (driverIn && ! splitWanted) { r.ok = true; r.text = "Guitar input OK: " + juce::String (d->getActiveInputChannels().countNumberOfSetBits()) + " driver channel(s) ticked on " + type + "."; return r; }
    if (splitWanted) {
        if (asio) {
            r.text = "No guitar input is open. The cable (" + splitDevice + ") can't open on its own because ASIO4ALL holds it exclusively (it is enabled in the ASIO4ALL panel). "
                     "Greyed entries in \"Active input channels\" are just unticked: click the box. Or untick the cable in the ASIO4ALL panel (Driver control panel).";
            if (chans.size() > 0) { r.fix = 1; r.action = "Use the cable through ASIO4ALL"; }
        } else {
            r.text = "No guitar input is open: the cable (" + splitDevice + ") can't open"
                     + (splitError.isNotEmpty() ? ": " + splitError.upToFirstOccurrenceOf (" (low-latency mode", false, false) : juce::String ("."))
                     + " Close other apps that use the cable (exclusive mode can't share a device), then retry.";
            r.fix = 3; r.action = "Retry the cable";
        }
        return r;
    }
    if (asio) {
        if (chans.size() > 0) { r.fix = 1; r.action = "Tick " + chans[0];
            r.text = "No guitar input is ticked. Tick one under \"Active input channels\" (greyed entries are just unticked: click the box), or choose a cable as split input on the right."; }
        else r.text = "ASIO4ALL offers no input. Open the ASIO4ALL panel (Driver control panel) and enable the cable (Real Tone / USB Guitar Adapter) there, or choose the cable as split input on the right.";
        return r;
    }
    r.text = "No guitar input is open. " + type + " drives the output only (exclusive mode can't share the cable), so the guitar is opened separately as a split input.";
    auto names = splitCandidates(); juce::String pick;
    for (auto& n : names) if (n.containsIgnoreCase ("rocksmith") || n.containsIgnoreCase ("guitar")) { pick = n; break; }
    if (pick.isNotEmpty()) { r.fix = 2; r.action = "Use " + pick.substring (0, 24) + (pick.length() > 24 ? "..." : ""); }
    return r;
}
void MainComponent::applyInputFix (int fix)
{
    auto* d = deviceManager.getCurrentAudioDevice(); if (d == nullptr) return;
    if (fix == 3) { openSplitInput (splitType, splitDevice); return; }
    if (fix == 2) {
        for (auto& n : splitCandidates()) if (n.containsIgnoreCase ("rocksmith") || n.containsIgnoreCase ("guitar")) { openSplitInput (splitType, n); return; }
        return;
    }
    if (fix == 1) {   // guitar through the ASIO driver's own input channel
        const auto chans = d->getInputChannelNames(); if (chans.isEmpty()) return;
        int idx = 0;
        for (int i = 0; i < chans.size(); ++i) if (chans[i].containsIgnoreCase ("guitar") || chans[i].containsIgnoreCase ("rocksmith") || chans[i].containsIgnoreCase ("usb")) { idx = i; break; }
        logEvent ("input: one-click fix - guitar through ASIO channel '" + chans[idx] + "'");
        closeSplitInput();
        auto s = deviceManager.getAudioDeviceSetup();
        s.inputDeviceName = s.outputDeviceName; s.useDefaultInputChannels = false; s.inputChannels.clear(); s.inputChannels.setBit (idx);
        deviceManager.setAudioDeviceSetup (s, true);
    }
}

juce::String MainComponent::splitStatusText() const
{
    if (splitDevice.isEmpty()) return "Split input: off (input and output share the output device's clock)";
    juce::String t;
    t << "Split input: " << splitType << " / " << splitDevice;
    if (splitError.isNotEmpty()) return t + "\nERROR: " + splitError;
    auto& tl = engine.driftIn.tel;
    t << "\nFIFO fill " << juce::String (tl.fill.load(), 0) << " / target " << juce::String (tl.target.load(), 0) << " smp ("
      << juce::String (engine.driftIn.latencyMs(), 1) << " ms added)   drift " << (tl.ppm.load() >= 0 ? "+" : "") << juce::String (tl.ppm.load(), 1) << " ppm"
      << "\nxruns: WASAPI " << (split ? split->xruns() : 0) << "  underrun-xfades " << tl.underruns.load() << "  overrun-xfades " << tl.overruns.load()
      << "  input stalls " << tl.stalls.load() << "  dropped blocks " << tl.pushDrops.load()
      << (tl.primed.load() ? "" : "\n(waiting for input samples)")
      << (ppmHighSeconds > 10 ? "\nWARNING: clock offset is at the +-500 ppm correction limit; expect a crossfade every few seconds" : "");
    return t;
}

void MainComponent::logEvent (const juce::String& text)
{
    if (driftLog == nullptr) {
        auto f = driftLogFile(); f.getParentDirectory().createDirectory();
        if (f.getSize() > 4 * 1024 * 1024) { f.getSiblingFile ("drift.log.1").deleteFile(); f.moveFileTo (f.getSiblingFile ("drift.log.1")); }
        driftLog = std::make_unique<juce::FileOutputStream> (f);
        if (! driftLog->openedOk()) { driftLog.reset(); return; }
        driftLog->writeText ("# " + juce::Time::getCurrentTime().toISO8601 (true) + " FtheBand " + juce::String (FTHEBAND_DISPLAY_VERSION)
                             + " started. drift line: ppm fill(lp) fillMin fillMax target underXfades overXfades stalls drops holds wasapiXruns\n", false, false, nullptr);
    }
    driftLog->writeText (juce::Time::getCurrentTime().toISO8601 (true) + " " + text + "\n", false, false, nullptr);
    driftLog->flush();
    if (driftLog->getPosition() > 4 * 1024 * 1024) driftLog.reset();   // rotate on the next line
}

juce::String MainComponent::deviceSummary()
{
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr) return "no output device";
    auto s = deviceManager.getAudioDeviceSetup();
    juce::String t;
    t << deviceManager.getCurrentAudioDeviceType() << " " << d->getName() << " | in: " << (s.inputDeviceName.isEmpty() || s.inputChannels.isZero() ? "-" : s.inputDeviceName)
      << " | out: " << s.outputDeviceName << " | " << juce::String (d->getCurrentSampleRate() / 1000.0, 1) << " kHz / " << d->getCurrentBufferSizeSamples() << " smp";
    return t;
}

bool MainComponent::watchInput (Watch& w, bool isMic)
{
    auto& dev = isMic ? mic : split;
    const auto& name = isMic ? micDevice : splitDevice;
    if (name.isEmpty() || deviceManager.getCurrentAudioDevice() == nullptr) { w = {}; return true; }
    const bool active = dev != nullptr && dev->isOpen() && (isMic ? engine.isMicInputActive() : engine.isSplitInputActive());
    juce::String why;
    if (! active) why = "not open (" + (isMic ? micError : splitError) + ")";
    else if (dev->errored.load()) why = "device reported an error";
    else {
        const int64_t n = (isMic ? engine.driftMic : engine.driftIn).samplesWritten();
        w.stuck = n == w.lastW ? w.stuck + 1 : 0; w.lastW = n;
        if (w.stuck >= 2) why = "no input samples for " + juce::String (w.stuck) + " s";
    }
    if (why.isEmpty()) { if (++w.healthy >= 10) { w.backoff = 0; w.attempts = 0; } return true; }
    w.healthy = 0;
    if (--w.retryIn > 0) return false;
    ++w.attempts;
    logEvent ("watchdog: " + juce::String (isMic ? "mic " : "split input ") + name + " " + why + " -> reopening (attempt " + juce::String (w.attempts) + ")");
    if (isMic) openMicInput (micDevice); else openSplitInput (splitType, splitDevice);
    w.backoff = juce::jlimit (2, 30, w.backoff * 2); w.retryIn = w.backoff; w.stuck = 0; w.lastW = -1;
    return false;
}

void MainComponent::driftTick()
{
    ++secs;
    if (tuneActive) tuneTick();
    if (secs == 1) {   // always leave a trace of what actually opened, even with no split input
        logEvent ("device: " + deviceSummary());
        if (auto* d = deviceManager.getCurrentAudioDevice())
            logEvent ("latency: output " + juce::String (d->getOutputLatencyInSamples()) + " smp (" + juce::String (d->getOutputLatencyInSamples() * 1000.0 / d->getCurrentSampleRate(), 2) + " ms) input "
                      + juce::String (d->getInputLatencyInSamples()) + " smp, buffer " + juce::String (d->getCurrentBufferSizeSamples()) + " smp, type " + deviceManager.getCurrentAudioDeviceType());
        const auto s = deviceManager.getAudioDeviceSetup();
        if (splitDevice.isEmpty() && deviceManager.getCurrentAudioDeviceType() == "ASIO" && ! s.inputChannels.isZero())
            logEvent ("WARNING: guitar comes in through the ASIO driver itself, not a split input. With ASIO4ALL combining a USB cable and the "
                      "Realtek, the two clocks are NOT synchronised and nothing corrects the drift: crackle that builds up after a while. "
                      "If guitar input and output are different physical devices inside ASIO4ALL, enable Split input on the Audio tab (and untick the cable in the ASIO4ALL panel).");
        if (splitDevice.isEmpty()) logEvent ("split input: off");
    }
    if (splitDevice.isNotEmpty()) watchInput (splitWatch, false);   // watchdog only for an enabled split input
    watchInput (micWatch, true);
    if (secs % 30 == 0) {
        const auto p = sj::ProcStats::now();
        if (startMB < 0) startMB = p.privateMB;
        const double cpu = lastCpu < 0 ? 0.0 : (p.cpuSeconds - lastCpu) / 30.0 * 100.0; lastCpu = p.cpuSeconds;
        auto* d = deviceManager.getCurrentAudioDevice();
        logEvent ("mem: working set " + juce::String (p.workingSetMB, 1) + " MB private " + juce::String (p.privateMB, 1) + " MB (since first sample "
                  + juce::String (p.privateMB - startMB, 1) + ") peak " + juce::String (p.peakMB, 1) + " MB | cpu " + juce::String (cpu, 1)
                  + " % of one core | audio cpu " + juce::String (deviceManager.getCpuUsage() * 100.0, 1) + " % | out xruns " + juce::String (d ? d->getXRunCount() : 0)
                  + " | recording " + (engine.recorder.isRecording() ? "on, dropped blocks " + juce::String (engine.recorder.droppedBlocks.load()) : juce::String ("off"))
                  + " | split " + (splitDevice.isEmpty() ? juce::String ("off") : engine.isSplitInputActive() ? juce::String ("active") : juce::String ("NOT ACTIVE")));
    }
    {   static int64_t lastG = 0; const int64_t g = engine.glitches();
        if (g != lastG) { logEvent ("GLITCH: late " + juce::String (engine.cbLate.load()) + " gaps " + juce::String (engine.cbGaps.load())
                                    + " nan " + juce::String (engine.nanEvents.load()) + " (src 0x" + juce::String::toHexString (engine.nanSource.load()) + ") budget "
                                    + juce::String (engine.cbBudgetUs.load()) + " us, audio cpu " + juce::String (deviceManager.getCpuUsage() * 100.0, 1) + " %"); lastG = g; } }
    if (split == nullptr || splitDevice.isEmpty() || ! engine.isSplitInputActive()) { splitStatus = splitStatusText(); ppmHighSeconds = 0; return; }
    auto& tl = engine.driftIn.tel;
    const float ppm = tl.ppm.load();
    ppmHighSeconds = std::abs (ppm) > 450.0f ? ppmHighSeconds + 1 : 0;
    splitStatus = splitStatusText();
    const auto mm = engine.driftIn.takeMinMax();
    juce::String line;
    line << "drift: ppm " << juce::String (ppm, 2) << " fill " << juce::String (tl.fill.load(), 1)
         << " min " << juce::String (mm.first > 1e8f ? 0.0f : mm.first, 0) << " max " << juce::String (mm.second, 0) << " target " << juce::String (tl.target.load(), 0)
         << " under " << tl.underruns.load() << " over " << tl.overruns.load() << " stalls " << tl.stalls.load()
         << " drops " << tl.pushDrops.load() << " holds " << tl.holds.load() << " wasapiXruns " << split->xruns() << " | cb n " << engine.cbCount.load() << " late " << engine.cbLate.load() << " gaps " << engine.cbGaps.load()
         << " maxUs " << engine.takeCbMaxUs() << "/" << engine.cbBudgetUs.load() << " nan " << engine.nanEvents.load() << " clip " << engine.clipSamples.load()
         << (tl.primed.load() ? "" : " UNPRIMED(no input)") << (ppmHighSeconds == 11 ? "  WARN |ppm|>450 for >10 s" : "");
    logEvent (line);
}

/** session.xml:
    <Session instrument tuning cents strangle muteBassChair bassGiveWay style fallbackBpm holdBars startMode countInClick monitor inputGain masterGain uiScale donateUrl skin(default|page)>
      <Chairs><Chair type="Drums" role="0" gain="0.9" mute="0" solo="0"/>...</Chairs> <DEVICESETUP .../> <SplitInput type device auto/> <Latency lowLatencyInput trimMs/> <Mic device gain monitor/> <PluginChain>...</PluginChain> </Session> */
void MainComponent::loadSession()
{
    if (! settingsFile().existsAsFile()) return;
    savedSession = juce::XmlDocument::parse (settingsFile());
    if (savedSession == nullptr || ! savedSession->hasTagName ("Session")) { savedSession = nullptr; return; }
    auto& x = *savedSession;
    skin = x.getStringAttribute ("skin", "default") == "page" ? 1 : 0;
    engine.instrument = (int) inputInstrumentFromId (x.getStringAttribute ("instrument", "bassvi"));
    engine.tuning = (int) tuningFromId (x.getStringAttribute ("tuning", "standard"));
    engine.customCents = (float) juce::jlimit (-300.0, 300.0, x.getDoubleAttribute ("cents", 0));
    engine.strangle = x.getBoolAttribute ("strangle", false);
    engine.muteBassChair = x.getBoolAttribute ("muteBassChair", false);
    engine.bassGiveWay = x.getBoolAttribute ("bassGiveWay", false);
    engine.style = juce::jlimit (0, styleNames.size() - 1, styleNames.indexOf (x.getStringAttribute ("style", "Rock")));
    engine.fallbackBpm = (float) juce::jlimit (50.0, 220.0, x.getDoubleAttribute ("fallbackBpm", 100));
    engine.holdBars = juce::jlimit (1, 16, x.getIntAttribute ("holdBars", 4));
    engine.startMode = x.getStringAttribute ("startMode", "play") == "button" ? 1 : 0; engine.countInClick = x.getBoolAttribute ("countInClick", true);
    engine.monitorGuitar = x.getBoolAttribute ("monitor", true);
    engine.guitarGain = (float) x.getDoubleAttribute ("inputGain", 1.0);
    engine.placeholderDrive = x.getBoolAttribute ("placeholderDrive", false);
    engine.masterGain = (float) x.getDoubleAttribute ("masterGain", 0.8);
    uiScale = (float) x.getDoubleAttribute ("uiScale", 0);
    if (x.hasAttribute ("donateUrl")) donateUrl = x.getStringAttribute ("donateUrl");
    if (auto* l = x.getChildByName ("Latency")) { preferLowLatency = l->getBoolAttribute ("lowLatencyInput", true); trimMs = (float) juce::jlimit (-50.0, 50.0, l->getDoubleAttribute ("trimMs", 0)); }
    if (auto* mc = x.getChildByName ("Mic")) {
        micDevice = mc->getStringAttribute ("device"); engine.micGain = (float) juce::jlimit (0.0, 4.0, mc->getDoubleAttribute ("gain", 1.0));
        engine.micMonitor = mc->getBoolAttribute ("monitor", false);
    }
    if (auto* cs = x.getChildByName ("Chairs")) {
        std::vector<ChairSpec> v; const auto names = chairTypeNames();
        for (auto* c : cs->getChildWithTagNameIterator ("Chair")) {
            const int t = names.indexOf (c->getStringAttribute ("type")); if (t < 0) continue;
            v.push_back ({ (ChairType) t, juce::jlimit (0, 3, c->getIntAttribute ("role")), (float) c->getDoubleAttribute ("gain", 0.8),
                           c->getBoolAttribute ("mute"), c->getBoolAttribute ("solo") });
        }
        if (! v.empty()) engine.setChairs (v);
    }
}

void MainComponent::saveSession()
{
    juce::XmlElement root ("Session");
    root.setAttribute ("instrument", inputInstrumentIds()[engine.instrument.load()]);
    root.setAttribute ("tuning", tuningIds()[engine.tuning.load()]);
    root.setAttribute ("cents", engine.customCents.load());
    root.setAttribute ("strangle", engine.strangle.load()); root.setAttribute ("muteBassChair", engine.muteBassChair.load()); root.setAttribute ("bassGiveWay", engine.bassGiveWay.load());
    root.setAttribute ("style", styleNames[engine.style.load()]); root.setAttribute ("fallbackBpm", engine.fallbackBpm.load()); root.setAttribute ("holdBars", engine.holdBars.load());
    root.setAttribute ("startMode", engine.startMode.load() == 1 ? "button" : "play"); root.setAttribute ("countInClick", engine.countInClick.load());
    root.setAttribute ("monitor", engine.monitorGuitar.load()); root.setAttribute ("inputGain", engine.guitarGain.load()); root.setAttribute ("placeholderDrive", engine.placeholderDrive.load());
    root.setAttribute ("masterGain", engine.masterGain.load()); root.setAttribute ("uiScale", uiScale); root.setAttribute ("donateUrl", donateUrl);
    root.setAttribute ("skin", skin == 1 ? "page" : "default");
    auto* cs = root.createNewChildElement ("Chairs");
    for (auto& c : engine.getChairs()) { auto* e = cs->createNewChildElement ("Chair");
        e->setAttribute ("type", chairTypeNames()[(int) c.type]); e->setAttribute ("role", c.role); e->setAttribute ("gain", c.gain);
        e->setAttribute ("mute", c.mute); e->setAttribute ("solo", c.solo); }
    if (auto dev = deviceManager.createStateXml()) root.addChildElement (dev.release());
    else if (savedSession != nullptr) if (auto* old = savedSession->getChildByName ("DEVICESETUP")) root.addChildElement (new juce::XmlElement (*old));
    { auto* si = root.createNewChildElement ("SplitInput"); si->setAttribute ("type", splitType); si->setAttribute ("device", splitDevice); si->setAttribute ("version", 2); }
    { auto* l = root.createNewChildElement ("Latency"); l->setAttribute ("lowLatencyInput", preferLowLatency); l->setAttribute ("trimMs", trimMs); }
    { auto* mc = root.createNewChildElement ("Mic"); mc->setAttribute ("device", micDevice); mc->setAttribute ("gain", engine.micGain.load()); mc->setAttribute ("monitor", engine.micMonitor.load()); }
    root.addChildElement (engine.plugins.createStateXml().release());
    settingsFile().getParentDirectory().createDirectory();
    root.writeTo (settingsFile());
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (reconfiguring) return;
    if (src != nullptr && splitDevice.isNotEmpty() && deviceManager.getCurrentAudioDeviceType() == "ASIO"
        && ! deviceManager.getAudioDeviceSetup().inputChannels.isZero()) {
        // the user ticked an ASIO input channel (e.g. the Real Tone cable in ASIO4ALL): that is an explicit choice, so the
        // driver's input wins. Before, the split-input logic below stripped the tick again, so the channel looked unselectable.
        logEvent ("input: ASIO input channel ticked -> split input (" + splitDevice + ") switched off, the guitar now comes through the driver");
        closeSplitInput();
    }
    if (src != nullptr) {
        // output restarted (new device/rate/buffer): stop split input, re-prepare the FIFO, restart it
        reopenSplitInputForOutput();
        maybeAutoSplit();
        if (micDevice.isNotEmpty()) openMicInput (micDevice);
        updateLatency (true);
        if (auto* ap = dynamic_cast<AudioPage*> (pages[3]->content.get())) ap->refresh();
        saveSession();
    }
    if (auto ds = deviceSummary(); ds != lastDeviceSummary) { lastDeviceSummary = ds; logEvent ("device: " + ds); }
    juce::String t;
    if (auto* d = deviceManager.getCurrentAudioDevice()) {
        auto s = deviceManager.getAudioDeviceSetup();
        t << deviceManager.getCurrentAudioDeviceType() << " | in: " << s.inputDeviceName << " | out: " << s.outputDeviceName << " | "
          << juce::String (d->getCurrentSampleRate() / 1000.0, 1) << " kHz / " << d->getCurrentBufferSizeSamples() << " smp | latency in "
          << d->getInputLatencyInSamples() << " out " << d->getOutputLatencyInSamples();
        t << " | " << latencyText (true).fromFirstOccurrenceOf ("= ", false, false).upToFirstOccurrenceOf (" (", false, false) << " total";
        if (splitDevice.isNotEmpty()) t << " | split in: " << splitDevice << (splitError.isNotEmpty() ? " (ERROR)" : "");
    } else t = "No audio device open - see the Audio tab";
    statusBar.setText (t, juce::dontSendNotification);
    if (auto* ap = dynamic_cast<AudioPage*> (pages[3]->content.get())) ap->dev.setText (t, juce::dontSendNotification);
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::spaceKey && onStartStop && audioOpen) { onStartStop(); return true; }
    return false;
}
/** Buttons/combos must not take keyboard focus, or a focused button would eat Space (JUCE clicks it). */
static void noFocusOnButtons (juce::Component& c) {
    for (auto* ch : c.getChildren()) {
        if (dynamic_cast<juce::Button*> (ch) != nullptr || dynamic_cast<juce::ComboBox*> (ch) != nullptr || dynamic_cast<juce::TabBarButton*> (ch) != nullptr) ch->setWantsKeyboardFocus (false);
        noFocusOnButtons (*ch);
    }
}
void MainComponent::timerCallback()
{
    engine.collectLibraries();
    if (audioOpen && ++focusTick % 30 == 0) {
        noFocusOnButtons (*this);
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        if ((f == nullptr || ! isParentOf (f)) && isShowing() && getPeer() != nullptr && getPeer()->isFocused()) grabKeyboardFocus();
    }
    const int i = tabs.getCurrentTabIndex();
    if (i >= 0 && i < pages.size()) pages[i]->content->tick();
    if (audioOpen && engine.plugins.pollLatencyChange()) {
        updateLatency (true);
        logEvent ("insert latency now " + juce::String (engine.plugins.getTotalLatencySamples()) + " smp");
    }
    if (audioOpen && ++tickCount % 30 == 0) { driftTick(); updateLatency(); }
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (bg); }
void MainComponent::resized()
{
    auto r = getLocalBounds();
    statusBar.setBounds (r.removeFromBottom (22).reduced (8, 0));
    tabs.setBounds (r);
}

juce::StringArray MainComponent::renderScreens (const juce::File& dir)
{
    dir.createDirectory(); juce::StringArray out;
    usePreview = true;
    preview.chord = { 8, Power, -1, 0.9f }; preview.key = { 5, true, 0.8f }; preview.bpm = 92.0; preview.beatLocked = true; preview.countIn = false; preview.silent = false;
    const float ch[12] = { 0.35f, 0.5f, 0.1f, 0.15f, 0.05f, 0.7f, 0.1f, 0.05f, 1.0f, 0.1f, 0.4f, 0.05f };
    for (int k = 0; k < 12; ++k) preview.chroma[(size_t) k] = ch[k];
    preview.floorDb = -60.5f; preview.gateDb = -50.0f; preview.nNotches = 3; preview.notchHz[0] = 48.2f; preview.notchHz[1] = 96.5f; preview.notchHz[2] = 145.0f; preview.noiseLearned = true;
    statusBar.setText ("Preview render (no audio device)", juce::dontSendNotification);
    struct Size { int w, h; float scale; } sizes[] = { { 1280, 720, 1.0f }, { 1280, 720, 3.0f }, { 1920, 1080, 1.0f }, { 960, 600, 1.0f } };
    for (auto s : sizes) {
        setSize (s.w, s.h);
        for (int i = 0; i < tabs.getNumTabs(); ++i) {
            if (s.scale > 1.0f && i > 0) continue;   // 4K @300 %: the Jam tab is enough to check text crispness
            tabs.setCurrentTabIndex (i); pages[i]->resized(); pages[i]->content->tick(); resized();
            auto img = createComponentSnapshot (getLocalBounds(), true, s.scale);
            auto f = dir.getChildFile (tabs.getTabNames()[i].toLowerCase() + "_" + juce::String (s.w) + "x" + juce::String (s.h)
                                       + (s.scale > 1.0f ? "@" + juce::String ((int) s.scale) + "x" : juce::String()) + ".png");
            f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png;
            if (os.openedOk() && png.writeImageToStream (img, os)) out.add (f.getFullPathName());
            if (i == 3 && s.scale == 1.0f) {   // Audio tab again with the "cable held by ASIO4ALL" message + one-click fix showing
                previewIssue = 1; pages[i]->content->tick(); pages[i]->resized();
                auto img2 = createComponentSnapshot (getLocalBounds(), true, 1.0f);
                auto f2 = dir.getChildFile ("audio-input-issue_" + juce::String (s.w) + "x" + juce::String (s.h) + ".png");
                f2.deleteFile(); juce::FileOutputStream os2 (f2);
                if (os2.openedOk() && png.writeImageToStream (img2, os2)) out.add (f2.getFullPathName());
                previewIssue = 0; pages[i]->content->tick();
            }
        }
    }
    usePreview = false;
    return out;
}
