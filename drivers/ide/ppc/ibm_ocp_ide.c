/*
 *    Copyright 2002 MontaVista Software Inc.
 *      Completed implementation.
 *      Author: Armin Kuster <akuster@mvista.com>
 *      MontaVista Software, Inc.  <source@mvista.com>
 *
 *    Module name: ibm_ocp_ide.c
 *
 *    Description:
 *
 *    Based on ocp_stbxxxx.c
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/hdreg.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include "../ide-timing.h"
#include <asm/ocp.h>
#include <asm/io.h>
#include <asm/scatterlist.h>
#include <asm/ppc4xx_dma.h>

#include "ide_modes.h"

#define IDE_VER			"2.0"
ppc_dma_ch_t dma_ch;

/* use DMA channel 2 for IDE DMA operations */
#define IDE_DMACH	2	/* 2nd DMA channel */
#define IDE_DMA_INT	6	/* IDE dma channel 2 interrupt */

#define WMODE	0		/* default to DMA line mode */
#define PIOMODE	0

#define MK_TIMING(AS, DIOP, DIOY, DH) \
	((FIT((AS),    0, 15) << 27) | \
	 (FIT((DIOP),  0, 63) << 20) | \
	 (FIT((DIOY),  0, 63) << 13) | \
	 (FIT((DH),    0,  7) << 9))

#define UTIMING_SETHLD	(EZ(20 /*tACK*/, SYS_CLOCK_NS) - 1 /*fixed cycles*/)
#define UTIMING_ENV	(EZ(20 /*tENV*/, SYS_CLOCK_NS) - 1 /*fixed cycles*/)
#define UTIMING_SS	(EZ(50 /*tSS */, SYS_CLOCK_NS) - 3 /*fixed cycles*/)
#define MK_UTIMING(CYC, RP) \
	((FIT(UTIMING_SETHLD, 0, 15) << 27) | \
	 (FIT(UTIMING_ENV,    0, 15) << 22) | \
	 (FIT((CYC),          0, 15) << 17) | \
	 (FIT((RP),           0, 63) << 10) | \
	 (FIT(UTIMING_SS,     0, 15) << 5)  | \
	 1 /* Turn on Ultra DMA */)

/* Define the period of the STB clock used to generate the
 * IDE bus timing.  The clock is actually 63 MHz, but it
 * get rounded in a favorable direction.
 */
#define IDE_SYS_FREQ	63	/* MHz */
#define SYS_CLOCK_NS	(1000 / IDE_SYS_FREQ)

struct whold_timing {
	short mode;
	short whold;
};

static struct whold_timing whold_timing[] = {

	{XFER_UDMA_5, 0},
	{XFER_UDMA_4, 0},
	{XFER_UDMA_3, 0},

	{XFER_UDMA_2, 0},
	{XFER_UDMA_1, 0},
	{XFER_UDMA_0, 0},

	{XFER_UDMA_SLOW, 0},

	{XFER_MW_DMA_2, 0},
	{XFER_MW_DMA_1, 0},
	{XFER_MW_DMA_0, 0},

	{XFER_SW_DMA_2, 0},
	{XFER_SW_DMA_1, 0},
	{XFER_SW_DMA_0, 10},

	{XFER_PIO_5, 10},
	{XFER_PIO_4, 10},
	{XFER_PIO_3, 15},

	{XFER_PIO_2, 20},
	{XFER_PIO_1, 30},
	{XFER_PIO_0, 50},

	{XFER_PIO_SLOW,},

	{-1}
};

/* The interface doesn't have register/PIO timing for each device,
 * but rather "fast" and "slow" timing.  We have to determeine
 * which is the "fast" device based upon their capability.
 */
static int pio_mode[2];

