/* The host task scheduler's test (scripts/tasks_test.sh; docs/PORT.md "The task scheduler", "On the host"): the
 * game's scheduler C (src/main/system/task.c) and its host glue (port/game/tasks.c) on psxstack's fibers
 * (runtime/fiber.c), with the rest of the game and of the runtime replaced by the stubs below. No disc, no game data.
 *
 * main() is the game's main(): it launches the scheduler with a main task and then idles, one vsync tick per turn,
 * as main()'s `for (;;) { rand(); PLATFORM_WAIT(); }` does on the host. A tick here is what psxstack's is to the game:
 * the root-counter handler that startTaskScheduler registered (handleVsyncPreemption), then the pump's end-of-tick
 * switch (port_fiber_pump_point). The tasks spawn, yield, wait, wake, kill and exit through the game's own functions
 * (spawnTask with 5, 6 and 9 arguments, through include/game.h's host macro), and one busy-waits across a tick so that
 * the handler preempts it; the main task busy-waits across one in the non-vsync mode. Each step appends a word to a
 * trace, which must equal EXPECTED: the order the PS1's scheduler gives, derived by hand from task.c and startup.s
 * (the derivation is the comment above EXPECTED). At every step the running fiber must be CURRENT_TASK's.
 *
 * Exit 0: every check passed. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "game.h"
#include "dcb/main.h"
#include "dcb/task.h"
#include "dcb/heap.h"
#include "dcb/vblank.h"

/* runtime/fiber.c's runtime side (port_runtime.h, which this unit cannot include beside the game's headers) */
extern int port_trace;
void port_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void port_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
void port_fiber_pump_point(void);

/* ---- The game's globals the scheduler uses (the .bss stand-ins, on the host) */
s32 TASK_VSYNC_MODE;
Task TASKS[32];
s16 CURRENT_TASK_PRIORITY;
s16 PREEMPTED_TASK_PRIORITY;
s16 DEFERRED_TASK_PRIORITY;
Task TASK_LIST_END;
struct TCB *KERNEL_TCB;
s32 VSYNC_EVENT;
Task *CURRENT_TASK;
Task *PREEMPTED_TASK;
Task *DEFERRED_TASK;
s32 TASK_GP;

/* ---- The trace (no <string.h>: game.h declares strlen, memset and the like with the PS1's types) */
static char trace[4096];
static int trace_len;
static int failures;

