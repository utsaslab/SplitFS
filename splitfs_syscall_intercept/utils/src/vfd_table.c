/*
 * =====================================================================================
 *
 *       Filename:  vfd_table.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/03/2019 06:03:17 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <fcntl.h>
#include <syscall.h>

#include <libsyscall_intercept_hook_point.h>
#include <splitfs-posix.h>

#include "constants.h"
#include "sys_util.h"
#include "preload.h"
#include "out.h"

struct vfile_description {
    SPLITFSfile *file;
    int ref_count;
};

static void
vf_ref_count_inc(struct vfile_description *entry) {

    __atomic_add_fetch(&entry->ref_count, 1, __ATOMIC_ACQ_REL);
}

static void
ref_vfd_entry(struct vfile_description *entry) {

    if (entry != NULL)
        vf_ref_count_inc(entry);
}

static int
vf_ref_count_dec_and_fetch(struct vfile_description *entry) {

    return __atomic_sub_fetch(&entry->ref_count, 1, __ATOMIC_ACQ_REL);
}

static struct vfile_description *vfd_table[OPEN_MAX];

static pthread_mutex_t vfd_table_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct vfile_description *free_vfile_slots[ARRAY_SIZE(vfd_table)];
static unsigned free_slot_count;
static pthread_mutex_t free_vfile_slot_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool
is_in_vfd_table_range(int number) {

    return (number >= 0) && (number < (int)ARRAY_SIZE(vfd_table));
}

static bool
can_be_in_vfd_table(int vfd) {

    if (!is_in_vfd_table_range(vfd))
        return false;

    return __atomic_load_n(vfd_table + vfd, __ATOMIC_CONSUME) != NULL;
}

static void
mark_as_free_file_slot(struct vfile_description *entry) {

    util_mutex_lock(&free_vfile_slot_mutex);

    free_vfile_slots[free_slot_count++] = entry;

    util_mutex_unlock(&free_vfile_slot_mutex);
}


static void
unref_vfd_entry(long fd, struct vfile_description *entry) {

    if (entry == NULL)
        return;

    if (vf_ref_count_dec_and_fetch(entry) == 0) {
        splitfs_close(fd, entry->file);
        mark_as_free_file_slot(entry);
    }
}

void splitfs_vfd_unref(int vfd) {
    return unref_vfd_entry(vfd, vfd_table[vfd]);
}

SPLITFSfile *
splitfs_vfd_ref(int vfd) {

    struct vfile_description *entry;

	util_mutex_lock(&vfd_table_mutex);

    ref_vfd_entry(vfd_table[vfd]);
    entry = vfd_table[vfd];

	util_mutex_unlock(&vfd_table_mutex);

    if (entry)
        return entry->file;
    else
        return NULL;
}

SPLITFSfile*
splitfs_execv_vfd_get(int vfd) {
	struct vfile_description *entry;
	entry = vfd_table[vfd];
	if (entry)
        return entry->file;
    else
        return NULL;
}

static struct vfile_description *
fetch_free_file_slot(void) {

    struct vfile_description *entry;

    util_mutex_lock(&free_vfile_slot_mutex);

    if (free_slot_count == 0)
        entry = NULL;
    else
        entry = free_vfile_slots[--free_slot_count];

    util_mutex_unlock(&free_vfile_slot_mutex);

    return entry;
}

static void
setup_free_slots(void) {

    static struct vfile_description store[ARRAY_SIZE(free_vfile_slots) - 1];

    for (unsigned i = 0; i < ARRAY_SIZE(store); ++i)
        mark_as_free_file_slot(store + i);
}

long
splitfs_vfd_assign(long vfd,
        struct splitfs_file *file) {

    struct vfile_description *entry = fetch_free_file_slot();
    if (entry == NULL)
        return -ENOMEM;

	*entry = (struct vfile_description) {
		.ref_count = 1, .file = file};

	util_mutex_lock(&vfd_table_mutex);

	vfd_table[vfd] = entry;

	util_mutex_unlock(&vfd_table_mutex);

	return vfd;
}

/*
 * vfd_dup2_under_mutex -- perform dup2
 * If the old_vfd refers to entry, increase the corresponding ref_count.
 * If the new_vfd refers to entry, decrease the corresponding ref_count -- this should be done by caller i.e., before the actual dup2 system call.
 * Overwrite the entry pointer in the vfd_table.
 * The order of the three operations does not matter, as long as the entries
 * as different, and all three happen while holding the vfd_table_mutex.
 *
 * Important: dup2 must be atomic from the user's point of view.
 */
