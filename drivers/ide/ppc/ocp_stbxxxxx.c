/*
 *    Copyright 2002 MontaVista Software Inc.
 *      Completed implementation.
 *      Author: Armin Kuster <akuster@mvista.com>
 *      MontaVista Software, Inc.  <source@mvista.com>
 *
 *    Module name: ocp_stbxxxx.c
 *
 *    Description:
 *
 *    Based on stb03xxx.c
 */

#include <linux/types.h>
#include <linux/hdreg.h>
#include <linux/delay.h>
#include <linux/ide.h>
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

extern char *ide_dmafunc_verbose(ide_dma_action_t dmafunc);

#define WMODE	0		/* default to DMA line mode */
#define PIOMODE	0
static volatile unsigned long dmastat;

/* Function Prototypes */
static void ocp_ide_tune_drive(ide_drive_t *, byte);
static byte ocp_ide_dma_2_pio(byte);
static int ocp_ide_tune_chipset(ide_drive_t *, byte);
static int ocp_ide_dmaproc(ide_dma_action_t, ide_drive_t *);

static void
ocp_ide_tune_drive(ide_drive_t * drive, byte pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
}

static byte
ocp_ide_dma_2_pio(byte xfer_rate)
{
	switch (xfer_rate) {
	case XFER_UDMA_5:
	case XFER_UDMA_4:
	case XFER_UDMA_3:
	case XFER_UDMA_2:
	case XFER_UDMA_1:
	case XFER_UDMA_0:
	case XFER_MW_DMA_2:
	case XFER_PIO_4:
		return 4;
	case XFER_MW_DMA_1:
	case XFER_PIO_3:
		return 3;
	case XFER_SW_DMA_2:
	case XFER_PIO_2:
		return 2;
	case XFER_MW_DMA_0:
	case XFER_SW_DMA_1:
	case XFER_SW_DMA_0:
	case XFER_PIO_1:
	case XFER_PIO_0:
	case XFER_PIO_SLOW:
	default:
		return 0;
	}
}

static int
ocp_ide_tune_chipset(ide_drive_t * drive, byte speed)
{
	int err = 0;

	ocp_ide_tune_drive(drive, ocp_ide_dma_2_pio(speed));

	if (!drive->init_speed)
		drive->init_speed = speed;
	err = ide_config_drive_speed(drive, speed);
	drive->current_speed = speed;
	return err;
}

static int
redwood_config_drive_for_dma(ide_drive_t * drive)
{
	struct hd_driveid *id = drive->id;
	byte speed;
	int func = ide_dma_off;

	/*
	 * Enable DMA on any drive that has multiword DMA
	 */
	if (id->field_valid & 2) {
		if (id->dma_mword & 0x0004) {
			speed = XFER_MW_DMA_2;
			func = ide_dma_on;
		} else if (id->dma_mword & 0x0002) {
			speed = XFER_MW_DMA_1;
			func = ide_dma_on;
		} else if (id->dma_mword & 1) {
			speed = XFER_MW_DMA_0;
			func = ide_dma_on;
		} else if (id->dma_1word & 0x0004) {
			speed = XFER_SW_DMA_2;
			func = ide_dma_on;
		} else {
			speed = XFER_PIO_0 +
			    ide_get_best_pio_mode(drive, 255, 5, NULL);
		}
	}

	ocp_ide_tune_drive(drive, ocp_ide_dma_2_pio(speed));
	return ocp_ide_dmaproc(func, drive);
}

