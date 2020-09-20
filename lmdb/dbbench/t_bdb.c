/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include <db.h>

#include "dbb.h"

// Cache size. Default 4 MB
int64_t FLAGS_cache_size = 4194304;

// Page size. Default is BDB default
int FLAGS_page_size = 0;

static DB_ENV *db;
static DB *dbh;

static void db_open(int flags) {
	int rc;
	DB_TXN *txn;
	int env_opt = DB_REGION_INIT, txn_flags;

	rc = mkdir(FLAGS_db, 0775);
	if (rc && errno != EEXIST) {
		perror("mkdir");
		exit(1);
	}

	// Create tuning options and open the database
	rc = db_env_create(&db, 0);
	rc = db->set_cachesize(db, FLAGS_cache_size >> 30, FLAGS_cache_size & 0x3fffffff, 1);
	rc = db->set_lk_max_locks(db, 100000);
	rc = db->set_lk_max_objects(db, 100000);
	if (flags != DBB_SYNC)
		env_opt |= DB_TXN_WRITE_NOSYNC;
	rc = db->set_flags(db, env_opt, 1);
	rc = db->log_set_config(db, DB_LOG_AUTO_REMOVE, 1);
	txn_flags = DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_TXN|DB_INIT_MPOOL|DB_THREAD;
	if (!FLAGS_use_existing_db)
		txn_flags |= DB_CREATE;
	rc = db->open(db, FLAGS_db, txn_flags, 0664);
	if (rc) {
		fprintf(stderr, "open error: %s\n", db_strerror(rc));
		exit(1);
	}
	rc = db_create(&dbh, db, 0);
	if (FLAGS_page_size)
		rc = dbh->set_pagesize(dbh, FLAGS_page_size);
	rc = dbh->open(dbh, NULL, "data.bdb", NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE|DB_THREAD, 0664);
}

static void db_close() {
	if (dbh) {
		dbh->close(dbh, 0);
		dbh = NULL;
	}
	if (db) {
		db->close(db, 0);
		db = NULL;
	}
}

static void db_write(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;

	DBT mkey, mval;
	DB_TXN *txn;
	DBB_val dv;
	char key[100];
	mkey.data = key;
	mval.size = FLAGS_value_size;
	mkey.flags = 0; mval.flags = 0;
	int64_t bytes = 0;

	// Write to database
	unsigned long i = 0;

	dv.dv_size = FLAGS_value_size;
	
	if (dg->dg_num != FLAGS_num) {
		char msg[100];
		snprintf(msg, sizeof(msg), "(%ld ops)", dg->dg_num);
		DBB_message(dl, msg);
	}
	do {
		db->txn_begin(db, NULL, &txn, 0);

		for (int j=0; j < dg->dg_batchsize; j++) {

			const uint64_t k = (dg->dg_order == DO_FORWARD) ? i+j : (DBB_random(dl->dl_rndctx) % FLAGS_num);
			int rc, flag = 0;
			mkey.size = snprintf(key, sizeof(key), "%016lx", k);
			bytes += FLAGS_value_size + mkey.size;
			DBB_randstring(dl, &dv);
			mval.data = dv.dv_data;
			rc = dbh->put(dbh, txn, &mkey, &mval, 0);
			if (rc) {
				fprintf(stderr, "set error: %s\n", db_strerror(rc));
			}
			DBB_opdone(dl);
		}
		txn->commit(txn, 0);
		i += dg->dg_batchsize;;
		db->txn_checkpoint(db,81920,1,0);
	} while (!DBB_done(dl));
	dl->dl_bytes += bytes;
}

static void db_read(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;

	DB_TXN *txn;
	DBC *cursor;
	DBT key, data;
	char ckey[100];
	int64_t read = 0;
	int64_t found = 0;
	int64_t bytes = 0;

	int op;

	key.flags = 0; data.flags = 0;

	if (dl->dl_order == DO_FORWARD)
		op = DB_NEXT;
	else if (dl->dl_order == DO_REVERSE)
		op = DB_PREV;
	else
		op = DB_SET;

	if (op != DB_SET) {
		db->txn_begin(db, NULL, &txn, DB_READ_COMMITTED);
		dbh->cursor(dbh, txn, &cursor, 0);
	} else {
		key.data = ckey;
	}
	do {
		uint64_t k;
		if (op == DB_SET) {
			k = DBB_random(dl->dl_rndctx) % FLAGS_num;
			key.size = snprintf(ckey, sizeof(ckey), "%016lx", k);
			read++;
			db->txn_begin(db, NULL, &txn, 0);
			dbh->cursor(dbh, txn, &cursor, 0);
		}
		if (!cursor->get(cursor, &key, &data, op)) {
			found++;
			bytes += key.size + data.size;
		}
		if (op == DB_SET) {
			cursor->close(cursor);
			txn->abort(txn);
		}
		DBB_opdone(dl);
	} while(!DBB_done(dl));

	dl->dl_bytes += bytes;
	if (op != DB_SET) {
		cursor->close(cursor);
		txn->abort(txn);
	}

	if (dl->dl_order == DO_RANDOM) {
		char msg[100];
		snprintf(msg, sizeof(msg), "(%zd of %zd found)", found, read);
		DBB_message(dl, msg);
	}
}

static char *db_verstr() {
	return DB_VERSION_STRING;
};

static arg_desc db_opts[] = {
	{ "cache_size", arg_long, &FLAGS_cache_size },
	{ "pagesize", arg_int, &FLAGS_page_size },
	{ NULL }
};

static DBB_backend db_bdb = {
	"bdb",
	"BerkeleyDB",
	db_opts,
	db_verstr,
	db_open,
	db_close,
	db_read,
	db_write
};

DBB_backend *dbb_backend = &db_bdb;

