// a module which repalces the standart POSIX functions with memory mapped equivalents

#include "nv_common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <cpuid.h>

#include "perfcount.h"

#include "non_temporal.h"
#include "fileops_nvp.h"
#include "merkleLogicalBtree.h"
#include "thread_handle.h"
#include "bg_clear_mmap.h"
#include "stack.h"
#include "lru_cache.h"
#include "perf_delay/add_delay.h"
#include "log.h"
#include "tbl_mmaps.h"

BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _nvp_, ALLOPS_WPAREN)
BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _nvp_, SHM_WPAREN)
BOOST_PP_SEQ_FOR_EACH(DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP, _nvp_, METAOPS)

void _nvp_init2(void);

MODULE_REGISTRATION_F("nvp", _nvp_, _nvp_init2(); );

#define NVP_WRAP_HAS_FD(op) \
	RETT_##op _nvp_##op ( INTF_##op ) {				\
		CHECK_RESOLVE_FILEOPS(_nvp_);				\
		DEBUG("_nvp_"#op" is just wrapping %s->"#op"\n", _nvp_fileops->name); \
		if(UNLIKELY(file>=OPEN_MAX)) { DEBUG("file descriptor too large (%i > %i)\n", file, OPEN_MAX-1); errno = EBADF; return (RETT_##op) -1; } \
		if(UNLIKELY(file<0)) { DEBUG("file < 0 (file = %i).  return -1;\n", file); errno = EBADF; return (RETT_##op) -1; } \
		if(UNLIKELY(!_nvp_fd_lookup[file].valid)) { DEBUG("That file descriptor (%i) is invalid\n", file); errno = EBADF; return -1; } \
		DEBUG("_nvp_" #op " is calling %s->" #op "\n", _nvp_fileops->name); \
		return (RETT_##op) _nvp_fileops->op( CALL_##op );	\
	}

#define NVP_WRAP_NO_FD(op)						\
	RETT_##op _nvp_##op ( INTF_##op ) {				\
		CHECK_RESOLVE_FILEOPS(_nvp_);				\
		DEBUG("_nvp_"#op" is just wrapping %s->"#op"\n", _nvp_fileops->name); \
		return _nvp_fileops->op( CALL_##op );			\
	}

#define NVP_WRAP_HAS_FD_IWRAP(r, data, elem) NVP_WRAP_HAS_FD(elem)
#define NVP_WRAP_NO_FD_IWRAP(r, data, elem) NVP_WRAP_NO_FD(elem)

BOOST_PP_SEQ_FOR_EACH(NVP_WRAP_HAS_FD_IWRAP, placeholder, (ACCEPT))
BOOST_PP_SEQ_FOR_EACH(NVP_WRAP_NO_FD_IWRAP, placeholder, (PIPE) (FORK) (SOCKET) (SOCKETPAIR) (VFORK) (CREAT))


/* ============================= memcpy =============================== */

extern long copy_user_nocache(void *dst, const void *src, unsigned size, int zerorest);

static inline int copy_from_user_inatomic_nocache(void *dst, const void *src, unsigned size) {
	return copy_user_nocache(dst, src, size, 0);
}

static inline void* my_memcpy_nocache(void* dst, const void* src, unsigned size) {
	if(copy_from_user_inatomic_nocache(dst, src, size)) {
		return dst;
	} else { 
		return 0;
	}
}

static inline void *intel_memcpy(void * __restrict__ b, const void * __restrict__ a, size_t n){
	char *s1 = b;
	const char *s2 = a;
	for(; 0<n; --n)*s1++ = *s2++;
	return b;
}

void *(*import_memcpy)(void * __restrict__ b, const void * __restrict__ a, size_t n);

extern void * __memcpy(void * __restrict__ to, const void * __restrict__ from, size_t len);

#define MMX2_MEMCPY_MIN_LEN 0x40
#define MMX_MMREG_SIZE 8

/* ============================= Fsync =============================== */

static unsigned long calculate_capacity(unsigned int height);
static inline size_t dynamic_remap(int file_fd, struct NVNode *node, int close);

static inline size_t swap_extents(struct NVFile *nvf, off_t offset1)
{
	size_t len_swapped = 0;
	off_t offset_in_page = 0;

	len_swapped = dynamic_remap(nvf->fd, nvf->node, 0);
	if (len_swapped < 0) {
		MSG("%s: Dynamic Remap system call failed. Err = %s\n",
		    __func__, strerror(errno));
		assert(0);
	}

	if (nvf->node->dr_info.start_addr != 0) {
		nvf->node->true_length = nvf->node->length;
		if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
			nvf->node->is_large_file = 1;
		nvf->node->dr_info.valid_offset = nvf->node->dr_info.dr_offset_end;
		offset_in_page = nvf->node->dr_info.valid_offset % MMAP_PAGE_SIZE;
		if (offset_in_page != 0)
			nvf->node->dr_info.valid_offset += MMAP_PAGE_SIZE - offset_in_page;
		if (nvf->node->dr_info.valid_offset < DR_SIZE) {
			offset_in_page = nvf->node->true_length % MMAP_PAGE_SIZE;
			if (offset_in_page != 0)
				nvf->node->dr_info.valid_offset += offset_in_page;
		}
			
		DEBUG_FILE("%s: Setting offset_start to DR_SIZE. FD = %d. Valid offset = %lu\n", __func__, nvf->fd, nvf->node->dr_info.valid_offset);
		DEBUG_FILE("%s: -------------------------------\n", __func__);
		nvf->node->dr_info.dr_offset_start = DR_SIZE;
		nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;

		if (nvf->node->dr_info.valid_offset > DR_SIZE)
			assert(0);
		if (nvf->node->dr_info.dr_offset_start > DR_SIZE)
			assert(0);
		if (nvf->node->dr_info.dr_offset_end > DR_SIZE)
			assert(0);
	}

	return len_swapped;
}

static inline void create_dr_mmap(struct NVNode *node, int is_overwrite)
{
	void *addr = NULL;
	struct stat stat_buf;
	char dr_fname[256];
	int dr_fd = 0, ret = 0;
	num_mmap++;
	
	struct free_dr_pool *send_to_global = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool));
       	
	if (is_overwrite) {
		_nvp_full_drs[full_dr_idx].dr_fd = node->dr_over_info.dr_fd;
		_nvp_full_drs[full_dr_idx].start_addr = node->dr_over_info.start_addr;
		_nvp_full_drs[full_dr_idx].size = DR_OVER_SIZE;
		full_dr_idx++;
	} else {
		_nvp_full_drs[full_dr_idx].dr_fd = node->dr_info.dr_fd;
		_nvp_full_drs[full_dr_idx].start_addr = node->dr_info.start_addr;
		_nvp_full_drs[full_dr_idx].size = DR_SIZE;
		full_dr_idx++;
	}
	
	if (is_overwrite)
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-OVER-XXXXXX");		
	else
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-XXXXXX");				
	dr_fd = _hub_find_fileop("posix")->OPEN(mktemp(dr_fname), O_RDWR | O_CREAT, 0666);
	if (dr_fd < 0) {
		MSG("%s: mkstemp of DR file failed. Err = %s\n",
		    __func__, strerror(errno));
		assert(0);
	}
	if (is_overwrite)
		ret = posix_fallocate(dr_fd, 0, DR_OVER_SIZE);
	else
		ret = posix_fallocate(dr_fd, 0, DR_SIZE);
			
	if (ret < 0) {
		MSG("%s: posix_fallocate failed. Err = %s\n",
		    __func__, strerror(errno));
		assert(0);
	}

	fstat(dr_fd, &stat_buf);

	if (is_overwrite) {
		node->dr_over_info.dr_fd = dr_fd;
		node->dr_over_info.start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_OVER_SIZE,
			 PROT_READ | PROT_WRITE, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 node->dr_over_info.dr_fd, //fd_with_max_perms,
			 0
			 );	

		if (node->dr_over_info.start_addr == 0) {
			MSG("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
			assert(0);
		}
		node->dr_over_info.valid_offset = 0;
		node->dr_over_info.dr_offset_start = 0;
		node->dr_over_info.dr_offset_end = DR_OVER_SIZE;
		node->dr_over_info.dr_serialno = stat_buf.st_ino;

	} else {
		node->dr_info.dr_fd = dr_fd;
		node->dr_info.start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_SIZE,
			 PROT_READ | PROT_WRITE, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 node->dr_info.dr_fd, //fd_with_max_perms,
			 0
			 );	

		if (node->dr_info.start_addr == 0) {
			MSG("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
			assert(0);
		}
		node->dr_info.valid_offset = 0;
		node->dr_info.dr_offset_start = DR_SIZE;
		node->dr_info.dr_offset_end = node->dr_info.valid_offset;
		node->dr_info.dr_serialno = stat_buf.st_ino;
	}
	
	DEBUG_FILE("%s: Unmapped and mapped DR file again\n", __func__);
}

static inline void change_dr_mmap(struct NVNode *node, int is_overwrite) {
	struct free_dr_pool *temp_dr_mmap = NULL;
	unsigned long offset_in_page = 0;

	DEBUG_FILE("%s: Throwing away DR File FD = %d\n", __func__, node->dr_info.dr_fd);	

	if (is_overwrite) {
		if( lfds711_queue_umm_dequeue(&qs_over, &qe_over) ) {
			// Found addr in global pool		
			struct free_dr_pool *temp_dr_info = NULL;
			temp_dr_info = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe_over );
			node->dr_over_info.start_addr = temp_dr_info->start_addr;
			node->dr_over_info.valid_offset = temp_dr_info->valid_offset;
			node->dr_over_info.dr_offset_start = temp_dr_info->dr_offset_start;
			node->dr_over_info.dr_fd = temp_dr_info->dr_fd;
			node->dr_over_info.dr_serialno = temp_dr_info->dr_serialno;
			node->dr_over_info.dr_offset_end = DR_OVER_SIZE;
			DEBUG_FILE("%s: DR found in global pool. Got from global pool. FD = %d\n",
				   __func__, temp_dr_info->dr_fd);
		} else {
			DEBUG_FILE("%s: Global queue empty\n", __func__);
			memset((void *)&node->dr_info, 0, sizeof(struct free_dr_pool));				
		}
	} else {	
		if( lfds711_queue_umm_dequeue(&qs, &qe) ) {
			// Found addr in global pool		
			struct free_dr_pool *temp_dr_info = NULL;
			temp_dr_info = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe );
			node->dr_info.start_addr = temp_dr_info->start_addr;
			node->dr_info.valid_offset = temp_dr_info->valid_offset;
			node->dr_info.dr_offset_start = DR_SIZE;
			node->dr_info.dr_fd = temp_dr_info->dr_fd;
			node->dr_info.dr_serialno = temp_dr_info->dr_serialno;
			node->dr_info.dr_offset_end = temp_dr_info->valid_offset;
			DEBUG_FILE("%s: DR found in global pool. Got from global pool. FD = %d\n",
				   __func__, temp_dr_info->dr_fd);
		} else {
			DEBUG_FILE("%s: Global queue empty\n", __func__);
			memset((void *)&node->dr_info, 0, sizeof(struct free_dr_pool));				
		}
	}
	
	__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);

	callBgCleaningThread(is_overwrite);
}

void perform_dynamic_remap(struct NVFile *nvf) {
	
	loff_t offset_in_page = 0;

	DEBUG_FILE("%s: Syncing file %d\n", __func__, nvf->fd);
	dynamic_remap(nvf->fd, nvf->node, 0);

	nvf->node->true_length = nvf->node->length;
	if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
		nvf->node->is_large_file = 1;
	nvf->node->dr_info.valid_offset = nvf->node->dr_info.dr_offset_end;
	offset_in_page = nvf->node->dr_info.valid_offset % MMAP_PAGE_SIZE;
	if (offset_in_page != 0)
		nvf->node->dr_info.valid_offset += MMAP_PAGE_SIZE - offset_in_page;
	if (nvf->node->dr_info.valid_offset < DR_SIZE) {
		offset_in_page = nvf->node->true_length % MMAP_PAGE_SIZE;
		if (offset_in_page != 0)
			nvf->node->dr_info.valid_offset += offset_in_page;
	}
			
	DEBUG_FILE("%s: Setting offset_start to DR_SIZE. FD = %d. Valid offset = %lu\n", __func__, nvf->fd, nvf->node->dr_info.valid_offset);
	DEBUG_FILE("%s: -------------------------------\n", __func__);
	nvf->node->dr_info.dr_offset_start = DR_SIZE;
	nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;
	
	if (nvf->node->dr_info.valid_offset > DR_SIZE)
		assert(0);
	if (nvf->node->dr_info.dr_offset_start > DR_SIZE)
		assert(0);
	if (nvf->node->dr_info.dr_offset_end > DR_SIZE)
		assert(0);
}

#if DATA_JOURNALING_ENABLED

static void get_lowest_tbl_elem(off_t *over_file_start,
				off_t *over_file_end,
				off_t *over_dr_start,
				off_t *over_dr_end,
				struct NVTable_maps *tbl,
				int idx_in_over)
{
	*over_file_start = tbl->tbl_mmaps[idx_in_over].file_start_off;
	*over_file_end = tbl->tbl_mmaps[idx_in_over].file_end_off;
	*over_dr_start = tbl->tbl_mmaps[idx_in_over].dr_start_off;
	*over_dr_end = tbl->tbl_mmaps[idx_in_over].dr_end_off;
}

static void get_tbl_elem_large(off_t *over_file_start,
			       off_t *over_file_end,
			       off_t *over_dr_start,
			       off_t *over_dr_end,
			       struct table_mmaps *tbl_mmaps,
			       int idx_in_over)
{
	*over_file_start = tbl_mmaps[idx_in_over].file_start_off;
	*over_file_end = tbl_mmaps[idx_in_over].file_end_off;
	*over_dr_start = tbl_mmaps[idx_in_over].dr_start_off;
	*over_dr_end = tbl_mmaps[idx_in_over].dr_end_off;
}


static int get_lowest_tbl_elem_large(off_t *over_file_start,
				     off_t *over_file_end,
				     off_t *over_dr_start,
				     off_t *over_dr_end,
				     struct table_mmaps *tbl_mmaps,
				     int tbl_mmap_index,
				     off_t max_value)
{	
	off_t min_value = max_value;
	int i = 0, idx_in_over = -1;	

	for (i = 0; i < tbl_mmap_index; i++) {
		if (tbl_mmaps[i].dr_end_off != 0 && tbl_mmaps[i].file_start_off < min_value) {
			idx_in_over = i;
			min_value = tbl_mmaps[i].file_start_off;
		}
	}

	if (idx_in_over != -1) {
		*over_file_start = tbl_mmaps[idx_in_over].file_start_off;
		*over_file_end = tbl_mmaps[idx_in_over].file_end_off;
		*over_dr_start = tbl_mmaps[idx_in_over].dr_start_off;
		*over_dr_end = tbl_mmaps[idx_in_over].dr_end_off;
		return 1;
	}

	return 0;
}

static inline size_t dynamic_remap_large(int file_fd, struct NVNode *node, int close)
{
	size_t len_to_write = 0, len_written = 0, len_to_swap = 0, len_swapped = 0;
	off_t app_start_addr = 0;
	off_t app_start_off = 0;
	off_t file_start_off = node->true_length;
	off_t over_file_start = 0, over_file_end = 0;
	off_t over_dr_start = 0, over_dr_end = 0;
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[node->serialno % OVER_TBL_MAX];
	struct NVTable_regions *regions = _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].regions;
	int region_id = 0;
	int valid = 0, i = 0, tbl_idx = 0;
	int max_region_id = 0;
	instrumentation_type swap_extents_time, insert_tbl_mmap_time;

	DEBUG_FILE("%s: START: file_fd = %d. dr start addr = %p, dr over start addr = %p, true_length = %lu, length = %lu, Inode number = %lu\n",
		   __func__, file_fd, node->dr_info.start_addr, node->dr_over_info.start_addr, node->true_length, node->length, node->serialno);
	
	if (node->dr_info.start_addr == 0 && node->dr_over_info.start_addr == 0)
		return 0;

	if (node->dr_info.start_addr != 0) {
		app_start_addr = node->dr_info.start_addr;
		app_start_off = node->dr_info.dr_offset_start;
	}	

	i = _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region;
	while (i < _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].max_dirty_region) {
		if (regions[i].highest_off >= node->true_length) {
			break;			
		}
		i++;
	}

	max_region_id = _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].max_dirty_region;
	region_id = _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region;	
	while (region_id <= max_region_id) {
		tbl_idx = 0;
		while (tbl_idx < tbl_over->tbl_mmap_index) {
			get_tbl_elem_large(&over_file_start,
					   &over_file_end,
					   &over_dr_start,
					   &over_dr_end,
					   regions[region_id].tbl_mmaps,
					   tbl_idx);
		
			if (over_dr_end == 0) {
				tbl_idx++;
				continue;
			}
			
			if (over_dr_start > over_dr_end) {
				MSG("%s: over_file_start = %lld, over_file_end = %lld, over_dr_start = %lld, over_dr_end = %lld\n", __func__, over_file_start, over_file_end, over_dr_start, over_dr_end);
				assert(0);
			}
			if (over_file_start > over_file_end)
				assert(0);
			if (over_dr_start > node->dr_over_info.dr_offset_end)
				assert(0);
			
			len_to_swap = over_file_end - over_file_start + 1;
			START_TIMING(swap_extents_t, swap_extents_time);
			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, over_dr fd = %d, file_start = %lld, over_dr start = %lld, over_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_over_info.dr_fd, over_file_start, over_dr_start, (const char *) node->dr_over_info.start_addr, len_to_swap);
			len_swapped = syscall(335, file_fd,
					      node->dr_over_info.dr_fd,
					      over_file_start,
					      over_dr_start,
					      (const char *) node->dr_over_info.start_addr,
					      len_to_swap);
#if 0
			len_swapped = _nvp_fileops->PWRITE(file_fd, (char *) (node->dr_over_info.start_addr + over_dr_start), len_to_swap, over_file_start);
#endif

			tbl_over->tbl_mmaps[tbl_idx].dr_end_off = 0;		
			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;
			tbl_idx++;
		}
		regions[region_id].region_dirty = 0;
		if (_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region == region_id)
			_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region++;
		
		region_id++;
	}

	while (region_id < _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].max_dirty_region) {
		tbl_idx = 0;
		while (regions[region_id].region_dirty == 1 && tbl_idx < regions[region_id].tbl_mmap_index) {
			valid = get_lowest_tbl_elem_large(&over_file_start,
							  &over_file_end,
							  &over_dr_start,
							  &over_dr_end,
							  regions[region_id].tbl_mmaps,
							  regions[region_id].tbl_mmap_index,
							  regions[region_id].highest_off);

			if (valid == 0)
				break;

			if (over_dr_start > over_dr_end)
				assert(0);
			if (over_file_start > over_file_end)
				assert(0);
			if (over_dr_start > node->dr_over_info.dr_offset_end)
				assert(0);
		
			if (file_start_off < over_file_start && app_start_addr != 0) {
				len_to_swap = over_file_start - file_start_off + 1;
				app_start_off = node->dr_info.dr_offset_start +
					file_start_off - node->true_length;
				app_start_addr = node->dr_info.start_addr +
					app_start_off;
			
				if (app_start_off > node->dr_info.dr_offset_end)
					assert(0);
						
				// Perform swap extents from append DR file
				START_TIMING(swap_extents_t, swap_extents_time);
				DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, app_dr fd = %d, file_start = %lld, app_dr start = %lld, app_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_info.dr_fd, file_start_off, app_start_off, (const char *) node->dr_info.start_addr, len_to_swap);
				len_swapped = syscall(335, file_fd,
						      node->dr_info.dr_fd,
						      file_start_off,
						      app_start_off,
						      (const char *) node->dr_info.start_addr,
						      len_to_swap);
				
				END_TIMING(swap_extents_t, swap_extents_time);
				num_appendfsync++;
				len_written += len_swapped;
				file_start_off += len_swapped;
				START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
				insert_tbl_mmap_entry(node,
						      file_start_off,
						      app_start_off,
						      len_swapped,
						      app_start_addr);
				END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			}

			if (over_dr_start > over_dr_end)
				assert(0);
			if (over_file_start > over_file_end)
				assert(0);
			if (over_file_start != file_start_off)
				assert(0);
			if (over_dr_start > node->dr_over_info.dr_offset_end)
				assert(0);

			// Perform swap extents based on over file
			START_TIMING(swap_extents_t, swap_extents_time);
			len_to_swap = over_file_end - over_file_start + 1;
			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, over_dr fd = %d, file_start = %lld, over_dr start = %lld, over_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_over_info.dr_fd, file_start_off, over_dr_start, (const char *) node->dr_over_info.start_addr, len_to_swap);
			len_swapped = syscall(335, file_fd,
					      node->dr_over_info.dr_fd,
					      over_file_start,
					      over_dr_start,
					      (const char *) node->dr_over_info.start_addr,
					      len_to_swap);
			
			tbl_over->tbl_mmaps[tbl_idx].dr_end_off = 0;		
			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;

			if (over_file_start > node->true_length)
				file_start_off += len_swapped;
			len_written += len_swapped;
			
			tbl_idx++;
		}

		regions[region_id].region_dirty = 0;
		region_id++;
		if (_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region == region_id)
			_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region++;
	}

	_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region = LARGE_TBL_REGIONS;
	_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].max_dirty_region = 0;


	if (app_start_addr != 0) {
		app_start_off = node->dr_info.dr_offset_start +
			file_start_off - node->true_length;
	       		
		if (node->dr_info.dr_offset_start > node->dr_info.dr_offset_end)
			assert(0);
		if (app_start_off > node->dr_info.dr_offset_end)
			assert(0);
		if ((app_start_off % MMAP_PAGE_SIZE) != (file_start_off % MMAP_PAGE_SIZE))
			assert(0);
		
		len_to_swap = node->dr_info.dr_offset_end - app_start_off;

		if (len_written < len_to_swap) {
			app_start_addr = node->dr_info.start_addr + app_start_off;
			
			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, app_dr fd = %d, file_start = %lld, app_dr start = %lld, app_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_info.dr_fd, file_start_off, app_start_off, (const char *) node->dr_info.start_addr, len_to_swap);
			// Perform swap extents from append DR file
			START_TIMING(swap_extents_t, swap_extents_time);
			len_swapped = syscall(335, file_fd,
					      node->dr_info.dr_fd,
					      file_start_off,
					      app_start_off,
					      (const char *) node->dr_info.start_addr,
					      len_to_swap);

#if 0
			len_swapped = _nvp_fileops->PWRITE(file_fd, (char *) (node->dr_info.start_addr + app_start_off), len_to_swap, file_start_off);
#endif
			if (len_swapped != len_to_swap)
				assert(0);
			
			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;		
			START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			insert_tbl_mmap_entry(node,
					      file_start_off,
					      app_start_off,
					      len_swapped,
					      app_start_addr);
			END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			len_written += len_swapped;
		}
	}
	return len_written;
}

#endif // DATA_JOURNALING_ENABLED 

static inline size_t dynamic_remap(int file_fd, struct NVNode *node, int close)
{
	size_t len_to_write = 0, len_written = 0, len_to_swap = 0, len_swapped = 0;
	off_t app_start_addr = 0;
	off_t app_start_off = 0;
	off_t file_start_off = node->true_length;
	off_t over_file_start = 0, over_file_end = 0;
	off_t over_dr_start = 0, over_dr_end = 0;
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[node->serialno % OVER_TBL_MAX];
	int idx_in_over = 0;
	instrumentation_type swap_extents_time, insert_tbl_mmap_time;

	DEBUG_FILE("%s: START: file_fd = %d. dr start addr = %p, dr over start addr = %p, true_length = %lu, length = %lu, Inode number = %lu\n",
		   __func__, file_fd, node->dr_info.start_addr, node->dr_over_info.start_addr, node->true_length, node->length, node->serialno);
	
	if (node->dr_info.start_addr == 0 && node->dr_over_info.start_addr == 0)
		return 0;

	if (node->dr_info.start_addr != 0) {
		app_start_addr = node->dr_info.start_addr;
		app_start_off = node->dr_info.dr_offset_start;
	}

#if DATA_JOURNALING_ENABLED
	
	if (node->is_large_file)
		return dynamic_remap_large(file_fd, node, close);
	
	while (idx_in_over < tbl_over->tbl_mmap_index) {
		get_lowest_tbl_elem(&over_file_start,
				    &over_file_end,
				    &over_dr_start,
				    &over_dr_end,
				    tbl_over,
				    idx_in_over);

		if (over_file_start >= node->true_length)
			break;

		if (over_dr_end == 0) {
			idx_in_over++;
			continue;
		}

		if (over_dr_start > over_dr_end) {
			MSG("%s: over_file_start = %lld, over_file_end = %lld, over_dr_start = %lld, over_dr_end = %lld\n", __func__, over_file_start, over_file_end, over_dr_start, over_dr_end);
			assert(0);
		}
		if (over_file_start > over_file_end)
			assert(0);
		if (over_dr_start > node->dr_over_info.dr_offset_end)
			assert(0);
			
		len_to_swap = over_file_end - over_file_start + 1;
		START_TIMING(swap_extents_t, swap_extents_time);
		DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, over_dr fd = %d, file_start = %lld, over_dr start = %lld, over_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_over_info.dr_fd, over_file_start, over_dr_start, (const char *) node->dr_over_info.start_addr, len_to_swap);
		len_swapped = syscall(335, file_fd,
				      node->dr_over_info.dr_fd,
				      over_file_start,
				      over_dr_start,
				      (const char *) node->dr_over_info.start_addr,
				      len_to_swap);		
#if 0
		len_swapped = _nvp_fileops->PWRITE(file_fd, (char *) (node->dr_over_info.start_addr + over_dr_start), len_to_swap, over_file_start);
#endif

		tbl_over->tbl_mmaps[idx_in_over].dr_end_off = 0;		
		END_TIMING(swap_extents_t, swap_extents_time);
		num_appendfsync++;
		idx_in_over++;
	}
	
	while (idx_in_over < tbl_over->tbl_mmap_index) {
		get_lowest_tbl_elem(&over_file_start,
				    &over_file_end,
				    &over_dr_start,
				    &over_dr_end,
				    tbl_over,
				    idx_in_over);

		if (over_dr_end == 0) {
			idx_in_over++;
			continue;
		}

		if (over_dr_start > over_dr_end)
			assert(0);
		if (over_file_start > over_file_end)
			assert(0);
		if (over_dr_start > node->dr_over_info.dr_offset_end)
			assert(0);
		
		if (file_start_off < over_file_start && app_start_addr != 0) {
			len_to_swap = over_file_start - file_start_off + 1;
			app_start_off = node->dr_info.dr_offset_start +
				file_start_off - node->true_length;
			app_start_addr = node->dr_info.start_addr +
				app_start_off;
			
			if (app_start_off > node->dr_info.dr_offset_end)
				assert(0);
						
			// Perform swap extents from append DR file
			START_TIMING(swap_extents_t, swap_extents_time);
			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, app_dr fd = %d, file_start = %lld, app_dr start = %lld, app_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_info.dr_fd, file_start_off, app_start_off, (const char *) node->dr_info.start_addr, len_to_swap);
			len_swapped = syscall(335, file_fd,
					      node->dr_info.dr_fd,
					      file_start_off,
					      app_start_off,
					      (const char *) node->dr_info.start_addr,
					      len_to_swap);
#if 0
			len_swapped = _nvp_fileops->PWRITE(file_fd, (char *) (node->dr_info.start_addr + app_start_off), len_to_swap, file_start_off);
#endif

			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;
			len_written += len_swapped;
			file_start_off += len_swapped;
			START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			insert_tbl_mmap_entry(node,
					      file_start_off,
					      app_start_off,
					      len_swapped,
					      app_start_addr);
			END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
		}

		if (over_dr_start > over_dr_end)
			assert(0);
		if (over_file_start > over_file_end)
			assert(0);
		if (over_file_start != file_start_off)
			assert(0);
		if (over_dr_start > node->dr_over_info.dr_offset_end)
			assert(0);
		
		// Perform swap extents based on over file
		START_TIMING(swap_extents_t, swap_extents_time);
		len_to_swap = over_file_end - over_file_start + 1;
		DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, over_dr fd = %d, file_start = %lld, over_dr start = %lld, over_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_over_info.dr_fd, file_start_off, over_dr_start, (const char *) node->dr_over_info.start_addr, len_to_swap);
	        len_swapped = syscall(335, file_fd,
				      node->dr_over_info.dr_fd,
				      over_file_start,
				      over_dr_start,
				      (const char *) node->dr_over_info.start_addr,
				      len_to_swap);
