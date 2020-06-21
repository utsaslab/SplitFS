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

typedef struct pax_buffer *paxbuf_t;

typedef enum pax_io_status
  {
    pax_io_success,
    pax_io_failure,
    pax_io_eof
  }
pax_io_status_t;

#define PAXBUF_READ  0x1
#define PAXBUF_WRITE 0x2
#define PAXBUF_CREAT 0x4

typedef pax_io_status_t (*paxbuf_io_fp) (void *closure,
					 void *data, size_t size,
					 size_t *ret_size);
typedef int (*paxbuf_seek_fp) (void *closure, off_t offset);
typedef int (*paxbuf_term_fp) (void *closure, int mode);
typedef int (*paxbuf_destroy_fp) (void *closure);
typedef int (*paxbuf_wrapper_fp) (void *closure);
typedef const char * (*paxbuf_error_fp) (void *closure);

int paxbuf_create (paxbuf_t *buf, int mode, void *closure, size_t record_size);
int paxbuf_open (paxbuf_t buf);
int paxbuf_close (paxbuf_t buf);
void paxbuf_set_io (paxbuf_t buf, paxbuf_io_fp rd, paxbuf_io_fp wr,
		    paxbuf_seek_fp seek);
void paxbuf_set_term (paxbuf_t buf,
		      paxbuf_term_fp open, paxbuf_term_fp close,
		      paxbuf_destroy_fp destroy);
void paxbuf_set_wrapper (paxbuf_t buf, paxbuf_wrapper_fp wrap);
void paxbuf_set_error (paxbuf_t buf, paxbuf_error_fp err);

pax_io_status_t paxbuf_read (paxbuf_t pbuf, char *buf, size_t size,
			     size_t *rsize);
pax_io_status_t paxbuf_write (paxbuf_t pbuf, char *buf, size_t size,
			      size_t *rsize);
int paxbuf_seek (paxbuf_t buf, off_t offset);

void paxbuf_destroy (paxbuf_t *buf);

void *paxbuf_get_data (paxbuf_t buf);
int paxbuf_get_mode (paxbuf_t buf);

