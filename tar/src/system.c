/* System-dependent calls for tar.

   Copyright 2003-2008, 2010, 2013-2014, 2016-2017 Free Software
   Foundation, Inc.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any later
   version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
   Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <system.h>

#include "common.h"
#include <priv-set.h>
#include <rmt.h>
#include <signal.h>
#include <wordsplit.h>

static _Noreturn void
xexec (const char *cmd)
{
  char *argv[4];

  argv[0] = (char *) "/bin/sh";
  argv[1] = (char *) "-c";
  argv[2] = (char *) cmd;
  argv[3] = NULL;

  execv ("/bin/sh", argv);
  exec_fatal (cmd);
}

#if MSDOS

bool
sys_get_archive_stat (void)
{
  return 0;
}

bool
sys_file_is_archive (struct tar_stat_info *p)
{
  return false;
}

void
sys_save_archive_dev_ino (void)
{
}

void
sys_detect_dev_null_output (void)
{
  static char const dev_null[] = "nul";

  dev_null_output = (strcmp (archive_name_array[0], dev_null) == 0
		     || (! _isrmt (archive)));
}

void
sys_wait_for_child (pid_t child_pid, bool eof)
{
}

void
sys_spawn_shell (void)
{
  spawnl (P_WAIT, getenv ("COMSPEC"), "-", 0);
}

/* stat() in djgpp's C library gives a constant number of 42 as the
   uid and gid of a file.  So, comparing an FTP'ed archive just after
   unpack would fail on MSDOS.  */

bool
sys_compare_uid (struct stat *a, struct stat *b)
{
  return true;
}

bool
sys_compare_gid (struct stat *a, struct stat *b)
{
  return true;
}

void
sys_compare_links (struct stat *link_data, struct stat *stat_data)
{
  return true;
}

int
sys_truncate (int fd)
{
  return write (fd, "", 0);
}

size_t
sys_write_archive_buffer (void)
{
  return full_write (archive, record_start->buffer, record_size);
}

/* Set ARCHIVE for writing, then compressing an archive.  */
void
sys_child_open_for_compress (void)
{
  FATAL_ERROR ((0, 0, _("Cannot use compressed or remote archives")));
}

/* Set ARCHIVE for uncompressing, then reading an archive.  */
void
sys_child_open_for_uncompress (void)
{
  FATAL_ERROR ((0, 0, _("Cannot use compressed or remote archives")));
}

#else

extern union block *record_start; /* FIXME */

static struct stat archive_stat; /* stat block for archive file */

bool
sys_get_archive_stat (void)
{
  return fstat (archive, &archive_stat) == 0;
}

bool
sys_file_is_archive (struct tar_stat_info *p)
{
  return (ar_dev && p->stat.st_dev == ar_dev && p->stat.st_ino == ar_ino);
}

/* Save archive file inode and device numbers */
void
sys_save_archive_dev_ino (void)
{
  if (!_isrmt (archive) && S_ISREG (archive_stat.st_mode))
    {
      ar_dev = archive_stat.st_dev;
      ar_ino = archive_stat.st_ino;
    }
  else
    ar_dev = 0;
}

/* Detect if outputting to "/dev/null".  */
void
sys_detect_dev_null_output (void)
{
  static char const dev_null[] = "/dev/null";
  struct stat dev_null_stat;

  dev_null_output = (strcmp (archive_name_array[0], dev_null) == 0
		     || (! _isrmt (archive)
			 && S_ISCHR (archive_stat.st_mode)
			 && stat (dev_null, &dev_null_stat) == 0
			 && archive_stat.st_dev == dev_null_stat.st_dev
			 && archive_stat.st_ino == dev_null_stat.st_ino));
}

void
sys_wait_for_child (pid_t child_pid, bool eof)
{
  if (child_pid)
    {
      int wait_status;

      while (waitpid (child_pid, &wait_status, 0) == -1)
	if (errno != EINTR)
	  {
	    waitpid_error (use_compress_program_option);
	    break;
	  }

      if (WIFSIGNALED (wait_status))
	{
	  int sig = WTERMSIG (wait_status);
	  if (!(!eof && sig == SIGPIPE))
	    FATAL_ERROR ((0, 0, _("Child died with signal %d"), sig));
	}
      else if (WEXITSTATUS (wait_status) != 0)
	FATAL_ERROR ((0, 0, _("Child returned status %d"),
		      WEXITSTATUS (wait_status)));
    }
}

