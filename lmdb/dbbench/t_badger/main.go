/* Copyright (c) 2017 Howard Chu @ Symas Corp. */

package main

/*
#cgo LDFLAGS: -lm
#cgo CFLAGS: -I.. -DNDEBUG
int main2(int argc, char *argv[]);
*/
import "C"

import (
	"os"
)

func main() {
	var argc = len(os.Args)
	var i int
	var argv = make([]*C.char, argc+1)
	for i = 0; i<argc; i++ {
		argv[i] = C.CString(os.Args[i])
	}
	argv[i] = nil
	C.main2(C.int(argc), &argv[0])
}