/* Structure of the memory mapped IDE control.
*/
typedef struct ide_regs {
	unsigned int si_stat;	/* IDE status */
	unsigned int si_intenable;	/* IDE interrupt enable */
	unsigned int si_control;	/* IDE control */
	unsigned int pad0[0x3d];
	unsigned int si_c0rt;	/* Chan 0 Register transfer timing */
	unsigned int si_c0fpt;	/* Chan 0 Fast PIO transfer timing */
	unsigned int si_c0timo;	/* Chan 0 timeout */
	unsigned int pad1[2];
	unsigned int si_c0d0u;	/* Chan 0 UDMA transfer timing */
#define si_c0d0m si_c0d0u	/* Chan 0 Multiword DMA timing */
	unsigned int pad2;
	unsigned int si_c0d1u;	/* Chan 0 dev 1 UDMA timing */
#define si_c0d1m si_c0d1u	/* Chan 0 dev 1 Multiword DMA timing */
	unsigned int si_c0c;	/* Chan 0 Control */
	unsigned int si_c0s0;	/* Chan 0 Status 0 */
	unsigned int si_c0ie;	/* Chan 0 Interrupt Enable */
	unsigned int si_c0s1;	/* Chan 0 Status 0 */
	unsigned int pad4[4];
	unsigned int si_c0dcm;	/* Chan 0 DMA Command */
	unsigned int si_c0tb;	/* Chan 0 PRD Table base address */
	unsigned int si_c0dct;	/* Chan 0 DMA Count */
	unsigned int si_c0da;	/* Chan 0 DMA Address */
	unsigned int si_c0sr;	/* Chan 0 Slew Rate Output Control */
	unsigned char pad5[0xa2];
	unsigned short si_c0adc;	/* Chan 0 Alt status/control */
	unsigned char si_c0d;	/* Chan 0 data */
	unsigned char si_c0ef;	/* Chan 0 error/features */
	unsigned char si_c0sct;	/* Chan 0 sector count */
	unsigned char si_c0sn;	/* Chan 0 sector number */
	unsigned char si_c0cl;	/* Chan 0 cylinder low */
	unsigned char si_c0ch;	/* Chan 0 cylinder high */
	unsigned char si_c0dh;	/* Chan 0 device/head */
	unsigned char si_c0scm;	/* Chan 0 status/command */
} ide_t;

/* The structure of the PRD entry.  The address must be word aligned,
 * and the count must be an even number of bytes.
 */
typedef struct {
	unsigned int prd_physptr;
	unsigned int prd_count;	/* Count only in lower 16 bits */
} prd_entry_t;
#define PRD_EOT		(uint)0x80000000	/* Set in prd_count */

/* The number of PRDs required in a single transfer from the upper IDE
 * functions.  I believe the maximum number is 128, but most seem to
 * code to 256.  It's probably best to keep this under one page......
 */
#define NUM_PRD	256

static volatile ide_t *idp;
/* Virtual and physical address of the PRD page.
*/
static prd_entry_t *prd_table;
static dma_addr_t prd_phys;

/* Function Prototypes */
static void ocp_ide_tune_drive(ide_drive_t *, byte);
static int ocp_ide_dma_off(ide_drive_t * drive);

/* The STB04 has a fixed number of cycles that get added in
 * regardless.  Adjust an ide_timing struct to accommodate that.
 */
static void
ocp_ide_adjust_timing(struct ide_timing *t)
{
	t->setup -= 2;
	t->act8b -= 1;
	t->rec8b -= 1;
	t->active -= 1;
	t->recover -= 1;
}

/* this iis barrowed from ide_timing_find_mode so we can find the proper 
 * whold parameter 
 */

static short
whold_timing_find_mode(short speed)
{
	struct whold_timing *t;

	for (t = whold_timing; t->mode != speed; t++)
		if (t->mode < 0)
			return 0;
	return t->whold;
}

