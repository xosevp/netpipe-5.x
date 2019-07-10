/* MP_Lite message-passing library
 * Copyright (C) 1997-2004 Dave Turner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Contact: Dave Turner - turner@ameslab.gov
 */

#include <stdlib.h>

#define MP_MEM
#include "globals.h"
#undef  MP_MEM

#ifdef TCP

void* MP_Malloc(size_t size)
{
  void* p;

  start_protect_from_sig_handler();

  p = malloc(size);

  stop_protect_from_sig_handler();

  return p;
}

void* MP_Realloc(void *ptr, size_t size)
{
  void* p;

  start_protect_from_sig_handler();  

  p = realloc(ptr, size);

  stop_protect_from_sig_handler();

  return p;
}

void* MP_Calloc(size_t nmemb, size_t size)
{
  void* p;
  
  start_protect_from_sig_handler();

  p = calloc(nmemb, size);

  stop_protect_from_sig_handler();

  return p;
}

void MP_Free(void *ptr)
{

  start_protect_from_sig_handler();

  free(ptr);

  stop_protect_from_sig_handler();

}

#else

void* MP_Malloc(size_t size) { return malloc(size); }

void* MP_Realloc(void *ptr, size_t size) { return realloc(ptr, size); }

void* MP_Calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }

void MP_Free(void *ptr) { free(ptr); }

#endif

