// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>
#include "Engine/Sampler.h"
#include "Engine/DevicePolicy.h"
#if JUCE_WINDOWS
 #include <shellapi.h>
#endif

namespace sj::setup {
/** First-run setup check. Nothing here downloads or installs anything by itself:
    - detect()/buildItems() only LOOK (registry, device list, sample folder);
    - the dialog lists every item first; each action is a button the player has to click;
    - FlexASIO (MIT) is the only thing FtheBand downloads, from its official GitHub release, after a consent box, HTTPS only, size-capped and
      verified against a pinned SHA-256 before it is run. The installer is never bundled with FtheBand;
    - ASIO4ALL is never downloaded or installed (its licence does not allow redistribution): the button only opens https://asio4all.org;
    - FtheBand is built with the static C++ runtime (/MT), so no Visual C++ redistributable is needed. */

static constexpr const char* Asio4AllUrl   = "https://asio4all.org";
static constexpr const char* FlexAsioUrl   = "https://github.com/dechamps/FlexASIO/releases/download/flexasio-1.10b/FlexASIO-1.10b.exe";
static constexpr const char* FlexAsioPrefix = "https://github.com/dechamps/FlexASIO/releases/download/";
static constexpr const char* FlexAsioSha256 = "fe496bcc08d6c421c6244c8a60ac7b538560bda138000fd1a54ab8ebce031209";   // official 1.10b asset
static constexpr juce::int64 FlexAsioSize    = 13749698;                                                              // bytes, official asset
static constexpr juce::int64 FlexAsioMaxSize = 32 * 1024 * 1024;                                                      // hard cap whatever the server says

enum class State { Ok, Missing, Info };
struct Item { juce::String id, title, detail; State state = State::Info; };

/** What the machine has. Plain data, so the decision logic (buildItems) can be tested without Windows. */
struct Probe {
    bool windows = false;
    bool asio4all = false, flexasio = false;
    bool samples = false;           // bundled or downloaded band samples found
    bool cableSeen = false;         // a USB guitar cable (Rocksmith / Real Tone ...) is listed as a capture device right now
    int  smartAppControl = 0;       // 0 off / unknown, 1 enforced, 2 evaluation
};

#if JUCE_WINDOWS
inline juce::StringArray registeredAsioDrivers()
{
    juce::StringArray out;
    for (REGSAM view : { (REGSAM) KEY_WOW64_64KEY, (REGSAM) KEY_WOW64_32KEY }) {
        HKEY k = nullptr;
        if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ | view, &k) != ERROR_SUCCESS) continue;
        for (DWORD i = 0;; ++i) {
            wchar_t name[256]; DWORD n = 256;
            if (RegEnumKeyExW (k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            out.addIfNotAlreadyThere (juce::String (name));
        }
        RegCloseKey (k);
    }
    return out;
}
inline int readSmartAppControl()
{
    HKEY k = nullptr; DWORD v = 0, sz = sizeof (v), type = 0; int r = 0;
    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CI\\Policy", 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExW (k, L"VerifiedAndReputablePolicyState", nullptr, &type, (LPBYTE) &v, &sz) == ERROR_SUCCESS && type == REG_DWORD) r = (int) v;
        RegCloseKey (k);
    }
    return r;
}
#endif

