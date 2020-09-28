// Header file for LEDGER Logging

#include "log.h"
#include "tbl_mmaps.h"

uint32_t crc32_for_byte(uint32_t r) {
	for(int j = 0; j < 8; ++j)
		r = (r & 1? 0: (uint32_t)0xEDB88320L) ^ r >> 1;
	return r ^ (uint32_t)0xFF000000L;
}

void create_crc32(const void *data, size_t n_bytes, uint32_t* crc) {
	static uint32_t table[0x100];
	if(!*table)
		for(size_t i = 0; i < 0x100; ++i)
			table[i] = crc32_for_byte(i);
	for(size_t i = 0; i < n_bytes; ++i)
		*crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
}

void persist_append_entry(uint32_t file_ino,
			  uint32_t dr_ino,
			  loff_t file_off,
			  loff_t dr_off,
			  size_t size) {
	loff_t log_off;
	struct append_log_entry app_entry;
 fetch_and_add:
	log_off = __sync_fetch_and_add(&app_log_tail, APPEND_LOG_ENTRY_SIZE);
	if (app_log_tail > app_log_lim) {
		if (__sync_bool_compare_and_swap(&clearing_app_log, 0, 1))
			sync_and_clear_app_log();
		goto fetch_and_add;
	}
	app_entry.file_ino = file_ino;
	app_entry.dr_ino = dr_ino;
	app_entry.file_offset = file_off;
	app_entry.dr_offset = dr_off;
	app_entry.data_size = size;
	create_crc32((void *) &(app_entry.file_ino), 32, &(app_entry.checksum)); 
	//app_entry.checksum = checksum; // [TODO] Calculate Checksum
	MEMCPY_NON_TEMPORAL((void *)app_log + log_off,
			    &app_entry,
			    APPEND_LOG_ENTRY_SIZE);
	_mm_sfence();	
#if NVM_DELAY
	perfmodel_add_delay(0, APPEND_LOG_ENTRY_SIZE);
#endif
}

void persist_op_entry(uint32_t op_type,
		      const char *fname1,
		      const char *fname2,
		      uint32_t mode,
		      uint32_t flags) {
	loff_t log_off;
	size_t padding = 0;
	struct op_log_entry op_entry;

	DEBUG_FILE("%s: START\n", __func__);

	op_entry.entry_size = OP_LOG_ENTRY_SIZE + strlen(fname1);
	op_entry.file1_size = strlen(fname1);
	if (fname2 != NULL) {
		op_entry.entry_size += strlen(fname2);
		op_entry.file2_size = strlen(fname2);
	}
	if (op_entry.entry_size % CLFLUSH_SIZE != 0)
		padding = CLFLUSH_SIZE - (op_entry.entry_size % CLFLUSH_SIZE);
	
 fetch_and_add:
	log_off = __sync_fetch_and_add(&op_log_tail, op_entry.entry_size + padding);
	if (op_log_tail > op_log_lim) {
		if (__sync_bool_compare_and_swap(&clearing_op_log, 0, 1))
			sync_and_clear_op_log();
		goto fetch_and_add;
	}

	op_entry.op_type = op_type;
	op_entry.mode = mode;
	op_entry.flags = flags;
	create_crc32((void *) &(op_entry.entry_size), op_entry.entry_size, &(op_entry.checksum)); 

	DEBUG_FILE("%s: Got the checksum. log_off = %lu, op_log = %lu\n", __func__, log_off, op_log);
	
	//op_entry.checksum = checksum; // [TODO] Calculate Checksum
	MEMCPY_NON_TEMPORAL((void *)op_log + log_off,
			    &op_entry,
			    OP_LOG_ENTRY_SIZE);
#if NVM_DELAY
	perfmodel_add_delay(0, OP_LOG_ENTRY_SIZE);
#endif
	log_off += OP_LOG_ENTRY_SIZE;
	MEMCPY_NON_TEMPORAL((void *)op_log + log_off,
			    fname1,
			    strlen(fname1));
#if NVM_DELAY
	perfmodel_add_delay(0, strlen(fname1));
#endif
	log_off += strlen(fname1);
	if (fname2 != NULL) {
		MEMCPY_NON_TEMPORAL((void *)op_log + log_off,
				    fname2,
				    strlen(fname2));
#if NVM_DELAY
		perfmodel_add_delay(0, strlen(fname2));
#endif
		log_off += strlen(fname2);
	}
	if (padding != 0) {
		char padstr[padding];
		MEMCPY_NON_TEMPORAL((void *)op_log + log_off,
				    padstr,
				    padding);
#if NVM_DELAY
		perfmodel_add_delay(0, padding);
#endif
	}

	_mm_sfence();
}