void
sys_spawn_shell (void)
{
  pid_t child;
  const char *shell = getenv ("SHELL");
  if (! shell)
    shell = "/bin/sh";
  child = xfork ();
  if (child == 0)
    {
      priv_set_restore_linkdir ();
      execlp (shell, "-sh", "-i", NULL);
      exec_fatal (shell);
    }
  else
    {
      int wait_status;
      while (waitpid (child, &wait_status, 0) == -1)
	if (errno != EINTR)
	  {
	    waitpid_error (shell);
	    break;
	  }
    }
}

bool
sys_compare_uid (struct stat *a, struct stat *b)
{
  return a->st_uid == b->st_uid;
}

bool
sys_compare_gid (struct stat *a, struct stat *b)
{
  return a->st_gid == b->st_gid;
}

bool
sys_compare_links (struct stat *link_data, struct stat *stat_data)
{
  return stat_data->st_dev == link_data->st_dev
         && stat_data->st_ino == link_data->st_ino;
}

int
sys_truncate (int fd)
{
  off_t pos = lseek (fd, (off_t) 0, SEEK_CUR);
  return pos < 0 ? -1 : ftruncate (fd, pos);
}

/* Return nonzero if NAME is the name of a regular file, or if the file
   does not exist (so it would be created as a regular file).  */
static int
is_regular_file (const char *name)
{
  struct stat stbuf;

  if (stat (name, &stbuf) == 0)
    return S_ISREG (stbuf.st_mode);
  else
    return errno == ENOENT;
}

size_t
sys_write_archive_buffer (void)
{
  return rmtwrite (archive, record_start->buffer, record_size);
}

#define	PREAD 0			/* read file descriptor from pipe() */
#define	PWRITE 1		/* write file descriptor from pipe() */

/* Duplicate file descriptor FROM into becoming INTO.
   INTO is closed first and has to be the next available slot.  */
static void
xdup2 (int from, int into)
{
  if (from != into)
    {
      int status = close (into);

      if (status != 0 && errno != EBADF)
	{
	  int e = errno;
	  FATAL_ERROR ((0, e, _("Cannot close")));
	}
      status = dup (from);
      if (status != into)
	{
	  if (status < 0)
	    {
	      int e = errno;
	      FATAL_ERROR ((0, e, _("Cannot dup")));
	    }
	  abort ();
	}
      xclose (from);
    }
}

static void wait_for_grandchild (pid_t pid) __attribute__ ((__noreturn__));

/* Propagate any failure of the grandchild back to the parent.  */
static void
wait_for_grandchild (pid_t pid)
{
  int wait_status;
  int exit_code = 0;

  while (waitpid (pid, &wait_status, 0) == -1)
    if (errno != EINTR)
      {
	waitpid_error (use_compress_program_option);
	break;
      }

  if (WIFSIGNALED (wait_status))
    raise (WTERMSIG (wait_status));
  else if (WEXITSTATUS (wait_status) != 0)
    exit_code = WEXITSTATUS (wait_status);

  exit (exit_code);
}