static int
ocp_ide_set_drive(ide_drive_t * drive, unsigned char speed)
{
	ide_drive_t *peer;
	struct ide_timing d, p, merge, *fast;
	int fast_device;
	unsigned int ctl;
	volatile unsigned int *dtiming;

	if (speed != XFER_PIO_SLOW && speed != drive->current_speed)
		if (ide_config_drive_speed(drive, speed))
			printk(KERN_WARNING
			       "ide%d: Drive %d didn't accept speed setting. Oh, well.\n",
			       drive->dn >> 1, drive->dn & 1);

	ide_timing_compute(drive, speed, &d, SYS_CLOCK_NS, SYS_CLOCK_NS);
	ocp_ide_adjust_timing(&d);

	/* This should be set somewhere else, but it isn't.....
	 */
	drive->dn = ((drive->select.all & 0x10) != 0);
	peer = HWIF(drive)->drives + (~drive->dn & 1);

	if (peer->present) {
		ide_timing_compute(peer, peer->current_speed, &p,
				   SYS_CLOCK_NS, SYS_CLOCK_NS);
		ocp_ide_adjust_timing(&p);
		ide_timing_merge(&p, &d, &merge,
				 IDE_TIMING_8BIT | IDE_TIMING_SETUP);
	} else {
		merge = d;
	}

	if (!drive->init_speed)
		drive->init_speed = speed;
	drive->current_speed = speed;

	/* Now determine which drive is faster, and set up the
	 * interface timing.  It would sure be nice if they would
	 * have just had the timing registers for each device......
	 */
	if (drive->dn & 1)
		pio_mode[1] = (int) speed;
	else
		pio_mode[0] = (int) speed;

	if (pio_mode[0] > pio_mode[1])
		fast_device = 0;
	else
		fast_device = 1;

	/* Now determine which of the drives
	 * the first call we only know one device, and on subsequent
	 * calls the user may manually change drive parameters.
	 * Make timing[0] the fast device and timing[1] the slow.
	 */
	if (fast_device == (drive->dn & 1))
		fast = &d;
	else
		fast = &p;

	/* Now we know which device is the fast one and which is
	 * the slow one.  The merged timing goes into the "regular"
	 * timing registers and represents the slower of both times.
	 */

	idp->si_c0rt = MK_TIMING(merge.setup, merge.act8b,
				 merge.rec8b,
				 whold_timing_find_mode(merge.mode));

	idp->si_c0fpt = MK_TIMING(fast->setup, fast->act8b,
				  fast->rec8b,
				  whold_timing_find_mode(fast->mode));

	/* Tell the interface which drive is the fast one.
	 */
	ctl = idp->si_c0c;	/* Chan 0 Control */
	ctl &= ~0x10000000;
	ctl |= fast_device << 28;
	idp->si_c0c = ctl;

	/* Set up DMA timing.
	 */
	if ((speed & XFER_MODE) != XFER_PIO) {
		/* NOTE: si_c0d0m and si_c0d0u are two different names
		 * for the same register.  Whether it is used for
		 * Multi-word DMA timings or Ultra DMA timings is
		 * determined by the LSB written into it.  This is also
		 * true for si_c0d1m and si_c0d1u.  */
		if (drive->dn & 1)
			dtiming = &(idp->si_c0d1m);
		else
			dtiming = &(idp->si_c0d0m);

		if ((speed & XFER_MODE) == XFER_UDMA) {
			static const int tRP[] = {
				EZ(160, SYS_CLOCK_NS) - 2 /*fixed cycles */ ,
				EZ(125, SYS_CLOCK_NS) - 2 /*fixed cycles */ ,
				EZ(100, SYS_CLOCK_NS) - 2 /*fixed cycles */ ,
				EZ(100, SYS_CLOCK_NS) - 2 /*fixed cycles */ ,
				EZ(100, SYS_CLOCK_NS) - 2 /*fixed cycles */ ,
				EZ(85, SYS_CLOCK_NS) - 2	/*fixed cycles */
			};
			static const int NUMtRP =
			    (sizeof (tRP) / sizeof (tRP[0]));
			*dtiming =
			    MK_UTIMING(d.udma,
				       tRP[FIT(speed & 0xf, 0, NUMtRP - 1)]);
		} else {
			/* Multi-word DMA.  Note that d.recover/2 is an
			 * approximation of MAX(tH, MAX(tJ, tN)) */
			*dtiming = MK_TIMING(d.setup, d.active,
					     d.recover, d.recover / 2);
		}
		drive->using_dma = 1;
	}

	return 0;
}

