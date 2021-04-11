/*
 * =====================================================================================
 *
 *       Filename:  mmap_pool.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/05/2019 05:22:53 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#ifndef SPLITFS_MMAP_POOL_H
#define SPLITFS_MMAP_POOL_H

void *splitfs_mmap_assign(void);
void splitfs_mmap_add(void *entry);

#endif
