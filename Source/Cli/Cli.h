// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>
namespace sj {
/** Offline tools shared by the GUI exe and FtheBandCli. Returns -1 if args contain no CLI command. */
int runCli (const juce::StringArray& args);
}
