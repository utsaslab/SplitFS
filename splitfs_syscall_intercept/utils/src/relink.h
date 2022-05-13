/*
 * =====================================================================================
 *
 *       Filename:  relink.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/05/2019 10:07:20 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#ifndef SPLITFS_RELINK_H
#define SPLITFS_RELINK_H

#include "file.h"
#include "sys_util.h"
#include "inode.h"
#include "staging.h"
#include <splitfs-posix.h>

void perform_relink(long fd2, off_t offset2,
        struct splitfs_vinode *inode,
        size_t count);

#endif
