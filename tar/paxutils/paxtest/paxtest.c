/* This file is part of GNU paxutils

   Copyright (C) 2005, 2007 Free Software Foundation, Inc.

   Written by Sergey Poznyakoff

   GNU paxutils is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any later
   version.

   GNU paxutils program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
   Public License for more details.

   You should have received a copy of the GNU General Public License along
   with GNU paxutils; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <paxtest.h>

#ifndef DEFAULT_BLOCKING_FACTOR
# define DEFAULT_BLOCKING_FACTOR 20
#endif

void
xalloc_die (void)
{
     error(1, ENOMEM, "Exiting");
}

void
dump (unsigned char *buf, size_t size)
{
  while (size)
    {
      int i;
      for (i = 0; i < 16 && size; i++, size--, buf++)
	printf ("%02X ", *buf);
      printf ("\n");
    }
}
  

void
read_and_dump (paxbuf_t pbuf)
{
  union block block;
  size_t size;
  pax_io_status_t rc;
  
  while ((rc = paxbuf_read (pbuf, block.buffer, sizeof block, &size))
	 == pax_io_success)
    {
      dump (block.buffer, size);
    }
  if (rc == pax_io_failure)
    error (1, 0, "Read error");
}

int
main (int argc, char **argv)
{
  paxbuf_t pbuf;
  int rc;

  if (argc == 1)
    error (1, 0, "Not enough arguments");

  tar_archive_create (&pbuf, argv[1], 0, PAXBUF_READ, DEFAULT_BLOCKING_FACTOR);
  
  rc = paxbuf_open (pbuf);
  printf ("Open: %d\n", rc);
  if (rc)
    abort ();
  read_and_dump (pbuf)
  paxbuf_close (pbuf);
  paxbuf_destroy (&pbuf);
  return 0;
}