#if 0		
		len_swapped = _nvp_fileops->PWRITE(file_fd, (char *) (node->dr_over_info.start_addr + over_dr_start), len_to_swap, over_file_start);
#endif

		tbl_over->tbl_mmaps[idx_in_over].dr_end_off = 0;		
		END_TIMING(swap_extents_t, swap_extents_time);
		num_appendfsync++;
		file_start_off += len_swapped;
		len_written += len_swapped;

		idx_in_over++;
	}

#endif // DATA_JOURNALING_ENABLED
	
	if (app_start_addr != 0) {
		app_start_off = node->dr_info.dr_offset_start +
			file_start_off - node->true_length;
	       		
		if (node->dr_info.dr_offset_start > node->dr_info.dr_offset_end)
			assert(0);
		if (app_start_off > node->dr_info.dr_offset_end)
			assert(0);
		if ((app_start_off % MMAP_PAGE_SIZE) != (file_start_off % MMAP_PAGE_SIZE))
			assert(0);
		
		len_to_swap = node->dr_info.dr_offset_end - app_start_off;

		if (len_written < len_to_swap) {
			app_start_addr = node->dr_info.start_addr + app_start_off;
			
			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, app_dr fd = %d, file_start = %lld, app_dr start = %lld, app_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_info.dr_fd, file_start_off, app_start_off, (const char *) node->dr_info.start_addr, len_to_swap);
			// Perform swap extents from append DR file
			len_swapped = syscall(335, file_fd,
					      node->dr_info.dr_fd,
					      file_start_off,
					      app_start_off,
					      (const char *) node->dr_info.start_addr,
					      len_to_swap);
#if 0
			len_swapped = _nvp_fileops->PWRITE(file_fd, (char *) (node->dr_info.start_addr + app_start_off), len_to_swap, file_start_off);
#endif
			if (len_swapped != len_to_swap) {
				MSG("%s: len_swapped = %lu. Len to swap = %lu\n", __func__, len_swapped, len_to_swap);
				if (len_swapped == -1) {
					MSG("%s: Swap extents failed. Err = %s\n", __func__, strerror(errno));
				}
				assert(0);
			}
			
			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;		
			START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			insert_tbl_mmap_entry(node,
					      file_start_off,
					      app_start_off,
					      len_swapped,
					      app_start_addr);
			END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			len_written += len_swapped;
		}
	}
	return len_written;
}

static inline void copy_appends_to_file(struct NVFile* nvf, int close, int fdsync)
{
	unsigned long start_addr, addr;
	off_t start_offset, curr_offset, unmap_from_page, unmap_to_page, dr_offset_start = 0, dr_offset_end = 0;
	int index;
	unsigned long offset_in_page = 0;
	size_t len_to_write = 0;
	long long posix_write = 0;
	instrumentation_type swap_extents_time, insert_tbl_mmap_time;

	if (close && nvf->node->reference > 1)
		goto out;
	
	dynamic_remap(nvf->fd, nvf->node, close);

	if (nvf->node->dr_info.start_addr != 0) {
		nvf->node->true_length = nvf->node->length;
		if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
			nvf->node->is_large_file = 1;
		nvf->node->dr_info.valid_offset = nvf->node->dr_info.dr_offset_end;
		offset_in_page = nvf->node->dr_info.valid_offset % MMAP_PAGE_SIZE;
		if (offset_in_page != 0)
			nvf->node->dr_info.valid_offset += MMAP_PAGE_SIZE - offset_in_page;
		if (nvf->node->dr_info.valid_offset < DR_SIZE) {
			offset_in_page = nvf->node->true_length % MMAP_PAGE_SIZE;
			if (offset_in_page != 0)
				nvf->node->dr_info.valid_offset += offset_in_page;
		}
			
		DEBUG_FILE("%s: Setting offset_start to DR_SIZE. FD = %d. Valid offset = %lu\n", __func__, nvf->fd, nvf->node->dr_info.valid_offset);
		DEBUG_FILE("%s: -------------------------------\n", __func__);
		nvf->node->dr_info.dr_offset_start = DR_SIZE;
		nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;

		if (nvf->node->dr_info.valid_offset > DR_SIZE)
			assert(0);
		if (nvf->node->dr_info.dr_offset_start > DR_SIZE)
			assert(0);
		if (nvf->node->dr_info.dr_offset_end > DR_SIZE)
			assert(0);
	}
	nvp_transfer_to_free_dr_pool(nvf->node);
 out:
	return;
}

/* FIXME: untested */
static inline void fsync_flush_on_fsync(struct NVFile* nvf, int cpuid, int close, int fdsync)
{
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED


	DEBUG_FILE("%s: Locking node\n", __func__);
	NVP_LOCK_NODE_WR(nvf);		
	DEBUG_FILE("%s: Locking tbl_app\n", __func__);
	if (tbl_app != NULL) {
		TBL_ENTRY_LOCK_WR(tbl_app);
	}
	DEBUG_FILE("%s: Locking tbl_over\n", __func__);	
	if (tbl_over != NULL)  {
		TBL_ENTRY_LOCK_WR(tbl_over);
	}
	DEBUG_FILE("%s: Calling copy_appends_to_file\n", __func__);
	copy_appends_to_file(nvf, close, fdsync);
	DEBUG_FILE("%s: RETURNING\n", __func__);
	if (tbl_over != NULL)  {
		TBL_ENTRY_UNLOCK_WR(tbl_over);
	}
	TBL_ENTRY_UNLOCK_WR(tbl_app);	
	NVP_UNLOCK_NODE_WR(nvf);
}

void *mmap_fsync_uncacheable_map(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
	void* result = MMAP( start, length, prot, flags, fd, offset );
	// mark the result as uncacheable // TODO
	// not_implemented++;

	return result;
}

void *memcpy_fsync_nontemporal_writes(void *dest, const void *src, size_t n)
{
	// TODO: an asm version of memcpy, with movdqa replaced by movntdq
//	void* result; = FSYNC_MEMCPY( not_implemented );

	_mm_mfence();

	return NULL;
//	return result;
}

void *memcpy_fsync_flush_on_write(void *dest, const void *src, size_t n)
{
	// first, perform the memcpy as usual
	void* result = MEMCPY(dest, src, n);

	// then, flush all the pages which were just modified.
	_mm_mfence();
	do_cflushopt_len(dest, n);
	_mm_mfence();

	return result;
}


/* ========================== Internal methods =========================== */

//void nvp_free_btree(unsigned long *root, struct merkleBtreeNode **merkle_root, unsigned long height, unsigned long *dirty_cache, int root_dirty_num, int total_dirty_mmaps);

void nvp_free_btree(unsigned long *root, unsigned long *merkle_root, unsigned long height, unsigned long *dirty_cache, int root_dirty_num, int total_dirty_mmaps);
void nvp_free_dr_mmaps();
void nvp_cleanup_node(struct NVNode *node, int free_root, int unmap_btree);

void nvp_cleanup(void)
{
	int i, j;

#if BG_CLOSING
	while(!waiting_for_signal)
		sleep(1);
	
	//cancel thread
	cancelBgThread();	
	exit_bgthread = 1;
	cleanup = 1;
	bgCloseFiles(1);
#endif

#if BG_CLEANING
	while (!waiting_for_cleaning_signal)
		sleep(1);

	//cancel thread
	cancelBgCleaningThread();
	exit_bg_cleaning_thread = 1;
#endif
	
	nvp_free_dr_mmaps();
	free(_nvp_fd_lookup);

	for (i = 0; i < NUM_NODE_LISTS; i++) {
		pthread_spin_lock(&node_lookup_lock[i]);
       
		for (j = 0; j< OPEN_MAX; j++) {		
			nvp_cleanup_node(&_nvp_node_lookup[i][j], 1, 1); 
		}
	
		pthread_spin_unlock(&node_lookup_lock[i]);
	
		free(_nvp_node_lookup[i]);
	}

	for (i = 0; i < OPEN_MAX; i++) {
		nvp_free_btree(_nvp_ino_mapping[i].root,
			       _nvp_ino_mapping[i].merkle_root,
			       _nvp_ino_mapping[i].height,
			       _nvp_ino_mapping[i].root_dirty_cache,
			       _nvp_ino_mapping[i].root_dirty_num,
			       _nvp_ino_mapping[i].total_dirty_mmaps);
	}	
	free(_nvp_ino_mapping);

	DEBUG_FILE("%s: CLEANUP FINISHED\n", __func__);
	MSG("%s: Done Cleaning up\n", __func__);
	exit(0);
}

void nvp_exit_handler(void)
{
	MSG("exit handler\n");
	MSG("Exit: print stats\n");
	//nvp_print_time_stats();
	nvp_print_io_stats();
	PRINT_TIME();
	
	MSG("calling cleanup\n");
	DEBUG_FILE("%s: CLEANUP STARTED\n", __func__);
	nvp_cleanup();
}

void _nvp_SIGUSR1_handler(int sig)
{
	MSG("SIGUSR1: print stats\n");
	//nvp_print_time_stats();
	nvp_print_io_stats();
	MSG("%s: BG THREAD CALLED %d TIMES\n", __func__, calledBgThread);

	PRINT_TIME();
}

void _nvp_SIGBUS_handler(int sig)
{
	ERROR("We got a SIGBUS (sig %i)! "
		"This almost certainly means someone tried to access an area "
		"inside an mmaped region but past the length of the mmapped "
		"file.\n", sig);
	MSG("%s: sigbus got\n", __func__);
	fflush(NULL);
	
	assert(0);
}

void _nvp_SHM_COPY() {

        int exec_ledger_fd = -1;
	int i,j;
	unsigned long offset_in_map = 0;
	int pid = getpid();
	char exec_nvp_filename[BUF_SIZE];

	sprintf(exec_nvp_filename, "exec-ledger-%d", pid);
	exec_ledger_fd = shm_open(exec_nvp_filename, O_RDONLY, 0666);

	if (exec_ledger_fd == -1) {
		printf("%s: shm_open failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}
	
	char *shm_area = mmap(NULL, 10*1024*1024, PROT_READ, MAP_SHARED, exec_ledger_fd, 0);
	if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(_nvp_fd_lookup, shm_area + offset_in_map, 1024 * sizeof(struct NVFile)) == NULL) {
		printf("%s: memcpy of fd lookup failed. Err = %s\n", __func__, strerror(errno));		
		assert(0);
	}
	
	offset_in_map += (1024 * sizeof(struct NVFile));

	if (memcpy(execve_fd_passing, shm_area + offset_in_map, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of offset passing failed. Err = %s\n", __func__, strerror(errno));
	}

	offset_in_map += (1024 * sizeof(int));

	for (i = 0; i < 1024; i++) {
		_nvp_fd_lookup[i].offset = (size_t*)calloc(1, sizeof(int));
		*(_nvp_fd_lookup[i].offset) = execve_fd_passing[i];
	}

	if (memcpy(_nvp_node_lookup[0], shm_area + offset_in_map, 1024*sizeof(struct NVNode)) == NULL) {
		printf("%s: memcpy of node lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	for (i = 0; i < 1024; i++) {
		_nvp_fd_lookup[i].node = NULL;
		_nvp_node_lookup[0][i].root_dirty_num = 0;
		_nvp_node_lookup[0][i].total_dirty_mmaps = 0;
		_nvp_node_lookup[0][i].isRootSet = 0;
		_nvp_node_lookup[0][i].height = 0;
		_nvp_node_lookup[0][i].root_dirty_num = 0;
		
		_nvp_node_lookup[0][i].root = _nvp_backup_roots[0][i].root;
		_nvp_node_lookup[0][i].merkle_root = _nvp_backup_roots[0][i].merkle_root;
	}

	offset_in_map += (1024*sizeof(struct NVNode));
	
	for (i = 0; i < 1024; i++) {
		if (_nvp_fd_lookup[i].fd != -1) {
			for (j = 0; j < 1024; j++) {
				if (_nvp_fd_lookup[i].serialno == _nvp_node_lookup[0][j].serialno) {
					_nvp_fd_lookup[i].node = &_nvp_node_lookup[0][j];
					break;
				}			
			}
		}
	}
	
	if (memcpy(_nvp_ino_lookup, shm_area + offset_in_map, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of ino lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));

	if (memcpy(_nvp_free_node_list[0], shm_area + offset_in_map, 1024*sizeof(struct StackNode)) == NULL) {
		printf("%s: memcpy of free node list failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	munmap(shm_area, 10*1024*1024);
	shm_unlink(exec_nvp_filename);
}

void _mm_cache_flush(void const* p) {
  asm volatile("clflush %0" : "+m" (*(volatile char *)(p)));
}

void _mm_cache_flush_optimised(void const* p) {
  asm volatile("clflushopt %0" : "+m" (*(volatile char *)(p)));
}

// Figure out if CLFLUSHOPT is supported 
int is_clflushopt_supported() {
	unsigned int eax, ebx, ecx, edx;
	__cpuid_count(7, 0, eax, ebx, ecx, edx);
	return ebx & bit_CLFLUSHOPT;
}

void _nvp_init2(void)
{
	int i, j;
	struct InodeToMapping *tempMapping;

	assert(!posix_memalign(((void**)&_nvp_zbuf), 4096, 4096));

	/*
	 Based on availability of CLFLUSHOPT instruction, point _mm_flush to the 
	 appropriate function
	*/
	if(is_clflushopt_supported()) {
		MSG("CLFLUSHOPT is supported!\n");
		_mm_flush = _mm_cache_flush_optimised;
	} else { 
		MSG("CLFLUSHOPT is not supported! Using CLFLUSH \n");
		_mm_flush = _mm_cache_flush;
	}

#if WORKLOAD_TAR | WORKLOAD_GIT | WORKLOAD_RSYNC
	ASYNC_CLOSING = 0;
#else
	ASYNC_CLOSING = 1;
#endif // WORKLOAD_TAR

	/* 
	 * Allocating and Initializing NVFiles. Total number of NVFiles = 1024. 
	 * _nvp_fd_lookup is an array of struct NVFile 
	*/
	_nvp_fd_lookup = (struct NVFile*)calloc(OPEN_MAX,
						sizeof(struct NVFile));	 	
	if (!_nvp_fd_lookup)
		assert(0);
	// Initializing the valid bits and locks of each NVFile
	for(i = 0; i < OPEN_MAX; i++) {
		_nvp_fd_lookup[i].valid = 0;
		NVP_LOCK_INIT(_nvp_fd_lookup[i].lock);
	}
	/* Initializing the closed file descriptor array */
	_nvp_closed_files = (struct ClosedFiles*)calloc(TOTAL_CLOSED_INODES, sizeof(struct ClosedFiles));
	for(i = 0; i < TOTAL_CLOSED_INODES; i++) {
		_nvp_closed_files[i].fd = -1;
		_nvp_closed_files[i].serialno = 0;
		_nvp_closed_files[i].index_in_free_list = -1;
		_nvp_closed_files[i].next_closed_file = -1;
		_nvp_closed_files[i].prev_closed_file = -1;
		NVP_LOCK_INIT(_nvp_closed_files[i].lock);
	}
	if(!_nvp_closed_files)
		assert(0);
	
	/* Initialize and allocate hash table for closed file descriptor array */
	inode_to_closed_file = (struct InodeClosedFile *)calloc(OPEN_MAX, sizeof(struct InodeClosedFile));
	for(i = 0; i < OPEN_MAX; i++) {
		inode_to_closed_file[i].index = -1;
		NVP_LOCK_INIT(inode_to_closed_file[i].lock);
	}
	if(!inode_to_closed_file)
		assert(0);

	lru_head = -1;
	lru_tail = -1;
	lru_tail_serialno = 0;	
	
	/* 
	   Allocate and initialize the free list for nodes
	*/
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		_nvp_free_node_list[i] = (struct StackNode*)calloc(OPEN_MAX,
								   sizeof(struct StackNode));
		for(j = 0; j < OPEN_MAX; j++) {
			_nvp_free_node_list[i][j].free_bit = 1;
			_nvp_free_node_list[i][j].next_free_idx = j+1;
		}
		_nvp_free_node_list[i][OPEN_MAX - 1].next_free_idx = -1;
	}	

	_nvp_free_lru_list = (struct StackNode*)calloc(OPEN_MAX,
						       sizeof(struct StackNode));
	for(i = 0; i < OPEN_MAX; i++) {
		_nvp_free_lru_list[i].free_bit = 1;
		_nvp_free_lru_list[i].next_free_idx = i+1;
	}
       	_nvp_free_lru_list[OPEN_MAX - 1].next_free_idx = -1;
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		if (!_nvp_free_node_list[i])
			assert(0);
	}	
	if(!_nvp_free_lru_list)
		assert(0);
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		_nvp_free_node_list_head[i] = 0;
	}
	_nvp_free_lru_list_head = 0;	
	/* 
	   Allocating and Initializing mmap cache. Can hold mmaps, merkle trees and dirty mmap caches belonging to 1024 files. _nvp_ino_mapping is an array of struct InodeToMapping 
	*/	
	_nvp_ino_mapping = (struct InodeToMapping*)calloc(OPEN_MAX, sizeof(struct InodeToMapping));
	memset((void *)_nvp_ino_mapping, 0, OPEN_MAX * sizeof(struct InodeToMapping));		
	if (!_nvp_ino_mapping)
		assert(0);
	for(i=0; i<OPEN_MAX; i++) {
		tempMapping = &_nvp_ino_mapping[i];
		// Allocating region to store mmap() addresses
		tempMapping->root = malloc(1024 * sizeof(unsigned long));
		memset((void *)tempMapping->root, 0, 1024 * sizeof(unsigned long));

		tempMapping->merkle_root = malloc(1024 * sizeof(unsigned long));
		memset((void *)tempMapping->merkle_root, 0, 1024 * sizeof(unsigned long));		

		// Allocating region to store dirty mmap caches
		tempMapping->root_dirty_cache = malloc(20 * sizeof(unsigned long));
		memset((void *)tempMapping->root_dirty_cache, 0, 20 * sizeof(unsigned long));

		tempMapping->root_dirty_num = 0;
		tempMapping->total_dirty_mmaps = 0;		

		// Initializing the inode numbers = keys to 0
		_nvp_ino_mapping[i].serialno = 0;				
	}
	/*
	 * Allocating and Initializing NVNode. Number of NVNodes = 1024. 
	 * _nvp_node_lookup is an array of struct NVNode 
	*/
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		_nvp_node_lookup[i] = (struct NVNode*)calloc(OPEN_MAX,
							  sizeof(struct NVNode));
		if (!_nvp_node_lookup[i])
			assert(0);

		_nvp_backup_roots[i] = (struct backupRoots*)calloc(OPEN_MAX,
								   sizeof(struct backupRoots));
		if (!_nvp_backup_roots[i])
			assert(0);

		
		memset((void *)_nvp_node_lookup[i], 0, OPEN_MAX * sizeof(struct NVNode));	
		// Allocating and initializing all the dynamic structs inside struct NVNode 
		for(j = 0; j < OPEN_MAX; j++) {
			// Initializing lock associated with NVNode
			NVP_LOCK_INIT(_nvp_node_lookup[i][j].lock);

			// Allocating and Initializing mmap() roots associated with NVNode 
			_nvp_node_lookup[i][j].root = malloc(1024 * sizeof(unsigned long));
			memset((void *)_nvp_node_lookup[i][j].root, 0, 1024 * sizeof(unsigned long));

			// Allocating and Initializing merkle tree roots associated with NVNode 
			_nvp_node_lookup[i][j].merkle_root = malloc(1024 * sizeof(unsigned long));			
			memset((void *)_nvp_node_lookup[i][j].merkle_root, 0, 1024 * sizeof(unsigned long));
			
			// Allocating and Initializing the dirty mmap cache associated with NVNode
			_nvp_node_lookup[i][j].root_dirty_cache = malloc(20 * sizeof(unsigned long));
			memset((void *)_nvp_node_lookup[i][j].root_dirty_cache, 0, 20 * sizeof(unsigned long));

			_nvp_node_lookup[i][j].root_dirty_num = 0;
			_nvp_node_lookup[i][j].total_dirty_mmaps = 0;

			// Allocating and Initializing DR root of the node
			memset((void *)&_nvp_node_lookup[i][j].dr_info, 0, sizeof(struct free_dr_pool));

			_nvp_backup_roots[i][j].root = _nvp_node_lookup[i][j].root;
			_nvp_backup_roots[i][j].merkle_root = _nvp_node_lookup[i][j].merkle_root;
			_nvp_backup_roots[i][j].root_dirty_cache = _nvp_node_lookup[i][j].root_dirty_cache;
			
		}
	}

	/*
	  Allocating and Initializing the free pool of DR mmap()s. Total number of mmap()s allowed = 1024.
	*/
	lfds711_queue_umm_init_valid_on_current_logical_core( &qs, &qe_dummy, NULL );

#if DATA_JOURNALING_ENABLED
	
	lfds711_queue_umm_init_valid_on_current_logical_core( &qs_over, &qe_dummy_over, NULL );	

#endif
	
	MMAP_PAGE_SIZE = getpagesize();
	MMAP_HUGEPAGE_SIZE = 2097152;

#if !POSIX_ENABLED
	init_logs();
#endif
	
	struct free_dr_pool *free_pool_mmaps;
	char prefault_buf[MMAP_PAGE_SIZE];
	char dr_fname[256];
	int dr_fd, ret;
	struct stat stat_buf;
	int max_perms = PROT_READ | PROT_WRITE;
	int num_dr_blocks = DR_SIZE / MMAP_PAGE_SIZE;
	free_pool_mmaps = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool)*INIT_NUM_DR);
	for (i = 0; i < MMAP_PAGE_SIZE; i++)
		prefault_buf[i] = '0';
	
	for (i = 0; i < INIT_NUM_DR; i++) {
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-XXXXXX");		
		dr_fd = _hub_find_fileop("posix")->OPEN(mktemp(dr_fname), O_RDWR | O_CREAT, 0666);		
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		ret = posix_fallocate(dr_fd, 0, DR_SIZE);		
		if (ret < 0) {
			MSG("%s: posix_fallocate failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		num_mmap++;
		num_drs++;
		free_pool_mmaps[i].start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_SIZE,
			 max_perms, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );
		fstat(dr_fd, &stat_buf);
		free_pool_mmaps[i].dr_serialno = stat_buf.st_ino;
		free_pool_mmaps[i].dr_fd = dr_fd;
	        free_pool_mmaps[i].valid_offset = 0;
	        free_pool_mmaps[i].dr_offset_start = DR_SIZE;
		free_pool_mmaps[i].dr_offset_end = free_pool_mmaps[i].valid_offset;

		for (j = 0; j < num_dr_blocks; j++) {
#if NON_TEMPORAL_WRITES

			if(MEMCPY_NON_TEMPORAL((char *)free_pool_mmaps[i].start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}
			
#else

			if(FSYNC_MEMCPY((char *)free_pool_mmaps[i].start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}

#endif // NON_TEMPORAL_WRITES

#if NVM_DELAY

			perfmodel_add_delay(0, MMAP_PAGE_SIZE);

#endif //NVM_DELAY
			
		}
			
		LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(free_pool_mmaps[i].qe,
						       &free_pool_mmaps[i] );
		
		lfds711_queue_umm_enqueue( &qs, &free_pool_mmaps[i].qe );
		MSG("%s: dr fd = %d, start addr = %p\n", __func__, dr_fd,
			   free_pool_mmaps[i].start_addr);
		dr_fname[0] = '\0';
		num_drs_left++;
	}

#if DATA_JOURNALING_ENABLED
	
	int num_dr_over_blocks = DR_OVER_SIZE / MMAP_PAGE_SIZE;
	free_pool_mmaps = NULL;
	free_pool_mmaps = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool)*INIT_NUM_DR_OVER);
	for (i = 0; i < MMAP_PAGE_SIZE; i++)
		prefault_buf[i] = '0';
	
	for (i = 0; i < INIT_NUM_DR_OVER; i++) {
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-OVER-XXXXXX");		
		dr_fd = _hub_find_fileop("posix")->OPEN(mktemp(dr_fname), O_RDWR | O_CREAT, 0666);		
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		ret = posix_fallocate(dr_fd, 0, DR_OVER_SIZE);		
		if (ret < 0) {
			MSG("%s: posix_fallocate failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		num_mmap++;
		num_drs++;
		free_pool_mmaps[i].start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_OVER_SIZE,
			 max_perms, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );
		fstat(dr_fd, &stat_buf);
		free_pool_mmaps[i].dr_serialno = stat_buf.st_ino;
		free_pool_mmaps[i].dr_fd = dr_fd;
	        free_pool_mmaps[i].valid_offset = 0;
	        free_pool_mmaps[i].dr_offset_start = free_pool_mmaps[i].valid_offset;
		free_pool_mmaps[i].dr_offset_end = DR_OVER_SIZE;

		for (j = 0; j < num_dr_over_blocks; j++) {

#if NON_TEMPORAL_WRITES

			if(MEMCPY_NON_TEMPORAL((char *)free_pool_mmaps[i].start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}

#else // NON_TEMPORAL_WRITES

			if(FSYNC_MEMCPY((char *)free_pool_mmaps[i].start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}

#endif // NON_TEMPORAL_WRITES

#if NVM_DELAY

			perfmodel_add_delay(0, MMAP_PAGE_SIZE);

#endif //NVM_DELAY

		}
			
		LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(free_pool_mmaps[i].qe,
						       &free_pool_mmaps[i] );
		
		lfds711_queue_umm_enqueue( &qs_over, &free_pool_mmaps[i].qe );
		MSG("%s: dr fd = %d, start addr = %p\n", __func__, dr_fd,
		    free_pool_mmaps[i].start_addr);
		dr_fname[0] = '\0';
		num_drs_left++;
	}

	// Creating array of full DRs to dispose at process end time.
	_nvp_full_drs = (struct full_dr *) malloc(1024*sizeof(struct full_dr));
	memset((void *) _nvp_full_drs, 0, 1024*sizeof(struct full_dr));
	full_dr_idx = 0;
	
	_nvp_tbl_regions = (struct NVLarge_maps *) malloc(LARGE_TBL_MAX*sizeof(struct NVLarge_maps));
	memset((void *) _nvp_tbl_regions, 0, LARGE_TBL_MAX*sizeof(struct NVLarge_maps));
	for (i = 0; i < LARGE_TBL_MAX; i++) {
       		_nvp_tbl_regions[i].regions = (struct NVTable_regions *) malloc(LARGE_TBL_REGIONS*sizeof(struct NVTable_regions));
		memset((void *) _nvp_tbl_regions[i].regions, 0, LARGE_TBL_REGIONS*sizeof(struct NVTable_regions));
		for (j = 0; j < LARGE_TBL_REGIONS; j++) {
			_nvp_tbl_regions[i].regions[j].tbl_mmaps = (struct table_mmaps *) malloc(PER_REGION_TABLES*sizeof(struct table_mmaps));
			_nvp_tbl_regions[i].regions[j].lowest_off = (REGION_COVERAGE)*(j + 1);
			_nvp_tbl_regions[i].regions[j].highest_off = 0;
			memset((void *) _nvp_tbl_regions[i].regions[j].tbl_mmaps, 0, PER_REGION_TABLES*sizeof(struct table_mmaps));			
		}
		_nvp_tbl_regions[i].min_dirty_region = LARGE_TBL_REGIONS;
		_nvp_tbl_regions[i].max_dirty_region = 0;
	}

	MSG("%s: Large regions set\n", __func__);
	
	_nvp_over_tbl_mmaps = (struct NVTable_maps *) malloc(OVER_TBL_MAX*sizeof(struct NVTable_maps));
	for (i = 0; i < OVER_TBL_MAX; i++) {
		_nvp_over_tbl_mmaps[i].tbl_mmaps = (struct table_mmaps *) malloc(NUM_OVER_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		memset((void *)_nvp_over_tbl_mmaps[i].tbl_mmaps, 0, NUM_OVER_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		_nvp_over_tbl_mmaps[i].tbl_mmap_index = 0;
		NVP_LOCK_INIT(_nvp_over_tbl_mmaps[i].lock);
	}

	MSG("%s: Tbl over mmaps set\n", __func__);
	
#endif	// DATA_JOURNALING_ENABLED
	
	_nvp_tbl_mmaps = (struct NVTable_maps *) malloc(APPEND_TBL_MAX*sizeof(struct NVTable_maps));
	for (i = 0; i < APPEND_TBL_MAX; i++) {
		_nvp_tbl_mmaps[i].tbl_mmaps = (struct table_mmaps *) malloc(NUM_APP_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		memset((void *)_nvp_tbl_mmaps[i].tbl_mmaps, 0, NUM_APP_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		_nvp_tbl_mmaps[i].tbl_mmap_index = 0;
		NVP_LOCK_INIT(_nvp_tbl_mmaps[i].lock);
	}

	MSG("%s: Tbl mmaps set\n", __func__);

	// Initializing global lock for accessing NVNode
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		pthread_spin_init(&node_lookup_lock[i], PTHREAD_PROCESS_SHARED);
	}
	pthread_spin_init(&global_lock, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&global_lock_closed_files, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&global_lock_lru_head, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&stack_lock, PTHREAD_PROCESS_SHARED);	

	MSG("%s: Global locks created\n", __func__);

	SANITYCHECK(MMAP_PAGE_SIZE > 100);
	INITIALIZE_TIMERS();
	/*
	  Setting up variables and initialization for background thread
	*/
	cleanup = 0;

	waiting_for_signal = 0;
	started_bgthread = 0;
	exit_bgthread = 0;
	waiting_for_cleaning_signal = 0;
	started_bg_cleaning_thread = 0;
	exit_bg_cleaning_thread = 0;

	lim_num_files = 100;
	lim_dr_mem = (5ULL) * 1024 * 1024 * 1024;
	lim_dr_mem_closed = 500 * 1024 * 1024;
	run_background_thread = 0;
	initEnvForBg();
	initEnvForBgClean();
	MSG("%s: initialized environment, OPEN_MAX = %d\n", __func__, OPEN_MAX);
        dr_mem_allocated = 0;
	dr_mem_closed_files = 0;
#if BG_CLOSING	
	calledBgThread = 0;
	startBgThread();
#endif
#if BG_CLEANING
	calledBgCleaningThread = 0;
	startBgCleaningThread();
#endif	
	/*
	 * Setting up signal handlers: SIGBUS and SIGUSR 
	 */
	DEBUG("Installing signal handler.\n");
	signal(SIGBUS, _nvp_SIGBUS_handler);
	/* For filebench */
	signal(SIGUSR1, _nvp_SIGUSR1_handler);
	/*
	  Setting up the exit handler to print stats 
	*/
	atexit(nvp_exit_handler);

	int pid = getpid();
	char exec_nvp_filename[BUF_SIZE];

	sprintf(exec_nvp_filename, "/dev/shm/exec-ledger-%d", pid);
	if (access(exec_nvp_filename, F_OK ) != -1)
		execv_done = 1;
	else
		execv_done = 0;

}

void nvp_transfer_to_free_dr_pool(struct NVNode *node)
{
	int i, num_free_dr_mmaps;
	struct free_dr_pool *free_pool_of_dr_mmap;
	unsigned long offset_in_page = 0;

#if DATA_JOURNALING_ENABLED
	
	if(node->dr_over_info.start_addr != 0) {
		free_pool_of_dr_mmap = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool));
		free_pool_of_dr_mmap->dr_offset_start = node->dr_over_info.dr_offset_start;
		free_pool_of_dr_mmap->dr_offset_end = DR_OVER_SIZE;
		free_pool_of_dr_mmap->start_addr = node->dr_over_info.start_addr;
		free_pool_of_dr_mmap->dr_fd = node->dr_over_info.dr_fd;
		free_pool_of_dr_mmap->dr_serialno = node->dr_over_info.dr_serialno;
		free_pool_of_dr_mmap->valid_offset = node->dr_over_info.valid_offset;

		LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(free_pool_of_dr_mmap->qe, free_pool_of_dr_mmap);
		lfds711_queue_umm_enqueue( &qs_over, &(free_pool_of_dr_mmap->qe) );

		memset((void *)&node->dr_over_info, 0, sizeof(struct free_dr_pool));
		__atomic_fetch_sub(&dr_mem_allocated, DR_OVER_SIZE, __ATOMIC_SEQ_CST);
	}
	
#endif // DATA_JOURNALING_ENABLED

	if(node->dr_info.start_addr != 0) {
		DEBUG_FILE("%s: Setting offset_start to DR_SIZE. DR_FD = %d\n",
			   __func__, node->dr_info.dr_fd);
		free_pool_of_dr_mmap = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool));
		offset_in_page = node->dr_info.valid_offset % MMAP_PAGE_SIZE;
		if (offset_in_page != 0)
			node->dr_info.valid_offset -= offset_in_page;
		free_pool_of_dr_mmap->dr_offset_start = DR_SIZE;
		free_pool_of_dr_mmap->dr_offset_end = node->dr_info.valid_offset;
		free_pool_of_dr_mmap->start_addr = node->dr_info.start_addr;
		free_pool_of_dr_mmap->dr_fd = node->dr_info.dr_fd;
		free_pool_of_dr_mmap->dr_serialno = node->dr_info.dr_serialno;
		free_pool_of_dr_mmap->valid_offset = node->dr_info.valid_offset;

		LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(free_pool_of_dr_mmap->qe, free_pool_of_dr_mmap);
		lfds711_queue_umm_enqueue( &qs, &(free_pool_of_dr_mmap->qe) );

		memset((void *)&node->dr_info, 0, sizeof(struct free_dr_pool));
		__atomic_fetch_sub(&dr_mem_allocated, DR_SIZE, __ATOMIC_SEQ_CST);
	}
}

void nvp_free_dr_mmaps()
{
	unsigned long addr;
	unsigned long offset_in_page = 0;
	struct free_dr_pool *temp_free_pool_of_dr_mmaps;
	int i = 0;
	
	while( lfds711_queue_umm_dequeue(&qs, &qe) ) {
		temp_free_pool_of_dr_mmaps = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe );
		addr = temp_free_pool_of_dr_mmaps->start_addr;
		munmap((void *)addr, DR_SIZE);

		// Fetch the name of the file before closing it.
		char fd_str[256];
		char new_path[256];
		sprintf(fd_str, "/proc/self/fd/%d", temp_free_pool_of_dr_mmaps->dr_fd);
		if (readlink(fd_str, new_path, sizeof(new_path)) == -1)
			assert(0);

		close(temp_free_pool_of_dr_mmaps->dr_fd);

		// Remove the file.
		_nvp_fileops->UNLINK(new_path);
		__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);
	}
	lfds711_queue_umm_cleanup( &qs, NULL );

#if DATA_JOURNALING_ENABLED
	
	while( lfds711_queue_umm_dequeue(&qs_over, &qe_over) ) {
		temp_free_pool_of_dr_mmaps = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe_over );
		addr = temp_free_pool_of_dr_mmaps->start_addr;
		munmap((void *)addr, DR_OVER_SIZE);

		// Fetch the name of the file before closing it.
		char fd_str[256];
		char new_path[256];
		sprintf(fd_str, "/proc/self/fd/%d", temp_free_pool_of_dr_mmaps->dr_fd);
		if (readlink(fd_str, new_path, sizeof(new_path)) == -1)
			assert(0);

		close(temp_free_pool_of_dr_mmaps->dr_fd);

		// Remove the file.
		_nvp_fileops->UNLINK(new_path);
		__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);
	}
	lfds711_queue_umm_cleanup( &qs_over, NULL );

	for (i = 0; i < full_dr_idx; i++) {
		addr = _nvp_full_drs[i].start_addr;
		munmap((void *)addr, _nvp_full_drs[i].size);
		close(_nvp_full_drs[i].dr_fd);
	}