void init_append_log() {

	int i = 0, ret = 0;
	unsigned long num_blocks = APPEND_LOG_SIZE / MMAP_PAGE_SIZE;
	char prefault_buf[MMAP_PAGE_SIZE];

	clearing_app_log = 0;
        app_log_tail = 0;
	app_log_lim = APPEND_LOG_SIZE;
	app_log_fd = -1;
        app_log = 0;

	MSG("%s: Initializing append log\n", __func__);

	app_log_fd = _hub_find_fileop("posix")->OPEN(APPEND_LOG_PATH, O_RDWR | O_CREAT, 0666);
	if (app_log_fd < 0) {
		MSG("%s: Creation of append log file failed. Err = %s\n",
		    __func__, strerror(errno));
		assert(0);
	}

	ret =  posix_fallocate(app_log_fd, 0, APPEND_LOG_SIZE);
	if (ret < 0) {
		MSG("%s: posix_fallocate append long failed. Err = %s\n",
		    __func__, strerror(errno));
		assert(0);
	}

	app_log = (unsigned long) FSYNC_MMAP
		(
		 NULL,
		 APPEND_LOG_SIZE,
		 PROT_READ | PROT_WRITE, //max_perms,
		 MAP_SHARED | MAP_POPULATE,
		 app_log_fd, //fd_with_max_perms,
		 0
		 );

	for (i = 0; i < MMAP_PAGE_SIZE; i++)
		prefault_buf[i] = '0';

	for (i = 0; i < num_blocks; i++) {
#if NON_TEMPORAL_WRITES
		if(MEMCPY_NON_TEMPORAL((void *)app_log + i*MMAP_PAGE_SIZE,
				       prefault_buf,
				       MMAP_PAGE_SIZE) == NULL) {
			MSG("%s: non-temporal memcpy app log failed\n", __func__);
			assert(0);
		}
#else // NON_TEMPORAL_WRITES
		if(FSYNC_MEMCPY((char *)app_log + i*MMAP_PAGE_SIZE,
				prefault_buf,
				MMAP_PAGE_SIZE) == NULL) {
			MSG("%s: temporal memcpy app log failed\n", __func__);
			assert(0);
		}
#endif // NON_TEMPORAL_WRITES
	}
}

void init_op_log() {

	int i = 0, ret = 0;
	unsigned long num_blocks = APPEND_LOG_SIZE / MMAP_PAGE_SIZE;
	char prefault_buf[MMAP_PAGE_SIZE];

	clearing_op_log = 0;
	op_log_tail = 0;
        op_log_lim = OP_LOG_SIZE;
	op_log_fd = -1;
	op_log = 0;


	op_log_fd = _hub_find_fileop("posix")->OPEN(OP_LOG_PATH, O_RDWR | O_CREAT, 0666);
	if (op_log_fd < 0) {
		MSG("%s: Creation of op log file failed. Err = %s\n",
		    __func__, strerror(errno));
		assert(0);
	}

	ret =  posix_fallocate(op_log_fd, 0, OP_LOG_SIZE);
	if (ret < 0) {
		MSG("%s: posix_fallocate op log failed. Err = %s\n",
		    __func__, strerror(errno));
		assert(0);
	}

	op_log = (unsigned long) FSYNC_MMAP
		(
		 NULL,
		 OP_LOG_SIZE,
		 PROT_READ | PROT_WRITE, //max_perms,
		 MAP_SHARED | MAP_POPULATE,
		 op_log_fd, //fd_with_max_perms,
		 0
		 );

	for (i = 0; i < MMAP_PAGE_SIZE; i++)
		prefault_buf[i] = '0';

	for (i = 0; i < num_blocks; i++) {
#if NON_TEMPORAL_WRITES
		if(MEMCPY_NON_TEMPORAL((void *)op_log + i*MMAP_PAGE_SIZE,
				       prefault_buf,
				       MMAP_PAGE_SIZE) == NULL) {
			MSG("%s: non-temporal memcpy op log failed\n", __func__);
			assert(0);
		}
#else // NON_TEMPORAL_WRITES
		if(FSYNC_MEMCPY((char *)op_log + i*MMAP_PAGE_SIZE,
				prefault_buf,
				MMAP_PAGE_SIZE) == NULL) {
			MSG("%s: temporal memcpy op log failed\n", __func__);
			assert(0);
		}
#endif // NON_TEMPORAL_WRITES
	}
}

