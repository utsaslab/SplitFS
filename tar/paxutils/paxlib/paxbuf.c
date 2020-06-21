/* This file is part of GNU paxutils

   Copyright (C) 2005, 2007 Free Software Foundation, Inc.

   Written by Sergey Poznyakoff

   GNU paxutils is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any later
   version.

   GNU paxutils is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
   Public License for more details.

   You should have received a copy of the GNU General Public License along
   with GNU paxutils; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <errno.h>
#include <gettext.h>
#include <system.h>
#include <paxbuf.h>

/* PAX buffer structure */
struct pax_buffer
{
  size_t record_size;         /* Size of a record, bytes */
  size_t record_level;        /* Number of bytes stored in the record */
  size_t pos;                 /* Current position in buffer */
  char  *record;              /* Record buffer, record_size bytes long */

  int status;                 /* Return code from the latest I/O */
  
    /* I/O functions */
  paxbuf_io_fp writer;        /* Writes data */
  paxbuf_io_fp reader;        /* Reads data */
  paxbuf_seek_fp seek;        /* Seeks the underlying transport layer */
    /* Terminal functions */
  paxbuf_term_fp open;        /* Open a new volume */ 
  paxbuf_term_fp close;       /* Close the existing volume */
  paxbuf_destroy_fp destroy;  /* Destroy the closure data */ 
    /* Other callbacks */
  paxbuf_wrapper_fp wrapper;  /* Called when writer or reader returns EOF */

  void *closure;              /* Implementation-specific data */
  int mode;                   /* Working mode */
};


/* Default callbacks. Do nothing useful, except bailing out */

static void
noinit (const char *name)
{
  fprintf (stderr,
	   _("INTERNAL ERROR: %s is not initialized. Please, report.\n"),
	   name);
  exit (1);
}

static pax_io_status_t
default_reader (void *closure, void *data, size_t size, size_t *ret_size)
{
  noinit ("pax_buffer.reader");
  return pax_io_failure;
}

static pax_io_status_t
default_writer (void *closure, void *data, size_t size, size_t *ret_size)
{
  noinit ("pax_buffer.writer");
  return pax_io_failure;
}

static int
default_seek (void *closure, off_t offset)
{
  noinit ("pax_buffer.seek");
  return -1;
}

static int
default_open (void *closure, int mode)
{
  noinit ("pax_buffer.open");
  return -1;
}

static int
default_close (void *closure, int mode)
{
  noinit ("pax_buffer.close");
  return -1;
}

static int
default_destroy (void *closure)
{
  noinit ("pax_buffer.destroy");
  return -1;
}

static int
default_wrapper (void *closure)
{
  noinit ("pax_buffer.wrapper");
  return -1;
}

static const char *
default_error (void *closure)
{
  return strerror (errno);
}


/* Interface funtions */

/* 1. Initialize/destroy */

int
paxbuf_create (paxbuf_t *pbuf, int mode, void *closure, size_t record_size)
{
  paxbuf_t buf;

  buf = malloc (sizeof *buf);
  if (!buf)
    return ENOMEM;
  buf->record = malloc (record_size);
  if (!buf->record)
    {
      free (buf);
      return ENOMEM;
    }
  
  buf->record_size = record_size;
  buf->record_level = 0;
  buf->closure = closure;
  buf->mode = mode;
  
  paxbuf_set_io (buf, default_reader, default_writer, default_seek);
  paxbuf_set_term (buf, default_open, default_close, default_destroy);
  paxbuf_set_wrapper (buf, default_wrapper);
  
  *pbuf = buf;
  return 0;
}

void
paxbuf_destroy (paxbuf_t *pbuf)
{
  paxbuf_t buf = *pbuf;
  free (buf->record);
  if (buf->destroy)
    buf->destroy (buf->closure);
  free (buf);
  *pbuf = NULL;
}

