/*
 * tc-init: We assume the TURBOchannel to be up and running so
 * just probe for Modules and fill in the global data structure
 * tc_bus.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) Harald Koerfgen, 1998
 */
#include <linux/string.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/addrspace.h>
#include <asm/errno.h>
#include <asm/dec/machtype.h>
#include <asm/dec/tcinfo.h>
#include <asm/dec/tcmodule.h>
#include <asm/dec/interrupts.h>

#include <asm/ptrace.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define TC_DEBUG

MODULE_LICENSE("GPL");
slot_info tc_bus[MAX_SLOT];
static int max_tcslot;
static tcinfo *info;

unsigned long system_base;

extern void (*dbe_board_handler)(struct pt_regs *regs);
extern unsigned long *(*rex_slot_address)(int);
extern void *(*rex_gettcinfo)(void);

/*
 * Interface to the world. Read comment in include/asm-mips/tc.h.
 */

int search_tc_card(char *name)
{
	int slot;
	slot_info *sip;

	for (slot = 0; slot <= max_tcslot; slot++) {
		sip = &tc_bus[slot];
		if ((sip->flags & FREE) && (strncmp(sip->name, name, strlen(name)) == 0)) {
			return slot;
		}
	}

	return -ENODEV;
}

void claim_tc_card(int slot)
{
	if (tc_bus[slot].flags & IN_USE) {
		printk("claim_tc_card: attempting to claim a card already in use\n");
		return;
	}
	tc_bus[slot].flags &= ~FREE;
	tc_bus[slot].flags |= IN_USE;
}

void release_tc_card(int slot)
{
	if (tc_bus[slot].flags & FREE) {
		printk("release_tc_card: attempting to release a card already free\n");
		return;
	}
	tc_bus[slot].flags &= ~IN_USE;
	tc_bus[slot].flags |= FREE;
}

unsigned long get_tc_base_addr(int slot)
{
	return tc_bus[slot].base_addr;
}

unsigned long get_tc_irq_nr(int slot)
{
	return tc_bus[slot].interrupt;
}

unsigned long get_tc_speed(void)
{
	return 100000 * (10000 / (unsigned long)info->clk_period);
}

/*
 * Probing for TURBOchannel modules
 */
static void __init my_dbe_handler(struct pt_regs *regs)
{
	regs->cp0_epc += 4;
}

static void __init tc_probe(unsigned long startaddr, unsigned long size, int max_slot)
{
	int i, slot;
	long offset;
	unsigned char *module;
	void (*old_be_handler)(struct pt_regs *regs);

	/* Install our exception handler temporarily */

	old_be_handler = dbe_board_handler;
	dbe_board_handler = my_dbe_handler;
	for (slot = 0; slot <= max_slot; slot++) {
		module = (char *)(startaddr + slot * size);
		offset = -1;
		if (module[OLDCARD + TC_PATTERN0] == 0x55 && module[OLDCARD + TC_PATTERN1] == 0x00
		  && module[OLDCARD + TC_PATTERN2] == 0xaa && module[OLDCARD + TC_PATTERN3] == 0xff)
			offset = OLDCARD;
		if (module[TC_PATTERN0] == 0x55 && module[TC_PATTERN1] == 0x00
		  && module[TC_PATTERN2] == 0xaa && module[TC_PATTERN3] == 0xff)
			offset = 0;

		if (offset != -1) {
			tc_bus[slot].base_addr = (unsigned long)module;
			for(i = 0; i < 8; i++) {
				tc_bus[slot].firmware[i] = module[TC_FIRM_VER + offset + 4 * i];
				tc_bus[slot].vendor[i] = module[TC_VENDOR + offset + 4 * i];
				tc_bus[slot].name[i] = module[TC_MODULE + offset + 4 * i];
			}
			tc_bus[slot].firmware[8] = 0;
			tc_bus[slot].vendor[8] = 0;
			tc_bus[slot].name[8] = 0;
			/*
			 * Looks unneccesary, but we may change
			 * TC? in the future
			 */
			switch (slot) {
			case 0:
				tc_bus[slot].interrupt = TC0;
				break;
			case 1:
				tc_bus[slot].interrupt = TC1;
				break;
			case 2:
				tc_bus[slot].interrupt = TC2;
				break;
			/*
			 * Yuck! DS5000/200 onboard devices
			 */
			case 5:
				tc_bus[slot].interrupt = SCSI_INT;
				break;
			case 6:
				tc_bus[slot].interrupt = ETHER;
				break;
			default:
				tc_bus[slot].interrupt = -1;
				break;
			}	
		}
	}

	dbe_board_handler = old_be_handler;
}

/*
 * the main entry
 */
void __init tc_init(void)
{
	int tc_clock;
	int i;
	unsigned long slot0addr;
	unsigned long slot_size;

	if (!TURBOCHANNEL)
		return;

	for (i = 0; i < MAX_SLOT; i++) {
		tc_bus[i].base_addr = 0;
		tc_bus[i].name[0] = 0;
		tc_bus[i].vendor[0] = 0;
		tc_bus[i].firmware[0] = 0;
		tc_bus[i].interrupt = -1;
		tc_bus[i].flags = FREE;
	}

	info = (tcinfo *) rex_gettcinfo();
	slot0addr = (unsigned long)KSEG1ADDR(rex_slot_address(0));

	switch (mips_machtype) {
	case MACH_DS5000_200:
		max_tcslot = 6;
		break;
	case MACH_DS5000_1XX:
	case MACH_DS5000_2X0:
		max_tcslot = 2;
		break;
	case MACH_DS5000_XX:
	default:
		max_tcslot = 1;
		break;
	}

	tc_clock = 10000 / info->clk_period;

	if (TURBOCHANNEL && info->slot_size && slot0addr) {
		printk("TURBOchannel rev. %1d at %2d.%1d MHz ", info->revision,
			tc_clock / 10, tc_clock % 10);
		printk("(with%s parity)\n", info->parity ? "" : "out");

		slot_size = info->slot_size << 20;

		tc_probe(slot0addr, slot_size, max_tcslot);

  		/*
  		 * All TURBOchannel DECstations have the onboard devices
 		 * where the (max_tcslot + 1 or 2 on DS5k/xx) Option Module
 		 * would be.
 		 */
 		if(mips_machtype == MACH_DS5000_XX)
 			i = 2;
		else
 			i = 1;
 		
 	        system_base = slot0addr + slot_size * (max_tcslot + i);

#ifdef TC_DEBUG
		for (i = 0; i <= max_tcslot; i++)
			if (tc_bus[i].base_addr) {
				printk("    slot %d: ", i);
				printk("%s %s %s\n", tc_bus[i].vendor,
					tc_bus[i].name, tc_bus[i].firmware);
			}
#endif
		ioport_resource.end = KSEG2 - 1;
	}
}

EXPORT_SYMBOL(search_tc_card);
EXPORT_SYMBOL(claim_tc_card);
EXPORT_SYMBOL(release_tc_card);
EXPORT_SYMBOL(get_tc_base_addr);
EXPORT_SYMBOL(get_tc_irq_nr);
EXPORT_SYMBOL(get_tc_speed);