static int
vfd_dup2_under_mutex(int old_vfd, int new_vfd)
{
	if (new_vfd < 0)
		return new_vfd;

	/*
	 * "If oldfd is a valid file descriptor, and newfd has the same value
	 * as oldfd, then dup2() does nothing, and returns newfd."
	 *
	 * It is easily verified if the old vfd is valid or not, by asking
	 * the kernel to dup2 the underlying (possible) memfd -- see the
	 * function calling this function.
	 */
	if (old_vfd == new_vfd)
		return new_vfd;

	if (!is_in_vfd_table_range(new_vfd)) {
		/* new_vfd can't be used to index the vfd_table */
		syscall_no_intercept(SYS_close, new_vfd);
		exit(1);
		return -ENOMEM;
	}

	if (vfd_table[old_vfd] == vfd_table[new_vfd])
		return new_vfd;

	ref_vfd_entry(vfd_table[old_vfd]);
	// the below commented call should be done by the caller i.e., before the actual system call for dup2 is done
	// unref_vfd_entry(new_vfd, vfd_table[new_vfd]);
	__atomic_store_n(vfd_table + new_vfd, vfd_table[old_vfd],
			__ATOMIC_RELEASE);

	return new_vfd;
}

/*
 * pmemfile_vfd_dup -- creates a new reference to a vfile_description entry,
 * if the vfd refers to one.
 */
int
splitfs_vfd_dup(int vfd)
{
	if (!can_be_in_vfd_table(vfd))
		return (int)syscall_no_intercept(SYS_dup, vfd);

	int new_vfd;
	util_mutex_lock(&vfd_table_mutex);

	new_vfd = (int)syscall_no_intercept(SYS_dup, vfd);

	new_vfd = vfd_dup2_under_mutex(vfd, new_vfd);

	util_mutex_unlock(&vfd_table_mutex);

	return new_vfd;
}

int
splitfs_vfd_fcntl_dup(int vfd, int min_new_vfd)
{
	if (!can_be_in_vfd_table(vfd))
		return (int)syscall_no_intercept(SYS_fcntl,
				vfd, F_DUPFD, min_new_vfd);

	int new_vfd;
	util_mutex_lock(&vfd_table_mutex);

	new_vfd = (int)syscall_no_intercept(SYS_fcntl,
				vfd, F_DUPFD, min_new_vfd);

	new_vfd = vfd_dup2_under_mutex(vfd, new_vfd);

	util_mutex_unlock(&vfd_table_mutex);

	return new_vfd;
}

/*
 * pmemfile_vfd_dup2 -- create a new reference to a vfile_description entry,
 * potentially replacing a reference to another one.
 */
int
splitfs_vfd_dup2(int old_vfd, int new_vfd)
{
	if ((!can_be_in_vfd_table(old_vfd)) && (!can_be_in_vfd_table(new_vfd)))
		return (int)syscall_no_intercept(SYS_dup2, old_vfd, new_vfd);

	int result;

	util_mutex_lock(&vfd_table_mutex);

	unref_vfd_entry(new_vfd, vfd_table[new_vfd]);

	result = (int)syscall_no_intercept(SYS_dup2, old_vfd, new_vfd);

	assert(result == new_vfd);
	vfd_dup2_under_mutex(old_vfd, new_vfd);

	util_mutex_unlock(&vfd_table_mutex);

	return result;
}

/*
 * pmemfile_vfd_dup3 -- Almost the same as dup2, with two differences:
 * It accepts an additional flag argument.
 * If the old and new fd arguments are the same, both do nothing, but
 * dup2 returns the specified fd, while dup3 indicates EINVAL error.
 */
int
splitfs_vfd_dup3(int old_vfd, int new_vfd, int flags)
{
	if (old_vfd == new_vfd)
		return -EINVAL;

	/*
	 * The only flag allowed in dup3 is O_CLOEXEC, and that is ignored
	 * by pmemfile as of now.
	 * Note: once O_CLOEXEC is handled, it can be stored in the
	 * vfd_table -- not in the corresponding vfile_description struct,
	 * as the flag is specific to fd numbers.
	 */
	if ((flags & ~O_CLOEXEC) != 0)
		return -EINVAL;

	return splitfs_vfd_dup2(old_vfd, new_vfd);
}

 /* pmemfile_vfd_close -- remove a reference from the vfd_table array (if
 * there was one at vfd_table[vfd]).
 * This does not necessarily close an underlying pmemfile file, as some
 * vfd_reference structs given to the user might still reference that entry.
 */
long
splitfs_vfd_close(int vfd)
{
	struct vfile_description *entry = NULL;
    long result = 0;

	util_mutex_lock(&vfd_table_mutex);

	entry = vfd_table[vfd];
	vfd_table[vfd] = NULL;

	util_mutex_unlock(&vfd_table_mutex);

	if (entry != NULL) {
		unref_vfd_entry(vfd, entry);
		result = 0;
	}

	result = syscall_no_intercept(SYS_close, vfd);
	return result;
}

void
splitfs_vfd_table_init(void) {

    setup_free_slots();
}
