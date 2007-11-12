/* 
 *   Creation Date: <2004/01/25 16:31:13 samuel>
 *   Time-stamp: <2004/01/29 22:33:29 samuel>
 *   
 *	<locks.h>
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

#ifndef _H_LOCKS
#define _H_LOCKS

/* mutex locks */

typedef struct semaphore			mol_mutex_t;
#define init_MUTEX_mol(mu)			init_MUTEX( mu )
#define free_MUTEX_mol(mu)			do {} while(0)
#define down_mol(x)				down(x)
#define up_mol(x)				up(x)


/* spinlocks */

typedef spinlock_t				mol_spinlock_t;
#define	spin_lock_mol(x)			spin_lock(x)
#define	spin_unlock_mol(x)			spin_unlock(x)
//#define spin_lock_irqsave_mol(x, flags)	spin_lock_irqsave(x, flags)
//#define spin_unlock_irqrestore_mol(x,flags)	spin_unlock_irqrestore(x, flags)
#define spin_lock_init_mol(x)			spin_lock_init(x)


#endif   /* _H_LOCKS */
