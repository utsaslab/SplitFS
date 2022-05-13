/*
 * =====================================================================================
 *
 *       Filename:  staging.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/04/2019 11:52:12 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#ifndef SPLITFS_STAGING_H
#define SPLITFS_STAGING_H

#include "file.h"

#define NUM_STAGING_FILES 10
#define STAGING_FILE_SIZE (160*1024*1024)

struct sfile_description {
    long fd;
    void *start_addr;
	uint32_t ino;
	off_t valid_offset;
	off_t start_offset;
	off_t end_offset;
};

long splitfs_staging_file_assign(struct splitfs_file *file);
void splitfs_add_to_staging_pool(struct sfile_description *staging);
void align_valid_offset(struct sfile_description *staging, off_t alignment);
void get_staging_file(struct splitfs_file *file);
void splitfs_spool_init(void);
void create_and_add_staging_files(int num_files);

#endif
