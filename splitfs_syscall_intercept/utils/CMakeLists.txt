cmake_minimum_required(VERSION 3.3)
project(splitfs C CXX)

set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

include(FindThreads)
include(CMakeDependentOption)

# XXX rename libpmemfile, since the whole repo is also just
# called pmemfile!
option(BUILD_LIBSPLITFS
    "build libsplitfs (requires libsyscall_intercept)" ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif (NOT CMAKE_BUILD_TYPE)

if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
	execute_process(COMMAND git describe
			OUTPUT_VARIABLE SRCVERSION
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET)
	if(NOT SRCVERSION)
		execute_process(COMMAND git log -1 --format=%h
				OUTPUT_VARIABLE SRCVERSION
				WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
				OUTPUT_STRIP_TRAILING_WHITESPACE)
	endif()
else()
	execute_process(COMMAND cat .version
			OUTPUT_VARIABLE SRCVERSION
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(WIN32)
	message(FATAL_ERROR "Windows is not supported")
endif()

include(FindPerl)
if(NOT PERL_FOUND)
	message(FATAL_ERROR "Perl not found")
endif()
if (PERL_VERSION_STRING VERSION_LESS 5.16)
	message(FATAL_ERROR "Too old Perl (<5.16)")
endif()

add_definitions(-DSRCVERSION="${SRCVERSION}")
add_definitions(-DPMEMFILE_MAJOR_VERSION=${VERSION_MAJOR})
add_definitions(-DPMEMFILE_MINOR_VERSION=${VERSION_MINOR})
if(NOT WIN32)
	add_definitions(-DUSE_VALGRIND)
endif()

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(GNUInstallDirs)
find_package(PkgConfig QUIET)

include_directories(include)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

function(join SEP OUT VALUES)
	string(REPLACE ";" "${SEP}" JOIN_TMP "${VALUES}")
	set(${OUT} "${JOIN_TMP}" PARENT_SCOPE)
endfunction()

# Checks whether flag is supported by current C compiler and appends
# it to the relevant cmake variable.
# 1st argument is a flag
# 2nd (optional) argument is a build type (debug, release, relwithdebinfo)
macro(add_c_flag flag)
	string(REPLACE - _ flag2 ${flag})
	string(REPLACE " " _ flag2 ${flag2})
	string(REPLACE = "_" flag2 ${flag2})
	set(check_name "C_HAS_${flag2}")

	check_c_compiler_flag("${flag}" "${check_name}")

	if (${${check_name}})
		if (${ARGC} EQUAL 1)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${flag}")
		else()
			set(CMAKE_C_FLAGS_${ARGV1} "${CMAKE_C_FLAGS_${ARGV1}} ${flag}")
		endif()
	endif()
endmacro()

# Checks whether flag is supported by current C++ compiler and appends
# it to the relevant cmake variable.
# 1st argument is a flag
# 2nd (optional) argument is a build type (debug, release, relwithdebinfo)
macro(add_cxx_flag flag)
	string(REPLACE - _ flag2 ${flag})
	string(REPLACE " " _ flag2 ${flag2})
	string(REPLACE = "_" flag2 ${flag2})
	set(check_name "CXX_HAS_${flag2}")

	check_cxx_compiler_flag(${flag} ${check_name})

	if (${${check_name}})
		if (${ARGC} EQUAL 1)
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
		else()
			set(CMAKE_CXX_FLAGS_${ARGV1} "${CMAKE_CXX_FLAGS_${ARGV1}} ${flag}")
		endif()
	endif()
endmacro()

# Checks whether flag is supported by current C & C++ compilers and appends
# it to the relevant cmake variables.
# 1st argument is a flag
# 2nd (optional) argument is a build type (debug, release, relwithdebinfo)
macro(add_flag flag)
	add_c_flag(${flag} ${ARGN})
	add_cxx_flag(${flag} ${ARGN})
endmacro()

add_flag(-Wall)
add_flag(-Wpointer-arith)
add_flag(-Wunused-macros)
add_flag(-Wsign-conversion)
add_flag(-Wsign-compare)
add_flag(-Wconversion)
add_flag(-Wunreachable-code-return)
add_flag(-Wmissing-variable-declarations)
add_flag(-fno-common)
add_flag(-Ofast)
add_flag(-funroll-loops)
add_flag(-ftree-vectorize)
add_flag(-m64)

# Conditionally using the -Wno-missing-field-initializers flag.
#
# As of 2017, the following clang erroneously issues a warning for
# the following code:
#
#  struct x { int a; int b; int c; };
#
#  struct x variable = {0};
#
# The warning is meant to warn about fields not being initialized while
# using this syntax in C++, but the clang warns while compiling C as well.
#
# See: https://llvm.org/bugs/show_bug.cgi?id=21689
# See also: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=36750
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
	add_c_flag(-Wno-missing-field-initializers)
endif()

add_c_flag(-Wmissing-prototypes)

check_c_compiler_flag(-Wl,-z,relro LINKER_HAS_RELRO)
if(LINKER_HAS_RELRO)
	set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS}  -Wl,-z,relro")
	set(CMAKE_SHARED_LINKER_FLAGS  "${CMAKE_SHARED_LINKER_FLAGS}  -Wl,-z,relro")
endif()

check_c_compiler_flag(-Wl,--warn-common LINKER_HAS_WARN_COMMON)
if(LINKER_HAS_WARN_COMMON)
	set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -Wl,--warn-common")
	set(CMAKE_SHARED_LINKER_FLAGS  "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--warn-common")
endif()

add_flag(-ggdb RELWITHDEBINFO)
add_flag(-ggdb DEBUG)
add_flag(-DDEBUG DEBUG)

add_flag(-ggdb RELWITHDEBINFO)
add_flag(-fno-omit-frame-pointer RELWITHDEBINFO)

add_flag("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2" RELEASE)

add_flag(-D_LARGEFILE64_SOURCE)

option(USE_ASAN "enable AddressSanitizer (debugging) (using with
BUILD_LIBSPLITFS=ON is experimental)" OFF)
option(USE_UBSAN "enable UndefinedBehaviorSanitizer (debugging) (using with
BUILD_LIBSPLITFS=ON is experimental)" OFF)
set(ASAN_RUNTIME "" CACHE STRING "(experimental) path to preloadable lib for sanitizers e.g.: /usr/lib/gcc/x86_64-linux-gnu/6/libasan.so")

macro(add_sanitizer_flag flag)
    if(BUILD_LIBSPLITFS)
        message(WARNING "Sanitizers might be incompatible with LIBSPLITFS.
        Running tests with sanitizers and BUILD_LIBSPLITFS is experimental")
	endif()

	set(SAVED_CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES} -fsanitize=${flag}")

	check_c_compiler_flag("-fsanitize=${flag}" C_HAS_ASAN_UBSAN)
	if(C_HAS_ASAN_UBSAN)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=${flag}")
	endif()

	check_cxx_compiler_flag("-fsanitize=${flag}" CXX_HAS_ASAN_UBSAN)
	if(CXX_HAS_ASAN_UBSAN)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=${flag}")
	endif()

	if(C_HAS_ASAN_UBSAN OR CXX_HAS_ASAN_UBSAN)
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${flag}")
	endif()

	set(CMAKE_REQUIRED_LIBRARIES ${SAVED_CMAKE_REQUIRED_LIBRARIES})
endmacro()

if(USE_ASAN)
	add_sanitizer_flag(address)
endif()
if(USE_UBSAN)
	add_sanitizer_flag(undefined)
endif()

if(DEVELOPER_MODE)
	add_flag(-Werror)

	check_c_compiler_flag(-Wl,--fatal-warnings LINKER_HAS_FATAL_WARNINGS)
	if(LINKER_HAS_FATAL_WARNINGS)
		set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -Wl,--fatal-warnings")
		set(CMAKE_SHARED_LINKER_FLAGS  "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--fatal-warnings")
	endif()
endif(DEVELOPER_MODE)

# Generates cstyle-$name target and attaches it as a dependency of global
# "cstyle" target. This target verifies C style of files in current source dir.
# If more arguments are used, then they are used as files to be checked
# instead.
# ${name} must be unique.
function(add_cstyle name)
	if(${ARGC} EQUAL 1)
		add_custom_target(cstyle-${name}
			COMMAND ${PERL_EXECUTABLE}
				${CMAKE_SOURCE_DIR}/utils/cstyle
				${CMAKE_CURRENT_SOURCE_DIR}/*.c
				${CMAKE_CURRENT_SOURCE_DIR}/*.h)
	else()
		add_custom_target(cstyle-${name}
			COMMAND ${PERL_EXECUTABLE}
				${CMAKE_SOURCE_DIR}/utils/cstyle ${ARGN})
	endif()
	add_dependencies(cstyle cstyle-${name})
endfunction()

# Generates cppstyle-$name and cppformat-$name targets and attaches them
# as dependencies of global "cstyle" and "cppformat" targets.
# cppstyle-$name target verifies C++ style of files in current source dir.
# cppformat-$name target reformats files in current source dir.
# If more arguments are used, then they are used as files to be checked
# instead.
# ${name} must be unique.
function(add_cppstyle name)
	if(NOT CLANG_FORMAT)
		return()
	endif()

	if(${ARGC} EQUAL 1)
		add_custom_target(cppstyle-${name}
			COMMAND ${PERL_EXECUTABLE}
				${CMAKE_SOURCE_DIR}/utils/cppstyle
				${CLANG_FORMAT}
				check
				${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
				${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
			)
		add_custom_target(cppformat-${name}
			COMMAND ${PERL_EXECUTABLE}
				${CMAKE_SOURCE_DIR}/utils/cppstyle
				${CLANG_FORMAT}
				format
				${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
				${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
			)
	else()
		add_custom_target(cppstyle-${name}
			COMMAND ${PERL_EXECUTABLE}
				${CMAKE_SOURCE_DIR}/utils/cppstyle
				${CLANG_FORMAT}
				check
				${ARGN}
			)
		add_custom_target(cppformat-${name}
			COMMAND ${PERL_EXECUTABLE}
				${CMAKE_SOURCE_DIR}/utils/cppstyle
				${CLANG_FORMAT}
				format
				${ARGN}
			)
	endif()

	add_dependencies(cppstyle cppstyle-${name})
	add_dependencies(cppformat cppformat-${name})
endfunction()

# Generates check-whitespace-$name target and attaches it as a dependency
# of global "check-whitespace" target. This target verifies C files in current
# source dir do not have any whitespace errors.
# If more arguments are used, then they are used as files to be checked
# instead.
# ${name} must be unique.
function(add_check_whitespace name)
	if(${ARGC} EQUAL 1)
		add_custom_target(check-whitespace-${name}
			COMMAND ${PERL_EXECUTABLE}
				${CMAKE_SOURCE_DIR}/utils/check_whitespace
				${CMAKE_CURRENT_SOURCE_DIR}/*.c
				${CMAKE_CURRENT_SOURCE_DIR}/*.h)
	else()
		add_custom_target(check-whitespace-${name}
			COMMAND ${PERL_EXECUTABLE}
				${CMAKE_SOURCE_DIR}/utils/check_whitespace ${ARGN})
	endif()
	add_dependencies(check-whitespace check-whitespace-${name})
endfunction()

add_custom_target(checkers ALL)
add_custom_target(cstyle)
add_custom_target(cppstyle)
add_custom_target(cppformat)
add_custom_target(check-whitespace)

add_custom_target(check-whitespace-main
		COMMAND ${PERL_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/utils/check_whitespace)
add_dependencies(check-whitespace check-whitespace-main)

add_custom_target(tests)

option(DEVELOPER_MODE "enable developer checks" OFF)
if(DEVELOPER_MODE)
	find_program(CLANG_FORMAT NAMES clang-format-3.9 clang-format-3.8)
	if (NOT CLANG_FORMAT)
		message(WARNING "clang-format not found - C++ sources will not be checked (needed version: 3.8 or 3.9)")
	endif()

	execute_process(COMMAND ${PERL_EXECUTABLE} -MText::Diff -e ""
			ERROR_QUIET
			RESULT_VARIABLE PERL_TEXT_DIFF_STATUS)
	if (PERL_TEXT_DIFF_STATUS)
		message(FATAL_ERROR "Text::Diff Perl module not found (install libtext-diff-perl or perl-Text-Diff)")
	endif()

	add_dependencies(checkers cstyle)
	add_dependencies(checkers cppstyle)
	add_dependencies(checkers check-whitespace)
endif(DEVELOPER_MODE)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
	IMMEDIATE @ONLY)

add_custom_target(uninstall
	COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

if(PKG_CONFIG_FOUND)
	pkg_check_modules(LIBUNWIND QUIET libunwind)
else()
	find_package(LIBUNWIND QUIET)
endif()
if(NOT LIBUNWIND_FOUND)
	message(WARNING "libunwind not found. Stack traces from tests will not be reliable")
endif()

if(BUILD_LIBSPLITFS)
	add_subdirectory(src)
endif()

