/*
 * =====================================================================================
 *
 *       Filename:  vfd_table.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/03/2019 07:39:24 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#ifndef SPLITFS_VFD_TABLE_H
#define SPLITFS_VFD_TABLE_H

struct vfile_description;
struct splitfs_file;

long splitfs_vfd_assign(long vfd, struct splitfs_file *file);
int splitfs_vfd_dup(int vfd);
int splitfs_vfd_fcntl_dup(int vfd, int min_new_vfd);
int splitfs_vfd_dup2(int old_vfd, int new_vfd);
int splitfs_vfd_dup3(int old_vfd, int new_vfd, int flags);
SPLITFSfile *splitfs_vfd_ref(int vfd);
void splitfs_vfd_unref(int vfd);

long splitfs_vfd_close(int vfd);

void splitfs_vfd_table_init(void);

SPLITFSfile *splitfs_execv_vfd_get(int vfd);

#endif
