// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#include "Cli/Cli.h"
int main (int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;   // MessageManager for ChildProcess etc.
    juce::StringArray a; for (int i = 1; i < argc; ++i) a.add (argv[i]);
    const int r = sj::runCli (a);
    if (r < 0) { std::printf ("FtheBandCli --eval [csv] | --analyze <wav> | --render-demo <style> <bpm> <key> <sec> <out.wav> [chairs] | --jam <in.wav> <style> <out.wav> [bpm] [chairs]\n"); return 2; }
    return r;
}
