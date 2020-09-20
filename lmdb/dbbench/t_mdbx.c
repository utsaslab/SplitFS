/* Copyright (c) 2017-2018 Howard Chu @ Symas Corp. */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include <snappy-c.h>
#include <mdbx.h>

#include "dbb.h"

// Initialize all memory before use
static int FLAGS_cleanmem = 0;

// If false, skip sync of meta pages on synchronous writes
static int FLAGS_metasync = 1;

// If true, use writable mmap
static int FLAGS_writemap = 1;

// The Linux kernel does readahead by default
static int FLAGS_readahead = 1;

// If true, use binary integer keys
static int FLAGS_intkey = 0;

static MDBX_env *env;
static MDBX_dbi dbi;

static void db_open(int flags) {
	int rc;
	MDBX_txn *txn;
	int env_opt = 0;
	char file_name[100], cmd[200];
	size_t msize;

	rc = mkdir(FLAGS_db, 0775);
	if (rc && errno != EEXIST) {
		perror("mkdir");
		exit(1);
	}

	if (flags != DBB_SYNC)
		env_opt = MDBX_NOSYNC;
	else if (!FLAGS_metasync)
		env_opt = MDBX_NOMETASYNC;

	if (FLAGS_writemap)
		env_opt |= MDBX_WRITEMAP;
#ifdef MDBX_CLEANMEM
	if (FLAGS_cleanmem)
		env_opt |= MDBX_CLEANMEM;
#endif
	if (!FLAGS_readahead)
		env_opt |= MDBX_NORDAHEAD;

	// Create tuning options and open the database
	rc = mdbx_env_create(&env);
	msize = FLAGS_num*32L*FLAGS_value_size/10;
	rc = mdbx_env_set_mapsize(env, msize);
	rc = mdbx_env_set_maxreaders(env, FLAGS_threads + 2);
	rc = mdbx_env_open(env, FLAGS_db, env_opt, 0664);
	if (rc) {
		fprintf(stderr, "open error: %s\n", mdbx_strerror(rc));
		exit(1);
	}
	rc = mdbx_txn_begin(env, NULL, 0, &txn);
	rc = mdbx_dbi_open(txn, NULL, FLAGS_intkey ? MDBX_INTEGERKEY:0, &dbi);
	rc = mdbx_txn_commit(txn);
}

static void db_close() {
	if (env) {
		mdbx_env_close(env);
		env = NULL;
	}
}

static void db_write(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;
	
	MDBX_val mkey, mval;
	MDBX_txn *txn;
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
		mkey.iov_base = &ikey;
		mkey.iov_len = sizeof(ikey);
	} else {
		mkey.iov_base = key;
	}
	mval.iov_len = FLAGS_value_size;

	if (dg->dg_order == DO_FORWARD)
		flag = MDBX_APPEND;

	do {
		MDBX_cursor *mc;
		mdbx_txn_begin(env, NULL, 0, &txn);
		mdbx_cursor_open(txn, dbi, &mc);

		for (int j=0; j < dg->dg_batchsize; j++) {

			const uint64_t k = (dg->dg_order == DO_FORWARD) ? i+j : (DBB_random(dl->dl_rndctx) % FLAGS_num);
			int rc;
			if (FLAGS_intkey)
				ikey = k;
			else
				mkey.iov_len = snprintf(key, sizeof(key), "%016lx", k);
			bytes += FLAGS_value_size + mkey.iov_len;
			DBB_randstring(dl, &dv);

			if (FLAGS_compression) {
				snappy_compress(dv.dv_data, dv.dv_size, buf, &mval.iov_len);
				mval.iov_base = buf;
			} else {
				mval.iov_base = dv.dv_data;
				mval.iov_len = dv.dv_size;
			}
			rc = mdbx_cursor_put(mc, &mkey, &mval, flag);
			if (rc) {
				fprintf(stderr, "set error: %s\n", mdbx_strerror(rc));
				break;
			}
			DBB_opdone(dl);
		}
		mdbx_cursor_close(mc);
		mdbx_txn_commit(txn);
		i += dg->dg_batchsize;
	} while (!DBB_done(dl));
	dl->dl_bytes += bytes;
	if (FLAGS_compression)
		free(buf);
}

static void db_read(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;

	MDBX_txn *txn;
	MDBX_cursor *cursor;
	MDBX_val key, data;
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
		key.iov_base = &ikey;
		key.iov_len = sizeof(ikey);
	} else {
		key.iov_base = ckey;
	}

	if (dl->dl_order == DO_FORWARD)
		op = MDBX_NEXT;
	else if (dl->dl_order == DO_REVERSE)
		op = MDBX_PREV;
	else
		op = MDBX_SET;

	mdbx_txn_begin(env, NULL, MDBX_RDONLY, &txn);
	mdbx_cursor_open(txn, dbi, &cursor);

	if (op == MDBX_SET)
		mdbx_txn_reset(txn);

	do {
		uint64_t k;
		if (op == MDBX_SET) {
			k = DBB_random(dl->dl_rndctx) % FLAGS_num;
			if (FLAGS_intkey)
				ikey = k;
			else
				key.iov_len = snprintf(ckey, sizeof(ckey), "%016lx", k);
			mdbx_txn_renew(txn);
			mdbx_cursor_renew(txn, cursor);
		}
		read++;
		if (!mdbx_cursor_get(cursor, &key, &data, op)) {
			if (FLAGS_compression) {
				size_t size;
				snappy_uncompressed_length((const char *)data.iov_base, data.iov_len, &size);
				snappy_uncompress((const char *)data.iov_base, data.iov_len, buf, &data.iov_len);
			} else {
				*(volatile char *)data.iov_base;
			}
			found++;
			bytes += key.iov_len + data.iov_len;
		}
		if (op == MDBX_SET)
			mdbx_txn_reset(txn);
		DBB_opdone(dl);
	} while(!DBB_done(dl));

	dl->dl_bytes += bytes;

	mdbx_cursor_close(cursor);
	mdbx_txn_abort(txn);

	if (dl->dl_order == DO_RANDOM) {
		char msg[100];
		snprintf(msg, sizeof(msg), "(%zd of %zd found)", found, read);
		DBB_message(dl, msg);
	}
	if (FLAGS_compression)
		free(buf);
}

static char *db_verstr() {
	return "0.1";
}

static arg_desc db_opts[] = {
	{ "metasync", arg_onoff, &FLAGS_metasync },
	{ "writemap", arg_onoff, &FLAGS_writemap },
	{ "readahead", arg_onoff, &FLAGS_readahead },
	{ "intkey", arg_onoff, &FLAGS_intkey },
	{ "cleanmem", arg_onoff, &FLAGS_cleanmem },
	{ NULL }
};

static DBB_backend db_mdbx = {
	"mdbx",
	"MDBX",
	db_opts,
	db_verstr,
	db_open,
	db_close,
	db_read,
	db_write
};

DBB_backend *dbb_backend = &db_mdbx;
