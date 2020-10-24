## Implementation details
Some of the implementation details of intercepted calls in SplitFS
- `fallocate, posix_fallocate`  
  - We pass this to the kernel.  
  - But before we pass this on to the kernel we fsync (relink) the file so that the kernel and SplitFS both see the file contents and metadata consistently.  
  - We update the file size after the system call accordingly in SplitFS before returning to the application.
  - TODO: Figure out if ext4 implementation of fallocate will move the existing blocks to a new location (maybe to make it contiguous?). If yes, we will also have clear the mmap table in SplitFS because they might get stale after the system call.
- `sync_file_range`  
  - sync_file_range guarantees data durability only for overwrites on certain filesystems. It does not guarantee metadata durability on any filesystem.  
  - In case of POSIX mode of SplitFS too, we guarantee data durability and not metadata durability, i.e we want to provide the same guarantees as posix.
  - The data durability is guaranteed by virtue of doing non temporal writes to the memory mapped file, so we don't really need to do anything here. In case where the file is not memory mapped (for e.g file size < 16MB) we pass it on to the underlying filesystem.
  - In case of Sync and Strict mode in SplitFS, this is guaranteed by the filesystemitself and sync_file_range is not required for durability.
- `O_CLOEXEC`
  - This is supported via `open` and `fcntl` in SplitFS. We store this flag value in SplitFS.
  - In the supported `exec` calls, we first close the files before passing the `exec` call to the kernel.  
  - We do not currently handle the failure scenario for `exec`
- `fcntl`
  - Currently in SplitFS we only handle value of the `close on exec` flag before it is passed through to the kernel.