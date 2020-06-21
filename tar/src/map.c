/* Owner/group mapping for tar

   Copyright 2015-2017 Free Software Foundation, Inc.

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
#include "wordsplit.h"
#include <hash.h>
#include <pwd.h>

struct mapentry
{
  uintmax_t orig_id;
  uintmax_t new_id;
  char *new_name;
};

static size_t
map_hash (void const *entry, size_t nbuckets)
{
  struct mapentry const *map = entry;
  return map->orig_id % nbuckets;
}

static bool
map_compare (void const *entry1, void const *entry2)
{
  struct mapentry const *map1 = entry1;
  struct mapentry const *map2 = entry2;
  return map1->orig_id == map2->orig_id;
}

static int
parse_id (uintmax_t *retval,
	  char const *arg, char const *what, uintmax_t maxval,
	  char const *file, unsigned line)
{
  uintmax_t v;
  char *p;
  
  errno = 0;
  v = strtoumax (arg, &p, 10);
  if (*p || errno)
    {
      error (0, 0, _("%s:%u: invalid %s: %s"),  file, line, what, arg);
      return -1;
    }
  if (v > maxval)
    {
      error (0, 0, _("%s:%u: %s out of range: %s"), file, line, what, arg);
      return -1;
    }
  *retval = v;
  return 0;
}

static void
map_read (Hash_table **ptab, char const *file,
	  uintmax_t (*name_to_id) (char const *), char const *what,
	  uintmax_t maxval)
{
  FILE *fp;
  char *buf = NULL;
  size_t bufsize = 0;
  ssize_t n;
  struct wordsplit ws;
  int wsopt;
  unsigned line;
  int err = 0;
  
  fp = fopen (file, "r");
  if (!fp)
    open_fatal (file);

  ws.ws_comment = "#";
  wsopt = WRDSF_COMMENT | WRDSF_NOVAR | WRDSF_NOCMD | WRDSF_SQUEEZE_DELIMS
          | WRDSF_QUOTE;
  line = 0;
  while ((n = getline (&buf, &bufsize, fp)) > 0)
    {
      struct mapentry *ent;
      uintmax_t orig_id, new_id;
      char *name = NULL;
      char *colon;
      
      ++line;
      if (wordsplit (buf, &ws, wsopt))
	FATAL_ERROR ((0, 0, _("%s:%u: cannot split line: %s"),
		      file, line, wordsplit_strerror (&ws)));
      wsopt |= WRDSF_REUSE;
      if (ws.ws_wordc == 0)
	continue;
      if (ws.ws_wordc != 2)
	{
	  error (0, 0, _("%s:%u: malformed line"), file, line);
	  err = 1;
	  continue;
	}

      if (ws.ws_wordv[0][0] == '+')
	{
	  if (parse_id (&orig_id, ws.ws_wordv[0]+1, what, maxval, file, line)) 
	    {
	      err = 1;
	      continue;
	    }
	}
      else if (name_to_id)
	{
	  orig_id = name_to_id (ws.ws_wordv[0]);
	  if (orig_id == UINTMAX_MAX)
	    {
	      error (0, 0, _("%s:%u: can't obtain %s of %s"),
		     file, line, what, ws.ws_wordv[0]);
	      err = 1;
	      continue;
	    }
	}

      colon = strchr (ws.ws_wordv[1], ':');
      if (colon)
	{
	  if (colon > ws.ws_wordv[1])
	    name = ws.ws_wordv[1];
	  *colon++ = 0;
	  if (parse_id (&new_id, colon, what, maxval, file, line)) 
	    {
	      err = 1;
	      continue;
	    }
	}
      else if (ws.ws_wordv[1][0] == '+')
	{
	  if (parse_id (&new_id, ws.ws_wordv[1], what, maxval, file, line)) 
	    {
	      err = 1;
	      continue;
	    }
	}
      else
	{
	  name = ws.ws_wordv[1];
	  new_id = name_to_id (ws.ws_wordv[1]);
	  if (new_id == UINTMAX_MAX)
	    {
	      error (0, 0, _("%s:%u: can't obtain %s of %s"),
		     file, line, what, ws.ws_wordv[1]);
	      err = 1;
	      continue;
	    }
	}

      ent = xmalloc (sizeof (*ent));
      ent->orig_id = orig_id;
      ent->new_id = new_id;
      ent->new_name = name ? xstrdup (name) : NULL;
      
      if (!((*ptab
	     || (*ptab = hash_initialize (0, 0, map_hash, map_compare, 0)))
	    && hash_insert (*ptab, ent)))
	xalloc_die ();
    }
  if (wsopt & WRDSF_REUSE)
    wordsplit_free (&ws);
  fclose (fp);
  if (err)
    FATAL_ERROR ((0, 0, _("errors reading map file")));
}

/* UID translation */

static Hash_table *owner_map;

static uintmax_t
name_to_uid (char const *name)
{
  struct passwd *pw = getpwnam (name);
  return pw ? pw->pw_uid : UINTMAX_MAX;
}

void
owner_map_read (char const *file)
{
  map_read (&owner_map, file, name_to_uid, "UID", TYPE_MAXIMUM (uid_t));
}

int
owner_map_translate (uid_t uid, uid_t *new_uid, char const **new_name)
{
  int rc = 1;
  
  if (owner_map)
    {
      struct mapentry ent, *res;
  
      ent.orig_id = uid;
      res = hash_lookup (owner_map, &ent);
      if (res)
	{
	  *new_uid = res->new_id;
	  *new_name = res->new_name;
	  return 0;
	}
    }

  if (owner_option != (uid_t) -1)
    {
      *new_uid = owner_option;
      rc = 0;
    }
  if (owner_name_option)
    {
      *new_name = owner_name_option;
      rc = 0;
    }

  return rc;
}

/* GID translation */

static Hash_table *group_map;

static uintmax_t
name_to_gid (char const *name)
{
  struct group *gr = getgrnam (name);
  return gr ? gr->gr_gid : UINTMAX_MAX;
}

void
group_map_read (char const *file)
{
  map_read (&group_map, file, name_to_gid, "GID", TYPE_MAXIMUM (gid_t));
}

int
group_map_translate (gid_t gid, gid_t *new_gid, char const **new_name)
{
  int rc = 1;
  
  if (group_map)
    {
      struct mapentry ent, *res;
  
      ent.orig_id = gid;
      res = hash_lookup (group_map, &ent);
      if (res)
	{
	  *new_gid = res->new_id;
	  *new_name = res->new_name;
	  return 0;
	}
    }

  if (group_option != (uid_t) -1)
    {
      *new_gid = group_option;
      rc = 0;
    }
  if (group_name_option)
    {
      *new_name = group_name_option;
      rc = 0;
    }
  
  return rc;
}
