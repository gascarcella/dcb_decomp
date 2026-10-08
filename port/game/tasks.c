/* The game's task switch on the host (docs/PORT.md "The task scheduler", "On the host"): src/main/startup.s's glue,
 * which psxstack does not compile (a .s file), written over psxstack's fibers (hooks.h "Fibers"). The scheduler's
 * logic stays the game's C (src/main/system/task.c: createTask, selectNextTask, handleVsyncPreemption, killTask,
 * wakeTask, exitCurrentTask); this file does what the asm does around it, with the same effect on the task records:
 *
 * - On the PS1 a task's context is its registers in Task.regs[] (the kernel's TCB layout); here it is a fiber, one per
 *   task in use (Task.fiber, created by createTask and startTaskScheduler; TASK_LIST_END's is the main fiber, main()'s
 *   idle loop). A switch is port_fiber_switch to the fiber of the task selectNextTask picked.
 * - The asm runs with the interrupts off between its entry and its `rfe`; on the host nothing can interrupt a task but
 *   a vsync tick, and a tick runs only where the game waits (VSync, PLATFORM_WAIT, the shim's own waits), so there is
 *   nothing to mask.
 * - What a stub left in registers for the resumed code (waitFrames's result in v0) is a return value here.
 *
 * spawnTask and resumeTask are declared without a prototype on the PS1 and called with as many arguments as the call
 * needs; on the host include/game.h turns each call into one with every argument an s32p and the missing ones 0, so
 * these definitions are the prototypes' (the names in parentheses: spawnTask and resumeTask are also macros). */
#include "common.h"
#include "game.h"
#include "dcb/main.h"
#include "dcb/task.h"

/* The flags' high half as yieldTask and waitFrames store it (`sh 0x8000` at offset 2): the slot in use, and only the s
 * registers saved, so the switch back returns into the stub (TASK_CONTEXT_SAVED clear). */
static void task_mark_yielded(Task *task) {
    task->status.flags = (s32)(((u32)task->status.flags & 0xFFFF) | TASK_IN_USE);
}

/* The common body's end (.L80014B3C): `next` (selectNextTask's choice, now CURRENT_TASK) runs. Returns when something
 * switches back to the caller's task; a no-op when next is the caller's task. */
static void task_switch_to(Task *next) {
    port_fiber_switch(next->fiber);
}

/* A task's fiber: the entry with its four arguments, as the first switch into a new task's context does (`jr` to
 * regs[R_EPC] with a0-a3 loaded). The entry may be an overlay function's PS1 address (a tag): the slot's current
 * overlay resolves it now, when the PS1 would jump there. Returning from the entry ends the task: regs[R_RA] is exitTask
 * on the PS1. */
void game_task_fiber_entry(void *arg) {
    Task *task = arg;
    void (*entry)(s32p, s32p, s32p, s32p);

    entry = (void (*)(s32p, s32p, s32p, s32p))port_overlay_resolve(1, (uintptr_t)task->entry);
    entry(task->args[0], task->args[1], task->args[2], task->args[3]);
    exitTask();
}

/* Masks the interrupts (Status IEc and the stacked bits). Nothing to mask on the host. */
void disableInterrupts(void) {
}

/* `jr ra; rfe`: pops the interrupt-enable stack. Nothing on the host. */
void restoreInterrupts(void) {
}

/* Records the caller's gp for the tasks (TASK_GP) and goes on into startTaskScheduler. The host has no gp: the tasks
 * share the program's globals. */
void launchTaskScheduler(s32 mode, s32 stackSize, void (*entry)(), s32 a0, s32 a1, s32 a2, s32 a3) {
    startTaskScheduler(mode, stackSize, (s32p)entry, a0, a1, a2, a3);
}

/* createTask with the interrupts off. The asm pushes one word before the call, so createTask sees the caller's fifth
 * argument (the entry) as its sixth: its fifth, `unused`, is the caller's a3 home slot. */
s32(spawnTask)(s32p taskId, s32p insertPos, s32p priority, s32p stackSize, s32p entry, s32p a0, s32p a1, s32p a2,
               s32p a3) {
    return createTask((s32)taskId, (s32)insertPos, (s32)priority, (s32)stackSize, 0, entry, a0, a1, a2, a3);
}

/* killTask with the interrupts off (killTask destroys the killed task's fiber). */
s32 endTask(s32 taskId) {
    return killTask(taskId);
}

/* wakeTask with the interrupts off: the task's waitFrames returns `result` at its next turn. */
s32(resumeTask)(s32p taskId, s32p result) {
    return wakeTask((s32)taskId, result);
}

/* Ends the running task: exitCurrentTask unlinks it, frees its heap blocks and selects the next task (the asm takes
 * selectNextTask's result from v0, which is now CURRENT_TASK); its fiber ends and the next one runs. */
s32 exitTask(void) {
    Task *task = CURRENT_TASK;

    exitCurrentTask();
    task->fiber = NULL;
    port_fiber_exit(CURRENT_TASK->fiber);
}

/* Gives the next task its turn; returns at this task's next turn. */
void yieldTask(void) {
    Task *task = CURRENT_TASK;

    task_mark_yielded(task);
    task_switch_to(selectNextTask(task));
}

/* Sleeps for `frames` of the task's turns (the scheduler's rounds: one per vsync while the tasks yield in time), or
 * until resumeTask, and returns the result resumeTask gave (0 when none).
 * - A result already waiting (resumeTask came first): one turn, then it is returned.
 * - Else the count is stored in the task (Task.waitFrames, which resumeTask zeroes) and goes down by one at the call
 *   and at each turn (waitFramesResume); once it is no longer positive the result is taken and cleared, and the task
 *   yields one last turn before it returns. So waitFrames(n) returns at the n-th turn after the call (n >= 1).
 * The SR bits the asm sets (interrupts on when the task runs again) have no host counterpart. */
s32p waitFrames(s32 frames) {
    Task *task = CURRENT_TASK;
    s32p result;

    if (task->wakeResult == 0) {
        task->waitFrames = frames;
        task_mark_yielded(task);
        while (--task->waitFrames > 0) {
            task_switch_to(selectNextTask(task));
        }
    }
    result = task->wakeResult;
    task->wakeResult = 0;
    task_switch_to(selectNextTask(task));
    return result;
}