/** Looks at this machine. captureNames = the input devices Windows Audio lists right now (for the cable check). */
inline Probe detect (const juce::StringArray& captureNames)
{
    Probe p;
   #if JUCE_WINDOWS
    p.windows = true;
    auto drivers = registeredAsioDrivers();
    for (auto& d : drivers) { if (d.containsIgnoreCase ("ASIO4ALL")) p.asio4all = true; if (d.containsIgnoreCase ("FlexASIO")) p.flexasio = true; }
    p.smartAppControl = readSmartAppControl();
   #endif
    p.samples = SampleLibrary::bundledRoot() != juce::File() || SampleLibrary::userSamplesDir().getChildFile ("samples.map").existsAsFile();
    for (auto& n : captureNames) if (sj::devpolicy::looksLikeGuitarCable (n)) p.cableSeen = true;
    // test hook (no effect unless set): FTHEBAND_SETUP_FAKE="windows=1,asio4all=0,flexasio=0,samples=1,cable=0,sac=1"
    if (auto fake = juce::SystemStats::getEnvironmentVariable ("FTHEBAND_SETUP_FAKE", {}); fake.isNotEmpty())
        for (auto& kv : juce::StringArray::fromTokens (fake, ",", "")) {
            const auto k = kv.upToFirstOccurrenceOf ("=", false, false).trim(); const int v = kv.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            if (k == "windows") p.windows = v != 0; else if (k == "asio4all") p.asio4all = v != 0; else if (k == "flexasio") p.flexasio = v != 0;
            else if (k == "samples") p.samples = v != 0; else if (k == "cable") p.cableSeen = v != 0; else if (k == "sac") p.smartAppControl = v;
        }
    return p;
}

/** The list the dialog shows. Pure function of the probe. Missing = something optional the player may want; Info = just a note. */
inline std::vector<Item> buildItems (const Probe& p)
{
    std::vector<Item> r;
    r.push_back ({ "asio4all", "ASIO4ALL (optional low-latency ASIO driver)",
        p.asio4all ? "Installed. Pick \"ASIO4ALL v2\" on the Audio tab." :
        "Not installed. Not required: the built-in low-latency mode (Audio tab) works without it. If you want it, the button opens https://asio4all.org in your browser. "
        "FtheBand never downloads or installs ASIO4ALL itself (its licence does not allow redistribution). Press Re-check after you installed it.",
        p.asio4all ? State::Ok : State::Missing });
    r.push_back ({ "flexasio", "FlexASIO (optional free ASIO driver, MIT licence)",
        p.flexasio ? "Installed. Pick \"FlexASIO\" on the Audio tab." :
        "Not installed. Optional alternative to ASIO4ALL. The button downloads the official FlexASIO 1.10b installer from github.com/dechamps/FlexASIO (13.7 MB, "
        "checked against a fixed SHA-256 before it runs), then starts it; Windows asks for permission. Nothing is downloaded until you click and confirm.",
        p.flexasio ? State::Ok : State::Missing });
    r.push_back ({ "samples", "Band sounds (drum, bass and keys samples)",
        p.samples ? "Found." : "No sample library found next to FtheBand.exe or in your user folder, so the band plays plain synth voices. "
                                "Get free CC0 / CC-BY packs on the \"Get more sounds\" tab, or unzip the full FtheBand download with its samples folder.",
        p.samples ? State::Ok : State::Missing });
    r.push_back ({ "cable", "Guitar input (USB guitar cable)",
        p.cableSeen ? "A USB guitar cable (Rocksmith Real Tone / USB Guitar Adapter) is plugged in and listed by Windows. No driver is needed for it."
                    : "No USB guitar cable is listed right now. Plug in the Rocksmith Real Tone cable (no driver needed: Windows treats it as a normal USB audio device), "
                      "then press Re-check. In Windows Sound settings, set the cable's Recording format to 48000 Hz if you have trouble.",
        p.cableSeen ? State::Ok : State::Missing });
    if (p.smartAppControl != 0)
        r.push_back ({ "sac", "Windows Smart App Control is on",
            "FtheBand.exe is not code-signed yet. Smart App Control (or SmartScreen) may block it or show a warning. Nothing to install: if Windows blocks it, "
            "right-click the downloaded zip > Properties > Unblock before unzipping, or choose More info > Run anyway.", State::Info });
    return r;
}
inline bool needsAttention (const std::vector<Item>& items) { for (auto& i : items) if (i.state == State::Missing) return true; return false; }

inline juce::File seenFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ftheband").getChildFile ("setup-seen.txt"); }
inline bool alreadySeen()    { return seenFile().existsAsFile(); }
inline void markSeen (bool seen) { if (seen) { seenFile().getParentDirectory().createDirectory(); seenFile().replaceWithText ("1"); } else seenFile().deleteFile(); }