static void
ocp_ide_tune_drive(ide_drive_t * drive, byte pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
}

/*
 * Fill in the next PRD entry.
 */

static int ocp_ide_build_prd_entry(prd_entry_t **table, unsigned int paddr, 
				   unsigned int size, int *count)
{

	/*
	 * Note that one PRD entry can transfer
	 * at most 65535 bytes.
	 */

	while (size) {
		unsigned int tc = (size < 0xfe00) ? size : 0xfe00;

		if (++(*count) >= NUM_PRD) {
		  printk(KERN_WARNING "DMA table too small\n");
			return 0;	/* revert to PIO for this request */
		}
		(*table)->prd_physptr = (paddr & 0xfffffffe);

		if ((*table)->prd_physptr & 0xF) {
			printk(KERN_WARNING "DMA buffer not 16 byte aligned.\n");
			return 0;	/* revert to PIO for this request */
		}
		
		(*table)->prd_count = (tc & 0xfffe);
		paddr += tc;
		size -= tc;
		++(*table);
	}

	return 1;
}


static int
ocp_ide_build_dmatable(ide_drive_t * drive, int wr)
{
	prd_entry_t *table;
	int count = 0;
	struct request *rq = HWGROUP(drive)->rq;
	unsigned long size, vaddr, paddr;
	unsigned long prd_size, prd_paddr = 0;
	struct bio_vec *bvec, *bvprv;
	struct bio *bio;
	int i;

	table = prd_table;

	bvprv = NULL;
	rq_for_each_bio(bio, rq) {
		bio_for_each_segment(bvec, bio, i) {
			paddr = bvec_to_phys(bvec);
			vaddr = (unsigned long) __va(paddr);
			size = bvec->bv_len;
			if (wr)
				consistent_sync((void *)vaddr, 
						size, PCI_DMA_TODEVICE);
			else
				consistent_sync((void *)vaddr,
						size, PCI_DMA_FROMDEVICE);

			if (!BIOVEC_PHYS_MERGEABLE(bvprv, bvec)) {
				if (ocp_ide_build_prd_entry(&table, 
							    prd_paddr,
							    prd_size,
							    &count) == 0)
					return 0; /* use PIO */
				prd_paddr = 0;
			}

			if (prd_paddr == 0) {
				prd_paddr = paddr;
				prd_size = size;
			} else {
			  prd_size += size;
			}

			bvprv = bvec;
		} /* segments in bio */
	} /* bios in rq */

	if (prd_paddr) {
		if (ocp_ide_build_prd_entry(&table, 
					    prd_paddr,
					    prd_size,
					    &count) == 0)
			return 0; /* use PIO */
	}

	/* Add the EOT to the last table entry.
	 */
	if (count) {
		table--;
		table->prd_count |= PRD_EOT;
	} else {
		printk(KERN_DEBUG "%s: empty DMA table?\n", drive->name);
	}

	return 1;
}

/*
 * ocp_ide_dma_intr() is the handler for disk read/write DMA interrupts
 * This is taken directly from ide-dma.c, which we can't use because
 * it requires PCI support.
 */
ide_startstop_t
ocp_ide_dma_intr(ide_drive_t * drive)
{
	int i;
	byte stat, dma_stat;

	dma_stat = HWIF(drive)->ide_dma_end(drive);
	stat = HWIF(drive)->INB(IDE_STATUS_REG);	/* get drive status */
	if (OK_STAT(stat, DRIVE_READY, drive->bad_wstat | DRQ_STAT)) {
		if (!dma_stat) {
			struct request *rq = HWGROUP(drive)->rq;
			rq = HWGROUP(drive)->rq;
			for (i = rq->nr_sectors; i > 0;) {
				i -= rq->current_nr_sectors;
				ide_end_request(drive, 1, 
						rq->current_nr_sectors );
			}
			return ide_stopped;
		}
		printk("%s: dma_intr: bad DMA status (dma_stat=%x)\n",
		       drive->name, dma_stat);
	}
	return ide_error(drive, "dma_intr", stat);
}

