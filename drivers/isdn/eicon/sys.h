
/*
 *
 * Copyright (C) Eicon Technology Corporation, 2000.
 *
 * This source file is supplied for the exclusive use with Eicon
 * Technology Corporation's range of DIVA Server Adapters.
 *
 * Eicon File Revision :    1.2  
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY 
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


/* Environment provided by system and miscellaneous definitions */

#if !defined(SYS_H)
#define SYS_H

/* abreviations for unsigned types */
typedef int boolean_t;

typedef unsigned char byte;

typedef unsigned long dword;
typedef unsigned short word;

/* abreviations for volatile types */

typedef volatile byte	vbyte;
typedef volatile word	vword;
typedef volatile dword	vdword;

/* Booleans */

#if !defined(TRUE)
#define TRUE    (1)
#define FALSE   (0)
#endif

/* NULL pointer */

#if !defined(NULL)
#define NULL    ((void *) 0)
#endif

/* MIN and MAX */

#if !defined(MIN)
#define MIN(a,b) ((a)>(b) ? (b) : (a))
#endif
#if !defined(MAX)
#define MAX(a,b) ((a)>(b) ? (a) : (b))
#endif

/* Return the dimension of an array */

#if !defined(DIM)
#define DIM(array)  (sizeof (array)/sizeof ((array)[0]))
#endif

/*
 * Return the number of milliseconds since last boot
 */

extern	dword	UxTimeGet(void);

extern	void 	DivasSprintf(char *buffer, char *format, ...);
extern	void 	DivasPrintf(char *format, ...);

/* fatal errors, asserts and tracing */

void HwFatalErrorFrom(char *file, int line);
void HwFatalError(void);
/* void HwAssert(char *file, int line, char *condition); */

#include <linux/kernel.h>
#define _PRINTK printk

#define _PRINTF	DivasPrintf
void _PRINTF(char *format, ...);
#define PRINTF(arg_list)	_PRINTF arg_list
#if defined DTRACE
# define DPRINTF(arg_list)	_PRINTF arg_list
# define KDPRINTF(arg_list)	_PRINTF arg_list ; _PRINTK arg_list ; _PRINTK("\n");
#else
# define DPRINTF(arg_list)	(void)0
# define KDPRINTF(arg_list)	_PRINTK arg_list ; _PRINTK("\n");
#endif

#if !defined(ASSERT)
#if defined DEBUG || defined DBG
# define HwFatalError()	HwFatalErrorFrom(__FILE__, __LINE__)
# define ASSERT(cond)								\
		if (!(cond)) 								\
		{											\
/*			HwAssert(__FILE__, __LINE__, #cond);*/	\
		}
#else
# define ASSERT(cond)	((void)0)
#endif
#endif /* !defined(ASSERT) */

#define TRACE	(_PRINTF(__FILE__"@%d\n", __LINE__))

#endif /* SYS_H */
