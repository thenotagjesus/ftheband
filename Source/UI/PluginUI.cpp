// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#include "UI/PluginUI.h"

namespace sj {
juce::File PluginLibrary::dir()
{ return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ftheband"); }

PluginLibrary::PluginLibrary (PluginChain& c) : chain (c)
{
    dir().createDirectory();
    juce::PropertiesFile::Options o;
    o.applicationName = "scanner"; o.filenameSuffix = ".settings"; o.folderName = "Ftheband";
    o.osxLibrarySubFolder = "Application Support";
    props = std::make_unique<juce::PropertiesFile> (dir().getChildFile ("scanner.settings"), o);
    if (auto xml = juce::XmlDocument::parse (dir().getChildFile ("plugins.xml")))
        chain.knownPlugins.recreateFromXml (*xml);
    chain.knownPlugins.addChangeListener (this);
}
PluginLibrary::~PluginLibrary() { chain.knownPlugins.removeChangeListener (this); }

void PluginLibrary::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = chain.knownPlugins.createXml()) xml->writeTo (dir().getChildFile ("plugins.xml"));
}

void PluginLibrary::showScanner()
{
    // "Options..." -> "Scan for new or updated VST3 plug-ins" prompts for folders: starts with the format's
    // default locations (e.g. C:\Program Files\Common Files\VST3, ~/.vst3, /usr/lib/vst3) and remembers
    // user-added folders in scanner.settings.
    auto* list = new juce::PluginListComponent (chain.formatManager, chain.knownPlugins,
                                                dir().getChildFile ("scan-crash.tmp"), props.get(), false);
    list->setSize (640, 480);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list); o.dialogTitle = "Plug-ins (use Options... to scan / add folders)";
    o.dialogBackgroundColour = juce::Colour (0xff22252b); o.useNativeTitleBar = true; o.resizable = true;
    o.launchAsync();
}

//==============================================================================
struct PluginSlotsPanel::EditorWindow : juce::DocumentWindow {
    EditorWindow (const juce::String& title, juce::AudioProcessorEditor* ed, std::function<void()> onClose)
        : DocumentWindow (title, juce::Colours::darkgrey, closeButton), closed (std::move (onClose)) {
        setUsingNativeTitleBar (true); setContentOwned (ed, true);
        setResizable (ed->isResizable(), false); centreWithSize (getWidth(), getHeight()); setVisible (true);
    }
    ~EditorWindow() override { clearContentComponent(); }  // editor must die before the plugin instance
    void closeButtonPressed() override { if (closed) juce::MessageManager::callAsync (closed); }
    std::function<void()> closed;
};

PluginSlotsPanel::PluginSlotsPanel (PluginChain& c) : chain (c)
{
    chain.onSlotWillChange = [this] (int s) { closeEditor (s); };
    for (int i = 0; i < PluginChain::NumSlots; ++i) {
        auto* l = labels.add (new juce::Label ({}, "Insert " + juce::String (i + 1)));
        auto* n = nameButtons.add (new juce::TextButton());
        auto* e = editButtons.add (new juce::TextButton ("Edit"));
        auto* b = bypassButtons.add (new juce::ToggleButton ("Bypass"));
        n->onClick = [this, i] { showSlotMenu (i); };
        e->onClick = [this, i] { openEditor (i); };
        b->onClick = [this, i, b] { chain.setBypassed (i, b->getToggleState()); if (onChainChanged) onChainChanged(); };
        for (juce::Component* x : { (juce::Component*) l, (juce::Component*) n, (juce::Component*) e, (juce::Component*) b }) addAndMakeVisible (x);
    }
    refresh();
}

PluginSlotsPanel::~PluginSlotsPanel()
{
    chain.onSlotWillChange = nullptr;
    for (int i = 0; i < PluginChain::NumSlots; ++i) closeEditor (i);
}

void PluginSlotsPanel::refresh()
{
    for (int i = 0; i < PluginChain::NumSlots; ++i) {
        nameButtons[i]->setButtonText (chain.slotName (i) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
        editButtons[i]->setEnabled (chain.getInstance (i) != nullptr);
        bypassButtons[i]->setToggleState (chain.isBypassed (i), juce::dontSendNotification);
    }
}

void PluginSlotsPanel::openEditor (int s)
{
    auto* p = chain.getInstance (s);
    if (p == nullptr) return;
    if (editors[(size_t) s]) { editors[(size_t) s]->toFront (true); return; }
    auto* ed = p->createEditorIfNeeded();
    if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor (*p);
    editors[(size_t) s] = std::make_unique<EditorWindow> ("Insert " + juce::String (s + 1) + ": " + p->getName(), ed,
                                                          [this, s] { closeEditor (s); });
}

void PluginSlotsPanel::closeEditor (int s) { editors[(size_t) s].reset(); }

void PluginSlotsPanel::showSlotMenu (int s)
{
    auto types = chain.knownPlugins.getTypes();
    juce::PopupMenu m, plugins;
    juce::KnownPluginList::addToMenu (plugins, types, juce::KnownPluginList::sortByManufacturer,
                                      chain.getInstance (s) ? chain.getInstance (s)->getPluginDescription().createIdentifierString() : juce::String());
    enum { idRemove = 1000000, idEdit };
    if (types.isEmpty()) m.addItem (-1, "(no plug-ins scanned - click Plug-ins...)", false);
    else m.addSubMenu ("Load plug-in", plugins);
    m.addItem (idEdit, "Show editor", chain.getInstance (s) != nullptr);
    m.addItem (idRemove, "Remove", chain.getInstance (s) != nullptr);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButtons[s]),
        [this, s, types] (int r) {
            if (r == idRemove) chain.unload (s);
            else if (r == idEdit) { openEditor (s); return; }
            else if (r > 0) {
                const int idx = juce::KnownPluginList::getIndexChosenByMenu (types, r);
                if (idx < 0) return;
                juce::String err;
                if (! chain.loadPlugin (s, types.getReference (idx), err))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load plug-in", err);
                else openEditor (s);
            } else return;
            refresh(); if (onChainChanged) onChainChanged();
        });
}

void PluginSlotsPanel::resized()
{
    auto r = getLocalBounds();
    const int h = juce::jmin (28, r.getHeight() / PluginChain::NumSlots);
    for (int i = 0; i < PluginChain::NumSlots; ++i) {
        auto row = r.removeFromTop (h).reduced (0, 2);
        labels[i]->setBounds (row.removeFromLeft (64));
        bypassButtons[i]->setBounds (row.removeFromRight (80));
        editButtons[i]->setBounds (row.removeFromRight (50)); row.removeFromRight (4);
        nameButtons[i]->setBounds (row);
    }
}
} // namespace sj