void sync_and_clear_app_log() {
	int i = 0, ret = -1;
	struct NVFile *nvf = NULL;
	int cpuid = GET_CPUID();
	instrumentation_type append_log_reinit_time;

	START_TIMING(append_log_reinit_t, append_log_reinit_time);
	for (i = 3; i < OPEN_MAX; i++) {
		nvf = &_nvp_fd_lookup[i];
		NVP_LOCK_FD_RD(nvf, cpuid);
		DEBUG_FILE("%s: Calling dynamic remap, because app log is full\n", __func__);
		if (nvf->fd > 0 && nvf->valid && !nvf->posix && nvf->node) {

			NVP_LOCK_NODE_WR(nvf);
			/* [TODO] Do Some checks to see if 
			 * there are appends, and if there are,
			 * perform the dynamic remap system call.
			 */
			if (nvf->node->true_length != nvf->node->length)
				swap_extents(nvf, 0);

			NVP_UNLOCK_NODE_WR(nvf);
		}
		DEBUG_FILE("%s: File %i synced. OPEN_MAX = %i\n", __func__, i, OPEN_MAX);
		NVP_UNLOCK_FD_RD(nvf, cpuid);
	}
	// memset((void *)app_log, 0, APPEND_LOG_SIZE);
	__sync_bool_compare_and_swap(&app_log_tail, app_log_tail, 0);
	__sync_bool_compare_and_swap(&clearing_app_log, 1, 0);
	END_TIMING(append_log_reinit_t, append_log_reinit_time);
}

void sync_and_clear_op_log() {
	int i = 0, ret = -1;
	struct NVFile *nvf = NULL;
	int cpuid = GET_CPUID();
	
	for (i = 3; i < OPEN_MAX; i++) {
		nvf = &_nvp_fd_lookup[i];
		NVP_LOCK_FD_RD(nvf, cpuid);
		if (nvf->valid) {
			ret = syncfs(nvf->fd);
			if (ret != 0) {
				DEBUG_FILE("%s: Syncfs failed. Err = %s\n",
					   __func__, strerror(errno));
				assert(0);
			}			
		}
		NVP_UNLOCK_FD_RD(nvf, cpuid);
	}
	memset((void *)op_log, 0, OP_LOG_SIZE);
	__sync_bool_compare_and_swap(&op_log_tail, op_log_tail, 0);
	__sync_bool_compare_and_swap(&clearing_op_log, 1, 0);
}