/* Set ARCHIVE for writing, then compressing an archive.  */
pid_t
sys_child_open_for_compress (void)
{
  int parent_pipe[2];
  int child_pipe[2];
  pid_t grandchild_pid;
  pid_t child_pid;

  signal (SIGPIPE, SIG_IGN);
  xpipe (parent_pipe);
  child_pid = xfork ();

  if (child_pid > 0)
    {
      /* The parent tar is still here!  Just clean up.  */

      archive = parent_pipe[PWRITE];
      xclose (parent_pipe[PREAD]);
      return child_pid;
    }

  /* The new born child tar is here!  */

  set_program_name (_("tar (child)"));
  signal (SIGPIPE, SIG_DFL);

  xdup2 (parent_pipe[PREAD], STDIN_FILENO);
  xclose (parent_pipe[PWRITE]);

  /* Check if we need a grandchild tar.  This happens only if either:
     a) the file is to be accessed by rmt: compressor doesn't know how;
     b) the file is not a plain file.  */

  if (!_remdev (archive_name_array[0])
      && is_regular_file (archive_name_array[0]))
    {
      if (backup_option)
	maybe_backup_file (archive_name_array[0], 1);

      /* We don't need a grandchild tar.  Open the archive and launch the
	 compressor.  */
      if (strcmp (archive_name_array[0], "-"))
	{
	  archive = creat (archive_name_array[0], MODE_RW);
	  if (archive < 0)
	    {
	      int saved_errno = errno;

	      if (backup_option)
		undo_last_backup ();
	      errno = saved_errno;
	      open_fatal (archive_name_array[0]);
	    }
	  xdup2 (archive, STDOUT_FILENO);
	}
      priv_set_restore_linkdir ();
      xexec (use_compress_program_option);
    }

  /* We do need a grandchild tar.  */

  xpipe (child_pipe);
  grandchild_pid = xfork ();

  if (grandchild_pid == 0)
    {
      /* The newborn grandchild tar is here!  Launch the compressor.  */

      set_program_name (_("tar (grandchild)"));

      xdup2 (child_pipe[PWRITE], STDOUT_FILENO);
      xclose (child_pipe[PREAD]);
      priv_set_restore_linkdir ();
      xexec (use_compress_program_option);
    }

  /* The child tar is still here!  */

  /* Prepare for reblocking the data from the compressor into the archive.  */

  xdup2 (child_pipe[PREAD], STDIN_FILENO);
  xclose (child_pipe[PWRITE]);

  if (strcmp (archive_name_array[0], "-") == 0)
    archive = STDOUT_FILENO;
  else
    {
      archive = rmtcreat (archive_name_array[0], MODE_RW, rsh_command_option);
      if (archive < 0)
	open_fatal (archive_name_array[0]);
    }

  /* Let's read out of the stdin pipe and write an archive.  */

  while (1)
    {
      size_t status = 0;
      char *cursor;
      size_t length;

      /* Assemble a record.  */

      for (length = 0, cursor = record_start->buffer;
	   length < record_size;
	   length += status, cursor += status)
	{
	  size_t size = record_size - length;

	  status = safe_read (STDIN_FILENO, cursor, size);
	  if (status == SAFE_READ_ERROR)
	    read_fatal (use_compress_program_option);
	  if (status == 0)
	    break;
	}

      /* Copy the record.  */

      if (status == 0)
	{
	  /* We hit the end of the file.  Write last record at
	     full length, as the only role of the grandchild is
	     doing proper reblocking.  */

	  if (length > 0)
	    {
	      memset (record_start->buffer + length, 0, record_size - length);
	      status = sys_write_archive_buffer ();
	      if (status != record_size)
		archive_write_error (status);
	    }

	  /* There is nothing else to read, break out.  */
	  break;
	}

      status = sys_write_archive_buffer ();
      if (status != record_size)
	archive_write_error (status);
    }

  wait_for_grandchild (grandchild_pid);
}

static void
run_decompress_program (void)
{
  int i;
  const char *p, *prog = NULL;
  struct wordsplit ws;
  int wsflags = (WRDSF_DEFFLAGS | WRDSF_ENV | WRDSF_DOOFFS) & ~WRDSF_NOVAR;

  ws.ws_env = (const char **) environ;
  ws.ws_offs = 1;

  for (p = first_decompress_program (&i); p; p = next_decompress_program (&i))
    {
      if (prog)
	{
	  WARNOPT (WARN_DECOMPRESS_PROGRAM,
		   (0, errno, _("cannot run %s"), prog));
	  WARNOPT (WARN_DECOMPRESS_PROGRAM,
		   (0, 0, _("trying %s"), p));
	}
      if (wordsplit (p, &ws, wsflags))
	FATAL_ERROR ((0, 0, _("cannot split string '%s': %s"),
		      p, wordsplit_strerror (&ws)));
      wsflags |= WRDSF_REUSE;
      memmove(ws.ws_wordv, ws.ws_wordv + ws.ws_offs,
	      sizeof(ws.ws_wordv[0])*ws.ws_wordc);
      ws.ws_wordv[ws.ws_wordc] = (char *) "-d";
      prog = p;
      execvp (ws.ws_wordv[0], ws.ws_wordv);
      ws.ws_wordv[ws.ws_wordc] = NULL;
    }
  if (!prog)
    FATAL_ERROR ((0, 0, _("unable to run decompression program")));
  exec_fatal (prog);
}

