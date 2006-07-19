/* 
 *   Creation Date: <2004/01/25 17:00:12 samuel>
 *   Time-stamp: <2004/01/29 22:32:30 samuel>
 *   
 *	<atomic.h>
 *	
 *	
 *   
 *   Copyright (C) 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *   
 */

#ifndef _H_ATOMIC
#define _H_ATOMIC

#define	mol_atomic_t			atomic_t
#define atomic_inc_return_mol(x)	atomic_inc_return(x)
#define atomic_inc_mol(x)		atomic_inc(x)
#define atomic_dec_mol(x)		atomic_dec(x)
#define atomic_read_mol(x)		atomic_read(x)

#endif   /* _H_ATOMIC */