ide_startstop_t
ocp_ide_intr(ide_drive_t * drive)
{
	int i;
	byte dma_stat;
	unsigned int nsect;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	struct request *rq = hwgroup->rq;
	unsigned long block, b1, b2, b3, b4;

	nsect = rq->current_nr_sectors;

	dma_stat = HWIF(drive)->dmaproc(ide_dma_end, drive);

	rq->sector += nsect;
	rq->buffer += nsect << 9;
	rq->errors = 0;
	i = (rq->nr_sectors -= nsect);
	ide_end_request(1, HWGROUP(drive));
	if (i > 0) {
		b1 = IN_BYTE(IDE_SECTOR_REG);
		b2 = IN_BYTE(IDE_LCYL_REG);
		b3 = IN_BYTE(IDE_HCYL_REG);
		b4 = IN_BYTE(IDE_SELECT_REG);
		block = ((b4 & 0x0f) << 24) + (b3 << 16) + (b2 << 8) + (b1);
		block++;
		if (drive->select.b.lba) {
			OUT_BYTE(block, IDE_SECTOR_REG);
			OUT_BYTE(block >>= 8, IDE_LCYL_REG);
			OUT_BYTE(block >>= 8, IDE_HCYL_REG);
			OUT_BYTE(((block >> 8) & 0x0f) | drive->select.all,
				 IDE_SELECT_REG);
		} else {
			unsigned int sect, head, cyl, track;
			track = block / drive->sect;
			sect = block % drive->sect + 1;
			OUT_BYTE(sect, IDE_SECTOR_REG);
			head = track % drive->head;
			cyl = track / drive->head;
			OUT_BYTE(cyl, IDE_LCYL_REG);
			OUT_BYTE(cyl >> 8, IDE_HCYL_REG);
			OUT_BYTE(head | drive->select.all, IDE_SELECT_REG);
		}

		if (rq->cmd == READ)
			dma_stat = HWIF(drive)->dmaproc(ide_dma_read, drive);
		else
			dma_stat = HWIF(drive)->dmaproc(ide_dma_write, drive);
		return ide_started;
	}
	return ide_stopped;
}

void
ocp_ide_dma_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	dmastat = get_dma_status();
#if WMODE
	if (dmastat & 0x1000) {
		//This should not happen at least in Word Mode, I have noticed this. Is it some timing problem ?
		printk
		    ("ocp_ide_dma_intr dma req pending from external device\n");
	}
#endif

	clr_dma_status(IDE_DMACH);
}

