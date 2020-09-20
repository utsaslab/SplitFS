/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/transaction_db.h>

#include "dbb.h"

// Number of bytes to buffer in memtable before compacting
// (initialized to default value by "main")
static int FLAGS_write_buffer_size = 0;

// Approximate size of user data packed per block (before compression.
// (initialized to default value by "main")
static int FLAGS_block_size = 0;

// Number of bytes to use as a cache of uncompressed data.
// Negative means use default settings.
static int64_t FLAGS_cache_size = -1;

// Maximum number of files to keep open at the same time (use default if == 0)
static int FLAGS_open_files = 0;

// Bloom filter bits per key.
// Negative means use default settings.
static int FLAGS_bloom_bits = -1;

using namespace rocksdb;

static std::shared_ptr<Cache> cache;
static std::shared_ptr<const FilterPolicy> filter_policy;
static TransactionDB *db;
static WriteOptions write_options;

static void db_open(int dbflags) {
	BlockBasedTableOptions block_based_options;
	Options options;
	options.create_if_missing = !FLAGS_use_existing_db;

	if (!FLAGS_write_buffer_size)
		FLAGS_write_buffer_size = Options().write_buffer_size;
	if (!FLAGS_block_size)
		FLAGS_block_size = BlockBasedTableOptions().block_size;
	if (!FLAGS_open_files)
		FLAGS_open_files = Options().max_open_files;
	if (FLAGS_cache_size >= 0 && !cache)
		cache = NewLRUCache(FLAGS_cache_size);
	if (FLAGS_bloom_bits >= 0 && !filter_policy)
		filter_policy = std::shared_ptr<const FilterPolicy>(NewBloomFilterPolicy(FLAGS_bloom_bits, false));

	if (cache)
		block_based_options.block_cache = cache;
	else
		block_based_options.no_block_cache = true;
	block_based_options.block_size = FLAGS_block_size;
	block_based_options.filter_policy = filter_policy;

	options.write_buffer_size = FLAGS_write_buffer_size;
	options.max_open_files = FLAGS_open_files;
	options.compression = FLAGS_compression != 0 ? kSnappyCompression : kNoCompression;
	options.table_factory.reset(NewBlockBasedTableFactory(block_based_options));
	write_options = WriteOptions();
	if (dbflags & DBB_SYNC)
		write_options.sync = true;

	TransactionDBOptions txn_db_options;
	Status s = TransactionDB::Open(options, txn_db_options, FLAGS_db, &db);
	if (!s.ok()) {
		fprintf(stderr, "open error: %s\n", s.ToString().c_str());
		exit(1);
	}
}

static void db_close() {
	delete db;
	db = NULL;
}

static void db_write(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;

	if (dg->dg_num != FLAGS_num) {
		char msg[100];
		snprintf(msg, sizeof(msg), "(%ld ops)", dg->dg_num);
		DBB_message(dl, msg);
	}

	DBB_val dv;
	Status s;
	int64_t bytes = 0;
	unsigned long i = 0;
	dv.dv_size = FLAGS_value_size;
	do {
		Transaction *txn = db->BeginTransaction(write_options);
		for (int j = 0; j < dg->dg_batchsize; j++) {
			const uint64_t k = (dg->dg_order == DO_FORWARD) ? i+j : (DBB_random(dl->dl_rndctx) % FLAGS_num);
			char key[100];
			snprintf(key, sizeof(key), "%016lx", k);
			DBB_randstring(dl, &dv);
			txn->Put(key, Slice((const char *)dv.dv_data, dv.dv_size));
			bytes += FLAGS_value_size + FLAGS_key_size;
			DBB_opdone(dl);
		}
		s = txn->Commit();
		delete txn;
		if (!s.ok()) {
			fprintf(stderr, "put error: %s\n", s.ToString().c_str());
			exit(1);
		}
		i += dg->dg_batchsize;
	} while (!DBB_done(dl));
	dl->dl_bytes += bytes;
}

static void db_read(DBB_local *dl) {
	DBB_global *dg = dl->dl_global;

	int64_t bytes = 0;
	if (dl->dl_order == DO_RANDOM) {
		ReadOptions options;
		std::string value;
		size_t read = 0;
		size_t found = 0;
		char key[100];
		do {
			const uint64_t k = DBB_random(dl->dl_rndctx) % FLAGS_num;
			snprintf(key, sizeof(key), "%016lx", k);
			read++;
			Transaction *txn = db->BeginTransaction(write_options);
			if (txn->Get(options, key, &value).ok()) {
				bytes += FLAGS_key_size + value.size();
				found++;
			}
			delete txn;
			DBB_opdone(dl);
		} while (!DBB_done(dl));
		char msg[100];
		snprintf(msg, sizeof(msg), "(%zd of %zd found)", found, read);
		DBB_message(dl, msg);
	} else {
		ReadOptions options;
		int i = 0;
		Transaction *txn = db->BeginTransaction(write_options);
		Iterator* iter = txn->GetIterator(options);
		void (Iterator::*seek)();
		void (Iterator::*next)();
		if (dl->dl_order == DO_FORWARD) {
			seek = &Iterator::SeekToFirst;
			next = &Iterator::Next;
		} else {
			seek = &Iterator::SeekToLast;
			next = &Iterator::Prev;
		}
		for ((iter->*seek)(); i < dg->dg_reads && iter->Valid(); (iter->*next)()) {
			bytes += iter->key().size() + iter->value().size();
			DBB_opdone(dl);
			++i;
		}
		delete iter;
		delete txn;
	}
	dl->dl_bytes += bytes;
}

static char *db_verstr() {
	static char vstr[32];
	snprintf(vstr, sizeof(vstr), "%d.%d.%d", ROCKSDB_MAJOR, ROCKSDB_MINOR, ROCKSDB_PATCH);
	return vstr;
}

static arg_desc db_opts[] = {
	{ "write_buffer_size", arg_int, &FLAGS_write_buffer_size },
	{ "block_size", arg_int, &FLAGS_block_size },
	{ "cache_size", arg_long, &FLAGS_cache_size },
	{ "open_files", arg_int, &FLAGS_open_files },
	{ "bloom_bits", arg_int, &FLAGS_bloom_bits },
	{ NULL }
};

static DBB_backend db_rocksdb = {
	"rocksdb",
	"RocksDB",
	db_opts,
	db_verstr,
	db_open,
	db_close,
	db_read,
	db_write
};

extern DBB_backend *dbb_backend;
DBB_backend *dbb_backend = &db_rocksdb;

