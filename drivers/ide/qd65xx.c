/*
 *  Copyright (C) 1996-2001  Linus Torvalds & author (see below)
 *
 *  Version 0.03	Cleaned auto-tune, added probe
 *  Version 0.04	Added second channel tuning
 *  Version 0.05	Enhanced tuning ; added qd6500 support
 *  Version 0.06	Added dos driver's list
 *  Version 0.07	Second channel bug fix
 *
 * QDI QD6500/QD6580 EIDE controller fast support
 *
 * Please set local bus speed using kernel parameter idebus
 *	for example, "idebus=33" stands for 33Mhz VLbus
 * To activate controller support, use "ide0=qd65xx"
 * To enable tuning, use "ide0=autotune"
 * To enable second channel tuning (qd6580 only), use "ide1=autotune"
 *
 * Rewritten from the work of Colten Edwards <pje120@cs.usask.ca> by
 * Samuel Thibault <samuel.thibault@fnac.net>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "timing.h"
#include "qd65xx.h"

/*
 * I/O ports are 0x30-0x31 (and 0x32-0x33 for qd6580)
 *            or 0xb0-0xb1 (and 0xb2-0xb3 for qd6580)
 *	-- qd6500 is a single IDE interface
 *	-- qd6580 is a dual IDE interface
 *
 * More research on qd6580 being done by willmore@cig.mot.com (David)
 * More Information given by Petr Soucek (petr@ryston.cz)
 * http://www.ryston.cz/petr/vlb
 */

/*
 * base: Timer1
 *
 *
 * base+0x01: Config (R/O)
 *
 * bit 0: ide baseport: 1 = 0x1f0 ; 0 = 0x170 (only useful for qd6500)
 * bit 1: qd65xx baseport: 1 = 0xb0 ; 0 = 0x30
 * bit 2: ID3: bus speed: 1 = <=33MHz ; 0 = >33MHz
 * bit 3: qd6500: 1 = disabled, 0 = enabled
 *        qd6580: 1
 * upper nibble:
 *        qd6500: 1100
 *        qd6580: either 1010 or 0101
 *
 *
 * base+0x02: Timer2 (qd6580 only)
 *
 *
 * base+0x03: Control (qd6580 only)
 *
 * bits 0-3 must always be set 1
 * bit 4 must be set 1, but is set 0 by dos driver while measuring vlb clock
 * bit 0 : 1 = Only primary port enabled : channel 0 for hda, channel 1 for hdb
 *         0 = Primary and Secondary ports enabled : channel 0 for hda & hdb
 *                                                   channel 1 for hdc & hdd
 * bit 1 : 1 = only disks on primary port
 *         0 = disks & ATAPI devices on primary port
 * bit 2-4 : always 0
 * bit 5 : status, but of what ?
 * bit 6 : always set 1 by dos driver
 * bit 7 : set 1 for non-ATAPI devices on primary port
 *	(maybe read-ahead and post-write buffer ?)
 */

static int timings[4]={-1,-1,-1,-1}; /* stores current timing for each timer */

/*
 * This routine is invoked from ide.c to prepare for access to a given drive.
 */

static void qd_select(struct ata_device *drive)
{
	u8 index = (((QD_TIMREG(drive)) & 0x80 ) >> 7) |
		(QD_TIMREG(drive) & 0x02);

	if (timings[index] != QD_TIMING(drive))
		outb(timings[index] = QD_TIMING(drive), QD_TIMREG(drive));
}

/*
 * computes the timing value where
 *	lower nibble represents active time,   in count of VLB clocks
 *	upper nibble represents recovery time, in count of VLB clocks
 */

static u8 qd6500_compute_timing(struct ata_channel *hwif, int active_time, int recovery_time)
{
	u8 active_cycle,recovery_cycle;

	if (system_bus_speed <= 33333) {
		active_cycle =   9  - IDE_IN(active_time   * system_bus_speed / 1000000 + 1, 2, 9);
		recovery_cycle = 15 - IDE_IN(recovery_time * system_bus_speed / 1000000 + 1, 0, 15);
	} else {
		active_cycle =   8  - IDE_IN(active_time   * system_bus_speed / 1000000 + 1, 1, 8);
		recovery_cycle = 18 - IDE_IN(recovery_time * system_bus_speed / 1000000 + 1, 3, 18);
	}

	return((recovery_cycle<<4) | 0x08 | active_cycle);
}

/*
 * idem for qd6580
 */

static u8 qd6580_compute_timing(int active_time, int recovery_time)
{
	u8 active_cycle   = 17 - IDE_IN(active_time   * system_bus_speed / 1000000 + 1, 2, 17);
	u8 recovery_cycle = 15 - IDE_IN(recovery_time * system_bus_speed / 1000000 + 1, 2, 15);

	return (recovery_cycle<<4) | active_cycle;
}

/*
 * tries to find timing from dos driver's table
 */

