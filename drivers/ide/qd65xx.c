/*
 *  linux/drivers/ide/qd65xx.c		Version 0.06	Aug 3, 2000
 *
 *  Copyright (C) 1996-2000  Linus Torvalds & author (see below)
 */

/*
 *  Version 0.03	Cleaned auto-tune, added probe
 *  Version 0.04	Added second channel tuning
 *  Version 0.05	Enhanced tuning ; added qd6500 support
 *  Version 0.06	added dos driver's list
 *
 * QDI QD6500/QD6580 EIDE controller fast support
 *
 * Please set local bus speed using kernel parameter idebus
 * 	for example, "idebus=33" stands for 33Mhz VLbus
 * To activate controller support, use "ide0=qd65xx"
 * To enable tuning, use "ide0=autotune"
 * To enable second channel tuning (qd6580 only), use "ide1=autotune"
 */

/*
 * Rewritten from the work of Colten Edwards <pje120@cs.usask.ca> by
 * Samuel Thibault <samuel.thibault@fnac.net>
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <asm/io.h>

#include "ide_modes.h"
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
 * 	(maybe read-ahead and post-write buffer ?)
 */

static int timings[4]={-1,-1,-1,-1}; /* stores current timing for each timer */

static void qd_write_reg (byte content, byte reg)
{
	unsigned long flags;

	save_flags(flags);	/* all CPUs */
	cli();			/* all CPUs */
	outb(content,reg);
	restore_flags(flags);	/* all CPUs */
}

byte __init qd_read_reg (byte reg)
{
	unsigned long flags;
	byte read;

	save_flags(flags);	/* all CPUs */
	cli();			/* all CPUs */
	read = inb(reg);
	restore_flags(flags);	/* all CPUs */
	return read;
}

/*
 * qd_select:
 *
 * This routine is invoked from ide.c to prepare for access to a given drive.
 */

static void qd_select (ide_drive_t *drive)
{
	byte index = (( (QD_TIMREG(drive)) & 0x80 ) >> 7) |
			(QD_TIMREG(drive) & 0x02);

	if (timings[index] != QD_TIMING(drive))
		qd_write_reg(timings[index] = QD_TIMING(drive), QD_TIMREG(drive));
}

/*
 * qd6500_compute_timing
 *
 * computes the timing value where
 * 	lower nibble represents active time,   in count of VLB clocks
 * 	upper nibble represents recovery time, in count of VLB clocks
 */

static byte qd6500_compute_timing (ide_hwif_t *hwif, int active_time, int recovery_time)
{
	byte active_cycle,recovery_cycle;

	if (system_bus_clock()<=33) {
		active_cycle =   9  - IDE_IN(active_time   * system_bus_clock() / 1000 + 1, 2, 9);
		recovery_cycle = 15 - IDE_IN(recovery_time * system_bus_clock() / 1000 + 1, 0, 15);
	} else {
		active_cycle =   8  - IDE_IN(active_time   * system_bus_clock() / 1000 + 1, 1, 8);
		recovery_cycle = 18 - IDE_IN(recovery_time * system_bus_clock() / 1000 + 1, 3, 18);
	}

	return((recovery_cycle<<4) | 0x08 | active_cycle);
}

/*
 * qd6580_compute_timing
 *
 * idem for qd6580
 */

static byte qd6580_compute_timing (int active_time, int recovery_time)
{
	byte active_cycle   = 17-IDE_IN(active_time   * system_bus_clock() / 1000 + 1, 2, 17);
	byte recovery_cycle = 15-IDE_IN(recovery_time * system_bus_clock() / 1000 + 1, 2, 15);

	return((recovery_cycle<<4) | active_cycle);
}

/*
 * qd_find_disk_type
 *
 * tries to find timing from dos driver's table
 */

static int qd_find_disk_type (ide_drive_t *drive,
		int *active_time, int *recovery_time)
{
	struct qd65xx_timing_s *p;
	char model[40];

	if (!*drive->id->model) return 0;

	strncpy(model,drive->id->model,40);
	ide_fixstring(model,40,1); /* byte-swap */

	for (p = qd65xx_timing ; p->offset != -1 ; p++) {
		if (!strncmp(p->model, model+p->offset,4)) {
			printk(KERN_DEBUG "%s: listed !\n",drive->name);
			*active_time = p->active;
			*recovery_time = p->recovery;
			return 1;
		}
	}
	return 0;
}

