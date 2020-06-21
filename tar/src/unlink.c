/* Unlink files.

   Copyright 2009, 2013-2014, 2016-2017 Free Software Foundation, Inc.

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
#include "common.h"
#include <quotearg.h>

struct deferred_unlink
  {
    struct deferred_unlink *next;   /* Next unlink in the queue */
    int dir_idx;                    /* Directory index in wd */
    char *file_name;                /* Name of the file to unlink, relative
				       to dir_idx */
    bool is_dir;                    /* True if file_name is a directory */
    off_t records_written;          /* Number of records written when this
				       entry got added to the queue */
  };

#define IS_CWD(p) \
  ((p)->is_dir \
   && ((p)->file_name[0] == 0 || strcmp ((p)->file_name, ".") == 0))

/* The unlink queue */
static struct deferred_unlink *dunlink_head, *dunlink_tail;

/* Number of entries in the queue */
static size_t dunlink_count;

/* List of entries available for allocation */
static struct deferred_unlink *dunlink_avail;

/* Delay (number of records written) between adding entry to the
   list and its actual removal. */
static size_t deferred_unlink_delay = 0;

static struct deferred_unlink *
dunlink_alloc (void)
{
  struct deferred_unlink *p;
  if (dunlink_avail)
    {
      p = dunlink_avail;
      dunlink_avail = p->next;
      p->next  = NULL;
    }
  else
    p = xmalloc (sizeof (*p));
  return p;
}

static void
dunlink_insert (struct deferred_unlink *anchor, struct deferred_unlink *p)
{
  if (anchor)
    {
      p->next = anchor->next;
      anchor->next = p;
    }
  else 
    {
      p->next = dunlink_head;
      dunlink_head = p;
    }
  if (!p->next)
    dunlink_tail = p;
  dunlink_count++;
}

static void
dunlink_reclaim (struct deferred_unlink *p)
{
  free (p->file_name);
  p->next = dunlink_avail;
  dunlink_avail = p;
}

static void
flush_deferred_unlinks (bool force)
{
  struct deferred_unlink *p, *prev = NULL;
  int saved_chdir = chdir_current;
  
  for (p = dunlink_head; p; )
    {
      struct deferred_unlink *next = p->next;

      if (force
	  || records_written > p->records_written + deferred_unlink_delay)
	{
	  chdir_do (p->dir_idx);
	  if (p->is_dir)
	    {
	      const char *fname;

	      if (p->dir_idx && IS_CWD (p))
		{
		  prev = p;
		  p = next;
		  continue;
		}
	      else
		fname = p->file_name;

	      if (unlinkat (chdir_fd, fname, AT_REMOVEDIR) != 0)
		{
		  switch (errno)
		    {
		    case ENOENT:
		      /* nothing to worry about */
		      break;
		    case EEXIST:
		      /* OpenSolaris >=10 sets EEXIST instead of ENOTEMPTY
			 if trying to remove a non-empty directory */
		    case ENOTEMPTY:
		      /* Keep the record in list, in the hope we'll
			 be able to remove it later */
		      prev = p;
		      p = next;
		      continue;

		    default:
		      rmdir_error (fname);
		    }
		}
	    }
	  else
	    {
	      if (unlinkat (chdir_fd, p->file_name, 0) != 0 && errno != ENOENT)
		unlink_error (p->file_name);
	    }
	  dunlink_reclaim (p);
	  dunlink_count--;
	  p = next;
	  if (prev)
	    prev->next = p;
	  else
	    dunlink_head = p;
	}
      else
	{
	  prev = p;
	  p = next;
	}
    }
  if (!dunlink_head)
    dunlink_tail = NULL;
  else if (force)
    {
      for (p = dunlink_head; p; )
	{
	  struct deferred_unlink *next = p->next;
	  const char *fname;

	  chdir_do (p->dir_idx);
	  if (p->dir_idx && IS_CWD (p))
	    {
	      fname = tar_dirname ();
	      chdir_do (p->dir_idx - 1);
	    }
	  else
	    fname = p->file_name;

	  if (unlinkat (chdir_fd, fname, AT_REMOVEDIR) != 0)
	    {
	      if (errno != ENOENT)
		rmdir_error (fname);
	    }
	  dunlink_reclaim (p);
	  dunlink_count--;
	  p = next;
	}
      dunlink_head = dunlink_tail = NULL;
    }	  
	    
  chdir_do (saved_chdir);
}

void
finish_deferred_unlinks (void)
{
  flush_deferred_unlinks (true);
  
  while (dunlink_avail)
    {
      struct deferred_unlink *next = dunlink_avail->next;
      free (dunlink_avail);
      dunlink_avail = next;
    }
}

void
queue_deferred_unlink (const char *name, bool is_dir)
{
  struct deferred_unlink *p;

  if (dunlink_head
      && records_written > dunlink_head->records_written + deferred_unlink_delay)
    flush_deferred_unlinks (false);

  p = dunlink_alloc ();
  p->next = NULL;
  p->dir_idx = chdir_current;
  p->file_name = xstrdup (name);
  normalize_filename_x (p->file_name);
  p->is_dir = is_dir;
  p->records_written = records_written;

  if (IS_CWD (p))
    {
      struct deferred_unlink *q, *prev;
      for (q = dunlink_head, prev = NULL; q; prev = q, q = q->next)
	if (IS_CWD (q) && q->dir_idx < p->dir_idx)
	  break;
      if (q)
	dunlink_insert (prev, p);
      else
	dunlink_insert (dunlink_tail, p);
    }
  else
    dunlink_insert (dunlink_tail, p);
}
