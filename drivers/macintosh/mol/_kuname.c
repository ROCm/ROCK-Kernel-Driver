/* 
 *   Creation Date: <2001/08/15 01:11:01 samuel>
 *   Time-stamp: <2003/10/24 10:22:00 samuel>
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
#include <linux/utsrelease.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#ifdef CONFIG_SMP
#define SMP_STRING	"-smp"
#else
#define SMP_STRING	""
#endif

#ifndef CONFIG_ALTIVEC
#define ALTIVEC_STRING	"-noav"
#else
#define ALTIVEC_STRING	""
#endif

#else
#define SMP_STRING	""
#define ALTIVEC_STRING	""
#endif

char *cross_compiling_magic = "-MAGIC-" UTS_RELEASE SMP_STRING ALTIVEC_STRING ;
