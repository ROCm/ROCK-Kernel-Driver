/*
 * Setup pointers to hardware dependand routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997 by Ralf Baechle
 */
#include <linux/bios32.h>
#include <linux/config.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/timex.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/cobalt.h>
#include <asm/pci.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/vector.h>

extern void cobalt_machine_restart(char *command);
extern void cobalt_machine_halt(void);
extern void cobalt_machine_power_off(void);

extern int serial_console;

/*
 * Initial irq handlers.
 */
static void no_action(int cpl, void *dev_id, struct pt_regs *regs)
{ 
}

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL};

extern struct feature cobalt_feature;
extern asmlinkage void cobalt_handle_int(void);

static void cobalt_irq_setup(void) 
{
	/*
	 * Clear all of the interrupts while we change the able around a bit.
	 */
 	set_cp0_status(ST0_IM, 0);

	/* Sets the exception_handler array. */
	set_except_vector(0, cobalt_handle_int);

	request_region(0xb0000020, 0x20, "pic1");
	request_region(0xb00000A0, 0x20, "pic2");
	setup_x86_irq(2, &irq2);

	cli();

	set_cp0_status(ST0_IM, IE_IRQ4 | IE_IRQ3 | IE_IRQ2 | IE_IRQ1 | IE_IRQ0);

	/* Setup VIA irq mask */
	VIA_PORT_WRITE(0x20, 0x10);
	VIA_PORT_WRITE(0x21, 0x00);
	VIA_PORT_WRITE(0x21, 0x00);

	VIA_PORT_WRITE(0xa0, 0x10);
	VIA_PORT_WRITE(0xa1, 0x00);
	VIA_PORT_WRITE(0xa1, 0x00);
}

void (*board_time_init)(struct irqaction *irq);

static void cobalt_calibrate_timer(void)
{
	volatile unsigned long *timer_reg = (volatile unsigned long *)0xb4000850;

	/* Default to 150MHZ, since this is what we are shipping. */
	*timer_reg = 500000;
}

static void cobalt_time_init(struct irqaction *irq)
{
	/* Load timer value for 100 Hz */
	cobalt_calibrate_timer();
	/* *((volatile unsigned long *) 0xb4000850) = (unsigned long) 440000; */

	setup_x86_irq(0, irq);

	/* Enable timer ints */
	*((volatile unsigned long *) 0xb4000864) = (unsigned long) 0x00000003;
	/* Unmask timer int */
	*((volatile unsigned long *) 0xb4000c1c) = (unsigned long) 0x00000100; 
}

extern struct pci_ops qube_pci_ops;
int cobalt_serial_present;
int cobalt_serial_type;
int cobalt_is_raq;

void cobalt_setup(void)
{
	tag *atag;

	/*
	 * We just check if a tag_screen_info can be gathered
	 * in setup_arch(), if yes we don't proceed futher...
	 */
	atag = bi_TagFind(tag_screen_info);
	if (!atag) {
		/*
		 * If no, we try to find the tag_arc_displayinfo which is
		 * always created by Milo for an ARC box (for now Milo only
		 * works on ARC boxes :) -Stoned.
		 */
		atag = bi_TagFind(tag_arcdisplayinfo);
		if (atag) {

			screen_info.orig_x = 
				((mips_arc_DisplayInfo*)TAGVALPTR(atag))->cursor_x;
			screen_info.orig_y = 
				((mips_arc_DisplayInfo*)TAGVALPTR(atag))->cursor_y;
			screen_info.orig_video_cols  = 
				((mips_arc_DisplayInfo*)TAGVALPTR(atag))->columns;
			screen_info.orig_video_lines  = 
				((mips_arc_DisplayInfo*)TAGVALPTR(atag))->lines;
		}
	}

	_machine_restart = cobalt_machine_restart;
	_machine_halt = cobalt_machine_halt;
	_machine_power_off = cobalt_machine_power_off;

	irq_setup = cobalt_irq_setup;
	board_time_init = cobalt_time_init;
	feature = &cobalt_feature;
	mips_io_port_base = COBALT_LOCAL_IO_SPACE;

	pci_ops = &qube_pci_ops;

#ifdef CONFIG_COBALT_SERIAL
	serial_console = 1;
#endif /* CONFIG_COBALT_SERIAL */

	/* We have to do this early, here, before the value could
	 * possibly be overwritten by the bootup sequence.
	 */
	cobalt_serial_present = *((unsigned long *) 0xa020001c);
	cobalt_serial_type    = *((unsigned long *) 0xa0200020);
	cobalt_is_raq         = (cobalt_serial_present != 0x0
				 && cobalt_serial_type == 0x1);
}

