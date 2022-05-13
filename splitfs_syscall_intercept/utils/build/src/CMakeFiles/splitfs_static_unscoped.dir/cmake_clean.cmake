file(REMOVE_RECURSE
  "libsplitfs_unscoped.pdb"
  "libsplitfs_unscoped.a"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/splitfs_static_unscoped.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