#endif // DATA_JOURNALING_ENABLED
	
}

void nvp_free_btree(unsigned long *root, unsigned long *merkle_root, unsigned long height, unsigned long *dirty_cache, int root_dirty_num, int total_dirty_mmaps)
{
	int i, dirty_index;
	dirty_index = 0;
	if (height == 0) {
		for(i = 0; i < root_dirty_num; i++) {
			dirty_index = dirty_cache[i];
			if(root && root[dirty_index]) {
				munmap((void *)root[dirty_index], MAX_MMAP_SIZE);
				root[dirty_index] = 0;
				merkle_root[dirty_index] = 0;
			}
		}
		root_dirty_num = 0;
		if(total_dirty_mmaps) {
			for (i = 0; i < 1024; i++) {
				if (root && root[i]) {
					DEBUG("munmap: %d, addr 0x%lx\n",
					      i, root[i]);
					munmap((void *)root[i], MAX_MMAP_SIZE);
					root[i] = 0;
					merkle_root[i] = 0;
				}
			}
		}
		return;
	}
	for (i = 0; i < 1024; i++) {
		if (root[i] && merkle_root[i]) {
			nvp_free_btree((unsigned long *)root[i], (unsigned long *)merkle_root[i],
				       height - 1, NULL, 0, 1);
			root[i] = 0;
			merkle_root[i] = 0;
		}
	}
	free(root);
	free(merkle_root);
}


void nvp_add_to_inode_mapping(struct NVNode *node, ino_t serialno)
{
	struct InodeToMapping *mappingToBeAdded;
	
	int index = serialno % 1024;
	int i, dirty_index;

	if (serialno == 0)
		return;

	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);
	mappingToBeAdded = &_nvp_ino_mapping[index];
	if(mappingToBeAdded->serialno != 0 && mappingToBeAdded->serialno != serialno) {
		// Replacing some mmap() in that global mmap() cache. So must munmap() all the mmap() ranges in that cache. 
		nvp_free_btree(mappingToBeAdded->root, mappingToBeAdded->merkle_root, mappingToBeAdded->height, mappingToBeAdded->root_dirty_cache, mappingToBeAdded->root_dirty_num, mappingToBeAdded->total_dirty_mmaps);		

		mappingToBeAdded->serialno = 0;
	}

	// Check if many mmap()s need to be copied. If total_dirty_mmaps is set, that means all the mmap()s need to be copied. 
	if(node->total_dirty_mmaps) {
		memcpy(mappingToBeAdded->root, node->root, 1024 * sizeof(unsigned long));
		memcpy(mappingToBeAdded->merkle_root, node->merkle_root, 1024 * sizeof(unsigned long));
		
	} else {
		// Only copy the dirty mmaps. The indexes can be found in the root_dirty_cache. 
		for(i = 0; i < node->root_dirty_num; i++) {
			dirty_index = node->root_dirty_cache[i];
			if(node->root && node->root[dirty_index])
				mappingToBeAdded->root[dirty_index] = node->root[dirty_index];

			if(node->merkle_root && node->merkle_root[dirty_index])
				mappingToBeAdded->merkle_root[dirty_index] = node->merkle_root[dirty_index];
		}
	}

	mappingToBeAdded->serialno = serialno;
	
	if(node->root_dirty_num)
		memcpy(mappingToBeAdded->root_dirty_cache, node->root_dirty_cache, 20 * sizeof(unsigned long));

	mappingToBeAdded->root_dirty_num = node->root_dirty_num;
	mappingToBeAdded->total_dirty_mmaps = node->total_dirty_mmaps;
	mappingToBeAdded->height = node->height;      	
}

/* 
 * This function is responsible for copying all the mapping from the global mmap() cache on to the mmap tree of the node. 
 */
int nvp_retrieve_inode_mapping(struct NVNode *node)
{

	struct InodeToMapping *mappingToBeRetrieved;
	int index = node->serialno % 1024;
	int dirty_index, i;
	
	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);

	/* 
	 * Get the mapping from the global mmap() cache, based on the inode number of the node whose mapping it should
         * be retrieved from. 
	 */
	mappingToBeRetrieved = &_nvp_ino_mapping[index];
	
	if(mappingToBeRetrieved->serialno == node->serialno) {

		/* 
		 * Copy the file backed mmap()s and the merkle roots. total_dirty_mmaps suggests that there are more than
		 * 20 mmaps that need to be copied.
		 */
		if(mappingToBeRetrieved->total_dirty_mmaps) {
			memcpy(node->root, mappingToBeRetrieved->root, 1024 * sizeof(unsigned long));
			memcpy(node->merkle_root, mappingToBeRetrieved->merkle_root, 1024 * sizeof(unsigned long));
			
		} else {
	
			for(i = 0; i < mappingToBeRetrieved->root_dirty_num; i++) {
				dirty_index = mappingToBeRetrieved->root_dirty_cache[i];
				if(mappingToBeRetrieved->root && mappingToBeRetrieved->root[dirty_index])
					node->root[dirty_index] = mappingToBeRetrieved->root[dirty_index];

				if(mappingToBeRetrieved->merkle_root && mappingToBeRetrieved->merkle_root[dirty_index])
					node->merkle_root[dirty_index] = mappingToBeRetrieved->merkle_root[dirty_index];
			}
		}
		
		// Copy the root_dirty_cache from the global mmap() cache on to the node mmap() cache
		//if(mappingToBeRetrieved->root_dirty_num)
		memcpy(node->root_dirty_cache, mappingToBeRetrieved->root_dirty_cache, 20 * sizeof(unsigned long));
		
		node->root_dirty_num = mappingToBeRetrieved->root_dirty_num;
		node->total_dirty_mmaps = mappingToBeRetrieved->total_dirty_mmaps;
		node->height = mappingToBeRetrieved->height;      	

		//printf("%s: end: node->root[0] = %lu, mapping root = %lu, mapping root dirty num = %d, node->serialno = %lu, index = %d, node reference = %d, thread_id = %lu\n", __func__, node->root[0], mappingToBeRetrieved->root[0], mappingToBeRetrieved->root_dirty_num, node->serialno, index, node->reference, pthread_self());

		goto out;
	}

	return -1;
 out:
	return 0;
}

void nvp_reset_mappings(struct NVNode *node)
{
	int i, dirty_index;
	
	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);

	if(node->root_dirty_num) {		
		// Check if many mmap()s need to be memset. If total_dirty_mmaps is set, that means all the mmap()s need to be copied 
		if(node->total_dirty_mmaps) {
			memset((void *)node->root, 0, 1024 * sizeof(unsigned long));		
			memset((void *)node->merkle_root, 0, 1024 * sizeof(unsigned long));	
		} else {
			// Only copy the dirty mmaps. The indexes can be found in the root_dirty_cache. 
			for(i = 0; i < node->root_dirty_num; i++) {
				dirty_index = node->root_dirty_cache[i];
				if(node->root && node->root[dirty_index]) {
					node->root[dirty_index] = 0;
					node->merkle_root[dirty_index] = 0;
				}
			}
		}
		if(node->root_dirty_num)
			memset((void *)node->root_dirty_cache, 0, 20 * sizeof(unsigned long));	
	}
	node->isRootSet = 0;
	node->height = 0;
	node->total_dirty_mmaps = 0;
	node->root_dirty_num = 0;
}

void nvp_cleanup_node(struct NVNode *node, int free_root, int unmap_btree)
{

	unsigned int height = node->height;
	unsigned long *root = node->root;
	unsigned long *merkle_root = node->merkle_root;
	unsigned long *dirty_cache;
	int total_dirty_mmaps = node->total_dirty_mmaps;
	int root_dirty_num = node->root_dirty_num;
	
	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);

	if(root_dirty_num > 0)
		dirty_cache = node->root_dirty_cache;
	else
		dirty_cache = NULL;
     
	if(unmap_btree && node->root_dirty_num) {
		// munmap() all the file backed mmap()s of this file. 
		nvp_free_btree(root, merkle_root, height, dirty_cache, root_dirty_num, total_dirty_mmaps);
	}
		
	/* 
	 * Deallocate everything related to NVNode. This should be done at the end when Ledger is exiting. 
	 */
	if (free_root && node->root[0]) {
		free(node->root);
		free(node->merkle_root);
		free(node->root_dirty_cache);
		node->root = NULL;
		node->merkle_root = NULL;
		node->root_dirty_cache = NULL;
		return;
	}
	// Copy all the DR mmap()s linked to this node, to the global pool of DR mmap()s
	/*
	 * Resetting the file backed mmap addresses, merkle tree addresses and the dirty file backed mmap cache of this node to 0. 
	 */
	if(!unmap_btree)
		nvp_reset_mappings(node);
}

void nvp_init_dr(struct NVNode *node)
{
	if(node->dr_info.start_addr == 0) {
		memset((void *)&node->dr_info, 0, sizeof(struct free_dr_pool));
	}
}

void nvp_init_node(struct NVNode *node)
{
	int i;
	if (!node->root) {
		node->root = malloc(1024 * sizeof(unsigned long));
		memset((void *)node->root, 0, 1024 * sizeof(unsigned long));
	}
	if(!node->merkle_root) {
		node->merkle_root = malloc(1024 * sizeof(struct merkleBtreeNode *));
		for(i=0; i<1024; i++)
			node->merkle_root[i] = 0;
	}
	if(!node->root_dirty_cache) {
		node->root_dirty_cache = malloc(20 * sizeof(unsigned long));
		memset((void *)node->root_dirty_cache, 0, 20 * sizeof(unsigned long));
	}					
}

struct NVNode * nvp_allocate_node(int list_idx)
{
	struct NVNode *node = NULL;
	int idx_in_list = -1;
	int i, candidate = -1;

	idx_in_list = pop_from_stack(1, 0, list_idx);	
	if(idx_in_list != -1) {
		node = &_nvp_node_lookup[list_idx][idx_in_list];
		node->index_in_free_list = idx_in_list;
		return node;
	}	
	/*
	 * Get the first unused NVNode from the global array of 1024 NVNodes. 
	 * If the node is not unusued but the reference number is
	 * 0, meaning that there is no thread that has this file open, 
	 * it can be used for holding info of the new file
	 */	
	for (i = 0; i < 1024; i++) {
		if (_nvp_node_lookup[list_idx][i].serialno == 0) {
			DEBUG("Allocate unused node %d\n", i);
			_nvp_free_node_list[list_idx][i].free_bit = 0;
			node->index_in_free_list = i;		
			_nvp_free_node_list_head[list_idx] = _nvp_free_node_list[list_idx][node->index_in_free_list].next_free_idx;
			node = &_nvp_node_lookup[list_idx][i];
			break;
		}
		if (candidate == -1 && _nvp_node_lookup[list_idx][i].reference == 0)
			candidate = i;
	}
	if (node) {
		return node;
	}
	if (candidate != -1) {
		node = &_nvp_node_lookup[list_idx][candidate];
		DEBUG("Allocate unreferenced node %d\n", candidate);
		node->index_in_free_list = candidate;		
		_nvp_free_node_list[list_idx][candidate].free_bit = 0;
		_nvp_free_node_list_head[list_idx] = _nvp_free_node_list[list_idx][candidate].next_free_idx;
		return node;
	}
	return NULL;	
}

struct NVNode * nvp_get_node(const char *path, struct stat *file_st, int result)
{
	int i, index, ret;
	struct NVNode *node = NULL;
	int node_list_idx = pthread_self() % NUM_NODE_LISTS;
	instrumentation_type node_lookup_lock_time, nvnode_lock_time;
	
	pthread_spin_lock(&node_lookup_lock[node_list_idx]);
	/* 
	 *  Checking to see if the file is already open by another thread. In this case, the same NVNode can be used by this thread            
	 *  too. But it will have its own separate NVFile, since the fd is unique per thread 
	 */
	index = file_st->st_ino % 1024;
	if (_nvp_ino_lookup[index]) {
		i = _nvp_ino_lookup[index];
		if ( _nvp_fd_lookup[i].node &&
		     _nvp_fd_lookup[i].node->serialno == file_st->st_ino) {
			DEBUG("File %s is (or was) already open in fd %i "
			      "(this fd hasn't been __open'ed yet)! "
			      "Sharing nodes.\n", path, i);
			
			node = _nvp_fd_lookup[i].node;
			SANITYCHECK(node != NULL);			
			NVP_LOCK_WR(node->lock);
			node->reference++;			
			NVP_LOCK_UNLOCK_WR(node->lock);
			
			pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
			goto out;
		}
	}
	/*
	 * This is the first time the file is getting opened. 
	 * The first unused NVNode is assigned here to hold info of the file.  
	 */
	if(node == NULL) {
		DEBUG("File %s is not already open. "
		      "Allocating new NVNode.\n", path);
		node = nvp_allocate_node(node_list_idx);
		NVP_LOCK_WR(node->lock);
		node->serialno = file_st->st_ino;
		node->reference++;
		NVP_LOCK_UNLOCK_WR(node->lock);
		if(UNLIKELY(!node)) {
			MSG("%s: Node is null\n", __func__);
			assert(0);
		}
	}
	index = file_st->st_ino % 1024;
	if (_nvp_ino_lookup[index] == 0)
		_nvp_ino_lookup[index] = result;
	
	node->free_list_idx = node_list_idx;
	
	pthread_spin_unlock(&node_lookup_lock[node_list_idx]);

	NVP_LOCK_WR(node->lock);

	/* 
	 * Checking if the mapping exists in the global mmap() cache for this inode number. 
	 * If it does, copy all the mapping
	 * from the global mmap() cache on to the NVNode mmap()
         */  
	nvp_add_to_inode_mapping(node, node->backup_serialno);
	nvp_reset_mappings(node);	
	ret = nvp_retrieve_inode_mapping(node);	
	if(ret != 0) {
		/* 
		 * If the height is not 0, that means that there exist levels 
		 * in the file backed mmap() tree. So need to free
		 * the file backed mmap() tree completely. 
		 */
		if(node->height != 0) 
			nvp_cleanup_node(node, 0, 1);		
	}
	node->length = file_st->st_size;
	node->maplength = 0;
	node->true_length = node->length;	
	if (node->true_length >= LARGE_FILE_THRESHOLD)
		node->is_large_file = 1;
	else
		node->is_large_file = 0;
	node->dr_mem_used = 0;
	if (node->true_length == 0) {
		clear_tbl_mmap_entry(&_nvp_tbl_mmaps[file_st->st_ino % APPEND_TBL_MAX]);

#if DATA_JOURNALING_ENABLED
		
		clear_tbl_mmap_entry(&_nvp_over_tbl_mmaps[file_st->st_ino % OVER_TBL_MAX]);

#endif // DATA_JOURNALING_ENABLED

	}

	if(node->dr_info.start_addr != 0 || node->dr_over_info.start_addr != 0) {
		DEBUG_FILE("%s: calling transfer to free pool. Inode = %lu\n", __func__, node->serialno);
		nvp_transfer_to_free_dr_pool(node);
	}
	node->async_file_close = 0;
	node->backup_serialno = node->serialno;
	
	NVP_LOCK_UNLOCK_WR(node->lock);
out:
	return node;
}

static unsigned long calculate_capacity(unsigned int height)
{
	unsigned long capacity = MAX_MMAP_SIZE;

	while (height) {
		capacity *= 1024;
		height--;
	}

	return capacity;
}

static unsigned int calculate_new_height(off_t offset)
{
	unsigned int height = 0;
	off_t temp_offset = offset / ((unsigned long)1024 * MAX_MMAP_SIZE);

	while (temp_offset) {
		temp_offset /= 1024;
		height++;
	}

	return height;
}