/** Downloads the pinned FlexASIO installer, verifies it, then runs it (Windows shows its own UAC prompt). One background thread. */
class FlexAsioInstaller : private juce::Thread {
public:
    using Done = std::function<void (bool ok, juce::String message)>;
    FlexAsioInstaller() : juce::Thread ("FlexAsioInstall") {}
    ~FlexAsioInstaller() override { alive->store (false); signalThreadShouldExit(); stopThread (30000); }
    bool start (Done d) { if (isThreadRunning()) return false; done = std::move (d); prog = 0; setStatus ("Starting"); startThread(); return true; }
    bool busy() const { return isThreadRunning(); }
    float progress() const { return prog.load(); }
    juce::String status() const { const juce::ScopedLock sl (lock); return stat; }
    void cancel() { signalThreadShouldExit(); }

    /** Download part, separated so it can be tested: fetch `url` (https, allowed prefix only) into memory, capped, then check SHA-256. */
    static bool fetchVerified (const juce::String& url, const juce::String& allowedPrefix, juce::int64 maxBytes, const juce::String& sha256Hex,
                               juce::MemoryBlock& out, juce::String& err, std::atomic<float>* progress = nullptr, const std::function<bool()>& cancelled = {})
    {
        if (! url.startsWithIgnoreCase ("https://") || ! url.startsWithIgnoreCase (allowedPrefix)) { err = "Refusing a download that is not from the official release URL"; return false; }
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (20000).withNumRedirectsToFollow (5);
        auto in = juce::URL (url).createInputStream (opts);
        if (in == nullptr) { err = "Could not reach the download server (no internet?)"; return false; }
        const auto total = in->getTotalLength();
        if (total > maxBytes) { err = "The file is larger than expected: not downloading it"; return false; }
        out.reset();
        char buf[1 << 16];
        for (;;) {
            if (cancelled && cancelled()) { err = "Cancelled"; return false; }
            const int n = in->read (buf, (int) sizeof (buf));
            if (n < 0) { err = "Download failed part-way"; return false; }
            if (n == 0) break;
            out.append (buf, (size_t) n);
            if ((juce::int64) out.getSize() > maxBytes) { err = "The download is larger than expected: stopped"; return false; }
            if (progress != nullptr && total > 0) progress->store ((float) ((double) out.getSize() / (double) total));
        }
        if (out.getSize() == 0) { err = "Empty download"; return false; }
        const auto h = juce::SHA256 (out.getData(), out.getSize()).toHexString().toLowerCase();
        if (h != sha256Hex.toLowerCase()) { err = "Checksum mismatch (expected " + sha256Hex.substring (0, 12) + "..., got " + h.substring (0, 12) + "...). The file was NOT run."; return false; }
        return true;
    }
private:
    void run() override
    {
        juce::String msg; bool ok = false;
        juce::MemoryBlock data;
        setStatus ("Downloading FlexASIO 1.10b");
        if (fetchVerified (FlexAsioUrl, FlexAsioPrefix, FlexAsioMaxSize, FlexAsioSha256, data, msg, &prog, [this] { return threadShouldExit(); })) {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ftheband-setup"); dir.createDirectory();
            auto f = dir.getChildFile ("FlexASIO-1.10b.exe"); f.deleteFile();
            if (! f.replaceWithData (data.getData(), data.getSize())) msg = "Could not save the installer to " + dir.getFullPathName();
            else {
                setStatus ("Verified. Starting the installer (Windows will ask for permission)");
                prog = 1.0f;
                ok = runInstaller (f, msg);
            }
        }
        auto cb = done; auto a = alive;
        juce::MessageManager::callAsync ([cb, ok, msg, a] { if (a->load() && cb) cb (ok, msg); });
    }
    bool runInstaller (const juce::File& f, juce::String& msg)
    {
       #if JUCE_WINDOWS
        SHELLEXECUTEINFOW sei {}; sei.cbSize = sizeof (sei); sei.fMask = SEE_MASK_NOCLOSEPROCESS; sei.lpVerb = L"open";
        const auto path = f.getFullPathName(); sei.lpFile = path.toWideCharPointer(); sei.nShow = SW_SHOWNORMAL;
        if (! ShellExecuteExW (&sei) || sei.hProcess == nullptr) {
            msg = GetLastError() == ERROR_CANCELLED ? "Installation cancelled (permission was not given)." : "Could not start the installer.";
            return false;
        }
        setStatus ("Waiting for the FlexASIO installer to finish");
        WaitForSingleObject (sei.hProcess, INFINITE);
        DWORD code = 1; GetExitCodeProcess (sei.hProcess, &code); CloseHandle (sei.hProcess);
        if (code != 0) { msg = "The FlexASIO installer ended with code " + juce::String ((int) code) + " (cancelled or failed)."; return false; }
        msg = "FlexASIO installed. Restart FtheBand, then pick \"FlexASIO\" on the Audio tab.";
        return true;
       #else
        juce::ignoreUnused (f); msg = "FlexASIO is a Windows driver; not started on this system."; return false;
       #endif
    }
    void setStatus (const juce::String& s) { const juce::ScopedLock sl (lock); stat = s; }
    Done done; std::atomic<float> prog { 0 }; juce::CriticalSection lock; juce::String stat;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

/** The prompt. Lists every item with its state before anything happens; each action is its own button. */
class SetupPanel : public juce::Component, private juce::Timer {
public:
    SetupPanel (std::function<juce::StringArray()> captureNamesFn, std::function<void()> onClose_)
        : captureNames (std::move (captureNamesFn)), onClose (std::move (onClose_))
    {
        header.setText ("FtheBand checks your PC for optional extras. Nothing is downloaded or installed unless you click a button below, and every item can be skipped. "
                        "The band, recording and built-in low-latency audio work without any of them.", juce::dontSendNotification);
        header.setJustificationType (juce::Justification::topLeft); header.setFont (juce::FontOptions (14.5f)); header.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (header);
        recheck.setButtonText ("Re-check"); recheck.onClick = [this] { refresh(); };
        close.setButtonText ("Close"); close.onClick = [this] { if (onClose) onClose(); };
        for (auto* c : std::initializer_list<juce::Component*> { &recheck, &close, &status }) addAndMakeVisible (c);
        status.setJustificationType (juce::Justification::centredLeft); status.setFont (juce::FontOptions (13.5f));
        mit.setText ("FlexASIO is Copyright (c) Etienne Dechamps and contributors, released under the MIT licence (github.com/dechamps/FlexASIO). "
                     "ASIO is a trademark of Steinberg Media Technologies GmbH; FtheBand is not affiliated with Steinberg, ASIO4ALL or FlexASIO.", juce::dontSendNotification);
        mit.setFont (juce::FontOptions (12.0f)); mit.setJustificationType (juce::Justification::topLeft); mit.setColour (juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible (mit);
        setSize (760, 560);
        refresh();
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1c1c22));
        int y = rowsTop;
        for (auto& r : rows) {
            auto b = juce::Rectangle<int> (16, y, getWidth() - 32, rowH - 6);
            g.setColour (juce::Colour (0xff26262e)); g.fillRoundedRectangle (b.toFloat(), 6.0f);
            const auto col = r.item.state == State::Ok ? juce::Colour (0xff62c370) : r.item.state == State::Missing ? juce::Colour (0xffe8b04a) : juce::Colour (0xff7fb2e5);
            g.setColour (col); g.fillRoundedRectangle (juce::Rectangle<float> ((float) b.getX(), (float) b.getY(), 5.0f, (float) b.getHeight()), 2.5f);
            g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
            g.drawText (r.item.title, b.getX() + 16, b.getY() + 6, b.getWidth() - 200, 20, juce::Justification::centredLeft);
            g.setColour (col); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (r.item.state == State::Ok ? "OK" : r.item.state == State::Missing ? "NOT FOUND" : "NOTE", b.getRight() - 120, b.getY() + 6, 110, 20, juce::Justification::centredRight);
            g.setColour (juce::Colours::lightgrey); g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (r.item.detail, b.getX() + 16, b.getY() + 28, b.getWidth() - 230, b.getHeight() - 32, juce::Justification::topLeft, 4);
            y += rowH;
        }
    }
    void resized() override
    {
        auto a = getLocalBounds().reduced (16);
        header.setBounds (a.removeFromTop (48)); rowsTop = a.getY() + 8;
        int y = rowsTop;
        for (auto& r : rows) { if (r.button != nullptr) r.button->setBounds (getWidth() - 16 - 210, y + 34, 196, 28); y += rowH; }
        auto bottom = getLocalBounds().reduced (16);
        auto b1 = bottom.removeFromBottom (30); close.setBounds (b1.removeFromRight (90)); b1.removeFromRight (8); recheck.setBounds (b1.removeFromRight (90));
        bottom.removeFromBottom (6); status.setBounds (bottom.removeFromBottom (24)); mit.setBounds (bottom.removeFromBottom (36));
    }
    void refresh()
    {
        const auto items = buildItems (detect (captureNames ? captureNames() : juce::StringArray()));
        rows.clear();
        for (auto& it : items) {
            Row r; r.item = it;
            if (it.id == "asio4all") { r.button = makeButton ("Get ASIO4ALL (opens asio4all.org)", [] { juce::URL (Asio4AllUrl).launchInDefaultBrowser(); }); }
            else if (it.id == "flexasio" && it.state == State::Missing) { r.button = makeButton ("Download and install FlexASIO...", [this] { askFlex(); }); r.button->setEnabled (! installer.busy()); }
            rows.push_back (std::move (r));
        }
        rowH = 96; setSize (760, 16 + 48 + 8 + (int) rows.size() * rowH + 6 + 36 + 6 + 24 + 6 + 30 + 16);
        resized(); repaint();
    }
    int rowCount() const { return (int) rows.size(); }
private:
    struct Row { Item item; std::unique_ptr<juce::TextButton> button; };
    std::unique_ptr<juce::TextButton> makeButton (const juce::String& text, std::function<void()> fn)
    {
        auto b = std::make_unique<juce::TextButton> (text); b->onClick = std::move (fn); addAndMakeVisible (*b); return b;
    }
    void askFlex()
    {
        juce::MessageBoxOptions o = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Download and install FlexASIO?")
            .withMessage ("FtheBand will download the official FlexASIO 1.10b installer (13.7 MB) from:\n" + juce::String (FlexAsioUrl)
                          + "\n\nIt is checked against SHA-256 " + juce::String (FlexAsioSha256).substring (0, 16) + "... before it is run. Then the FlexASIO installer starts and "
                            "Windows asks for permission. FlexASIO is free software under the MIT licence.\n\nNothing happens if you press Cancel.")
            .withButton ("Download and install").withButton ("Cancel").withAssociatedComponent (this);
        juce::AlertWindow::showAsync (o, [this, sp = juce::Component::SafePointer<SetupPanel> (this)] (int r) { if (sp != nullptr && r == 1) startFlex(); });
    }
    void startFlex()
    {
        for (auto& r : rows) if (r.button != nullptr && r.item.id == "flexasio") r.button->setEnabled (false);
        installer.start ([this, sp = juce::Component::SafePointer<SetupPanel> (this)] (bool ok, juce::String msg) {
            if (sp == nullptr) return;
            lastResult = msg; status.setText (msg, juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, ok ? juce::Colour (0xff62c370) : juce::Colour (0xffe8b04a));
            stopTimer(); refresh(); status.setText (msg, juce::dontSendNotification);
        });
        startTimerHz (4);
    }
    void timerCallback() override { status.setText (installer.status() + " (" + juce::String (juce::roundToInt (installer.progress() * 100.0f)) + " %)", juce::dontSendNotification); }

    std::function<juce::StringArray()> captureNames; std::function<void()> onClose;
    juce::Label header, status, mit; juce::TextButton recheck, close;
    std::vector<Row> rows; int rowH = 96, rowsTop = 70; juce::String lastResult; FlexAsioInstaller installer;
};
} // namespace sj::setup
