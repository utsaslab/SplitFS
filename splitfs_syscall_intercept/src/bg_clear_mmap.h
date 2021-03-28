#ifndef __NV_CLEAN_THREAD_HANDLER_H_
#define __NV_CLEAN_THREAD_HANDLER_H_

#include <stdatomic.h>
#include <nv_common.h>

#include "file.h"
#include "handle_mmaps.h"
#include "timers.h"
#include "fsync.h"

extern int run_background_cleaning_thread;
extern int started_bg_cleaning_thread;
extern int exit_bg_cleaning_thread;
extern int calledBgCleaningThread;
extern int waiting_for_cleaning_signal;

void startBgCleaningThread();
void waitForBgCleaningThread();
void cancelBgCleaningThread();
void initEnvForBgClean();
void callBgCleaningThread(int is_overwrite);

#endif
