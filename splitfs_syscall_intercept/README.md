# SplitFS

###How to use
1. At the root of the repo create a build directory: `mkdir build && cd build`
2. Do cmake to generate makefile: `cmake ../` # Include -DCMAKE_BUILD_TYPE=Debug if you want to enable logging.
3. Run make: `make`
4. You should see the .so file at `build/src/libsplitfs.so`
5. Run application using: `LD_PRELOAD=<repo_root>/build/src/libsplitfs.so <command_to_run_your_app>`
