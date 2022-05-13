/*
 * =====================================================================================
 *
 *       Filename:  relink.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/05/2019 07:34:28 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <syscall.h>
#include "splitfs-posix.h"
#include "utils.h"
#include "relink.h"
#include "table_mmaps.h"
#include "out.h"

void
perform_relink(long fd2, off_t offset2,
        struct splitfs_vinode *inode,
        size_t count) {

    if (!inode)
        FATAL("inode is NULL");
    if (!inode->staging)
        FATAL("staging is NULL");

    struct sfile_description *staging = inode->staging;

    long fd1 = staging->fd;
    off_t offset1 = staging->valid_offset;
    const char *start_addr = (const char *)staging->start_addr;

    if (!start_addr)
        FATAL("start_addr is NULL");

    /* perform the relink */
    long ret = (long) syscall_no_intercept(335, fd2,
        fd1, offset2, offset1,
        start_addr, count);

    //long ret = syscall_no_intercept(SYS_pwrite64, fd2, (char *) (start_addr + offset1), count, offset2);

    if (ret < 0)
        FATAL("ret is less than 0");
    if (!inode->tbl_mmap)
        FATAL("tbl_mmap is null");

    LOG(0, "will insert into table mmap");
    /* Add entry to tbl_mmap */
    splitfs_insert_tbl_entry(inode->tbl_mmap, offset2,
        offset1, count, (unsigned long)staging->start_addr + (unsigned long)offset1);

    /* page align the valid_offset of the staging file */
    staging->valid_offset = offset1 + (off_t)count;
    align_valid_offset(staging, PAGE_SIZE);

    /* give away the staging file from file2 to global pool */
    splitfs_add_to_staging_pool(staging);
}
