/*
 *  linux/drivers/ide/qd6580.c		Version 0.04	June 4, 2000
 *
 *  Copyright (C) 1996-2000  Linus Torvalds & author (see below)
 */

/*
 *  Version 0.03	Cleaned auto-tune, added probe
 *  Version 0.04	Added second channel tuning
 *
 * QDI QD6580 EIDE controller fast support
 *
 * To activate controller support use kernel parameter "ide0=qd6580"
 * To enable tuning use kernel parameter "ide0=autotune"
 * To enable tuning second channel (not really tested),
 *    use parameter "ide1=autotune"
 */

/* 
 * Rewritten from the work of Colten Edwards <pje120@cs.usask.ca> by
 * Samuel Thibault <samuel.thibault@fnac.net>
 */

#undef REALLY_SLOW_IO           /* most systems can safely undef this */

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

/*
 * I/O ports are 0xb0-0xb3
 *            or 0x30-0x33
 *	-- this is a dual IDE interface with I/O chips
 *
 * More research on qd6580 being done by willmore@cig.mot.com (David)
 * More Information given by Petr Sourcek (petr@ryston.cz)
 * http://www.ryston.cz/petr/vlb
 */

/*
 * 0xb0: Timer1
 *
 *
 * 0xb1: Config
 *
 * bit 0: ide baseport: 1 = 0x1f0 ; 0 = 0x170
 *   (? Strange: the Dos driver uses it, and then forces baseport to 0x1f0 ?)
 * bit 1: qd baseport: 1 = 0xb0 ; 0 = 0x30
 * bit 2: ID3: bus speed: 1 = <=33MHz ; 0 = >33MHz
 * bit 3: 1 for qd6580
 * upper nibble is either 1010 or 0101, or else it isn't a qd6580
 *
 *
 * 0xb2: Timer2
 *
 *
 * 0xb3: Control
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
 * bit 7 : set 1 for non-ATAPI devices (read-ahead and post-write buffer ?)
 */

/* truncates a in [b,c] */
#define IDE_IN(a,b,c)   ( ((a)<(b)) ? (b) : ( (a)>(c) ? (c) : (a)) )

typedef struct ide_hd_timings_s {
	int active_time;		/* Active pulse (ns) minimum */
	int recovery_time;		/* Recovery pulse (ns) minimum */
} ide_hd_timings_t;

static int basePort;		/* base port address (0x30 or 0xb0) */
static byte config;			/* config register of qd6580 */
static byte control;		/* control register of qd6580 */

static int bus_clock;		/* Vesa local bus clock (ns) */
static int tuned=0;			/* to remember whether we've already been tuned */
static int snd_tuned=0;		/* to remember whether we've already been tuned */
static int nb_disks_prim=0;	/* number of disk drives on primary port */

/*
 * write_reg
 *
 * writes the specified byte on the specified register
 */

static void write_reg ( byte content, byte reg )
{
	unsigned long flags;

	save_flags(flags);		/* all CPUs */
	cli();					/* all CPUs */
	outb_p(content,reg);
	inb(0x3f6);
	restore_flags(flags);	/* all CPUs */
}

/*
 * tune_drive
 *
 * Finds timings for the specified drive, returns it in struct t
 */

