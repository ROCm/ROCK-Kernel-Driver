/* 
 *   Creation Date: <2001/08/15 01:11:01 samuel>
 *   Time-stamp: <2003/08/27 22:47:56 samuel>
 *   
 *	<kuname.c>
 *	
 *	Extract from the kernel source
 *   
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include <linux/config.h>
#include <linux/version.h>

#ifdef CONFIG_SMP
#define SMP_STRING	"-smp"
#else
#define SMP_STRING	""
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#ifndef CONFIG_ALTIVEC
#define ALTIVEC_STRING	"-noav"
#else
#define ALTIVEC_STRING	""
#endif

#else 
#define ALTIVEC_STRING	""
#endif

char *cross_compiling_magic = "-MAGIC-" UTS_RELEASE SMP_STRING ALTIVEC_STRING ;
