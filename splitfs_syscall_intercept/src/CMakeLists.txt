#
# Copyright 2017, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set(HUB_SOURCES
    fileops_hub.c
    nvp_printf.c
    )

set(NVP_SOURCES
    add_delay.c
    dup.c
    file.c
    fsync.c
    handle_mmaps.c
    ioctl.c
    log.c
    mmap_cache.c
    non_temporal.c
    nvp_printf.c
    read.c
    relink.c
    seek.c
    splitfs_posix.c
    stack.c
    staging.c
    tbl_mmaps.c
    timers.c
    truncate.c
    unlink.c
    write.c
    )

set(EXPORTED_SYMBOLS
    splitfs_dup
    splitfs_file
    splitfs_fsync
    splitfs_ioctl
    splitfs_read
    splitfs_seek
    splitfs_truncate
    splitfs_unlink
    splitfs_write
    )

if(PKG_CONFIG_FOUND)
	pkg_check_modules(SYSCALL_INTERCEPT libsyscall_intercept)
	pkg_check_modules(CAP libcap)
else()
	find_package(SYSCALL_INTERCEPT QUIET)
	find_package(CAP QUIET)
endif()

if(NOT CAP_FOUND)
	message(FATAL_ERROR
		"libcap not found - needed by libpmemfile
		to skip building libpmemfile, set the BUILD_LIBPMEMFILE option to OFF")
endif()

if(NOT SYSCALL_INTERCEPT_FOUND)
	message(FATAL_ERROR
		"libsyscall_intercept not found - needed by libpmemfile
		to skip building libpmemfile, set the BUILD_LIBPMEMFILE option to OFF")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_definitions(-DPRINT_DEBUG_FILE=0 -DDATA_JOURNALING_ENABLED=1
    -DPOSIX_ENABLED=0 -DNVM_DELAY=0 -DSYSCALL_APPENDS=0 -DPASS_THROUGH_CALLS=0
    -DINSTRUMENT_CALLS=0 -DSHOW_DEBUG=0 -DSPIN_ON_ERROR=0 -DUSE_PTHREAD_LOCK=0
    -DUSE_SCHED_GETCPU=1 -DINTEGRITY_CHECK=0 -DMEASURE_TIMING=0
    -DUSE_SINGLE_LOCK=0 -DENABLE_FSYNC_TO_BS=0 -DENABLE_FSYNC_TO_CACHE=0
    -DENABLE_FALLOC=1 -DUSE_BTREE=1 -DUNMAP_ON_CLOSE=0)

# XXX OBJECT library type is not supported on some old cmake versions

add_library(fileops_nvp_o OBJECT ${NVP_SOURCES})

add_library(nvp_shared SHARED $<TARGET_OBJECTS:fileops_nvp_o>)
add_library(nvp_static_unscoped STATIC $<TARGET_OBJECTS:fileops_nvp_o>)

target_link_libraries(nvp_shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(nvp_shared PRIVATE ${CMAKE_DL_LIBS})
target_link_libraries(nvp_shared PRIVATE
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libsplitfs.map)

set_target_properties(nvp_static_unscoped PROPERTIES OUTPUT_NAME fileops_nvp_unscoped)
set_target_properties(nvp_shared PROPERTIES OUTPUT_NAME fileops_nvp)
#set_target_properties(nvp_shared PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})

add_custom_command(OUTPUT libfileops_nvp.a
		COMMAND objcopy --localize-hidden `sed -n
        "'s/^\\s*\\([a-zA-Z0-9_]*\\);$$/-G \\1/p'"
        ${CMAKE_CURRENT_SOURCE_DIR}/libsplitfs.map` libfileops_nvp_unscoped.a
        libfileops_nvp.a
		DEPENDS nvp_static_unscoped)
add_custom_target(nvp_static ALL DEPENDS libfileops_nvp.a)

install(TARGETS nvp_shared LIBRARY
	CONFIGURATIONS Release None RelWithDebInfo
	DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(TARGETS nvp_shared LIBRARY
	CONFIGURATIONS Debug
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/nvp_debug)

add_library(splitfs_o OBJECT ${HUB_SOURCES})

add_library(splitfs_shared SHARED $<TARGET_OBJECTS:splitfs_o>)
add_library(splitfs_static_unscoped STATIC $<TARGET_OBJECTS:splitfs_o>)

target_link_libraries(splitfs_shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(splitfs_shared PRIVATE ${CMAKE_DL_LIBS})
target_link_libraries(splitfs_shared PRIVATE nvp_shared)
target_link_libraries(splitfs_shared PRIVATE
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libsplitfs.map)

set_target_properties(splitfs_static_unscoped PROPERTIES OUTPUT_NAME splitfs_unscoped)
set_target_properties(splitfs_shared PROPERTIES OUTPUT_NAME splitfs)
#set_target_properties(splitfs_shared PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})

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