/*
 * qd_timing_ok:
 *
 * check whether timings don't conflict
 */

static int qd_timing_ok (ide_drive_t drives[])
{
	return (IDE_IMPLY(drives[0].present && drives[1].present,
			IDE_IMPLY(QD_TIMREG(drives) == QD_TIMREG(drives+1),
			          QD_TIMING(drives) == QD_TIMING(drives+1))));
	/* if same timing register, must be same timing */
}

/*
 * qd_set_timing:
 *
 * records the timing, and enables selectproc as needed
 */

static void qd_set_timing (ide_drive_t *drive, byte timing)
{
	ide_hwif_t *hwif = HWIF(drive);

	drive->drive_data &= 0xff00;
	drive->drive_data |= timing;
	if (qd_timing_ok(hwif->drives)) {
		qd_select(drive); /* selects once */
		hwif->selectproc = NULL;
	} else
		hwif->selectproc = &qd_select;

	printk(KERN_DEBUG "%s: %#x\n",drive->name,timing);
}

/*
 * qd6500_tune_drive
 */

static void qd6500_tune_drive (ide_drive_t *drive, byte pio)
{
	int active_time   = 175;
	int recovery_time = 415; /* worst case values from the dos driver */

	if (drive->id && !qd_find_disk_type(drive,&active_time,&recovery_time)
		&& drive->id->tPIO && (drive->id->field_valid & 0x02)
		&& drive->id->eide_pio >= 240) {

		printk(KERN_INFO "%s: PIO mode%d\n", drive->name,
				drive->id->tPIO);
		active_time = 110;
		recovery_time = drive->id->eide_pio - 120;
	}

	qd_set_timing(drive,qd6500_compute_timing(HWIF(drive),active_time,recovery_time));
}

/*
 * qd6580_tune_drive
 */

static void qd6580_tune_drive (ide_drive_t *drive, byte pio)
{
	ide_pio_data_t d;
	int base = HWIF(drive)->select_data;
	int active_time   = 175;
	int recovery_time = 415; /* worst case values from the dos driver */

	if (drive->id && !qd_find_disk_type(drive,&active_time,&recovery_time)) {
		pio = ide_get_best_pio_mode(drive, pio, 255, &d);
		pio = IDE_MIN(pio,4);

		switch (pio) {
			case 0: break;
			case 3:
				if (d.cycle_time >= 110) {
					active_time = 86;
					recovery_time = d.cycle_time-102;
				} else
					printk(KERN_WARNING "%s: Strange recovery time !\n",drive->name);
				break;
			case 4:
				if (d.cycle_time >= 69) {
					active_time = 70;
					recovery_time = d.cycle_time-61;
				} else
					printk(KERN_WARNING "%s: Strange recovery time !\n",drive->name);
				break;
			default:
				if (d.cycle_time >= 180) {
					active_time = 110;
					recovery_time = d.cycle_time - 120;
				} else {
					active_time = ide_pio_timings[pio].active_time;
					recovery_time = d.cycle_time
							-active_time;
				}
		}
		printk(KERN_INFO "%s: PIO mode%d\n",drive->name,pio);
	}

	if (!HWIF(drive)->channel && drive->media != ide_disk) {
		qd_write_reg(0x5f,QD_CONTROL_PORT);
		printk(KERN_WARNING "%s: ATAPI: disabled read-ahead FIFO and post-write buffer on %s.\n",drive->name,HWIF(drive)->name);
	}

	qd_set_timing(drive,qd6580_compute_timing(active_time,recovery_time));
}

/*
 * qd_testreg
 *
 * tests if the given port is a register
 */

