// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <JuceHeader.h>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
 #pragma comment (lib, "psapi.lib")
#elif JUCE_LINUX
 #include <unistd.h>
 #include <sys/resource.h>
#endif

namespace sj {
/** Process memory + CPU, for the drift.log "mem" line and the --soak test.
    Windows: working set / private bytes (GetProcessMemoryInfo). Linux: RSS from /proc/self/statm, private = RSS - shared. */
struct ProcStats {
    double workingSetMB = 0, privateMB = 0, peakMB = 0, cpuSeconds = 0;
    static ProcStats now()
    {
        ProcStats p;
       #if JUCE_WINDOWS
        PROCESS_MEMORY_COUNTERS_EX m {}; m.cb = sizeof (m);
        if (GetProcessMemoryInfo (GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*) &m, sizeof (m))) {
            p.workingSetMB = m.WorkingSetSize / 1048576.0; p.privateMB = m.PrivateUsage / 1048576.0; p.peakMB = m.PeakWorkingSetSize / 1048576.0; }
        FILETIME c, e, k, u;
        if (GetProcessTimes (GetCurrentProcess(), &c, &e, &k, &u)) {
            auto s = [] (FILETIME f) { return (double) (((uint64_t) f.dwHighDateTime << 32) | f.dwLowDateTime) * 1e-7; };
            p.cpuSeconds = s (k) + s (u); }
       #elif JUCE_LINUX
        long pages = 0, res = 0, shared = 0;
        if (FILE* f = std::fopen ("/proc/self/statm", "r")) { if (std::fscanf (f, "%ld %ld %ld", &pages, &res, &shared) != 3) res = shared = 0; std::fclose (f); }
        const double pg = (double) sysconf (_SC_PAGESIZE) / 1048576.0;
        p.workingSetMB = res * pg; p.privateMB = (res - shared) * pg;
        rusage ru {}; if (getrusage (RUSAGE_SELF, &ru) == 0) { p.peakMB = ru.ru_maxrss / 1024.0;
            p.cpuSeconds = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6; }
       #endif
        return p;
    }
};
} // namespace sj