static int qd_find_disk_type(struct ata_device *drive,
		int *active_time, int *recovery_time)
{
	struct qd65xx_timing_s *p;
	char model[40];

	if (!*drive->id->model) return 0;

	strncpy(model,drive->id->model, 40);
	for (p = qd65xx_timing ; p->offset != -1 ; p++) {
		if (!strncmp(p->model, model+p->offset, 4)) {
			printk(KERN_DEBUG "%s: listed !\n", drive->name);
			*active_time = p->active;
			*recovery_time = p->recovery;
			return 1;
		}
	}
	return 0;
}

/*
 * check whether timings don't conflict
 */

static int qd_timing_ok(struct ata_device drives[])
{
	return (IDE_IMPLY(drives[0].present && drives[1].present,
			IDE_IMPLY(QD_TIMREG(drives) == QD_TIMREG(drives+1),
			          QD_TIMING(drives) == QD_TIMING(drives+1))));
	/* if same timing register, must be same timing */
}

/*
 * records the timing, and enables selectproc as needed
 */

static void qd_set_timing(struct ata_device *drive, u8 timing)
{
	struct ata_channel *hwif = drive->channel;

	drive->drive_data &= 0xff00;
	drive->drive_data |= timing;
	if (qd_timing_ok(hwif->drives)) {
		qd_select(drive); /* selects once */
		hwif->selectproc = NULL;
	} else
		hwif->selectproc = &qd_select;

	printk(KERN_DEBUG "%s: %#x\n", drive->name, timing);
}

static void qd6500_tune_drive(struct ata_device *drive, u8 pio)
{
	int active_time   = 175;
	int recovery_time = 415; /* worst case values from the dos driver */

	if (drive->id && !qd_find_disk_type(drive, &active_time, &recovery_time)
		&& drive->id->tPIO && (drive->id->field_valid & 0x02)
		&& drive->id->eide_pio >= 240) {

		printk(KERN_INFO "%s: PIO mode%d\n", drive->name, drive->id->tPIO);
		active_time = 110;
		recovery_time = drive->id->eide_pio - 120;
	}

	qd_set_timing(drive, qd6500_compute_timing(drive->channel, active_time, recovery_time));
}

static void qd6580_tune_drive(struct ata_device *drive, u8 pio)
{
	struct ata_timing *t;
	int base = drive->channel->select_data;
	int active_time   = 175;
	int recovery_time = 415; /* worst case values from the dos driver */

	if (drive->id && !qd_find_disk_type(drive, &active_time, &recovery_time)) {

		if (pio == 255)
			pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
		else
			pio = XFER_PIO_0 + min_t(u8, pio, 4);

		t = ata_timing_data(pio);

		switch (pio) {
			case 0: break;
			case 3:
				if (t->cycle >= 110) {
					active_time = 86;
					recovery_time = t->cycle - 102;
				} else
					printk(KERN_WARNING "%s: Strange recovery time !\n",drive->name);
				break;
			case 4:
				if (t->cycle >= 69) {
					active_time = 70;
					recovery_time = t->cycle - 61;
				} else
					printk(KERN_WARNING "%s: Strange recovery time !\n",drive->name);
				break;
			default:
				if (t->cycle >= 180) {
					active_time = 110;
					recovery_time = t->cycle - 120;
				} else {
					active_time = t->active;
					recovery_time = t->cycle - active_time;
				}
		}
		printk(KERN_INFO "%s: PIO mode%d\n", drive->name, pio - XFER_PIO_0);
	}

	if (!drive->channel->unit && drive->type != ATA_DISK) {
		outb(0x5f, QD_CONTROL_PORT);
		printk(KERN_WARNING "%s: ATAPI: disabled read-ahead FIFO and post-write buffer on %s.\n", drive->name, drive->channel->name);
	}

	qd_set_timing(drive, qd6580_compute_timing(active_time, recovery_time));
}

/*
 * tests if the given port is a register
 */

static int __init qd_testreg(int port)
{
	u8 savereg;
	u8 readreg;
	unsigned long flags;

	save_flags(flags);	/* all CPUs */
	cli();			/* all CPUs */
	savereg = inb_p(port);
	outb_p(QD_TESTVAL, port);	/* safe value */
	readreg = inb_p(port);
	outb(savereg, port);
	restore_flags(flags);	/* all CPUs */

	if (savereg == QD_TESTVAL) {
		printk(KERN_ERR "Outch ! the probe for qd65xx isn't reliable !\n");
		printk(KERN_ERR "Please contact maintainers to tell about your hardware\n");
		printk(KERN_ERR "Assuming qd65xx is not present.\n");
		return 1;
	}

	return (readreg != QD_TESTVAL);
}

/*
 * called to setup an ata channel : adjusts attributes & links for tuning
 */

void __init qd_setup(int unit, int base, int config, unsigned int data0, unsigned int data1, void (*tuneproc) (struct ata_device *, u8 pio))
{
	struct ata_channel *hwif = &ide_hwifs[unit];

	hwif->chipset = ide_qd65xx;
	hwif->unit = unit;
	hwif->select_data = base;
	hwif->config_data = config;
	hwif->drives[0].drive_data = data0;
	hwif->drives[1].drive_data = data1;
	hwif->io_32bit = 1;
	hwif->tuneproc = tuneproc;
}

