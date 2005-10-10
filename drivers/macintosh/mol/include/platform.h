/* 
 *   Creation Date: <2001/12/29 19:46:46 samuel>
 *   Time-stamp: <2003/08/09 23:46:32 samuel>
 *   
 *	<platform.h>
 *	
 *	Misc definitions needed on certain platforms
 *   
 *   Copyright (C) 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_PLATFORM
#define _H_PLATFORM

typedef unsigned long long 	ullong;
typedef long long 		llong;

typedef signed char 		s8;
typedef unsigned char 		u8;
typedef signed short		s16;
typedef unsigned short 		u16;
typedef signed int 		s32;
typedef unsigned int 		u32;
typedef signed long long	s64;
typedef unsigned long long	u64;

#define TO_ULLONG( hi, lo ) 	(((ullong)(hi)<< 32 ) | (lo) )
#define TO_LLONG( hi, lo ) 	(((llong)(hi)<< 32 ) | (lo) )

#ifndef TEMP_FAILURE_RETRY
# define TEMP_FAILURE_RETRY(expression) \
  (__extension__                                                              \
    ({ long int __result;                                                     \
       do __result = (long int) (expression);                                 \
       while (__result == -1L && errno == EINTR);                             \
       __result; }))
#endif

/*
 * Allow us to mark functions as 'deprecated' and have gcc emit a nice
 * warning for each use, in hopes of speeding the functions removal.
 * Usage is:
 *              int __deprecated foo(void)
 */
#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define __builtin_expect(x, expected_value) (x)
#endif

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#ifndef HAVE_CLEARENV
static inline int clearenv( void ) { extern char **environ; environ=NULL; return 0; }
#endif

#endif   /* _H_PLATFORM */
