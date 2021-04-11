/*
 * =====================================================================================
 *
 *       Filename:  staging.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/04/2019 10:29:30 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <fcntl.h>
#include <syscall.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <string.h>

#include <libsyscall_intercept_hook_point.h>
#include <splitfs-posix.h>

#include "constants.h"
#include "sys_util.h"
#include "file.h"
#include "staging.h"
#include "add_delay.h"
#include "out.h"

#define STAGING_FILE_TEMPLATE "/mnt/pmem_emul/Staging-XXXXXX"

static struct sfile_description *free_spool_slots[OPEN_MAX];
static unsigned free_slot_count;
static pthread_mutex_t free_spool_slot_mutex = PTHREAD_MUTEX_INITIALIZER;
// static int staging_counter;

static void get_staging_filename(char *filename) {
    filename[sprintf(filename, "%s", STAGING_FILE_TEMPLATE)] = '\0';
    mktemp(filename);
    filename[strlen(filename)] = '\0';
}

static void
create_staging_file(struct sfile_description *entry) {

    long fd = 0;
    struct stat sbuf;
    long ret = 0;
    char filename[256];

    if (!entry)
        FATAL("entry is false");

    // staging_counter++;

    get_staging_filename(filename);

    fd = syscall_no_intercept(SYS_open, filename, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        FATAL("creating of staging file failed\n");
    }

    ret = syscall_no_intercept(SYS_fallocate, (int)fd, 0, 0, STAGING_FILE_SIZE);
    if (ret < 0) {
        FATAL("posix_fallocate failed\n");
    }

    ret = syscall_no_intercept(SYS_stat, filename, &sbuf);
    if (ret != 0) {
        FATAL("stat failed\n");
    }

    entry->fd = fd;
    entry->ino = (uint32_t)sbuf.st_ino;
}

static void
map_staging_file(struct sfile_description *entry) {

    if (!entry)
        FATAL("entry is null");

    char *prefault_buf;

    prefault_buf = calloc(1, (2*1024*1024));

    entry->start_addr = mmap(NULL,
            STAGING_FILE_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_POPULATE,
            (int)entry->fd,
            0);

    if (entry->start_addr == NULL) {
        FATAL("mmap failed\n");
    }

    for (unsigned i = 0; i < (STAGING_FILE_SIZE) / (2*1024*1024); i++) {
        memcpy((char*)entry->start_addr + (i*2*1024*1024),
               prefault_buf,
               (2*1024*1024));
        perfmodel_add_delay(0, (2*1024*1024));
    }

    entry->valid_offset = 0;
    entry->start_offset = 0;
    entry->end_offset = 0;

    free(prefault_buf);
}

static struct sfile_description *
fetch_free_spool_slot(void) {

    struct sfile_description *entry;

    util_mutex_lock(&free_spool_slot_mutex);

    if (free_slot_count == 0)
        entry = NULL;
    else
        entry = free_spool_slots[--free_slot_count];

    util_mutex_unlock(&free_spool_slot_mutex);

    return entry;
}


static void
mark_as_free_file_slot(struct sfile_description *entry) {

    util_mutex_lock(&free_spool_slot_mutex);

    free_spool_slots[free_slot_count++] = entry;

    util_mutex_unlock(&free_spool_slot_mutex);
}

static void
setup_free_slots(void) {

    static struct sfile_description store[NUM_STAGING_FILES];
    for (unsigned i = 0; i < NUM_STAGING_FILES; i++) {
        create_staging_file(&store[i]);
        map_staging_file(&store[i]);
        mark_as_free_file_slot(store + i);
    }
}

void
splitfs_add_to_staging_pool(struct sfile_description *staging) {

    if (staging) {
        mark_as_free_file_slot(staging);
    }
}

long
splitfs_staging_file_assign(struct splitfs_file *file) {

    if (!file)
        FATAL("file is null");

    struct sfile_description *entry = fetch_free_spool_slot();
    if (entry == NULL)
        return -1;

    file->vinode->staging = entry;

	return 0;
}

void
align_valid_offset(struct sfile_description *staging, off_t alignment) {

    //ASSERT(staging);
    if (!staging)
        FATAL("staging is null");

    off_t staging_alignment = staging->valid_offset % PAGE_SIZE;
    off_t desired_alignment = alignment % PAGE_SIZE;
    off_t diff_align = 0;

    LOG(0, "desired_alignment = %lu, staging alignment = %lu", desired_alignment, staging_alignment);

    if (staging_alignment < desired_alignment) {
        diff_align = desired_alignment - staging_alignment;
        staging->valid_offset += diff_align;
    } else {
        diff_align = PAGE_SIZE - staging_alignment;
        diff_align += desired_alignment;
        staging->valid_offset += diff_align;
    }

    staging_alignment = staging->valid_offset % PAGE_SIZE;
    //ASSERT(staging_alignment == desired_alignment);
    if (staging_alignment != desired_alignment)
        FATAL("Alignment is not correct");
}

void
get_staging_file(struct splitfs_file *file) {

    if (file->vinode->length != file->vinode->sync_length)
        FATAL("Length mismatch");

    long ret = splitfs_staging_file_assign(file);
    off_t alignment = (off_t)file->vinode->sync_length;

    if (ret != 0) {
        file->vinode->staging = (struct sfile_description *) malloc(sizeof(struct sfile_description));
        create_staging_file(file->vinode->staging);
        map_staging_file(file->vinode->staging);
    }
    align_valid_offset(file->vinode->staging, alignment);
}

void
create_and_add_staging_files(int num_files) {

    struct sfile_description *new_store[num_files];

    for (int i = 0; i < num_files; i++) {
        new_store[i] = malloc(sizeof(struct sfile_description));
        create_staging_file(new_store[i]);
        map_staging_file(new_store[i]);
        mark_as_free_file_slot(new_store[i]);
    }
}

void
splitfs_spool_init(void) {

    setup_free_slots();
}
