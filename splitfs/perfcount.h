#ifndef PERF_COUNT_STATS_H
#define PERF_COUNT_STATS_H

#define MAX_CPUS 16

#include <stdio.h>
#include <stdint.h>

typedef uint64_t timing_t;

#define MY_ALIGNED __attribute__ ((__aligned__(256)))

struct vm_timing_stat {
	timing_t count;
	timing_t total_time;
} MY_ALIGNED;

typedef struct vm_timing_stat stat_per_cpu[MAX_CPUS];

static inline timing_t getcycles(void) {
	uint32_t lo, hi;
	__asm__ __volatile__ ( //serialize
                "xorl %%eax,%%eax \n        cpuid"
                ::: "%rax", "%rbx", "%rcx", "%rdx");
                /* We cannot use "=A", since this would use %rax on x86_64 */
        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
	return (uint64_t)hi << 32 | lo;
}

#define cpuid(func,ax,bx,cx,dx)\
	__asm__ __volatile__ ("cpuid":\
	"=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (func));

static inline int get_cpuid() {
	uint32_t eax=0;
	uint32_t ebx=0;
	uint32_t ecx=1;
	uint32_t edx=0;

	cpuid(0x0B, eax,ebx,ecx,edx);

	int id = (((edx&1)<<3) + ((edx>>4)&1) + (edx&0xe));
	return id;
}

#define perf_clear_stat(s) { \
	int i; \
	for(i=0; i<MAX_CPUS; i++) { \
		s[i].count = 0; \
		s[i].total_time = 0; \
	} \
}

static inline struct vm_timing_stat perf_condense_stat(stat_per_cpu s) {
	struct vm_timing_stat result;
	result.count = 0;
	result.total_time = 0;
	int i;
	for(i=0; i<MAX_CPUS; i++) {
		result.count += s[i].count;
		result.total_time += s[i].total_time;
	}
	return result;
}

//static inline timing_t start_timing()
#define perf_start_timing getcycles

// static inline void end_timing(stat_per_cpu s, timing_t start_time) {
#define perf_end_timing(s, start_time) { \
	timing_t end_time = getcycles(); \
	int cpu = get_cpuid(); \
	if ( end_time > start_time ) { \
		s[cpu].count++; \
		s[cpu].total_time += end_time - start_time; \
	} \
}
//TODO: why do we only add on this case?

// static inline void perf_increment_count(stat_per_cpu s) {
#define perf_increment_count(s) s[get_cpuid()].count++

static inline void perf_print_stat(FILE* fd, stat_per_cpu s, const char* name)
{
	if(name == 0) {
		name = "NO_NAME_GIVEN";
	}

	struct vm_timing_stat result = perf_condense_stat(s);

	float cycles_per_count = ((float)result.total_time)/((float)result.count);

	fprintf(fd, "Finished timing \"%s\": %lu results in %lu cycles: "
	"%f cycles per result; %f microseconds per result (assuming clock of 2.26GHz)\n",
	name, result.count, result.total_time,
	cycles_per_count,
	cycles_per_count/2261L); // TODO: measure clock speed instead of assuming
}	

#endif

