/* Checkpoint management for tar.

   Copyright 2007, 2013-2014, 2016-2017 Free Software Foundation, Inc.

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
#include <sys/ioctl.h>
#include <termios.h>
#include "fprintftime.h"

enum checkpoint_opcode
  {
    cop_dot,
    cop_bell,
    cop_echo,
    cop_ttyout,
    cop_sleep,
    cop_exec,
    cop_totals
  };

struct checkpoint_action
{
  struct checkpoint_action *next;
  enum checkpoint_opcode opcode;
  union
  {
    time_t time;
    char *command;
  } v;
};

/* Checkpointing counter */
static unsigned checkpoint;

/* List of checkpoint actions */
static struct checkpoint_action *checkpoint_action, *checkpoint_action_tail;

static struct checkpoint_action *
alloc_action (enum checkpoint_opcode opcode)
{
  struct checkpoint_action *p = xzalloc (sizeof *p);
  if (checkpoint_action_tail)
    checkpoint_action_tail->next = p;
  else
    checkpoint_action = p;
  checkpoint_action_tail = p;
  p->opcode = opcode;
  return p;
}

static char *
copy_string_unquote (const char *str)
{
  char *output = xstrdup (str);
  size_t len = strlen (output);
  if ((*output == '"' || *output == '\'')
      && output[len-1] == *output)
    {
      memmove (output, output+1, len-2);
      output[len-2] = 0;
    }
  unquote_string (output);
  return output;
}

void
checkpoint_compile_action (const char *str)
{
  struct checkpoint_action *act;

  if (strcmp (str, ".") == 0 || strcmp (str, "dot") == 0)
    alloc_action (cop_dot);
  else if (strcmp (str, "bell") == 0)
    alloc_action (cop_bell);
  else if (strcmp (str, "echo") == 0)
    alloc_action (cop_echo);
  else if (strncmp (str, "echo=", 5) == 0)
    {
      act = alloc_action (cop_echo);
      act->v.command = copy_string_unquote (str + 5);
    }
  else if (strncmp (str, "exec=", 5) == 0)
    {
      act = alloc_action (cop_exec);
      act->v.command = copy_string_unquote (str + 5);
    }
  else if (strncmp (str, "ttyout=", 7) == 0)
    {
      act = alloc_action (cop_ttyout);
      act->v.command = copy_string_unquote (str + 7);
    }
  else if (strncmp (str, "sleep=", 6) == 0)
    {
      char *p;
      time_t n = strtoul (str+6, &p, 10);
      if (*p)
	FATAL_ERROR ((0, 0, _("%s: not a valid timeout"), str));
      act = alloc_action (cop_sleep);
      act->v.time = n;
    }
  else if (strcmp (str, "totals") == 0)
    alloc_action (cop_totals);
  else
    FATAL_ERROR ((0, 0, _("%s: unknown checkpoint action"), str));
}

void
checkpoint_finish_compile (void)
{
  if (checkpoint_option)
    {
      if (!checkpoint_action)
	/* Provide a historical default */
	checkpoint_compile_action ("echo");
    }
  else if (checkpoint_action)
    /* Otherwise, set default checkpoint rate */
    checkpoint_option = DEFAULT_CHECKPOINT;
}

static const char *checkpoint_total_format[] = {
  "R",
  "W",
  "D"
};

static long
getwidth (FILE *fp)
{
  char const *columns;

#ifdef TIOCGWINSZ
  struct winsize ws;
  if (ioctl (fileno (fp), TIOCGWINSZ, &ws) == 0 && 0 < ws.ws_col)
    return ws.ws_col;
#endif

  columns = getenv ("COLUMNS");
  if (columns)
    {
      long int col = strtol (columns, NULL, 10);
      if (0 < col)
	return col;
    }

  return 80;
}

static char *
getarg (const char *input, const char ** endp, char **argbuf, size_t *arglen)
{
  if (input[0] == '{')
    {
      char *p = strchr (input + 1, '}');
      if (p)
	{
	  size_t n = p - input;
	  if (n > *arglen)
	    {
	      *arglen = n;
	      *argbuf = xrealloc (*argbuf, *arglen);
	    }
	  n--;
	  memcpy (*argbuf, input + 1, n);
	  (*argbuf)[n] = 0;
	  *endp = p + 1;
	  return *argbuf;
	}
    }

  *endp = input;
  return NULL;
}

static int tty_cleanup;

static const char *def_format =
  "%{%Y-%m-%d %H:%M:%S}t: %ds, %{read,wrote}T%*\r";

