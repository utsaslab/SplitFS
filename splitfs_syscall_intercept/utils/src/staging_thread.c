/*
 * =====================================================================================
 *
 *       Filename:  staging_thread.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/07/2019 11:19:23 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#include "staging_thread.h"

static pthread_t cleaning_thread;
static pthread_cond_t cleaning_signal;
static pthread_mutex_t thread_mutex;

static bool thread_running;
static bool thread_started;
static bool wait_for_signal;

static void add_staging_files() {
    int num_staging_files = 1;
    create_and_add_staging_files(num_staging_files);
    thread_running = false;
}

static void *thread_wrapper() {
 start:
	pthread_mutex_lock(&thread_mutex);
	wait_for_signal = true;
	while(!thread_running) {
	 	pthread_cond_wait(&cleaning_signal, &thread_mutex);
	}
	wait_for_signal = false;
	pthread_mutex_unlock(&thread_mutex);
    add_staging_files();
	if(!exit_staging_thread)
		goto start;
	thread_started = false;
	return NULL;
}

static void activate_thread() {
	pthread_mutex_lock(&thread_mutex);
    thread_running = true;
	pthread_cond_signal(&cleaning_signal);
	pthread_mutex_unlock(&thread_mutex);
}

static void thread_init() {
	pthread_cond_init(&cleaning_signal, NULL);
	pthread_mutex_init(&thread_mutex, NULL);
}

void splitfs_start_thread() {
	if (!thread_started) {
		thread_started = true;
        thread_init();
		pthread_create(&cleaning_thread, NULL, &thread_wrapper, NULL);
	}
}

void splitfs_thread_wait() {
	if(thread_started) {
		pthread_join(cleaning_thread, NULL);
	}
}

void splitfs_exit_thread() {
	if(thread_started) {
		pthread_cancel(cleaning_thread);
		pthread_testcancel();
	}
}


void splitfs_call_thread() {
	if(thread_running)
		return;

	activate_thread();
}
