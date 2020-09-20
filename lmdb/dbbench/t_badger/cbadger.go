/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

package main

/*
#include "dbb.h"
struct BadgerDB;
struct BadgerTxn;
struct BadgerCursor;

typedef struct BadgerDB BadgerDB;
typedef struct BadgerTxn BadgerTxn;
typedef struct BadgerCursor BadgerCursor;

#define BADGER_DB_NOSYNC	1
#define BADGER_TXN_READONLY	1
*/
import "C"

import (
	"github.com/dgraph-io/badger"
	"unsafe"
)

//export BadgerOpen
func BadgerOpen(dir *C.char, flags C.int, p0 **C.BadgerDB ) int {
	var d0 unsafe.Pointer
	d0 = (unsafe.Pointer)(p0)
	db := (**badger.DB)(d0)
	opt := badger.DefaultOptions
	opt.Dir = C.GoString(dir)
	opt.ValueDir = opt.Dir
	if (flags & C.BADGER_DB_NOSYNC) != 0 {
		opt.SyncWrites = false
	}
	var err error
	*db, err = badger.Open(&opt)
	if err != nil {
		return -1
	}
	return 0
}

//export BadgerClose
func BadgerClose(p0 *C.BadgerDB) int {
	var d0 unsafe.Pointer
	d0 = (unsafe.Pointer)(p0)
	db := (*badger.DB)(d0)
	err := db.Close()
	if err != nil {
		return -1
	}
	return 0
}

//export BadgerTxnBegin
func BadgerTxnBegin(p0 *C.BadgerDB, flags C.int, p1 **C.BadgerTxn) {
	var d0, t0 unsafe.Pointer
	d0 = (unsafe.Pointer)(p0)
	db := (*badger.DB)(d0)
	t0 = (unsafe.Pointer)(p1)
	txn := (**badger.Txn)(t0)
	var update bool
	update = (flags & C.BADGER_TXN_READONLY) == 0
	*txn = db.NewTransaction(update)
}

//export BadgerTxnCommit
func BadgerTxnCommit(p0 *C.BadgerTxn) int {
	var t0 unsafe.Pointer
	t0 = (unsafe.Pointer)(p0)
	txn := (*badger.Txn)(t0)
	err := txn.Commit(nil)
	if err != nil {
		return -1
	}
	return 0
}

//export BadgerTxnAbort
func BadgerTxnAbort(p0 *C.BadgerTxn) {
	var t0 unsafe.Pointer
	t0 = (unsafe.Pointer)(p0)
	txn := (*badger.Txn)(t0)
	txn.Discard()
}

type slicer struct {
	ptr unsafe.Pointer
	len int
	cap int
}

func valslice(val *C.DBB_val, slc *[]byte) {
	var kptr unsafe.Pointer
	kptr = (unsafe.Pointer)(slc)
	var sptr *slicer
	sptr = (*slicer)(kptr)
	sptr.ptr = val.dv_data
	sptr.len = int(val.dv_size)
	sptr.cap = sptr.len
}

//export BadgerGet
func BadgerGet(p0 *C.BadgerTxn, key *C.DBB_val, val *C.DBB_val) int {
	var t0 unsafe.Pointer
	t0 = (unsafe.Pointer)(p0)
	txn := (*badger.Txn)(t0)
	var ks []byte
	valslice(key, &ks)
	i, err := txn.Get(ks)
	if err != nil {
		return -1
	}
	v, err := i.Value()
	val.dv_size = C.size_t(len(v))
	val.dv_data = unsafe.Pointer(&v[0])
	return 0
}

//export BadgerPut
func BadgerPut(p0 *C.BadgerTxn, key *C.DBB_val, val *C.DBB_val) int {
	var t0 unsafe.Pointer
	t0 = (unsafe.Pointer)(p0)
	txn := (*badger.Txn)(t0)
	var ks []byte
	var vs []byte
	valslice(key, &ks)
	valslice(val, &vs)
	err := txn.Set(ks, vs, 0)
	if err != nil {
		return -1
	}
	return 0
}

//export BadgerCursorOpen
func BadgerCursorOpen(p0 *C.BadgerTxn, flags C.int, p1 **C.BadgerCursor) {
	var t0, c0 unsafe.Pointer
	t0 = (unsafe.Pointer)(p0)
	txn := (*badger.Txn)(t0)
	c0 = (unsafe.Pointer)(p1)
	cursor := (**badger.Iterator)(c0)
	opt := badger.DefaultIteratorOptions
	if (flags & 1) != 0 {
		opt.Reverse = true
	}
	*cursor = txn.NewIterator(opt)
	(*cursor).Rewind()
}

//export BadgerCursorNext
func BadgerCursorNext(p0 *C.BadgerCursor, key *C.DBB_val, val *C.DBB_val) int {
	var c0 unsafe.Pointer
	c0 = (unsafe.Pointer)(p0)
	cursor := (*badger.Iterator)(c0)
	if !cursor.Valid() {
		return -1
	}
	it := cursor.Item()
	v, err := it.Value()
	if err != nil {
		return -1
	}
	val.dv_size = C.size_t(len(v))
	val.dv_data = unsafe.Pointer(&v[0])
	k := it.Key()
	key.dv_size = C.size_t(len(k))
	key.dv_data = unsafe.Pointer(&k[0])
	cursor.Next()
	return 0
}

//export BadgerCursorClose
func BadgerCursorClose(p0 *C.BadgerCursor) {
	var c0 unsafe.Pointer
	c0 = (unsafe.Pointer)(p0)
	cursor := (*badger.Iterator)(c0)
	cursor.Close()
}
