# Copyright (c) 2017 Howard Chu @ Symas Corp.

MAINSRCS = main.c args.c histogram.c random.c
MAINOBJS = main.o args.o histogram.o random.o

TESTSRCS = t_lmdb.cc
TESTOBJS = t_lmdb.o

BINDIR = bin
TESTS = $(BINDIR)/t_lmdb

OPT = -O2 -DNDEBUG
CC = gcc -pthread
CXX = g++ -pthread
CFLAGS = $(OPT)

all: $(BINDIR) $(TESTS)

$(BINDIR):
	mkdir $(BINDIR)

clean:
	rm -f dbb.o $(MAINOBJS) $(TESTOBJS) $(TESTS)

dbb.o:	$(MAINOBJS)
	$(LD) -r -o $@ $^

$(BINDIR)/t_bdb: dbb.o t_bdb.o
	$(CC) -o $@ $^ -Wl,-Bstatic -ldb -Wl,-Bdynamic -lm

$(BINDIR)/t_lmdb: dbb.o t_lmdb.o
	$(CC) -o $@ $^ -Wl,-Bstatic -llmdb -Wl,-Bdynamic -lsnappy -lm

$(BINDIR)/t_mdbx: dbb.o t_mdbx.o
	$(CC) -o $@ $^ -Wl,-Bstatic -lmdbx -Wl,-Bdynamic -lsnappy -lm

$(BINDIR)/t_leveldb: dbb.o t_leveldb.o
	$(CXX) -o $@ $^ ../leveldb/out-static/libleveldb.a -lsnappy
t_leveldb.o: t_leveldb.cc
	$(CXX) -c $(CFLAGS) -I../leveldb/include $^

$(BINDIR)/t_basho: dbb.o t_basho.o
	$(CXX) -o $@ $^ ../basho_leveldb/libleveldb.a -lsnappy
t_basho.o: t_basho.cc
	$(CXX) -c $(CFLAGS) -I../basho_leveldb/include $^

$(BINDIR)/t_hyper: dbb.o t_hyper.o
	$(CXX) -o $@ $^ ../HyperLevelDB/.libs/libhyperleveldb.a -lsnappy
t_hyper.o: t_hyper.cc
	$(CXX) -c $(CFLAGS) -I../HyperLevelDB/include $^

$(BINDIR)/t_pebbles: dbb.o t_pebbles.o
	$(CXX) -o $@ $^ ../pebblesdb/.libs/libhyperleveldb.a -lsnappy
t_pebbles.o: t_pebbles.cc
	$(CXX) -c $(CFLAGS) -I../pebblesdb/include $^

$(BINDIR)/t_rocksdb: dbb.o t_rocksdb.o
	$(CXX) -o $@ $^ ../rocksdb/librocksdb.a -lsnappy -llz4 -lz -lbz2 -lzstd
t_rocksdb.o: t_rocksdb.cc
	$(CXX) -std=c++11 -c $(CFLAGS) -I../rocksdb/include $^

$(BINDIR)/t_badger: t_badger/t_badger
	cp t_badger/t_badger $(BINDIR)

t_badger/t_badger: t_badger/cbadger.go t_badger/main.go t_badger/t_badger.c $(MAINSRCS)
	cd t_badger; go build
