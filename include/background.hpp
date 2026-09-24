#pragma once
// Background work on the console: chart loading, cover decoding, preview
// stems, gem renders, downloads.
#ifdef __SWITCH__
#include <atomic>
#include <switch.h>
#endif

namespace fret {
// Background priorities, numerically above the main thread's 0x2C (higher
// numbers run later). Loaders answer what the player is looking at, so they
// preempt bulk work such as the start-up gem renders, which only need to be
// done before the first song. A tie would be worse than it sounds: Horizon
// never switches between equal priorities, so a loader queued behind a gem
// render waited seconds for it to finish, with the main thread waiting on it.
enum class Work { Loader = 0x30, Bulk = 0x38 };
// Call first thing on a worker thread. Horizon does not time-slice threads of
// equal priority on one core, and new threads start on core 0 at the main
// thread's priority, so a loader there only ran while rendering waited on
// vsync: a chart took seconds to appear unless the player pressed A and the
// main thread blocked on it. This moves the thread to a spare core (1 or 2,
// taking turns) at a lower priority, so it soaks up that core's idle time
// while the input poller and audio mixer there still preempt it.
inline void runInBackground(Work work = Work::Loader) {
#ifdef __SWITCH__
    static std::atomic<unsigned> turn{0};
    u64 mask = 0;
    if (R_SUCCEEDED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        int spare[2], count = 0;
        for (int core : {1, 2})
            if (mask & BIT(core))
                spare[count++] = core;
        if (count) {
            const int core = spare[turn++ % unsigned(count)];
            svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, u32(BIT(core)));
        }
    }
    svcSetThreadPriority(CUR_THREAD_HANDLE, s32(work));
#else
    (void)work;
#endif
}
} // namespace fret