static int nvp_get_mmap_address(struct NVFile *nvf, off_t offset, size_t count, unsigned long *mmap_addr, unsigned long *bitmap_root, off_t *offset_within_mmap, size_t *extent_length, int wr_lock, int cpuid, struct NVTable_maps *tbl_app, struct NVTable_maps *tbl_over)
{
	int i;
	int index;
	unsigned int height = nvf->node->height;
	unsigned int new_height;
	unsigned long capacity = MAX_MMAP_SIZE;
	unsigned long *root = nvf->node->root;

#if !NON_TEMPORAL_WRITES	
	unsigned long *merkle_root = nvf->node->merkle_root;
	unsigned long merkle_start_addr;
#endif

	unsigned long start_addr;
	off_t start_offset = offset;
	instrumentation_type nvnode_lock_time, file_mmap_time;
	
	DEBUG("Get mmap address: offset 0x%lx, height %u\n", offset, height);
	DEBUG("root @ %p\n", root);

	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;

		DEBUG("index %d\n", index);
#if !NON_TEMPORAL_WRITES	
		if (index >= 1024 || root[index] == 0 || merkle_root[index] == 0) {
#else
		if (index >= 1024 || root[index] == 0) {
#endif
			goto not_found;
		}
		if (height) {
			root = (unsigned long *)root[index];

#if !NON_TEMPORAL_WRITES	
			merkle_root = (unsigned long *)merkle_root[index];
#endif

			DEBUG("%p\n", root);
		} else {
			start_addr = root[index];

#if !NON_TEMPORAL_WRITES
			merkle_start_addr = merkle_root[index];
#endif
			DEBUG("addr 0x%lx\n", start_addr);
		}
		start_offset = start_offset % capacity;
	} while(height--);
	//NVP_END_TIMING(lookup_t, lookup_time);

#if !NON_TEMPORAL_WRITES	
	if (IS_ERR(start_addr) || start_addr == 0 || merkle_start_addr == 0) {
#else
	if (IS_ERR(start_addr) || start_addr == 0) {
#endif
		MSG("ERROR!\n");
		fflush(NULL);
		assert(0);
	}

        (*mmap_addr) = (start_addr + (offset % MAX_MMAP_SIZE));
	*offset_within_mmap = offset % MAX_MMAP_SIZE;

#if !NON_TEMPORAL_WRITES	
	*bitmap_root = merkle_start_addr;
#endif
	(*extent_length) = (MAX_MMAP_SIZE - (offset % MAX_MMAP_SIZE));

	DEBUG("Found: mmap addr 0x%lx, extent length %lu\n",
			*mmap_addr, *extent_length);
	return 0;

not_found:
	DEBUG("Not found, perform mmap\n");

	if (offset >= ALIGN_MMAP_DOWN(nvf->node->true_length)) {
		DEBUG("File length smaller than offset: "
			"length 0x%lx, offset 0x%lx\n",
			nvf->node->length, offset);
		return 1;
	}
		
	if (!wr_lock) {
		if (tbl_over != NULL)  {
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		START_TIMING(nvnode_lock_t, nvnode_lock_time);
		NVP_LOCK_NODE_WR(nvf);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL)  {
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
	        END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}

	start_offset = ALIGN_MMAP_DOWN(offset);	
		
	if (start_offset + MAX_MMAP_SIZE > nvf->node->true_length) {
		ERROR("File length smaller than offset: "
			"length 0x%lx, offset 0x%lx\n",
			nvf->node->length, offset);
		MSG("%s: file length smaller than offset\n", __func__);
		return 1;
	}

	START_TIMING(file_mmap_t, file_mmap_time);	
	int max_perms = ((nvf->canRead) ? PROT_READ : 0) | 
			((nvf->canWrite) ? PROT_WRITE : 0);

	start_addr = (unsigned long) FSYNC_MMAP
	(
		NULL,
		MAX_MMAP_SIZE,
		max_perms, //max_perms,
		MAP_SHARED | MAP_POPULATE,
//		MAP_SHARED,
		nvf->fd, //fd_with_max_perms,
		start_offset
		//0
	);

	END_TIMING(file_mmap_t, file_mmap_time);

	DEBUG("%s: created mapping of address = %lu, inode = %lu, thread id = %lu\n", __func__, start_addr, nvf->node->serialno, pthread_self());
	
	/* Bitmap Tree creation */
#if !NON_TEMPORAL_WRITES	
	createTree((struct merkleBtreeNode **)&merkle_start_addr);
	if (IS_ERR(start_addr) || start_addr == 0 || merkle_start_addr == 0) {
#else
	if (IS_ERR(start_addr) || start_addr == 0) {		
#endif       
		MSG("mmap failed for fd %i: %s, mmap count %d, addr %lu, errno is %lu\n",
		    nvf->fd, strerror(errno), num_mmap, start_addr, errno);
		MSG("Open count %d, close count %d\n", num_open, num_close);
		MSG("Use posix operations for fd %i instead.\n", nvf->fd);
		nvf->posix = 1;
		fflush(NULL);
		assert(0);
	}

	DEBUG_FILE("%s: Performed mmap. Start_addr = %p, inode no = %lu\n", __func__, (void *) start_addr, nvf->node->serialno);

	num_mmap++;

	DEBUG("mmap offset 0x%lx, start_offset 0x%lx\n", offset, start_offset);

	height = nvf->node->height;
	new_height = calculate_new_height(offset);

	if (height < new_height) {
		MSG("Increase height from %u to %u\n", height, new_height);

		while (height < new_height) {
			unsigned long old_root = (unsigned long)nvf->node->root;
			nvf->node->root = malloc(1024 * sizeof(unsigned long));

#if !NON_TEMPORAL_WRITES	
			unsigned long old_merkle_root = (unsigned long)nvf->node->merkle_root;
			nvf->node->merkle_root = malloc(1024 * sizeof(unsigned long));
			for (i = 0; i < 1024; i++) {
				nvf->node->root[i] = 0;
				nvf->node->merkle_root[i] = 0;
			}
			nvf->node->merkle_root[0] = (unsigned long)old_merkle_root;
#else
			for (i = 0; i < 1024; i++) {
				nvf->node->root[i] = 0;
			}
#endif
			DEBUG("Malloc new root @ %p\n", nvf->node->root);
			nvf->node->root[0] = (unsigned long)old_root;
			DEBUG("Old root 0x%lx\n", nvf->node->root[0]);
			height++;
		}

		nvf->node->height = new_height;
		height = new_height;
	}

	root = nvf->node->root;
#if !NON_TEMPORAL_WRITES	
	merkle_root = nvf->node->merkle_root;
#endif
	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;
		DEBUG("index %d\n", index);
		if (height) {
			if (root[index] == 0) {
				root[index] = (unsigned long)malloc(1024 *
						sizeof(unsigned long));

#if !NON_TEMPORAL_WRITES	
				merkle_root[index] = (unsigned long)malloc(1024 * sizeof(unsigned long));
				root = (unsigned long *)root[index];
				merkle_root = (unsigned long *)merkle_root[index];
				for (i = 0; i < 1024; i++) {
					root[i] = 0;
					merkle_root[i] = 0;
				}
#else
				root = (unsigned long *)root[index];
				for (i = 0; i < 1024; i++) {
					root[i] = 0;
				}
#endif				
			} else {
				root = (unsigned long *)root[index];
#if !NON_TEMPORAL_WRITES	
				merkle_root = (unsigned long *)merkle_root[index];
#endif
			}
		} else {
			root[index] = start_addr;
			nvf->node->root_dirty_cache[nvf->node->root_dirty_num] = index;
			if(!nvf->node->total_dirty_mmaps) {
				nvf->node->root_dirty_num++;
				if(nvf->node->root_dirty_num == 20)
					nvf->node->total_dirty_mmaps = 1;
			}
#if !NON_TEMPORAL_WRITES	
			merkle_root[index] = merkle_start_addr;
#endif
		}
		start_offset = start_offset % capacity;
	} while(height--);

	nvf->node->isRootSet = 1;
	(*mmap_addr) = (start_addr + (offset % MAX_MMAP_SIZE));
	*offset_within_mmap = offset % MAX_MMAP_SIZE;

#if !NON_TEMPORAL_WRITES	
	*bitmap_root = merkle_start_addr;
#endif
	(*extent_length) = (MAX_MMAP_SIZE - (offset % MAX_MMAP_SIZE));

	if (!wr_lock) {
		if (tbl_over != NULL)  {
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
	}

	DEBUG("mmap addr 0x%lx, extent length %lu\n",
			*mmap_addr, *extent_length);

	return 0;
}

#if DATA_JOURNALING_ENABLED
	
static void nvp_manage_over_dr_memory(struct NVFile *nvf, uint64_t *extent_length,
				      uint64_t len_to_write, off_t start_offset,
				      int index)
{
	int i;
	/* 
	 * Check if the reads are being served from DR. If yes, then all the future reads should
	 * be performed through the file backed memory, for the appended and fsync()ed region. 
	 */

	DEBUG_FILE("%s START: dr_offset_start = %lu, dr_offset_end = %lu\n",
		   __func__, nvf->node->dr_over_info.dr_offset_start, nvf->node->dr_over_info.dr_offset_end);
	if(*extent_length >= len_to_write)
		nvf->node->dr_over_info.dr_offset_start = start_offset + len_to_write;
}

#endif // DATA_JOURNALING_ENABLED
	
static void nvp_manage_dr_memory(struct NVFile *nvf, uint64_t *extent_length,
				 uint64_t len_to_write, off_t start_offset,
				 int index)
{
	int i;
	unsigned long offset_within_mmap = 0;
	/* 
	 * Check if the reads are being served from DR. If yes, then all the future reads should
	 * be performed through the file backed memory, for the appended and fsync()ed region. 
	 */

	offset_within_mmap = start_offset;
	DEBUG_FILE("%s START: dr_offset_start = %lu, dr_offset_end = %lu, offset_within_mmap = %lu\n",
		   __func__, nvf->node->dr_info.dr_offset_start, nvf->node->dr_info.dr_offset_end,
		   offset_within_mmap);
	if(nvf->node->dr_info.dr_offset_start > offset_within_mmap)	
		// Update the portion from which the dirty DR region starts. 
		nvf->node->dr_info.dr_offset_start = offset_within_mmap;
	if(*extent_length > len_to_write) {
		if(nvf->node->dr_info.dr_offset_end < (offset_within_mmap + len_to_write))
			// Update the portion till which the dirty DR region exists
			nvf->node->dr_info.dr_offset_end = offset_within_mmap + len_to_write;
	} else {
		// It is a large write. So finish writing to this mmap. 
		if(nvf->node->dr_info.dr_offset_end < (offset_within_mmap + *extent_length))  
			nvf->node->dr_info.dr_offset_end = DR_SIZE;
	}
	DEBUG_FILE("%s END: dr_offset_start = %lu, dr_offset_end = %lu, offset_within_mmap = %lu\n",
		   __func__, nvf->node->dr_info.dr_offset_start, nvf->node->dr_info.dr_offset_end,
		   offset_within_mmap);
	if (nvf->node->dr_info.valid_offset > DR_SIZE)
		assert(0);
	if (nvf->node->dr_info.dr_offset_start > DR_SIZE)
		assert(0);
	if (nvf->node->dr_info.dr_offset_end > DR_SIZE)
		assert(0);
}

#if DATA_JOURNALING_ENABLED 
 
static int nvp_get_over_dr_address(struct NVFile *nvf,
				   off_t offset,
				   size_t len_to_write, 
				   unsigned long *mmap_addr,
				   off_t *offset_within_mmap,
				   size_t *extent_length,
				   int wr_lock,
				   int cpuid,
				   struct NVTable_maps *tbl_app,
				   struct NVTable_maps *tbl_over)
{
	int index;
	unsigned long capacity = DR_OVER_SIZE;
	unsigned long start_addr, unaligned_file_end;
	off_t file_offset = offset, offset_within_page = 0;
	off_t start_offset = 0;
	struct stat stat_buf;
	instrumentation_type nvnode_lock_time, dr_mem_queue_time;
	
	DEBUG("Get mmap address: offset 0x%lx, height %u\n",
	      offset, height);
	/* The index of the mmap in the global DR pool.
	 * Max number of entries = 1024. 
	 */
	if (nvf->node->dr_over_info.start_addr == 0) 
		goto not_found;

	/* Anonymous mmap at that index is present for the file.
	 * So get the start address. 
	 */
	start_addr = nvf->node->dr_over_info.start_addr;
	DEBUG("addr 0x%lx\n", start_addr);
	// Get the offset in the mmap to which the memcpy must be performed. 
	if (IS_ERR(start_addr) || start_addr == 0) {
		MSG("%s: ERROR!\n", __func__);
		assert(0);
	}
	/* address we want to perform memcpy(). The start_offset
	 * is the offset with relation to node->true_length. 
	 */
	start_offset = nvf->node->dr_over_info.dr_offset_start;

	DEBUG_FILE("%s: DR valid_offset = %lu. Start offset = %lu, true length = %lu\n",
		   __func__, nvf->node->dr_over_info.valid_offset,
		   start_offset, nvf->node->true_length);

	if ((start_offset % MMAP_PAGE_SIZE) != (file_offset % MMAP_PAGE_SIZE)) {
		offset_within_page = start_offset % MMAP_PAGE_SIZE;
		if (offset_within_page != 0) {
			start_offset += MMAP_PAGE_SIZE - offset_within_page;
		}
		offset_within_page = file_offset % MMAP_PAGE_SIZE;
		if (offset_within_page != 0) {
			start_offset += offset_within_page;
		}
	}

	if (start_offset >= DR_OVER_SIZE) {		
		DEBUG_FILE("%s: start_offset = %lld, DR_OVER_SIZE = %lu, dr_offset_start = %lld\n",
			   __func__, start_offset, DR_OVER_SIZE, nvf->node->dr_over_info.dr_offset_start);     	
	}

	if (nvf->node->dr_over_info.valid_offset > start_offset)
		assert(0);
	
	*mmap_addr = start_addr + start_offset;
	*offset_within_mmap = start_offset;
	/* This gives how much free space is remaining in the
	 * current anonymous mmap. 
	 */
	if (start_offset < DR_OVER_SIZE)
		*extent_length = DR_OVER_SIZE - start_offset;
	else
		*extent_length = 0;
	/* The mmap for that index was not found. Performing mmap 
	 * in this section. 	
	 */
	if (!wr_lock) {
		if (tbl_over != NULL)  {
			TBL_ENTRY_UNLOCK_WR(tbl_over);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		START_TIMING(nvnode_lock_t, nvnode_lock_time);
		NVP_LOCK_NODE_WR(nvf);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_WR(tbl_app);
		}
		if (tbl_over != NULL) {
			TBL_ENTRY_LOCK_WR(tbl_over);
		}
		END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}

	nvp_manage_over_dr_memory(nvf, extent_length, len_to_write,
				  start_offset, index);

	if (nvf->node->dr_over_info.dr_offset_end != DR_OVER_SIZE)
		assert(0);
	
	return 0;

not_found:	
	/* The mmap for that index was not found. Performing mmap
	 * in this section. 	
	 */
	if (!wr_lock) {
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_WR(tbl_over);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		START_TIMING(nvnode_lock_t, nvnode_lock_time);
		NVP_LOCK_NODE_WR(nvf);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_WR(tbl_app);
		}
		if (tbl_over != NULL)  {
			TBL_ENTRY_LOCK_WR(tbl_over);
		}
		END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}
       
	START_TIMING(dr_mem_queue_t, dr_mem_queue_time);

	if( lfds711_queue_umm_dequeue(&qs_over, &qe_over) ) {
		// Found addr in global pool		
		struct free_dr_pool *temp_dr_info = NULL;
		unsigned long offset_in_page = 0;		
		temp_dr_info = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe_over );
		nvf->node->dr_over_info.start_addr = temp_dr_info->start_addr;
		nvf->node->dr_over_info.valid_offset = temp_dr_info->valid_offset;
		nvf->node->dr_over_info.dr_fd = temp_dr_info->dr_fd;
		nvf->node->dr_over_info.dr_serialno = temp_dr_info->dr_serialno;
		nvf->node->dr_over_info.dr_offset_start = temp_dr_info->dr_offset_start;
		nvf->node->dr_over_info.dr_offset_end = DR_OVER_SIZE;
		__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);
	} else {
		DEBUG_FILE("%s: Allocating new DR\n", __func__);
		// Nothing in global pool
		int dr_fd = 0;
		int i = 0;
		char dr_fname[256];
		unsigned long offset_in_page = 0;
		int num_blocks = DR_OVER_SIZE / MMAP_PAGE_SIZE;
		int max_perms = ((nvf->canRead) ? PROT_READ : 0) | 
			((nvf->canWrite) ? PROT_WRITE : 0);
		DEBUG_FILE("%s: DR not found in global pool. Allocated dr_file variable\n", __func__);
		
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-OVER-XXXXXX");		
		dr_fd = _nvp_fileops->OPEN(mktemp(dr_fname), O_RDWR | O_CREAT, 0666);		
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		posix_fallocate(dr_fd, 0, DR_SIZE);		
		num_mmap++;		
		num_drs++;
		num_drs_critical_path++;
		nvf->node->dr_over_info.start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_OVER_SIZE,
			 max_perms, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );

		DEBUG_FILE("%s: Setting offset_start to DR_SIZE. FD = %d\n",
			   __func__, nvf->fd);
		fstat(dr_fd, &stat_buf);
		nvf->node->dr_over_info.dr_serialno = stat_buf.st_ino;
		nvf->node->dr_over_info.dr_fd = dr_fd;
		nvf->node->dr_over_info.valid_offset = 0;
		nvf->node->dr_over_info.dr_offset_start = 0;
		nvf->node->dr_over_info.dr_offset_end = DR_OVER_SIZE;
		dr_fname[0] = '\0';
		DEBUG_FILE("%s: DR not found in global pool. Initialized DR_INFO. FD = %d\n", __func__, dr_fd);
	}
	start_addr = nvf->node->dr_over_info.start_addr;
	__atomic_fetch_add(&dr_mem_allocated, DR_OVER_SIZE,
			   __ATOMIC_SEQ_CST);
	nvf->node->dr_mem_used += DR_OVER_SIZE;

	END_TIMING(dr_mem_queue_t, dr_mem_queue_time);
	if (IS_ERR(start_addr) || start_addr == 0)
	{
		MSG("mmap failed for  %s, mmap count %d, addr %lu, errno is %lu\n",
		    strerror(errno), num_mmap, start_addr, errno);
		MSG("Open count %d, close count %d\n",
		    num_open, num_close);
		nvf->posix = 1;
		assert(0);
	}	
	/* Get the index of the mmap from the size of mmap and 
	 * from the offset.
	 */ 
	DEBUG_FILE("%s: offset requested = %lu\n", __func__, offset);
	start_offset = nvf->node->dr_over_info.dr_offset_start;
	offset_within_page = start_offset % MMAP_PAGE_SIZE;
	if (offset_within_page != 0) {
		start_offset += MMAP_PAGE_SIZE - offset_within_page;
	}
	offset_within_page = file_offset % MMAP_PAGE_SIZE;
	if (offset_within_page != 0) {
		start_offset += offset_within_page;
	}

	if ((start_offset % MMAP_PAGE_SIZE) != (file_offset % MMAP_PAGE_SIZE))
		assert(0);

	if (start_offset >= DR_OVER_SIZE) {
		DEBUG_FILE("%s: start_offset = %lld, DR_OVER_SIZE = %lu, dr_offset_start = %lld\n",
			   __func__, start_offset, DR_OVER_SIZE, nvf->node->dr_over_info.dr_offset_start);   	
	}

	if (nvf->node->dr_over_info.valid_offset > start_offset)
		assert(0);

	*mmap_addr = start_addr + start_offset;
	*offset_within_mmap = start_offset;

	if (start_offset < DR_OVER_SIZE)
		*extent_length = DR_OVER_SIZE - start_offset;
	else
		*extent_length = 0;
	
	DEBUG_FILE("%s: Will do manage DR memory if it is a write\n",
		   __func__);

	nvp_manage_over_dr_memory(nvf, extent_length,
				  len_to_write, start_offset, index);

	if (nvf->node->dr_over_info.dr_offset_end != DR_OVER_SIZE)
		assert(0);

	return 0;
}

#endif // DATA_JOURNALING_ENABLED
 
static int nvp_get_dr_mmap_address(struct NVFile *nvf, off_t offset,
				   size_t len_to_write, size_t count,
				   unsigned long *mmap_addr,
				   off_t *offset_within_mmap,
				   size_t *extent_length, int wr_lock,
				   int cpuid, int iswrite,
				   struct NVTable_maps *tbl_app,
				   struct NVTable_maps *tbl_over)
{
	int index;
	unsigned long capacity = DR_SIZE;
	unsigned long start_addr, unaligned_file_end;
	off_t start_offset = offset;
	struct stat stat_buf;
	instrumentation_type nvnode_lock_time, dr_mem_queue_time;
	
	DEBUG("Get mmap address: offset 0x%lx, height %u\n",
	      offset, height);
	/* The index of the mmap in the global DR pool.
	 * Max number of entries = 1024. 
	 */
	if (nvf->node->dr_info.start_addr == 0) {
		if(iswrite)
			/* Have to get the mmap from the 
			 * global anonymous pool. 
			 */
			goto not_found;
		else {
			/* If it is a read, then the anonymous mmap 
			 * must be found. Otherwise something is wrong. 
			 */
			ERROR("dr mmap not found\n");
		        MSG("%s: dr mmap not found\n", __func__);
			assert(0);
		}
	}
	/* Anonymous mmap at that index is present for the file.
	 * So get the start address. 
	 */
	start_addr = nvf->node->dr_info.start_addr;
	DEBUG("addr 0x%lx\n", start_addr);
	// Get the offset in the mmap to which the memcpy must be performed. 
	if (IS_ERR(start_addr) || start_addr == 0) {
		MSG("%s: ERROR!\n", __func__);
		assert(0);
	}
	/* address we want to perform memcpy(). The start_offset
	 * is the offset with relation to node->true_length. 
	 */
	DEBUG_FILE("%s: DR valid_offset = %lu. Start offset = %lu, true length = %lu\n",
		   __func__, nvf->node->dr_info.valid_offset,
		   start_offset, nvf->node->true_length);
	start_offset = (start_offset +
			nvf->node->dr_info.valid_offset);	
	*mmap_addr = start_addr + start_offset;
	*offset_within_mmap = start_offset;
	/* This gives how much free space is remaining in the
	 * current anonymous mmap. 
	 */
	*extent_length = DR_SIZE - start_offset;
	/* The mmap for that index was not found. Performing mmap 
	 * in this section. 	
	 */
	
	if (!wr_lock) {
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		START_TIMING(nvnode_lock_t, nvnode_lock_time);
		NVP_LOCK_NODE_WR(nvf);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
		END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}
	if(iswrite) {
		nvp_manage_dr_memory(nvf, extent_length, len_to_write,
				     start_offset, index);		
	}

	if (!wr_lock && !iswrite) {
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
	}
		
	return 0;

not_found:	
	/* The mmap for that index was not found. Performing mmap
	 * in this section. 	
	 */
	if (!wr_lock) {
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		START_TIMING(nvnode_lock_t, nvnode_lock_time);
		NVP_LOCK_NODE_WR(nvf);
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
		END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}
       
	START_TIMING(dr_mem_queue_t, dr_mem_queue_time);

	if( lfds711_queue_umm_dequeue(&qs, &qe) ) {
		// Found addr in global pool		
		struct free_dr_pool *temp_dr_info = NULL;
		unsigned long offset_in_page = 0;		
		temp_dr_info = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe );
		nvf->node->dr_info.start_addr = temp_dr_info->start_addr;
		nvf->node->dr_info.valid_offset = temp_dr_info->valid_offset;
		nvf->node->dr_info.dr_offset_start = DR_SIZE;
		nvf->node->dr_info.dr_fd = temp_dr_info->dr_fd;
		nvf->node->dr_info.dr_serialno = temp_dr_info->dr_serialno;
		DEBUG_FILE("%s: GOT FROM GLOBAL POOL. DR found in global pool. Got from global pool. FD = %d\n",
			   __func__, temp_dr_info->dr_fd);
		if (nvf->node->dr_info.valid_offset < DR_SIZE) {
			offset_in_page = nvf->node->true_length % MMAP_PAGE_SIZE;
			if (offset_in_page != 0)
				nvf->node->dr_info.valid_offset += offset_in_page;
		}
		nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;
		__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);
	} else {
		DEBUG_FILE("%s: Allocating new DR\n", __func__);
		// Nothing in global pool
		int dr_fd = 0;
		int i = 0;
		char dr_fname[256];
		unsigned long offset_in_page = 0;
		int num_blocks = DR_SIZE / MMAP_PAGE_SIZE;
		int max_perms = ((nvf->canRead) ? PROT_READ : 0) | 
			((nvf->canWrite) ? PROT_WRITE : 0);
		DEBUG_FILE("%s: DR not found in global pool. Allocated dr_file variable\n", __func__);
		
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-XXXXXX");		
		dr_fd = _nvp_fileops->OPEN(mktemp(dr_fname), O_RDWR | O_CREAT, 0666);		
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		posix_fallocate(dr_fd, 0, DR_SIZE);		
		num_mmap++;		
		num_drs++;
		num_drs_critical_path++;
		nvf->node->dr_info.start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_SIZE,
			 max_perms, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );

		DEBUG_FILE("%s: Setting offset_start to DR_SIZE. FD = %d\n",
			   __func__, nvf->fd);
		fstat(dr_fd, &stat_buf);
		nvf->node->dr_info.dr_serialno = stat_buf.st_ino;
		nvf->node->dr_info.dr_fd = dr_fd;
		nvf->node->dr_info.valid_offset = 0;
		nvf->node->dr_info.dr_offset_start = DR_SIZE;
		offset_in_page = nvf->node->true_length % MMAP_PAGE_SIZE;
		if (offset_in_page != 0)
			nvf->node->dr_info.valid_offset += offset_in_page;
		nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;
		dr_fname[0] = '\0';
		DEBUG_FILE("%s: DR not found in global pool. Initialized DR_INFO. FD = %d\n", __func__, dr_fd);
	}
	start_addr = nvf->node->dr_info.start_addr;
	__atomic_fetch_add(&dr_mem_allocated, DR_SIZE,
			   __ATOMIC_SEQ_CST);
	nvf->node->dr_mem_used += DR_SIZE;

	END_TIMING(dr_mem_queue_t, dr_mem_queue_time);
	if (IS_ERR(start_addr) || start_addr == 0)
	{
		MSG("mmap failed for  %s, mmap count %d, addr %lu, errno is %lu\n",
		    strerror(errno), num_mmap, start_addr, errno);
		MSG("Open count %d, close count %d\n",
		    num_open, num_close);
		nvf->posix = 1;
		assert(0);
	}	
	/* Get the index of the mmap from the size of mmap and 
	 * from the offset.
	 */ 
	DEBUG_FILE("%s: offset requested = %lu\n", __func__, offset);
	start_offset = (start_offset +
			nvf->node->dr_info.valid_offset);	
	*mmap_addr = start_addr + start_offset;
	*offset_within_mmap = start_offset;
	*extent_length = DR_SIZE - start_offset;

	DEBUG_FILE("%s: Will do manage DR memory if it is a write\n",
		   __func__);
	if(iswrite) 
		nvp_manage_dr_memory(nvf, extent_length,
				     len_to_write, start_offset, index);
	
	if (!wr_lock && !iswrite) {
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
	}
	
	return 0;
}

 RETT_PREAD _nvp_read_beyond_true_length(INTF_PREAD, int wr_lock, int cpuid, struct NVFile *nvf, struct NVTable_maps *tbl_app, struct NVTable_maps *tbl_over)
{
	size_t len_to_read, extent_length, read_count;
	unsigned long mmap_addr;
	off_t read_offset_beyond_true_length, offset_within_mmap;
	instrumentation_type copy_appendread_time, get_dr_mmap_time;
	
	num_anon_read++;
	
	//printf("%s: here beyond true length\n", __func__);
	read_count = 0;
	len_to_read = count;

	read_offset_beyond_true_length = offset - nvf->node->true_length;

	while (len_to_read > 0) {

#if DATA_JOURNALING_ENABLED

		read_tbl_mmap_entry(nvf->node,
				    offset,
				    len_to_read,
				    &mmap_addr,
				    &extent_length,
				    1);		
		if (mmap_addr == 0) {
			START_TIMING(get_dr_mmap_t, get_dr_mmap_time);		
			nvp_get_dr_mmap_address(nvf, read_offset_beyond_true_length,
						len_to_read, read_count,
						&mmap_addr, &offset_within_mmap, &extent_length,
						wr_lock, cpuid, 0, tbl_app, tbl_over);
			END_TIMING(get_dr_mmap_t, get_dr_mmap_time);
		}

#else // DATA_JOURNALING_ENABLED

		START_TIMING(get_dr_mmap_t, get_dr_mmap_time);		
		nvp_get_dr_mmap_address(nvf, read_offset_beyond_true_length,
					len_to_read, read_count,
					&mmap_addr, &offset_within_mmap, &extent_length,
					wr_lock, cpuid, 0, tbl_app, tbl_over);
		END_TIMING(get_dr_mmap_t, get_dr_mmap_time);
			
#endif // DATA_JOURNALING_ENABLED
		
		if(extent_length > len_to_read)
			extent_length = len_to_read;
		
		START_TIMING(copy_appendread_t, copy_appendread_time);
		if(FSYNC_MEMCPY(buf, (char *)mmap_addr, extent_length) != buf) {
			MSG("%s: memcpy read failed\n", __func__);
			assert(0);
		}

#if NVM_DELAY
		perfmodel_add_delay(1, extent_length);
#endif		
		END_TIMING(copy_appendread_t, copy_appendread_time);
		num_memcpy_read++;
		memcpy_read_size += extent_length;
		read_offset_beyond_true_length += extent_length;
		read_count  += extent_length;
		buf += extent_length;
		len_to_read -= extent_length;
		anon_read_size += extent_length;
	}

	if (tbl_over != NULL)	{
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
	}
	if (tbl_app != NULL) {
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
	}
	return read_count;	
}

