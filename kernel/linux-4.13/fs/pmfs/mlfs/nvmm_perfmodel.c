#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/cpufreq.h>
#include <asm/fpu/api.h>

#define _MAX_INT    0x7ffffff

#define ENABLE_PERF_MODEL
#define ENABLE_BANDWIDTH_MODEL

#define BANDWIDTH_MONITOR_NS 10000
#define SEC_TO_NS(x) (x * 1000000000UL)

static uint64_t monitor_start = 0, monitor_end = 0, now = 0;
atomic64_t bandwidth_consumption = ATOMIC_INIT(0);

int nvm_perf_model = 1;
EXPORT_SYMBOL(nvm_perf_model);

spinlock_t dax_nvm_spinlock;

static uint32_t read_latency_ns = 150;
static uint32_t wbarrier_latency_ns = 0;
static uint32_t nvm_bandwidth = 8000;
static uint32_t dram_bandwidth = 63000;
static uint64_t cpu_freq_mhz = 3600UL;

#define NS2CYCLE(__ns) (((__ns) * cpu_freq_mhz) / 1000)
#define CYCLE2NS(__cycles) (((__cycles) * 1000) / cpu_freq_mhz)
#if defined(__i386__)

static inline unsigned long long asm_rdtsc(void)
{
	unsigned long long int x;
	__asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
	return x;
}

static inline unsigned long long asm_rdtscp(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"ecx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );

}
#elif defined(__x86_64__)

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
#else
//#error "Only support for X86 architecture"
#endif

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

#if 0 //Aerie.
void perfmodel_add_delay(int read, size_t size)
{
#ifdef ENABLE_PERF_MODEL
    uint32_t extra_latency = 0;
#endif

    spin_lock(&dax_nvm_spinlock);
#ifdef ENABLE_PERF_MODEL
    if (read) {
        extra_latency = read_latency_ns;
    } else {
#ifdef ENABLE_BANDWIDTH_MODEL
        // Due to the writeback cache, write does not have latency
        // but it has bandwidth limit.
        // The following is emulated delay when bandwidth is full
        extra_latency = (int)size *
            (1 - (float)(((float) nvm_bandwidth)/1000) /
             (((float)dram_bandwidth)/1000)) / (((float)nvm_bandwidth)/1000);
        extra_latency += read_latency_ns;
#else
        //write delay.
        extra_latency += read_latency_ns;
#endif
    }

    trace_printk("NVM delay %u ns\n", extra_latency);
    emulate_latency_ns(extra_latency);
#endif

    spin_unlock(&dax_nvm_spinlock);

    return;
}
#else
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
#endif
EXPORT_SYMBOL(perfmodel_add_delay);

static ssize_t cpu_freq_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu MHz\n", cpu_freq_mhz);
}

static ssize_t cpu_freq_store(struct kobject *kobj,
                 struct kobj_attribute *attr,
                 const char *buf, size_t count)
{
	int value;
	int err;

	err = kstrtoint(buf, 10, &value);
	if (err || value > _MAX_INT || value < 0)
		return -EINVAL;

	cpu_freq_mhz = value;
	return count;
}

static struct kobj_attribute cpu_freq_attr =
    __ATTR(cpu_freq, 0644, cpu_freq_show, cpu_freq_store);

static ssize_t nvm_bandwidth_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u MB/s\n", nvm_bandwidth);
}

static ssize_t nvm_bandwidth_store(struct kobject *kobj,
                 struct kobj_attribute *attr,
                 const char *buf, size_t count)
{
	int value;
	int err;

	err = kstrtoint(buf, 10, &value);
	if (err || value > _MAX_INT || value < 0)
		return -EINVAL;

	nvm_bandwidth = value;
	return count;
}

static struct kobj_attribute nvm_bandwidth_attr =
    __ATTR(nvm_bandwidth, 0644, nvm_bandwidth_show, nvm_bandwidth_store);

static ssize_t dram_bandwidth_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u MB/s\n", dram_bandwidth);
}