void	AddTagsEndSymbol(void);
int bi_TagAdd (enum bi_tag type, unsigned long size, void *data);
unsigned long next_tag = (unsigned long) NULL;
unsigned long memory_upper = (unsigned long) NULL;

int bi_TagAdd (enum bi_tag type, unsigned long size, void *data)
{
	tag t;
	unsigned long addr;

	t.tag = type;
	t.size = size;

	/*
	 * If next_tag equals NULL it means it's the first tag we're asked
	 * to create.
	 */
	if (next_tag == (unsigned long) NULL)  {
		/*
		 * If memory_upper not equals NULL it means that identify()
		 * was able to figure out how much memory is present in the
		 * box so we initialize next_tag from it.
		 */
		if (memory_upper != (unsigned long) NULL)
			next_tag = memory_upper;

		/* Else we rely on the fact that the first tag we create for
		 * a box for which we don't know how RAM it gots is a tag of
		 * type tag_memupper.  This is ensured by the first entry in
		 * the defaults tag list for such a box (see identify.c).
		 * First we check this.
		 */
		else {
			/* Ok it's a memupper tag: we put it's value in 
			 * memory_upper so launch() can pass it to the
			 * kernel in register a0 and we initialize next_tag.
			 */
 			next_tag = *(unsigned long *) data;
			memory_upper = *((unsigned long *) data);
		}
	}

	/* We put the tag structure.  */
	addr = next_tag - (sizeof (tag));

	memcpy ((void *) addr, (void *) &t, (size_t) (sizeof (tag)));

	/* We put the tag's data if any.  */
	if (size != 0) {
		addr = addr - size;
		memcpy ((void *) addr, data, (size_t) (t.size));
	}

	/*
	 * Set next_tag ready for the next tag creation.
	 */
	next_tag = addr;
	AddTagsEndSymbol();

	return 0;
}

void SetUpBootInfo(void)
{
	unsigned long LongVar;
	int atag;

	/*
	 * This is hard coded here but will change when we have a
	 * Size mem routine.
	 */

	/*
	 * 64mb of memory.
	 */

 	//mips_memory_upper = 0x84000000;


#ifndef BOOTLOADER

#if 0	/* You break'a the kernel I break'a ya face. -DaveM */

	/* Eight meg of memory.         */
	mips_memory_upper = 0x80800000;  /* XXX  this appears to be unused  - 
                                           this assignment is not present in the normal
                                           $cvstree/linux/arch/mips/cobalt/setup.c */
#endif
#else
#include "../../../../include/diagdefs.h"
	mips_memory_upper = (unsigned long) kBootloaderMipsMemoryUpper;
#endif
 
	LongVar = mips_memory_upper;
	atag = bi_TagAdd(tag_memupper, ULONGSIZE, &LongVar);

	/* Here is the machine type.
	*/
	LongVar = MACH_COBALT_27;
	atag = bi_TagAdd(tag_machtype, ULONGSIZE, &LongVar);

	LongVar = 0x80000000;
	atag = bi_TagAdd(tag_memlower, ULONGSIZE, &LongVar);

	LongVar = CPU_R4300;
	atag = bi_TagAdd(tag_cputype, ULONGSIZE, &LongVar);

	LongVar = MACH_GROUP_COBALT;
	atag = bi_TagAdd(tag_machgroup, ULONGSIZE, &LongVar);

	LongVar = 0;
	atag = bi_TagAdd(tag_scache_size, ULONGSIZE, &LongVar);

	LongVar = 48;
	atag = bi_TagAdd(tag_tlb_entries, ULONGSIZE, &LongVar);

 	LongVar = 0;
	atag = bi_TagAdd(tag_drive_info, ULONGSIZE, &LongVar);

	LongVar = 0xe0800000;
	atag = bi_TagAdd(tag_vram_base, ULONGSIZE, &LongVar);

	LongVar = 0;
	atag = bi_TagAdd(tag_dummy, 0, &LongVar);
}

