// CPU time per frame by part; see perf_cpu.h.

#include "perf_cpu.h"

#include "../common/log.h"

#include <windows.h>
#include <intrin.h>

namespace trlvr
{
    namespace
    {
        // Timed with the TSC, not QueryPerformanceCounter: under FEX/Wine on
        // the Frame a QPC costs 120-170 ns (a wow64 syscall) against ~40 ns
        // for an rdtsc (measured 2026-10-28), and PerfDrawCalls/PerfDrawMath
        // used four QPCs per draw. Buckets accumulate raw ticks; the report
        // turns them into ms with a tick rate measured against QPC over the
        // same interval, so no TSC frequency is assumed.
        typedef unsigned long long Ticks;

        bool g_enabled = true;

        Ticks g_total[PerfBucketCount]{};
        Ticks g_started[PerfBucketCount]{};
        int g_depth[PerfBucketCount]{};
        unsigned g_calls[PerfBucketCount]{};

        // Frame phases: update = end of Present to the first draw; render =
        // first draw to the next Present.
        Ticks g_frame_start = 0;
        Ticks g_first_draw = 0;
        bool g_drawn = false;
        Ticks g_update = 0, g_render = 0, g_present = 0;
        Ticks g_present_start = 0;

        // Interval start, for the tick rate (0 = not started).
        LONGLONG g_qpc_start = 0;
        Ticks g_tsc_start = 0;

        LONGLONG qpc_now()
        {
            LARGE_INTEGER t{};
            QueryPerformanceCounter(&t);
            return t.QuadPart;
        }

        // Starts the rate interval on first use (a few calls per frame at
        // most, never per draw).
        void ensure_interval()
        {
            if (!g_qpc_start)
            {
                g_qpc_start = qpc_now();
                g_tsc_start = __rdtsc();
            }
        }

        void reset_accumulators()
        {
            for (int b = 0; b < PerfBucketCount; ++b)
            {
                g_total[b] = 0;
                g_calls[b] = 0;
            }
            g_update = g_render = g_present = 0;
        }
    }

    void perf_cpu_set_enabled(bool enabled)
    {
        if (enabled == g_enabled)
            return;
        g_enabled = enabled;
        // Start a clean interval either way: no mixed on/off averages, and
        // no stale phase starts after a gap.
        reset_accumulators();
        g_qpc_start = 0;
        g_tsc_start = 0;
        g_frame_start = g_present_start = g_first_draw = 0;
        g_drawn = false;
    }

    bool perf_cpu_enabled()
    {
        return g_enabled;
    }

    void perf_cpu_begin(PerfBucket bucket)
    {
        if (!g_enabled)
            return;
        // Only the outermost call of a bucket is timed (recursion, nesting).
        if (g_depth[bucket]++ == 0)
            g_started[bucket] = __rdtsc();
    }

    void perf_cpu_end(PerfBucket bucket)
    {
        // Disabled leaves every depth at 0, so this is the single branch;
        // a scope open when the switch flips off still closes cleanly.
        if (g_depth[bucket] <= 0)
            return;
        if (--g_depth[bucket] == 0)
        {
            if (!g_enabled)
                return;
            g_total[bucket] += __rdtsc() - g_started[bucket];
            ++g_calls[bucket];
        }
    }

    void perf_cpu_draw()
    {
        if (!g_enabled)
            return;
        if (!g_drawn)
        {
            g_drawn = true;
            ensure_interval();
            g_first_draw = __rdtsc();
            if (g_frame_start)
                g_update += g_first_draw - g_frame_start;
        }
    }

    void perf_cpu_present_begin()
    {
        if (!g_enabled)
            return;
        ensure_interval();
        g_present_start = __rdtsc();
        if (g_drawn)
            g_render += g_present_start - g_first_draw;
    }

    void perf_cpu_present_end()
    {
        if (!g_enabled)
            return;
        g_frame_start = __rdtsc();
        if (g_present_start)
            g_present += g_frame_start - g_present_start;
        g_drawn = false;
    }

    void perf_cpu_report(unsigned frames)
    {
        // Disabled: no line at all (nothing was accumulated), and the
        // interval restarts when the switch comes back on.
        if (!g_enabled || !frames)
            return;
        ensure_interval();
        const LONGLONG qpc = qpc_now();
        const Ticks tsc = __rdtsc();
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        const double interval_ms = freq.QuadPart
            ? double(qpc - g_qpc_start) * 1000.0 / double(freq.QuadPart)
            : 0.0;
        const double ms_per_tick = (tsc > g_tsc_start && interval_ms > 0.0)
            ? interval_ms / double(tsc - g_tsc_start) : 0.0;
        g_qpc_start = qpc;
        g_tsc_start = tsc;

        double total[PerfBucketCount];
        for (int b = 0; b < PerfBucketCount; ++b)
            total[b] = double(g_total[b]) * ms_per_tick;
        const double update_ms = double(g_update) * ms_per_tick;
        const double render_ms = double(g_render) * ms_per_tick;
        const double present_ms = double(g_present) * ms_per_tick;
        const double n = frames;
        log("perf cpu: per frame %.1f ms game update before drawing, %.1f "
            "ms rendering (draw calls %.1f, of it mod matrix math %.1f), "
            "%.1f ms at Present (submit "
            "%.1f, input %.1f, rest is WaitGetPoses and the swap)",
            update_ms / n, render_ms / n, total[PerfDrawCalls] / n,
            total[PerfDrawMath] / n,
            present_ms / n, total[PerfSubmit] / n,
            total[PerfInput] / n);
        log("perf cpu: mod hooks incl. the game code they wrap, ms per "
            "frame: camera %.2f (%.0f calls), draw Lara %.2f (%.0f), draw "
            "other instances %.2f (%.0f), "
            "visibility %.2f (%.0f), terrain %.2f (%.0f), line probe %.2f "
            "(%.0f), aim target %.2f (%.0f), movement %.2f (%.0f), UI draw "
            "%.2f (%.0f)",
            total[PerfCamera] / n, g_calls[PerfCamera] / n,
            total[PerfDrawLara] / n, g_calls[PerfDrawLara] / n,
            total[PerfDrawInstance] / n, g_calls[PerfDrawInstance] / n,
            total[PerfVisibility] / n, g_calls[PerfVisibility] / n,
            total[PerfTerrain] / n, g_calls[PerfTerrain] / n,
            total[PerfLineProbe] / n, g_calls[PerfLineProbe] / n,
            total[PerfAimTarget] / n, g_calls[PerfAimTarget] / n,
            total[PerfMovement] / n, g_calls[PerfMovement] / n,
            total[PerfUiDraw] / n, g_calls[PerfUiDraw] / n);
        reset_accumulators();
    }
}
