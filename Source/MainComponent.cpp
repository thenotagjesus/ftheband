// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#include "MainComponent.h"
#include "Util/ProcStats.h"
using namespace sj;

#ifndef SESSIONJAM_DONATE_URL
 #define SESSIONJAM_DONATE_URL ""
#endif
#ifndef SESSIONJAM_SOURCE_URL
 #define SESSIONJAM_SOURCE_URL ""
#endif

namespace {
const juce::Colour bg (0xff15171c), panel (0xff1f232a), panel2 (0xff2a2e36), accent (0xffe8a33d), textDim (0xffa0a6b0);
juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
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

static void styleLabel (juce::Label& l, float h = 15.0f, juce::Colour c = juce::Colours::white)
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
            else e.startRecording (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SessionJam Takes"));
        };
        takesBtn.onClick = [] { auto f = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SessionJam Takes"); f.createDirectory(); f.startAsProcess(); };
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
        if (bs == 1) { col = juce::Colours::limegreen; return "Band: PLAYING"; }
        if (bs == 2) { col = juce::Colours::orange; return "Band: fading out - play to bring it back"; }
        if (bs == 3) { col = juce::Colours::yellow; return "Band: counting in..."; }
        if (bs == 4) { col = juce::Colours::orange; return "Band: ending"; }
        if (ph == AudioEngine::Phase::Idle) return "Band: stopped";
        if (ph == AudioEngine::Phase::Learning) return "Band: learning noise - don't play";
        if (button) { if (e.bandStartReq.load()) { col = juce::Colours::yellow; return "Band: starting..."; } return "Band: ready - press Start band"; }
        col = juce::Colour (0xff5aa9e6); return "Band: ARMED - waiting for you";
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
        g.setColour (panel); g.fillRoundedRectangle (chordArea.toFloat(), 10);
        auto a = chordArea.reduced (20, 14);
        const auto t = (Tuning) e.tuning.load(); const float c = e.customCents.load();
        g.setColour (textDim); g.setFont (font (15)); g.drawText ("CHORD (sounding)", a.removeFromTop (20), juce::Justification::left);
        g.setColour (s.chord.valid() ? accent : textDim); g.setFont (font (88, true));
        g.drawText (s.chord.valid() ? juce::String (s.chord.name()) : juce::String ("--"), a.removeFromTop (100), juce::Justification::left);
        g.setColour (juce::Colours::white); g.setFont (font (18));
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
        g.setColour (panel); g.fillRoundedRectangle (chromaArea.toFloat(), 10);
        auto ca = chromaArea.reduced (14, 12);
        g.setColour (textDim); g.setFont (font (14)); g.drawText ("Pitch classes (sounding)", ca.removeFromTop (18), juce::Justification::left);
        auto lab = ca.removeFromBottom (18); const float w = (float) ca.getWidth() / 12.0f;
        for (int i = 0; i < 12; ++i) {
            auto bar = juce::Rectangle<float> ((float) ca.getX() + (float) i * w + 3, (float) ca.getY() + 4, w - 6, (float) ca.getHeight() - 4);
            g.setColour (panel2); g.fillRect (bar);
            g.setColour (i == s.chord.root ? accent : juce::Colour (0xff5aa9e6));
            g.fillRect (bar.removeFromBottom (bar.getHeight() * juce::jlimit (0.0f, 1.0f, s.chroma[(size_t) i])));
            g.setColour (juce::Colours::lightgrey); g.drawText (noteName (i), juce::Rectangle<float> ((float) lab.getX() + (float) i * w, (float) lab.getY(), w, 18), juce::Justification::centred);
        }
        // ---- input meter with gate marker, mic meter under it
        g.setColour (panel); g.fillRoundedRectangle (meterArea.toFloat(), 8);
        auto all = meterArea.reduced (10, 6);
        auto meter = [&] (juce::Rectangle<int> ma, const juce::String& name, float db, bool on, float gate) {
            auto lbl = ma.removeFromLeft (150);
            auto x = [&] (float d) { return (float) ma.getX() + (float) ma.getWidth() * juce::jlimit (0.0f, 1.0f, (d + 72.0f) / 72.0f); };
            g.setColour (panel2); g.fillRect (ma);
            if (on) {
                g.setColour (db > -3 ? juce::Colours::red : db > -12 ? juce::Colours::orange : juce::Colours::limegreen);
                g.fillRect (juce::Rectangle<float> ((float) ma.getX(), (float) ma.getY(), x (db) - (float) ma.getX(), (float) ma.getHeight()));
                if (gate > -100) { g.setColour (juce::Colours::white); g.fillRect (x (gate) - 1, (float) ma.getY() - 2, 2.0f, (float) ma.getHeight() + 4); }
            }
            g.setColour (on ? juce::Colours::white : textDim); g.setFont (font (14));
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
        void paint (juce::Graphics& g) override { g.setColour (panel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 10); }
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
    PluginSlotsPanel slots; juce::TextButton scan { "Plug-ins..." }; juce::Label help, help2, latency;
    std::function<void()> onChanged;
    InsertsPage (AudioEngine& en, PluginLibrary& l) : e (en), lib (l), slots (en.plugins) {
        for (auto* c : std::initializer_list<juce::Component*> { &slots, &scan, &help, &help2, &latency }) addAndMakeVisible (c);
        styleLabel (help, 14, textDim); styleLabel (help2, 14, textDim); styleLabel (latency, 15);
        help2.setJustificationType (juce::Justification::topLeft);
        help2.setText ("Inserts run inside the audio callback on the same samples (no extra buffer of delay); only a plug-in's own reported\n"
                       "latency adds to what you hear. Recorded stems are delay-compensated: DI, band and mic are shifted to line up with GuitarFX.",
                       juce::dontSendNotification);
        help.setText ("Four VST3 inserts on the input (after the placeholder drive, before the GuitarFX stem). The DI stem is always recorded dry.",
                      juce::dontSendNotification);
        scan.onClick = [this] { lib.showScanner(); };
        slots.refresh();
    }
    void resized() override {
        auto r = getLocalBounds().reduced (16);
        auto row = r.removeFromTop (32); scan.setBounds (row.removeFromLeft (140)); row.removeFromLeft (16); latency.setBounds (row);
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
    juce::ToggleButton lowLat { "Low-latency input mode (WASAPI IAudioClient3, smallest period)" };
    juce::Label bufL { {}, "Output buffer" }, micL { {}, "Microphone (own clock, recorded as the Mic stem)" }, trimL { {}, "Recording trim (ms)" }, bufHint, micStat;
    juce::ComboBox bufBox, micBox;
    juce::Slider trim { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    explicit AudioPage (MainComponent& mc) : m (mc), sel (mc.deviceManager, 1, 1, 2, 2, false, false, false, false) {
        for (auto* c : std::initializer_list<juce::Component*> { &sel, &dev, &splitL, &splitStat, &splitBox, &autoSplit, &rescan, &panelBtn,
                                                                  &lowLat, &bufL, &micL, &trimL, &bufHint, &micStat, &bufBox, &micBox, &trim }) addAndMakeVisible (c);
        styleLabel (dev, 14, textDim); styleLabel (splitL, 15); styleLabel (splitStat, 14, textDim);
        styleLabel (bufL, 14); styleLabel (micL, 15); styleLabel (trimL, 14); styleLabel (bufHint, 13, textDim); styleLabel (micStat, 14, textDim);
        micStat.setJustificationType (juce::Justification::topLeft);
        lowLat.onClick = [this] { m.preferLowLatency = lowLat.getToggleState(); if (m.splitDevice.isNotEmpty()) m.openSplitInput (m.splitType, m.splitDevice);
                                  if (m.micDevice.isNotEmpty()) m.openMicInput (m.micDevice); m.saveSession(); };
        bufBox.onChange = [this] {
            const int bs = bufBox.getSelectedId(); if (bs <= 0) return;
            auto s = m.deviceManager.getAudioDeviceSetup(); if (s.bufferSize == bs) return;
            s.bufferSize = bs; m.deviceManager.setAudioDeviceSetup (s, true);   // change listener reopens the split inputs
        };
        bufHint.setText ("256 recommended (ASIO4ALL: never below 128); raise it if the Jam tab shows audio glitches", juce::dontSendNotification);
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
    void choose() {
        const int id = splitBox.getSelectedId(); if (id <= 0) return;
        if (id == 1) m.closeSplitInput();
        else m.openSplitInput (m.splitType, splitBox.getText());
        m.saveSession(); refresh();
    }
    void tick() override {
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
        sel.setBounds (r.removeFromLeft (640)); r.removeFromLeft (20);
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

// ================================================================================================ About
struct AboutPage : Content {
    juce::Label title, body; juce::HyperlinkButton licence, source, donate;
    explicit AboutPage (const juce::String& donateUrl) {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &body, &licence }) addAndMakeVisible (c);
        styleLabel (title, 28); title.setFont (font (28, true));
        title.setText (juce::String ("Session Jam ") + JUCE_APPLICATION_VERSION_STRING, juce::dontSendNotification);
        styleLabel (body, 15); body.setMinimumHorizontalScale (1.0f); body.setJustificationType (juce::Justification::topLeft);
        body.setText ("A band that follows you: play, and drums, bass and keys follow your chords, tempo and intensity.\n\n"
                      "Copyright (C) 2026 Session Jam contributors.\n"
                      "This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public\n"
                      "License, version 3. It comes with ABSOLUTELY NO WARRANTY. If you run a modified version for others over a network,\n"
                      "you must offer them its source. Built with JUCE (AGPLv3).\n\n"
                      "Wurlitzer EP200 samples by Greg Sullivan (sullivang.net), SFZ mapping by kinwie, licensed under CC BY 3.0\n"
                      "(creativecommons.org/licenses/by/3.0). Trimmed and resampled for Session Jam.\n"
                      "Drum, cymbal and bass samples: Karoryfer Samples (Gogodze Phu Vol. II, Black And Blue Basses) and\n"
                      "Versilian Studios (VCSL), CC0. Full list: CREDITS.md.",
                      juce::dontSendNotification);
        licence.setButtonText ("GNU AGPL v3 licence text"); licence.setURL (juce::URL ("https://www.gnu.org/licenses/agpl-3.0.html"));
        const juce::String src (SESSIONJAM_SOURCE_URL);
        if (src.isNotEmpty()) { source.setButtonText ("Source code: " + src); source.setURL (juce::URL (src)); addAndMakeVisible (source); }
        if (donateUrl.isNotEmpty()) { donate.setButtonText ("Donate"); donate.setURL (juce::URL (donateUrl)); addAndMakeVisible (donate); }  // hidden unless configured
        for (auto* h : { &licence, &source, &donate }) h->setJustificationType (juce::Justification::left);
    }
    void resized() override {
        auto r = getLocalBounds().reduced (24);
        title.setBounds (r.removeFromTop (40)); r.removeFromTop (8); body.setBounds (r.removeFromTop (250));
        licence.setBounds (r.removeFromTop (26).removeFromLeft (400)); source.setBounds (r.removeFromTop (26).removeFromLeft (700));
        r.removeFromTop (10); donate.setBounds (r.removeFromTop (30).removeFromLeft (120));
    }
    void paint (juce::Graphics& g) override { g.fillAll (bg); }
};

// ================================================================================================ MainComponent
juce::File MainComponent::settingsFile()
{ return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("SessionJam").getChildFile ("session.xml"); }
juce::File MainComponent::recordingsFolder()
{ return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SessionJam Takes"); }

float MainComponent::autoScaleFor (juce::Rectangle<int> area)
{ return juce::jlimit (0.5f, 1.0f, juce::jmin ((float) area.getWidth() / (float) LogicalW, (float) area.getHeight() / (float) LogicalH)); }

MainComponent::MainComponent (bool openAudio) : audioOpen (openAudio)
{
    donateUrl = SESSIONJAM_DONATE_URL;
    if (audioOpen) loadSession();
    tabs.setTabBarDepth (34); tabs.setOutline (0);
    auto add = [this] (const juce::String& name, Content* c, int mw, int mh) {
        auto* p = pages.add (new Page (std::unique_ptr<Content> (c), mw, mh)); tabs.addTab (name, panel, p, false); };
    auto* jam = new JamPage (*this, engine); onStartStop = [jam] { jam->startStop(); };
    add ("Jam", jam, 1200, 600);
    setWantsKeyboardFocus (true);
    add ("Band", new BandPage (*this, engine), 1200, 560);
    auto* ins = new InsertsPage (engine, pluginLibrary); add ("Inserts", ins, 800, 260);
    add ("Audio", new AudioPage (*this), 1150, 620);
    add ("About", new AboutPage (donateUrl), 900, 400);
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
    }
    setSize (LogicalW, LogicalH);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (audioOpen) { saveSession(); deviceManager.removeChangeListener (this); split.reset(); mic.reset(); engine.setSplitInputActive (false); engine.setMicInputActive (false);
                     deviceManager.removeAudioCallback (&engine); }
}

/** Startup device policy:
    1) restore saved DEVICESETUP (+ <SplitInput type device/>) if present;
    2) Windows, ASIO4ALL present and a USB guitar interface visible to WASAPI: ASIO4ALL OUTPUT-ONLY (enable only the Realtek in
       its panel) @ 48 kHz / 128 (64 if available) + split input: the interface on its own WASAPI stream, drift-corrected;
    3) Windows, ASIO4ALL without a separate cable: ASIO4ALL in+out (one clock domain only if ASIO4ALL is set up that way);
    4) WASAPI shared 48 kHz: default output; USB guitar interface (if found) as a split input, since it is a different device;
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
    auto* wasapi = findType ("Windows Audio");
    juce::ignoreUnused (findCable);   // no name-based adoption of the cable: inputs come from the driver (ASIO4ALL panel) / device settings
    if (auto* asio = findType ("ASIO")) {
        asio->scanForDevices();
        for (auto& n : asio->getDeviceNames()) if (n.containsIgnoreCase ("ASIO4ALL")) {
            juce::AudioDeviceManager::AudioDeviceSetup s; s.inputDeviceName = s.outputDeviceName = n; s.sampleRate = 48000; s.bufferSize = 128;
            if (tryApply ("ASIO", s, true)) {
                if (auto* d = deviceManager.getCurrentAudioDevice())
                    {   // safe default: 256 (5.3 ms) if the driver allows, else the smallest size >= 256, else the largest it offers
                        int want = 0; for (int b : d->getAvailableBufferSizes()) if (b >= 256) { want = b; break; }
                        if (want == 0 && ! d->getAvailableBufferSizes().isEmpty()) want = d->getAvailableBufferSizes().getLast();
                        if (want > 0 && want != d->getCurrentBufferSizeSamples()) { auto cur = deviceManager.getAudioDeviceSetup(); cur.bufferSize = want; deviceManager.setAudioDeviceSetup (cur, true); } }
                return;
            }
        }
    }
    if (wasapi != nullptr) {
        juce::AudioDeviceManager::AudioDeviceSetup s;
        s.inputDeviceName = wasapi->getDeviceNames (true)[wasapi->getDefaultDeviceIndex (true)];
        s.outputDeviceName = wasapi->getDeviceNames (false)[wasapi->getDefaultDeviceIndex (false)];
        s.sampleRate = 48000; s.bufferSize = 480;
        if (tryApply ("Windows Audio", s, true)) return;
    }
   #else
    juce::ignoreUnused (findType);
   #endif
    if (auto* t = deviceManager.getCurrentDeviceTypeObject()) {
        auto s = deviceManager.getAudioDeviceSetup();
        s.inputDeviceName = pickInput (t); s.sampleRate = 48000;
        if (! tryApply (t->getTypeName(), s, true)) deviceManager.initialiseWithDefaultDevices (1, 2);
    }
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
    if (typeName.isEmpty() || typeName.startsWith ("Windows Audio (Low")) typeName = "Windows Audio";   // low-latency twin is chosen in SplitInput
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
{ return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("SessionJam").getChildFile ("drift.log"); }

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
        driftLog->writeText ("# " + juce::Time::getCurrentTime().toISO8601 (true) + " Session Jam " + juce::String (ProjectInfo::versionString)
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
    if (secs == 1) {   // always leave a trace of what actually opened, even with no split input
        logEvent ("device: " + deviceSummary());
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
    <Session instrument tuning cents strangle muteBassChair bassGiveWay style fallbackBpm holdBars startMode countInClick monitor inputGain masterGain uiScale donateUrl>
      <Chairs><Chair type="Drums" role="0" gain="0.9" mute="0" solo="0"/>...</Chairs> <DEVICESETUP .../> <SplitInput type device auto/> <Latency lowLatencyInput trimMs/> <Mic device gain monitor/> <PluginChain>...</PluginChain> </Session> */
void MainComponent::loadSession()
{
    if (! settingsFile().existsAsFile()) return;
    savedSession = juce::XmlDocument::parse (settingsFile());
    if (savedSession == nullptr || ! savedSession->hasTagName ("Session")) { savedSession = nullptr; return; }
    auto& x = *savedSession;
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
    root.setAttribute ("monitor", engine.monitorGuitar.load()); root.setAttribute ("inputGain", engine.guitarGain.load());
    root.setAttribute ("masterGain", engine.masterGain.load()); root.setAttribute ("uiScale", uiScale); root.setAttribute ("donateUrl", donateUrl);
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
        }
    }
    usePreview = false;
    return out;
}