RETT_PREAD read_from_file_mmap(int file,
			       off_t read_offset_within_true_length,
			       size_t len_to_read_within_true_length,
			       int wr_lock,
			       int cpuid,
			       void *buf, 
			       struct NVFile *nvf,
			       struct NVTable_maps *tbl_app,
			       struct NVTable_maps *tbl_over)
{
	int ret = 0, ret_get_addr = 0;
	unsigned long mmap_addr = 0, bitmap_root = 0;
	off_t offset_within_mmap = 0;
	size_t extent_length = 0, read_count = 0, posix_read = 0;
	instrumentation_type copy_overread_time, get_mmap_time;
	
	START_TIMING(get_mmap_t, get_mmap_time);
	ret = nvp_get_mmap_address(nvf,
				   read_offset_within_true_length,
				   read_count,
				   &mmap_addr,
				   &bitmap_root,
				   &offset_within_mmap,
				   &extent_length,
				   wr_lock,
				   cpuid,
				   tbl_app,
				   tbl_over);
	END_TIMING(get_mmap_t, get_mmap_time);

	switch (ret) {
	case 0: // Mmaped. Do memcpy.
		break;
	case 1: // Not mmaped. Calling Posix pread.
		posix_read = _nvp_fileops->PREAD(file,
						 buf,
						 len_to_read_within_true_length,
						 read_offset_within_true_length);
		num_posix_read++;
		posix_read_size += posix_read;

		return posix_read;
	default:
		break;
	}

	if (extent_length > len_to_read_within_true_length)
		extent_length = len_to_read_within_true_length;		
		
	START_TIMING(copy_overread_t, copy_overread_time);
	DEBUG_FILE("%s: Reading from addr = %p, offset = %lu, size = %lu\n", __func__, (void *) mmap_addr, offset_within_mmap, extent_length);
	if(FSYNC_MEMCPY(buf, (const void * restrict)mmap_addr, extent_length) != buf) {
		printf("%s: memcpy read failed\n", __func__);
		fflush(NULL);
		assert(0);
	}

#if NVM_DELAY
	perfmodel_add_delay(1, extent_length);
#endif

	END_TIMING(copy_overread_t, copy_overread_time);

	num_memcpy_read++;
	memcpy_read_size += extent_length;

	return extent_length;
}


RETT_PWRITE write_to_file_mmap(int file,
			      off_t write_offset_within_true_length,
			      size_t len_to_write_within_true_length,
			      int wr_lock,
			      int cpuid,
			      const void *buf, 
			      struct NVFile *nvf)
{
    int ret = 0;
    unsigned long mmap_addr = 0, bitmap_root = 0;
    off_t offset_within_mmap = 0;
    size_t extent_length = 0, write_count = 0, posix_write = 0, data_written = 0;
    instrumentation_type copy_overwrite_time, get_mmap_time;

    while(len_to_write_within_true_length > 0) {
        START_TIMING(get_mmap_t, get_mmap_time);
        ret = nvp_get_mmap_address(nvf,
                write_offset_within_true_length,
                write_count,
                &mmap_addr,
                &bitmap_root,
                &offset_within_mmap,
                &extent_length,
                wr_lock,
                cpuid,
                NULL,
                NULL);
        END_TIMING(get_mmap_t, get_mmap_time);

        switch (ret) {
            case 0: // Mmaped. Do memcpy.
                break;
            case 1: // Not mmaped. Calling Posix pread.
                posix_write = _nvp_fileops->PWRITE(file,
                        buf,
                        len_to_write_within_true_length,
                        write_offset_within_true_length);
                num_posix_write++;
                posix_write_size += posix_write;
                return posix_write;
            default:
                break;
        }

        if (extent_length > len_to_write_within_true_length)
            extent_length = len_to_write_within_true_length;

        START_TIMING(copy_overwrite_t, copy_overwrite_time);

#if NON_TEMPORAL_WRITES
        DEBUG_FILE("%s: args: mmap_addr = %p, offset in mmap = %lu, length to write = %lu\n", __func__, (char *)mmap_addr, offset_within_mmap, extent_length);
        if(MEMCPY_NON_TEMPORAL((char *)mmap_addr, buf, extent_length) == NULL) {
            printf("%s: non-temporal memcpy failed\n", __func__);
            fflush(NULL);
            assert(0);
        }
        num_write_nontemporal++;
        non_temporal_write_size += extent_length;
#else //NON_TEMPORAL_WRITES
        if(FSYNC_MEMCPY((char *)mmap_addr, buf, extent_length) == NULL) {
            printf("%s: non-temporal memcpy failed\n", __func__);
            fflush(NULL);
            assert(0);
        }
#endif //NON TEMPORAL WRITES
#if NVM_DELAY
        perfmodel_add_delay(0, extent_length);
#endif
        num_memcpy_write++;
        memcpy_write_size += extent_length;
        len_to_write_within_true_length -= extent_length;
        write_offset_within_true_length += extent_length;
        buf += extent_length;
        data_written += extent_length;
        num_mfence++;
        _mm_sfence();

        END_TIMING(copy_overwrite_t, copy_overwrite_time);
    }
    return data_written;
} 

 
 RETT_PREAD _nvp_do_pread(INTF_PREAD, int wr_lock, int cpuid, struct NVFile *nvf, struct NVTable_maps *tbl_app, struct NVTable_maps *tbl_over)
{
	SANITYCHECKNVF(nvf);
	long long read_offset_within_true_length = 0;
	size_t read_count, extent_length, read_count_beyond_true_length;
	size_t len_to_read_within_true_length;
	size_t posix_read = 0;
	unsigned long mmap_addr = 0;
	unsigned long bitmap_root = 0;
	off_t offset_within_mmap;
	ssize_t available_length = (nvf->node->length) - offset;
	instrumentation_type copy_overread_time, read_tbl_mmap_time;

	if (UNLIKELY(!nvf->canRead)) {
		DEBUG("FD not open for reading: %i\n", file);
		errno = EBADF;
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		return -1;
	}

	else if (UNLIKELY(offset < 0))
	{
		DEBUG("Requested read at negative offset (%li)\n", offset);
		errno = EINVAL;
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		return -1;
	}

	if(nvf->aligned)
	{
		DEBUG("This read must be aligned.  Checking alignment.\n");

		if(UNLIKELY(available_length <= 0))
		{
			DEBUG("Actually there weren't any bytes available "
				"to read.  Bye! (length %li, offset %li, "
				"available_length %li)\n", nvf->node->length,
				offset, available_length);
			if (tbl_over != NULL)	{
				TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			}
			if (tbl_app != NULL) {
				TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			}
			return 0;
		}

		if(UNLIKELY(count % 512))
		{
			DEBUG("cout is not aligned to 512 (count was %i)\n",
				count);

			errno = EINVAL;
			if (tbl_over != NULL)	{
				TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			}
			if (tbl_app != NULL) {
				TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			}
			return -1;
		}
		if(UNLIKELY(offset % 512))
		{
			DEBUG("offset was not aligned to 512 (offset was %i)\n",
				offset);

			errno = EINVAL;
			if (tbl_over != NULL) {
				TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			}
			if (tbl_app != NULL) {
				TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			}
			return -1;
		}
		if(UNLIKELY(((long long int)buf & (512-1)) != 0))
		{
			DEBUG("buffer was not aligned to 512 (buffer was %p, "
				"mod 512=%i)\n", buf, (long long int)buf % 512);
			errno = EINVAL;
			if (tbl_over != NULL)	
				TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);			
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);	
			return -1;
		}
	}

	ssize_t len_to_read = count;
	if (count > available_length)
	{
		len_to_read = available_length;
		DEBUG("Request read length was %li, but only %li bytes "
			"available. (filelen = %li, offset = %li, "
			"requested %li)\n", count, len_to_read,
			nvf->node->length, offset, count);
	}

	if(UNLIKELY( (len_to_read <= 0) || (available_length <= 0) ))
	{
		if (tbl_over != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);	
		return 0; // reading 0 bytes is easy!
	}

	DEBUG("mmap is length %li, len_to_read is %li\n", nvf->node->maplength,
		len_to_read);

	SANITYCHECK(len_to_read + offset <= nvf->node->length);
		
	read_count = 0;

	/*
	 * if data to be read <= true_length_for_read, then it can be read from file backed mmap. Otherwise, it can be
	 * read from anonymous mmap 
	 * len_to_read_within_true_length = amount of data that can be read using file backed mmap. 
	 */
	read_offset_within_true_length = (offset > nvf->node->true_length) ? -1 : offset;
		
	if(read_offset_within_true_length == -1)
		len_to_read_within_true_length = 0;
	else {
		len_to_read_within_true_length = (len_to_read + offset > nvf->node->true_length) ? nvf->node->true_length - offset : len_to_read;
	}

	DEBUG_FILE("%s: len of read request = %lu, offset = %lu. True Size of file = %lu. Fake file size = %lu\n", __func__, len_to_read_within_true_length, read_offset_within_true_length, nvf->node->true_length, nvf->node->length);

	while (len_to_read_within_true_length > 0) {
		// Get the file backed mmap address from which the read is to be performed. 
		
		START_TIMING(read_tbl_mmap_t, read_tbl_mmap_time);
		read_tbl_mmap_entry(nvf->node,
				    read_offset_within_true_length,
				    len_to_read_within_true_length,
				    &mmap_addr,
				    &extent_length,
				    1);
		END_TIMING(read_tbl_mmap_t, read_tbl_mmap_time);
		
		DEBUG_FILE("%s: addr to read = %p, size to read = %lu. Inode = %lu\n", __func__, mmap_addr, extent_length, nvf->node->serialno);
		DEBUG("Pread: get_mmap_address returned %d, length %llu\n",
			ret, extent_length);

		if (mmap_addr == 0) {
			extent_length = read_from_file_mmap(file,
							    read_offset_within_true_length,
							    len_to_read_within_true_length,
							    wr_lock,
							    cpuid,
							    buf,
							    nvf,
							    tbl_app,
							    tbl_over);
			goto post_read;

		}
		
		DEBUG_FILE("%s: memcpy args: buf = %p, mmap_addr = %p, length = %lu. File off = %lld. Inode = %lu\n", __func__, buf, (void *) mmap_addr, extent_length, read_offset_within_true_length, nvf->node->serialno);
		START_TIMING(copy_overread_t, copy_overread_time);
		if(FSYNC_MEMCPY(buf,
				(void *)mmap_addr,
				extent_length) != buf) {
			printf("%s: memcpy read failed\n", __func__);
			fflush(NULL);
			assert(0);
		}
#if NVM_DELAY
		perfmodel_add_delay(1, extent_length);
#endif		
		END_TIMING(copy_overread_t, copy_overread_time);
		// Add the NVM read latency

		num_memcpy_read++;
		memcpy_read_size += extent_length;
	post_read:
		len_to_read -= extent_length;
		len_to_read_within_true_length -= extent_length;
		read_offset_within_true_length += extent_length;
		read_count  += extent_length;
		buf += extent_length;
		offset += extent_length;
	}
	
	if(!len_to_read) {
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		DEBUG_FILE("%s: Returning from read over. Size = %lu\n", __func__, read_count);
		return read_count;
	}

	// If we need to read from anonymous memory, call _nvp_read_beyond_true_length
	read_count_beyond_true_length = _nvp_read_beyond_true_length(file,
								     buf,
								     len_to_read,
								     offset,
								     wr_lock,
								     cpuid,
								     nvf,
								     tbl_app,
								     tbl_over);
	read_count += read_count_beyond_true_length;
	
	DEBUG_FILE("%s: Returning from read beyond. Size = %lu\n", __func__, read_count);
	return read_count;
}

/* 
 * _nvp_extend_write gets called whenever there is an append to a file. The write first goes to the
 * anonymous memory region through memcpy. During fsync() time, the data is copied non-temporally from
 * anonymous DRAM to the file. 
 */
 RETT_PWRITE _nvp_extend_write(INTF_PWRITE,
			       int wr_lock,
			       int cpuid,
			       struct NVFile *nvf,
			       struct NVTable_maps *tbl_app,
			       struct NVTable_maps *tbl_over)
 {

	size_t len_to_write, write_count;
	off_t write_offset;
	instrumentation_type get_dr_mmap_time, copy_appendwrite_time, clear_dr_time, swap_extents_time;
	
	// Increment counter for append
	_nvp_wr_extended++;
	num_memcpy_write++;
	num_append_write++;	
	DEBUG("Request write length %li will extend file. "
	      "(filelen=%li, offset=%li, count=%li, extension=%li)\n",
	      count, nvf->node->length, offset, count, extension);		
	len_to_write = count;
	write_count = 0;
	write_offset = offset;
	DEBUG_FILE("%s: requesting write of size %lu, offset = %lu. FD = %d\n", __func__, count, offset, nvf->fd);
	unsigned long mmap_addr;
	off_t offset_within_mmap, write_offset_wrt_true_length;
	size_t extent_length, extension_with_node_length;
	instrumentation_type append_log_entry_time;
	extension_with_node_length = 0;

 get_addr:	
	/* This is used mostly to check if the write is not an append,
	 * but is way beyond the length of the file. 
	 */
	write_offset_wrt_true_length = write_offset - nvf->node->true_length;
	DEBUG_FILE("%s: write offset = %lu, true length = %lu\n", __func__, write_offset, nvf->node->true_length);
	// The address to perform the memcpy to is got from this function. 
	START_TIMING(get_dr_mmap_t, get_dr_mmap_time);
	
	nvp_get_dr_mmap_address(nvf, write_offset_wrt_true_length, len_to_write,
				write_count, &mmap_addr, &offset_within_mmap,
				&extent_length, wr_lock, cpuid, 1, tbl_app, tbl_over);
	DEBUG_FILE("%s: extent_length = %lu, len_to_write = %lu\n",
		   __func__, extent_length, len_to_write);
	
	END_TIMING(get_dr_mmap_t, get_dr_mmap_time);
	
	DEBUG_FILE("%s: ### EXTENT_LENGTH >= LEN_TO_WRITE extent_length = %lu, len_to_write = %lu\n", __func__,
		   extent_length, len_to_write);
	if (extent_length < len_to_write) {
		nvf->node->dr_info.dr_offset_end -= extent_length;
		size_t len_swapped = swap_extents(nvf, nvf->node->true_length);			
		off_t offset_in_page = 0;
		nvf->node->true_length = nvf->node->length;
		if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
			nvf->node->is_large_file = 1;
		START_TIMING(clear_dr_t, clear_dr_time);
		DEBUG_FILE("%s: EXTENT_LENGTH < LEN_TO_WRITE, EXTENT FD = %d, extent_length = %lu, len_to_write = %lu\n",
			   __func__,
			   nvf->node->dr_info.dr_fd,
			   extent_length,
			   len_to_write);
#if BG_CLEANING
		change_dr_mmap(nvf->node, 0);
#else
		create_dr_mmap(nvf->node, 0);
#endif
		END_TIMING(clear_dr_t, clear_dr_time);

		offset_in_page = nvf->node->true_length % MMAP_PAGE_SIZE;
		if (offset_in_page != 0 && nvf->node->dr_info.valid_offset < DR_SIZE) {
			nvf->node->dr_info.valid_offset += offset_in_page;
			nvf->node->dr_info.dr_offset_start = DR_SIZE;
			nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;
		}
		if (nvf->node->dr_info.valid_offset == DR_SIZE) {
			nvf->node->dr_info.valid_offset = DR_SIZE;
			nvf->node->dr_info.dr_offset_start = DR_SIZE;
			nvf->node->dr_info.dr_offset_end = DR_SIZE;
		}
		DEBUG_FILE("%s: RECEIVED OTHER EXTENT. dr fd = %d, dr addr = %p, dr v.o = %lu, dr start off = %lu, dr end off = %lu\n",
			   __func__, nvf->node->dr_info.dr_fd, nvf->node->dr_info.start_addr, nvf->node->dr_info.valid_offset,
			   nvf->node->dr_info.dr_offset_start, nvf->node->dr_info.dr_offset_end);

		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
		DEBUG_FILE("%s: Cleared mmap\n", __func__);
		goto get_addr;
	}
	if (extent_length > len_to_write)
		extent_length = len_to_write;
	if((extent_length + write_offset) > nvf->node->length)
		extension_with_node_length = extent_length + write_offset - nvf->node->length;
	
	if ((mmap_addr % MMAP_PAGE_SIZE) != (nvf->node->length % MMAP_PAGE_SIZE))
		assert(0);
	
	nvf->node->length += extension_with_node_length;
	
	memcpy_write_size += extent_length;
	append_write_size += extent_length;
	
	if (!wr_lock) {
		if (tbl_over != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);	
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
	}
	
#if SYSCALL_APPENDS

	offset_within_mmap = write_offset - nvf->node->true_length;
	_nvp_fileops->PWRITE(nvf->node->dr_info.dr_fd, buf, extent_length, offset_within_mmap);
	_nvp_fileops->FSYNC(nvf->fd);

#else // SYSCALL APPENDS

	// Write to anonymous DRAM. No dirty tracking to be performed here. 
	START_TIMING(copy_appendwrite_t, copy_appendwrite_time);

#if NON_TEMPORAL_WRITES

	DEBUG_FILE("%s: memcpy args: buf = %p, mmap_addr = %p, length = %lu. File off = %lld. Inode = %lu\n", __func__, buf, (void *) mmap_addr, extent_length, write_offset, nvf->node->serialno);
	if(MEMCPY_NON_TEMPORAL((char *)mmap_addr, buf, extent_length) == NULL) {
		printf("%s: non-temporal memcpy failed\n", __func__);
		fflush(NULL);
		assert(0);
	}
	//_mm_sfence();
	//num_mfence++;
	num_write_nontemporal++;
	non_temporal_write_size += extent_length;

#else // NON TEMPORAL WRITES

	if(FSYNC_MEMCPY((char *)mmap_addr, buf, extent_length) == NULL) {
		printf("%s: non-temporal memcpy failed\n", __func__);
		fflush(NULL);
		assert(0);
	}

#endif // NON_TEMPORAL_WRITES

#if NVM_DELAY

	perfmodel_add_delay(0, extent_length);

#endif // NVM_DELAY

	END_TIMING(copy_appendwrite_t, copy_appendwrite_time);
	
	if (tbl_over != NULL)	{
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
	}
	if (tbl_app != NULL) {
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
	}
	if (!wr_lock) {
		NVP_UNLOCK_NODE_RD(nvf, cpuid);	
	} else {
		NVP_UNLOCK_NODE_WR(nvf);
	}
	NVP_UNLOCK_FD_RD(nvf, cpuid);
	// Log the append

#if !POSIX_ENABLED
	
	START_TIMING(append_log_entry_t, append_log_entry_time);
	persist_append_entry(nvf->node->serialno,
			     nvf->node->dr_info.dr_serialno,
			     offset,
			     offset_within_mmap,
			     extent_length);
	END_TIMING(append_log_entry_t, append_log_entry_time);
#endif
	
#endif // SYSCALL APPENDS
	
	len_to_write -= extent_length;
	write_offset += extent_length;
	write_count  += extent_length;
	buf += extent_length;			

	DEBUG_FILE("%s: Returning write count = %lu. FD = %d\n", __func__, write_count, nvf->fd);
	return write_count;
}

RETT_PWRITE _nvp_do_pwrite(INTF_PWRITE,
			   int wr_lock,
			   int cpuid,
			   struct NVFile *nvf,
			   struct NVTable_maps *tbl_app,
			   struct NVTable_maps *tbl_over)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	off_t write_offset, offset_within_mmap;
	size_t write_count, extent_length;
	size_t posix_write;
	unsigned long mmap_addr = 0;
	unsigned long bitmap_root = 0;
	uint64_t extendFileReturn;
	instrumentation_type appends_time, read_tbl_mmap_time, copy_overwrite_time, get_dr_mmap_time,
		append_log_entry_time, clear_dr_time, insert_tbl_mmap_time;
	DEBUG_FILE("_nvp_do_pwrite. fd = %d, offset = %lu, count = %lu\n", file, offset, count);		
	_nvp_wr_total++;
	
	 SANITYCHECKNVF(nvf);	
	 if(UNLIKELY(!nvf->canWrite)) {
		 DEBUG("FD not open for writing: %i\n", file);
		 errno = EBADF;
		 if (tbl_over != NULL)	{
			 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		 }
		 if (tbl_app != NULL) {
			 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		 }
		 NVP_UNLOCK_NODE_RD(nvf, cpuid);
		 NVP_UNLOCK_FD_RD(nvf, cpuid);
		 return -1;
	 }
	 if(nvf->aligned)
		 {
			 DEBUG("This write must be aligned.  Checking alignment.\n");
			 if(UNLIKELY(count % 512))
				 {
					 DEBUG("count is not aligned to 512 (count was %li)\n",
					       count);
					 errno = EINVAL;
					 if (tbl_over != NULL)	{
						 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
					 }
					 if (tbl_app != NULL) {
						 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
					 }
					 NVP_UNLOCK_NODE_RD(nvf, cpuid);
					 NVP_UNLOCK_FD_RD(nvf, cpuid);
					 return -1;
				 }
			 if(UNLIKELY(offset % 512))
				 {
					 DEBUG("offset was not aligned to 512 "
					       "(offset was %li)\n", offset);
					 errno = EINVAL;
					 if (tbl_over != NULL)	{
						 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
					 }
					 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);	
					 NVP_UNLOCK_NODE_RD(nvf, cpuid);
					 NVP_UNLOCK_FD_RD(nvf, cpuid);
					 return -1;
				 }

			 if(UNLIKELY(((long long int)buf & (512-1)) != 0))
				 {
					 DEBUG("buffer was not aligned to 512 (buffer was %p, "
					       "mod 512 = %li)\n", buf,
					       (long long int)buf % 512);
					 errno = EINVAL;
					 if (tbl_over != NULL)	{
						 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
					 }
					 if (tbl_app != NULL) {
						 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
					 }
					 NVP_UNLOCK_NODE_RD(nvf, cpuid);
					 NVP_UNLOCK_FD_RD(nvf, cpuid);
					 return -1;
				 }
		 }
	 if(nvf->append)
		 {
			 DEBUG("this fd (%i) is O_APPEND; setting offset from the "
			       "passed value (%li) to the end of the file (%li) "
			       "prior to writing anything\n", nvf->fd, offset,
			       nvf->node->length);
			 offset = nvf->node->length;
		 }

	 ssize_t len_to_write;
	 ssize_t extension_with_read_length;
	 DEBUG("time for a Pwrite. file length %li, offset %li, extension %li, count %li\n", nvf->node->length, offset, extension, count);

	 len_to_write = count;
		
	 SANITYCHECK(nvf->valid);
	 SANITYCHECK(nvf->node != NULL);
	 SANITYCHECK(buf > 0);
	 SANITYCHECK(count >= 0);

	 write_count = 0;
	 write_offset = offset;		

	 if (write_offset >= nvf->node->length + 1) {
		 DEBUG_FILE("%s: Hole getting created. Doing Write system call\n", __func__);
		 posix_write = _nvp_fileops->PWRITE(file, buf, count, write_offset);
		 _nvp_fileops->FSYNC(file);
		 num_posix_write++;
		 posix_write_size += posix_write;
		 if (!wr_lock) {
			 if (tbl_over != NULL)	{
				 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			 }
			 if (tbl_app != NULL) {
				 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			 }
			 NVP_UNLOCK_NODE_RD(nvf, cpuid);
			 NVP_LOCK_NODE_WR(nvf);
			 if (tbl_app != NULL) {
				 TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
			 }
			 if (tbl_over != NULL)	{
				 TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
			 }
		 }
		 if (write_offset + count <= nvf->node->length) {
			 DEBUG_FILE("%s: offset fault. Offset of write = %lu, count = %lu, node length = %lu\n", __func__, write_offset, count, nvf->node->length);
			 assert(0);
		 }			

		 nvf->node->length = write_offset + count;
		 nvf->node->true_length = nvf->node->length;		
		 if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
			 nvf->node->is_large_file = 1;

		 if (tbl_over != NULL)	{
			 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		 }
		 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		 NVP_UNLOCK_NODE_WR(nvf);
		 NVP_UNLOCK_FD_RD(nvf, cpuid);
		 return posix_write;
	 }

	 if (write_offset == nvf->node->length)
		 goto appends;

	 if (write_offset >= nvf->node->true_length) {
		 MSG("%s: write_offset = %lu, true_length = %lu\n", __func__, write_offset, nvf->node->true_length);
		 assert(0);
	 }

#if DATA_JOURNALING_ENABLED	 
	 
	 if (tbl_over != NULL)	{
		 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);	
		 TBL_ENTRY_LOCK_WR(tbl_over);
	 }
	
	 // Get the file backed mmap address to which the write is to be performed. 
 get_addr:	
	 START_TIMING(get_dr_mmap_t, get_dr_mmap_time);		

	 nvp_get_over_dr_address(nvf, write_offset, len_to_write,
				 &mmap_addr, &offset_within_mmap,
				 &extent_length, wr_lock, cpuid,
				 tbl_app, tbl_over);
	 DEBUG_FILE("%s: extent_length = %lu, len_to_write = %lu\n",
		    __func__, extent_length, len_to_write);		
	 END_TIMING(get_dr_mmap_t, get_dr_mmap_time);

	 if (extent_length < len_to_write) {
		 //size_t len_swapped = swap_extents(nvf, nvf->node->true_length);		
		 off_t offset_in_page = 0;
		 START_TIMING(clear_dr_t, clear_dr_time);
		 DEBUG_FILE("%s: EXTENT_LENGTH < LEN_TO_WRITE, EXTENT FD = %d, extent_length = %lu, len_to_write = %lu\n",
			    __func__,
			    nvf->node->dr_info.dr_fd,
			    extent_length,
			    len_to_write);
#if BG_CLEANING
		 change_dr_mmap(nvf->node, 1);
#else
		 create_dr_mmap(nvf->node, 1);
#endif
		 END_TIMING(clear_dr_t, clear_dr_time);

		 DEBUG_FILE("%s: RECEIVED OTHER EXTENT. dr over fd = %d, dr over addr = %p, dr over start off = %lu, dr over end off = %lu\n", __func__, nvf->node->dr_over_info.dr_fd, nvf->node->dr_over_info.start_addr, nvf->node->dr_over_info.dr_offset_start, nvf->node->dr_over_info.dr_offset_end);

		 if (tbl_over != NULL)	{
			 TBL_ENTRY_UNLOCK_WR(tbl_over);
		 }
		 TBL_ENTRY_UNLOCK_WR(tbl_app);
		 NVP_UNLOCK_NODE_WR(nvf);
		 NVP_LOCK_NODE_RD(nvf, cpuid);
		 if (tbl_app != NULL) {
			 TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		 }
		 if (tbl_over != NULL)	{
			 TBL_ENTRY_LOCK_WR(tbl_over);
		 }
		 DEBUG_FILE("%s: Cleared mmap\n", __func__);		 
		 goto get_addr;
	 }

	 
	if (tbl_over != NULL)	
		TBL_ENTRY_UNLOCK_WR(tbl_over);
	 TBL_ENTRY_UNLOCK_WR(tbl_app);
	 NVP_UNLOCK_NODE_WR(nvf);
	 NVP_LOCK_NODE_RD(nvf, cpuid);
	 if (tbl_app != NULL) {
		 TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	 }
	 if (tbl_over != NULL) {
		 TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
	 }
