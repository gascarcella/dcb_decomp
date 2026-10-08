#ifndef DCB_TASK_H
#define DCB_TASK_H

#include "game.h"
#include "dcb/main.h"

void setTaskVsyncMode(s32 vsyncMode);
long handleVsyncPreemption();
s32 startTaskScheduler(s32 mode, s32 stackSize, s32p entry, s32p a0, s32p a1, s32p a2, s32p a3);
s32 createTask(s32 taskId, s32 insertPos, s32 priority, s32 stackSize, s32 unused, s32p entry, s32p a0, s32p a1, s32p a2, s32p a3);
s32 killTask(s32 taskId);
Task *selectNextTask(Task *current);
void exitCurrentTask(void);
int killOtherTasks(void);
s32 wakeTask(s32 taskId, s32p result);
s32 getTaskWaitFrames(s32 taskId);
s32 getCurrentTaskId();
#ifdef PC_PORT
/* port/game/tasks.c: a task's fiber starts here (createTask and startTaskScheduler create it) */
void game_task_fiber_entry(void *task);
#endif

#endif /* DCB_TASK_H */
