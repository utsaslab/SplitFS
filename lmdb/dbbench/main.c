/* Copyright (c) 2017-2018 Howard Chu @ Symas Corp. */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "dbb.h"

// Comma-separated list of operations to run in the specified order
//   Actual benchmarks:
//
//   fillseq	   -- write N values in sequential key order in async mode
//   fillrandom	-- write N values in random key order in async mode
//   overwrite	 -- overwrite N values in random key order in async mode
//   fillseqsync   -- write N/100 values in sequential key order in sync mode
//   fillseqbatch  -- batch write N values in sequential key order in async mode
//   fillrandsync  -- write N/100 values in random key order in sync mode
//   fillrandbatch  -- batch write N values in random key order in async mode
//   readseq	   -- read N times sequentially
//   readreverse   -- read N times in reverse order
//   readrandom	-- read N times in random order
static char* FLAGS_benchmarks =
	"fillrandsync,"
	"fillrandom,"
	"fillrandbatch,"
	"fillseqsync,"
	"fillseq,"
	"fillseqbatch,"
	"overwrite,"
#if 0
	"overwritebatch,"
#endif
	"readrandom,"
	"readseq,"
	"readreverse,"
	;

// Number of key/values to place in database
int64_t FLAGS_num = 1000000;

// Number of read operations to do.  If negative, do FLAGS_num reads.
int64_t FLAGS_reads = -1;

// Number of concurrent threads to run.
int FLAGS_threads = 1;

// Maximum number of threads.
int FLAGS_max_threads = 0;

// Time in seconds for the random-ops tests to run.
int FLAGS_duration = 0;

// Per-thread rate limit on writes per second.
// Only for the readwhilewriting test.
int FLAGS_writes_per_second;

// Stats are reported every N seconds when this is
// greater than zero.
int FLAGS_stats_period = 0;

// Size of each value
int FLAGS_value_size = 1024;

// Size of each key
int FLAGS_key_size = 16;

// Number of key/values to place in database
int FLAGS_batch = 100;

// Arrange to generate values that shrink to this fraction of
// their original size after compression
float FLAGS_compression_ratio = 0.5;

// Compression disabled by default
int FLAGS_compression = 0;

// Print histogram of operation timings
int FLAGS_histogram = 0;

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
int FLAGS_use_existing_db = 0;

// If true, running on a raw device
int FLAGS_rawdev = 0;

// Use the db with the following name.
const char* FLAGS_db = "/mnt/pmem_emul/db";

static rndctx **seeds;
static Hstctx **hists;

extern DBB_backend *dbb_backend;

static char *CompressibleString(rndctx *ctx, float ratio, int len, char *buf)
{
	char *ptr, *end;
	int i, raw = (int)len * ratio;
	if (raw < 1) raw = 1;

	ptr = buf;
	end = buf + len;
	for (i=0; i<raw; i++)
		*ptr++ = (DBB_random(ctx) % 96) + ' ';

	i = 0;
	while (ptr < end) {
		*ptr++ = buf[i++];
		if (i >= raw)
			i = 0;
	}
	return end;
}

#define RAND_STRSIZE	10485600
static void GenerateString(DBB_local *dl)
{
	int i;
	char *ptr, *end;
	dl->dl_randstr = malloc(RAND_STRSIZE);
	dl->dl_randstrpos = 0;

	ptr = dl->dl_randstr;
	end = ptr + RAND_STRSIZE;
	while (ptr < end)
		ptr = CompressibleString(dl->dl_rndctx, FLAGS_compression_ratio, 100, ptr);
}

/* Helper for quickly generating random data */
void DBB_randstring(DBB_local *dl, DBB_val *val)
{
	if (dl->dl_randstrpos + val->dv_size > RAND_STRSIZE) {
		dl->dl_randstrpos = 0;
		assert(val->dv_size < RAND_STRSIZE);
	}
	val->dv_data = dl->dl_randstr + dl->dl_randstrpos;
	dl->dl_randstrpos += val->dv_size;
}