static void tune_drive ( ide_drive_t *drive, byte pio, ide_hd_timings_t *t )
{
	ide_pio_data_t d;

	t->active_time   = 175;
	t->recovery_time = 415; /* worst cases values from the dos driver */

	if (!drive->present) {	/* not present : free to give any timing */
		t->active_time = 0;
		t->recovery_time = 0;
		return;
	}

	pio = ide_get_best_pio_mode(drive, pio, 255, &d);
	pio = IDE_MIN(pio,4);

	switch (pio) {
		case 0: break;
		case 3:
			if (d.cycle_time >= 110) {
				t->active_time = 86;
				t->recovery_time = d.cycle_time-102;
			} else {
				printk("%s: Strange recovery time !\n",drive->name);
				return;
			}
			break;
		case 4:
			if (d.cycle_time >= 69) {
				t->active_time = 70;
				t->recovery_time = d.cycle_time-61;
			} else {
				printk("%s: Strange recovery time !\n",drive->name);
				return;
			}
			break;
		default:
			if (d.cycle_time >= 180) {
				t->active_time = 110;
				t->recovery_time = d.cycle_time - 120;
			} else {
				t->active_time = ide_pio_timings[pio].active_time;
				t->recovery_time = d.cycle_time
						-t->active_time;
			}
	}
	printk("%s: PIO mode%d, tim1=%dns tim2=%dns\n", drive->name, pio, t->active_time, t->recovery_time);

	if (drive->media == ide_disk)
		nb_disks_prim++;
	else {
/* need to disable read-ahead FIFO and post-write buffer for ATAPI drives*/
		write_reg(0x5f,basePort+0x03);
		printk("%s: Warning: please try to connect this drive to secondary IDE port\nto improve data transfer rate on primary IDE port.\n",drive->name);
	}
}

/*
 * tune_snd_drive
 *
 * Finds timings for the specified drive, using second channel rules
 */

static void tune_snd_drive ( ide_drive_t *drive, byte pio, ide_hd_timings_t *t )
{
	ide_pio_data_t d;

	t->active_time   = 175;
	t->recovery_time = 415;

	if (!drive->present) {	/* not present : free to give any timing */
		t->active_time = 0;
		t->recovery_time = 0;
		return;
	}

	pio = ide_get_best_pio_mode(drive, pio, 255, &d);

	if ((pio) && (d.cycle_time >= 180)) {
		t->active_time = 115;
		t->recovery_time = d.cycle_time - 115;
	}
	printk("%s: PIO mode%d, tim1=%dns tim2=%dns\n", drive->name, pio, t->active_time, t->recovery_time);

	if ((drive->media == ide_disk) && (nb_disks_prim<2)) {
/* a disk drive on secondary port while there's room on primary, which is the
 * only one that has read-ahead fifo and post-write buffer ? What a waste !*/
		printk("%s: Warning: please try to connect this drive to primary IDE port\nto improve data transfer rate.\n",drive->name);
	}
}

/*
 * compute_timing
 *
 * computes the timing value where
 *    lower nibble is active time,   in count of VLB clocks, 17-(from 2 to 17)
 *    upper nibble is recovery time, in count of VLB clocks, 15-(from 2 to 15)
 */

static byte compute_timing ( char name[6], ide_hd_timings_t *t )
{
	byte active_cycle;
	byte recovery_cycle;
	byte parameter;

	active_cycle   = 17-IDE_IN(t->active_time   / bus_clock + 1, 2, 17);
	recovery_cycle = 15-IDE_IN(t->recovery_time / bus_clock + 1, 2, 15);

	parameter = active_cycle | (recovery_cycle<<4);

	printk("%s: tim1=%dns tim2=%dns => %#x\n", name, t[0].active_time, t[0].recovery_time, parameter);
	return(parameter);
}

/*
 * tune_ide
 *
 * Tunes the whole hwif, ie tunes each drives, and in case we have to share,
 * takes the worse timings to tune qd6580
 */

static void tune_ide ( ide_hwif_t *hwif, byte pio )
{
	unsigned long flags;
	ide_hd_timings_t t[2]={{0,0},{0,0}};
	int bus_speed = ide_system_bus_speed ();

	bus_clock = 1000 / bus_speed;

	save_flags(flags);		/* all CPUs */
	cli();					/* all CPUs */
	outb( (bus_clock<30) ? 0x0 : 0x0a, basePort + 0x02);
	outb( 0x40 | ((control & 0x02) ? 0x9f:0x1f), basePort+0x03);
	restore_flags(flags);

	tune_drive (&hwif->drives[0], pio, &t[0]);
	tune_drive (&hwif->drives[1], pio, &t[1]);

	if (control & 0x01) { /* only primary port enabled, can tune separately */
		write_reg(compute_timing (hwif->drives[0].name, &t[0]),basePort);
		write_reg(compute_timing (hwif->drives[1].name, &t[1]),basePort+0x02);
	} else {			  /* both ports enabled, we have to share */

		t[0].active_time   = IDE_MAX(t[0].active_time,  t[1].active_time);
		t[0].recovery_time = IDE_MAX(t[0].recovery_time,t[1].recovery_time);
		write_reg(compute_timing (hwif->name, &t[0]),basePort);
	}
}

