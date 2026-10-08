#ifndef DCB_LOADER_H
#define DCB_LOADER_H

#include "game.h"

extern s32 FILE_LOADER_BUSY;

void mountDriveTask(s32p path, s32 parentTask);
s32p loadFileTagged(s32 *path, s32 parentTask, s32 heapTag);
void loadFileToAddress();
s32p loadFile();

#endif /* DCB_LOADER_H */
