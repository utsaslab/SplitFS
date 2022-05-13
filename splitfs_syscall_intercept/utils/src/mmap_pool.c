/*
 * =====================================================================================
 *
 *       Filename:  mmap_pool.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/05/2019 05:21:23 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>

#include "constants.h"
#include "mmap_pool.h"
#include "sys_util.h"

static void *free_mmap_slots[OPEN_MAX];
static unsigned free_slot_count;
static pthread_mutex_t free_mmap_slot_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *
fetch_free_mmap_slot(void) {

    void *entry;

    util_mutex_lock(&free_mmap_slot_mutex);

    if (free_slot_count == 0)
        entry = NULL;
    else
        entry = free_mmap_slots[--free_slot_count];

    util_mutex_unlock(&free_mmap_slot_mutex);

    return entry;
}

void *
splitfs_mmap_assign() {

    void *entry = fetch_free_mmap_slot();
    return entry;
}

void
splitfs_mmap_add(void *entry) {

    util_mutex_lock(&free_mmap_slot_mutex);

    free_mmap_slots[free_slot_count++] = entry;

    util_mutex_unlock(&free_mmap_slot_mutex);
}
