/*
 *  Copyright (C) 1996  Linus Torvalds & author (see below)
 */

/*
 * ALI M14xx chipset EIDE controller
 *
 * Works for ALI M1439/1443/1445/1487/1489 chipsets.
 *
 * Adapted from code developed by derekn@vw.ece.cmu.edu.  -ml
 * Derek's notes follow:
 *
 * I think the code should be pretty understandable,
 * but I'll be happy to (try to) answer questions.
 *
 * The critical part is in the ali14xx_tune_drive function.  The init_registers
 * function doesn't seem to be necessary, but the DOS driver does it, so
 * I threw it in.
 *
 * I've only tested this on my system, which only has one disk.  I posted
 * it to comp.sys.linux.hardware, so maybe some other people will try it
 * out.
 *
 * Derek Noonburg  (derekn@ece.cmu.edu)
 * 95-sep-26
 *
 * Update 96-jul-13:
 *
 * I've since upgraded to two disks and a CD-ROM, with no trouble, and
 * I've also heard from several others who have used it successfully.
 * This driver appears to work with both the 1443/1445 and the 1487/1489
 * chipsets.  I've added support for PIO mode 4 for the 1487.  This
 * seems to work just fine on the 1443 also, although I'm not sure it's
 * advertised as supporting mode 4.  (I've been running a WDC AC21200 in
 * mode 4 for a while now with no trouble.)  -Derek
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "timing.h"

/* port addresses for auto-detection */
#define ALI_NUM_PORTS 4
static int ports[ALI_NUM_PORTS] __initdata =
    { 0x074, 0x0f4, 0x034, 0x0e4 };

/* register initialization data */
struct reg_initializer {
	u8 reg, data;
};

static struct reg_initializer init_data[] __initdata = {
	{0x01, 0x0f}, {0x02, 0x00}, {0x03, 0x00}, {0x04, 0x00},
	{0x05, 0x00}, {0x06, 0x00}, {0x07, 0x2b}, {0x0a, 0x0f},
	{0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x28, 0x00},
	{0x29, 0x00}, {0x2a, 0x00}, {0x2f, 0x00}, {0x2b, 0x00},
	{0x2c, 0x00}, {0x2d, 0x00}, {0x2e, 0x00}, {0x30, 0x00},
	{0x31, 0x00}, {0x32, 0x00}, {0x33, 0x00}, {0x34, 0xff},
	{0x35, 0x03}, {0x00, 0x00}
};

/* timing parameter registers for each drive */
static struct {
	u8 reg1, reg2, reg3, reg4;
} reg_tab[4] = {
	{
	0x03, 0x26, 0x04, 0x27},	/* drive 0 */
	{
	0x05, 0x28, 0x06, 0x29},	/* drive 1 */
	{
	0x2b, 0x30, 0x2c, 0x31},	/* drive 2 */
	{
	0x2d, 0x32, 0x2e, 0x33},	/* drive 3 */
};

static int base_port;		/* base port address */
static int reg_port;		/* port for register number */
static int data_port;		/* port for register data */
static u8 reg_on;		/* output to base port to access registers */
static u8 reg_off;		/* output to base port to close registers */

/*
 * Read a controller register.
 */
static inline u8 in_reg(u8 reg)
{
	outb_p(reg, reg_port);
	return inb(data_port);
}

/*
 * Write a controller register.
 */
static inline void out_reg(u8 data, u8 reg)
{
	outb_p(reg, reg_port);
	outb_p(data, data_port);
}

/*
 * Set PIO mode for the specified drive.
 * This function computes timing parameters
 * and sets controller registers accordingly.
 */
static void ali14xx_tune_drive(struct ata_device *drive, u8 pio)
{
	int drive_num;
	int time1, time2;
	u8 param1, param2, param3, param4;
	unsigned long flags;
	struct ata_timing *t;

	if (pio == 255)
		pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		pio = XFER_PIO_0 + min_t(u8, pio, 4);

	t = ata_timing_data(pio);

	/* calculate timing, according to PIO mode */
	time1 = t->cycle;
	time2 = t->active;
	param3 = param1 = (time2 * system_bus_speed + 999999) / 1000000;
	param4 = param2 =
	    (time1 * system_bus_speed + 999999) / 1000000 - param1;
	if (pio < XFER_PIO_3) {
		param3 += 8;
		param4 += 8;
	}
	printk(KERN_DEBUG
	       "%s: PIO mode%d, t1=%dns, t2=%dns, cycles = %d+%d, %d+%d\n",
	       drive->name, pio - XFER_PIO_0, time1, time2, param1, param2,
	       param3, param4);

	/* stuff timing parameters into controller registers */
	drive_num = (drive->channel->index << 1) + drive->select.b.unit;
	save_flags(flags);	/* all CPUs */
	cli();			/* all CPUs */
	outb_p(reg_on, base_port);
	out_reg(param1, reg_tab[drive_num].reg1);
	out_reg(param2, reg_tab[drive_num].reg2);
	out_reg(param3, reg_tab[drive_num].reg3);
	out_reg(param4, reg_tab[drive_num].reg4);
	outb_p(reg_off, base_port);
	restore_flags(flags);	/* all CPUs */
}

/*
 * Auto-detect the IDE controller port.
 */
static int __init find_port(void)
{
	int i;
	unsigned long flags;

	local_irq_save(flags);
	for (i = 0; i < ALI_NUM_PORTS; i++) {
		base_port = ports[i];
		reg_off = inb(base_port);
		for (reg_on = 0x30; reg_on <= 0x33; reg_on++) {
			outb_p(reg_on, base_port);
			if (inb(base_port) == reg_on) {
				u8 t;
				reg_port = base_port + 4;
				data_port = base_port + 8;
				t = in_reg(0) & 0xf0;
				outb_p(reg_off, base_port);
				local_irq_restore(flags);
				if (t != 0x50)
					return 0;
				return 1;	/* success */
			}
		}
		outb_p(reg_off, base_port);
	}
	local_irq_restore(flags);

	return 0;
}

/*
 * Initialize controller registers with default values.
 */
static int __init init_registers(void)
{
	struct reg_initializer *p;
	unsigned long flags;
	u8 t;

	local_irq_save(flags);
	outb_p(reg_on, base_port);
	for (p = init_data; p->reg != 0; ++p)
		out_reg(p->data, p->reg);
	outb_p(0x01, reg_port);
	t = inb(reg_port) & 0x01;
	outb_p(reg_off, base_port);
	local_irq_restore(flags);

	return t;
}

void __init init_ali14xx(void)
{
	/* auto-detect IDE controller port */
	if (!find_port()) {
		printk(KERN_ERR "ali14xx: not found\n");
		return;
	}

	printk(KERN_DEBUG "ali14xx: base=%#03x, reg_on=%#02x\n",
	       base_port, reg_on);
	ide_hwifs[0].chipset = ide_ali14xx;
	ide_hwifs[1].chipset = ide_ali14xx;
	ide_hwifs[0].tuneproc = &ali14xx_tune_drive;
	ide_hwifs[1].tuneproc = &ali14xx_tune_drive;
	ide_hwifs[0].unit = ATA_PRIMARY;
	ide_hwifs[1].unit = ATA_SECONDARY;

	/* initialize controller registers */
	if (!init_registers()) {
		printk(KERN_ERR "ali14xx: Chip initialization failed\n");
		return;
	}
}