#else // DATA_JOURNALING_ENABLED

	 START_TIMING(read_tbl_mmap_t, read_tbl_mmap_time);
	 read_tbl_mmap_entry(nvf->node, write_offset,
			     len_to_write, &mmap_addr,
			     &extent_length, 1);
	 END_TIMING(read_tbl_mmap_t, read_tbl_mmap_time);
	 
	 if (mmap_addr == 0) {
		 extent_length = write_to_file_mmap(file, write_offset,
						    len_to_write, wr_lock,
						    cpuid, buf, 
						    nvf);
	 
		 goto post_write;	 
	 }

#endif // DATA_JOURNALING_ENABLED
	 
	 if (extent_length > len_to_write)
		 extent_length = len_to_write;
		
	 // The write is performed to file backed mmap			
	 START_TIMING(copy_overwrite_t, copy_overwrite_time);

#if NON_TEMPORAL_WRITES
	 
	 DEBUG_FILE("%s: memcpy args: buf = %p, mmap_addr = %p, length = %lu. File off = %lld. Inode = %lu\n", __func__, buf, (void *) mmap_addr, extent_length, write_offset, nvf->node->serialno);

	 if(MEMCPY_NON_TEMPORAL((char *)mmap_addr, buf, extent_length) == NULL) {
		 printf("%s: non-temporal memcpy failed\n", __func__);
		 fflush(NULL);
		 assert(0);
	 }
	 _mm_sfence();
	 num_mfence++;
	 num_write_nontemporal++;
	 non_temporal_write_size += extent_length;			

#else //NON_TEMPORAL_WRITES

	 if(FSYNC_MEMCPY((char *)mmap_addr, buf, extent_length) != (char *)mmap_addr) {
		 printf("%s: memcpy failed\n", __func__);
		 fflush(NULL);
		 assert(0);
	 }

#if DIRTY_TRACKING

	 modifyBmap((struct merkleBtreeNode *)bitmap_root, offset_within_mmap, extent_length);

#endif //DIRTY_TRACKING

	 num_memcpy_write++;

#endif //NON_TEMPORAL_WRITES		

#if NVM_DELAY
	 perfmodel_add_delay(0, extent_length);
#endif

	 END_TIMING(copy_overwrite_t, copy_overwrite_time);

#if DATA_JOURNALING_ENABLED
	 
	 START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
	 insert_over_tbl_mmap_entry(nvf->node,
				    write_offset,
				    offset_within_mmap,
				    extent_length,
				    mmap_addr);
	 END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);

#endif // DATA_JOURNALING_ENABLED
	 
 post_write:
	 memcpy_write_size += extent_length;
	 len_to_write -= extent_length;
	 write_offset += extent_length;
	 write_count  += extent_length;
	 buf += extent_length;
		
	 if (tbl_over != NULL)	{
		 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
	 }
	 if (tbl_app != NULL) {
		 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
	 }
	 NVP_UNLOCK_NODE_RD(nvf, cpuid);
	 NVP_UNLOCK_FD_RD(nvf, cpuid);

#if DATA_JOURNALING_ENABLED
	 
	 START_TIMING(append_log_entry_t, append_log_entry_time);
	 persist_append_entry(nvf->node->serialno,
			      nvf->node->dr_over_info.dr_serialno,
			      write_offset,
			      offset_within_mmap,
			      extent_length);
	 END_TIMING(append_log_entry_t, append_log_entry_time);

#endif // DATA_JOURNALING_ENABLED
	 
	 return write_count;
	 
	 // If we need to append data, we should call _nvp_extend_write to write to anonymous mmap. 
 appends:		
	 START_TIMING(appends_t, appends_time);
	 extendFileReturn = _nvp_extend_write(file, buf,
					      len_to_write,
					      write_offset,
					      wr_lock, cpuid,
					      nvf,
					      tbl_app,
					      tbl_over);
	 END_TIMING(appends_t, appends_time);
	 len_to_write -= extendFileReturn;
	 write_count += extendFileReturn;
	 write_offset += extendFileReturn;		 
	 buf += extendFileReturn;			
	
	 DEBUG("About to return from _nvp_PWRITE with ret val %li.  file len: "
		 "%li, file off: %li, map len: %li, node %p\n",
		 count, nvf->node->length, nvf->offset,
		 nvf->node->maplength, nvf->node);
	 return write_count;
 }

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

RETT_SEEK64 _nvp_do_seek64(INTF_SEEK64, struct NVFile *nvf)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("_nvp_do_seek64\n");

	//struct NVFile* nvf = &_nvp_fd_lookup[file];
	
	DEBUG("_nvp_do_seek64: file len %li, map len %li, current offset %li, "
		"requested offset %li with whence %li\n", 
		nvf->node->length, nvf->node->maplength, *nvf->offset,
		offset, whence);

	switch(whence)
	{
		case SEEK_SET:
			if(offset < 0)
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) = offset ;
			//if(offset == 0)
				//INITIALIZE_TIMER();
			return *(nvf->offset);

		case SEEK_CUR:
			if((*(nvf->offset) + offset) < 0)
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) += offset ;
			return *(nvf->offset);

		case SEEK_END:
			if( nvf->node->length + offset < 0 )
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}

			*(nvf->offset) = nvf->node->length + offset;
			return *(nvf->offset);

		default:
			DEBUG("Invalid whence parameter.\n");
			errno = EINVAL;
			return -1;
	}
     
	assert(0); // unreachable
	return -1;
}

static ssize_t _nvp_check_read_size_valid(size_t count)
{ 
	if(count == 0)
	{
		DEBUG("Requested a read of 0 length.  No problem\n");
		return 0;
	}
	else if(count < 0)
	{
		DEBUG("Requested read of negative bytes (%li)\n", count);
		errno = EINVAL;
		return -1;
	}

	return count;
}

static ssize_t _nvp_check_write_size_valid(size_t count)
{
	if(count == 0)
	{
		DEBUG("Requested a write of 0 bytes.  No problem\n");
		return 0;
	}

	if(((signed long long int)count) < 0)
	{
		DEBUG("Requested a write of %li < 0 bytes.\n",
			(signed long long int)count);
		errno = EINVAL;
		return -1;
	}

	return count;
}

/* ========================== POSIX API methods =========================== */


RETT_CLOSE _nvp_REAL_CLOSE(INTF_CLOSE, ino_t serialno, int async_file_closing) {

	RETT_CLOSE result;
	CHECK_RESOLVE_FILEOPS(_nvp_);
	instrumentation_type node_lookup_lock_time, nvnode_lock_time, close_syscall_time,
		copy_to_dr_pool_time, copy_to_mmap_cache_time, give_up_node_time;
	int cpuid;
	int node_list_idx;
		
	if (file < 0)
		return -1;
	
	struct NVFile* nvf = &_nvp_fd_lookup[file];
	num_close++;	
	if (nvf->posix) {
		nvf->valid = 0;
		nvf->posix = 0;
		NVP_LOCK_NODE_WR(nvf);
		nvf->node->reference--;
		NVP_UNLOCK_NODE_WR(nvf);
		if (nvf->node->reference == 0) {
			nvf->node->serialno = 0;
			int index = nvf->serialno % 1024;
			_nvp_ino_lookup[index] = 0;
		}
		nvf->serialno = 0;
		DEBUG("Call posix CLOSE for fd %d\n", nvf->fd);
		result = _nvp_fileops->CLOSE(CALL_CLOSE);
		return result;
	}

	DEBUG_FILE("_nvp_REAL_CLOSE(%i): Ref count = %d\n", file, nvf->node->reference);	
	DEBUG_FILE("%s: Calling fsync flush on fsync\n", __func__);
	cpuid = GET_CPUID();
#if !SYSCALL_APPENDS
	fsync_flush_on_fsync(nvf, cpuid, 1, 0);	
#endif
	/* 
	 * nvf->node->reference contains how many threads have this file open. 
	 */
	node_list_idx = nvf->node->free_list_idx;

	pthread_spin_lock(&node_lookup_lock[node_list_idx]);

	if(nvf->valid == 0) {
		pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
		return -1;
	}
	if(nvf->node->reference <= 0) {
		pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
		return -1;
	}
	if(nvf->node->serialno != serialno) {
		pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
		return -1;
	}	

	NVP_LOCK_NODE_WR(nvf);
	nvf->node->reference--;
	NVP_UNLOCK_NODE_WR(nvf);

	if (nvf->node->reference == 0) {
		nvf->node->serialno = 0;				
		push_in_stack(1, 0, nvf->node->index_in_free_list, node_list_idx);
	}		
	if (async_file_closing) {
		nvf->node->async_file_close = 1;
	}
	pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
		
	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);	
	NVP_LOCK_NODE_WR(nvf);

	// setting valid to 0 means that this fd is not open. So can be used for a subsequent open of same or different file.
	if(nvf->valid == 0) {
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		return -1;
	}
	if(nvf->node->reference < 0) {
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		return -1;
	}
	if(nvf->serialno != serialno) {
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		return -1;
	}

	nvf->valid = 0;
	if (nvf->node->reference == 0) {
		nvp_add_to_inode_mapping(nvf->node, nvf->serialno);
		nvf->node->backup_serialno = 0;		
		int index = nvf->serialno % 1024;
		_nvp_ino_lookup[index] = 0;
		DEBUG("Close Cleanup node for %d\n", file);
		if(nvf->node->dr_info.start_addr != 0 || nvf->node->dr_over_info.start_addr != 0) {
			nvp_transfer_to_free_dr_pool(nvf->node);
		}
		nvf->node->async_file_close = 0;
		nvp_cleanup_node(nvf->node, 0, 0);
	}
	nvf->serialno = 0;

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf);

	// close() system call of the file is done here. 
	//START_TIMING(close_syscall_t, close_syscall_time);
	result = _nvp_fileops->CLOSE(CALL_CLOSE);
	return result;
}

