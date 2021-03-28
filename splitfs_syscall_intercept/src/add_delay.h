#ifndef _LEDGER_ADD_DELAY_H_
#define _LEDGER_ADD_DELAY_H_

// to use O_DIRECT flag
//
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdint.h>

#include "util.h"

void perfmodel_add_delay(int read, size_t size);
	
#endif