static int
ocp_ide_dmaproc(ide_dma_action_t func, ide_drive_t * drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	int i, reading = 0;
	struct request *rq = HWGROUP(drive)->rq;
	unsigned long flags;
	unsigned long length;

	switch (func) {
	case ide_dma_off:
	case ide_dma_off_quietly:
		/*disable_dma */
		return 0;

	case ide_dma_on:
#if PIOMODE
		return 1;
#endif

		mtdcr(DCRN_DMACR2, 0);
		clr_dma_status(IDE_DMACH);

		save_flags(flags);
		cli();
		if (ide_request_irq
		    (IDE_DMA_INT, &ocp_ide_dma_intr, SA_INTERRUPT,
		     hwif->name, hwif->hwgroup)) {
			printk("ide_redwood: ide_request_irq failed int=%d\n",
			       IDE_DMA_INT);
			restore_flags(flags);
			return 1;
		}
		restore_flags(flags);

		drive->using_dma = (func == ide_dma_on);
#if WMODE
		mtdcr(DCRN_DCRXBCR, 0);
		mtdcr(DCRN_CICCR, mfdcr(DCRN_CICCR) | 0x00000400);
#else
		/* Configure CIC reg for line mode dma */
		mtdcr(DCRN_CICCR, mfdcr(DCRN_CICCR) & ~0x00000400);
#endif
		return 0;

	case ide_dma_check:
		return redwood_config_drive_for_dma(drive);
	case ide_dma_read:
		reading = 1;
	case ide_dma_write:
		if (drive->media != ide_disk)
			return -1;

		if (get_channel_config(IDE_DMACH, &dma_ch) & DMA_CHANNEL_BUSY )	/* DMA is busy? */
			return -1;


		if (reading) {
			dma_cache_inv((unsigned long) rq->buffer,
				      rq->current_nr_sectors * 512);
#if WMODE
			set_src_addr(IDE_DMACH, 0);
#else
			set_src_addr(IDE_DMACH, 0xfce00000);
#endif
			set_dst_addr(IDE_DMACH, virt_to_bus(rq->buffer));
		} else {
			dma_cache_wback_inv((unsigned long) rq->buffer,
					    rq->current_nr_sectors * 512);
			set_src_addr(IDE_DMACH, virt_to_bus(rq->buffer));
#if WMODE
			set_dst_addr(2, 0);
#else
			set_dst_addr(IDE_DMACH, 0xfce00000);
#endif
		}

#if WMODE
		length = rq->current_nr_sectors * 512 / 2;
#else
		length = rq->current_nr_sectors * 512 / 16;
#endif
		OUT_BYTE(rq->current_nr_sectors, IDE_NSECTOR_REG);
		set_dma_count(IDE_DMACH, length);

		/* CE=0 disable DMA */
		/* Set up the Line Buffer Control Register
		 * 11d1xxxx0.. - 11=Mode 2 (120 ns cycle), d=1/0(read/write)
		 * 1=active, 0=1X clock mode.
		 */

		if (reading) {
#if WMODE
			set_dma_mode(IDE_DMACH,DMA_TD | TM_S_MM);
#else
		mtdcr(DCRN_DCRXBCR, 0x90000000);
		set_dma_mode(IDE_DMACH,SET_DMA_DAI(1) | SET_DMA_SAI(0) | DMA_MODE_MM_DEVATDST);
#endif
		} else {
#if WMODE
			set_dma_mode(IDE_DMACH,DMA_TD | TM_S_MM);
#else
			mtdcr(DCRN_DCRXBCR, 0xB0000000);
			set_dma_mode(IDE_DMACH,SET_DMA_DAI(0) | SET_DMA_SAI(1) | DMA_MODE_MM_DEVATDST);
#endif
		}

		set_dma_mode(hwif->hw.dma, reading
			     ? DMA_MODE_READ : DMA_MODE_WRITE);
		drive->waiting_for_dma = 1;
		ide_set_handler(drive, &ocp_ide_intr, WAIT_CMD, NULL);
		OUT_BYTE(reading ? WIN_READDMA : WIN_WRITEDMA, IDE_COMMAND_REG);

	case ide_dma_begin:
		/* enable DMA */
		enable_dma_interrupt(IDE_DMACH);
		/* wait for dma to complete (channel 2 terminal count) */
		for (i = 0; i < 5000000; i++) {
			if (dmastat & DMA_CS2)
				break;
		}
		dmastat = 0;
		return 0;

	case ide_dma_end:
		drive->waiting_for_dma = 0;

		/* disable DMA */
		disable_dma_interrupt(IDE_DMACH);
		return 0;

	case ide_dma_test_irq:
		return 1;	/* returns 1 if dma irq issued, 0 otherwise */

	case ide_dma_bad_drive:
	case ide_dma_good_drive:
	case ide_dma_verbose:
	case ide_dma_timeout:
	case ide_dma_retune:
	case ide_dma_lostirq:
		printk("ide_dmaproc: chipset supported %s func only: %d\n",
		       ide_dmafunc_verbose(func), func);
		return 1;
	default:
		printk("ide_dmaproc: unsupported %s func: %d\n",
		       ide_dmafunc_verbose(func), func);
		return 1;

	}

}
void
ibm4xx_ide_spinup(int index)
{
	int i;
	ide_ioreg_t *io_ports;

	printk("ide_redwood: waiting for drive ready..");
	io_ports = ide_hwifs[index].io_ports;

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

	printk("..");

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

	printk("..");

	outb_p(0xa0, io_ports[6]);
	printk("Drive spun up \n");
}

int
nonpci_ide_default_irq(ide_ioreg_t base)
{
	return IDE0_IRQ;
}
/*
 * setup_ocp_ide()
 * Completes the setup of a on-chip ide controller card, once found.
 */
int __init setup_ocp_ide (ide_hwif_t *hwif)
{

	unsigned long ioaddr;
	int i, index;
	
	ide_drive_t *drive;
	ide_hwif_t *hwif2;
	struct dc_ident ident;
	ide_startstop_t startstop;
	
	if (!hwif) return 0;


	if (!request_region(REDWOOD_IDE_CMD, 0x10, "IDE"))
		return;

	if (!request_region(REDWOOD_IDE_CTRL, 2, "IDE")) {
		release_region(REDWOOD_IDE_CMD, 0x10);
		return;
	}

	ioaddr = (unsigned long) ioremap(REDWOOD_IDE_CMD, 0x10);

	hw->irq = IDE0_IRQ;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = ioaddr;
		ioaddr += 2;
	}
	hw->io_ports[IDE_CONTROL_OFFSET] =
	    (unsigned long) ioremap(REDWOOD_IDE_CTRL, 2);

	/* use DMA channel 2 for IDE DMA operations */
	hw->dma = IDE_DMACH;
