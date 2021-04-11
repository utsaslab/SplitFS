#ifndef SPLITFS_BG_STAGING_H
#define SPLITFS_BG_STAGING_H

#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <stdbool.h>
#include "staging.h"

extern bool exit_staging_thread;

void splitfs_start_thread(void);
void splitfs_thread_wait(void);
void splitfs_exit_thread(void);
void splitfs_call_thread(void);

#endif