static int
format_checkpoint_string (FILE *fp, size_t len,
			  const char *input, bool do_write,
			  unsigned cpn)
{
  const char *opstr = do_write ? gettext ("write") : gettext ("read");
  char uintbuf[UINTMAX_STRSIZE_BOUND];
  char *cps = STRINGIFY_BIGINT (cpn, uintbuf);
  const char *ip;

  static char *argbuf = NULL;
  static size_t arglen = 0;
  char *arg = NULL;

  if (!input)
    {
      if (do_write)
	/* TRANSLATORS: This is a "checkpoint of write operation",
	 *not* "Writing a checkpoint".
	 E.g. in Spanish "Punto de comprobaci@'on de escritura",
	 *not* "Escribiendo un punto de comprobaci@'on" */
	input = gettext ("Write checkpoint %u");
      else
	/* TRANSLATORS: This is a "checkpoint of read operation",
	 *not* "Reading a checkpoint".
	 E.g. in Spanish "Punto de comprobaci@'on de lectura",
	 *not* "Leyendo un punto de comprobaci@'on" */
	input = gettext ("Read checkpoint %u");
    }

  for (ip = input; *ip; ip++)
    {
      if (*ip == '%')
	{
	  if (*++ip == '{')
	    {
	      arg = getarg (ip, &ip, &argbuf, &arglen);
	      if (!arg)
		{
		  fputc ('%', fp);
		  fputc (*ip, fp);
		  len += 2;
		  continue;
		}
	    }
	  switch (*ip)
	    {
	    case 'c':
	      len += format_checkpoint_string (fp, len, def_format, do_write,
					       cpn);
	      break;

	    case 'u':
	      fputs (cps, fp);
	      len += strlen (cps);
	      break;

	    case 's':
	      fputs (opstr, fp);
	      len += strlen (opstr);
	      break;

	    case 'd':
	      len += fprintf (fp, "%.0f", compute_duration ());
	      break;

	    case 'T':
	      {
		const char **fmt = checkpoint_total_format, *fmtbuf[3];
		struct wordsplit ws;
		compute_duration ();

		if (arg)
		  {
		    ws.ws_delim = ",";
		    if (wordsplit (arg, &ws, WRDSF_NOVAR | WRDSF_NOCMD |
				           WRDSF_QUOTE | WRDSF_DELIM))
		      ERROR ((0, 0, _("cannot split string '%s': %s"),
			      arg, wordsplit_strerror (&ws)));
		    else
		      {
			int i;

			for (i = 0; i < ws.ws_wordc; i++)
			  fmtbuf[i] = ws.ws_wordv[i];
			for (; i < 3; i++)
			  fmtbuf[i] = NULL;
			fmt = fmtbuf;
		      }
		  }
		len += format_total_stats (fp, fmt, ',', 0);
		if (arg)
		  wordsplit_free (&ws);
	      }
	      break;

	    case 't':
	      {
		struct timeval tv;
		struct tm *tm;
		const char *fmt = arg ? arg : "%c";

		gettimeofday (&tv, NULL);
		tm = localtime (&tv.tv_sec);
		len += fprintftime (fp, fmt, tm, 0, tv.tv_usec * 1000);
	      }
	      break;

	    case '*':
	      {
		long w = arg ? strtol (arg, NULL, 10) : getwidth (fp);
		for (; w > len; len++)
		  fputc (' ', fp);
	      }
	      break;

	    default:
	      fputc ('%', fp);
	      fputc (*ip, fp);
	      len += 2;
	      break;
	    }
	  arg = NULL;
	}
      else
	{
	  fputc (*ip, fp);
	  if (*ip == '\r')
	    {
	      len = 0;
	      tty_cleanup = 1;
	    }
	  else
	    len++;
	}
    }
  fflush (fp);
  return len;
}

static FILE *tty = NULL;

static void
run_checkpoint_actions (bool do_write)
{
  struct checkpoint_action *p;

  for (p = checkpoint_action; p; p = p->next)
    {
      switch (p->opcode)
	{
	case cop_dot:
	  fputc ('.', stdlis);
	  fflush (stdlis);
	  break;

	case cop_bell:
	  if (!tty)
	    tty = fopen ("/dev/tty", "w");
	  if (tty)
	    {
	      fputc ('\a', tty);
	      fflush (tty);
	    }
	  break;

	case cop_echo:
	  {
	    int n = fprintf (stderr, "%s: ", program_name);
	    format_checkpoint_string (stderr, n, p->v.command, do_write,
				      checkpoint);
	    fputc ('\n', stderr);
	  }
	  break;

	case cop_ttyout:
	  if (!tty)
	    tty = fopen ("/dev/tty", "w");
	  if (tty)
	    format_checkpoint_string (tty, 0, p->v.command, do_write,
				      checkpoint);
	  break;

	case cop_sleep:
	  sleep (p->v.time);
	  break;

	case cop_exec:
	  sys_exec_checkpoint_script (p->v.command,
				      archive_name_cursor[0],
				      checkpoint);
	  break;

	case cop_totals:
	  compute_duration ();
	  print_total_stats ();
	}
    }
}

void
checkpoint_flush_actions (void)
{
  struct checkpoint_action *p;

  for (p = checkpoint_action; p; p = p->next)
    {
      switch (p->opcode)
	{
	case cop_ttyout:
	  if (tty && tty_cleanup)
	    {
	      long w = getwidth (tty);
	      while (w--)
		fputc (' ', tty);
	      fputc ('\r', tty);
	      fflush (tty);
	    }
	  break;
	default:
	  /* nothing */;
	}
    }
}

void
checkpoint_run (bool do_write)
{
  if (checkpoint_option && !(++checkpoint % checkpoint_option))
    run_checkpoint_actions (do_write);
}

void
checkpoint_finish (void)
{
  if (checkpoint_option)
    {
      checkpoint_flush_actions ();
      if (tty)
	fclose (tty);
    }
}
