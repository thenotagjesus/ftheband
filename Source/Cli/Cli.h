// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <JuceHeader.h>
namespace sj {
/** Offline tools shared by the GUI exe and SessionJamCli. Returns -1 if args contain no CLI command. */
int runCli (const juce::StringArray& args);
}