/* Set ARCHIVE for uncompressing, then reading an archive.  */
pid_t
sys_child_open_for_uncompress (void)
{
  int parent_pipe[2];
  int child_pipe[2];
  pid_t grandchild_pid;
  pid_t child_pid;

  xpipe (parent_pipe);
  child_pid = xfork ();

  if (child_pid > 0)
    {
      /* The parent tar is still here!  Just clean up.  */

      archive = parent_pipe[PREAD];
      xclose (parent_pipe[PWRITE]);
      return child_pid;
    }

  /* The newborn child tar is here!  */

  set_program_name (_("tar (child)"));
  signal (SIGPIPE, SIG_DFL);

  xdup2 (parent_pipe[PWRITE], STDOUT_FILENO);
  xclose (parent_pipe[PREAD]);

  /* Check if we need a grandchild tar.  This happens only if either:
     a) we're reading stdin: to force unblocking;
     b) the file is to be accessed by rmt: compressor doesn't know how;
     c) the file is not a plain file.  */

  if (strcmp (archive_name_array[0], "-") != 0
      && !_remdev (archive_name_array[0])
      && is_regular_file (archive_name_array[0]))
    {
      /* We don't need a grandchild tar.  Open the archive and lauch the
	 uncompressor.  */

      archive = open (archive_name_array[0], O_RDONLY | O_BINARY, MODE_RW);
      if (archive < 0)
	open_fatal (archive_name_array[0]);
      xdup2 (archive, STDIN_FILENO);
      priv_set_restore_linkdir ();
      run_decompress_program ();
    }

  /* We do need a grandchild tar.  */

  xpipe (child_pipe);
  grandchild_pid = xfork ();

  if (grandchild_pid == 0)
    {
      /* The newborn grandchild tar is here!  Launch the uncompressor.  */

      set_program_name (_("tar (grandchild)"));

      xdup2 (child_pipe[PREAD], STDIN_FILENO);
      xclose (child_pipe[PWRITE]);
      priv_set_restore_linkdir ();
      run_decompress_program ();
    }

  /* The child tar is still here!  */

  /* Prepare for unblocking the data from the archive into the
     uncompressor.  */

  xdup2 (child_pipe[PWRITE], STDOUT_FILENO);
  xclose (child_pipe[PREAD]);

  if (strcmp (archive_name_array[0], "-") == 0)
    archive = STDIN_FILENO;
  else
    archive = rmtopen (archive_name_array[0], O_RDONLY | O_BINARY,
		       MODE_RW, rsh_command_option);
  if (archive < 0)
    open_fatal (archive_name_array[0]);

  /* Let's read the archive and pipe it into stdout.  */

  while (1)
    {
      char *cursor;
      size_t maximum;
      size_t count;
      size_t status;

      clear_read_error_count ();

    error_loop:
      status = rmtread (archive, record_start->buffer, record_size);
      if (status == SAFE_READ_ERROR)
	{
	  archive_read_error ();
	  goto error_loop;
	}
      if (status == 0)
	break;
      cursor = record_start->buffer;
      maximum = status;
      while (maximum)
	{
	  count = maximum < BLOCKSIZE ? maximum : BLOCKSIZE;
	  if (full_write (STDOUT_FILENO, cursor, count) != count)
	    write_error (use_compress_program_option);
	  cursor += count;
	  maximum -= count;
	}
    }

  xclose (STDOUT_FILENO);

  wait_for_grandchild (grandchild_pid);
}