RETT_OPEN _nvp_OPEN(INTF_OPEN)
{	
	int result;	
	instrumentation_type open_time, clf_lock_time, nvnode_lock_time;
#if BG_CLOSING
	int closed_filedesc = -1, fd = -1, hash_index = -1;
#if SEQ_LIST || RAND_LIST
	struct ClosedFiles *clf = NULL;
#else // SEQ_LIST || RAND_LIST	
	struct InodeClosedFile *tbl = NULL;
#endif // SEQ_LIST || RAND_LIST
#endif // BG_CLOSING

	START_TIMING(open_t, open_time);
	GLOBAL_LOCK_WR();
	
#if PASS_THROUGH_CALLS
	num_open++;
	if (FLAGS_INCLUDE(oflag,O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);		
		// Open system call is done here  
		result = _nvp_fileops->OPEN(path, oflag & (~O_APPEND), mode);
	} else { 
		result = _nvp_fileops->OPEN(path, oflag & (~O_APPEND));
	}	
	if(result<0)
	{
		DEBUG("_nvp_OPEN->%s_OPEN failed: %s\n",
			_nvp_fileops->name, strerror(errno));

		GLOBAL_UNLOCK_WR();
		END_TIMING(open_t, open_time);
		return result;
	}	

	GLOBAL_UNLOCK_WR();
	END_TIMING(open_t, open_time);
	return result;
#endif // PASS_THROUGH_CALLS
	
	CHECK_RESOLVE_FILEOPS(_nvp_);
	
	if(path==NULL) {
		DEBUG("Invalid path.\n");
		errno = EINVAL;
		END_TIMING(open_t, open_time);

		GLOBAL_UNLOCK_WR();
		return -1;
	}

        DEBUG_FILE("_nvp_OPEN(%s)\n", path);
	num_open++;
	
	DEBUG("Attempting to _nvp_OPEN the file \"%s\" with the following "
		"flags (0x%X): ", path, oflag);
	
	/*
	 *  Print all the flags passed to open() 
	 */
	if((oflag&O_RDWR)||((oflag&O_RDONLY)&&(oflag&O_WRONLY))) {
		DEBUG_P("O_RDWR ");
	} else if(FLAGS_INCLUDE(oflag,O_WRONLY)) {
		DEBUG_P("O_WRONLY ");
	} else if(FLAGS_INCLUDE(oflag, O_RDONLY)) {
		DEBUG_P("O_RDONLY ");
	}
	DUMP_FLAGS(oflag,O_APPEND);
	DUMP_FLAGS(oflag,O_CREAT);
	DUMP_FLAGS(oflag,O_TRUNC);
	DUMP_FLAGS(oflag,O_EXCL);
	DUMP_FLAGS(oflag,O_SYNC);
	DUMP_FLAGS(oflag,O_ASYNC);
	DUMP_FLAGS(oflag,O_DSYNC);
	DUMP_FLAGS(oflag,O_FSYNC);
	DUMP_FLAGS(oflag,O_RSYNC);
	DUMP_FLAGS(oflag,O_NOCTTY);
	DUMP_FLAGS(oflag,O_NDELAY);
	DUMP_FLAGS(oflag,O_NONBLOCK);
	DUMP_FLAGS(oflag,O_DIRECTORY);
	DUMP_FLAGS(oflag,O_LARGEFILE);
	DUMP_FLAGS(oflag,O_NOATIME);
	DUMP_FLAGS(oflag,O_DIRECT);
	DUMP_FLAGS(oflag,O_NOFOLLOW);
	DEBUG_P("\n");

	struct stat file_st;		
	// Initialize NVNode 
	struct NVNode* node = NULL;

#if BG_CLOSING		
	if (async_close_enable) {
		if(num_files_closed >= 800 || (dr_mem_closed_files >= ((5ULL) * 1024 * 1024 * 1024))) {
			ASYNC_CLOSING = 0;
			checkAndActivateBgThread();		
		}
	}
#endif // BG_CLOSING
	
	/*
	 * If creation of the file is involved, 3 parameters must be passed to open(). 
	 * Otherwise, 2 parameters must be passed
	 */   
	if (FLAGS_INCLUDE(oflag,O_CREAT))
	{
		va_list arg;
		instrumentation_type op_log_entry_time;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);		
		// Open system call is done here  
		DEBUG_FILE("%s: calling open with path = %s, flag = %d, mode = %d, ino addr = %p, ino size addr = %p\n", __func__, path, oflag, mode, &file_st.st_ino, &file_st.st_size);
		result = syscall(334, path, oflag & (~O_APPEND), mode, &file_st.st_ino, &file_st.st_size);
#if !POSIX_ENABLED
		if (result >= 0) {
			START_TIMING(op_log_entry_t, op_log_entry_time);
			persist_op_entry(LOG_FILE_CREATE,
					 path,
					 NULL,
					 mode,
					 oflag);
			END_TIMING(op_log_entry_t, op_log_entry_time);
		}
#endif
		//result = _nvp_fileops->OPEN(path, oflag & (~O_APPEND), mode);
	} else { 
		DEBUG_FILE("%s: calling open with path = %s, flag = %d, mode = 0666, ino addr = %p, ino size addr = %p\n", __func__, path, oflag, &file_st.st_ino, &file_st.st_size);
		result = syscall(334, path, oflag & (~O_APPEND), 0666, &file_st.st_ino, &file_st.st_size);
	}	
	if(result<0)
	{
		DEBUG("_nvp_OPEN->%s_OPEN failed: %s\n",
			_nvp_fileops->name, strerror(errno));
		END_TIMING(open_t, open_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}	
        DEBUG_FILE("_nvp_OPEN(%s), fd = %d\n", path, result);
	SANITYCHECK(&_nvp_fd_lookup[result] != NULL);	
	struct NVFile* nvf = NULL;

#if BG_CLOSING	
	if (async_close_enable)
		checkAndActivateBgThread();
	GLOBAL_LOCK_CLOSE_WR();
	hash_index = file_st.st_ino % TOTAL_CLOSED_INODES;
#if SEQ_LIST || RAND_LIST
	clf = &_nvp_closed_files[hash_index];

	LRU_NODE_LOCK_WR(clf);

	fd = remove_from_seq_list_hash(clf, file_st.st_ino);
#else // SEQ_LIST || RAND_LIST
	tbl = &inode_to_closed_file[hash_index];
	NVP_LOCK_HASH_TABLE_WR(tbl);	
	fd = remove_from_lru_list_hash(file_st.st_ino, 0);
#endif // SEQ_LIST || RAND_LIST		
	if(fd >= 0) {
		if ((oflag & O_RDWR) || FLAGS_INCLUDE(oflag, O_RDONLY)) {	
			num_close++;
			closed_filedesc = fd;			
			__atomic_fetch_sub(&num_files_closed, 1, __ATOMIC_SEQ_CST);
#if SEQ_LIST || RAND_LIST
			LRU_NODE_UNLOCK_WR(clf);
#else // SEQ_LIST || RAND_LIST
			NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif // SEQ_LIST || RAND_LIST
			GLOBAL_UNLOCK_CLOSE_WR();
			
			_nvp_fileops->CLOSE(result);			
			result = closed_filedesc;
			nvf = &_nvp_fd_lookup[result];			
			node = nvf->node;
			__atomic_fetch_sub(&dr_mem_closed_files, nvf->node->dr_mem_used, __ATOMIC_SEQ_CST);
			NVP_LOCK_FD_WR(nvf);
			NVP_LOCK_NODE_WR(nvf);
			nvf->valid = 0;			
			goto initialize_nvf;
		}
	}

#if SEQ_LIST || RAND_LIST
	LRU_NODE_UNLOCK_WR(clf);
#else // SEQ_LIST || RAND_LIST
	NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif // SEQ_LIST || RAND_LIST
	GLOBAL_UNLOCK_CLOSE_WR();	
#endif	// BG_CLOSING
	// Retrieving the NVFile corresponding to the file descriptor returned by open() system call
	nvf = &_nvp_fd_lookup[result];	
	DEBUG("_nvp_OPEN succeeded for path %s: fd %i returned. "
		"filling in file info\n", path, result);

	NVP_LOCK_FD_WR(nvf);
	
	// Check if the file descriptor is already open. If open, something is wrong and return error
	if(nvf->valid)
	{
		ERROR("There is already a file open with that FD (%i)!\n", result);
		assert(0);
		END_TIMING(open_t, open_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	/*
	 * NVNode is retrieved here. Keeping this check because in quill it was required. Not necessary in Ledger
	 */
	if(node == NULL)
	{
		// Find or allocate a NVNode
		node = nvp_get_node(path, &file_st, result);
		NVP_LOCK_WR(node->lock);
	}

#if BG_CLOSING
 initialize_nvf:
#endif // BG_CLOSING
	nvf->fd = result;	
	nvf->node = node;
	nvf->posix = 0;
	nvf->serialno = file_st.st_ino;	
	/* 
	 * Write the entry of this file into the global inode number struct. 
	 * This contains the fd of the thread that first 
	 * opened this file. 
	 */
	// Set FD permissions
	if((oflag & O_RDWR)||((oflag & O_RDONLY) && (oflag & O_WRONLY))) {
		DEBUG("oflag (%i) specifies O_RDWR for fd %i\n", oflag, result);
		nvf->canRead = 1;
		nvf->canWrite = 1;
	} else if(oflag&O_WRONLY) {

#if WORKLOAD_TAR | WORKLOAD_GIT | WORKLOAD_RSYNC

		nvf->posix = 0;
		nvf->canRead = 1;
		nvf->canWrite = 1;

#else // WORKLOAD_TAR

		MSG("File %s is opened O_WRONLY.\n", path);
		MSG("Does not support mmap, use posix instead.\n");
		nvf->posix = 1;
		nvf->canRead = 0;		
		nvf->canWrite = 1;
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		END_TIMING(open_t, open_time);
		GLOBAL_UNLOCK_WR();
		return nvf->fd;

#endif // WORKLOAD_TAR

	} else if(FLAGS_INCLUDE(oflag, O_RDONLY)) {
		DEBUG("oflag (%i) specifies O_RDONLY for fd %i\n",
			oflag, result);
		nvf->canRead = 1;
		nvf->canWrite = 0;
	} else {
		DEBUG("File permissions don't include read or write!\n");
		nvf->canRead = 0;
		nvf->canWrite = 0;		
		assert(0);
	}
	
	if(FLAGS_INCLUDE(oflag, O_APPEND)) {
		nvf->append = 1;
	} else {
		nvf->append = 0;
	}

	SANITYCHECK(nvf->node != NULL);
	if(FLAGS_INCLUDE(oflag, O_TRUNC) && nvf->node->length)
	{
		DEBUG("We just opened a file with O_TRUNC that was already "
			"open with nonzero length %li.  Updating length.\n",
			nvf->node->length);
		nvf->node->length = 0;
	}
	nvf->posix = 0;
	nvf->debug = 0;
	
	/* This is a nasty workaround for FIO */
	if (path[0] == '/' && path[1] == 's'
			&& path[2] == 'y' && path[3] == 's') {
		nvf->posix = 1;
		MSG("A Posix Path: %s\n", path);
	}

	/* For BDB log file, workaround the fdsync issue */
	if (path[29] == 'l' && path[30] == 'o' && path[31] == 'g') {
		nvf->debug = 1;
	}

	nvf->offset = (size_t*)calloc(1, sizeof(int));
	*nvf->offset = 0;

	if(FLAGS_INCLUDE(oflag, O_DIRECT) && (DO_ALIGNMENT_CHECKS)) {
		nvf->aligned = 1;
	} else {
		nvf->aligned = 0;
	}

	nvf->valid = 1;
	
	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf);
	
	errno = 0;
	END_TIMING(open_t, open_time);

	GLOBAL_UNLOCK_WR();
	return nvf->fd;
}

RETT_MKNOD _nvp_MKNOD(INTF_MKNOD)
{
	RETT_MKNOD result = 0;
	instrumentation_type op_log_entry_time;
	
	result = _nvp_fileops->MKNOD(CALL_MKNOD);

#if !POSIX_ENABLED
	if (S_ISREG(mode)) {
		START_TIMING(op_log_entry_t, op_log_entry_time);
		persist_op_entry(LOG_FILE_CREATE,
				 path,
				 NULL,
				 mode,
				 0);		
		END_TIMING(op_log_entry_t, op_log_entry_time);
	}
#endif
	
	return result;
}

RETT_MKNODAT _nvp_MKNODAT(INTF_MKNODAT)
{
	RETT_MKNOD result = 0;

	result = _nvp_fileops->MKNODAT(CALL_MKNODAT);

	char new_path[256];
	int path_len = 0;
	instrumentation_type op_log_entry_time;
	
	if (S_ISREG(mode)) {
		if (dirfd == AT_FDCWD) {
			if (path[0] != '/') {
				if (getcwd(new_path, sizeof(new_path)) == NULL)
					assert(0);
				path_len = strlen(new_path);
				new_path[path_len] = '/';
				new_path[path_len+1] = '\0';
			
				if (strcat(new_path, path) != new_path)
					assert(0);
			} else {
				if (strcpy(new_path, path) == NULL)
					assert(0);
			}
		} else {
			char fd_str[256];				
			if (path[0] != '/') {
				sprintf(fd_str, "/proc/self/fd/%d", dirfd);
				if (readlink(fd_str, new_path, sizeof(new_path)) == -1)
					assert(0);
				path_len = strlen(new_path);
				new_path[path_len] = '/';
				new_path[path_len+1] = '\0';
				if (strcat(new_path, path) != new_path)
					assert(0);
			} else {
				if (strcpy(new_path, path) == NULL)
					assert(0);
			}
		}
	}

#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_FILE_CREATE,
			 new_path,
			 NULL,
			 mode,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;			  
}
 
#ifdef TRACE_FP_CALLS
RETT_FOPEN _nvp_FOPEN(INTF_FOPEN)
{
	int oflag = 0;
	int fd = -1;
	RETT_FOPEN fp = NULL;
	

#if PASS_THROUGH_CALLS
	num_open++;
	fp = _nvp_fileops->FOPEN(path, mode);
	return fp;
#endif // PASS_THROUGH_CALLS
	
	if (!strcmp(mode,"w+") || !strcmp(mode,"a+")) {
		oflag |= O_RDWR;
		oflag |= O_CREAT;
	}
	else if (!strcmp(mode,"r+"))
		oflag |= O_RDWR;
	else if (!strcmp(mode, "w") || !strcmp(mode, "a")) {
		oflag |= O_WRONLY;
		oflag |= O_CREAT;
	} else if (!strcmp(mode, "r"))
		oflag |= O_RDONLY;
	else {
		assert(0);
	}
	
	if (mode[0] == 'a') 
		oflag |= O_APPEND;

	if (FLAGS_INCLUDE(oflag,O_CREAT)) 
		fd = _nvp_OPEN(path, oflag, 0666);
	else
		fd = _nvp_OPEN(path, oflag);

	fp = fdopen(fd, mode);
	if (!fp) {
		printf("%s: fdopen failed! error = %s, fd = %d, mode = %s\n", __func__, strerror(errno), fd, mode);
		fflush(NULL);
		assert(0);
	}
	
	return fp;
}

RETT_FOPEN64 _nvp_FOPEN64(INTF_FOPEN64) {
	 //Do Nothing.
	return _nvp_FOPEN(CALL_FOPEN);
}
 
#endif

RETT_CLOSE _nvp_CLOSE(INTF_CLOSE)
{
	RETT_CLOSE result;
	ino_t serialno;
	struct NVFile* nvf = NULL;
	instrumentation_type close_time;

	START_TIMING(close_t, close_time);

	GLOBAL_LOCK_WR();
	DEBUG_FILE("_nvp_CLOSE(%i)\n", file);
	
#if PASS_THROUGH_CALLS	
	num_close++;
	result = _nvp_fileops->CLOSE(CALL_CLOSE);
	GLOBAL_UNLOCK_WR();
	END_TIMING(close_t, close_time);
	return result;	
#endif // PASS_THROUGH_CALLS
	
	if (!async_close_enable)
		goto sync_close_bg_enabled;
			
#if BG_CLOSING
	CHECK_RESOLVE_FILEOPS(_nvp_);
	instrumentation_type clf_lock_time;
	int previous_closed_filedesc = -1;
	ino_t previous_closed_serialno = 0, stale_serialno = 0;
	int cpuid, stale_fd = -1;
	int hash_index = -1;
#if SEQ_LIST || RAND_LIST
	struct ClosedFiles *clf = NULL;
#else //SEQ_LIST || RAND_LIST	
	struct InodeClosedFile *tbl = NULL;
#endif	//SEQ_LIST || RAND_LIST
		
	//num_close++;
	// Get the struct NVFile from the file descriptor
	
        nvf = &_nvp_fd_lookup[file];
	
	if (nvf->posix) {
		nvf->valid = 0;
		nvf->posix = 0;
		NVP_LOCK_NODE_WR(nvf);
		nvf->node->reference--;
		NVP_UNLOCK_NODE_WR(nvf);
		if (nvf->node->reference == 0) {
			nvf->node->serialno = 0;
			int index = nvf->serialno % 1024;
			_nvp_ino_lookup[index] = 0;
		}
		nvf->serialno = 0;
		DEBUG("Call posix CLOSE for fd %d\n", nvf->fd);
		result = _nvp_fileops->CLOSE(CALL_CLOSE);
		END_TIMING(close_t, close_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}
	
	serialno = nvf->node->serialno;	
	GLOBAL_LOCK_CLOSE_WR();

	hash_index = serialno % TOTAL_CLOSED_INODES;

#if SEQ_LIST || RAND_LIST
	clf = &_nvp_closed_files[hash_index];

	//START_TIMING(clf_lock_t, clf_lock_time);
	LRU_NODE_LOCK_WR(clf);
	//END_TIMING(clf_lock_t, clf_lock_time);
#else //SEQ_LIST || RAND_LIST
	tbl = &inode_to_closed_file[hash_index];
	NVP_LOCK_HASH_TABLE_WR(tbl);
#endif	//SEQ_LIST || RAND_LIST
	cpuid = GET_CPUID();
	NVP_LOCK_NODE_RD(nvf, cpuid);

	if(nvf->node->reference == 1) {
		NVP_UNLOCK_NODE_RD(nvf, cpuid);		
		__atomic_fetch_add(&dr_mem_closed_files, nvf->node->dr_mem_used, __ATOMIC_SEQ_CST);		
#if SEQ_LIST || RAND_LIST
		stale_fd = insert_in_seq_list(clf, &stale_serialno, file, serialno);
#else //SEQ_LIST || RAND_LIST
		stale_fd = insert_in_lru_list(file, serialno, &stale_serialno);
#endif	//SEQ_LIST || RAND_LIST			
		if(stale_fd >= 0 && stale_serialno > 0) {
			previous_closed_filedesc = stale_fd;
			previous_closed_serialno = stale_serialno;
		}		
		
		if(previous_closed_filedesc != -1) {
			_nvp_REAL_CLOSE(previous_closed_filedesc, previous_closed_serialno, 1);
		}
		else 
			__atomic_fetch_add(&num_files_closed, 1, __ATOMIC_SEQ_CST);
			
#if SEQ_LIST || RAND_LIST
		LRU_NODE_UNLOCK_WR(clf);
#else //SEQ_LIST || RAND_LIST
		NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif //SEQ_LIST || RAND_LIST		
		GLOBAL_UNLOCK_CLOSE_WR();			

		END_TIMING(close_t, close_time);
		GLOBAL_UNLOCK_WR();
		return 0;
	}

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
#if SEQ_LIST || RAND_LIST
	LRU_NODE_UNLOCK_WR(clf);
#else //SEQ_LIST || RAND_LIST
	NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif //SEQ_LIST || RAND_LIST
	GLOBAL_UNLOCK_CLOSE_WR();
#else //BG_CLOSING
	nvf = &_nvp_fd_lookup[file];
	serialno = nvf->node->serialno;	
#endif //BG_CLOSING
	result = _nvp_REAL_CLOSE(CALL_CLOSE, serialno, 0);	
	END_TIMING(close_t, close_time);
	GLOBAL_UNLOCK_WR();
	return result;	

 sync_close_bg_enabled:
	nvf = &_nvp_fd_lookup[file];

#if WORKLOAD_TAR | WORKLOAD_GIT | WORKLOAD_RSYNC

	if (nvf->node == NULL) {
		nvf->valid = 0;
		nvf->serialno = 0;
		result = _nvp_fileops->CLOSE(CALL_CLOSE);
		END_TIMING(close_t, close_time);
		return result;				
	}
	
#endif
	
	serialno = nvf->node->serialno;	
	result = _nvp_REAL_CLOSE(CALL_CLOSE, serialno, 0);	
	END_TIMING(close_t, close_time);
	GLOBAL_UNLOCK_WR();
	return result;		
}

RETT_OPENAT _nvp_OPENAT(INTF_OPENAT) {
	return _nvp_fileops->OPENAT(CALL_OPENAT);
}

RETT_EXECVE _nvp_EXECVE(INTF_EXECVE) {

	int exec_ledger_fd = -1, i = 0;
	unsigned long offset_in_map = 0;
	int pid = getpid();
	char exec_nvp_filename[BUF_SIZE];

	for (i = 0; i < 1024; i++) {
		if (_nvp_fd_lookup[i].offset != NULL)
			execve_fd_passing[i] = *(_nvp_fd_lookup[i].offset);
		else
			execve_fd_passing[i] = 0;
	}

	sprintf(exec_nvp_filename, "exec-ledger-%d", pid);
	exec_ledger_fd = shm_open(exec_nvp_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (exec_ledger_fd == -1) {
		printf("%s: %s\n", __func__, strerror(errno));
		assert(0);
	}

	int res = _nvp_fileops->FTRUNC64(exec_ledger_fd, (10*1024*1024));
	if (res == -1) {
		printf("%s: ftruncate failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	char *shm_area = mmap(NULL, 10*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, exec_ledger_fd, 0);
	if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(shm_area + offset_in_map, _nvp_fd_lookup, 1024 * sizeof(struct NVFile)) == NULL) {
		printf("%s: memcpy of fd lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(struct NVFile));

	if (memcpy(shm_area + offset_in_map, execve_fd_passing, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of execve offset failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));


	if (memcpy(shm_area + offset_in_map, _nvp_node_lookup[0], 1024*sizeof(struct NVNode)) == NULL) {
		printf("%s: memcpy of node lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024*sizeof(struct NVNode));

	if (memcpy(shm_area + offset_in_map, _nvp_ino_lookup, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of ino lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));

	if (memcpy(shm_area + offset_in_map, _nvp_free_node_list[0], 1024*sizeof(struct StackNode)) == NULL) {
		printf("%s: memcpy of free node list failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	nvp_free_dr_mmaps();
	offset_in_map += (1024 * sizeof(struct StackNode));
	
	return _nvp_fileops->EXECVE(CALL_EXECVE);
}

RETT_EXECVP _nvp_EXECVP(INTF_EXECVP) {

	int exec_ledger_fd = -1, i = 0;
	unsigned long offset_in_map = 0;
	int pid = getpid();
	char exec_nvp_filename[BUF_SIZE];

	for (i = 0; i < 1024; i++) {
		if (_nvp_fd_lookup[i].offset != NULL)
			execve_fd_passing[i] = *(_nvp_fd_lookup[i].offset);
		else
			execve_fd_passing[i] = 0;
	}

	for (i = 0; i < OPEN_MAX; i++) {
		if (_nvp_fd_lookup[i].node != NULL && _nvp_fd_lookup[i].valid)
			_nvp_FSYNC(_nvp_fd_lookup[i].fd);
	}
	
	sprintf(exec_nvp_filename, "exec-ledger-%d", pid);
	exec_ledger_fd = shm_open(exec_nvp_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (exec_ledger_fd == -1) {
		printf("%s: %s\n", __func__, strerror(errno));
		assert(0);
	}

	int res = _nvp_fileops->FTRUNC64(exec_ledger_fd, (10*1024*1024));
	if (res == -1) {
		printf("%s: ftruncate failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	char *shm_area = mmap(NULL, 10*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, exec_ledger_fd, 0);
	if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(shm_area + offset_in_map, _nvp_fd_lookup, 1024 * sizeof(struct NVFile)) == NULL) {
		printf("%s: memcpy of fd lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(struct NVFile));

	if (memcpy(shm_area + offset_in_map, execve_fd_passing, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of execve offset failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));
	
	
	if (memcpy(shm_area + offset_in_map, _nvp_node_lookup[0], 1024*sizeof(struct NVNode)) == NULL) {
		printf("%s: memcpy of node lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}
	
	offset_in_map += (1024*sizeof(struct NVNode));
	
	if (memcpy(shm_area + offset_in_map, _nvp_ino_lookup, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of ino lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));

	if (memcpy(shm_area + offset_in_map, _nvp_free_node_list[0], 1024*sizeof(struct StackNode)) == NULL) {
		printf("%s: memcpy of free node list failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(struct StackNode));

	nvp_free_dr_mmaps();
	return _nvp_fileops->EXECVP(CALL_EXECVP);
}

RETT_EXECV _nvp_EXECV(INTF_EXECV) {

	int exec_ledger_fd = -1, i = 0;
	unsigned long offset_in_map = 0;
	int pid = getpid();
	char exec_nvp_filename[BUF_SIZE];

	for (i = 0; i < 1024; i++) {
		if (_nvp_fd_lookup[i].offset != NULL)
			execve_fd_passing[i] = *(_nvp_fd_lookup[i].offset);
		else
			execve_fd_passing[i] = 0;
	}

	for (i = 0; i < OPEN_MAX; i++) {
		if (_nvp_fd_lookup[i].node != NULL && _nvp_fd_lookup[i].valid)
			_nvp_FSYNC(_nvp_fd_lookup[i].fd);
	}
	
	sprintf(exec_nvp_filename, "exec-ledger-%d", pid);
	exec_ledger_fd = shm_open(exec_nvp_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (exec_ledger_fd == -1) {
		printf("%s: %s\n", __func__, strerror(errno));
		assert(0);
	}

	int res = _nvp_fileops->FTRUNC64(exec_ledger_fd, (10*1024*1024));
	if (res == -1) {
		printf("%s: ftruncate failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	char *shm_area = mmap(NULL, 10*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, exec_ledger_fd, 0);
	if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(shm_area + offset_in_map, _nvp_fd_lookup, 1024 * sizeof(struct NVFile)) == NULL) {
		printf("%s: memcpy of fd lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(struct NVFile));

	if (memcpy(shm_area + offset_in_map, execve_fd_passing, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of execve offset failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));
	
	
	if (memcpy(shm_area + offset_in_map, _nvp_node_lookup[0], 1024*sizeof(struct NVNode)) == NULL) {
		printf("%s: memcpy of node lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}
	
	offset_in_map += (1024*sizeof(struct NVNode));
	
	if (memcpy(shm_area + offset_in_map, _nvp_ino_lookup, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of ino lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));

	if (memcpy(shm_area + offset_in_map, _nvp_free_node_list[0], 1024*sizeof(struct StackNode)) == NULL) {
		printf("%s: memcpy of free node list failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(struct StackNode));

	nvp_free_dr_mmaps();
	return _nvp_fileops->EXECV(CALL_EXECV);
}

#ifdef TRACE_FP_CALLS
RETT_FCLOSE _nvp_FCLOSE(INTF_FCLOSE)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	RETT_FCLOSE result;
	int fd = -1;
	
#if PASS_THROUGH_CALLS
	result = _nvp_fileops->FCLOSE(CALL_FCLOSE);	
	return result;
#endif
	
	fd = fileno(fp);
	result = _nvp_CLOSE(fd);
	return result;
}
#endif


#ifdef TRACE_FP_CALLS
RETT_FREAD _nvp_FREAD(INTF_FREAD)
{
	DEBUG_FILE("%s: start\n", __func__);
	DEBUG("_nvp_READ %d\n", fileno(fp));
	RETT_READ result;

#if PASS_THROUGH_CALLS
	num_read++;
	result = _nvp_fileops->FREAD(CALL_FREAD);
	return result;
#endif
	
	struct NVFile* nvf = &_nvp_fd_lookup[fileno(fp)];
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];
	
#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else // DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED
	
	if (nvf->posix) {
		DEBUG("Call posix READ for fd %d\n", nvf->fd);
		result = _nvp_fileops->FREAD(CALL_FREAD);
		read_size += result;
		num_posix_read++;
		posix_read_size += result;
		return result;
	}

	result = _nvp_check_read_size_valid(length);
	if (result <= 0) {
		return result;
	}

	int cpuid = GET_CPUID();

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);
	if (tbl_app != NULL) {
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	}
	if (tbl_over != NULL)  {
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);	
	}
	
	result = _nvp_do_pread(fileno(fp),
			       buf,
			       length*nmemb, 
			       __sync_fetch_and_add(nvf->offset, length),
			       0,
			       cpuid,
			       nvf,
			       tbl_app,
			       tbl_over);
	
	NVP_UNLOCK_NODE_RD(nvf, cpuid);

	if(result == length)	{
		DEBUG("PREAD succeeded: extending offset from %li to %li\n",
			*nvf->offset - result, *nvf->offset);
	}
	else if (result <= 0){
		DEBUG("_nvp_READ: PREAD failed; not changing offset. "
			"(returned %i)\n", result);		
		//assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length);
	} else {
		DEBUG("_nvp_READ: PREAD failed; Not fully read. "
			"(returned %i)\n", result);
		// assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length - result);
	}

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	num_read++;
	read_size += result;

	return result;
}
#endif

RETT_READ _nvp_READ(INTF_READ)
{
	DEBUG_FILE("_nvp_READ %d\n", file);
	num_read++;
	RETT_READ result;
	instrumentation_type read_time;

	START_TIMING(read_t, read_time);
	GLOBAL_LOCK_WR();
	
#if PASS_THROUGH_CALLS	
	num_read++;
	result = _nvp_fileops->READ(CALL_READ);
	GLOBAL_UNLOCK_WR();
	END_TIMING(read_t, read_time);
	return result;
#endif
	
	struct NVFile* nvf = &_nvp_fd_lookup[file];
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED
	
	if(nvf->posix) {
		DEBUG("Call posix READ for fd %d\n", nvf->fd);
		result = _nvp_fileops->READ(CALL_READ);
		read_size += result;
		num_posix_read++;
		posix_read_size += result;
		END_TIMING(read_t, read_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	result = _nvp_check_read_size_valid(length);
	if (result <= 0) {
		END_TIMING(read_t, read_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	int cpuid = GET_CPUID();

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_LOCK_NODE_RD(nvf, cpuid);
	if (tbl_app != NULL) {
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	}
	if (tbl_over != NULL)  {
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);	
	}
	
	result = _nvp_do_pread(CALL_READ,
			       __sync_fetch_and_add(nvf->offset, length),
			       0,
			       cpuid,
			       nvf,
			       tbl_app,
			       tbl_over);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);

	if(result == length)	{
		DEBUG("PREAD succeeded: extending offset from %li to %li\n",
			*nvf->offset - result, *nvf->offset);
	}
	else if (result <= 0){
		DEBUG("_nvp_READ: PREAD failed; not changing offset. "
			"(returned %i)\n", result);		
		//assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length);
	} else {
		DEBUG("_nvp_READ: PREAD failed; Not fully read. "
			"(returned %i)\n", result);
		// assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length - result);
	}

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	read_size += result;

	END_TIMING(read_t, read_time);
	DEBUG_FILE("_nvp_READ %d returns %lu\n", file, result);
	GLOBAL_UNLOCK_WR();
	return result;
}


#ifdef TRACE_FP_CALLS
RETT_FWRITE _nvp_FWRITE(INTF_FWRITE)
{
	DEBUG_FILE("_nvp_WRITE %d\n", fileno(fp));
	num_write++;
	RETT_FWRITE result;

#if PASS_THROUGH_CALLS
	result = _nvp_fileops->FWRITE(CALL_FWRITE);
	return result;
#endif
	
	struct NVFile* nvf = &_nvp_fd_lookup[fileno(fp)];
	struct stat sbuf;
	
	if (nvf->posix) {
		DEBUG_FILE("Call posix WRITE for fd %d\n", nvf->fd);
		result = _nvp_fileops->FWRITE(CALL_FWRITE);
		write_size += result;
		num_posix_write++;
		posix_write_size += result;
		return result;
	}

	if (nvf->node == NULL) {
		DEBUG_FILE("Call posix WRITE for fd %d\n", nvf->fd);
		result = _nvp_fileops->FWRITE(CALL_FWRITE);
		write_size += result;
		num_posix_write++;
		posix_write_size += result;
		return result;
	}
	
	int cpuid = GET_CPUID();
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED
	
	result = _nvp_check_write_size_valid(length);
	if (result <= 0) {
		return result;
	}

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid); //TODO

	if (tbl_app != NULL) {
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	}
	if (tbl_over != NULL)	{
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
	}

	result = _nvp_do_pwrite(fileno(fp),
				buf,
				length*nmemb,
				__sync_fetch_and_add(nvf->offset, length),
				0,
				cpuid,
				nvf,
				tbl_app,
				tbl_over);
	
	DEBUG("About to return from _nvp_WRITE with ret val %i (errno %i). "
		"file len: %li, file off: %li, map len: %li\n",
		result, errno, nvf->node->length, nvf->offset,
		nvf->node->maplength);

	write_size += result;
	return result;
}
#endif

#ifdef TRACE_FP_CALLS
RETT_FSEEK _nvp_FSEEK(INTF_FSEEK)
{
	RETT_WRITE result;
	int fd = -1;

	fd = fileno(fp);
	result = _nvp_SEEK(fd, offset, whence);
	return result;
}
#endif

#ifdef TRACE_FP_CALLS
RETT_FTELL _nvp_FTELL(INTF_FTELL)
{
	RETT_FTELL result;
	int fd = -1;
	struct NVFile *nvf = NULL;
	
	fd = fileno(fp);
	nvf = &_nvp_fd_lookup[fd];	
	result = _nvp_SEEK(fd, *(nvf->offset), SEEK_CUR);
	return result;	
}
#endif

#ifdef TRACE_FP_CALLS
RETT_FTELLO _nvp_FTELLO(INTF_FTELLO)
{
	DEBUG_FILE("%s: start\n", __func__);
	RETT_FTELLO result;

	result = _nvp_FTELL(CALL_FTELLO);
	return result;
}
#endif

RETT_WRITE _nvp_WRITE(INTF_WRITE)
{
	DEBUG("_nvp_WRITE %d\n", file);
	num_write++;
	RETT_WRITE result;
	instrumentation_type write_time;

	START_TIMING(write_t, write_time);

	GLOBAL_LOCK_WR();
	
#if PASS_THROUGH_CALLS
	result = _nvp_fileops->WRITE(CALL_WRITE);
	GLOBAL_UNLOCK_WR();
	END_TIMING(write_t, write_time);
	return result;
#endif
	
	struct NVFile* nvf = &_nvp_fd_lookup[file];
	
	if (nvf->posix) {
		DEBUG("Call posix WRITE for fd %d\n", nvf->fd);
		result = _nvp_fileops->WRITE(CALL_WRITE);		
		write_size += result;
		num_posix_write++;
		posix_write_size += result;
		END_TIMING(write_t, write_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	if (nvf->node == NULL) {
		result = _nvp_fileops->WRITE(CALL_WRITE);		
		write_size += result;
		num_posix_write++;
		posix_write_size += result;
		END_TIMING(write_t, write_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}
	
	int cpuid = GET_CPUID();
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED

	result = _nvp_check_write_size_valid(length);
	if (result <= 0) {
		END_TIMING(write_t, write_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_LOCK_NODE_RD(nvf, cpuid); //TODO

	if (tbl_app != NULL) {
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	}
	if (tbl_over != NULL)	{
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
	}

	result = _nvp_do_pwrite(CALL_WRITE,
				__sync_fetch_and_add(nvf->offset, length),
				0,
				cpuid,
				nvf,
				tbl_app,
				tbl_over);

	if(result >= 0)
	{
		if(nvf->append)
		{
			DEBUG("PWRITE succeeded and append == true. "
				"Setting offset to end...\n"); 
			//fflush(NULL);
			//assert(_nvp_do_seek64(nvf->fd, 0, SEEK_END, nvf)
			//	!= (RETT_SEEK64)-1);
		}
		else
		{
			DEBUG("PWRITE succeeded: extending offset "
				"from %li to %li\n",
				*nvf->offset - result, *nvf->offset);
		}
	}

	DEBUG("About to return from _nvp_WRITE with ret val %i (errno %i). "
		"file len: %li, file off: %li, map len: %li\n",
		result, errno, nvf->node->length, nvf->offset,
		nvf->node->maplength);

	write_size += result;

	END_TIMING(write_t, write_time);
	GLOBAL_UNLOCK_WR();

	DEBUG_FILE("%s: Returning %d\n", __func__, result);
	return result;
}

RETT_PREAD _nvp_PREAD(INTF_PREAD)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	DEBUG_FILE("_nvp_PREAD %d\n", file);
	num_read++;
	instrumentation_type read_time;
	RETT_PREAD result;

	START_TIMING(pread_t, read_time);
	GLOBAL_LOCK_WR();

#if PASS_THROUGH_CALLS
	result = _nvp_fileops->PREAD(CALL_PREAD);
	GLOBAL_UNLOCK_WR();
	END_TIMING(pread_t, read_time);
	return result;
#endif
	
	struct NVFile* nvf = &_nvp_fd_lookup[file];
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED

	if (nvf->posix) {
		DEBUG("Call posix PREAD for fd %d\n", nvf->fd);
		result = _nvp_fileops->PREAD(CALL_PREAD);
		read_size += result;
		num_posix_read++;
		posix_read_size += result;

		END_TIMING(pread_t, read_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	result = _nvp_check_read_size_valid(count);
	if (result <= 0) {
		END_TIMING(pread_t, read_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	int cpuid = GET_CPUID();
	
	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	if (tbl_app != NULL) {
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	}
	if (tbl_over != NULL)	{
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
	}

	result = _nvp_do_pread(CALL_PREAD, 0, cpuid, nvf, tbl_app, tbl_over);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	NVP_UNLOCK_FD_RD(nvf, cpuid);

	read_size += result;

	END_TIMING(pread_t, read_time);
	DEBUG_FILE("_nvp_PREAD %d returns %lu\n", file, result);
	GLOBAL_UNLOCK_WR();
	return result;
}

RETT_PWRITE _nvp_PWRITE(INTF_PWRITE)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	DEBUG("_nvp_PWRITE %d\n", file);
	num_write++;
	instrumentation_type write_time;
	RETT_PWRITE result;

	START_TIMING(pwrite_t, write_time);
	GLOBAL_LOCK_WR();
	
#if PASS_THROUGH_CALLS
	result = _nvp_fileops->PWRITE(CALL_PWRITE);
	GLOBAL_UNLOCK_WR();
	END_TIMING(pwrite_t, write_time);
	return result;
#endif
		
	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix PWRITE for fd %d\n", nvf->fd);
		result = _nvp_fileops->PWRITE(CALL_PWRITE);
		write_size += result;
		num_posix_write++;
		posix_write_size += result;
		END_TIMING(pwrite_t, write_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	if (nvf->node == NULL) {
		result = _nvp_fileops->PWRITE(CALL_PWRITE);		
		write_size += result;
		num_posix_write++;
		posix_write_size += result;
		END_TIMING(write_t, write_time);
		GLOBAL_UNLOCK_WR();
		return result;
	}

	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED
	
	result = _nvp_check_write_size_valid(count);
	if (result <= 0) {
		END_TIMING(pwrite_t, write_time);
		return result;
	}
	int cpuid = GET_CPUID();
	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	if (tbl_app != NULL) {
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	}
	if (tbl_over != NULL)	{
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
	}
	
	ssize_t available_length = (nvf->node->length) - offset;

	result = _nvp_do_pwrite(CALL_PWRITE,
				0,
				cpuid,
				nvf,
				tbl_app,
				tbl_over);

	write_size += result;

	END_TIMING(pwrite_t, write_time);

	GLOBAL_UNLOCK_WR();

	DEBUG_FILE("%s: Returning %d\n", __func__, result);
	return result;
}


RETT_SEEK _nvp_SEEK(INTF_SEEK)
{
	DEBUG("_nvp_SEEK\n");
	RETT_SEEK ret = 0;

	GLOBAL_LOCK_WR();

#if PASS_THROUGH_CALLS
	ret = _nvp_fileops->SEEK(CALL_SEEK);
	GLOBAL_UNLOCK_WR();
	return ret;
#endif	
	ret = _nvp_SEEK64(CALL_SEEK);
	GLOBAL_UNLOCK_WR();
	return ret;
}

RETT_SEEK64 _nvp_SEEK64(INTF_SEEK64)
{

	CHECK_RESOLVE_FILEOPS(_nvp_);
	RETT_SEEK64 result = 0;
	instrumentation_type seek_time;
	
	DEBUG("_nvp_SEEK64 %d\n", file);
	START_TIMING(seek_t, seek_time);

#if PASS_THROUGH_CALLS
	result = _nvp_fileops->SEEK64(CALL_SEEK);
	END_TIMING(seek_t, seek_time);
	return result;
#endif	
	
	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix SEEK64 for fd %d\n", nvf->fd);
		END_TIMING(seek_t, seek_time);
		DEBUG_FILE("%s: END\n", __func__);
		return _nvp_fileops->SEEK64(CALL_SEEK64);
	}

	int cpuid = GET_CPUID();

	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	result =  _nvp_do_seek64(CALL_SEEK64, nvf);	
	
	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	NVP_UNLOCK_FD_WR(nvf);

	END_TIMING(seek_t, seek_time);
	return result;
}

RETT_FTRUNC _nvp_FTRUNC(INTF_FTRUNC)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	RETT_FTRUNC ret = 0;

#if PASS_THROUGH_CALLS
	ret = _nvp_fileops->FTRUNC(CALL_FTRUNC);
	return ret;
#endif	

	DEBUG("_nvp_FTRUNC\n");

	ret = _nvp_FTRUNC64(CALL_FTRUNC64);
	return ret;
}

RETT_TRUNC _nvp_TRUNC(INTF_TRUNC)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	RETT_TRUNC ret = 0;
	struct stat stat_buf;
	int fd = 0;
	
#if PASS_THROUGH_CALLS
	ret = _nvp_fileops->TRUNC(CALL_TRUNC);
	return ret;
#endif	

	DEBUG("_nvp_TRUNC\n");

	if (stat(path, &stat_buf) == -1) 
		return -1;

	fd = _nvp_ino_lookup[stat_buf.st_ino % 1024];
	if (fd <= 0) {
		return _nvp_fileops->TRUNC(CALL_TRUNC);
	}

	ret = _nvp_FTRUNC64(fd, length);	
	return ret;
}
 
RETT_FTRUNC64 _nvp_FTRUNC64(INTF_FTRUNC64)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	instrumentation_type clear_mmap_tbl_time;
#if PASS_THROUGH_CALLS
	return _nvp_fileops->FTRUNC64(CALL_FTRUNC);
#endif	

	DEBUG("_nvp_TRUNC64\n");

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED	
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED

	
	if (nvf->posix) {
		DEBUG("Call posix TRUNC64 for fd %d\n", nvf->fd);
		return _nvp_fileops->FTRUNC64(CALL_FTRUNC64);
	}

	int cpuid = GET_CPUID();
	NVP_LOCK_FD_RD(nvf, cpuid);
	NVP_CHECK_NVF_VALID(nvf);
	NVP_LOCK_NODE_WR(nvf);
	if (tbl_app != NULL) {
		TBL_ENTRY_LOCK_WR(tbl_app);
	}
	if (tbl_over != NULL)	{
		TBL_ENTRY_LOCK_WR(tbl_over);
	}

	if(!nvf->canWrite) {
		DEBUG("FD not open for writing: %i\n", file);
		errno = EINVAL;
		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_WR(tbl_over);
		}
		TBL_ENTRY_UNLOCK_WR(tbl_app);	
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_RD(nvf, cpuid);
		return -1;
	}

	if(length == nvf->node->length)
	{
		DEBUG("_nvp_TRUNC64: requested length was the same as old "
			"length (%li).\n", nvf->node->length);
		if (tbl_over != NULL) {
			TBL_ENTRY_UNLOCK_WR(tbl_over);
		}
		TBL_ENTRY_UNLOCK_WR(tbl_app);	
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_RD(nvf, cpuid);
		return 0;
	}

	int result = _nvp_fileops->FTRUNC64(CALL_FTRUNC64);
	_nvp_fileops->FSYNC(file);
	
	if(result != 0)
	{
		ERROR("%s->TRUNC64 failed (returned %li, requested %li): %s\n",
			_nvp_fileops->name, result, length, strerror(errno));
		assert(0);
	}

	if(length > nvf->node->length)
	{
		DEBUG_FILE("TRUNC64 extended file from %li to %li\n",
			nvf->node->length, length);
	}
	else 
	{
		DEBUG_FILE("TRUNC64 shortened file from %li to %li\n",
			nvf->node->length, length);
	}

	nvf->node->true_length = length;
	if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
		nvf->node->is_large_file = 1;

	nvf->node->length = length;
	if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
		nvf->node->is_large_file = 1;
	START_TIMING(clear_mmap_tbl_t, clear_mmap_tbl_time);
	clear_tbl_mmap_entry(tbl_app);
	clear_tbl_mmap_entry(tbl_over);
	END_TIMING(clear_mmap_tbl_t, clear_mmap_tbl_time);

	if (tbl_over != NULL)	
		TBL_ENTRY_UNLOCK_WR(tbl_over);	
	TBL_ENTRY_UNLOCK_WR(tbl_app);	
	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_RD(nvf, cpuid);

	return result;
}

RETT_READV _nvp_READV(INTF_READV)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("CALL: _nvp_READV\n");

	//TODO: opportunities for optimization exist here
	int fail = 0;
	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _nvp_READ(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_nvp_READV failed on iov %i\n", i);
		return -1;
	}

	return 0;
}

RETT_WRITEV _nvp_WRITEV(INTF_WRITEV)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	DEBUG("CALL: _nvp_WRITEV\n");

	//TODO: opportunities for optimization exist here
	int fail = 0;
	int i;
	for(i=0; i<iovcnt; i++)
	{
		fail |= _nvp_WRITE(file, iov[i].iov_base, iov[i].iov_len);
		if(fail) { break; }
	}

	if(fail != 0) {
		DEBUG("_nvp_WRITEV failed on iov %i\n", i);
		return -1;
	}

	return 0;
}

RETT_DUP _nvp_DUP(INTF_DUP)
{
	DEBUG("_nvp_DUP(" PFFS_DUP ")\n", CALL_DUP);
	RETT_DUP ret = 0;
	
	//CHECK_RESOLVE_FILEOPS(_nvp_);
	if(file < 0) {
		ret = _nvp_fileops->DUP(CALL_DUP);
		//GLOBAL_UNLOCK_WR();
		return ret;
	}

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	 
	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);	
	NVP_LOCK_NODE_WR(nvf); // TODO

	int result = _nvp_fileops->DUP(CALL_DUP);

	if(result < 0) 
	{
		DEBUG("Call to _nvp_DUP->%s->DUP failed: %s\n",
			_nvp_fileops->name, strerror(errno));
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		//GLOBAL_UNLOCK_WR();
		return result;
	}

	struct NVFile* nvf2 = &_nvp_fd_lookup[result];

	nvf->valid = 0;
	nvf2->valid = 0;
	
	if (nvf->posix) {
		DEBUG("Call posix DUP for fd %d\n", nvf->fd);
		nvf2->posix = nvf->posix;
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		//GLOBAL_UNLOCK_WR();
		return result;
	}

	NVP_LOCK_FD_WR(nvf2);
	
	if(nvf2->valid) {
		ERROR("fd %i was already in use!\n", result);
		assert(!nvf2->valid);
	}
	else
	{
		//free(nvf2->offset); // TODO: free this iff it's not in use anymore to avoid memory leaks
	}
	
	nvf2->fd 	= result;
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
	return nvf2->fd;
}

RETT_DUP2 _nvp_DUP2(INTF_DUP2)
{
	//CHECK_RESOLVE_FILEOPS(_nvp_);
	DEBUG("_nvp_DUP2(" PFFS_DUP2 ")\n", CALL_DUP2);
	RETT_DUP2 ret = 0;

	if(file<0) {
		ret = _nvp_fileops->DUP(CALL_DUP);
		return ret;
	}

	if(fd2<0) {
		DEBUG("Invalid fd2\n");
		errno = EBADF;
		return -1;
	}

	if(file == fd2)
	{
		DEBUG("Input and output files were the same (%i)\n", file);
		return file;
	}

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	struct NVFile* nvf2 = &_nvp_fd_lookup[fd2];

	if (nvf->posix) {
		DEBUG("Call posix DUP2 for fd %d\n", nvf->fd);
		nvf2->posix = nvf->posix;
		int result = _nvp_fileops->DUP2(CALL_DUP2);
		nvf2->fd = result;
		return result;
	}

	//int iter;

	if(file > fd2)
	{
		NVP_LOCK_FD_WR(nvf);
		NVP_LOCK_FD_WR(nvf2);
	} else {
		NVP_LOCK_FD_WR(nvf2);
		NVP_LOCK_FD_WR(nvf);
	}

	if( (!nvf->valid)||(!nvf2->valid) ) {
		errno = EBADF;
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

	int result = _nvp_fileops->DUP2(CALL_DUP2);

	if(result < 0)
	{
		DEBUG("_nvp_DUP2 failed to %s->DUP2(%i, %i) "
			"(returned %i): %s\n", _nvp_fileops->name, file,
			fd2, result, strerror(errno));
		NVP_UNLOCK_NODE_WR(nvf);
		if(nvf->node != nvf2->node) { NVP_UNLOCK_NODE_WR(nvf2); }
		NVP_UNLOCK_FD_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf2);
		//GLOBAL_UNLOCK_WR();
		return result;
	}
	else
	{
		//free(nvf2->offset); // TODO: free this iff it's not in use anymore to avoid memory leaks
	}

	nvf2->valid = 0;
	
	if(nvf2->node && nvf->node != nvf2->node) { NVP_UNLOCK_NODE_WR(nvf2); }

	_nvp_test_invalidate_node(nvf2);

	if(result != fd2)
	{
		WARNING("result of _nvp_DUP2(%i, %i) didn't return the fd2 "
			"that was just closed.  Technically this doesn't "
			"violate POSIX, but I DON'T LIKE IT. "
			"(Got %i, expected %i)\n",
			file, fd2, result, fd2);
		assert(0);

		NVP_UNLOCK_FD_WR(nvf2);

		nvf2 = &_nvp_fd_lookup[result];

		NVP_LOCK_FD_WR(nvf2);

		if(nvf2->valid)
		{
			DEBUG("%s->DUP2 returned a result which corresponds "
				"to an already open NVFile! dup2(%i, %i) "
				"returned %i\n", _nvp_fileops->name,
				file, fd2, result);
			assert(0);
		}
	}

	nvf2->fd = result;
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
	
	return nvf2->fd;
}

RETT_IOCTL _nvp_IOCTL(INTF_IOCTL)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);

	DEBUG("CALL: _nvp_IOCTL\n");

	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	//GLOBAL_LOCK_WR();
	RETT_IOCTL result = _nvp_fileops->IOCTL(file, request, third);
	//GLOBAL_UNLOCK_WR();
	return result;
}

RETT_UNLINK _nvp_UNLINK(INTF_UNLINK)
{
	struct stat file_st;
	int index, tbl_mmap_idx, over_tbl_mmap_idx;
	struct InodeToMapping* mappingToBeRemoved;
	instrumentation_type unlink_time, clf_lock_time, clear_mmap_tbl_time, op_log_entry_time;
	RETT_UNLINK result = 0;
#if BG_CLOSING
	int hash_index = -1, closed_filedesc = -1, stale_fd = 0;
	ino_t closed_serialno = 0;
#if SEQ_LIST || RAND_LIST
	struct ClosedFiles *clf = NULL;
#else //SEQ_LIST || RAND_LIST
	struct InodeClosedFile *tbl = NULL;
#endif //SEQ_LIST || RAND_LIST
#endif //BG_CLOSING

	START_TIMING(unlink_t, unlink_time);
	GLOBAL_LOCK_WR();

#if PASS_THROUGH_CALLS
	num_unlink++;
        result = _nvp_fileops->UNLINK(CALL_UNLINK);
	GLOBAL_UNLOCK_WR();
	END_TIMING(unlink_t, unlink_time);
	return result;
#endif
	
	num_stat++;
	
	CHECK_RESOLVE_FILEOPS(_nvp_);
	DEBUG("CALL: _nvp_UNLINK\n");

	if (stat(path, &file_st) == 0) {
		index = file_st.st_ino % OPEN_MAX;
		tbl_mmap_idx = file_st.st_ino % APPEND_TBL_MAX;
		struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[tbl_mmap_idx];

#if DATA_JOURNALING_ENABLED
		over_tbl_mmap_idx = file_st.st_ino % OVER_TBL_MAX;
		struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[over_tbl_mmap_idx];
#else
		struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED

		DEBUG_FILE("%s: Deleting file: %s. Inode = %lu\n", __func__, path, file_st.st_ino);

		START_TIMING(clear_mmap_tbl_t, clear_mmap_tbl_time);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_WR(tbl_app);
		}
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_WR(tbl_over);
		}
		clear_tbl_mmap_entry(tbl_app);

#if DATA_JOURNALING_ENABLED

		clear_tbl_mmap_entry(tbl_over);

#endif // DATA_JOURNALING_ENABLED
		
		if (tbl_over != NULL)	
			TBL_ENTRY_UNLOCK_WR(tbl_over);
		TBL_ENTRY_UNLOCK_WR(tbl_app);
		END_TIMING(clear_mmap_tbl_t, clear_mmap_tbl_time);

#if BG_CLOSING
		GLOBAL_LOCK_CLOSE_WR();		
		hash_index = file_st.st_ino % TOTAL_CLOSED_INODES;
#if SEQ_LIST || RAND_LIST
		clf = &_nvp_closed_files[hash_index];

		LRU_NODE_LOCK_WR(clf);

		stale_fd = remove_from_seq_list_hash(clf, file_st.st_ino);
#else //SEQ_LIST || RAND_LIST
		tbl = &inode_to_closed_file[hash_index];
		NVP_LOCK_HASH_TABLE_WR(tbl);		
		stale_fd = remove_from_lru_list_hash(file_st.st_ino, 0);
#endif	//SEQ_LIST || RAND_LIST	
		if(stale_fd >= 0) {
			closed_filedesc = stale_fd;
			closed_serialno = file_st.st_ino;

			if(!_nvp_REAL_CLOSE(closed_filedesc, closed_serialno, 1)) 
				__atomic_fetch_sub(&num_files_closed, 1, __ATOMIC_SEQ_CST);
		}
#if SEQ_LIST || RAND_LIST
		LRU_NODE_UNLOCK_WR(clf);
#else //SEQ_LIST || RAND_LIST
		NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif //SEQ_LIST || RAND_LIST
		GLOBAL_UNLOCK_CLOSE_WR();
#endif //BG_CLOSING
		mappingToBeRemoved = &_nvp_ino_mapping[index];
		if(file_st.st_ino == mappingToBeRemoved->serialno && mappingToBeRemoved->root_dirty_num) {
			nvp_free_btree(mappingToBeRemoved->root, mappingToBeRemoved->merkle_root, mappingToBeRemoved->height, mappingToBeRemoved->root_dirty_cache, mappingToBeRemoved->root_dirty_num, mappingToBeRemoved->total_dirty_mmaps);					
			mappingToBeRemoved->serialno = 0;
		}
	}	
	num_unlink++;
	result = _nvp_fileops->UNLINK(CALL_UNLINK);
	
#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_FILE_UNLINK,
			 path,
			 NULL,
			 0,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	
	END_TIMING(unlink_t, unlink_time);
	GLOBAL_UNLOCK_WR();
	return result;
}

RETT_UNLINKAT _nvp_UNLINKAT(INTF_UNLINKAT)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	instrumentation_type op_log_entry_time;
	
	DEBUG("CALL: _nvp_UNLINKAT\n");

	//GLOBAL_LOCK_WR();
	RETT_UNLINKAT result = _nvp_fileops->UNLINKAT(CALL_UNLINKAT);

#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_FILE_UNLINK,
			 path,
			 NULL,
			 0,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;
}

RETT_FSYNC _nvp_FSYNC(INTF_FSYNC)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	RETT_FSYNC result = 0;
	instrumentation_type fsync_time;
	int cpuid = -1;

	START_TIMING(fsync_t, fsync_time);
	GLOBAL_LOCK_WR();

#if PASS_THROUGH_CALLS
	num_fsync++;
	result = _nvp_fileops->FSYNC(file);
	GLOBAL_UNLOCK_WR();
	END_TIMING(fsync_t, fsync_time);
	return 0;
#endif
	
	// Retrieve the NVFile from the global array of NVFiles
	cpuid = GET_CPUID();
	struct NVFile* nvf = &_nvp_fd_lookup[file];
	// This goes to fsync_flush_on_fsync()	
	FSYNC_FSYNC(nvf, cpuid, 0, 0);
	num_fsync++;	
	END_TIMING(fsync_t, fsync_time);
	GLOBAL_UNLOCK_WR();
	return result;
}

RETT_FDSYNC _nvp_FDSYNC(INTF_FDSYNC)
{
	CHECK_RESOLVE_FILEOPS(_nvp_);
	RETT_FDSYNC result = 0;
	int cpuid = -1;
	instrumentation_type fsync_time;

	START_TIMING(fsync_t, fsync_time);
	GLOBAL_LOCK_WR();

#if PASS_THROUGH_CALLS
	num_fsync++;
	result = _nvp_fileops->FSYNC(file);
	GLOBAL_UNLOCK_WR();
	END_TIMING(fsync_t, fsync_time);
	return 0;
#endif
	
	struct NVFile* nvf = &_nvp_fd_lookup[file];

	cpuid = GET_CPUID();
	FSYNC_FSYNC(nvf, cpuid, 0, 1);
	num_fsync++;

	GLOBAL_UNLOCK_WR();
	return result;
}


RETT_MKDIR _nvp_MKDIR(INTF_MKDIR)
{
	DEBUG_FILE("CALL: _nvp_MKDIR\n");
	instrumentation_type op_log_entry_time;
	// Write to op log
	RETT_MKDIR result = _nvp_fileops->MKDIR(CALL_MKDIR);
	DEBUG_FILE("%s: System call returned %d. Logging\n", __func__, result);
	
#if PASS_THROUGH_CALLS
	return result;
#endif	

#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_DIR_CREATE,
			 path,
			 NULL,
			 mode,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;
}

RETT_RENAME _nvp_RENAME(INTF_RENAME)
{
	DEBUG_FILE("CALL: _nvp_RENAME\n");
	RETT_RENAME result = _nvp_fileops->RENAME(CALL_RENAME);
	instrumentation_type op_log_entry_time;
	// Write to op log

#if PASS_THROUGH_CALLS
	return result;
#endif	

#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_RENAME,
			 old,
			 new,
			 0,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;
}

RETT_LINK _nvp_LINK(INTF_LINK)
{
	DEBUG_FILE("CALL: _nvp_LINK\n");
	RETT_LINK result = _nvp_fileops->LINK(CALL_LINK);
	instrumentation_type op_log_entry_time;
	// Write to op log

#if PASS_THROUGH_CALLS
	return result;
#endif	

#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_LINK,
			 path1,
			 path2,
			 0,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;
}

RETT_SYMLINK _nvp_SYMLINK(INTF_SYMLINK)
{
	DEBUG_FILE("CALL: _nvp_SYMLINK\n");
	RETT_SYMLINK result = _nvp_fileops->SYMLINK(CALL_SYMLINK);
	instrumentation_type op_log_entry_time;
	// Write to op log

#if PASS_THROUGH_CALLS
	return result;
#endif	

#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_SYMLINK,
			 path1,
			 path2,
			 0,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;
}

RETT_RMDIR _nvp_RMDIR(INTF_RMDIR)
{
	DEBUG_FILE("CALL: _nvp_RMDIR\n");
	RETT_RMDIR result = _nvp_fileops->RMDIR(CALL_RMDIR);
	instrumentation_type op_log_entry_time;
	// Write to op log

#if PASS_THROUGH_CALLS
	return result;
#endif	

#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_DIR_DELETE,
			 path,
			 NULL,
			 0,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;
}
 
RETT_SYMLINKAT _nvp_SYMLINKAT(INTF_SYMLINKAT)
{
	DEBUG_FILE("CALL: _nvp_SYMLINKAT\n");
	instrumentation_type op_log_entry_time;
	RETT_SYMLINKAT result = _nvp_fileops->SYMLINKAT(CALL_SYMLINKAT);
	// Write to op log

#if !POSIX_ENABLED
	char path[256];
	int path_len = 0;
	if (newdirfd == AT_FDCWD) {
		if (new_path[0] != '/') {
			if (getcwd(path, sizeof(path)) == NULL)
				assert(0);
			
			path_len = strlen(path);
			path[path_len] = '/';
			path[path_len+1] = '\0';

			if (strcat(path, new_path) == NULL)
				assert(0);
		} else {
			if (strcpy(path, new_path) == NULL)
				assert(0);
		}
	} else {
		char fd_str[256];				
		if (new_path[0] != '/') {
			sprintf(fd_str, "/proc/self/fd/%d", newdirfd);
			if (readlink(fd_str, path, sizeof(path)) < 0)
				assert(0);

			path_len = strlen(path);
			path[path_len] = '/';
			path[path_len+1] = '\0';
			if (strcat(path, new_path) == NULL)
				assert(0);

		} else {
			if (strcpy(path, new_path) == NULL)
				assert(0);
		}
	}

	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_SYMLINK,
			 old_path,
			 path,
			 0,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;
}

RETT_MKDIRAT _nvp_MKDIRAT(INTF_MKDIRAT)
{
	DEBUG_FILE("CALL: _nvp_MKDIRAT\n");
	instrumentation_type op_log_entry_time;
	RETT_MKDIRAT result = _nvp_fileops->MKDIRAT(CALL_MKDIRAT);
	
	// Write to op log

#if !POSIX_ENABLED
	char new_path[256];
	int path_len = 0;
	if (dirfd == AT_FDCWD) {
		if (path[0] != '/') {
			if (getcwd(new_path, sizeof(new_path)) == NULL)
				assert(0);
			path_len = strlen(new_path);
			new_path[path_len] = '/';
			new_path[path_len+1] = '\0';
			
			if (strcat(new_path, path) != new_path)
				assert(0);
		} else {
			if (strcpy(new_path, path) == NULL)
				assert(0);
		}
	} else {
		char fd_str[256];				
		if (path[0] != '/') {
			sprintf(fd_str, "/proc/self/fd/%d", dirfd);
			if (readlink(fd_str, new_path, sizeof(new_path)) == -1)
				assert(0);
			path_len = strlen(new_path);
			new_path[path_len] = '/';
			new_path[path_len+1] = '\0';
			if (strcat(new_path, path) != new_path)
				assert(0);
		} else {
			if (strcpy(new_path, path) == NULL)
				assert(0);
		}
	}

	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_DIR_CREATE,
			 new_path,
			 NULL,
			 mode,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return result;			  
} 

 
/* 
RETT_STAT _nvp_STAT(INTF_STAT)
{
	RETT_STAT result = 0;
	struct NVFile *nvf = NULL;
	int cpuid = GET_CPUID();
	return _nvp_fileops->STAT(_STAT_VER, path, buf);
	assert(0);
	result = _nvp_fileops->STAT(CALL_STAT);
	pthread_spin_lock(&node_lookup_lock[0]);
	if (_nvp_ino_lookup[buf->st_ino % 1024] != 0) {
		int fd = _nvp_ino_lookup[buf->st_ino % 1024];
		nvf = &_nvp_fd_lookup[fd];
		NVP_LOCK_FD_RD(nvf, cpuid);
		if (nvf->posix) {
			NVP_UNLOCK_FD_RD(nvf, cpuid);
			pthread_spin_unlock(&node_lookup_lock[0]);
			return result;
		}
		if (nvf->valid) {
			buf->st_size = nvf->node->length;
		}
		NVP_UNLOCK_FD_RD(nvf, cpuid);		
	}
	pthread_spin_unlock(&node_lookup_lock[0]);
	return result;
}
 
RETT_STAT64 _nvp_STAT64(INTF_STAT64)
{
	RETT_STAT64 result = 0;
	result = _nvp_fileops->STAT64(CALL_STAT64);
	struct NVFile *nvf = NULL;
	int cpuid = GET_CPUID();
	assert(0);
	pthread_spin_lock(&node_lookup_lock[0]);
	if (_nvp_ino_lookup[buf->st_ino % 1024] != 0) {
		int fd = _nvp_ino_lookup[buf->st_ino % 1024];
		nvf = &_nvp_fd_lookup[fd];
		NVP_LOCK_FD_RD(nvf, cpuid);
		if (nvf->posix) {
			NVP_UNLOCK_FD_RD(nvf, cpuid);
			pthread_spin_unlock(&node_lookup_lock[0]);
			return result;
		}
		if (nvf->valid) {
			buf->st_size = nvf->node->length;
		}
		NVP_UNLOCK_FD_RD(nvf, cpuid);		
	}
	pthread_spin_unlock(&node_lookup_lock[0]);
	return result;	
}

RETT_LSTAT _nvp_LSTAT(INTF_LSTAT)
{
	return _nvp_STAT64(CALL_STAT64);
}

RETT_LSTAT64 _nvp_LSTAT64(INTF_LSTAT64)
{
	return _nvp_STAT64(CALL_STAT64);
}
 
RETT_FSTAT _nvp_FSTAT(INTF_FSTAT)
{
	RETT_FSTAT result = 0;
	result = _nvp_fileops->FSTAT(CALL_FSTAT);
	struct NVFile *nvf = NULL;
	int cpuid = GET_CPUID();
	assert(0);
	nvf = &_nvp_fd_lookup[file];
	NVP_LOCK_FD_RD(nvf, cpuid);
	if (nvf->posix) {
		NVP_UNLOCK_FD_RD(nvf, cpuid);
		return result;
	}
	if (nvf->valid)
		buf->st_size = nvf->node->length;
	NVP_UNLOCK_FD_RD(nvf, cpuid);
	return result;
}

RETT_FSTAT64 _nvp_FSTAT64(INTF_FSTAT64)
{
	RETT_FSTAT64 result = 0;
	result = _nvp_fileops->FSTAT64(CALL_FSTAT64);
	struct NVFile *nvf = NULL;
	int cpuid = GET_CPUID();
	assert(0);
	nvf = &_nvp_fd_lookup[file];
	NVP_LOCK_FD_RD(nvf, cpuid);
	if (nvf->posix) {
		NVP_UNLOCK_FD_RD(nvf, cpuid);
		return;
	}
	if (nvf->valid)
		buf->st_size = nvf->node->length;
	NVP_UNLOCK_FD_RD(nvf, cpuid);
	return result;
}

RETT_POSIX_FALLOCATE _nvp_POSIX_FALLOCATE(INTF_POSIX_FALLOCATE)
{
	RETT_POSIX_FALLOCATE result = 0;
	struct NVFile *nvf = NULL;
	struct stat sbuf;
	int cpuid = GET_CPUID();
	return _nvp_fileops->POSIX_FALLOCATE(CALL_POSIX_FALLOCATE);
	assert(0);
	nvf = &_nvp_fd_lookup[file];
	NVP_LOCK_FD_RD(nvf, cpuid);
	if (nvf->posix) {
		result = _nvp_fileops->POSIX_FALLOCATE(CALL_POSIX_FALLOCATE);
		return result;
	}
	NVP_LOCK_NODE_WR(nvf);
	result = _nvp_fileops->POSIX_FALLOCATE(CALL_POSIX_FALLOCATE);
	_nvp_fileops->FSTAT(_STAT_VER, file, &sbuf);
	nvf->node->true_length = sbuf.st_size;
	nvf->node->length = nvf->node->true_length;
	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_RD(nvf, cpuid);
	return result;
}

RETT_POSIX_FALLOCATE64 _nvp_POSIX_FALLOCATE64(INTF_POSIX_FALLOCATE64)
{
	RETT_POSIX_FALLOCATE64 result = 0;
	struct NVFile *nvf = NULL;
	struct stat sbuf;
	return _nvp_fileops->POSIX_FALLOCATE64(CALL_POSIX_FALLOCATE64);
	assert(0);
	int cpuid = GET_CPUID();
	nvf = &_nvp_fd_lookup[file];
	NVP_LOCK_FD_RD(nvf, cpuid);
	if (nvf->posix) {
		result = _nvp_fileops->POSIX_FALLOCATE64(CALL_POSIX_FALLOCATE64);
		return result;
	}
	NVP_LOCK_NODE_WR(nvf);
	result = _nvp_fileops->POSIX_FALLOCATE64(CALL_POSIX_FALLOCATE);
	_nvp_fileops->FSTAT(_STAT_VER, file, &sbuf);
	nvf->node->true_length = sbuf.st_size;
	nvf->node->length = nvf->node->true_length;
	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_RD(nvf, cpuid);
	return result;
}

RETT_FALLOCATE _nvp_FALLOCATE(INTF_FALLOCATE)
{
	RETT_FALLOCATE result = 0;
	struct NVFile *nvf = NULL;
	struct stat sbuf;
	int cpuid = GET_CPUID();
	return _nvp_fileops->FALLOCATE(CALL_FALLOCATE);
	assert(0);
	nvf = &_nvp_fd_lookup[file];
	NVP_LOCK_FD_RD(nvf, cpuid);
	if (nvf->posix) {
		result = _nvp_fileops->FALLOCATE(CALL_FALLOCATE);
		return result;
	}
	NVP_LOCK_NODE_WR(nvf);
	result = _nvp_fileops->FALLOCATE(CALL_FALLOCATE);
	_nvp_fileops->FSTAT(_STAT_VER, file, &sbuf);
	nvf->node->true_length = sbuf.st_size;
	nvf->node->length = nvf->node->true_length;
	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_RD(nvf, cpuid);
	return result;
}
*/

