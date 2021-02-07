// This serves as a common framework for any libraries serving runtime-determined file ops.
// Does aliasing for ALIAS_*, redirecting them to whatever internal functions are desired.

/*
#define fopen fopen_orig
#include <stdio.h>
#undef fopen
*/

#include "nv_common.h"
#include "ledger.h"

#define ENV_HUB_FOP "NVP_HUB_FOP"

#define ENV_TREE_FILE "NVP_TREE_FILE"

//#define LIBC_SO_LOC "/lib64/libc-2.5.so"
#define LIBC_SO_LOC "/lib/x86_64-linux-gnu/libc.so.6"

// for a given file descriptor (index), stores the fileops to use on that fd
// all vlaues initialized to the posix ops
struct Fileops_p** _hub_fd_lookup;
struct Fileops_p*  _hub_managed_fileops;

void* _libc_so_p;

int _hub_fileops_count = 0;
struct Fileops_p* _hub_fileops_lookup[MAX_FILEOPS];

int _hub_add_and_resolve_fileops_tree_from_file(char* filename);

int hub_check_resolve_fileops(char* tree);
void _hub_init2(void);

#define HUB_CHECK_RESOLVE_FILEOPS(NAME, FUNCT)  \
	if (_hub_managed_fileops == NULL && _hub_fileops == NULL) \
		_hub_init();					  \
	assert( _hub_managed_fileops != NULL );			  \
	assert( _hub_fileops         != NULL );

