/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include "_cgo_export.h"

static BadgerDB *db;

static void db_open(int flags) {
	int rc;
	int env_opt = 0;

	rc = mkdir(FLAGS_db, 0775);
	if (rc && errno != EEXIST) {
		perror("mkdir");
		exit(1);
	}

	if (flags != DBB_SYNC)
		env_opt = BADGER_DB_NOSYNC;

	rc = BadgerOpen((char *)FLAGS_db, env_opt, &db);
	if (rc) {
		fprintf(stderr, "open errors\n");
		exit(1);
	}
}

static void db_close() {
	if (db) {
		BadgerClose(db);
		db = NULL;
	}
}

static void db_write(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;

	DBB_val dkey, dval;
	BadgerTxn *txn;
	char key[100];
	int64_t bytes = 0;

	// Write to database
	unsigned long i = 0;


	if (dg->dg_num != FLAGS_num) {
		char msg[100];
		snprintf(msg, sizeof(msg), "(%ld ops)", dg->dg_num);
		DBB_message(dl, msg);
	}

	dkey.dv_data = key;
	dval.dv_size = FLAGS_value_size;

	do {
		BadgerTxnBegin(db, 0, &txn);

		for (int j=0; j < dg->dg_batchsize; j++) {

			const uint64_t k = (dg->dg_order == DO_FORWARD) ? i+j : (DBB_random(dl->dl_rndctx) % FLAGS_num);
			int rc;
			dkey.dv_size = snprintf(key, sizeof(key), "%016lx", k);
			bytes += FLAGS_value_size + dkey.dv_size;
			DBB_randstring(dl, &dval);

			rc = BadgerPut(txn, &dkey, &dval);
			if (rc) {
				fprintf(stderr, "set error\n");
				break;
			}
			DBB_opdone(dl);
		}
		BadgerTxnCommit(txn);
		i += dg->dg_batchsize;
	} while (!DBB_done(dl));
	dl->dl_bytes += bytes;
}

static void db_read(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;

	BadgerTxn *txn;
	BadgerCursor *cursor;
	DBB_val key, data;
	size_t read = 0;
	size_t found = 0;
	int64_t bytes = 0;
	char ckey[100];
	char *buf;

	int flag = 0;

	key.dv_data = ckey;

	if (dl->dl_order == DO_FORWARD)
		flag = 0;
	else if (dl->dl_order == DO_REVERSE)
		flag = 1;

	if (dl->dl_order != DO_RANDOM) {
		BadgerTxnBegin(db, BADGER_TXN_READONLY, &txn);
		BadgerCursorOpen(txn, flag, &cursor);
	}

	do {
		uint64_t k;
		read++;
		if (dl->dl_order == DO_RANDOM) {
			k = DBB_random(dl->dl_rndctx) % FLAGS_num;
			key.dv_size = snprintf(ckey, sizeof(ckey), "%016lx", k);
			BadgerTxnBegin(db, BADGER_TXN_READONLY, &txn);
			if (!BadgerGet(txn, &key, &data)) {
				found++;
				bytes += key.dv_size + data.dv_size;
			}
		} else {
			if (!BadgerCursorNext(cursor, &key, &data)) {
				found++;
				bytes += key.dv_size + data.dv_size;
			}
		}
		if (dl->dl_order == DO_RANDOM)
			BadgerTxnAbort(txn);
		DBB_opdone(dl);
	} while(!DBB_done(dl));

	dl->dl_bytes += bytes;

	if (dl->dl_order != DO_RANDOM) {
		BadgerCursorClose(cursor);
		BadgerTxnAbort(txn);
	}

	if (dl->dl_order == DO_RANDOM) {
		char msg[100];
		snprintf(msg, sizeof(msg), "(%zd of %zd found)", found, read);
		DBB_message(dl, msg);
	}
}

static char *db_verstr() {
	return "git";
}

static arg_desc db_opts[] = {
	{ NULL }
};

static DBB_backend db_badger = {
	"badger",
	"Badger",
	db_opts,
	db_verstr,
	db_open,
	db_close,
	db_read,
	db_write
};

DBB_backend *dbb_backend = &db_badger;
