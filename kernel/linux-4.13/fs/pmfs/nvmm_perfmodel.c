#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/cpufreq.h>
#include <asm/fpu/api.h>
#include "pmfs_def.h"

#define _MAX_INT    0x7ffffff

#define ENABLE_PERF_MODEL
#define ENABLE_BANDWIDTH_MODEL

#define BANDWIDTH_MONITOR_NS 10000
#define SEC_TO_NS(x) (x * 1000000000UL)

static uint64_t monitor_start = 0, monitor_end = 0, now = 0;
atomic64_t bandwidth_consumption = ATOMIC_INIT(0);

int nvm_perf_model = 1;

spinlock_t dax_nvm_spinlock;

static uint32_t read_latency_ns = 220;
static uint32_t wbarrier_latency_ns = 0;
static uint32_t nvm_bandwidth = 21000;
static uint32_t dram_bandwidth = 63000;
static uint64_t cpu_freq_mhz = 3600UL;

#define NVM_LATENCY 100
#define NS2CYCLE(__ns) (((__ns) * cpu_freq_mhz) / 1000)
#define CYCLE2NS(__cycles) (((__cycles) * 1000) / cpu_freq_mhz)

static inline unsigned long long asm_rdtsc(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static inline unsigned long long asm_rdtscp(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

/* kernel floating point 
 * http://yarchive.net/comp/linux/kernel_fp.html
 *  set CFLAGs with -mhard-float
 *  
 *  in code.
 *	kernel_fpu_begin();
 *	...
 *	kernel_fpu_end();
 */

static inline void emulate_latency_ns(int ns)
{
    uint64_t cycles, start, stop;

    start = asm_rdtscp();
    cycles = NS2CYCLE(ns);

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

    now = asm_rdtscp();

    if (now >= monitor_end) {
		monitor_start = now;
		monitor_end = monitor_start + NS2CYCLE(BANDWIDTH_MONITOR_NS);
		atomic64_set(&bandwidth_consumption, 0);
	}

	atomic64_add(size, &bandwidth_consumption);

	if (atomic64_read(&bandwidth_consumption) >=
		((nvm_bandwidth << 20) / (SEC_TO_NS(1UL) / BANDWIDTH_MONITOR_NS)))
        do_bandwidth_delay = 1;
    else
        do_bandwidth_delay = 0;
#endif

#ifdef ENABLE_PERF_MODEL
    if (read) {
        extra_latency = read_latency_ns;
    } else
        extra_latency = wbarrier_latency_ns;

    // bandwidth delay for both read and write.
    if (do_bandwidth_delay) {
        // Due to the writeback cache, write does not have latency
        // but it has bandwidth limit.
        // The following is emulated delay when bandwidth is full
        extra_latency += (int)size *
            (1 - (float)(((float) nvm_bandwidth)/1000) /
             (((float)dram_bandwidth)/1000)) / (((float)nvm_bandwidth)/1000);
        // Bandwidth is enough, so no write delay.
		spin_lock(&dax_nvm_spinlock);
		emulate_latency_ns(extra_latency);
		spin_unlock(&dax_nvm_spinlock);
    } else
		emulate_latency_ns(extra_latency);
#endif


    return;
}
