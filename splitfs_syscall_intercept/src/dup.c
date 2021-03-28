/*
 * =====================================================================================
 *
 *       Filename:  dup.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:36:54 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <libsyscall_intercept_hook_point.h>
#include <stdlib.h>

#include <nv_common.h>
#include "nvp_lock.h"
#include "file.h"
#include "inode.h"

void _nvp_test_invalidate_node(struct NVFile* nvf)
{
	struct NVNode* node = nvf->node;

	DEBUG("munmapping temporarily diabled...\n"); // TODO

	return;

	SANITYCHECK(node!=NULL);

	pthread_spin_lock(&node_lookup_lock[(int) (pthread_self() % NUM_NODE_LISTS)]);
	NVP_LOCK_NODE_WR(nvf);
	node->reference--;
	NVP_UNLOCK_NODE_WR(nvf);
	if (node->reference == 0) {
		NVP_LOCK_NODE_WR(nvf);
		int index = nvf->serialno % 1024;
		_nvp_ino_lookup[index] = 0;
		// FIXME: Also munmap?
		nvp_cleanup_node(nvf->node, 0, 1);
		node->serialno = 0;
		NVP_UNLOCK_NODE_WR(nvf);
	}
	pthread_spin_unlock(&node_lookup_lock[(int) (pthread_self() % NUM_NODE_LISTS)]);

}

RETT_SYSCALL_INTERCEPT _sfs_DUP(INTF_SYSCALL)
{
	DEBUG("In %s\n", __func__);
	int ret = 0, file;
	
	file = (int)arg0;

	MSG("DUP. FD: %d.\n", file);

	if((file<0) || (file >= OPEN_MAX) ) {
		MSG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		*result = -EBADF;
		return RETT_NO_PASS_KERN;
	}

	if(!_fd_intercept_lookup[file]) {
		return RETT_PASS_KERN;
	}

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_WR(nvf); // TODO

	ret = syscall_no_intercept(SYS_dup, file);

	if(ret < 0) 
	{
		DEBUG("Call to %s failed: %s\n", __func__, strerror(-ret));
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		//GLOBAL_UNLOCK_WR();
		*result = ret;
		return RETT_NO_PASS_KERN;
	}

	struct NVFile* nvf2 = &_nvp_fd_lookup[ret];

	nvf->valid = 0;
	nvf2->valid = 0;

	if (nvf->posix) {
		DEBUG("Call posix DUP for fd %d\n", nvf->fd);
		nvf2->posix = nvf->posix;
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		//GLOBAL_UNLOCK_WR();
		*result = ret;
		return RETT_NO_PASS_KERN;
	}

	NVP_LOCK_FD_WR(nvf2);

	if(nvf2->valid) {
		ERROR("fd %i was already in use!\n", ret);
		assert(!nvf2->valid);
	}
	else
	{
		//free(nvf2->offset); // TODO: free this iff it's not in use anymore to avoid memory leaks
	}

	nvf2->fd 	= ret;
	nvf2->offset 	= nvf->offset;
	nvf2->canRead 	= nvf->canRead;
	nvf2->canWrite 	= nvf->canWrite;
	nvf2->append 	= nvf->append;
	nvf2->aligned   = nvf->aligned;
	nvf2->serialno 	= nvf->serialno;
	nvf2->node 	= nvf->node;
	nvf2->posix 	= nvf->posix;

	SANITYCHECK(nvf2->node != NULL);

	nvf->node->reference++;
	nvf->valid      = 1;
	nvf2->valid 	= 1;

	NVP_UNLOCK_NODE_WR(nvf); // nvf2->node->lock == nvf->node->lock since nvf and nvf2 share a node
	NVP_UNLOCK_FD_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf2);

	GLOBAL_UNLOCK_WR();
	*result = nvf2->fd;
	return RETT_NO_PASS_KERN;
}

RETT_SYSCALL_INTERCEPT _sfs_DUP2(INTF_SYSCALL)
{
	DEBUG("In %s\n", __func__);
	int ret = 0, file, fd2;

	file = (int)arg0;
	fd2 = (int)arg1;

	MSG("DUP2. FD1: %d. FD2: %d\n", file, fd2);

	if((file<0) || (file >= OPEN_MAX) ) {
		MSG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		*result = -EBADF;
		return RETT_NO_PASS_KERN;
	}

	if( (fd2<0) || (fd2 >= OPEN_MAX) ) {
		MSG("fd %i is larger than the maximum number of open files; ignoring it.\n", fd2);
		errno = -EBADF;
		return RETT_NO_PASS_KERN;
	}

	if(!_fd_intercept_lookup[file]) {
		return RETT_PASS_KERN;
	}

	if(file == fd2)
	{
		DEBUG("Input and output files were the same (%i)\n", file);
		*result = file;
		return RETT_NO_PASS_KERN;
	}

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	struct NVFile* nvf2 = &_nvp_fd_lookup[fd2];

	if (nvf->posix) {
		DEBUG("Call posix DUP2 for fd %d\n", nvf->fd);
		nvf2->posix = nvf->posix;
		ret = syscall_no_intercept(SYS_dup2, file, fd2);
		nvf2->fd = ret;
		*result = ret;
		return RETT_NO_PASS_KERN;
	}

	if(file > fd2)
	{
		NVP_LOCK_FD_WR(nvf);
		NVP_LOCK_FD_WR(nvf2);
	} else {
		NVP_LOCK_FD_WR(nvf2);
		NVP_LOCK_FD_WR(nvf);
	}

	if( (!nvf->valid)||(!nvf2->valid) ) {
		// errno = EBADF; // TODO: Uncomment this?
		DEBUG("Invalid FD1 %i or FD2 %i\n", file, fd2);
//		NVP_UNLOCK_FD_WR(nvf);
//		NVP_UNLOCK_FD_WR(nvf2);
	}

	if(nvf->node == nvf2->node || !nvf2->node) {
		NVP_LOCK_NODE_WR(nvf);
	} else {
		if(nvf->node > nvf2->node) {
			NVP_LOCK_NODE_WR(nvf);
			NVP_LOCK_NODE_WR(nvf2);
		} else {
			NVP_LOCK_NODE_WR(nvf2);
			NVP_LOCK_NODE_WR(nvf);
		}
	}

	ret = syscall_no_intercept(SYS_dup2, file, fd2);

	if(ret < 0)
	{
		DEBUG("_nvp_DUP2 failed to %s "
			"(returned %i): %s\n", __func__, file,
			fd2, ret, strerror(-ret));
		NVP_UNLOCK_NODE_WR(nvf);
		if(nvf->node != nvf2->node) { NVP_UNLOCK_NODE_WR(nvf2); }
		NVP_UNLOCK_FD_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf2);
		//GLOBAL_UNLOCK_WR();
		*result = -ret;
		return RETT_NO_PASS_KERN;
	}
	else
	{
		//free(nvf2->offset); // TODO: free this iff it's not in use anymore to avoid memory leaks
	}

	nvf2->valid = 0;

	if(nvf2->node && nvf->node != nvf2->node) { NVP_UNLOCK_NODE_WR(nvf2); }

	_nvp_test_invalidate_node(nvf2);

	if(ret != fd2)
	{
		WARNING("ret of _nvp_DUP2(%i, %i) didn't return the fd2 "
			"that was just closed.  Technically this doesn't "
			"violate POSIX, but I DON'T LIKE IT. "
			"(Got %i, expected %i)\n",
			file, fd2, ret, fd2);
		assert(0);

		NVP_UNLOCK_FD_WR(nvf2);

		nvf2 = &_nvp_fd_lookup[ret];

		NVP_LOCK_FD_WR(nvf2);

		if(nvf2->valid)
		{
			DEBUG("%s->DUP2 returned a ret which corresponds "
				"to an already open NVFile! dup2(%i, %i) "
				"returned %i\n", __func__,
				file, fd2, ret);
			assert(0);
		}
	}

	nvf2->fd = ret;
	nvf2->offset = nvf->offset;
	nvf2->canRead = nvf->canRead;
	nvf2->canWrite = nvf->canWrite;
	nvf2->append = nvf->append;
	nvf2->aligned = nvf->aligned;
	nvf2->serialno = nvf->serialno;
	nvf2->node = nvf->node;
	nvf2->valid = nvf->valid;
	nvf2->posix = nvf->posix;
	// Increment the refernce count as this file 
	// descriptor is pointing to the same NVFNode
	nvf2->node->reference++;

	SANITYCHECK(nvf2->node != NULL);
	SANITYCHECK(nvf2->valid);

	DEBUG("fd2 should now match fd1. "
		"Testing to make sure this is true.\n");

	NVP_CHECK_NVF_VALID_WR(nvf2);

	NVP_UNLOCK_NODE_WR(nvf); // nvf2 was already unlocked.  old nvf2 was not the same node, but new nvf2 shares a node with nvf1
	NVP_UNLOCK_FD_WR(nvf2);
	NVP_UNLOCK_FD_WR(nvf);

	*result = nvf2->fd;
	return RETT_NO_PASS_KERN;
}