static int timecmp(struct timeval *a, struct timeval *b)
{
	int ret;
	ret = a->tv_sec - b->tv_sec;
	if (ret)
		return ret;
	return a->tv_usec - b->tv_usec;
}

static void timesub(struct timeval *end, struct timeval *beg)
{
	end->tv_sec -= beg->tv_sec;
	end->tv_usec -= beg->tv_usec;
	if (end->tv_usec < 0) {
		end->tv_usec += 1000000;
		end->tv_sec--;
	}
}

static void Merge(DBB_local *dst, DBB_local *src) {
	DBB_hstmerge(dst->dl_hstctx, src->dl_hstctx);
	dst->dl_done += src->dl_done;
	dst->dl_bytes += src->dl_bytes;
	dst->dl_seconds += src->dl_seconds;
	if (timecmp(&dst->dl_start, &src->dl_start) > 0)
		dst->dl_start = src->dl_start;
	if (timecmp(&dst->dl_finish, &src->dl_finish) < 0)
		dst->dl_finish = src->dl_finish;

	/* Just keep the messages from one thread */
	if (!dst->dl_message.dv_data && src->dl_message.dv_data)
		dst->dl_message = src->dl_message;
}

void DBB_message(DBB_local *dl, char *msg) {
	int len = strlen(msg) + 1;	/* add a space */
	char *ptr;
	if (dl->dl_message.dv_size) {
		ptr = realloc(dl->dl_message.dv_data, dl->dl_message.dv_size + len);
		if (!ptr)
			return;
		dl->dl_message.dv_data = ptr;
		ptr += dl->dl_message.dv_size - 1;
	} else {
		ptr = malloc(len + 1);
		if (!ptr)
			return;
		dl->dl_message.dv_data = ptr;
		dl->dl_message.dv_size = 1;
	}
	*ptr++ = ' ';
	strcpy(ptr, msg);
	dl->dl_message.dv_size += len;
}

