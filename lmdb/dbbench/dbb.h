/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include "args.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DBB_val {
	size_t	dv_size;
	void	*dv_data;
} DBB_val;

/* histogram.c */
struct Hstctx;
typedef struct Hstctx Hstctx;
extern void DBB_hstadd(Hstctx *, struct timeval *);
extern void DBB_hstmerge(Hstctx *dst, Hstctx *src);
extern void DBB_hstprint(Hstctx *ctx);
extern void DBB_hstinit(Hstctx *);
extern Hstctx *DBB_hstctx();

/* random.c */
struct rndctx;
typedef struct rndctx rndctx;
extern uint64_t DBB_random(rndctx *);
extern void DBB_srandom(rndctx *, uint64_t seed);
extern void DBB_randjump(rndctx *, rndctx *);
extern rndctx *DBB_randctx();

/* dbflags */
#define DBB_SYNC	1		/* synchronous writes */
#define DBB_INTKEY	2		/* integer keys */

typedef enum DBB_order {
	DO_RANDOM, DO_FORWARD, DO_REVERSE
} DBB_order;

typedef enum DBB_op {
	DO_READ, DO_WRITE, DO_READWRITE
} DBB_op;

typedef struct DBB_global {
	pthread_mutex_t dg_mu;
	pthread_cond_t dg_cv;
	int dg_threads;
	// Worker threads go through the following states:
	//    (1) initializing
	//    (2) waiting for others to be initialized
	//    (3) running
	//    (4) done

	int dg_initialized;
	int dg_done;
	int dg_start;

	int dg_dbflags;
	DBB_order dg_order;
	DBB_op dg_op;

	int64_t dg_num;
	int64_t dg_reads;
	int dg_batchsize;
} DBB_global;

typedef struct DBB_local {
	DBB_global *dl_global;
	rndctx *dl_rndctx;
	Hstctx *dl_hstctx;
	char *dl_randstr;
	DBB_order dl_order;
	DBB_op dl_op;
	pthread_t dl_tid;
	int dl_writes_per_sec;
	int dl_id;

	int dl_randstrpos;

	/* used for readwhilewriting */
	int dl_wps_by_10;
	int dl_writes;
	struct timeval dl_lastwrite;

	/* used for stats reporting */
	struct timeval dl_start;
	struct timeval dl_finish;
	struct timeval dl_last_op_finish;
	struct timeval dl_last_report_finish;
	int64_t dl_done;
	int64_t dl_last_report_done;
	int64_t dl_bytes;
	int64_t dl_max_ops;
	int dl_seconds;
	int dl_max_seconds;
	DBB_val dl_message;
} DBB_local;

typedef char *(verfunc)();
typedef void (openfunc)(int dbflags);
typedef void (closefunc)();
typedef void (readfunc)(DBB_local *arg);
typedef void (writefunc)(DBB_local *arg);
typedef struct DBB_backend {
	const char *db_name;		/* short name of DB engine */
	const char *db_longname;	/* long name of DB engine */
	arg_desc *db_args;			/* DB-specific options */
	verfunc *db_verstr;		/* version of DB engine */
	openfunc *db_open;
	closefunc *db_close;
	readfunc *db_read;
	writefunc *db_write;
} DBB_backend;

/* main.c */
// Number of key/values to place in database
extern int64_t FLAGS_num;

// Number of read operations to do.  If negative, do FLAGS_num reads.
extern int64_t FLAGS_reads;

// Number of concurrent threads to run.
extern int FLAGS_threads;

// Maximum number of concurrent threads to run.
extern int FLAGS_max_threads;

// Time in seconds for the random-ops tests to run.
extern int FLAGS_duration;

// Per-thread rate limit on writes per second.
// Only for the readwhilewriting test.
extern int FLAGS_writes_per_second;

// Stats are reported every N seconds when this is
// greater than zero.
extern int FLAGS_stats_period;

// Size of each value
extern int FLAGS_value_size;

// Size of each key
extern int FLAGS_key_size;

// Number of key/values to store per atomic write.
extern int FLAGS_batch;

// Arrange to generate values that shrink to this fraction of
// their original size after compression
extern float FLAGS_compression_ratio;

// Compression disabled by default
extern int FLAGS_compression;

// Print histogram of operation timings
extern int FLAGS_histogram;

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
extern int FLAGS_use_existing_db;

// Use the db with the following name.
extern const char* FLAGS_db;

void DBB_message(DBB_local *dl, char *msg);
void DBB_randstring(DBB_local *dl, DBB_val *val);
void DBB_opdone(DBB_local *dl);
int DBB_done(DBB_local *dl);

#ifdef __cplusplus
}
#endif