static void
dec_to_env (char const *envar, uintmax_t num)
{
  char buf[UINTMAX_STRSIZE_BOUND];
  char *numstr;

  numstr = STRINGIFY_BIGINT (num, buf);
  if (setenv (envar, numstr, 1) != 0)
    xalloc_die ();
}

static void
time_to_env (char const *envar, struct timespec t)
{
  char buf[TIMESPEC_STRSIZE_BOUND];
  if (setenv (envar, code_timespec (t, buf), 1) != 0)
    xalloc_die ();
}

static void
oct_to_env (char const *envar, unsigned long num)
{
  char buf[1+1+(sizeof(unsigned long)*CHAR_BIT+2)/3];

  snprintf (buf, sizeof buf, "0%lo", num);
  if (setenv (envar, buf, 1) != 0)
    xalloc_die ();
}

static void
str_to_env (char const *envar, char const *str)
{
  if (str)
    {
      if (setenv (envar, str, 1) != 0)
	xalloc_die ();
    }
  else
    unsetenv (envar);
}

static void
chr_to_env (char const *envar, char c)
{
  char buf[2];
  buf[0] = c;
  buf[1] = 0;
  if (setenv (envar, buf, 1) != 0)
    xalloc_die ();
}

static void
stat_to_env (char *name, char type, struct tar_stat_info *st)
{
  str_to_env ("TAR_VERSION", PACKAGE_VERSION);
  str_to_env ("TAR_ARCHIVE", *archive_name_cursor);
  dec_to_env ("TAR_VOLUME", archive_name_cursor - archive_name_array + 1);
  dec_to_env ("TAR_BLOCKING_FACTOR", blocking_factor);
  str_to_env ("TAR_FORMAT",
	      archive_format_string (current_format == DEFAULT_FORMAT ?
				     archive_format : current_format));
  chr_to_env ("TAR_FILETYPE", type);
  oct_to_env ("TAR_MODE", st->stat.st_mode);
  str_to_env ("TAR_FILENAME", name);
  str_to_env ("TAR_REALNAME", st->file_name);
  str_to_env ("TAR_UNAME", st->uname);
  str_to_env ("TAR_GNAME", st->gname);
  time_to_env ("TAR_ATIME", st->atime);
  time_to_env ("TAR_MTIME", st->mtime);
  time_to_env ("TAR_CTIME", st->ctime);
  dec_to_env ("TAR_SIZE", st->stat.st_size);
  dec_to_env ("TAR_UID", st->stat.st_uid);
  dec_to_env ("TAR_GID", st->stat.st_gid);

  switch (type)
    {
    case 'b':
    case 'c':
      dec_to_env ("TAR_MINOR", minor (st->stat.st_rdev));
      dec_to_env ("TAR_MAJOR", major (st->stat.st_rdev));
      unsetenv ("TAR_LINKNAME");
      break;

    case 'l':
    case 'h':
      unsetenv ("TAR_MINOR");
      unsetenv ("TAR_MAJOR");
      str_to_env ("TAR_LINKNAME", st->link_name);
      break;

    default:
      unsetenv ("TAR_MINOR");
      unsetenv ("TAR_MAJOR");
      unsetenv ("TAR_LINKNAME");
      break;
    }
}

static pid_t global_pid;
static void (*pipe_handler) (int sig);

int
sys_exec_command (char *file_name, int typechar, struct tar_stat_info *st)
{
  int p[2];

  xpipe (p);
  pipe_handler = signal (SIGPIPE, SIG_IGN);
  global_pid = xfork ();

  if (global_pid != 0)
    {
      xclose (p[PREAD]);
      return p[PWRITE];
    }

  /* Child */
  xdup2 (p[PREAD], STDIN_FILENO);
  xclose (p[PWRITE]);

  stat_to_env (file_name, typechar, st);

  priv_set_restore_linkdir ();
  xexec (to_command_option);
}