static ssize_t dram_bandwidth_store(struct kobject *kobj,
                 struct kobj_attribute *attr,
                 const char *buf, size_t count)
{
	int value;
	int err;

	err = kstrtoint(buf, 10, &value);
	if (err || value > _MAX_INT || value < 0)
		return -EINVAL;

	dram_bandwidth = value;
	return count;
}

static struct kobj_attribute dram_bandwidth_attr =
    __ATTR(dram_bandwidth, 0644, dram_bandwidth_show, dram_bandwidth_store);

static ssize_t wbarrier_latency_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u ns\n", wbarrier_latency_ns);
}

static ssize_t wbarrier_latency_store(struct kobject *kobj,
                 struct kobj_attribute *attr,
                 const char *buf, size_t count)
{
	int value;
	int err;

	err = kstrtoint(buf, 10, &value);
	if (err || value > _MAX_INT || value < 0)
		return -EINVAL;

	wbarrier_latency_ns = value;
	return count;
}

static struct kobj_attribute wbarrier_latency_attr =
    __ATTR(wbarrier_latency, 0644, wbarrier_latency_show, wbarrier_latency_store);

static ssize_t read_latency_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u ns\n", read_latency_ns);
}

static ssize_t read_latency_store(struct kobject *kobj,
                 struct kobj_attribute *attr,
                 const char *buf, size_t count)
{
	int value;
	int err;

	err = kstrtoint(buf, 10, &value);
	if (err || value > _MAX_INT || value < 0)
		return -EINVAL;

	read_latency_ns = value;
	return count;
}

static struct kobj_attribute read_latency_attr =
    __ATTR(read_latency, 0644, read_latency_show, read_latency_store);

static ssize_t enable_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
    if (nvm_perf_model == 1) {
        return sprintf(buf, "enabled\n");
    }
    else if (nvm_perf_model == 0)
        return sprintf(buf, "disabled\n");
    else
        return sprintf(buf, "invalid value\n");
}

static ssize_t enable_store(struct kobject *kobj,
                 struct kobj_attribute *attr,
                 const char *buf, size_t count)
{
	int value;
	int err;

	err = kstrtoint(buf, 10, &value);
	if (err || value > 1 || value < 0)
		return -EINVAL;

	nvm_perf_model = value;
	return count;
}

static struct kobj_attribute enable_attr =
    __ATTR(enable, 0644, enable_show, enable_store);

static struct attribute *mlfs_attr[] = {
    &enable_attr.attr,
    &read_latency_attr.attr,
    &wbarrier_latency_attr.attr,
    &nvm_bandwidth_attr.attr,
    &dram_bandwidth_attr.attr,
    &cpu_freq_attr.attr,
    NULL,
};

static struct attribute_group mlfs_attr_group = {
    .attrs = mlfs_attr,
};

/* sysfs path: /sys/kernel/mm/mlfs/ */
static int mlfs_init_sysfs(struct kobject **mlfs_kobj)
{
    int err;

    *mlfs_kobj = kobject_create_and_add("mlfs", mm_kobj);

    if (unlikely(!*mlfs_kobj)) {
        pr_err("failed to create mlfs kobject\n");
        return -ENOMEM;
    }

    err = sysfs_create_group(*mlfs_kobj, &mlfs_attr_group);
    if (err) {
        pr_err("failed to register mlfs attr group\n");
        goto delete_kobj;
    }

    return 0;

delete_kobj:
    kobject_put(*mlfs_kobj);
    return err;
}

static void mlfs_exit_sysfs(struct kobject *mlfs_kobj)
{
	sysfs_remove_group(mlfs_kobj, &mlfs_attr_group);
	kobject_put(mlfs_kobj);
}

static int __init perfmodel_init(void)
{
	int err = 0;
	struct kobject *mlfs_kobj;

	err = mlfs_init_sysfs(&mlfs_kobj);
	if (err)
		goto err_exit;

	return 0;

	// unused
	mlfs_exit_sysfs(mlfs_kobj);
err_exit:
	return err;
}

subsys_initcall(perfmodel_init);
