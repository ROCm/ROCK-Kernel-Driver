/* 
 * dvb_functions.h: isolate some Linux specific stuff from the dvb-core
 *                  that can't be expressed as a one-liner
 *                  in order to make porting to other environments easier
 *
 * Copyright (C) 2003 Convergence GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Lesser Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef __DVB_FUNCTIONS_H__
#define __DVB_FUNCTIONS_H__

/**
 *  a sleeping delay function, waits i ms
 *
 */
static inline
void dvb_delay(int i)
{
	current->state=TASK_INTERRUPTIBLE;
	schedule_timeout((HZ*i)/1000);
}

/* we don't mess with video_usercopy() any more,
we simply define out own dvb_usercopy(), which will hopefull become
generic_usercopy()  someday... */

extern int dvb_usercopy(struct inode *inode, struct file *file,
	                    unsigned int cmd, unsigned long arg,
			    int (*func)(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg));

extern void dvb_kernel_thread_setup (const char *thread_name);

#endif