void
sys_wait_command (void)
{
  int status;

  if (global_pid < 0)
    return;

  signal (SIGPIPE, pipe_handler);
  while (waitpid (global_pid, &status, 0) == -1)
    if (errno != EINTR)
      {
        global_pid = -1;
        waitpid_error (to_command_option);
        return;
      }

  if (WIFEXITED (status))
    {
      if (!ignore_command_error_option && WEXITSTATUS (status))
	ERROR ((0, 0, _("%lu: Child returned status %d"),
		(unsigned long) global_pid, WEXITSTATUS (status)));
    }
  else if (WIFSIGNALED (status))
    {
      WARN ((0, 0, _("%lu: Child terminated on signal %d"),
	     (unsigned long) global_pid, WTERMSIG (status)));
    }
  else
    ERROR ((0, 0, _("%lu: Child terminated on unknown reason"),
	    (unsigned long) global_pid));

  global_pid = -1;
}

int
sys_exec_info_script (const char **archive_name, int volume_number)
{
  pid_t pid;
  char uintbuf[UINTMAX_STRSIZE_BOUND];
  int p[2];
  static void (*saved_handler) (int sig);

  xpipe (p);
  saved_handler = signal (SIGPIPE, SIG_IGN);

  pid = xfork ();

  if (pid != 0)
    {
      /* Master */

      int rc;
      int status;
      char *buf = NULL;
      size_t size = 0;
      FILE *fp;

      xclose (p[PWRITE]);
      fp = fdopen (p[PREAD], "r");
      rc = getline (&buf, &size, fp);
      fclose (fp);

      if (rc > 0 && buf[rc-1] == '\n')
	buf[--rc] = 0;

      while (waitpid (pid, &status, 0) == -1)
	if (errno != EINTR)
	  {
	    signal (SIGPIPE, saved_handler);
	    waitpid_error (info_script_option);
	    return -1;
	  }

      signal (SIGPIPE, saved_handler);

      if (WIFEXITED (status))
	{
	  if (WEXITSTATUS (status) == 0 && rc > 0)
	    *archive_name = buf;
	  else
	    free (buf);
	  return WEXITSTATUS (status);
	}

      free (buf);
      return -1;
    }

  /* Child */
  setenv ("TAR_VERSION", PACKAGE_VERSION, 1);
  setenv ("TAR_ARCHIVE", *archive_name, 1);
  setenv ("TAR_VOLUME", STRINGIFY_BIGINT (volume_number, uintbuf), 1);
  setenv ("TAR_BLOCKING_FACTOR",
	  STRINGIFY_BIGINT (blocking_factor, uintbuf), 1);
  setenv ("TAR_SUBCOMMAND", subcommand_string (subcommand_option), 1);
  setenv ("TAR_FORMAT",
	  archive_format_string (current_format == DEFAULT_FORMAT ?
				 archive_format : current_format), 1);
  setenv ("TAR_FD", STRINGIFY_BIGINT (p[PWRITE], uintbuf), 1);

  xclose (p[PREAD]);

  priv_set_restore_linkdir ();
  xexec (info_script_option);
}

void
sys_exec_checkpoint_script (const char *script_name,
			    const char *archive_name,
			    int checkpoint_number)
{
  pid_t pid;
  char uintbuf[UINTMAX_STRSIZE_BOUND];

  pid = xfork ();

  if (pid != 0)
    {
      /* Master */

      int status;

      while (waitpid (pid, &status, 0) == -1)
	if (errno != EINTR)
	  {
	    waitpid_error (script_name);
	    break;
	  }

      return;
    }

  /* Child */
  setenv ("TAR_VERSION", PACKAGE_VERSION, 1);
  setenv ("TAR_ARCHIVE", archive_name, 1);
  setenv ("TAR_CHECKPOINT", STRINGIFY_BIGINT (checkpoint_number, uintbuf), 1);
  setenv ("TAR_BLOCKING_FACTOR",
	  STRINGIFY_BIGINT (blocking_factor, uintbuf), 1);
  setenv ("TAR_SUBCOMMAND", subcommand_string (subcommand_option), 1);
  setenv ("TAR_FORMAT",
	  archive_format_string (current_format == DEFAULT_FORMAT ?
				 archive_format : current_format), 1);
  priv_set_restore_linkdir ();
  xexec (script_name);
}

#endif /* not MSDOS */
