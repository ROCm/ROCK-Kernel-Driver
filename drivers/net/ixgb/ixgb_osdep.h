/*******************************************************************************

  
  Copyright(c) 1999 - 2003 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  Linux NICS <linux.nics@intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
*******************************************************************************/

/* glue for the OS independant part of ixgb 
 * includes register access macros
 */

#ifndef IXGB_OSDEP_H
#define IXGB_OSDEP_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

/* FIXME: eliminate me */
#define msec_delay(x)	do { if(in_interrupt()) { \
	                	mdelay(x); \
			} else { \
				set_current_state(TASK_UNINTERRUPTIBLE); \
				schedule_timeout((x * HZ)/1000); \
			} } while(0)

typedef enum {
	FALSE = 0,
	TRUE = 1
} boolean_t;

#define MSGOUT(S, A, B)	printk(KERN_DEBUG S "\n", A, B)

#if DBG
#define ASSERT(x)	if(!(x)) BUG()
#define DEBUGOUT(S)		printk(KERN_ERR S "\n")
#define DEBUGOUT1(S, A...)	printk(KERN_ERR S "\n", A)
#else
#define ASSERT(x)
#define DEBUGOUT(S)
#define DEBUGOUT1(S, A...)
#endif

#define DEBUGOUT2 DEBUGOUT1
#define DEBUGOUT3 DEBUGOUT1
#define DEBUGOUT7 DEBUGOUT1
#define DEBUGFUNC(F)        DEBUGOUT(F)

#define IXGB_WRITE_REG(a, reg, value) ( \
    writel((value), ((a)->hw_addr + IXGB_##reg)))

#define IXGB_READ_REG(a, reg) ( \
    readl((a)->hw_addr + IXGB_##reg))

#define IXGB_WRITE_REG_ARRAY(a, reg, offset, value) ( \
    writel((value), ((a)->hw_addr + IXGB_##reg + ((offset) << 2))))

#define IXGB_READ_REG_ARRAY(a, reg, offset) ( \
    readl((a)->hw_addr + IXGB_##reg + ((offset) << 2)))

#endif				/* IXGB_OSDEP_H */