#ifdef WMODE
   /*Word Mode psc(11-12)=00,pwc(13-18)=000110, phc(19-21)=010, 22=1, 30=1  ----  0xCB02*/

    dma_ch.mode	=TM_S_MM;	  /* xfer from peripheral to mem */
    dma_ch.td	= DMA_TD;
    dma_ch.buffer_enable = FALSE;
    dma_ch.tce_enable = FALSE;
    dma_ch.etd_output = FALSE;
    dma_ch.pce = FALSE;
    dma_ch.pl = EXTERNAL_PERIPHERAL;    /* no op */
    dma_ch.pwidth = PW_16;
    dma_ch.dai = TRUE;
    dma_ch.sai = FALSE;
    dma_ch.psc = 0;                      /* set the max setup cycles */
    dma_ch.pwc = 6;                     /* set the max wait cycles  */
    dma_ch.phc = 2;                      /* set the max hold cycles  */
    dma_ch.cp = PRIORITY_LOW;
    dma_ch.int_enable = FALSE;
    dma_ch.ch_enable = FALSE;		/* No chaining */
    dma_ch.tcd_disable = TRUE;		/* No chaining */
#else
/*Line Mode psc(11-12)=00,pwc(13-18)=000001, phc(19-21)=010, 22=1, 30=1  ----  0x2B02*/

   dma_ch.mode	=DMA_MODE_MM_DEVATSRC;	  /* xfer from peripheral to mem */
   dma_ch.td	= DMA_TD;
   dma_ch.buffer_enable = FALSE;
    dma_ch.tce_enable = FALSE;
    dma_ch.etd_output = FALSE;
    dma_ch.pce = FALSE;
    dma_ch.pl = EXTERNAL_PERIPHERAL;    /* no op */
    dma_ch.pwidth = PW_64;		/* Line mode on stbs */
    dma_ch.dai = TRUE;
    dma_ch.sai = FALSE;
    dma_ch.psc = 0;                      /* set the max setup cycles */
    dma_ch.pwc = 1;                     /* set the max wait cycles  */
    dma_ch.phc = 2;                      /* set the max hold cycles  */
    dma_ch.cp = PRIORITY_LOW;
    dma_ch.int_enable = FALSE;
    dma_ch.ch_enable = FALSE;		/* No chaining */
    dma_ch.tcd_disable = TRUE;		/* No chaining */

#endif
    if (hw_init_dma_channel(IDE_DMACH, &dma_ch) != DMA_STATUS_GOOD)
        return -EBUSY;

	/* init CIC select2 reg to connect external DMA port 3 to internal
	 * DMA channel 2
	 */
       map_dma_port(IDE_DMACH,EXT_DMA_3,DMA_CHAN_2); 

	index = 0;

	ide_hwifs[index].tuneproc = &ocp_ide_tune_drive;
	ide_hwifs[index].drives[0].autotune = 1;
#ifdef CONFIG_BLK_DEV_IDEDMA
	ide_hwifs[index].autodma = 1;
	ide_hwifs[index].dmaproc = &ocp_ide_dmaproc;
#endif
	ide_hwifs[index].speedproc = &ocp_ide_tune_chipset;
	ide_hwifs[index].noprobe = 0;

	memcpy(ide_hwifs[index].io_ports, hw->io_ports, sizeof (hw->io_ports));
	ide_hwifs[index].irq = hw->irq;
	ibm4xx_ide_spinup(index);
}

void __init ocp_ide_init(void)
{
	unsigned int	index;
	ide_hwif_t	*hwif;

	for (index = 0; index < MAX_HWIFS; index++) {
		hwif = &ide_hwifs[index];
			setup_ocp_ide(hwif);
	}
}

static int __devinit ocp_ide_probe(struct ocp_device *pdev)
{
	printk("IBM IDE driver version %s\n", IDEVR);

	DBG("Vendor:%x Device:%x.%d @%p irq:%d\n",pdev->vendor, pdev->device,pdev->num,pdev->paddr,pdev->irq);

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

void __init ide_scan_ocpdev (struct ocp_device *dev)
{

}

void __init ide_scan_ocpbus (int scan_direction)
{
	struct ocp_device *dev;

	if (!scan_direction) {
		ocp_for_each_dev(dev) {
			ide_scan_ocpdev(dev);
		}
	} else {
		ocp_for_each_dev_reverse(dev) {
			ide_scan_ocpdev(dev);
		}
	}
}

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