/*
 * tune_snd_ide
 *
 * Tunes the whole secondary hwif, ie tunes each drives, and takes the worse
 * timings to tune qd6580
 */

static void tune_snd_ide ( ide_hwif_t *hwif, byte pio )
{
	ide_hd_timings_t t[2]={{0,0},{0,0}};

	tune_snd_drive (&hwif->drives[0], pio, &t[0]);
	tune_snd_drive (&hwif->drives[1], pio, &t[1]);

	t[0].active_time   = IDE_MAX(t[0].active_time,  t[1].active_time);
	t[0].recovery_time = IDE_MAX(t[0].recovery_time,t[1].recovery_time);

	write_reg(compute_timing (hwif->name, &t[0]),basePort+0x02);
}

/*
 * tune_qd6580
 *
 * tunes the hwif if not tuned
 */

static void tune_qd6580 (ide_drive_t *drive, byte pio)
{
	if (! tuned) {
		tune_ide(HWIF(drive), pio);
		tuned = 1;
	}
}

/*
 * tune_snd_qd6580
 *
 * tunes the second hwif if not tuned
 */

static void tune_snd_qd6580 (ide_drive_t *drive, byte pio)
{
	if (! snd_tuned) {
		tune_snd_ide(HWIF(drive), pio);
		snd_tuned = 1;
	}
}

/*
 * testreg
 *
 * tests if the given port is a register
 */

static int __init testreg(int port)
{
	byte savereg;
	byte readreg;
	unsigned long flags;

	save_flags(flags);		/* all CPUs */
	cli();					/* all CPUs */
	savereg = inb(port);
	outb_p(0x15,port);		/* safe value */
	readreg = inb_p(port);
	outb(savereg,port);
	restore_flags(flags);	/* all CPUs */

	if (savereg == 0x15) {
		printk("Outch ! the probe for qd6580 isn't reliable !\n");
		printk("Please contact maintainers to tell about your hardware\n");
		printk("Assuming qd6580 is not present.\n");
		return 0;
	}

	return (readreg == 0x15);
}

/*
 * trybase:
 *
 * tries to find a qd6580 at the given base and save it if found
 */

static int __init trybase (int base)
{
	unsigned long flags;

	save_flags(flags);		/* all CPUs */
	cli();					/* all CPUs */
	config = inb(base+0x01);
	control = inb(base+0x03);
	restore_flags(flags);	/* all CPUs */

	if (((config & 0xf0) != 0x50) && ((config & 0xf0) != 0xa0)) return(0);
	if (! ( ((config & 0x02) == 0x0) == (base == 0x30) ) ) return (0);

	/* Seems to be OK, let's use it */

	basePort = base;
	return(testreg(base));
}

/*
 * probe:
 *
 * probes qd6580 at 0xb0 (the default) or 0x30
 */

static int __init probe (void)
{
	return (trybase(0xb0) ? 1 : trybase(0x30));
}

/*
 * init_qd6580:
 *
 * called at the very beginning of initialization ; should just probe and link
 */

void __init init_qd6580 (void)
{
	if (! probe()) {
		printk("qd6580: not found\n");
		return;
	}

	printk("qd6580: base=%#x, config=%#x, control=%#x\n", basePort, config, control);

	ide_hwifs[0].chipset = ide_qd6580;
	ide_hwifs[0].tuneproc = &tune_qd6580;
	if (!(control & 0x01)) {
		ide_hwifs[1].chipset = ide_qd6580;
		ide_hwifs[1].tuneproc = &tune_snd_qd6580;
		ide_hwifs[0].mate = &ide_hwifs[1];
		ide_hwifs[1].mate = &ide_hwifs[0];
		ide_hwifs[1].channel = 1;
	}
}