#define HUB_ADD_FUNCTP(SOFILE, FUNCT)					\
	dlsym_result = dlsym(SOFILE, MK_STR3( ALIAS_##FUNCT ) );	\
	if(!dlsym_result) {						\
		ERROR("Couldn't find symbol \"%s\" for " #FUNCT " in \"" #SOFILE "\"\n", MK_STR3( ALIAS_##FUNCT ) ); \
		ERROR("%s\n", dlerror());				\
		assert(0);						\
	}								\
	fo->FUNCT = (RETT_##FUNCT(*)(INTF_##FUNCT)) dlsym_result ;

#define HUB_ADD_SHM_FUNCTP(SOFILE, FUNCT)				\
	dlsym_result = dlsym(SOFILE, MK_STR3( _nvp_##FUNCT ) );		\
	if(!dlsym_result) {						\
		ERROR("Couldn't find symbol \"%s\" for " #FUNCT " in \"" #SOFILE "\"\n", MK_STR3( ALIAS_##FUNCT ) ); \
		ERROR("%s\n", dlerror());				\
		assert(0);						\
	}								\
	fo->FUNCT = (RETT_##FUNCT(*)(INTF_##FUNCT)) dlsym_result ;

#define HUB_ADD_FUNCTP_IWRAP(r, data, elem) HUB_ADD_FUNCTP(data, elem) 
#define HUB_ADD_FUNCTP_SHM_IWRAP(r, data, elem) HUB_ADD_SHM_FUNCTP(data, elem) 

#define HUB_INIT_FILEOPS_P(NAME, SOFILELOC)				\
	so_p = dlopen(SOFILELOC, RTLD_LAZY|RTLD_LOCAL);			\
	if(!so_p) {							\
		ERROR("Couldn't locate \"%s\" at supplied path \"%s\"\n", NAME, SOFILELOC); \
		ERROR("%s\n", dlerror());				\
		assert(0);						\
	}								\
	struct Fileops_p* fo = (struct Fileops_p*) calloc(1, sizeof(struct Fileops_p)); \
	fo->name = (char*)calloc(strlen(NAME)+1, sizeof(char));		\
	fo->name = strcpy(fo->name, NAME);				\
	DEBUG("Populating %s Fileops_p from \"%s\"\n", NAME, SOFILELOC); \
	void* dlsym_result;						\
	BOOST_PP_SEQ_FOR_EACH(HUB_ADD_FUNCTP_IWRAP, so_p, ALLOPS_WPAREN) \
	BOOST_PP_SEQ_FOR_EACH(HUB_ADD_FUNCTP_IWRAP, so_p, METAOPS) 	\
	BOOST_PP_SEQ_FOR_EACH(HUB_ADD_FUNCTP_SHM_IWRAP, so_p, SHM_WPAREN) \
	

#define HUB_ADD_POSIX_FUNCTP(FUNCT)					\
	dlsym_result = dlsym(_libc_so_p, MK_STR3( ALIAS_##FUNCT ) );	\
	if(!dlsym_result) {						\
		ERROR("Couldn't find symbol \"%s\" for " #FUNCT " in \"" LIBC_SO_LOC "\"\n", MK_STR3( ALIAS_##FUNCT ) ); \
		ERROR("%s\n", dlerror());				\
		assert(0);						\
	}								\
	fo->FUNCT = (RETT_##FUNCT(*)(INTF_##FUNCT)) dlsym_result ;

#define HUB_ADD_POSIX_FUNCTP_IWRAP(r, data, elem) HUB_ADD_POSIX_FUNCTP(elem) 

#define HUB_INIT_POSIX_FILEOPS_P(NAME)					\
	struct Fileops_p* fo = (struct Fileops_p*) calloc(1, sizeof(struct Fileops_p)); \
	fo->name = (char*)calloc(strlen(NAME)+1, sizeof(char));		\
	fo->name = strcpy(fo->name, NAME);				\
	DEBUG("Populating hub Fileops_p from libc.so at \"" LIBC_SO_LOC "\"\n" ); \
	void* dlsym_result;						\
	BOOST_PP_SEQ_FOR_EACH(HUB_ADD_POSIX_FUNCTP_IWRAP, xxx, ALLOPS_WPAREN) \
	BOOST_PP_SEQ_FOR_EACH(HUB_ADD_POSIX_FUNCTP_IWRAP, xxx, METAOPS) \
	_hub_add_fileop(fo); 


int _hub_add_and_resolve_fileops_tree_from_file(char* filename)
{
	if(filename==NULL) {
		ERROR("Filename was null! Can't build tree! (Did you forget to set env var %s?)\n", ENV_TREE_FILE);
		assert(0);
	} else {
		DEBUG("Got filename %s\n", filename);
	}

	if(access(filename, R_OK)) {
		ERROR("Couldn't open file %s for reading: %s\n", filename, strerror(errno));
		assert(0);
	} else {
		DEBUG("File %s is OK for reading\n", filename);
	}

	DEBUG("Reading from file %s\n", filename);

	//FILE *file = NULL;
	char *buffer;
	unsigned long fileLen;

	/*
	//Open file
	printf("%s: here calling fopen, name of file = %s\n", __func__, filename);

	
	file = fopen(filename, "r");
	if (!file)
	{
		ERROR("Unable to open file %s", filename);
		assert(0);
	}
	
	//Get file length
	fseek(file, 0, SEEK_END);
	fileLen = ftell(file);
	fseek(file, 0, SEEK_SET);
	*/
	//printf("%s: filename = %s, fileLen = %d\n", __func__, filename, fileLen);
	
	//Allocate memory
	fileLen  = 22;
	buffer = (char*) calloc(fileLen+1, sizeof(char));
	if (!buffer)
	{
		ERROR("Memory error!");
		//fclose(file);
		assert(0);
	}
	//fread(buffer, fileLen, 1, file);
	//fclose(file);

	memcpy(buffer, "hub(posix,nvp(posix))\n", 22);

	char* modules_to_load[MAX_FILEOPS];
	int module_count = 0;

	char* b = (char*) calloc(strlen(buffer)+1, sizeof(char));
	memcpy(b, buffer, strlen(buffer)+1);

	char* tok = strtok(b, "(), \t\n");
	
	while(tok)
	{
		//DEBUG("Got token \"%s\"\n", tok);
		if(module_count == MAX_FILEOPS) {
			ERROR("Too many fileops!  Change MAX_FILEOPS in nv_common.h (current max is %i)\n", MAX_FILEOPS);
			assert(0);
		}
		int i;
		int cmp_match = 0;
		for(i=0; i<module_count; i++) {
			if(!strcmp(modules_to_load[i], tok)) {
				cmp_match = 1;
				break;
			}
		}
		if(cmp_match) {
			DEBUG("Module %s was already in the list; skipping.\n", tok);
		} else {
		//	DEBUG("Adding module %s\n", tok);
			modules_to_load[module_count] = calloc(strlen(tok)+1, sizeof(char));
			memcpy(modules_to_load[module_count], tok, strlen(tok)+1);
			module_count++;
		}
		tok = strtok(NULL, "(), \t\n");
	}

	if(strcmp(modules_to_load[0],"hub")) {
		ERROR("Invalid format: first item must be hub\n");
		assert(0);
	}

	char* tree = (char*) calloc(fileLen, sizeof(char));
	int i;
	char c[2] = { '\0', '\0' };

	// strip whitespace from the tree
	for(i=0; i<fileLen; i++)
	{
		if(!isspace(buffer[i]))
		{
			c[0] = buffer[i];
			strcat(tree, c);
		}
	}

	DEBUG("Here's the tree without whitespace: %s\n", tree);

        DEBUG("%i modules will be loaded (not counting hub).  Here are their names:\n", module_count-1);
	char fname[256];
	void* so_p;
	for(i=1; i<module_count; i++)
	{
		sprintf(fname, "libfileops_%s.so", modules_to_load[i]);
	        DEBUG("%s (%s)\n", modules_to_load[i], fname);
	}
	for(i=1; i<module_count; i++)
	{
		sprintf(fname, "libfileops_%s.so", modules_to_load[i]);
		if(strcmp(modules_to_load[i],"posix")==0) {
			DEBUG("Module \"posix\" is loaded manually; skipping.\n");
		} else {
			DEBUG("Loading module \"%s\"\n", modules_to_load[i]);
			HUB_INIT_FILEOPS_P(modules_to_load[i], fname);
		}
	}

	DEBUG("Done adding fileops.  Resolving all fileops...\n");

	_hub_resolve_all_fileops(tree);

	DEBUG("Done initializing hub and resolving fileops.\n");

	return 0;
}


// Declare and do aliasing for every function with finite parameters.
BOOST_PP_SEQ_FOR_EACH(DECLARE_AND_ALIAS_FUNCTS_IWRAP, _hub_, ALLOPS_FINITEPARAMS_WPAREN)

// OPEN and IOCTL don't have finite parameters; declare and alias them manually.
RETT_OPEN ALIAS_OPEN(INTF_OPEN) WEAK_ALIAS("_hub_OPEN");
RETT_OPEN  _hub_OPEN(INTF_OPEN);

RETT_CREAT ALIAS_CREAT(INTF_CREAT) WEAK_ALIAS("_hub_CREAT");
RETT_CREAT  _hub_CREAT(INTF_CREAT);

RETT_OPENAT ALIAS_OPENAT(INTF_OPENAT) WEAK_ALIAS("_hub_OPENAT");
RETT_OPENAT _hub_OPENAT(INTF_OPENAT);

RETT_EXECVE ALIAS_EXECVE(INTF_EXECVE) WEAK_ALIAS("_hub_EXECVE");
RETT_EXECVE _hub_EXECVE(INTF_EXECVE);

RETT_EXECVP ALIAS_EXECVP(INTF_EXECVP) WEAK_ALIAS("_hub_EXECVP");
RETT_EXECVP _hub_EXECVP(INTF_EXECVP);

RETT_EXECV ALIAS_EXECV(INTF_EXECV) WEAK_ALIAS("_hub_EXECV");
RETT_EXECV _hub_EXECV(INTF_EXECV);

RETT_MKNOD ALIAS_MKNOD(INTF_MKNOD) WEAK_ALIAS("_hub_MKNOD");
RETT_MKNOD  _hub_MKNOD(INTF_MKNOD);

RETT_MKNODAT ALIAS_MKNODAT(INTF_MKNODAT) WEAK_ALIAS("_hub_MKNODAT");
RETT_MKNODAT  _hub_MKNODAT(INTF_MKNODAT);

RETT_SHM_COPY _hub_SHM_COPY();

#ifdef TRACE_FP_CALLS				 
RETT_FOPEN ALIAS_FOPEN(INTF_FOPEN) WEAK_ALIAS("_hub_FOPEN"); 
RETT_FOPEN  _hub_FOPEN(INTF_FOPEN); 

RETT_FOPEN64 ALIAS_FOPEN64(INTF_FOPEN64) WEAK_ALIAS("_hub_FOPEN64");
RETT_FOPEN64  _hub_FOPEN64(INTF_FOPEN64);									    
#endif 

RETT_IOCTL ALIAS_IOCTL(INTF_IOCTL) WEAK_ALIAS("_hub_IOCTL");
RETT_IOCTL  _hub_IOCTL(INTF_IOCTL);

RETT_OPEN64 ALIAS_OPEN64(INTF_OPEN64) WEAK_ALIAS("_hub_OPEN64");
RETT_OPEN64  _hub_OPEN64(INTF_OPEN64);

RETT_MKSTEMP ALIAS_MKSTEMP(INTF_MKSTEMP) WEAK_ALIAS("_hub_MKSTEMP");
RETT_MKSTEMP  _hub_MKSTEMP(INTF_MKSTEMP);

RETT_MKDIR ALIAS_MKDIR(INTF_MKDIR) WEAK_ALIAS("_hub_MKDIR");
RETT_MKDIR  _hub_MKDIR(INTF_MKDIR);

RETT_RENAME ALIAS_RENAME(INTF_RENAME) WEAK_ALIAS("_hub_RENAME");
RETT_RENAME  _hub_RENAME(INTF_RENAME);

RETT_RMDIR ALIAS_RMDIR(INTF_RMDIR) WEAK_ALIAS("_hub_RMDIR");
RETT_RMDIR  _hub_RMDIR(INTF_RMDIR);

RETT_LINK ALIAS_LINK(INTF_LINK) WEAK_ALIAS("_hub_LINK");
RETT_LINK  _hub_LINK(INTF_LINK);

RETT_SYMLINK ALIAS_SYMLINK(INTF_SYMLINK) WEAK_ALIAS("_hub_SYMLINK");
RETT_SYMLINK  _hub_SYMLINK(INTF_SYMLINK);

RETT_SYMLINKAT ALIAS_SYMLINKAT(INTF_SYMLINKAT) WEAK_ALIAS("_hub_SYMLINKAT");
RETT_SYMLINKAT  _hub_SYMLINKAT(INTF_SYMLINKAT);

RETT_MKDIRAT ALIAS_MKDIRAT(INTF_MKDIRAT) WEAK_ALIAS("_hub_MKDIRAT");
RETT_MKDIRAT  _hub_MKDIRAT(INTF_MKDIRAT);

RETT_TRUNC ALIAS_TRUNC(INTF_TRUNC) WEAK_ALIAS("_hub_TRUNC");
RETT_TRUNC  _hub_TRUNC(INTF_TRUNC);

MODULE_REGISTRATION_F("hub", _hub_, _hub_init2(); )


// Creates the set of standard posix functions as a module.
void _hub_init2(void)
{
	execv_done = 0;
	int i = 0;
	MSG("%s: START\n", __func__);
	DEBUG("Initializing the libnvp hub.  If you're reading this, the library is being loaded! (this is PID %i, my parent is %i)\n", getpid(), getppid());
	DEBUG("Call tree will be parsed from %s\n", getenv(ENV_TREE_FILE));

	DEBUG("Initializing posix module\n");

	_libc_so_p = dlopen(LIBC_SO_LOC, RTLD_LAZY|RTLD_LOCAL);

	if(!_libc_so_p) {
		ERROR("Couldn't locate libc.so at supplied path \"" LIBC_SO_LOC "\"\n");
		ERROR("%s\n", dlerror());
		assert(0);
	}
	
	HUB_INIT_POSIX_FILEOPS_P("posix");

	_hub_fd_lookup = (struct Fileops_p**) calloc(OPEN_MAX, sizeof(struct Fileops_p*));
	
	assert(_hub_find_fileop("hub")!=NULL);
	_hub_find_fileop("hub")->resolve = hub_check_resolve_fileops;

	DEBUG("%s: calling add and resolve fileops for nvp\n", __func__);
	_hub_add_and_resolve_fileops_tree_from_file(getenv(ENV_TREE_FILE));

	DEBUG("Currently printing on stderr\n");

	_nvp_print_fd = fdopen(_hub_find_fileop("posix")->DUP(2), "a"); 
        debug_fd = _hub_find_fileop("posix")->FOPEN("/tmp/ledger_dbg.tmp", "a");
	DEBUG("Now printing on fd %p\n", _nvp_print_fd);
	assert(_nvp_print_fd >= 0);

	if (execv_done)
		_hub_SHM_COPY();
 out:
	execv_done = 0;
	
	//_nvp_debug_handoff();
	MSG("%s: END\n", __func__);
}


// used instead of the default fileops resolver
int hub_check_resolve_fileops(char* tree)
{
	//printf("%s: start\n", __func__);
	RESOLVE_TWO_FILEOPS("hub", _hub_fileops, _hub_managed_fileops);

	int i;
	for(i=0; i<OPEN_MAX; i++)
	{
		_hub_fd_lookup[i] = _hub_fileops;
	}

	return 0;
}

#define HUB_WRAP_HAS_FD(op)						\
	RETT_##op _hub_##op ( INTF_##op ) {				\
		HUB_CHECK_RESOLVE_FILEOPS(_hub_, op);			\
		RETT_##op result;					\
		DEBUG_FILE("CALL: _hub_" #op "\n");			\
		if(file>=OPEN_MAX) { MSG("file descriptor too large (%i > %i)\n", file, OPEN_MAX-1); errno = EBADF; return (RETT_##op) -1; } \
		if(file<0) { MSG("file < 0 (file = %i).  return -1;\n", file); errno = EBADF; return (RETT_##op) -1; } \
		if(_hub_fd_lookup[file]==NULL) { MSG("_hub_"#op": That file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file); errno = EBADF; return -1; } \
	        DEBUG("_hub_" #op " is calling %s->" #op "\n", _hub_fd_lookup[file]->name); \
		result = (RETT_##op) _hub_fd_lookup[file]->op( CALL_##op ); \
		DEBUG_FILE("_hub_" #op " returns\n");			\
		return result;						\
	}

#define HUB_WRAP_NO_FD(op)						\
	RETT_##op _hub_##op ( INTF_##op ) {				\
		int result = 0;						\
		HUB_CHECK_RESOLVE_FILEOPS(_hub_, op);			\
		DEBUG("CALL: " MK_STR(_hub_##op) "\n");			\
		DEBUG("_hub_" #op " is calling %s->" #op "\n", _hub_fileops->name); \
                result = _hub_fileops->op( CALL_##op );			\
		return result;						\
	}

#define HUB_WRAP_PIPE()					\
	RETT_PIPE _hub_PIPE(INTF_PIPE) {		\
		int result = 0;				\
		HUB_CHECK_RESOLVE_FILEOPS(_hub_, op);	\
		result = _hub_fileops->PIPE(CALL_PIPE);	\
		_hub_fd_lookup[file[0]] = _hub_fileops;	\
		_hub_fd_lookup[file[1]] = _hub_fileops;	\
		return result;				\
	}

#define HUB_WRAP_SOCKET()						\
	RETT_SOCKETPAIR _hub_SOCKETPAIR(INTF_SOCKETPAIR) {		\
		int result = 0;						\
		HUB_CHECK_RESOLVE_FILEOPS(_hub_, op);			\
		result = _hub_fileops->SOCKETPAIR(CALL_SOCKETPAIR);	\
		_hub_fd_lookup[sv[0]] = _hub_fileops;			\
		_hub_fd_lookup[sv[1]] = _hub_fileops;			\
		return result;						\
	}

#ifdef TRACE_FP_CALLS
#define HUB_WRAP_HAS_FP(op)						\
	RETT_##op _hub_##op ( INTF_##op ) {				\
		HUB_CHECK_RESOLVE_FILEOPS(_hub_, op);			\
		DEBUG("CALL: _hub_" #op "\n");				\
		if(fileno(fp)>=OPEN_MAX) { DEBUG("file descriptor too large (%i > %i)\n", fileno(fp), OPEN_MAX-1); errno = EBADF; return (RETT_##op) -1; } \
		if(fileno(fp)<0) { DEBUG("file < 0 (file = %i).  return -1;\n", fileno(fp)); errno = EBADF; return (RETT_##op) -1; } \
		if(_hub_fd_lookup[fileno(fp)]==NULL) { DEBUG("_hub_"#op": That file descriptor (%i) is invalid: perhaps you didn't call open first?\n", fileno(fp)); errno = EBADF; return -1; } \
		DEBUG("_hub_" #op " is calling %s->" #op "\n", _hub_fd_lookup[fileno(fp)]->name); \
		return (RETT_##op) _hub_fd_lookup[fileno(fp)]->op( CALL_##op );	\
	}

#define HUB_WRAP_HAS_FP_IWRAP(r, data, elem) HUB_WRAP_HAS_FP(elem)
BOOST_PP_SEQ_FOR_EACH(HUB_WRAP_HAS_FP_IWRAP, placeholder, FILEOPS_WITH_FP)
#endif

#define HUB_WRAP_HAS_FD_IWRAP(r, data, elem) HUB_WRAP_HAS_FD(elem)
#define HUB_WRAP_NO_FD_IWRAP(r, data, elem) HUB_WRAP_NO_FD(elem)
#define HUB_WRAP_PIPE_IWRAP(r, data, elem) HUB_WRAP_PIPE()
#define HUB_WRAP_SOCKET_IWRAP(r, data, elem) HUB_WRAP_SOCKET()

BOOST_PP_SEQ_FOR_EACH(HUB_WRAP_HAS_FD_IWRAP, placeholder, FILEOPS_WITH_FD)
BOOST_PP_SEQ_FOR_EACH(HUB_WRAP_NO_FD_IWRAP, placeholder, FILEOPS_WITHOUT_FD)
BOOST_PP_SEQ_FOR_EACH(HUB_WRAP_PIPE_IWRAP, placeholder, FILEOPS_PIPE)
BOOST_PP_SEQ_FOR_EACH(HUB_WRAP_SOCKET_IWRAP, placeholder, FILEOPS_SOCKET)

void _hub_resolve_all_fileops(char* tree)
{
	// do a resolve for all ops in the table so far
	int i;
	DEBUG("_hub_fileops_lookup contains %i elements, and here they are:\n", _hub_fileops_count);
	for(i=0; i<_hub_fileops_count; i++)
	{
		DEBUG("\t%s\n", _hub_fileops_lookup[i]->name);
	}

	for(i=0; i<_hub_fileops_count; i++)
	{
		DEBUG("Resoving %i of %i: %s\n", i+1, _hub_fileops_count, _hub_fileops_lookup[i]->name);

		if(!strcmp(_hub_fileops_lookup[i]->name, "posix")) { continue; }
		
		_hub_fileops_lookup[i]->resolve(tree);
	}
}

struct Fileops_p* default_resolve_fileops(char* tree, char* name)
{
	DEBUG("Resolving \"%s\" fileops using default resolver.\n", name);
	
	struct Fileops_p* fileops = *(resolve_n_fileops(tree, name, 1));

	if(fileops == NULL){
		ERROR("Couldn't resolve fileops %s\n", name);
		assert(0);
	}
	DEBUG("\"%s\" resolved to \"%s\"\n", name, fileops->name);
	return fileops;
}

struct Fileops_p** resolve_n_fileops(char* tree, char* name, int count)
{
	struct Fileops_p** result = (struct Fileops_p**) calloc(count, sizeof(struct Fileops_p*));

	char* start = strstr(tree, name);
	if(!start){
		ERROR("Coudln't find this module (%s) in the tree (%s)\n", name, tree);
		assert(0);
	}
	
	start += strlen(name)+1;

	int slot = 0;
	for(slot = 0; slot<count; slot++)
	{
	//	DEBUG("Attempting to fill slot %i\n", slot);

		int paren_level = 0;
		int i;
		// find the next punctuation mark
		for(i=0; i<strlen(start); i++)
		{
			if(ispunct(start[i])) { break; }
		}

		char* module_name = (char*) calloc(i+3, sizeof(char));
		module_name = memcpy(module_name, start, i);

	//	DEBUG("Module %s looking for module %s for slot %i\n", name, module_name, slot);
		
		result[slot] = _hub_find_fileop(module_name);
		if(result[slot] == NULL){
			ERROR("Couldn't resolve fileops %s slot %i to expected module %s\n", name, slot, module_name);
			assert(0);
		} else {
	//		DEBUG("Successfully resolved slot %i to module %s\n", slot, result[slot]->name);
		}

		// if we have more tokens to find, skip to the start of the next one
		// otherwise fuhgeddaboudit
		if(slot+1>=count) {
	//		DEBUG("That was the last token; we out.\n");
			continue;
		}
	//	DEBUG("Old start: %s\n", start);
		start += i;
	//	DEBUG("New start: %s\n", start);

		// if it's a comma, the next element follows immediately
		if(start[0] == ',') {
	//		DEBUG("Next element was a comma; it's a leaf.\n");
			start++;
			continue;
		}
		
		// we have to do some digging to find the next element
		assert(start[0]=='(');
	//	DEBUG("Next element was a paren; it's an internal node.\n");
		start++;
		paren_level=1;
		for(i=0; i<strlen(start); i++)
		{
			//DEBUG("%c\n", start[i]);
			switch(start[i]) {
				case '(': paren_level++; break;
				case ')': paren_level--; break;
			}
			if(paren_level == 0) {
				start += i+2;
				break;
			}
		}
	//	DEBUG("After skipping stuff, start looks like this: %s\n", start);
	}
	return result;
}



// registers a module to be searched later
void _hub_add_fileop(struct Fileops_p* fo)
{

	DEBUG("Registering Fileops_p \"%s\" at index %i\n",
		fo->name, _hub_fileops_count);

	int i=0;
	for(i=0; i<_hub_fileops_count; i++)
	{
		if(!strcmp(_hub_fileops_lookup[i]->name, fo->name))
		{
			MSG("Can't add fileop %s: one with the same name already exists at index %i\n", fo->name, i);
			//assert(0);
			return;
		}
	}

	if(_hub_fileops_count >= MAX_FILEOPS) {
		ERROR("_hub_fileops_lookup is full: too many Fileops_p!\n");
		ERROR("Maximum supported: %i\n", MAX_FILEOPS);
		ERROR("Check fileops_compareharness.c to increase\n");
		return;
	}

	_hub_fileops_lookup[_hub_fileops_count] = fo;
	_hub_fileops_count++;
}


// given the name of a Fileops_p, returns the index.
// if the specified fileop is not found, will return the index of "posix"
// or -1 if "posix" isn't found (critical failure)
struct Fileops_p* _hub_find_fileop(const char* name)
{
	if(name == NULL) {
		DEBUG("Name was null; using default: \"posix\"\n");
		name = "posix";
		assert(0);
	}

	int i;
	for(i=0; i<_hub_fileops_count; i++)
	{
		if(_hub_fileops_lookup[i] == NULL) { break; }
		if(strcmp(name, _hub_fileops_lookup[i]->name)==0)
		{
			return _hub_fileops_lookup[i];
		}
	}

	DEBUG("Fileops_p \"%s\" not found; resolving to \"posix\"\n", name);
	name = "posix";
	assert(0);

	for(i=0; i<_hub_fileops_count; i++)
	{
		if(_hub_fileops_lookup[i] == NULL) { break; }
		if(strcmp(name, _hub_fileops_lookup[i]->name)==0)
		{
			return _hub_fileops_lookup[i];
		}
	}

	assert(0);

	return NULL;
}


RETT_OPEN _hub_OPEN(INTF_OPEN)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, OPEN);
	int access_result;

	DEBUG_FILE("_hub_OPEN = %s\n", path);
	access_result = access(path, F_OK);
	/**
	 * We need to check if 'path' is a valid pointer, but not crash it 
	 * (segfault) if it's invalid.
	 * 
	 * Since it is not possible to validate a pointer in the user-space, 
	 * we are making an access system call which validates  
	 * the pointer.
	 */
	if(access_result == -1 && errno == EFAULT) {
		return -1;
	}
        DEBUG_FILE("CALL: _hub_OPEN for (%s)\n", path);
	struct Fileops_p* op_to_use = NULL;
	int result;

	op_to_use = _hub_managed_fileops;	

	/* In case of absoulate path specified, check if it belongs to the persistent memory 
	 * mount and only then use SplitFS, else redirect to POSIX
	 */
	if(path[0] == '/') {
		int len = strlen(NVMM_PATH);
		char dest[len + 1];
		dest[len] = '\0';
		strncpy(dest, path, len);

		if(strcmp(dest, NVMM_PATH) != 0) {
			op_to_use = _hub_fileops;
			goto opening;
		}
	}


	/*
        if (!strcmp(path, "/dev/shm/exec-ledger") || !strcmp(path, "/dev/shm/exec-hub")) {
		op_to_use = _hub_fileops;
		goto opening;
	}
	*/
		
	if(access_result)
	{		
		if(FLAGS_INCLUDE(oflag, O_CREAT)) {
			DEBUG("File does not exist and is set to be created.  Using managed fileops (%s)\n",
			      _hub_managed_fileops->name);
			op_to_use = _hub_managed_fileops;
		} else {
			DEBUG("File does not exist and is not set to be created.  Using unmanaged fileops (%s)\n",
			      _hub_fileops->name);
			op_to_use = _hub_fileops;
		}
		
	} else {// file exists
		struct stat file_st;
		
		if(stat(path, &file_st)) {
			DEBUG("_hub: failed to get device stats for \"%s\" (error: %s).  Using unmanaged fileops (%s)\n",
				path, strerror(errno), _hub_fileops->name);
			op_to_use = _hub_fileops;
		}
		else if(S_ISREG(file_st.st_mode)) {
			DEBUG("_hub: file exists and is a regular file.  Using managed fileops (%s)\n",
			      _hub_managed_fileops->name);
			op_to_use = _hub_managed_fileops;
		}
		else if (S_ISBLK(file_st.st_mode)) {
			DEBUG("_hub: file exists and is a block device.  Using managed fileops (%s)\n",
			      _hub_managed_fileops->name);
			op_to_use = _hub_managed_fileops;
		}
		else
		{
			DEBUG("_hub: file exists and is not a regular file.  Using unmanaged fileops (%s)\n",
			      _hub_fileops->name);
			op_to_use = _hub_fileops;
		}
		
	}
	
	
	// op_to_use = _hub_managed_fileops;	
	assert(op_to_use != NULL);

 opening:
	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		
		va_end(arg);
		result = op_to_use->OPEN(path, oflag, mode);
		
#if WORKLOAD_TAR | WORKLOAD_GIT | WORKLOAD_RSYNC
		if (op_to_use == _hub_fileops)
			_hub_find_fileop("posix")->FSYNC(result);
#endif // WORKLOAD_TAR
		
	} else {
		result = op_to_use->OPEN(path, oflag);
	}

	if(result >= 0)
	{
		_hub_fd_lookup[result] = op_to_use;

		DEBUG("_hub_OPEN assigning fd %i fileop %s\n", result, op_to_use->name);
	}
	else 
	{
		DEBUG("_hub_OPEN->%s_OPEN failed; not assigning fileop. Path = %s, flag = %d, Err = %s\n", op_to_use->name, path, oflag, strerror(errno));
	}
	
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
				
	return result;
}

RETT_MKNOD _hub_MKNOD(INTF_MKNOD)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, MKNOD);
        DEBUG_FILE("CALL: _hub_MKNOD for (%s)\n", path);
	struct Fileops_p* op_to_use = NULL;
	int result;
	
	op_to_use = _hub_managed_fileops;

	return op_to_use->MKNOD(CALL_MKNOD);
}

RETT_MKNODAT _hub_MKNODAT(INTF_MKNODAT)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, MKNODAT);
        DEBUG_FILE("CALL: _hub_MKNODAT for (%s)\n", path);

	struct Fileops_p* op_to_use = NULL;
	int result;
	
	op_to_use = _hub_managed_fileops;

	return op_to_use->MKNODAT(CALL_MKNODAT);	
}

RETT_CREAT _hub_CREAT(INTF_CREAT)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, CREAT);
        DEBUG_FILE("CALL: _hub_CREAT for (%s)\n", path);

	return _hub_OPEN(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

RETT_OPENAT _hub_OPENAT(INTF_OPENAT)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, OPENAT);

	DEBUG("CALL: _hub_OPEN for %s\n", path);

	struct Fileops_p* op_to_use = NULL;

	int result;

	if (dirfd == AT_FDCWD) {
		result = _hub_OPEN(CALL_OPEN);
		if (result < 0) {
			DEBUG("%s: openat failed. Err = %s\n", __func__, strerror(errno));
		}
		return result;
	}

	char new_path[256];
	char fd_str[256];				
	int path_len = 0;
	if (path[0] != '/') {
		sprintf(fd_str, "/proc/self/fd/%d", dirfd);
		if (readlink(fd_str, new_path, sizeof(new_path)) == -1)
			assert(0);
		path_len = strlen(new_path);
		new_path[path_len] = '/';
		new_path[path_len+1] = '\0';
		if (strcat(new_path, path) != new_path)
			assert(0);
	} else {
		if (strcpy(new_path, path) == NULL)
			assert(0);
	}

	if (FLAGS_INCLUDE(oflag, O_CREAT)) {
			va_list arg;
			va_start(arg, oflag);
			int mode = va_arg(arg, int);
			va_end(arg);
			return _hub_OPEN(new_path, oflag, mode);
	}

	return _hub_OPEN(new_path, oflag);
}

RETT_EXECVE _hub_EXECVE(INTF_EXECVE) {
	int pid = getpid();
	char exec_hub_filename[BUF_SIZE];

        HUB_CHECK_RESOLVE_FILEOPS(_hub_, EXECVE);

        struct Fileops_p* op_to_use = NULL;
        int result = -1, exec_hub_fd = -1, i = 0;
        int hub_ops[1024];
        unsigned long offset_in_map = 0;

        for (i = 0; i < 1024; i++) {
		if (_hub_fd_lookup[i] == NULL)
			hub_ops[i] = 0;
		if (_hub_fd_lookup[i] == _hub_fileops)
			hub_ops[i] = 1;
		if (_hub_fd_lookup[i] == _hub_managed_fileops)
			hub_ops[i] = 2;
	}

        sprintf(exec_hub_filename, "exec-hub-%d", pid);
        exec_hub_fd = shm_open(exec_hub_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (exec_hub_fd == -1) {
		printf("%s: %s\n", __func__, strerror(errno));
		assert(0);
	}

        int res = _hub_fileops->FTRUNC64(exec_hub_fd, (1024*1024));
        if (res == -1) {
		printf("%s: ftruncate failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

        char *shm_area = mmap(NULL, 1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, exec_hub_fd, 0);
        if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

        if (memcpy(shm_area + offset_in_map, hub_ops, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of hub ops failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

        offset_in_map += (1024 * sizeof(int));

        op_to_use = _hub_managed_fileops;
        result = op_to_use->EXECVE(CALL_EXECVE);

        return result;
}

RETT_EXECVP _hub_EXECVP(INTF_EXECVP) {
	int pid = getpid();
	char exec_hub_filename[BUF_SIZE];

	HUB_CHECK_RESOLVE_FILEOPS(_hub_, EXECVP);

	struct Fileops_p* op_to_use = NULL;
	int result = -1, exec_hub_fd = -1, i = 0;
	int hub_ops[1024];
	unsigned long offset_in_map = 0;
	
	for (i = 0; i < 1024; i++) {
		if (_hub_fd_lookup[i] == NULL)
			hub_ops[i] = 0;
		if (_hub_fd_lookup[i] == _hub_fileops)
			hub_ops[i] = 1;
		if (_hub_fd_lookup[i] == _hub_managed_fileops)
			hub_ops[i] = 2;
	}
	
	sprintf(exec_hub_filename, "exec-hub-%d", pid);
	exec_hub_fd = shm_open(exec_hub_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (exec_hub_fd == -1) {
		printf("%s: %s\n", __func__, strerror(errno));
		assert(0);
	}

	int res = _hub_fileops->FTRUNC64(exec_hub_fd, (1024*1024));
	if (res == -1) {
		printf("%s: ftruncate failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	char *shm_area = mmap(NULL, 1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, exec_hub_fd, 0);
	if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(shm_area + offset_in_map, hub_ops, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of hub ops failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));
	
	op_to_use = _hub_managed_fileops;
	result = op_to_use->EXECVP(CALL_EXECVP);

	return result;
}

RETT_EXECV _hub_EXECV(INTF_EXECV) {
	int pid = getpid();
	char exec_hub_filename[BUF_SIZE];

	HUB_CHECK_RESOLVE_FILEOPS(_hub_, EXECV);

	struct Fileops_p* op_to_use = NULL;
	int result = -1, exec_hub_fd = -1, i = 0;
	int hub_ops[1024];
	unsigned long offset_in_map = 0;
	
	for (i = 0; i < 1024; i++) {
		if (_hub_fd_lookup[i] == NULL)
			hub_ops[i] = 0;
		if (_hub_fd_lookup[i] == _hub_fileops)
			hub_ops[i] = 1;
		if (_hub_fd_lookup[i] == _hub_managed_fileops)
			hub_ops[i] = 2;
	}
	
	sprintf(exec_hub_filename, "exec-hub-%d", pid);
	exec_hub_fd = shm_open(exec_hub_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (exec_hub_fd == -1) {
		printf("%s: %s\n", __func__, strerror(errno));
		assert(0);
	}

	int res = _hub_fileops->FTRUNC64(exec_hub_fd, (1024*1024));
	if (res == -1) {
		printf("%s: ftruncate failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	char *shm_area = mmap(NULL, 1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, exec_hub_fd, 0);
	if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(shm_area + offset_in_map, hub_ops, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of hub ops failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));
	
	op_to_use = _hub_managed_fileops;
	result = op_to_use->EXECV(CALL_EXECV);

	return result;
}

RETT_SHM_COPY _hub_SHM_COPY() {

	HUB_CHECK_RESOLVE_FILEOPS(_hub_, SHM_COPY);
	int exec_hub_fd = -1, i = 0;
	int hub_ops[1024];
	unsigned long offset_in_map = 0;
	int pid = getpid();
	char exec_hub_filename[BUF_SIZE];
	
	sprintf(exec_hub_filename, "exec-hub-%d", pid);
	exec_hub_fd = shm_open(exec_hub_filename, O_RDONLY, 0666);
	if (exec_hub_fd == -1) {
		MSG("%s: %s\n", __func__, strerror(errno));
		assert(0);
	}

	char *shm_area = mmap(NULL, 1024*1024, PROT_READ, MAP_SHARED, exec_hub_fd, 0);
	if (shm_area == NULL) {
		MSG("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(hub_ops, shm_area + offset_in_map, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of hub ops failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	for (i = 0; i < 1024; i++) {
		if (hub_ops[i] == 0)
			_hub_fd_lookup[i] = NULL;
		if (hub_ops[i] == 1)
			_hub_fd_lookup[i] = _hub_fileops;
		if (hub_ops[i] == 2)
			_hub_fd_lookup[i] = _hub_managed_fileops;
	}

        munmap(shm_area, 1024*1024);
	shm_unlink(exec_hub_filename);

	return _hub_managed_fileops->SHM_COPY();
}


#ifdef TRACE_FP_CALLS
RETT_FOPEN _hub_FOPEN(INTF_FOPEN)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, FOPEN);

	DEBUG("CALL: _hub_FOPEN for %s\n", path);

	FILE *fp = NULL;
	int fd = -1, oflag = 0;
	int num_mode_chars = 0;

	if ((mode[0] == 'w' || mode[0] == 'a') && mode[1] == '+') {
		oflag |= O_RDWR;
		oflag |= O_CREAT;
		num_mode_chars += 2;
	}
	else if (mode[0] == 'r' && mode[1] == '+') {
		oflag |= O_RDWR;
		num_mode_chars += 2;
	}
	else if (mode[0] == 'w' || mode[0] == 'a') {
		oflag |= O_WRONLY;
		oflag |= O_CREAT;
		num_mode_chars += 2;
	}
	else if (mode[0] == 'r') {
		oflag |= O_RDONLY;
		num_mode_chars++;
	}
	else {
		assert(0);
	}


	if (mode[0] == 'a') {
		oflag |= O_APPEND;
		num_mode_chars++;
	}

	if (mode[num_mode_chars] == 'e') {
		oflag |= O_CLOEXEC;
		num_mode_chars++;
	}

	if (FLAGS_INCLUDE(oflag,O_CREAT)) 
		fd = _hub_OPEN(path, oflag, 0666);
	else
		fd = _hub_OPEN(path, oflag);

	if (fd >= 0) {
		fp = fdopen(fd, mode);
		if (!fp) {
			printf("%s: fdopen failed! error = %s, fd = %d, mode = %s\n", __func__, strerror(errno), fd, mode);
			fflush(NULL);
			assert(0);
		}
	} else {
		DEBUG("%s: fdopen failed! error = %s, fd = %d, mode = %s\n", __func__, strerror(errno), fd, mode);
		//assert(0);
	}
	
	return fp;
}

RETT_FOPEN64 _hub_FOPEN64(INTF_FOPEN64) {

	return _hub_FOPEN(CALL_FOPEN64);
}

#endif // TRACE_FP_CALLS

RETT_MKSTEMP _hub_MKSTEMP(INTF_MKSTEMP)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, MKSTEMP);
	
	DEBUG_FILE("Called _hub_mkstemp with template %s; making a new filename...\n", file);

	char *suffix = file + strlen(file) - 6;
	
	if(suffix == NULL) {
		DEBUG("Invalid template string (%s) passed to mkstemp\n", file);
		errno = EINVAL;

		return -1;
	}
	
	RETT_OPEN result = -1;

	int i;
	int success = 0;
	for(i=0; i<1000000; i++)
		{
			sprintf(suffix, "%.6i", i);

			int fs = access(file, F_OK);

			if(fs == -1) { // file doesn't exist; we're good
				success = 1;
				break;
			}

		}

	if(!success) {
		DEBUG("No available file names!\n");
                //pthread_spin_unlock(&global_lock);			
		return -1;
	}

	DEBUG("Generated filename %s.  Calling (regular) open...\n", file);

	result = _hub_OPEN(file, O_CREAT | O_EXCL | O_RDWR, 0600);

	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_MKSTEMP64 _hub_MKSTEMP64(INTF_MKSTEMP64)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, MKSTEMP64);
	return _hub_MKSTEMP(CALL_MKSTEMP);
}

RETT_CLOSE _hub_CLOSE(INTF_CLOSE)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, CLOSE);

	DEBUG_FILE("%s: fd = %d\n", __func__, file);

	if( (file<0) || (file >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		errno = EBADF;
		return -1;
	}

	if(_hub_fd_lookup[file]==NULL) {
		DEBUG("That file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file);
		errno = EBADF;
		return -1;
	}
	
	DEBUG("CALL: _hub_CLOSE\n");

	assert(_hub_fd_lookup[file]!=NULL);
	assert(_hub_fd_lookup[file]->name!=NULL);

	DEBUG("_hub_CLOSE is calling %s->CLOSE\n", _hub_fd_lookup[file]->name);
	
	struct Fileops_p* temp = _hub_fd_lookup[file];

	// Restore it to the state similar to the initialised state.
	_hub_fd_lookup[file] = _hub_fileops;
	int result = temp->CLOSE(CALL_CLOSE);

	if(result) {
		DEBUG("call to %s->CLOSE failed: %s\n", _hub_fileops->name, strerror(errno));
		return result;
	}
	
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

#ifdef TRACE_FP_CALLS				
RETT_FCLOSE _hub_FCLOSE(INTF_FCLOSE)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, FCLOSE);

	if(_hub_fd_lookup[fileno(fp)]==NULL) {
		DEBUG("That file descriptor (%i) is invalid: perhaps you didn't call open first?\n", fileno(fp));
		errno = EBADF;
		return -1;
	}
	
	DEBUG_FILE("CALL: _hub_FCLOSE\n");

	return _hub_CLOSE(fileno(fp));
}
#endif

RETT_DUP _hub_DUP(INTF_DUP)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, DUP);

	DEBUG_FILE("CALL: _hub_DUP(%i)\n", file);
	
	if( (file<0) || (file >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		errno = EBADF;
		return -1;
	}
	
	if(_hub_fd_lookup[file]==NULL) {
		DEBUG("That file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file);
		errno = EBADF;
		return -1;
	}
	
	assert(_hub_fd_lookup[file]!=NULL);
	assert(_hub_fd_lookup[file]->name!=NULL);
	
	DEBUG("_hub_DUP  is calling %s->DUP\n", _hub_fd_lookup[file]->name);
	
	int result = _hub_fd_lookup[file]->DUP(CALL_DUP);

	if(result >= 0) {
		DEBUG("Hub(dup) managing new FD %i with same ops (\"%s\") as initial FD (%i)\n",
			result, _hub_fd_lookup[file]->name, file);
		_hub_fd_lookup[result] = _hub_fd_lookup[file];
	}

	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}


RETT_DUP2 _hub_DUP2(INTF_DUP2)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, DUP2);

	DEBUG_FILE("CALL: _hub_DUP2(%i, %i)\n", file, fd2);

	if( (file<0) || (file >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		errno = EBADF;
		return -1;
	}
	
	if( (fd2<0) || (fd2 >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", fd2);
		errno = EBADF;
		return -1;
	}
	
	if(_hub_fd_lookup[file]==NULL) {
		DEBUG("The first file descriptor (%i) is invalid: perhaps you didn't call open first?\n", file);
		errno = EBADF;
		return -1;
	}

	if(_hub_fd_lookup[fd2]==NULL) {
		DEBUG("The second file descriptor (%i) is invalid: perhaps you didn't call open first?\n", fd2);
		errno = EBADF;
		return -1;
	}

	if( _hub_fd_lookup[file] != _hub_fd_lookup[fd2] ) {
		WARNING("fd1 (%i) and fd2 (%i) do not have the same handlers! (%s and %s)\n", file, fd2,
			_hub_fd_lookup[file]->name, _hub_fd_lookup[fd2]->name );
		if(_hub_fd_lookup[file] == _hub_managed_fileops) {
			DEBUG("I'm going to allow this because it's closing the unmanaged file\n");
		}
		else
		{
			DEBUG("This shall be allowed because we want to handle normal files with Posix operations.\n");
		}
	} else {
		DEBUG("_hub_DUP2: fd1 (%i) and fd2 (%i) have the same handler (%s)\n", file, fd2,
			_hub_fd_lookup[file]->name);
	}
	
	DEBUG("_hub_DUP2 is calling %s->DUP2(%i, %i) (p=%p)\n", _hub_fd_lookup[file]->name, file, fd2,
		_hub_fd_lookup[file]->DUP2);

	int result = _hub_fd_lookup[file]->DUP2(CALL_DUP2);


	if(result < 0)
	{
		DEBUG("DUP2 call had an error.\n");
		WARNING("fd2 (%i) may not be correctly marked as valid/invalid in submodules\n", fd2);
	}
	else
	{ 
		DEBUG("DUP2 call completed successfully.\n");

		DEBUG("Hub(dup2) managing new FD %i with same ops (\"%s\") as initial FD (%i)\n",
			result, _hub_fd_lookup[file]->name, file);

		_hub_fd_lookup[result] = _hub_fd_lookup[file];
	}

	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_IOCTL _hub_IOCTL(INTF_IOCTL)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, IOCTL);

	DEBUG_FILE("CALL: _hub_IOCTL\n");
	
	va_list arg;
	va_start(arg, request);
	int* third = va_arg(arg, int*);

	RETT_IOCTL result = _hub_fileops->IOCTL(file, request, third);
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_OPEN64 _hub_OPEN64(INTF_OPEN64)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, OPEN64);

	DEBUG("CALL: _hub_OPEN64\n");
	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		va_list arg;
		va_start(arg, oflag);
		int mode = va_arg(arg, int);
		va_end(arg);
		return _hub_OPEN(path, oflag, mode);
	} else {
		return _hub_OPEN(path, oflag);
	}
}

RETT_SOCKET _hub_SOCKET(INTF_SOCKET)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, SOCKET);

	DEBUG("CALL: _hub_SOCKET\n");

	RETT_SOCKET result = _hub_fileops->SOCKET(CALL_SOCKET);

	if (result > 0) {
		//sockets always use default fileops
		_hub_fd_lookup[result] = _hub_fileops;
	}

	return result;
}

RETT_ACCEPT _hub_ACCEPT(INTF_ACCEPT)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, ACCEPT);

        //pthread_spin_lock(&global_lock);			
	DEBUG("CALL: _hub_ACCEPT\n");

	RETT_ACCEPT result = _hub_fileops->ACCEPT(CALL_ACCEPT);

	if (result > 0) {
		//sockets always use default fileops
		_hub_fd_lookup[result] = _hub_fileops;
	}

        //pthread_spin_unlock(&global_lock);			
	return result;
}

RETT_UNLINK _hub_UNLINK(INTF_UNLINK)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, UNLINK);

	DEBUG_FILE("CALL: _hub_UNLINK\n");

	RETT_UNLINK result = _hub_managed_fileops->UNLINK(CALL_UNLINK);

	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_UNLINKAT _hub_UNLINKAT(INTF_UNLINKAT)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, UNLINKAT);

	DEBUG_FILE("CALL: _hub_UNLINKAT\n");

	RETT_UNLINKAT result = _hub_managed_fileops->UNLINKAT(CALL_UNLINKAT);

	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_MKDIR _hub_MKDIR(INTF_MKDIR)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, MKDIR);
	DEBUG_FILE("CALL: _hub_MKDIR\n");
	RETT_MKDIR result = _hub_managed_fileops->MKDIR(CALL_MKDIR);
	// Write to op log
	DEBUG_FILE("%s: System call returned %d. Logging\n", __func__, result);	
	return result;
}

RETT_RENAME _hub_RENAME(INTF_RENAME)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, RENAME);
	DEBUG_FILE("CALL: _hub_RENAME\n");
	RETT_RENAME result = _hub_managed_fileops->RENAME(CALL_RENAME);
	// Write to op log
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_LINK _hub_LINK(INTF_LINK)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, LINK);
	DEBUG_FILE("CALL: _hub_LINK\n");
	RETT_LINK result = _hub_managed_fileops->LINK(CALL_LINK);
	// Write to op log
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_SYMLINK _hub_SYMLINK(INTF_SYMLINK)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, SYMLINK);
	DEBUG_FILE("CALL: _hub_SYMLINK\n");
	RETT_SYMLINK result = _hub_managed_fileops->SYMLINK(CALL_SYMLINK);
	// Write to op log
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_RMDIR _hub_RMDIR(INTF_RMDIR)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, RMDIR);
	DEBUG_FILE("CALL: _hub_RMDIR\n");
	RETT_RMDIR result = _hub_managed_fileops->RMDIR(CALL_RMDIR);
	// Write to op log
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_SYMLINKAT _hub_SYMLINKAT(INTF_SYMLINKAT)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, SYMLINKAT);
	DEBUG_FILE("CALL: _hub_SYMLINKAT\n");
	RETT_SYMLINKAT result = _hub_managed_fileops->SYMLINKAT(CALL_SYMLINKAT);
	// Write to op log
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_MKDIRAT _hub_MKDIRAT(INTF_MKDIRAT)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, MKDIRAT);
	DEBUG_FILE("CALL: _hub_MKDIRAT\n");
	RETT_MKDIRAT result = _hub_managed_fileops->MKDIRAT(CALL_MKDIRAT);
	// Write to op log
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;
}

RETT_TRUNC _hub_TRUNC(INTF_TRUNC)
{
	HUB_CHECK_RESOLVE_FILEOPS(_hub_, TRUNC);
	DEBUG_FILE("CALL: _hub_TRUNC\n");
	RETT_TRUNC result = _hub_managed_fileops->TRUNC(CALL_TRUNC);
	// Write to op log
	DEBUG_FILE("%s: Return = %d\n", __func__, result);
	return result;	
}

/*
RETT_STAT _hub_STAT(INTF_STAT)
{
	CHECK_RESOLVE_FILEOPS(_hub_);
	RETT_STAT result;
	result = _hub_managed_fileops->STAT(CALL_STAT);
	return result;
}

RETT_STAT64 _hub_STAT64(INTF_STAT64)
{
	CHECK_RESOLVE_FILEOPS(_hub_);
	RETT_STAT64 result;
	result = _hub_managed_fileops->STAT64(CALL_STAT64);	
	return result;
}

RETT_LSTAT _hub_LSTAT(INTF_LSTAT)
{
	CHECK_RESOLVE_FILEOPS(_hub_);
	RETT_LSTAT result;
	result = _hub_managed_fileops->LSTAT(CALL_LSTAT);	
	return result;
}

RETT_LSTAT64 _hub_LSTAT64(INTF_LSTAT64)
{
	CHECK_RESOLVE_FILEOPS(_hub_);
	RETT_LSTAT64 result;
	result = _hub_managed_fileops->LSTAT64(CALL_LSTAT64);	
	return result;
}
*/

/*
RETT_CLONE _hub_CLONE(INTF_CLONE)
{
	CHECK_RESOLVE_FILEOPS(_hub_);

	DEBUG("CALL: _hub_CLONE\n");

	DEBUG("\n"
	"        CCCCCCCCCCCCCLLLLLLLLLLL                  OOOOOOOOO     NNNNNNNN        NNNNNNNNEEEEEEEEEEEEEEEEEEEEEE\n"
	"     CCC::::::::::::CL:::::::::L                OO:::::::::OO   N:::::::N       N::::::NE::::::::::::::::::::E\n"
	"   CC:::::::::::::::CL:::::::::L              OO:::::::::::::OO N::::::::N      N::::::NE::::::::::::::::::::E\n"
	"  C:::::CCCCCCCC::::CLL:::::::LL             O:::::::OOO:::::::ON:::::::::N     N::::::NEE::::::EEEEEEEEE::::E\n"
	" C:::::C       CCCCCC  L:::::L               O::::::O   O::::::ON::::::::::N    N::::::N  E:::::E       EEEEEE\n"
	"C:::::C                L:::::L               O:::::O     O:::::ON:::::::::::N   N::::::N  E:::::E             \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON:::::::N::::N  N::::::N  E::::::EEEEEEEEEE   \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON::::::N N::::N N::::::N  E:::::::::::::::E   \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON::::::N  N::::N:::::::N  E:::::::::::::::E   \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON::::::N   N:::::::::::N  E::::::EEEEEEEEEE   \n"
	"C:::::C                L:::::L               O:::::O     O:::::ON::::::N    N::::::::::N  E:::::E             \n"
	" C:::::C       CCCCCC  L:::::L         LLLLLLO::::::O   O::::::ON::::::N     N:::::::::N  E:::::E       EEEEEE\n"
	"  C:::::CCCCCCCC::::CLL:::::::LLLLLLLLL:::::LO:::::::OOO:::::::ON::::::N      N::::::::NEE::::::EEEEEEEE:::::E\n"
	"   CC:::::::::::::::CL::::::::::::::::::::::L OO:::::::::::::OO N::::::N       N:::::::NE::::::::::::::::::::E\n"
	"     CCC::::::::::::CL::::::::::::::::::::::L   OO:::::::::OO   N::::::N        N::::::NE::::::::::::::::::::E\n"
	"        CCCCCCCCCCCCCLLLLLLLLLLLLLLLLLLLLLLLL     OOOOOOOOO     NNNNNNNN         NNNNNNNEEEEEEEEEEEEEEEEEEEEEE\n"
	);

	assert(0);

	return (RETT_CLONE) -1;
}
*/

// breaking the build