/* ....and another one....
*/
int
report_drive_dmaing(ide_drive_t * drive)
{
	struct hd_driveid *id = drive->id;

	if ((id->field_valid & 4) && (eighty_ninty_three(drive)) &&
	    (id->dma_ultra & (id->dma_ultra >> 11) & 7)) {
		if ((id->dma_ultra >> 13) & 1) {
			printk(", UDMA(100)");	/* UDMA BIOS-enabled! */
		} else if ((id->dma_ultra >> 12) & 1) {
			printk(", UDMA(66)");	/* UDMA BIOS-enabled! */
		} else {
			printk(", UDMA(44)");	/* UDMA BIOS-enabled! */
		}
	} else if ((id->field_valid & 4) &&
		   (id->dma_ultra & (id->dma_ultra >> 8) & 7)) {
		if ((id->dma_ultra >> 10) & 1) {
			printk(", UDMA(33)");	/* UDMA BIOS-enabled! */
		} else if ((id->dma_ultra >> 9) & 1) {
			printk(", UDMA(25)");	/* UDMA BIOS-enabled! */
		} else {
			printk(", UDMA(16)");	/* UDMA BIOS-enabled! */
		}
	} else if (id->field_valid & 4) {
		printk(", (U)DMA");	/* Can be BIOS-enabled! */
	} else {
		printk(", DMA");
	}
	return 1;
}

static int
ocp_ide_check_dma(ide_drive_t * drive)
{
	struct hd_driveid *id = drive->id;
	int enable = 1;
	int speed;

	drive->using_dma = 0;

	if (drive->media == ide_floppy)
		enable = 0;

	/* Check timing here, we may be able to include XFER_UDMA_66
	 * and XFER_UDMA_100.  This basically tells the 'best_mode'
	 * function to also consider UDMA3 to UDMA5 device timing.
	 */
	if (enable) {
		/* Section 1.6.2.6 "IDE Controller, ATA/ATAPI-5" in the STB04xxx
		 * Datasheet says the following modes are supported:
		 *   PIO modes 0 to 4
		 *   Multiword DMA modes 0 to 2
		 *   UltraDMA modes 0 to 4
		 */
		int map = XFER_PIO | XFER_EPIO | XFER_MWDMA | XFER_UDMA;
		/* XFER_EPIO includes both PIO modes 4 and 5.  Mode 5 is not
		 * valid for the STB04, so mask it out of consideration just
		 * in case some drive sets it...
		 */
		id->eide_pio_modes &= ~4;

		/* Allow UDMA_66 only if an 80 conductor cable is connected. */
		if (eighty_ninty_three(drive))
			map |= XFER_UDMA_66;

		speed = ide_find_best_mode(drive, map);
		ocp_ide_set_drive(drive, speed);

		if (HWIF(drive)->autodma &&
		    (((speed & XFER_MODE) == XFER_PIO) ||
		     ((speed & XFER_MODE) == XFER_EPIO))) {
			drive->using_dma = 0;
		}
	}

	return 0;
}

static int ocp_ide_dma_off_quietly(ide_drive_t * drive)
{
	drive->using_dma = 0;
	return 0;
}

static int ocp_ide_dma_off(ide_drive_t * drive)
{
	printk(KERN_INFO "%s: DMA disabled\n", drive->name);
	return ocp_ide_dma_off_quietly(drive);
}

static int ocp_ide_dma_on(ide_drive_t * drive)
{
	return ocp_ide_check_dma(drive);
}

static int ocp_ide_dma_check(ide_drive_t * drive)
{
	return ocp_ide_dma_on(drive);
}

static int __ocp_ide_dma_begin(ide_drive_t * drive, int writing)
{
	idp->si_c0tb = (unsigned int) prd_phys;
	idp->si_c0s0 = 0xdc800000;	/* Clear all status */
	idp->si_c0ie = 0x90000000;	/* Enable all intr */
	idp->si_c0dcm = 0;
	idp->si_c0dcm =
		(writing ? 0x09000000 : 0x01000000);
	return 0;
}

