file(REMOVE_RECURSE
  "libsplitfs.pdb"
  "libsplitfs.so"
  "libsplitfs.so.0.0.0"
  "libsplitfs.so.0"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/splitfs_shared.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
