#ifndef __NV_THREAD_HANDLER_H_
#define __NV_THREAD_HANDLER_H_

#include <pthread.h>
#include <stdatomic.h>
#include "fileops_nvp.h"
#include "lru_cache.h"
#include "timers.h"

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

pthread_spinlock_t      global_lock_closed_files;
pthread_spinlock_t      global_lock;

static void bgCloseFiles(int main_thread) {

	instrumentation_type clf_lock_time, bg_thread_time;
	int closed_filedesc = -1;
	ino_t closed_serialno = 0;
#if SEQ_LIST || RAND_LIST
	struct ClosedFiles *clf = NULL;
	int i=0;
#else //SEQ_LIST || RAND_LIST	
	struct InodeClosedFile *tbl = NULL;
	int hash_index = -1;
#endif //SEQ_LIST || RAND_LIST

	START_TIMING(bg_thread_t, bg_thread_time);

	GLOBAL_LOCK_CLOSE_WR();

#if SEQ_LIST	
	for (i = 0; i < 1024; i++) {		
		if (!num_files_closed)
			break;

		clf = &_nvp_closed_files[i];
		LRU_NODE_LOCK_WR(clf);

		closed_filedesc = remove_from_seq_list(clf, &closed_serialno);
		if (!_nvp_REAL_CLOSE(closed_filedesc, closed_serialno, 1)) {
			__atomic_fetch_sub(&num_files_closed, 1, __ATOMIC_SEQ_CST);
		}

	        LRU_NODE_UNLOCK_WR(clf);
	}	       

#elif RAND_LIST
	while(num_files_closed) {		
		if (dr_mem_closed_files <= (500*1024*1024) && num_files_closed < 500 && !cleanup) {
			ASYNC_CLOSING = 1;
			break;
		}

		i = rand() % TOTAL_CLOSED_INODES;
		clf = &_nvp_closed_files[i];

		if (clf->fd == -1)
			continue;
		
		START_TIMING(clf_lock_t, clf_lock_time);
		LRU_NODE_LOCK_WR(clf);
		END_TIMING(clf_lock_t, clf_lock_time);

		closed_filedesc = remove_from_seq_list(clf, &closed_serialno);

		if (!_nvp_REAL_CLOSE(closed_filedesc, closed_serialno, 1)) {
			__atomic_fetch_sub(&num_files_closed, 1, __ATOMIC_SEQ_CST);
			if (!main_thread)
				num_async_close++;
		}

	        LRU_NODE_UNLOCK_WR(clf);
	}
#else	
	while (num_files_closed) {		
		hash_index = lru_tail_serialno % 1024;
		tbl = &inode_to_closed_file[hash_index];
		NVP_LOCK_HASH_TABLE_WR(tbl);

		if (lru_tail_serialno == 0 || tbl->index == -1) {
			NVP_UNLOCK_HASH_TABLE_WR(tbl);
			break;			
		}

		if (dr_mem_closed_files <= (500*1024*1024) && !cleanup) {
			NVP_UNLOCK_HASH_TABLE_WR(tbl);
			break;
		}
			
		closed_filedesc = remove_from_lru_list_policy(&closed_serialno);
		
		if (!_nvp_REAL_CLOSE(closed_filedesc, closed_serialno, 1)) {
			__atomic_fetch_sub(&num_files_closed, 1, __ATOMIC_SEQ_CST);
		}
		
		NVP_UNLOCK_HASH_TABLE_WR(tbl);
	}

#endif
	
	GLOBAL_UNLOCK_CLOSE_WR();

	END_TIMING(bg_thread_t, bg_thread_time);

	run_background_thread = 0;
}

static void *bgThreadWrapper() {

 start:
	pthread_mutex_lock(&mu);
	
	waiting_for_signal = 1;
	while(!run_background_thread) { 
	 	pthread_cond_wait(&bgsignal, &mu); 
	} 
		
	waiting_for_signal = 0;

	pthread_mutex_unlock(&mu);

	bgCloseFiles(0);

	if(!exit_bgthread)
		goto start;

	started_bgthread = 0;

	return NULL;
}

static void activateBgThread() {

	pthread_mutex_lock(&mu);

	run_background_thread = 1;
	pthread_cond_signal(&bgsignal);
	
	pthread_mutex_unlock(&mu);	

	//bgCloseFiles();
}

void startBgThread() {

	if (!started_bgthread) {
		started_bgthread = 1;
		pthread_create(&bgthread, NULL, &bgThreadWrapper, NULL);
	}
}

void waitForBgThread() {
	if(started_bgthread) {
		pthread_join(bgthread, NULL);
	}
}

void cancelBgThread() {
	if(started_bgthread) {
		pthread_cancel(bgthread);
		pthread_testcancel();
	}
}

void initEnvForBg() {	
	pthread_cond_init(&bgsignal, NULL);
	pthread_mutex_init(&mu, NULL);
}

void checkAndActivateBgThread() {
	if(run_background_thread)
		return;	
	if(dr_mem_closed_files > lim_dr_mem_closed || num_files_closed > lim_num_files || cleanup) {
		calledBgThread++;
		activateBgThread();	
	}
}

#endif
