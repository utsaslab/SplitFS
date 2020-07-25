/* Warnings for GNU tar.

   Copyright 2009, 2012-2014, 2016-2017 Free Software Foundation, Inc.

   This file is part of GNU tar.

   GNU tar is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   GNU tar is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <system.h>
#include <argmatch.h>

#include "common.h"

static char const *const warning_args[] = {
  "all",
  "alone-zero-block",
  "bad-dumpdir",
  "cachedir",
  "contiguous-cast",
  "file-changed",
  "file-ignored",
  "file-removed",
  "file-shrank",
  "file-unchanged",
  "filename-with-nuls",
  "ignore-archive",
  "ignore-newer",
  "new-directory",
  "rename-directory",
  "symlink-cast",
  "timestamp",
  "unknown-cast",
  "unknown-keyword",
  "xdev",
  "decompress-program",
  "existing-file",
  "xattr-write",
  "record-size",
  "failed-read",
  NULL
};

static int warning_types[] = {
  WARN_ALL,
  WARN_ALONE_ZERO_BLOCK,
  WARN_BAD_DUMPDIR,
  WARN_CACHEDIR,
  WARN_CONTIGUOUS_CAST,
  WARN_FILE_CHANGED,
  WARN_FILE_IGNORED,
  WARN_FILE_REMOVED,
  WARN_FILE_SHRANK,
  WARN_FILE_UNCHANGED,
  WARN_FILENAME_WITH_NULS,
  WARN_IGNORE_ARCHIVE,
  WARN_IGNORE_NEWER,
  WARN_NEW_DIRECTORY,
  WARN_RENAME_DIRECTORY,
  WARN_SYMLINK_CAST,
  WARN_TIMESTAMP,
  WARN_UNKNOWN_CAST,
  WARN_UNKNOWN_KEYWORD,
  WARN_XDEV,
  WARN_DECOMPRESS_PROGRAM,
  WARN_EXISTING_FILE,
  WARN_XATTR_WRITE,
  WARN_RECORD_SIZE,
  WARN_FAILED_READ
};

ARGMATCH_VERIFY (warning_args, warning_types);

int warning_option = WARN_ALL;

void
set_warning_option (const char *arg)
{
  int negate = 0;
  int option;

  if (strcmp (arg, "none") == 0)
    {
      warning_option = 0;
      return;
    }
  if (strlen (arg) > 2 && memcmp (arg, "no-", 3) == 0)
    {
      negate = 1;
      arg += 3;
    }

  option = XARGMATCH ("--warning", arg,
		      warning_args, warning_types);
  if (negate)
    warning_option &= ~option;
  else
    warning_option |= option;
}
