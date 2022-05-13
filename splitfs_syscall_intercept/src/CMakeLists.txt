set(SOURCES
    add_delay.c
    execv.c
    fallocate.c
    file.c
    fsync.c
    handle_mmaps.c
    hash_map.c
    inode.c
    intel_intrin.c
    lseek.c
    mmap_pool.c
    os_thread_pthread.c
    os_util_linux.c
    out.c
    path_resolve.c
    preload.c
    read.c
    relink.c
    splitfs-posix.c
    staging.c
    staging_thread.c
    stat.c
    syscall_early_filter.c
    table_mmaps.c
    truncate.c
    unlink.c
    utils.c
    vfd_table.c
    write.c
    )

set(EXPORTED_SYMBOLS
    splitfs_fallocate
    splitfs_file
    splitfs_fsync
    splitfs_lseek
    splitfs_read
    splitfs_stat
    splitfs_truncate
    splitfs_unlink
    splitfs_write
    splitfs_execv
    )

if(PKG_CONFIG_FOUND)
	pkg_check_modules(SYSCALL_INTERCEPT libsyscall_intercept)
	pkg_check_modules(CAP libcap)
else()
	find_package(SYSCALL_INTERCEPT QUIET)
	find_package(CAP QUIET)
endif()

if(NOT SYSCALL_INTERCEPT_FOUND)
	message(FATAL_ERROR
		"libsyscall_intercept not found - needed by libpmemfile
		to skip building libpmemfile, set the BUILD_LIBPMEMFILE option to OFF")
endif()

if(NOT CAP_FOUND)
	message(FATAL_ERROR
		"libcap not found - needed by libpmemfile
		to skip building libpmemfile, set the BUILD_LIBPMEMFILE option to OFF")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# XXX OBJECT library type is not supported on some old cmake versions
add_library(splitfs_o OBJECT ${SOURCES})

include_directories(${SYSCALL_INTERCEPT_INCLUDE_DIRS} ..)
link_directories(${SYSCALL_INTERCEPT_LIBRARY_DIRS})

add_library(splitfs_shared SHARED $<TARGET_OBJECTS:splitfs_o>)
add_library(splitfs_static_unscoped STATIC $<TARGET_OBJECTS:splitfs_o>)

target_link_libraries(splitfs_shared PRIVATE ${SYSCALL_INTERCEPT_LIBRARIES})
target_link_libraries(splitfs_shared PRIVATE ${CAP_LIBRARIES})
target_link_libraries(splitfs_shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(splitfs_shared PRIVATE ${CMAKE_DL_LIBS})
target_link_libraries(splitfs_shared PRIVATE rt)
target_link_libraries(splitfs_shared PRIVATE
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libsplitfs.map)

set_target_properties(splitfs_static_unscoped PROPERTIES OUTPUT_NAME splitfs_unscoped)
set_target_properties(splitfs_shared PROPERTIES OUTPUT_NAME splitfs)
set_target_properties(splitfs_shared PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})

add_custom_command(OUTPUT libsplitfs.a
		COMMAND objcopy --localize-hidden `sed -n
        "'s/^\\s*\\([a-zA-Z0-9_]*\\);$$/-G \\1/p'"
        ${CMAKE_CURRENT_SOURCE_DIR}/libsplitfs.map` libsplitfs_unscoped.a
        libsplitfs.a
		DEPENDS splitfs_static_unscoped)
add_custom_target(splitfs_static ALL DEPENDS libsplitfs.a)

install(TARGETS splitfs_shared LIBRARY
	CONFIGURATIONS Release None RelWithDebInfo
	DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(TARGETS splitfs_shared LIBRARY
	CONFIGURATIONS Debug
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/splitfs_debug)

#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile.a
#	CONFIGURATIONS Release None
#	DESTINATION ${CMAKE_INSTALL_LIBDIR})

#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile.a
#	CONFIGURATIONS Debug
#	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pmemfile_debug)

add_cstyle(splitfs)
add_check_whitespace(splitfs)
