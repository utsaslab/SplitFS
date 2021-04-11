/*
 * =====================================================================================
 *
 *       Filename:  truncate.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/06/2019 04:19:30 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#ifndef SPLITFS_TRUNCATE_H
#define SPLITFS_TRUNCATE_H

#include "utils.h"
#include "handle_mmaps.h"
#include "inode.h"
#include "splitfs-posix.h"
#include "out.h"

long splitfs_truncate_vinode(struct splitfs_vinode *inode, off_t length);

#endif
