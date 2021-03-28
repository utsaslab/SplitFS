#ifndef __NV_THREAD_HANDLER_H_
#define __NV_THREAD_HANDLER_H_

#include <pthread.h>
#include <stdatomic.h>
#include "file.h"
#include "lru_cache.h"
#include "timers.h"
#include "nvp_lock.h"

pthread_t bgthread;
pthread_cond_t bgsignal;
pthread_mutex_t mu;

uint64_t lim_dr_mem_closed;
uint64_t lim_num_files;
uint64_t lim_dr_mem;

int run_background_thread;
int started_bgthread;
int exit_bgthread;
int calledBgThread;
int waiting_for_signal;
int cleanup;

void activateBgThread();

void *bgThreadWrapper();

void startBgThread();

void waitForBgThread();

void cancelBgThread();

void initEnvForBg();

void checkAndActivateBgThread();

void bgCloseFiles(int main_thread);
#endif
