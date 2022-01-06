/*
 * =====================================================================================
 *
 *       Filename:  relink.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/28/2019 11:33:39 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#ifndef SPLITFS_RELINK_H
#define SPLITFS_RELINK_H

#include <nv_common.h>
#include "file.h"

size_t dynamic_remap(int file_fd, struct NVNode *node, int close);
size_t swap_extents(struct NVFile *nvf, int close);
void perform_dynamic_remap(struct NVFile *nvf);

#endif