void ledger_op_log_recovery() {

	int flags = 0, ret = 0;
	char fname1[256], fname2[256];
	struct op_log_entry op_entry;
	uint32_t computed_checksum = 0;
	op_log_tail = 0;
	
	while(op_log_tail < op_log_lim) {
		if (app_log + app_log_tail == '0')
			goto end;		
		memcpy(&op_entry,
		       (void *) (op_log + op_log_tail),
		       OP_LOG_ENTRY_SIZE);

		switch (op_entry.op_type) {
		case LOG_DIR_CREATE:
			memcpy(fname1,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE),
			       op_entry.file1_size
			       );			
			create_crc32((void *) &(op_entry.entry_size),
			      op_entry.entry_size,
			      &(computed_checksum)); 
			if (computed_checksum != op_entry.checksum) {
				DEBUG_FILE("%s: checksum missmatch\n", __func__);
				return;
			}
			ret = access(fname1, F_OK);
			if (ret == 0)
				goto next;
			if (ret != 0) {
				if (errno != ENOENT) {
					MSG("%s: access failed for file %s\n",
					    __func__, fname1);
					assert(0);
				}
			}
			ret = mkdir(fname1, op_entry.mode);
			if (ret != 0) {
				MSG("%s: mkdir failed. Err = %s\n",
				    __func__, strerror(errno));
				assert(0);
			}
			break;
		case LOG_RENAME:
			memcpy(fname1,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE),
			       op_entry.file1_size
			       );
			memcpy(fname2,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE + op_entry.file1_size),
			       op_entry.file2_size
			       );
			create_crc32((void *) &(op_entry.entry_size),
			      op_entry.entry_size,
			      &(computed_checksum)); 
			if (computed_checksum != op_entry.checksum) {
				DEBUG_FILE("%s: checksum missmatch\n", __func__);
				return;
			}
			ret = access(fname2, F_OK);
			if (ret == 0)
				goto next;
			if (ret != 0) {
				if (errno != ENOENT) {
					MSG("%s: access failed for file %s\n",
					    __func__, fname2);
					assert(0);
				}
			}
			ret = rename(fname1, fname2);
			if (ret != 0) {
				MSG("%s: rename failed. Err = %s\n",
				    __func__, strerror(errno));
				assert(0);
			}
			break;
		case LOG_LINK:
			memcpy(fname1,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE),
			       op_entry.file1_size
			       );
			memcpy(fname2,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE + op_entry.file1_size),
			       op_entry.file2_size
			       );
			create_crc32((void *) &(op_entry.entry_size),
			      op_entry.entry_size,
			      &(computed_checksum)); 
			if (computed_checksum != op_entry.checksum) {
				DEBUG_FILE("%s: checksum missmatch\n", __func__);
				return;
			}
			ret = access(fname2, F_OK);
			if (ret == 0)
				goto next;
			if (ret != 0) {
				if (errno != ENOENT) {
					MSG("%s: access failed for file %s\n",
					    __func__, fname2);
					assert(0);
				}
			}
			ret = link(fname1, fname2);
			if (ret != 0) {
				MSG("%s: link failed. Err = %s\n",
				    __func__, strerror(errno));
				assert(0);
			}
			break;
		case LOG_SYMLINK:
			memcpy(fname1,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE),
			       op_entry.file1_size
			       );
			memcpy(fname2,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE + op_entry.file1_size),
			       op_entry.file2_size
			       );
			create_crc32((void *) &(op_entry.entry_size),
			      op_entry.entry_size,
			      &(computed_checksum)); 
			if (computed_checksum != op_entry.checksum) {
				DEBUG_FILE("%s: checksum missmatch\n", __func__);
				return;
			}
			ret = access(fname2, F_OK);
			if (ret == 0)
				goto next;
			if (ret != 0) {
				if (errno != ENOENT) {
					MSG("%s: access failed for file %s\n",
					    __func__, fname2);
					assert(0);
				}
			}
			ret = symlink(fname1, fname2);
			if (ret != 0) {
				MSG("%s: symlink failed. Err = %s\n",
				    __func__, strerror(errno));
				assert(0);
			}
			break;
		case LOG_DIR_DELETE:
			memcpy(fname1,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE),
			       op_entry.file1_size
			       );			
			create_crc32((void *) &(op_entry.entry_size),
			      op_entry.entry_size,
			      &(computed_checksum)); 
			if (computed_checksum != op_entry.checksum) {
				DEBUG_FILE("%s: checksum missmatch\n", __func__);
				return;
			}
			ret = access(fname1, F_OK);
			if (ret != 0) {
				if (errno != ENOENT) {
					MSG("%s: access failed for file %s\n",
					    __func__, fname1);
					assert(0);
				}
				else
					goto next;
			}
			ret = rmdir(fname1);
			if (ret != 0) {
				MSG("%s: rmdir failed. Err = %s\n",
				    __func__, strerror(errno));
				assert(0);
			}
			break;			
		case LOG_FILE_CREATE:
			memcpy(fname1,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE),
			       op_entry.file1_size
			       );			
			create_crc32((void *) &(op_entry.entry_size),
			      op_entry.entry_size,
			      &(computed_checksum)); 
			if (computed_checksum != op_entry.checksum) {
				DEBUG_FILE("%s: checksum missmatch\n", __func__);
				return;
			}
			ret = access(fname1, F_OK);
			if (ret == 0)
				goto next;
			if (ret != 0) {
				if (errno != ENOENT) {
					MSG("%s: access failed for file %s\n",
					    __func__, fname1);
					assert(0);
				}
			}
			ret = open(fname1, op_entry.flags, op_entry.mode);
			if (ret < 0) {
				MSG("%s: create file failed. Err = %s\n",
				    __func__, strerror(errno));
				assert(0);
			}
			break;
		case LOG_FILE_UNLINK:
			memcpy(fname1,
			       (void *) (op_log + op_log_tail + OP_LOG_ENTRY_SIZE),
			       op_entry.file1_size
			       );			
			create_crc32((void *) &(op_entry.entry_size),
			      op_entry.entry_size,
			      &(computed_checksum)); 
			if (computed_checksum != op_entry.checksum) {
				DEBUG_FILE("%s: checksum missmatch\n", __func__);
				return;
			}
			ret = access(fname1, F_OK);
			if (ret != 0) {
				if (errno != ENOENT) {
					MSG("%s: access failed for file %s\n",
					    __func__, fname1);
					assert(0);
				}
				else
					goto next;
			}
			ret = unlink(fname1);
			if (ret != 0) {
				MSG("%s: unlink failed. Err = %s\n",
				    __func__, strerror(errno));
				assert(0);
			}
			break;
		}

	next:
		op_log_tail += op_entry.entry_size;
		if (op_log_tail % CLFLUSH_SIZE != 0) {
			op_log_tail += (CLFLUSH_SIZE - (op_log_tail % CLFLUSH_SIZE));
		}
	}

 end:
	DEBUG_FILE("%s: Op log recovery completed successfully\n",
		   __func__);		
}