static int ocp_ide_dma_begin(ide_drive_t * drive)
{
	idp->si_c0tb = (unsigned int) prd_phys;
	idp->si_c0s0 = 0xdc800000;	/* Clear all status */
	idp->si_c0ie = 0x90000000;	/* Enable all intr */
	idp->si_c0dcm = 0;
	idp->si_c0dcm =	0x01000000;
	return 0;
}

static int ocp_ide_dma_io(ide_drive_t * drive, int writing)
{
	if (!ocp_ide_build_dmatable(drive, writing))
		return 1;

	drive->waiting_for_dma = 1;
	if (drive->media != ide_disk)
		return 0;
	ide_set_handler(drive, &ocp_ide_dma_intr, WAIT_CMD, NULL);
	HWIF(drive)->OUTB(writing ? WIN_WRITEDMA : WIN_READDMA,
		 IDE_COMMAND_REG);
	return __ocp_ide_dma_begin(drive, writing);
}

static int ocp_ide_dma_read(ide_drive_t * drive)
{
	return ocp_ide_dma_io(drive, 0);
}

static int ocp_ide_dma_write(ide_drive_t * drive)
{
	return ocp_ide_dma_io(drive, 1);
}

static int ocp_ide_dma_end(ide_drive_t * drive)
{
	unsigned int dstat;

	drive->waiting_for_dma = 0;
	dstat = idp->si_c0s1;
	idp->si_c0s0 = 0xdc800000;	/* Clear all status */
	/* verify good dma status */
	return (dstat & 0x80000000);
}

static int ocp_ide_dma_test_irq(ide_drive_t * drive)
{
	return idp->si_c0s0 & 0x10000000 ? 1 : 0;
}

static int ocp_ide_dma_verbose(ide_drive_t * drive)
{
	return report_drive_dmaing(drive);
}

static unsigned int
ocp_ide_spinup(int index)
{
	int i, ret;
	ide_ioreg_t *io_ports;

	ret = 1;
	printk("OCP ide: waiting for drive spinup");
	printk("ioports for drive %d @ %p\n",index,ide_hwifs[index].io_ports);
	io_ports = ide_hwifs[index].io_ports;
	printk(".");
	
	/* wait until drive is not busy (it may be spinning up) */
	for (i = 0; i < 30; i++) {
		unsigned char stat;
		stat = inb_p(io_ports[7]);
		/* wait for !busy & ready */
		if ((stat & 0x80) == 0) {
			break;
		}
		udelay(1000 * 1000);	/* 1 second */
	}

	printk(".");

	/* select slave */
	outb_p(0xa0 | 0x10, io_ports[6]);

	for (i = 0; i < 30; i++) {
		unsigned char stat;
		stat = inb_p(io_ports[7]);
		/* wait for !busy & ready */
		if ((stat & 0x80) == 0) {
			break;
		}
		udelay(1000 * 1000);	/* 1 second */
	}
	if( i < 30){
		outb_p(0xa0, io_ports[6]);
		printk("Drive spun up \n");
	} else {
		printk("Drive spin up Failed !\n");
		ret = 0;
	}
	return (ret);
}

int
ocp_ide_default_irq(ide_ioreg_t base)
{
	return IDE0_IRQ;
}

/*
 * setup_ocp_ide()
 * Completes the setup of a on-chip ide controller card, once found.
 */