static void TimeStr(time_t seconds, char *buf) {
	struct tm tm;
	localtime_r(&seconds, &tm);
	sprintf(buf, "%04d/%02d/%02d-%02d:%02d:%02d",
		tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void DBB_opdone(DBB_local *dl) {
	if (FLAGS_histogram) {
		struct timeval now;
		struct timeval last;
		gettimeofday(&now, NULL);
		last = dl->dl_last_op_finish;
		dl->dl_last_op_finish = now;
		timesub(&now, &last);
		DBB_hstadd(dl->dl_hstctx, &now);
		if (now.tv_sec > 0 || now.tv_usec > 20000) {
			fprintf(stderr, "long op: %d.%06d micros%30s\r", (int)now.tv_sec, (int)now.tv_usec, "");
			fflush(stderr);
		}
	}
	dl->dl_done++;
}

static void Report(DBB_local *dl, const char *name) {
	char rate[100];
	// Pretend at least one op was done in case we are running a benchmark
	// that does not call FinishedSingleOp().
	if (dl->dl_done < 1) dl->dl_done = 1;

	timesub(&dl->dl_finish, &dl->dl_start);
	double elapsed = dl->dl_finish.tv_sec + dl->dl_finish.tv_usec * 1e-6;
	if (dl->dl_bytes > 0) {
		// Rate is computed on actual elapsed time, not the sum of per-thread
		// elapsed times.
		snprintf(rate, sizeof(rate), "%6.1f MB/s",
			(dl->dl_bytes / 1048576.0) / elapsed);
	} else {
		rate[0] = '\0';
	}

	double throughput = (double)dl->dl_done/elapsed;

	fprintf(stdout, "%-13s : %11.3f micros/op %ld ops/sec;%s%s\n",
			name,
			elapsed * 1e6 / dl->dl_done,
			(long)throughput, rate, dl->dl_message.dv_data ? (char *)dl->dl_message.dv_data : "");
	if (FLAGS_histogram) {
		DBB_hstprint(dl->dl_hstctx);
	}
	fflush(stdout);
}

int DBB_done(DBB_local *dl) {
	/* If rate limiting, check every 100ms */
	if (dl->dl_wps_by_10) {
		struct timeval now;
		++dl->dl_writes;
		if (dl->dl_writes >= dl->dl_wps_by_10) {
			struct timeval tv;
			int delta;
			gettimeofday(&now, NULL);
			tv = now;
			timesub(&tv, &dl->dl_lastwrite);
			tv.tv_usec += tv.tv_sec * 1000000;
			dl->dl_writes = 0;
			dl->dl_lastwrite = now;
			if (tv.tv_usec < 100000) {
				usleep(100000 - tv.tv_usec);
				gettimeofday(&dl->dl_lastwrite, NULL);
			}
		}
	}
	if (dl->dl_max_seconds) {
		if (!(dl->dl_done % 1000)) {
		// Recheck every appx 1000 ops (exact iff increment is factor of 1000)
			time_t now = time(NULL);
			return (now - dl->dl_start.tv_sec >= dl->dl_max_seconds);
		} else {
			return 0;
		}
	} else {
		return dl->dl_done >= dl->dl_max_ops;
	}
}

static void PrintWarnings() {
#if defined(__GNUC__) && !defined(__OPTIMIZE__)
	fprintf(stdout,
			"WARNING: Optimization is disabled: benchmarks unnecessarily slow\n"
			);
#endif
#ifndef NDEBUG
	fprintf(stdout,
			"WARNING: Assertions are enabled; benchmarks unnecessarily slow\n");
#endif
}

static void PrintEnvironment() {
	fprintf(stdout, "%s: version %s\n", dbb_backend->db_longname, dbb_backend->db_verstr());

#if defined(__linux)
	time_t now = time(NULL);
	fprintf(stdout, "Date:	   %s", ctime(&now));  // ctime() adds newline

	FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
	if (cpuinfo != NULL) {
		char line[1000];
		int num_cpus = 0;
		char cpu_type[1000];
		char cache_size[1000];
		cpu_type[0] = '\0';
		cache_size[0] = '\0';
		while (fgets(line, sizeof(line), cpuinfo) != NULL) {
			char *val, *ptr;
			char *sep = strchr(line, ':');
			if (sep == NULL) {
				continue;
			}
			*sep++ = '\0';
			ptr = strchr(line, '\t');
			if (ptr)
				*ptr = '\0';
			val = sep+1;
			if (!strcmp(line,"model name")) {
				strcpy(cpu_type, val);
			} else if (!strcmp(line, "processor")) {
				++num_cpus;
			} else if (!strcmp(line, "cache size")) {
				strcpy(cache_size, val);
			}
		}
		fclose(cpuinfo);
		if (!cpu_type[0]) {
			char family[250], manuf[250], version[250], freq[200], *ptr;
			int cachei = 0;
			cpuinfo = popen("dmidecode -t processor", "r");
			if (cpuinfo) {
				while(fgets(line, sizeof(line), cpuinfo)) {
					char *sep = strchr(line, ':');
					if (!sep) continue;
					*sep = '\0';
					sep += 2;
					ptr = strrchr(sep, '\n');
					if (ptr) *ptr = '\0';
					ptr = line+1;
					if (!strcmp(ptr, "Manufacturer")) {
						strcpy(manuf, sep);
					} else if (!strcmp(ptr, "Family")) {
						strcpy(family, sep);
					} else if (!strcmp(ptr, "Version")) {
						strcpy(version, sep);
					} else if (!strcmp(ptr, "Max Speed")) {
						strcpy(freq, sep);
					}
				}
				pclose(cpuinfo);
				snprintf(cpu_type, sizeof(cpu_type), "%s %s %s %s\n", manuf, family, version, freq);
				cpuinfo = popen("dmidecode -t cache", "r");
				while(fgets(line, sizeof(line), cpuinfo)) {
					char *sep = strchr(line, ':');
					if (!sep) continue;
					*sep = '\0';
					sep += 2;
					ptr = line+1;
					if (!strcmp(ptr, "Installed Size")) {
						int i = atoi(sep);
						if (i > cachei)
							strcpy(cache_size, sep);
					}
				}
				pclose(cpuinfo);
			}
		}
		fprintf(stdout, "CPU:		%d * %s", num_cpus, cpu_type);
		fprintf(stdout, "CPUCache:   %s", cache_size);
	}
#endif
}

static void PrintHeader() {
	PrintEnvironment();
	fprintf(stdout, "Keys:	   %d bytes each\n", FLAGS_key_size);
	fprintf(stdout, "Values:	 %d bytes each (%d bytes after compression)\n",
			FLAGS_value_size,
			(int)(FLAGS_value_size * FLAGS_compression_ratio + 0.5));
	fprintf(stdout, "Entries:	%ld\n", FLAGS_num);
	fprintf(stdout, "RawSize:	%.1f MB (estimated)\n",
			(((int64_t)(FLAGS_key_size + FLAGS_value_size) * FLAGS_num)
			 / 1048576.0));
	fprintf(stdout, "FileSize:   %.1f MB (estimated)\n",
			(((FLAGS_key_size + FLAGS_value_size * FLAGS_compression_ratio) * FLAGS_num)
			 / 1048576.0));
	PrintWarnings();
	fprintf(stdout, "------------------------------------------------\n");
	fflush(stdout);
}

static void *RunThread(void *v) {
	DBB_local *dl = v;

	pthread_mutex_lock(&dl->dl_global->dg_mu);
	dl->dl_global->dg_initialized++;
	if (dl->dl_global->dg_initialized >= dl->dl_global->dg_threads)
		pthread_cond_broadcast(&dl->dl_global->dg_cv);

	while(!dl->dl_global->dg_start)
		pthread_cond_wait(&dl->dl_global->dg_cv, &dl->dl_global->dg_mu);
	pthread_mutex_unlock(&dl->dl_global->dg_mu);

	dl->dl_done = 0;
	dl->dl_last_report_done = 0;
	dl->dl_bytes = 0;
	dl->dl_seconds = 0;
	dl->dl_max_ops = dl->dl_op == DO_READ ? dl->dl_global->dg_reads : dl->dl_global->dg_num;
	dl->dl_max_seconds = dl->dl_order == DO_RANDOM ? FLAGS_duration : 0;
	dl->dl_message.dv_size = 0;
	dl->dl_message.dv_data = NULL;
	gettimeofday(&dl->dl_start, NULL);
	dl->dl_finish = dl->dl_start;
	dl->dl_last_op_finish = dl->dl_start;
	dl->dl_last_report_finish = dl->dl_start;
	if (dl->dl_wps_by_10)
		dl->dl_lastwrite = dl->dl_start;
	if (dl->dl_op == DO_READ)
		dbb_backend->db_read(dl);
	else
		dbb_backend->db_write(dl);

	gettimeofday(&dl->dl_finish, NULL);
	dl->dl_seconds = dl->dl_finish.tv_sec - dl->dl_start.tv_sec;

	pthread_mutex_lock(&dl->dl_global->dg_mu);
	dl->dl_global->dg_done++;
	if (dl->dl_global->dg_done >= dl->dl_global->dg_threads)
		pthread_cond_broadcast(&dl->dl_global->dg_cv);
	pthread_mutex_unlock(&dl->dl_global->dg_mu);
	return NULL;
}

static void *StatsThread(void *v) {
	DBB_local *args = v;
	DBB_global *dg = args->dl_global;
	int i;
	int banner = 0;

	pthread_mutex_lock(&dg->dg_mu);
	while(!dg->dg_start)
		pthread_cond_wait(&dg->dg_cv, &dg->dg_mu);
	pthread_mutex_unlock(&dg->dg_mu);
	while(1) {
		struct timeval now, end1, end2;
		sleep(FLAGS_stats_period);
		if (dg->dg_done >= dg->dg_threads)
			break;
		if (!banner) {
			banner = 1;
			fprintf(stderr,
			"     Timestamp     	Thread	Cur Ops	Tot Ops	Cur Rate	Avg Rate	Cur Sec 	Tot Sec\n");
		}
		gettimeofday(&now, NULL);
		char buf[20];
		TimeStr(now.tv_sec, buf);
		for (i=0; i<dg->dg_threads; i++) {
			DBB_local *dl = &args[i];
			int64_t done = dl->dl_done;
			end1 = now; end2 = now;
			timesub(&end1, &dl->dl_last_report_finish);
			timesub(&end2, &dl->dl_start);
			fprintf(stderr,
			"%s\t%d\t%zd\t%zd\t%.1f\t%.1f\t%d.%06d\t%d.%06d\n",
			buf, dl->dl_id,
			done - dl->dl_last_report_done, done,
			(float)(done - dl->dl_last_report_done) / end1.tv_sec,
			(float)done / end2.tv_sec,
			(int)end1.tv_sec, (int)end1.tv_usec, (int)end2.tv_sec, (int)end2.tv_usec);
			dl->dl_last_report_done = dl->dl_done;
			dl->dl_last_report_finish = now;
		}
		fflush(stderr);
	}
}

static void RunBenchmark(int n, const char *name, DBB_global *dg) {
	DBB_local *args;
	int i;
	pthread_t stats_thread;

	dg->dg_threads = n;
	dg->dg_initialized = 0;
	dg->dg_done = 0;
	dg->dg_start = 0;

	args = malloc(n * sizeof(DBB_local));
	for (i = 0; i < n; i++) {
		args[i].dl_global = dg;
		args[i].dl_rndctx = seeds[i];
		args[i].dl_hstctx = hists[i];
		args[i].dl_order = dg->dg_order;
		args[i].dl_id = i;
		args[i].dl_randstrpos = 0;
		if (dg->dg_op == DO_READWRITE) {
			if (i)
				args[i].dl_op = DO_READ;
			else {
				args[i].dl_op = DO_WRITE;
				args[i].dl_writes_per_sec = FLAGS_writes_per_second;
				args[i].dl_wps_by_10 = FLAGS_writes_per_second / 10;
				args[i].dl_writes = 0;
				args[i].dl_lastwrite.tv_sec = 0;
				args[i].dl_lastwrite.tv_usec = 0;
			}
		} else {
			args[i].dl_op = dg->dg_op;
			args[i].dl_writes_per_sec = 0;
		}
		if (args[i].dl_op == DO_WRITE)
			GenerateString(&args[i]);
		pthread_create(&args[i].dl_tid, NULL, RunThread, &args[i]);
	}
	if (FLAGS_stats_period)
		pthread_create(&stats_thread, NULL, StatsThread, args);

	pthread_mutex_lock(&dg->dg_mu);
	while (dg->dg_initialized < n) {
		pthread_cond_wait(&dg->dg_cv, &dg->dg_mu);
	}

	dg->dg_start = 1;
	pthread_cond_broadcast(&dg->dg_cv);
	while (dg->dg_done < n) {
		pthread_cond_wait(&dg->dg_cv, &dg->dg_mu);
	}
	pthread_mutex_unlock(&dg->dg_mu);

	if (dg->dg_op == DO_READWRITE) {
		Report(&args[0], "writer");
		for (i = 2; i < n; i++) {
			Merge(&args[1], &args[i]);
		}
		Report(&args[1], name);
	} else {
		for (i = 1; i < n; i++) {
			Merge(&args[0], &args[i]);
		}
		Report(&args[0], name);
	}

	if (FLAGS_stats_period)
		pthread_join(stats_thread, NULL);
	for (i = 0; i < n; i++) {
		DBB_srandom(seeds[i], DBB_random(seeds[i]));
		pthread_join(args[i].dl_tid, NULL);
	}
	free(args);
}

static void killdb() {
#define VM_ALIGN	0x200000
	if (FLAGS_rawdev) {
		int fd;
		char *ptr;
		fd = open(FLAGS_db, O_RDWR);
		ptr = mmap(NULL, VM_ALIGN, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		memset(ptr, 0, 16384);
		msync(ptr, 16384, MS_SYNC);
		munmap(ptr, VM_ALIGN);
		close(fd);
	} else {
		char cmd[200];
		sprintf(cmd, "rm -rf %s", FLAGS_db);
		if (system(cmd)) exit(1);
	}
}

static int db_open;

static void Benchmark() {
	PrintHeader();

	char *benchmarks = (char *)FLAGS_benchmarks;
	DBB_global dg;

	pthread_mutex_init(&dg.dg_mu, NULL);
	pthread_cond_init(&dg.dg_cv, NULL);

	if (!FLAGS_use_existing_db) {
		dbb_backend->db_close();
		killdb();
		db_open = 0;
	}
	while (benchmarks != NULL) {
		char *sep = strchr(benchmarks, ',');
		char *name, namebuf[64];
		int fresh_db = 0;
		int num_threads = FLAGS_threads;
		if (sep == NULL) {
			name = benchmarks;
			benchmarks = NULL;
		} else {
			int len = sep-benchmarks;
			strncpy(namebuf, benchmarks, len);
			benchmarks = sep+1;
			name = namebuf;
			namebuf[len] = '\0';
			benchmarks = NULL;
		}

		dg.dg_num = FLAGS_num;
		dg.dg_reads = (FLAGS_reads < 0) ? FLAGS_num : FLAGS_reads;
		dg.dg_batchsize = 1;
		dg.dg_dbflags = 0;

		if (!strcmp(name, "fillseq")) {
			dg.dg_op = DO_WRITE;
			dg.dg_order = DO_FORWARD;
			fresh_db = 1;
		} else if (!strcmp(name, "fillseqbatch")) {
			dg.dg_op = DO_WRITE;
			dg.dg_order = DO_FORWARD;
			fresh_db = 1;
			dg.dg_batchsize = FLAGS_batch;
		} else if (!strcmp(name, "fillrandom")) {
			dg.dg_op = DO_WRITE;
			dg.dg_order = DO_RANDOM;
			fresh_db = 1;
		} else if (!strcmp(name, "fillrandbatch")) {
			dg.dg_op = DO_WRITE;
			dg.dg_order = DO_RANDOM;
			fresh_db = 1;
			dg.dg_batchsize = FLAGS_batch;
		} else if (!strcmp(name, "overwrite")) {
			dg.dg_op = DO_WRITE;
			dg.dg_order = DO_RANDOM;
		} else if (!strcmp(name, "overwritebatch")) {
			dg.dg_op = DO_WRITE;
			dg.dg_order = DO_RANDOM;
			dg.dg_batchsize = FLAGS_batch;
		} else if (!strcmp(name, "fillrandsync")) {
			dg.dg_op = DO_WRITE;
			dg.dg_order = DO_RANDOM;
			fresh_db = 1;
			dg.dg_num /= 1000;
			if (dg.dg_num<10) dg.dg_num=10;
			dg.dg_dbflags = DBB_SYNC;
		} else if (!strcmp(name, "fillseqsync")) {
			dg.dg_op = DO_WRITE;
			dg.dg_order = DO_FORWARD;
			fresh_db = 1;
			dg.dg_num /= 1000;
			if (dg.dg_num<10) dg.dg_num=10;
			dg.dg_dbflags = DBB_SYNC;
		} else if (!strcmp(name, "readseq")) {
			dg.dg_op = DO_READ;
			dg.dg_order = DO_FORWARD;
		} else if (!strcmp(name, "readreverse")) {
			dg.dg_op = DO_READ;
			dg.dg_order = DO_REVERSE;
		} else if (!strcmp(name, "readrandom")) {
			dg.dg_op = DO_READ;
			dg.dg_order = DO_RANDOM;
		} else if (!strcmp(name, "readwhilewriting")) {
			dg.dg_op = DO_READWRITE;
			dg.dg_order = DO_RANDOM;
			num_threads++;
		} else {
			if (*name) {	// No error message for empty name
				fprintf(stderr, "unknown benchmark '%s'\n", name);
			}
			continue;
		}

		if (fresh_db) {
			if (FLAGS_use_existing_db) {
				fprintf(stdout, "%-12s : skipped (--use_existing_db is true)\n", name);
				continue;
			}
			if (db_open) {
				dbb_backend->db_close();
				killdb();
				db_open = 0;
			}
		}
		if (!db_open) {
			dbb_backend->db_open(dg.dg_dbflags);
			db_open = 1;
		}

		RunBenchmark(num_threads, name, &dg);
		if (dg.dg_op == DO_WRITE || dg.dg_op == DO_READWRITE) {
			char cmd[200];
			sprintf(cmd, "du %s", FLAGS_db);
			if (system(cmd)) exit(1);
		}
		benchmarks = NULL;
	}
	pthread_cond_destroy(&dg.dg_cv);
	pthread_mutex_destroy(&dg.dg_mu);
}

static int benchflag(char *arg) {
	FLAGS_benchmarks = arg;
	return 1;
}

static arg_desc main_args[] = {
	{ "benchmarks", arg_magic, benchflag },
	{ "compression", arg_onoff, &FLAGS_compression },
	{ "compression_ratio", arg_float, &FLAGS_compression_ratio },
	{ "histogram", arg_onoff, &FLAGS_histogram },
	{ "use_existing_db", arg_onoff, &FLAGS_use_existing_db },
	{ "num", arg_long, &FLAGS_num },
	{ "batch", arg_int, &FLAGS_batch },
	{ "reads", arg_long, &FLAGS_reads },
	{ "threads", arg_int, &FLAGS_threads },
	{ "max_threads", arg_int, &FLAGS_max_threads },
	{ "duration", arg_int, &FLAGS_duration },
	{ "stats_period", arg_int, &FLAGS_stats_period },
	{ "writes_per_second", arg_int, &FLAGS_writes_per_second },
	{ "value_size", arg_int, &FLAGS_value_size },
	{ "key_size", arg_int, &FLAGS_key_size },
	{ "db", arg_string, &FLAGS_db },
	{ NULL }
};

static char dirbuf[1024];

int main2(int argc, char *argv[]) {
	int i, aret;
	arg_setup(main_args, dbb_backend->db_args);

	while ((aret = arg_process(argc, argv)) >= 0) {

	/* Choose a location for the test database if none given with --db=<path> */
	if (FLAGS_db == NULL) {
		char *dir = getenv("TEST_TMPDIR");
		if (!dir) {
			dir = dirbuf;
			sprintf(dirbuf, "/tmp/dbbench-%d", geteuid());
		}
		mkdir(dir, 0775);
		strcat(dirbuf, "/");
		strcat(dirbuf, dbb_backend->db_name);
		FLAGS_db = dir;
	}

	{
		struct stat st;
		int rc;
		rc = stat(FLAGS_db, &st);
		if (rc == 0 && (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)))
			FLAGS_rawdev = 1;
	}

	if (!FLAGS_max_threads)
		FLAGS_max_threads = FLAGS_threads;

	if (!seeds)
		/* Set up an extra randctx */
		seeds = calloc(FLAGS_max_threads+1, sizeof(rndctx *));
	for (i=0; i<FLAGS_max_threads+1; i++)
		if (!seeds[i]) seeds[i] = DBB_randctx();
	DBB_srandom(seeds[0], 0);
	for (i=1; i<FLAGS_threads+1; i++)
		DBB_randjump(seeds[i-1], seeds[i]);

	if (!hists)
		hists = calloc(FLAGS_max_threads+1, sizeof(Hstctx *));
	for (i=0; i<FLAGS_max_threads+1; i++)
		if (!hists[i]) hists[i] = DBB_hstctx();
	for (i=0; i<FLAGS_threads+1; i++)
		DBB_hstinit(hists[i]);

	Benchmark();
	if (!aret)
		break;
	break;
	}
	if (db_open)
		dbb_backend->db_close();
	return 0;
}

int main() __attribute__((weak, alias ("main2")));
