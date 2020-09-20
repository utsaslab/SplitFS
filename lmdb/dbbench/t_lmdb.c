/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include <snappy-c.h>
#include <lmdb.h>

#include "dbb.h"

// Initialize all memory before use
static int FLAGS_cleanmem = 0;

// If false, skip sync of meta pages on synchronous writes
static int FLAGS_metasync = 1;

// If true, use writable mmap
static int FLAGS_writemap = 1;

// The Linux kernel does readahead by default
static int FLAGS_readahead = 1;

#ifdef MDB_DIRECTIO
// Use direct I/O
static int FLAGS_directio = 0;

// If true, set explicit page size
static int FLAGS_pagesize = 0;
#endif

// If true, use binary integer keys
static int FLAGS_intkey = 0;

static MDB_env *env;
static MDB_dbi dbi;

static void db_open(int flags) {
	int rc;
	MDB_txn *txn;
	int env_opt = 0;
	char file_name[100], cmd[200];
	size_t msize;

	rc = mkdir(FLAGS_db, 0775);
	if (rc && errno != EEXIST) {
		perror("mkdir");
		exit(1);
	}

	if (flags != DBB_SYNC)
		env_opt = MDB_NOSYNC;
	else if (!FLAGS_metasync)
		env_opt = MDB_NOMETASYNC;

	if (FLAGS_writemap)
		env_opt |= MDB_WRITEMAP;
	if (!FLAGS_readahead)
		env_opt |= MDB_NORDAHEAD;
#ifdef MDB_CLEANMEM
	if (FLAGS_cleanmem)
		env_opt |= MDB_CLEANMEM;
#endif
#ifdef MDB_DIRECTIO
	if (FLAGS_directio)
		env_opt |= MDB_DIRECTIO;
#endif

	// Create tuning options and open the database
	rc = mdb_env_create(&env);
	msize = FLAGS_num*32L*FLAGS_value_size/10;
#ifdef MDB_DIRECTIO
	if (FLAGS_pagesize)
		rc = mdb_env_set_pagesize(env, FLAGS_pagesize);
#endif
	rc = mdb_env_set_mapsize(env, msize);
	rc = mdb_env_set_maxreaders(env, FLAGS_max_threads + 2);
	rc = mdb_env_open(env, FLAGS_db, env_opt, 0664);
	if (rc) {
		fprintf(stderr, "open error: %s\n", mdb_strerror(rc));
		exit(1);
	}
	rc = mdb_txn_begin(env, NULL, 0, &txn);
	rc = mdb_open(txn, NULL, FLAGS_intkey ? MDB_INTEGERKEY:0, &dbi);
	rc = mdb_txn_commit(txn);
}

static void db_close() {
	if (env) {
		mdb_env_close(env);
		env = NULL;
	}
}

static void db_write(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;
	
	MDB_val mkey, mval;
	MDB_txn *txn;
	DBB_val dv;
	char *buf;
	char key[100];
	int flag = 0;
	unsigned long ikey;
	int64_t bytes = 0;

	// Write to database
	unsigned long i = 0;

	dv.dv_size = FLAGS_value_size;

	if (dg->dg_num != FLAGS_num) {
		char msg[100];
		snprintf(msg, sizeof(msg), "(%ld ops)", dg->dg_num);
		DBB_message(dl, msg);
	}

	if (FLAGS_compression)
		buf = (char *)malloc(FLAGS_value_size);

	if (FLAGS_intkey) {
		mkey.mv_data = &ikey;
		mkey.mv_size = sizeof(ikey);
	} else {
		mkey.mv_data = key;
	}
	mval.mv_size = FLAGS_value_size;

	if (dg->dg_order == DO_FORWARD)
		flag = MDB_APPEND;

	do {
		MDB_cursor *mc;
		mdb_txn_begin(env, NULL, 0, &txn);
		mdb_cursor_open(txn, dbi, &mc);

		for (int j=0; j < dg->dg_batchsize; j++) {

			const uint64_t k = (dg->dg_order == DO_FORWARD) ? i+j : (DBB_random(dl->dl_rndctx) % FLAGS_num);
			int rc;
			if (FLAGS_intkey)
				ikey = k;
			else
				mkey.mv_size = snprintf(key, sizeof(key), "%016lx", k);
			bytes += FLAGS_value_size + mkey.mv_size;
			DBB_randstring(dl, &dv);

			if (FLAGS_compression) {
				snappy_compress(dv.dv_data, dv.dv_size, buf, &mval.mv_size);
				mval.mv_data = buf;
			} else {
				mval.mv_data = dv.dv_data;
				mval.mv_size = dv.dv_size;
			}
			rc = mdb_cursor_put(mc, &mkey, &mval, flag);
			if (rc) {
				fprintf(stderr, "set error: %s\n", mdb_strerror(rc));
				break;
			}
			DBB_opdone(dl);
		}
		mdb_cursor_close(mc);
		mdb_txn_commit(txn);
		i += dg->dg_batchsize;
	} while (!DBB_done(dl));
	dl->dl_bytes += bytes;
	if (FLAGS_compression)
		free(buf);
}

