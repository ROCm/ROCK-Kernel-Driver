/*
 * SN1 Platform specific error Support
 *
 * Copyright (C) 2001-2002 Silicon Graphics, Inc. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/ptrace.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/smp.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn1/bedrock.h>
#include <asm/sn/intr.h>
#include <asm/sn/addrs.h>

/**
 * snia_error_intr_handler - handle SN specific error interrupts
 * @irq: error interrupt received
 * @devid: device causing the interrupt
 * @pt_regs: saved register state
 *
 * This routine is called when certain interrupts occur on SN systems.
 * It will either recover from the situations that caused the interrupt
 * or panic.
 */
void
snia_error_intr_handler(int irq, void *devid, struct pt_regs *pt_regs)
{
	unsigned long long intpend_val;
	unsigned long long bit;

	switch (irq) {
	case SGI_UART_IRQ:
		/*
		 * This isn't really an error interrupt.  We're just
		 * here because we have to do something with them.
		 * This is probably wrong, and this code will be
		 * removed.
		 */
		intpend_val = LOCAL_HUB_L(PI_INT_PEND0);
		if ( (bit = ~(1L<<GFX_INTR_A)) ==
			(intpend_val & ~(1L<<GFX_INTR_A)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			return;
		}
		if ( (bit = ~(1L<<GFX_INTR_B)) ==
			(intpend_val & ~(1L<<GFX_INTR_B)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			return;
		}
		if ( (bit = ~(1L<<PG_MIG_INTR)) ==
			(intpend_val & ~(1L<<PG_MIG_INTR)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			return;
		}
		if ( (bit = ~(1L<<UART_INTR)) ==
			(intpend_val & ~(1L<<UART_INTR)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			return;
		}
		if ( (bit = ~(1L<<CC_PEND_A)) ==
			(intpend_val & ~(1L<<CC_PEND_A)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			return;
		}
		if ( (bit = ~(1L<<CC_PEND_B)) ==
			(intpend_val & ~(1L<<CC_PEND_B)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			return;
		}
		printk("Received SGI_UART_IRQ (65), but no intpend0 bits were set???\n");
		return;
	case SGI_HUB_ERROR_IRQ:
		/*
		 * These are mostly error interrupts of various
		 * sorts.  We need to do more than panic here, but
		 * what the heck, this is bring up.
		 */
		intpend_val = LOCAL_HUB_L(PI_INT_PEND1);
		
		if ( (bit = ~(1L<<XB_ERROR)) ==
			(intpend_val & ~(1L<<XB_ERROR)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED XB_ERROR on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<LB_ERROR)) ==
			(intpend_val & ~(1L<<LB_ERROR)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED LB_ERROR on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<NACK_INT_A)) ==
			(intpend_val & ~(1L<<NACK_INT_A)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED NACK_INT_A on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<NACK_INT_B)) ==
			(intpend_val & ~(1L<<NACK_INT_B)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED NACK_INT_B on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<CLK_ERR_INTR)) ==
			(intpend_val & ~(1L<<CLK_ERR_INTR)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED CLK_ERR_INTR on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<COR_ERR_INTR_A)) ==
			(intpend_val & ~(1L<<COR_ERR_INTR_A)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED COR_ERR_INTR_A on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<COR_ERR_INTR_B)) ==
			(intpend_val & ~(1L<<COR_ERR_INTR_B)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED COR_ERR_INTR_B on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<MD_COR_ERR_INTR)) ==
			(intpend_val & ~(1L<<MD_COR_ERR_INTR)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED MD_COR_ERR_INTR on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<NI_ERROR_INTR)) ==
			(intpend_val & ~(1L<<NI_ERROR_INTR)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED NI_ERROR_INTR on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		if ( (bit = ~(1L<<MSC_PANIC_INTR)) ==
			(intpend_val & ~(1L<<MSC_PANIC_INTR)) ) {
			LOCAL_HUB_CLR_INTR(bit);
			panic("RECEIVED MSC_PANIC_INTR on cpu %d, cnode %d\n",
				smp_processor_id(), 
				cpuid_to_cnodeid(smp_processor_id()));
		}
		printk("Received SGI_XB_ERROR_IRQ (182) but no intpend1 bits are set???\n");
		return;
	default:
		printk("Received invalid irq in snia_error_intr_handler()\n");
	}
}
