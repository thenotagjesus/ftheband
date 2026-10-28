// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>

namespace sj {

/** One-time migration from the app's former name ("SessionJam") to "FtheBand".
    On first run, if the Ftheband user-data folder has no saved session yet and the old SessionJam folder exists, every
    file in it (session.xml, plugins.xml, scanner.settings, drift.log, ...) is COPIED into the Ftheband folder so the
    saved audio setup, plug-in list and log carry over. Existing FtheBand files are never overwritten and the old
    folder is never modified or deleted. A marker file stops it from running again. Returns the number of files copied. */
inline int migrateLegacyUserData (const juce::File& appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory))
{
    const auto oldDir = appData.getChildFile ("SessionJam");
    const auto newDir = appData.getChildFile ("Ftheband");
    const auto marker = newDir.getChildFile (".migrated-from-SessionJam");
    if (! oldDir.isDirectory() || marker.existsAsFile() || newDir.getChildFile ("session.xml").existsAsFile()) return 0;

    // newDir may already exist because the sound-pack downloader creates Ftheband/samples; that must not block migration.
    if (! newDir.createDirectory().wasOk()) return 0;
    int copied = 0;
    for (const auto& f : oldDir.findChildFiles (juce::File::findFiles, true)) {
        const auto rel = f.getRelativePathFrom (oldDir);
        const auto dest = newDir.getChildFile (rel);
        if (dest.existsAsFile()) continue;
        dest.getParentDirectory().createDirectory();
        if (f.copyFileTo (dest)) ++copied;
    }
    marker.replaceWithText ("Copied " + juce::String (copied) + " file(s) from " + oldDir.getFullPathName() + " on "
                            + juce::Time::getCurrentTime().toISO8601 (true) + "\n");
    return copied;
}

} // namespace sj
