#include "add_delay.h"

// Set CPU frequency correctly
#define _CPUFREQ 3600LLU /* MHz */

#define NS2CYCLE(__ns) (((__ns) * _CPUFREQ) / 1000)
#define CYCLE2NS(__cycles) (((__cycles) * 1000) / _CPUFREQ)

#define BANDWIDTH_MONITOR_NS 10000
#define SEC_TO_NS(x) (x * 1000000000UL)

#define ENABLE_PERF_MODEL 
#define ENABLE_BANDWIDTH_MODEL 

// performance parameters
/* SCM read extra latency than DRAM */
uint32_t SCM_EXTRA_READ_LATENCY_NS = 220;
// We assume WBARRIER LATENCY is 0 since write back queue can hide this even in 
// power failure.
// https://software.intel.com/en-us/blogs/2016/09/12/deprecate-pcommit-instruction
uint32_t SCM_WBARRIER_LATENCY_NS = 0;

/* SCM write bandwidth */
uint32_t SCM_BANDWIDTH_MB = 21000;
/* DRAM system peak bandwidth */
uint32_t DRAM_BANDWIDTH_MB = 63000;

uint64_t bandwidth_consumption;
static uint64_t monitor_start = 0, monitor_end = 0, now = 0;

pthread_mutex_t mlfs_nvm_mutex;

static inline void PERSISTENT_BARRIER(void)
{
	asm volatile ("sfence\n" : : );
}

///////////////////////////////////////////////////////

static inline void emulate_latency_ns(int ns)
{
	uint64_t cycles, start, stop;

	start = asm_rdtscp();
	cycles = NS2CYCLE(ns);
	//printf("cycles %lu\n", cycles);

	do {
		/* RDTSC doesn't necessarily wait for previous instructions to complete
		 * so a serializing instruction is usually used to ensure previous
		 * instructions have completed. However, in our case this is a desirable
		 * property since we want to overlap the latency we emulate with the
		 * actual latency of the emulated instruction.
		 */
		stop = asm_rdtscp();
	} while (stop - start < cycles);
}

void perfmodel_add_delay(int read, size_t size)
{
#ifdef ENABLE_PERF_MODEL
	uint32_t extra_latency;
	uint32_t do_bandwidth_delay;

	// Only allowed for mkfs.
	/*
	if (!bandwidth_consumption) {
		if (!warning)  {
			printf("\033[31m WARNING: Bandwidth tracking variable is not set."
					" Running program must be mkfs \033[0m\n");
			warning = 1;
		}
		return ;
	}
	*/
	
	now = asm_rdtscp();

	if (now >= monitor_end) {
		monitor_start = now;
		monitor_end = monitor_start + NS2CYCLE(BANDWIDTH_MONITOR_NS);
		bandwidth_consumption = 0;
	} 

	if (__sync_add_and_fetch(&bandwidth_consumption, size) >=
			((SCM_BANDWIDTH_MB << 20) / (SEC_TO_NS(1UL) / BANDWIDTH_MONITOR_NS)))
		do_bandwidth_delay = 1;
	else
		do_bandwidth_delay = 0;

	if (read) {
		extra_latency = SCM_EXTRA_READ_LATENCY_NS;
	} else
		extra_latency = SCM_WBARRIER_LATENCY_NS;

	// bandwidth delay for both read and write.
	if (do_bandwidth_delay) {
		// Due to the writeback cache, write does not have latency
		// but it has bandwidth limit.
		// The following is emulated delay when bandwidth is full
		extra_latency += (int)size *
			(1 - (float)(((float) SCM_BANDWIDTH_MB)/1000) /
			 (((float)DRAM_BANDWIDTH_MB)/1000)) / (((float)SCM_BANDWIDTH_MB)/1000);
		pthread_mutex_lock(&mlfs_nvm_mutex);
		emulate_latency_ns(extra_latency);
		pthread_mutex_unlock(&mlfs_nvm_mutex);
	} else
		emulate_latency_ns(extra_latency);

#endif
	return;
}
