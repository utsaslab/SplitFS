#ifndef __NV_CLEAN_THREAD_HANDLER_H_
#define __NV_CLEAN_THREAD_HANDLER_H_

#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include "fileops_nvp.h"
#include "timers.h"

pthread_t bg_cleaning_thread;
pthread_cond_t bg_cleaning_signal;
pthread_mutex_t mu_clean;

int run_background_cleaning_thread;
int started_bg_cleaning_thread;
int exit_bg_cleaning_thread;
int calledBgCleaningThread;
int waiting_for_cleaning_signal;
int clean_overwrite;
int waiting_for_signal;

static void clean_dr_mmap() {
	struct free_dr_pool *temp_dr_good_info = NULL;
	int dr_fd = 0, ret = 0, i = 0, j = 0;
	int num_blocks = clean_overwrite == 1 ? (DR_OVER_SIZE / MMAP_PAGE_SIZE) : (DR_SIZE / MMAP_PAGE_SIZE);
	char prefault_buf[MMAP_PAGE_SIZE];
	struct stat stat_buf;
	char dr_fname[256];

	DEBUG_FILE("%s: Enterred BG thread successfully. Will mmap\n", __func__);

	for (i = 0; i < MMAP_PAGE_SIZE; i++)
		prefault_buf[i] = '0';
	for (i = 0; i < BG_NUM_DR; i++) {
		temp_dr_good_info = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool));
		if (clean_overwrite)
			sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-OVER-XXXXXX");
		else
			sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-XXXXXX");
		dr_fd = _hub_find_fileop("posix")->OPEN(mktemp(dr_fname), O_RDWR | O_CREAT, 0666);
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		if (clean_overwrite)
			ret = posix_fallocate(dr_fd, 0, DR_OVER_SIZE);
		else
			ret = posix_fallocate(dr_fd, 0, DR_SIZE);

		if (ret < 0) {
			MSG("%s: posix_fallocate failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}

		if (clean_overwrite) {
			temp_dr_good_info->start_addr = (unsigned long) FSYNC_MMAP
				(
				 NULL,
				 DR_OVER_SIZE,
				 PROT_READ | PROT_WRITE, //max_perms,
				 MAP_SHARED | MAP_POPULATE,
				 dr_fd, //fd_with_max_perms,
				 0
				 );
		} else {
			temp_dr_good_info->start_addr = (unsigned long) FSYNC_MMAP
				(
				 NULL,
				 DR_SIZE,
				 PROT_READ | PROT_WRITE, //max_perms,
				 MAP_SHARED | MAP_POPULATE,
				 dr_fd, //fd_with_max_perms,
				 0
				 );
		}
		if (temp_dr_good_info->start_addr == 0) {
			MSG("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
			assert(0);
		}

		for (j = 0; j < num_blocks; j++) {
#if NON_TEMPORAL_WRITES
			if(MEMCPY_NON_TEMPORAL((char *)temp_dr_good_info->start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}
#else //NON_TEMPORAL_WRITES
			if(FSYNC_MEMCPY((char *)temp_dr_good_info->start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}
#endif //NON_TEMPORAL_WRITES
#if NVM_DELAY
			perfmodel_add_delay(0, MMAP_PAGE_SIZE);
#endif //NVM_DELAY
		}

		num_mmap++;
		num_drs++;
		fstat(dr_fd, &stat_buf);
		temp_dr_good_info->dr_serialno = stat_buf.st_ino;
		temp_dr_good_info->valid_offset = 0;
		if (clean_overwrite) {
			temp_dr_good_info->dr_offset_start = 0;
			temp_dr_good_info->dr_offset_end = DR_OVER_SIZE;
		} else {
			temp_dr_good_info->dr_offset_start = DR_SIZE;
			temp_dr_good_info->dr_offset_end = temp_dr_good_info->valid_offset;
		}
		temp_dr_good_info->dr_fd = dr_fd;
		DEBUG_FILE("%s: Unmapped and mapped DR file again\n", __func__);
		DEBUG_FILE("%s: REMAPPED USELESS FILE dr_fd = %d, dr addr = %p, dr v.o = %lu, dr off start = %lu, dr off end = %lu\n",
			   __func__, temp_dr_good_info->dr_fd, temp_dr_good_info->start_addr, temp_dr_good_info->valid_offset,
			   temp_dr_good_info->dr_offset_start, temp_dr_good_info->dr_offset_end);

		LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(temp_dr_good_info->qe, temp_dr_good_info);
		if (clean_overwrite) 
			lfds711_queue_umm_enqueue( &qs_over, &(temp_dr_good_info->qe) );
		else 
			lfds711_queue_umm_enqueue( &qs, &(temp_dr_good_info->qe) );
		dr_fname[0] = '\0';
		__atomic_fetch_add(&num_drs_left, 1, __ATOMIC_SEQ_CST);
	}
	
	DEBUG_FILE("%s: Returning successfully\n", __func__);
	DEBUG_FILE("%s: ------------- \n", __func__);

	run_background_cleaning_thread = 0;
	clean_overwrite = 0;
}

static void *bgThreadCleaningWrapper() {
 start:
	pthread_mutex_lock(&mu_clean);
	waiting_for_cleaning_signal = 1;
	while(!run_background_cleaning_thread) {
	 	pthread_cond_wait(&bg_cleaning_signal, &mu_clean);
	}
	waiting_for_cleaning_signal = 0;
	pthread_mutex_unlock(&mu_clean);
        clean_dr_mmap();
	if(!exit_bg_cleaning_thread)
		goto start;
	started_bg_cleaning_thread = 0;
	return NULL;
}

static void activateBgCleaningThread(int is_overwrite) {
	pthread_mutex_lock(&mu_clean);
	run_background_cleaning_thread = 1;
	if (is_overwrite)
		clean_overwrite = 1;
	else
		clean_overwrite = 0;
	pthread_cond_signal(&bg_cleaning_signal);
	pthread_mutex_unlock(&mu_clean);
}

void startBgCleaningThread() {
	if (!started_bg_cleaning_thread) {
		started_bg_cleaning_thread = 1;
		pthread_create(&bg_cleaning_thread, NULL, &bgThreadCleaningWrapper, NULL);
	}
}

void waitForBgCleaningThread() {
	if(started_bg_cleaning_thread) {
		pthread_join(bg_cleaning_thread, NULL);
	}
}

void cancelBgCleaningThread() {
	if(started_bg_cleaning_thread) {
		pthread_cancel(bg_cleaning_thread);
		pthread_testcancel();
	}
}

void initEnvForBgClean() {
	pthread_cond_init(&bg_cleaning_signal, NULL);
	pthread_mutex_init(&mu_clean, NULL);
}

void callBgCleaningThread(int is_overwrite) {
	if(run_background_cleaning_thread)
		return;
	calledBgCleaningThread++;
	activateBgCleaningThread(is_overwrite);
}

#endif
