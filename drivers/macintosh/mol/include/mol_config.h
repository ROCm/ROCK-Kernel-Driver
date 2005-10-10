/* 
 *   Creation Date: <1999/05/30 15:30:25 samuel>
 *   Time-stamp: <2004/01/14 21:14:28 samuel>
 *   
 *	<mol_config.h>
 *	
 *	Header to be included first...
 *   
 *   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_MOL_CONFIG
#define _H_MOL_CONFIG

/* Some debugging flags */
#define COLLECT_RVEC_STATISTICS
//#define EMULATE_603
//#define ENABLE_ASSERT

#define _GNU_SOURCE
#define _REENTRANT
#define _LARGEFILE64_SOURCE

#if defined(__powerpc__) && !defined(__ppc__)
#define __ppc__
#endif
#if defined(__ppc__) && !defined(__powerpc__)
#define __powerpc__
#endif

#if !defined(__linux__) && !defined(__ASSEMBLY__)
typedef unsigned long	ulong;
#endif

#if !defined(__ASSEMBLY__) && !defined(__KERNEL__)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include "config.h"
#include "autoconf.h"

#ifdef CONFIG_OLDWORLD
#define OLDWORLD_SUPPORT
#endif
#ifndef HAVE_ALSA
#undef CONFIG_ALSA
#endif
#ifndef HAVE_X11
#undef CONFIG_X11
#undef CONFIG_XDGA
#endif
#ifndef HAVE_XDGA
#undef CONFIG_XDGA
#endif

#include "platform.h"

/* from emulaiton/main.c */
extern int in_security_mode;

/* common MOL header fiels */

#include "debugger.h"		/* for printm */
#include "extralib.h"

#endif /* __ASSEMBLY__ && __KERNEL__ */

#ifdef __ASSEMBLY__
changequote([[[[[,]]]]])
[[[[[	/* shield includes from m4-expansion */
#endif

#endif   /* _H_MOL_CONFIG */