int __init setup_ocp_ide (struct ocp_device *pdev)
{
	ide_hwif_t	*hwif;
	unsigned int uicdcr;
	
	hwif = &ide_hwifs[pdev->num];
	hwif->index = pdev->num;
#ifdef WMODE
   /*Word Mode psc(11-12)=00,pwc(13-18)=000110, phc(19-21)=010, 22=1, 30=1  ----  0xCB02*/

    dma_ch.mode	=TM_S_MM;	  /* xfer from peripheral to mem */
    dma_ch.pwidth = PW_16;
    dma_ch.pwc = 6;                     /* set the max wait cycles  */
#else
/*Line Mode psc(11-12)=00,pwc(13-18)=000001, phc(19-21)=010, 22=1, 30=1  ----  0x2B02*/

    dma_ch.mode	=DMA_MODE_MM_DEVATSRC;	  /* xfer from peripheral to mem */
    dma_ch.pwidth = PW_64;		/* Line mode on stbs */
    dma_ch.pwc = 1;                     /* set the max wait cycles  */
#endif

    dma_ch.td	= DMA_TD;
    dma_ch.buffer_enable = 0;
    dma_ch.tce_enable = 0;
    dma_ch.etd_output = 0;
    dma_ch.pce = 0;
    dma_ch.pl = EXTERNAL_PERIPHERAL;    /* no op */
    dma_ch.dai = 1;
    dma_ch.sai = 0;
    dma_ch.psc = 0;                      /* set the max setup cycles */
    dma_ch.phc = 2;                      /* set the max hold cycles  */
    dma_ch.cp = PRIORITY_LOW;
    dma_ch.int_enable = 0;
    dma_ch.ch_enable = 0;		/* No chaining */
    dma_ch.tcd_disable = 1;		/* No chaining */

    if (hw_init_dma_channel(IDE_DMACH, &dma_ch) != DMA_STATUS_GOOD)
        return -EBUSY;

    /* init CIC select2 reg to connect external DMA port 3 to internal
     * DMA channel 2
     */
    map_dma_port(IDE_DMACH,EXT_DMA_3,DMA_CHAN_2); 

    /* Enable the interface.
     */
    idp->si_control = 0x80000000;
    idp->si_c0s0 = 0xdc800000;	/* Clear all status */
    idp->si_intenable = 0x80000000;

    /* Per the STB04 data sheet:
     *  1)  tTO = ((8*RDYT) + 1) * SYS_CLK
     * and:
     *  2)  tTO >= 1250 + (2 * SYS_CLK) - t2
     * Solving the first equation for RDYT:
     *             (tTO/SYS_CLK) - 1
     *  3)  RDYT = -----------------
     *                     8
     * Substituting equation 2) for tTO in equation 3:
     *             ((1250 + (2 * SYS_CLK) - t2)/SYS_CLK) - 1
     *  3)  RDYT = -----------------------------------------
     *                                8
     * It's just the timeout so having it too long isn't too
     * significant, so we'll just assume t2 is zero.  All this math
     * is handled by the compiler and RDYT ends up being 11 assuming
     * that SYS_CLOCK_NS is 15.
     */
    idp->si_c0timo = (EZ(EZ(1250 + 2 * SYS_CLOCK_NS, SYS_CLOCK_NS) - 1, 8)) << 23;	/* Chan 0 timeout */

    /* Stuff some slow default PIO timing.
     */
    idp->si_c0rt = MK_TIMING(6, 19, 15, 2);
    idp->si_c0fpt = MK_TIMING(6, 19, 15, 2);
    
    /* We should probably have UIC functions to set external
     * interrupt level/edge.
     */
    uicdcr = mfdcr(DCRN_UIC_PR(UIC0));
    uicdcr &= ~(0x80000000 >> IDE0_IRQ);
    mtdcr(DCRN_UIC_PR(UIC0), uicdcr);
    mtdcr(DCRN_UIC_TR(UIC0), 0x80000000 >> IDE0_IRQ);

    /* Grab a page for the PRD Table.
     */
    prd_table = (prd_entry_t *) consistent_alloc(GFP_KERNEL,
						 NUM_PRD *
						 sizeof
						 (prd_entry_t),
						 &prd_phys);


    if(!ocp_ide_spinup(hwif->index))
	    return 0;
    
    return 1;
}