void
paxbuf_set_io (paxbuf_t buf,
	       paxbuf_io_fp rd, paxbuf_io_fp wr, paxbuf_seek_fp seek)
{
  buf->writer = wr;
  buf->reader = rd;
  buf->seek = seek;
}

void
paxbuf_set_term (paxbuf_t buf,
		 paxbuf_term_fp open, paxbuf_term_fp close,
		 paxbuf_destroy_fp destroy)
{
  buf->open = open;
  buf->close = close;
  buf->destroy = destroy;
}

void
paxbuf_set_wrapper (paxbuf_t buf, paxbuf_wrapper_fp wrap)
{
  buf->wrapper = wrap;
}


/* 2. I/O operations and seek */

static pax_io_status_t
fill_buffer (paxbuf_t buf)
{
  pax_io_status_t status = pax_io_success;

  buf->record_level = 0;
  do
    {
      size_t s = 0;
      
      status = buf->reader (buf->closure, buf->record + buf->record_level,
			    buf->record_size - buf->record_level, &s);
      buf->record_level += s;
    }
  while ((status == pax_io_success && buf->record_level < buf->record_size)
	 || (status == pax_io_eof
	     && buf->wrapper
	     && buf->wrapper (buf->closure) == 0));

  buf->pos = 0;
  return status;
}

static pax_io_status_t
flush_buffer (paxbuf_t buf)
{
  pax_io_status_t status = pax_io_success;

  buf->record_level = 0;
  do
    {
      size_t s = 0;
      status = buf->writer (buf->closure, buf->record + buf->record_level,
			    buf->record_size - buf->record_level, &s);
      buf->record_level += s;
    }
  while ((status == pax_io_success && buf->record_level < buf->record_size)
	 || (status == pax_io_eof
	     && buf->wrapper
	     && buf->wrapper (buf->closure) == 0));
  buf->pos = 0;
  return status;
}

pax_io_status_t
paxbuf_read (paxbuf_t buf, char *data, size_t size, size_t *rsize)
{
  pax_io_status_t status = pax_io_success;

  *rsize = 0;
  while (size && status == pax_io_success)
    {
      size_t s;

      if (buf->pos == buf->record_level)
	{
	  status = fill_buffer (buf);
	  if (status == pax_io_failure)
	    break;
	}
      s = buf->record_level - buf->pos;
      if (s > size)
	s = size;
      memcpy (data, buf->record + buf->pos, s);
      data += s;
      buf->pos += s;
      size -= s;
      *rsize += s;
    }
  return status;
}

pax_io_status_t
paxbuf_write (paxbuf_t buf, char *data, size_t size, size_t *wsize)
{
  pax_io_status_t status = pax_io_success;

  *wsize = 0;
  while (size && status == pax_io_success)
    {
      size_t s;

      if (buf->pos == buf->record_size)
	{
	  status = flush_buffer (buf);
	  if (status == pax_io_failure)
	    break;
	}
      s = buf->record_size - buf->pos;
      if (s > size)
	s = size;
      memcpy (buf->record + buf->pos, data, s);
      data += s;
      buf->pos += s;
      size -= s;
      *wsize += s;
    }
  return status;
}

int
paxbuf_seek (paxbuf_t buf, off_t offset)
{
  /* FIXME */
  return buf->seek (buf->closure, offset);
}


/* 3. Open/close */
int
paxbuf_open (paxbuf_t buf)
{
  return buf->open (buf->closure, buf->mode);
}

int
paxbuf_close (paxbuf_t buf)
{
  pax_io_status_t status;
  if ((buf->mode & PAXBUF_WRITE) && buf->pos != 0)
    status = flush_buffer (buf);
  return buf->close (buf->closure, buf->mode) || status != pax_io_success;
}


/* Accessors */

void *
paxbuf_get_data (paxbuf_t buf)
{
  return buf->closure;
}

int
paxbuf_get_mode (paxbuf_t buf)
{
  return buf->mode;
}

