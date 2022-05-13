/*
 * =====================================================================================
 *
 *       Filename:  inode.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/03/2019 11:51:07 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <inttypes.h>

#include "constants.h"
#include "inode.h"
#include "sys_util.h"
#include "utils.h"
#include "hash_map.h"
#include "relink.h"
#include "table_mmaps.h"
#include "out.h"

static struct splitfs_vinode *free_vinode_slots[OPEN_MAX];
static unsigned free_slot_count;
static pthread_mutex_t free_vinode_slot_mutex = PTHREAD_MUTEX_INITIALIZER;


static struct splitfs_vinode *fetch_free_inode_slot(void) {

    struct splitfs_vinode *entry;

    if (free_slot_count == 0)
        entry = NULL;
    else
        entry = free_vinode_slots[--free_slot_count];

    return entry;
}

struct splitfs_vinode *
splitfs_vinode_assign(uint32_t serialno) {

    struct splitfs_vinode *entry = NULL;

    util_mutex_lock(&free_vinode_slot_mutex);

    entry = hash_map_get(file_inode_map, (uint32_t) serialno);

    if (entry == NULL) {
        entry = fetch_free_inode_slot();
        hash_map_put(file_inode_map, serialno, (void*)entry);
    }

    if (entry == NULL)
        goto out;

    os_rwlock_wrlock(&entry->rwlock);

    __sync_fetch_and_add(&entry->ref, 1);

    os_rwlock_unlock(&entry->rwlock);

out:
    util_mutex_unlock(&free_vinode_slot_mutex);

    return entry;
}

struct splitfs_vinode *
splitfs_map_vinode_check(uint32_t serialno) {


    util_mutex_lock(&free_vinode_slot_mutex);

    struct splitfs_vinode *entry = hash_map_get(file_inode_map, serialno);

    util_mutex_unlock(&free_vinode_slot_mutex);

    return entry;
}

void splitfs_vinode_add(struct splitfs_vinode *entry) {

    util_mutex_lock(&free_vinode_slot_mutex);

    free_vinode_slots[free_slot_count++] = entry;

    util_mutex_unlock(&free_vinode_slot_mutex);
}

void splitfs_inode_free_list_init(void) {

    static struct splitfs_vinode store[ARRAY_SIZE(free_vinode_slots) - 1];
    for (unsigned i = 0; i < ARRAY_SIZE(store); ++i) {
        os_rwlock_init(&store[i].rwlock);

        store[i].file_mmaps = (uint64_t *) calloc(1, PER_NODE_MMAPS * sizeof(uint64_t));
        //store[i].dirty_file_mmap_cache = (uint32_t *) calloc(1, DIRTY_MMAP_CACHE_SIZE * sizeof(uint32_t));

        splitfs_vinode_add(store + i);
    }
}

long splitfs_vinode_unref(long fd, struct splitfs_vinode *inode) {

    long ret = 0;

    if (!inode) {
        ret = -1;
        goto end;
    }

    ret = 0;
    uint32_t serialno = 0;
    os_rwlock_wrlock(&inode->rwlock);

	if (__sync_sub_and_fetch(&inode->ref, 1) == 0) {
        LOG(0, "ref = 0");
        if (inode->staging &&
                inode_get_uncommitted_size(inode) > inode_get_sync_size(inode)) {
            LOG(0, "doing relink");
            perform_relink(fd, (off_t)inode_get_sync_size(inode),
            inode,
            inode_get_uncommitted_size(inode) - inode_get_sync_size(inode));
        }

        util_mutex_lock(&mmap_cache_mutex);
        hash_map_put(global_mmap_cache, inode->serialno, inode->file_mmaps);
        util_mutex_unlock(&mmap_cache_mutex);

        util_mutex_lock(&tbl_mmap_mutex);
        hash_map_put(tbl_mmap_cache, inode->serialno, inode->tbl_mmap);
        util_mutex_unlock(&tbl_mmap_mutex);

        inode->file_mmaps = NULL;

        serialno = inode_get_ino(inode);
        inode_set_ino(inode, 0);
        inode_set_uncommitted_size(inode, 0);
        inode_set_sync_size(inode, 0);
        inode_set_large_file_status(inode, false);
        inode->staging = NULL;
    }

    os_rwlock_unlock(&inode->rwlock);

    util_mutex_lock(&free_vinode_slot_mutex);
    os_rwlock_rdlock(&inode->rwlock);

    if (inode_get_ref(inode) == 0) {
        LOG(0, "removing entry from <ino, struct vinode> map");
        hash_map_remove(file_inode_map, serialno, (void *)inode);
        LOG(0, "adding inode to free list");
        free_vinode_slots[free_slot_count++] = inode;
    }

    os_rwlock_unlock(&inode->rwlock);
    util_mutex_unlock(&free_vinode_slot_mutex);

end:
    return ret;
}
