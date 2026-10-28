// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>
#include "Engine/PluginChain.h"

namespace sj {
/** Owns persistent plugin-scanner state in AppData/Ftheband: plugins.xml (KnownPluginList),
    scanner.settings (user-added search folders, via PluginListComponent), and a crash "dead man's pedal". */
class PluginLibrary : private juce::ChangeListener {
public:
    explicit PluginLibrary (PluginChain&);
    ~PluginLibrary() override;
    void showScanner();
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    static juce::File dir();
    PluginChain& chain;
    std::unique_ptr<juce::PropertiesFile> props;
};

/** 4 insert-slot rows: [Insert n] [plugin name ▾] [Edit] [Bypass]. Plugin editors open in their own windows. */
class PluginSlotsPanel : public juce::Component {
public:
    explicit PluginSlotsPanel (PluginChain&);
    ~PluginSlotsPanel() override;
    void resized() override;
    void refresh();
    void openEditor (int slot);
    void closeEditor (int slot);
    std::function<void()> onChainChanged;   // e.g. save session
private:
    void showSlotMenu (int slot);
    struct EditorWindow;
    PluginChain& chain;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::TextButton> nameButtons, editButtons;
    juce::OwnedArray<juce::ToggleButton> bypassButtons;
    std::array<std::unique_ptr<EditorWindow>, PluginChain::NumSlots> editors;
};
} // namespace sj