static int __init qd_testreg(int port)
{
	byte savereg;
	byte readreg;
	unsigned long flags;

	save_flags(flags);	/* all CPUs */
	cli();			/* all CPUs */
	savereg = inb_p(port);
	outb_p(QD_TESTVAL,port);	/* safe value */
	readreg = inb_p(port);
	outb(savereg,port);
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
 * probe:
 *
 * looks at the specified baseport, and if qd found, registers & initialises it
 * return 1 if another qd may be probed
 */

int __init probe (int base)
{
	byte config;
	byte index;

	config = qd_read_reg(QD_CONFIG_PORT);

	if (! ((config & QD_CONFIG_BASEPORT) >> 1 == (base == 0xb0)) ) return 1;

	index = ! (config & QD_CONFIG_IDE_BASEPORT);

	if ((config & 0xf0) == QD_CONFIG_QD6500) {
		ide_hwif_t *hwif = &ide_hwifs[index];

		if (qd_testreg(base)) return 1;		/* bad register */

			/* qd6500 found */

		printk(KERN_NOTICE "%s: qd6500 at %#x\n",
			ide_hwifs[index].name, base);
		
		printk(KERN_DEBUG "qd6500: config=%#x, ID3=%u\n",
			config, QD_ID3);
		
		if (config & QD_CONFIG_DISABLED) {
			printk(KERN_WARNING "qd6500 is disabled !\n");
			return 1;
		}

		hwif->chipset = ide_qd65xx;
		hwif->select_data = base;
		hwif->config_data = config;
		hwif->drives[0].drive_data =
		hwif->drives[1].drive_data = QD6500_DEF_DATA;
		hwif->drives[0].io_32bit =
		hwif->drives[1].io_32bit = 1;
		hwif->tuneproc = &qd6500_tune_drive;
		return 1;
	}

	if (((config & 0xf0) == QD_CONFIG_QD6580_A) || ((config & 0xf0) == QD_CONFIG_QD6580_B)) {

		byte control;

		if (qd_testreg(base) || qd_testreg(base+0x02)) return 1;
			/* bad registers */

			/* qd6580 found */

		control = qd_read_reg(QD_CONTROL_PORT);

		printk(KERN_NOTICE "qd6580 at %#x\n", base);
		printk(KERN_DEBUG "qd6580: config=%#x, control=%#x, ID3=%u\n",
			config, control, QD_ID3);

		if (control & QD_CONTR_SEC_DISABLED) {
			ide_hwif_t *hwif = &ide_hwifs[index];

			/* secondary disabled */
			printk(KERN_INFO "%s: qd6580: single IDE board\n",
					ide_hwifs[index].name);

			hwif->chipset = ide_qd65xx;
			hwif->select_data = base;
			hwif->config_data = config | (control <<8);
			hwif->drives[0].drive_data =
			hwif->drives[1].drive_data = QD6580_DEF_DATA;
			hwif->drives[0].io_32bit =
			hwif->drives[1].io_32bit = 1;
			hwif->tuneproc = &qd6580_tune_drive;

			qd_write_reg(QD_DEF_CONTR,QD_CONTROL_PORT);

			return 1;
		} else {
			int i,j;
			/* secondary enabled */
			printk(KERN_INFO "%s&%s: qd6580: dual IDE board\n",
					ide_hwifs[0].name,ide_hwifs[1].name);

			for (i=0;i<2;i++) {

				ide_hwifs[i].chipset = ide_qd65xx;
				ide_hwifs[i].mate = &ide_hwifs[i^1];
				ide_hwifs[i].channel = i;

				ide_hwifs[i].select_data = base;
				ide_hwifs[i].config_data = config | (control <<8);
				ide_hwifs[i].tuneproc = &qd6580_tune_drive;

				for (j=0;j<2;j++) {
					ide_hwifs[i].drives[j].drive_data = QD6580_DEF_DATA;
					ide_hwifs[i].drives[j].io_32bit = 1;
				}
			}

			qd_write_reg(QD_DEF_CONTR,QD_CONTROL_PORT);

			return 0; /* no other qd65xx possible */
		}
	}
	/* no qd65xx found */
	return 1;
}

/*
 * init_qd65xx:
 *
 * called at the very beginning of initialization ; should just probe and link
 */

void __init init_qd65xx (void)
{
	if (probe(0x30)) probe(0xb0);
}
