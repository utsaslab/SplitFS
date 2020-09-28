// Header file for nvmfileops.c

#ifndef __NV_FILEOPS_H_
#define __NV_FILEOPS_H_

#include "nv_common.h"
#include "ledger.h"
#include "nvp_lock.h"
#include "liblfds711/inc/liblfds711.h"

#define _NVP_USE_DEFAULT_FILEOPS NULL

// Declare the real_close function, as it will be called by bg thread
RETT_CLOSE _nvp_REAL_CLOSE(INTF_CLOSE, ino_t serialno, int async_file_closing);
size_t swap_extents(struct NVFile *nvf, int close);

/******************* Data Structures ********************/


/******************* Checking ********************/

#define NOSANITYCHECK 1
#if NOSANITYCHECK
	#define SANITYCHECK(x)
#else
	#define SANITYCHECK(x) if(UNLIKELY(!(x))) { ERROR("NVP_SANITY("#x") failed!\n"); exit(101); }
#endif

#define NVP_CHECK_NVF_VALID(nvf) do{					\
		if(UNLIKELY(!nvf->valid)) {				\
			DEBUG("Invalid file descriptor: %i\n", file);	\
			errno = 0;					\
			return -1;					\
		}							\
		else							\
			{						\
				DEBUG("this function is operating on node %p\n", nvf->node); \
				SANITYCHECKNVF(nvf);			\
			}						\
	} while(0)

#define NVP_CHECK_NVF_VALID_WR(nvf) do{					\
		if(UNLIKELY(!nvf->valid)) {				\
			DEBUG("Invalid file descriptor: %i\n", file);	\
			errno = 0;					\
			return -1;					\
		}							\
		else {							\
				DEBUG("this function is operating on node %p\n", nvf->node); \
				SANITYCHECKNVF(nvf);			\
			}						\
	} while(0)

#define SANITYCHECKNVF(nvf)						\
	SANITYCHECK(nvf->valid);					\
	SANITYCHECK(nvf->node != NULL);					\
	SANITYCHECK(nvf->fd >= 0);					\
	SANITYCHECK(nvf->fd < OPEN_MAX);				\
	SANITYCHECK(nvf->offset != NULL);				\
	SANITYCHECK(*nvf->offset >= 0);					\
	SANITYCHECK(nvf->node->length >=0);				\
	SANITYCHECK(nvf->node->maplength >= nvf->node->length);		\
	SANITYCHECK(nvf->node->data != NULL)

/*
  #define SANITYCHECKNVF(nvf)						\
  SANITYCHECK(nvf->valid);						\
  SANITYCHECK(nvf->node != NULL);					\
  SANITYCHECK(nvf->fd >= 0);						\
  SANITYCHECK(nvf->fd < OPEN_MAX);					\
  SANITYCHECK(nvf->offset != NULL);					\
  SANITYCHECK(*nvf->offset >= 0);					\
  SANITYCHECK(nvf->serialno != 0);					\
  SANITYCHECK(nvf->serialno == nvf->node->serialno);			\
  SANITYCHECK(nvf->node->length >=0);					\
  SANITYCHECK(nvf->node->maplength >= nvf->node->length);		\
  SANITYCHECK(nvf->node->data != NULL)
*/

#endif