static int __devinit ocp_ide_probe(struct ocp_device *pdev)
{
	int i;
	unsigned int index;
	hw_regs_t * hw;
	unsigned char *ip;

	printk("IBM STB04xxx IDE driver version %s\n", IDE_VER);

	hw = kmalloc(sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return 0;
	memset(hw, 0, sizeof(*hw));

	if (!request_region(pdev->paddr, IDE0_SIZE, "IDE")) {
		printk(KERN_WARNING "ocp_ide: failed request_region\n");
		return -1;
	}

	if ((idp = (ide_t *) ioremap(pdev->paddr,
				     IDE0_SIZE)) == NULL) {
		printk(KERN_WARNING "ocp_ide: failed ioremap\n");
		return -1;
	}

	pdev->dev.driver_data = (void *) idp;

	pdev->ocpdev  = (void *) hw;
	index = pdev->num;
	ip = (unsigned char *) (&(idp->si_c0d));	/* Chan 0 data */

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = (unsigned long) (ip++);
	}

	hw->io_ports[IDE_CONTROL_OFFSET] = (unsigned long) (&(idp->si_c0adc));
	hw->irq = pdev->irq;

	/* use DMA channel 2 for IDE DMA operations */
	hw->dma = IDE_DMACH;

	ide_hwifs[index].tuneproc = &ocp_ide_tune_drive;
	ide_hwifs[index].drives[0].autotune = 1;
	ide_hwifs[index].autodma = 1;
	ide_hwifs[index].ide_dma_off = &ocp_ide_dma_off;
	ide_hwifs[index].ide_dma_off_quietly = &ocp_ide_dma_off_quietly;
	ide_hwifs[index].ide_dma_host_off = &ocp_ide_dma_off_quietly;
	ide_hwifs[index].ide_dma_on = &ocp_ide_dma_on;
	ide_hwifs[index].ide_dma_host_on = &ocp_ide_dma_on;
	ide_hwifs[index].ide_dma_check = &ocp_ide_dma_check;
	ide_hwifs[index].ide_dma_read = &ocp_ide_dma_read;
	ide_hwifs[index].ide_dma_write = &ocp_ide_dma_write;
	ide_hwifs[index].ide_dma_begin = &ocp_ide_dma_begin;
	ide_hwifs[index].ide_dma_end = &ocp_ide_dma_end;
	ide_hwifs[index].ide_dma_test_irq = &ocp_ide_dma_test_irq;
	ide_hwifs[index].ide_dma_verbose = &ocp_ide_dma_verbose;
	ide_hwifs[index].speedproc = &ocp_ide_set_drive;
	ide_hwifs[index].noprobe = 0;

	memcpy(ide_hwifs[index].io_ports, hw->io_ports, sizeof (hw->io_ports));
	ide_hwifs[index].irq = pdev->irq;

	ocp_force_power_on(pdev);
	return 1;
}

static void __devexit ocp_ide_remove_one (struct ocp_device *pdev)
{
	ocp_force_power_off(pdev);
}

static struct ocp_device_id ocp_ide_id_tbl[] __devinitdata = {
	{OCP_VENDOR_IBM,OCP_FUNC_IDE},
	{0,}
};

MODULE_DEVICE_TABLE(ocp,ocp_ide_id_tbl );

static struct ocp_driver ocp_ide_driver = {
	.name		= "ocp_ide",
	.id_table	= ocp_ide_id_tbl,
	.probe		= ocp_ide_probe,
	.remove		= __devexit_p(ocp_ide_remove_one),
#if defined(CONFIG_PM)
	.suspend	= ocp_generic_suspend,
	.resume		= ocp_generic_resume,
#endif /* CONFIG_PM */
};


void __init std_ide_cntl_scan(void)
{
	struct ocp_device *dev;
	int i, max;
	printk("OCP ide ver:%s\n", IDE_VER);

	ocp_module_init(&ocp_ide_driver);
	max = ocp_get_num(OCP_FUNC_IDE);
	for(i = 0; i < max; i++){
		dev = ocp_get_dev(OCP_FUNC_IDE,i);
		if(!dev)	
		  setup_ocp_ide(dev);
	}
}
#if 0
#if defined (CONFIG_MODULE)
static int __init
ocp_ide_init(void)
{
	printk("OCP ide ver:%s\n", IDE_VER);
	return ocp_module_init(&ocp_ide_driver);
}

void __exit
ocp_ide_fini(void)
{
	ocp_unregister_driver(&ocp_ide_driver);
}

module_init(ocp_ide_init);
module_exit(ocp_ide_fini);
#endif
#endif

