/*
 * Setup pointers to hardware-dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 2000 by Ralf Baechle
 */
#include <asm/ptrace.h>
#include <linux/config.h>
#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/pci.h>
#include <linux/mc146818rtc.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/pc_keyb.h>
#include <linux/ide.h>

#include <asm/bcache.h>
#include <asm/bootinfo.h>
#include <asm/keyboard.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/sni.h>

/*
 * Initial irq handlers.
 */
static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL};

extern asmlinkage void sni_rm200_pci_handle_int(void);

extern void sni_machine_restart(char *command);
extern void sni_machine_halt(void);
extern void sni_machine_power_off(void);

extern struct ide_ops std_ide_ops;
extern struct rtc_ops std_rtc_ops;
extern struct kbd_ops std_kbd_ops;

static void __init sni_irq_setup(void)
{
	set_except_vector(0, sni_rm200_pci_handle_int);
	request_region(0x20,0x20, "pic1");
	request_region(0xa0,0x20, "pic2");	
	i8259_setup_irq(2, &irq2);
	/*
	 * IRQ0 seems to be the irq for PC style stuff.
	 * I don't know how to handle the debug button interrupt, so
	 * don't use this button yet or bad things happen ...
	 */
	set_cp0_status(ST0_IM, IE_IRQ1 | IE_IRQ3 | IE_IRQ4);
}

void (*board_time_init)(struct irqaction *irq);

static void __init sni_rm200_pci_time_init(struct irqaction *irq)
{
	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	i8259_setup_irq(0, irq);
}

unsigned char aux_device_present;
extern unsigned char sni_map_isa_cache;

/*
 * A bit more gossip about the iron we're running on ...
 */
static inline void sni_pcimt_detect(void)
{
	char boardtype[80];
	unsigned char csmsr;
	char *p = boardtype;
	unsigned int asic;

	csmsr = *(volatile unsigned char *)PCIMT_CSMSR;

	p += sprintf(p, "%s PCI", (csmsr & 0x80) ? "RM200" : "RM300");
	if ((csmsr & 0x80) == 0)
		p += sprintf(p, ", board revision %s",
		             (csmsr & 0x20) ? "D" : "C");
	asic = csmsr & 0x80;
	asic = (csmsr & 0x08) ? asic : !asic;
	p += sprintf(p, ", ASIC PCI Rev %s", asic ? "1.0" : "1.1");
	printk("%s.\n", boardtype);
}

int __init page_is_ram(unsigned long pagenr)
{
	return 1;
}

void __init sni_rm200_pci_setup(void)
{
#if 0 /* XXX Tag me deeper  */
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
#endif

	sni_pcimt_detect();
	sni_pcimt_sc_init();

	irq_setup = sni_irq_setup;
	mips_io_port_base = SNI_PORT_BASE;

	/*
	 * Setup (E)ISA I/O memory access stuff
	 */
	isa_slot_offset = 0xb0000000;
	// sni_map_isa_cache = 0;
	EISA_bus = 1;

	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	/* XXX FIXME: CONFIG_RTC */
	request_region(0x70,0x10,"rtc");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
	board_time_init = sni_rm200_pci_time_init;

	_machine_restart = sni_machine_restart;
	_machine_halt = sni_machine_halt;
	_machine_power_off = sni_machine_power_off;

	aux_device_present = 0xaa;

	/*
	 * Some cluefull person has placed the PCI config data directly in
	 * the I/O port space ...
	 */
	request_region(0xcfc,0x04,"PCI config data");

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &std_ide_ops;
#endif
	conswitchp = &vga_con;

	screen_info = (struct screen_info) {
		0, 0,		/* orig-x, orig-y */
		0,		/* unused */
		52,		/* orig_video_page */
		3,		/* orig_video_mode */
		80,		/* orig_video_cols */
		4626, 3, 9,	/* unused, ega_bx, unused */
		50,		/* orig_video_lines */
		0x22,		/* orig_video_isVGA */
		16		/* orig_video_points */
	};

	rtc_ops = &std_rtc_ops;
	kbd_ops = &std_kbd_ops;
#ifdef CONFIG_PSMOUSE
	aux_device_present = 0xaa;
#endif
}