void AddTagsEndSymbol(void)
{
	short	X;
	X = 1;
}

/*
 * Oh shit, this is so crappy ...
 */
#include <linux/mm.h>
#include <asm/pgtable.h>
int my_cacheflush(unsigned long start, unsigned long size, unsigned int what)
{
	flush_cache_range(current->mm, start, start + size);
	return 0;
}


/*
 * The ROM set the flag to 0x1 to turn off output.  This is interpreted
 * as "valid", "no kernel output", and index '0' into the following
 * tables.  We overload entry '0' to specify a sensible default rate.
 */
static int cons_baud_int[]  = { 9600, 0,  9600, 0, 0,  115200, 0, 0 };
static int cons_baud_baud[] = { B9600, 0, B9600, 0, 0, B115200, 0, 0 };

static union cobalt_cons_info
cobalt_get_console_info(void)
{
	char board_id = 0;
	static union cobalt_cons_info cons_info;
	static int read_info = 0;
	extern void add_init_env(char *);

	if (read_info)
		return cons_info;

	VIA_PORT_WRITE(VIA_CMOS_ADDR, VIA_CMOS_CONSOLE_FLG);
	cons_info.ccons_char = VIA_PORT_READ(VIA_CMOS_DATA);

#if 0
	printk("cobalt_get_console_info: read 0x%x from console flag\n",
	       cons_info.ccons_char);
#endif

	/*
	 * CMOS hasn't been initialized or baud rate isn't known.
	 * - read the board id and provide backwards compat.
	 */
	if (cons_info.ccons_bits.valid != VIA_CMOS_CONS_VALID
	    || ! cons_baud_int[cons_info.ccons_bits.baud]
	    || ! cons_baud_baud[cons_info.ccons_bits.baud]) {
		pcibios_read_config_byte(0, PCI_DEVSHFT(COBALT_PCICONF_VIA),
	                                 VIA_COBALT_BRD_ID_REG, &board_id);

#if 0
		printk("cobalt_get_console_info: read 0x%x from board config\n",
		       board_id);
#endif

		switch (VIA_COBALT_BRD_REG_to_ID(board_id)) {
		case COBALT_BRD_ID_QUBE1:
			cons_info.ccons_bits.baud = VIA_CMOS_CONS_115K;
			cons_info.ccons_bits.kout = 1;
			break;
		case COBALT_BRD_ID_RAQ1:
			cons_info.ccons_bits.baud = VIA_CMOS_CONS_9600;
			cons_info.ccons_bits.kout = 1;
			break;
		case COBALT_BRD_ID_QUBE2:
			cons_info.ccons_bits.baud = VIA_CMOS_CONS_9600;
			cons_info.ccons_bits.kout = 0;
			break;
		case COBALT_BRD_ID_RAQ2:
			cons_info.ccons_bits.baud = VIA_CMOS_CONS_9600;
			cons_info.ccons_bits.kout = 1;
			break;
		}
		cons_info.ccons_bits.valid = VIA_CMOS_CONS_VALID;
	}

	read_info = 1;

#if defined(DEBUG_LOADER)
	cons_info.ccons_bits.kout = 1;
#endif

	if (!cons_info.ccons_bits.kout) {
		add_init_env("CONSOLE=/dev/null");
	}

#if 0
	printk("cobalt_get_console_info: returning 0x%x\n",
		cons_info.ccons_char);
#endif

	return cons_info;
}

int
cobalt_cons_koutok(void)
{
	union cobalt_cons_info cons;

	cons = cobalt_get_console_info();

	return cons.ccons_bits.kout;
}

int
cobalt_cons_baudbaud(void)
{
	union cobalt_cons_info cons;

	cons = cobalt_get_console_info();

	return cons_baud_baud[cons.ccons_bits.baud];
}

int
cobalt_cons_baudint(void)
{
	union cobalt_cons_info cons;

	cons = cobalt_get_console_info();

	return cons_baud_int[cons.ccons_bits.baud];
}
