/*
 *   Creation Date: <2004/04/10 22:14:43 samuel>
 *   Time-stamp: <2004/04/10 22:26:24 samuel>
 *
 *	<dbg.h>
 *
 *
 *
 *   Copyright (C) 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 */

#ifndef _H_DBG
#define _H_DBG

#ifdef CONFIG_MOL_HOSTED

#ifdef printk
#undef printk
#endif

#define printk			printm
extern int			printm( const char *fmt, ... );
extern void			debugger( int n );

#endif /* CONFIG_MOL_HOSTED */

#endif   /* _H_DBG */