/*
 * called to unsetup an ata channel : back to default values, unlinks tuning
 */
void __init qd_unsetup(int unit) {
	struct ata_channel *hwif = &ide_hwifs[unit];
	u8 config = hwif->config_data;
	int base = hwif->select_data;
	void *tuneproc = (void *) hwif->tuneproc;

	if (!(hwif->chipset == ide_qd65xx)) return;

	printk(KERN_NOTICE "%s: back to defaults\n", hwif->name);

	hwif->selectproc = NULL;
	hwif->tuneproc = NULL;

	if (tuneproc == (void *) qd6500_tune_drive) {
		// will do it for both
		outb(QD6500_DEF_DATA, QD_TIMREG(&hwif->drives[0]));
	} else if (tuneproc == (void *) qd6580_tune_drive) {
		if (QD_CONTROL(hwif) & QD_CONTR_SEC_DISABLED) {
			outb(QD6580_DEF_DATA, QD_TIMREG(&hwif->drives[0]));
			outb(QD6580_DEF_DATA2, QD_TIMREG(&hwif->drives[1]));
		} else {
			outb(unit ? QD6580_DEF_DATA2 : QD6580_DEF_DATA, QD_TIMREG(&hwif->drives[0]));
		}
	} else {
		printk(KERN_WARNING "Unknown qd65xx tuning fonction !\n");
		printk(KERN_WARNING "keeping settings !\n");
	}
}

/*
 * looks at the specified baseport, and if qd found, registers & initialises it
 * return 1 if another qd may be probed
 */

int __init qd_probe(int base)
{
	u8 config;
	int unit;

	config = inb(QD_CONFIG_PORT);

	if (! ((config & QD_CONFIG_BASEPORT) >> 1 == (base == 0xb0)) ) return 1;

	unit = ! (config & QD_CONFIG_IDE_BASEPORT);

	if ((config & 0xf0) == QD_CONFIG_QD6500) {
		if (qd_testreg(base)) return 1;		/* bad register */

		/* qd6500 found */

		printk(KERN_NOTICE "%s: qd6500 at %#x\n", ide_hwifs[unit].name, base);
		printk(KERN_DEBUG "qd6500: config=%#x, ID3=%u\n", config, QD_ID3);

		if (config & QD_CONFIG_DISABLED) {
			printk(KERN_WARNING "qd6500 is disabled !\n");
			return 1;
		}

		qd_setup(unit, base, config, QD6500_DEF_DATA, QD6500_DEF_DATA, &qd6500_tune_drive);
		return 1;
	}

	if (((config & 0xf0) == QD_CONFIG_QD6580_A) || ((config & 0xf0) == QD_CONFIG_QD6580_B)) {
		u8 control;

		if (qd_testreg(base) || qd_testreg(base+0x02)) return 1;
			/* bad registers */

		/* qd6580 found */

		control = inb(QD_CONTROL_PORT);

		printk(KERN_NOTICE "qd6580 at %#x\n", base);
		printk(KERN_DEBUG "qd6580: config=%#x, control=%#x, ID3=%u\n", config, control, QD_ID3);

		if (control & QD_CONTR_SEC_DISABLED) {
			/* secondary disabled */
			printk(KERN_INFO "%s: qd6580: single IDE board\n", ide_hwifs[unit].name);
			qd_setup(unit, base, config | (control << 8), QD6580_DEF_DATA, QD6580_DEF_DATA2, &qd6580_tune_drive);
			outb(QD_DEF_CONTR, QD_CONTROL_PORT);

			return 1;
		} else {
			/* secondary enabled */
			printk(KERN_INFO "%s&%s: qd6580: dual IDE board\n", ide_hwifs[0].name, ide_hwifs[1].name);

			qd_setup(ATA_PRIMARY, base, config | (control << 8), QD6580_DEF_DATA, QD6580_DEF_DATA, &qd6580_tune_drive);
			qd_setup(ATA_SECONDARY, base, config | (control << 8), QD6580_DEF_DATA2, QD6580_DEF_DATA2, &qd6580_tune_drive);
			outb(QD_DEF_CONTR, QD_CONTROL_PORT);

			return 0; /* no other qd65xx possible */
		}
	}
	/* no qd65xx found */
	return 1;
}

#ifndef MODULE
/*
 * called by ide.c when parsing command line
 */

void __init init_qd65xx(void)
{
	if (qd_probe(0x30)) qd_probe(0xb0);
}

#else

MODULE_AUTHOR("Samuel Thibault");
MODULE_DESCRIPTION("support of qd65xx vlb ide chipset");
MODULE_LICENSE("GPL");

int __init qd65xx_mod_init(void)
{
	if (qd_probe(0x30)) qd_probe(0xb0);
	if (ide_hwifs[0].chipset != ide_qd65xx && ide_hwifs[1].chipset != ide_qd65xx) return -ENODEV;
	return 0;
}
module_init(qd65xx_mod_init);

void __init qd65xx_mod_exit(void)
{
	qd_unsetup(ATA_PRIMARY);
	qd_unsetup(ATA_SECONDARY);
}
module_exit(qd65xx_mod_exit);
#endif
