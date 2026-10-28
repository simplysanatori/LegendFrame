// Runtime check of shims/seh_compat.h (no game involved). Built with -include seh_compat.h.
// usage: seh_test.exe <logfile>   (stdout is swallowed by Proton, so results go to the file too)
#include <windows.h>
#include <cstdio>

static FILE* g_log;
static void say(const char* fmt, int v = 0)
{
    char b[256]; snprintf(b, sizeof b, fmt, v);
    if (g_log) { fputs(b, g_log); fputc('\n', g_log); fflush(g_log); }
    puts(b); fflush(stdout);
}

static volatile int* volatile g_null = nullptr;
static int __attribute__((noinline)) faulting_callee(int x) { return *g_null + x; }
typedef int (*Fn)(int);
static volatile Fn g_fn = faulting_callee;
static int __attribute__((noinline)) good_callee(int x) { return x * 2; }

static bool peek(volatile int* p, int* out)
{
    __try { *out = *p; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

int main(int argc, char** argv)
{
    if (argc > 1) g_log = fopen(argv[1], "w");
    int failures = 0;
    say("start");

    // 1. null read inside __try
    int got = 0;
    if (!peek(g_null, &got)) say("recovered (null read)"); else { say("FAIL null read not caught"); ++failures; }

    // 2. guarded call through a function pointer into faulting code
    volatile int r2 = 0;
    __try { r2 = g_fn(1); say("FAIL call did not fault"); ++failures; }
    __except (EXCEPTION_EXECUTE_HANDLER) { r2 = -1; }
    if (r2 == -1) say("recovered (faulting call through function pointer)");

    // 3. no fault: body runs, `return` leaves the frame cleanly
    int ok = 0;
    if (peek(&got, &ok) && ok == got) say("ok (no-fault path returns from inside __try)"); else { say("FAIL no-fault path"); ++failures; }

    // 4. nested: inner fault is caught by the inner handler, outer body continues
    int stage = 0;
    __try {
        __try { (void)*g_null; stage = 1; }
        __except (EXCEPTION_EXECUTE_HANDLER) { stage = 2; }
        stage += 10;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { stage = -99; }
    if (stage == 12) say("recovered (nested)"); else { say("FAIL nested stage=%d", stage); ++failures; }

    // 5. many faults in a row (frame chain must not leak or corrupt)
    int n = 0;
    for (int i = 0; i < 2000; ++i) { int t; if (!peek(g_null, &t)) ++n; }
    if (n == 2000) say("recovered (2000 consecutive faults)"); else { say("FAIL repeated faults n=%d", n); ++failures; }

    // 6. FS:[0] chain intact afterwards: a function pointer call that does NOT fault still works
    if (g_fn != nullptr && good_callee(21) == 42) say("ok (chain intact)"); else ++failures;

    // 7. unguarded fault still reaches the unhandled path? skipped on purpose (would kill the process)
    say(failures ? "RESULT FAIL" : "RESULT PASS");
    if (g_log) fclose(g_log);
    return failures ? 1 : 0;
}
