// Header file for LEDGER Logging

#ifndef _LEDGER_LOG_H_
#define _LEDGER_LOG_H_

#include "nv_common.h"
#include "fileops_nvp.h"
#include <ftw.h>

#define APPEND_LOG_ENTRY_SIZE 64
#define OP_LOG_ENTRY_SIZE 37
#define APPEND_LOG_PATH "/mnt/pmem_emul/append.log"
#define OP_LOG_PATH "/mnt/pmem_emul/operation.log"
#define APPEND_LOG_SIZE (128*1024*1024)
#define OP_LOG_SIZE (128*1024*1024)

enum log_types {
	LOG_DIR_CREATE,
	LOG_RENAME,
	LOG_LINK,
	LOG_SYMLINK,
	LOG_DIR_DELETE,
	LOG_FILE_APPEND,
	LOG_FILE_CREATE,
	LOG_FILE_UNLINK,
	LOG_TYPES_NUM,
};

struct append_log_entry {
	uint32_t checksum;
	uint32_t file_ino;
	uint32_t dr_ino;
	loff_t file_offset;
	loff_t dr_offset;
	size_t data_size;
	uint8_t padding[28];
};

struct op_log_entry {
	uint32_t checksum;
	size_t entry_size;
	size_t file1_size;
	size_t file2_size;
	uint8_t op_type;
	uint32_t mode;
	uint32_t flags;
};

struct inode_path {
	uint32_t file_ino;
	size_t file_size;
	char path[256];
	struct inode_path *next;
};

uint8_t clearing_app_log;
uint8_t clearing_op_log;
loff_t app_log_tail;
loff_t op_log_tail;
loff_t app_log_lim;
loff_t op_log_lim;
int app_log_fd;
int op_log_fd;
unsigned long app_log;
unsigned long op_log;
struct inode_path *ino_path_head;

void init_logs();
void persist_op_entry(uint32_t op_type,
		      const char *fname1,
		      const char *fname2,
		      uint32_t mode,
		      uint32_t flags);
void persist_append_entry(uint32_t file_ino,
			  uint32_t dr_ino,
			  loff_t file_off,
			  loff_t dr_off,
			  size_t size);
void sync_and_clear_app_log();
void sync_and_clear_op_log();

#endif