static void db_read(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;

	MDB_txn *txn;
	MDB_cursor *cursor;
	MDB_val key, data;
	size_t read = 0;
	size_t found = 0;
	int64_t bytes = 0;
	char ckey[100];
	int ikey;
	char *buf;

	int op;

	if (FLAGS_compression)
		buf = (char *)malloc(FLAGS_value_size);

	if (FLAGS_intkey) {
		key.mv_data = &ikey;
		key.mv_size = sizeof(ikey);
	} else {
		key.mv_data = ckey;
	}

	if (dl->dl_order == DO_FORWARD)
		op = MDB_NEXT;
	else if (dl->dl_order == DO_REVERSE)
		op = MDB_PREV;
	else
		op = MDB_SET;

	mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
	mdb_cursor_open(txn, dbi, &cursor);

	if (op == MDB_SET)
		mdb_txn_reset(txn);

	do {
		uint64_t k;
		if (op == MDB_SET) {
			k = DBB_random(dl->dl_rndctx) % FLAGS_num;
			if (FLAGS_intkey)
				ikey = k;
			else
				key.mv_size = snprintf(ckey, sizeof(ckey), "%016lx", k);
			mdb_txn_renew(txn);
			mdb_cursor_renew(txn, cursor);
		}
		read++;
		if (!mdb_cursor_get(cursor, &key, &data, op)) {
			if (FLAGS_compression) {
				size_t size;
				snappy_uncompressed_length((const char *)data.mv_data, data.mv_size, &size);
				snappy_uncompress((const char *)data.mv_data, data.mv_size, buf, &data.mv_size);
			} else {
				*(volatile char *)data.mv_data;
			}
			found++;
			bytes += key.mv_size + data.mv_size;
		}
		if (op == MDB_SET)
			mdb_txn_reset(txn);
		DBB_opdone(dl);
	} while(!DBB_done(dl));

	dl->dl_bytes += bytes;

	mdb_cursor_close(cursor);
	mdb_txn_abort(txn);

	if (dl->dl_order == DO_RANDOM) {
		char msg[100];
		snprintf(msg, sizeof(msg), "(%zd of %zd found)", found, read);
		DBB_message(dl, msg);
	}
	if (FLAGS_compression)
		free(buf);
}

static char *db_verstr() {
	return MDB_VERSION_STRING;
}

static arg_desc db_opts[] = {
	{ "metasync", arg_onoff, &FLAGS_metasync },
	{ "writemap", arg_onoff, &FLAGS_writemap },
	{ "readahead", arg_onoff, &FLAGS_readahead },
	{ "intkey", arg_onoff, &FLAGS_intkey },
	{ "cleanmem", arg_onoff, &FLAGS_cleanmem },
#ifdef MDB_DIRECTIO
	{ "directio", arg_onoff, &FLAGS_directio },
	{ "pagesize", arg_int, &FLAGS_pagesize },
#endif
	{ NULL }
};

static DBB_backend db_lmdb = {
	"lmdb",
	"LMDB",
	db_opts,
	db_verstr,
	db_open,
	db_close,
	db_read,
	db_write
};

DBB_backend *dbb_backend = &db_lmdb;
