#ifndef _STORAGE_H_
#define _STORAGE_H_

#include "global/global.h"
#include "spdk/sync.h"


// Interface for different storage engines.
struct storage_operations
{
	uint8_t *(*init)(uint8_t dev, char *dev_path);
	int (*read)(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
	int (*read_unaligned)(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset,
			uint32_t io_size);
	int (*write)(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
	int (*write_unaligned)(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset,
			uint32_t io_size);
	int (*erase)(uint8_t dev, addr_t blockno, uint32_t io_size);
	int (*commit)(uint8_t dev);
	int (*wait_io)(uint8_t dev, int isread);
	int (*readahead)(uint8_t dev, addr_t blockno, uint32_t io_size);
	void (*exit)(uint8_t dev);
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 To get the dev-dax size,
 cat /sys/devices/platform/e820_pmem/ndbus0/region0/size
 0: not used
 1: g_root_dev
 2: g_ssd_dev
 3: g_hdd_dev
 4~ per-application log device
*/

// device size in bytes
static uint64_t dev_size[g_n_devices + 1] = {0, 21474836480UL, 128849018880UL, 193273528320UL, 2147483648UL};

extern struct storage_operations storage_dax;
extern struct storage_operations storage_spdk;
extern struct storage_operations storage_hdd;

// ramdisk
uint8_t *ramdisk_init(uint8_t dev, char *dev_path);
int ramdisk_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int ramdisk_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int ramdisk_erase(uint8_t dev, uint32_t blockno, addr_t io_size);
void ramdisk_exit(uint8_t dev);

// pmem
uint8_t *pmem_init(uint8_t dev, char *dev_path);
int pmem_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int pmem_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int pmem_write_unaligned(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset,
		uint32_t io_size);
int pmem_erase(uint8_t dev, addr_t blockno, uint32_t io_size);
void pmem_exit(uint8_t dev);

// pmem-dax
uint8_t *dax_init(uint8_t dev, char *dev_path);
int dax_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int dax_read_unaligned(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset, 
		uint32_t io_size);
int dax_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int dax_write_unaligned(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t offset, 
		uint32_t io_size);
int dax_erase(uint8_t dev, addr_t blockno, uint32_t io_size);
int dax_commit(uint8_t dev);
void dax_exit(uint8_t dev);

// SPDK (PCIe-SSD)
uint8_t *spdk_init(uint8_t dev, char *dev_path);
int spdk_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int spdk_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int spdk_erase(uint8_t dev, addr_t blockno, uint32_t io_size);
int spdk_wait_io(uint8_t dev, int isread);
int spdk_readahead(uint8_t dev, addr_t blockno, uint32_t io_size);
void spdk_exit(uint8_t dev);

// HDD
uint8_t *hdd_init(uint8_t dev, char *dev_path);
int hdd_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int hdd_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size);
int hdd_commit(uint8_t dev);
int hdd_erase(uint8_t dev, addr_t blockno, uint32_t io_size);
int hdd_wait_io(uint8_t dev);
int hdd_readahead(uint8_t dev, addr_t blockno, uint32_t io_size);
void hdd_exit(uint8_t dev);

extern uint64_t *bandwidth_consumption;

#ifdef __cplusplus
}
#endif

#endif
