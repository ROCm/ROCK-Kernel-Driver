/* memory.h - memory allocation
 *	Copyright (C) 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
 *
 * This file is part of GNUPG.
 *
 * GNUPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GNUPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef G10_MEMORY_H
#define G10_MEMORY_H

#include <linux/mm.h>


#define m_alloc(n)		kmalloc(n, GFP_KERNEL)
#define m_alloc_clear(n)	kmalloc(n, GFP_KERNEL) /* can't memset, no size or don't know how to get it */
#define m_alloc_secure(n)	kmalloc(n, GFP_KERNEL)
#define m_alloc_secure_clear(n) kmalloc(n, GFP_KERNEL) /* can't memset, no size or don't know how to get it */
#define m_free(n)		kfree(n) 
#define m_check(n)		/* nothing to do here */
#define m_size(n)               sizeof(n)
#define m_is_secure(n)          1

/* &&&& realloc kernel hack, should check this */
#define m_realloc(n,m)		krealloc((n),(m))

static inline void *
krealloc(void *ptr, size_t size)
{
  void *tmp = NULL;
  if (size) {
    tmp = kmalloc(size,GFP_KERNEL);
    if (ptr) memcpy(tmp,ptr,size);
  }
  kfree(ptr);
  return tmp;
}

#define DBG_MEMORY    memory_debug_mode
#define DBG_MEMSTAT   memory_stat_debug_mode

#define EXTERN_UNLESS_MAIN_MODULE extern

EXTERN_UNLESS_MAIN_MODULE int memory_debug_mode;
EXTERN_UNLESS_MAIN_MODULE int memory_stat_debug_mode;


#endif /*G10_MEMORY_H*/