static int ino_path_info(const char *fpath,
			 const struct stat *sb,
			 int tflag,
			 struct FTW *ftwbuf) {

	struct inode_path *ino_path = NULL;
	if (tflag != FTW_F)
		return 0;
	if (ino_path_head == NULL) {
		ino_path_head = (struct inode_path *) malloc(sizeof(struct inode_path));
		ino_path = ino_path_head;
	} else {
		ino_path = ino_path_head;
		while (ino_path->next != NULL)
			ino_path = ino_path->next;
		ino_path->next = (struct inode_path *) malloc(sizeof(struct inode_path));
		ino_path = ino_path->next;
	}
	strcpy(ino_path->path, fpath);
	ino_path->file_ino = sb->st_ino;
	ino_path->file_size = sb->st_size;
	ino_path->next = NULL;
	return 0;
}

static void get_relevant_file(struct inode_path *ino_path_file,
			      ino_t file_ino) {
	struct inode_path *ino_path = NULL;	

	if (ino_path_head == NULL) {
		ino_path_file->file_ino = 0;
		return;
	}

	ino_path = ino_path_head;
	while (ino_path != NULL) {
		if (ino_path->file_ino == file_ino) {
			ino_path_file->file_ino = ino_path->file_ino;
			ino_path_file->file_size = ino_path->file_size;
			strcpy(ino_path_file->path, ino_path->path);
			ino_path_file->next = NULL;
			return;
		} else 
			ino_path = ino_path->next;
	}
}
	
void ledger_append_log_recovery() {

	int flags = 0, ret = 0, file_fd = 0, dr_fd = 0;
	unsigned long dr_addr = 0;
	struct inode_path ino_path_file, ino_path_dr;	
	struct append_log_entry app_entry;
	uint32_t computed_checksum = 0;
	ino_path_head = NULL;
	app_log_tail = 0;
	
	ret = nftw(NVMM_PATH, ino_path_info, 20, 0);
	if (ret == -1) {
		MSG("%s: nftw failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	while(app_log_tail < app_log_lim) {
		if (app_log + app_log_tail == '0')
			goto end;
		memcpy(&app_entry,
		       (void *) (app_log + app_log_tail),
		       APPEND_LOG_ENTRY_SIZE);
		create_crc32((void *) &(app_entry.file_ino), 32, &computed_checksum);
		if (computed_checksum != app_entry.checksum) {
			DEBUG_FILE("%s: checksum missmatch\n", __func__);
			return;
		}
		get_relevant_file(&ino_path_file, app_entry.file_ino);
		get_relevant_file(&ino_path_dr, app_entry.dr_ino);
		if (ino_path_file.file_ino == 0 || ino_path_dr.file_ino == 0)
			goto next;
		if ((ino_path_file.file_size != app_entry.file_offset) ||
		    (ino_path_dr.file_size < app_entry.dr_offset + app_entry.data_size))
			goto next;
		// Open file X, file DR.
		file_fd = open(ino_path_file.path, O_RDWR);
		if (file_fd < 0) {
			MSG("%s: Open failed for path %s\n",
			    __func__, ino_path_file.path);
			assert(0);
		}
		dr_fd = open(ino_path_dr.path, O_RDWR);
		if (dr_fd < 0) {
			MSG("%s: Open failed for path %s\n",
			    __func__, ino_path_dr.path);
			assert(0);
		}
			
		// MAP DR file.
		dr_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 ino_path_dr.file_size,
			 PROT_READ | PROT_WRITE, //max_perms,
			 MAP_PRIVATE | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );
		if (dr_addr == 0) {
			MSG("%s: mmap failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		
		// Do dynamic remap between file X and DR file or simply copy data.
		ret = syscall(335, file_fd,
			      dr_fd,
			      app_entry.file_offset,
			      app_entry.dr_offset,
			      (const char *)dr_addr,
			      app_entry.data_size);
		if (ret < 0) {
			MSG("%s: Dynamic remap called failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}		
		
		// Close file X and DR file.
		ret = munmap((void *) dr_addr, ino_path_dr.file_size);
		if (ret < 0) {
			MSG("%s: unmap of dr file failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		close(dr_fd);
		close(file_fd);
	next:
		app_log_tail += APPEND_LOG_ENTRY_SIZE;
	}

 end:
	DEBUG_FILE("%s: Append log recovery completed successfully\n",
		   __func__);	
}