static int same(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void check(int ok, const char *what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void say(const char *fmt, ...) {
    char word[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(word, sizeof(word), fmt, ap);
    va_end(ap);
    if (port_fiber_current() != CURRENT_TASK->fiber) {
        printf("FAIL: at \"%s\" the running fiber is %d, CURRENT_TASK's (slot %d) is %d\n", word,
               port_fiber_index(port_fiber_current()), CURRENT_TASK->id, port_fiber_index(CURRENT_TASK->fiber));
        failures++;
    }
    trace_len += snprintf(trace + trace_len, sizeof(trace) - (size_t)trace_len, "%s%s", trace_len != 0 ? " " : "", word);
    if (trace_len >= (int)sizeof(trace)) {
        port_fatal("the trace is full");
    }
}

/* ---- The stubs: the heap, the kernel's events and root counters, the vblank counters, the overlay resolver, the
 * runtime's log */
static int critical;
static int allocs, frees, resolves, vblanks, ticks;
static unsigned long event_desc;
static s32 (*event_handler)();
static int event_enabled, rcnt_started;

/* LIBAPI as the stack declares it (include/game.h's PC_PORT side) */
s32 EnterCriticalSection(void) {
    critical++;
    return 1;
}

void ExitCriticalSection(void) {
    critical--;
}

void *allocHeapBlock(s32 size, s32 ownerTag) {
    check(ownerTag == -3, "a task stack's heap tag is -3");
    allocs++;
    return malloc((size_t)size);
}

s32p freeHeapBlock(void *ptr) {
    frees++;
    free(ptr);
    return 0;
}

s32 freeHeapBlocksByTag(s32 tag) {
    say("free%d", (int)tag);
    return 0;
}

s32 OpenEvent(u32 desc, s32 spec, s32 mode, s32 (*func)()) {
    check(spec == 2 && mode == 0x1000, "OpenEvent(RCnt3, EvSpINT, EvMdINTR, ...)");
    event_desc = desc;
    event_handler = func;
    return 0x7001;
}

s32 EnableEvent(s32 event) {
    event_enabled = event == 0x7001;
    return 1;
}

s32 SetRCnt(u32 spec, u16 target, s32 mode) {
    check(spec == 0xF2000003 && target == 1 && mode == 0x1000, "SetRCnt(RCnt3, 1, RCntMdINTR)");
    return 1;
}

s32 StartRCnt(u32 spec) {
    rcnt_started = spec == 0xF2000003;
    return 1;
}

void tickVblankCounters(void) {
    vblanks++;
}

void (*port_overlay_resolve(int tier, uintptr_t addr))(void) {
    check(tier == 1, "a task entry resolves in tier 1");
    resolves++;
    return (void (*)(void))addr;
}

int port_trace;

void port_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

void port_fatal(const char *fmt, ...) {
    va_list ap;
    printf("FATAL: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\ntrace so far: %s\n", trace);
    exit(1);
}

/* One vsync tick, as psxstack's: the root counter's handler inside the tick, then the switch it asked for at the
 * tick's end. Whichever fiber calls it is the one that "ticked" (VSync, PLATFORM_WAIT). */
static void tick(void) {
    ticks++;
    if (event_handler != NULL && event_enabled && rcnt_started) {
        event_handler();
    }
    port_fiber_pump_point();
}

/* A busy-wait for the next vsync on the running task (a PLATFORM_WAIT spin). */
static void spin_one_tick(void) {
    int t = ticks;
    while (ticks == t) {
        tick();
    }
}

/* ---- The tasks */
static int cookie, token, done;

static void task_a(s32p a0, s32p a1, s32p a2, s32p a3) {
    s32p r;
    say("A:start:%s:%d,%d,%d", a0 == (s32p)&cookie ? "cookie" : "?", (int)a1, (int)a2, (int)a3);
    spin_one_tick(); /* the handler preempts A to the main task at this tick's end */
    say("A:after-tick");
    yieldTask();
    say("A:deferred-turn");
    r = waitFrames(2);
    say("A:waited:%d", (int)r);
    waitFrames(0x7FFFFFFF); /* killed by D meanwhile */
    say("A:unreachable");
}

static void task_b(s32p a0, s32p a1, s32p a2, s32p a3) {
    say("B:start:%d,%d,%d,%d", (int)a0, (int)a1, (int)a2, (int)a3);
    yieldTask();
    say("B:wake-M");
    resumeTask(0, &token); /* a pointer result, at pointer width */
    /* returns: the task ends (regs[R_RA] is exitTask on the PS1) */
}

static void task_c(s32p a0, s32p a1, s32p a2, s32p a3) {
    s32p r;
    say("C:start:%d,%d,%d,%d", (int)a0, (int)a1, (int)a2, (int)a3);
    r = waitFrames(0x7FFFFFFF); /* until D's resumeTask */
    say("C:woke:%d", (int)r);
}

static void task_d(void) {
    s32 e1, e2, e3;
    say("D:start");
    resumeTask(2); /* one argument: the result is 0 */
    say("D:kill1:%d", (int)endTask(1));
    e1 = endTask(1); /* no longer in use */
    e2 = endTask(0); /* the main task */
    e3 = endTask(4); /* itself */
    say("D:errors:%d,%d,%d", (int)e1, (int)e2, (int)e3);
    exitTask();
    say("D:unreachable");
}

static void task_e(s32p a0) {
    say("E:%x", (unsigned)a0);
}

static void task_f(void) {
    for (;;) {
        say("F");
        yieldTask();
    }
}

static void main_task(s32p a0) {
    s32p r;
    say("M:start:%d", (int)a0);
    spawnTask(0, -1, 5, 0x400, task_a, &cookie);       /* slot 1, priority 5 */
    spawnTask(0, -1, 3, 0x400, task_b, 7, -2, 9, -4);  /* slot 2, priority 3: before A */
    r = waitFrames(3);
    say("M:woke:%s", r == (s32p)&token ? "token" : "?");
    spawnTask(0, -1, 2, 0x400, task_c);                /* B's slot 2 and fiber 3 again */
    spawnTask(4, -1, 7, 0x400, task_d, 0);             /* slot 4, after A */
    say("M:fibers:%d,%d", port_fiber_index(TASKS[2].fiber), port_fiber_index(TASKS[4].fiber));
    yieldTask();
    say("M:round2");
    yieldTask();
    say("M:round3");
    yieldTask();
    say("M:round4");
    spawnTask(0, -1, 1, 0x400, task_e, 0x12345);       /* the killed A's slot 1 and fiber 2 */
    say("M:E:%d:%d", (int)TASKS[1].id, port_fiber_index(TASKS[1].fiber));
    setTaskVsyncMode(0);
    spawnTask(0, -1, 1, 0x400, task_f);                /* slot 2, after E (same priority) */
    spin_one_tick(); /* the main task ticks: no switch; it gets the next turn again (DEFERRED_TASK) */
    say("M:ticked");
    yieldTask();
    say("M:deferred");
    yieldTask();
    say("M:end");
    endTask(2);
    done = 1;
    say("M:done");
    waitFrames(0x7FFFFFFF);
    say("M:unreachable");
}

/* The order from task.c's rules (list order from TASKS[0]; the list's end is main()'s idle loop, "I"):
 * - tick 1 preempts the idle (priority 0xFFFF) to M. M spawns A (5) and B (3): T0 B A END. M waits 3 turns: B, which
 *   yields, then A, which spins into tick 2: A is preempted (PREEMPTED = A, 5) to M. M counts and yields to B (3 is not
 *   PREEMPTED's 5), which wakes M with &token and returns: exitCurrentTask frees tag 2 and selects A as PREEMPTED (and
 *   DEFERRED, its next being A, 5): A goes on after its tick and yields, and END's 0xFFFF above DEFERRED's 5 gives A
 *   its deferred turn; A waits 2, yielding to END: the idle.
 * - Tick 3: M's count is zeroed (woken): it takes &token and yields its last turn to A, whose count ends: idle.
 *   Tick 4: M returns &token, spawns C (2, slot 2, fiber 3) and D (7, slot 4, fiber 4): T0 C A D END; yields to C,
 *   which waits; A returns 0 from its wait and waits again; D wakes C (result 0), kills A (tag 1; then -0x83 for A
 *   again, -5 for TASKS[0], -4 for itself) and exits (tag 4) to END.
 * - Ticks 5 and 6: M yields to C, whose zeroed count ends, then C returns 0 and ends (tag 2).
 * - Tick 7: M spawns E (1, slot 1, the destroyed A's fiber 2), sets the non-vsync mode, spawns F (1, after E) and spins
 *   into tick 8 itself: priority 0, so no switch, and DEFERRED becomes TASKS[0]: M's next yield comes back to M
 *   (E's 1 above DEFERRED's 0); the one after goes to E, which ends (tag 1) into F, which yields to END.
 * - Tick 9: M kills F (tag 2) and waits: the idle sees `done`. */
static const char EXPECTED[] =
    "L I M:start:11 B:start:7,-2,9,-4 A:start:cookie:0,0,0 B:wake-M free2 A:after-tick A:deferred-turn I I "
    "M:woke:token M:fibers:3,4 C:start:0,0,0,0 A:waited:0 D:start free1 D:kill1:0 D:errors:-131,-5,-4 free4 I "
    "M:round2 I M:round3 C:woke:0 free2 I M:round4 M:E:1:2 M:ticked M:deferred E:12345 free1 F I M:end free2 M:done";

int main(void) {
    int i, in_use = 0;

    launchTaskScheduler(1, 0x400, (void (*)())main_task, 11, 0, 0, 0);
    say("L");
    /* the handler is task.c's s32 wrapper of handleVsyncPreemption on the host; the trace's preemptions show it runs */
    check(event_desc == 0xF2000003 && event_handler != NULL, "the root counter 3's event has a handler");
    while (!done && ticks < 64) {
        say("I");
        tick();
    }

    check(same(trace, EXPECTED), "the order of execution");
    if (!same(trace, EXPECTED)) {
        printf("  got:      %s\n  expected: %s\n", trace, EXPECTED);
    }
    check(ticks == 9 && vblanks == 9, "9 ticks, each counted by tickVblankCounters");
    check(critical == 0, "EnterCriticalSection and ExitCriticalSection pair up");
    check(resolves == 7, "every task's entry resolved once, at its first switch");
    check(allocs == 7 && frees == 6, "a stack per task, freed when it ends (the main task's stays)");
    for (i = 0; i < 32; i++) {
        in_use += TASKS[i].status.flags < 0;
    }
    check(in_use == 1 && TASKS[0].status.flags < 0, "only the main task is left");
    check(TASKS[0].next == &TASK_LIST_END && TASK_LIST_END.prev == TASKS, "the list is back to TASKS[0] alone");
    check(CURRENT_TASK == &TASK_LIST_END && port_fiber_current() == port_fiber_main(), "the idle loop runs at the end");
    if (failures != 0) {
        printf("tasks_host_test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("tasks_host_test: OK (%d ticks, %d bytes of trace as expected)\n", ticks, trace_len);
    return 0;
}
