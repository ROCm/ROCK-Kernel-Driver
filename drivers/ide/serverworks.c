/*
 * linux/drivers/ide/serverworks.c		Version 0.6	05 April 2002
 *
 * Copyright (C) 1998-2000 Michel Aubry
 * Copyright (C) 1998-2000 Andrzej Krzysztofowicz
 * Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 * Portions copyright (c) 2001 Sun Microsystems
 *
 *
 * RCC/ServerWorks IDE driver for Linux
 *
 *   OSB4: `Open South Bridge' IDE Interface (fn 1)
 *         supports UDMA mode 2 (33 MB/s)
 *
 *   CSB5: `Champion South Bridge' IDE Interface (fn 1)
 *         all revisions support UDMA mode 4 (66 MB/s)
 *         revision A2.0 and up support UDMA mode 5 (100 MB/s)
 *
 *         *** The CSB5 does not provide ANY register ***
 *         *** to detect 80-conductor cable presence. ***
 *
 *   CSB6: `Champion South Bridge' IDE Interface (optional: third channel)
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/io.h>

#include "ide_modes.h"

#define DISPLAY_SVWKS_TIMINGS	1
#undef SVWKS_DEBUG_DRIVE_INFO

#if defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

#define SVWKS_MAX_DEVS		2
static struct pci_dev *svwks_devs[SVWKS_MAX_DEVS];
static int n_svwks_devs;

static byte svwks_revision = 0;

static int svwks_get_info(char *, char **, off_t, int);
extern int (*svwks_display_info)(char *, char **, off_t, int); /* ide-proc.c */

static int svwks_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	int i;

	p += sprintf(p, "\n                             "
			"ServerWorks OSB4/CSB5/CSB6\n");

	for (i = 0; i < n_svwks_devs; i++) {
		struct pci_dev *dev = svwks_devs[i];
		u32 bibma = pci_resource_start(dev, 4);
		u32 reg40, reg44;
		u16 reg48, reg56;
		u8  reg54, c0=0, c1=0;

		pci_read_config_dword(dev, 0x40, &reg40);
		pci_read_config_dword(dev, 0x44, &reg44);
		pci_read_config_word(dev, 0x48, &reg48);
		pci_read_config_byte(dev, 0x54, &reg54);
		pci_read_config_word(dev, 0x56, &reg56);

		/*
		 * at that point bibma+0x2 et bibma+0xa are byte registers
		 * to investigate:
		 */
		c0 = inb_p((unsigned short)bibma + 0x02);
		c1 = inb_p((unsigned short)bibma + 0x0a);

		switch(dev->device) {
			case PCI_DEVICE_ID_SERVERWORKS_CSB6IDE:
				p += sprintf(p, "\n                            "
					"ServerWorks CSB6 Chipset (rev %02x)\n",
					svwks_revision);
				break;
			case PCI_DEVICE_ID_SERVERWORKS_CSB5IDE:
				p += sprintf(p, "\n                            "
					"ServerWorks CSB5 Chipset (rev %02x)\n",
					svwks_revision);
				break;
			case PCI_DEVICE_ID_SERVERWORKS_OSB4IDE:
				p += sprintf(p, "\n                            "
					"ServerWorks OSB4 Chipset (rev %02x)\n",
					svwks_revision);
				break;
			default:
				p += sprintf(p, "\n                            "
					"ServerWorks %04x Chipset (rev %02x)\n",
					dev->device, svwks_revision);
				break;
		}

		p += sprintf(p, "------------------------------- "
				"General Status "
				"---------------------------------\n");
		p += sprintf(p, "--------------- Primary Channel "
				"---------------- Secondary Channel "
				"-------------\n");
		p += sprintf(p, "                %sabled                         %sabled\n",
				(c0&0x80) ? "dis" : " en",
				(c1&0x80) ? "dis" : " en");
		p += sprintf(p, "--------------- drive0 --------- drive1 "
				"-------- drive0 ---------- drive1 ------\n");
		p += sprintf(p, "DMA enabled:    %s              %s"
				"             %s               %s\n",
			(c0&0x20) ? "yes" : "no ",
			(c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ",
			(c1&0x40) ? "yes" : "no " );
		p += sprintf(p, "UDMA enabled:   %s              %s"
				"             %s               %s\n",
			(reg54 & 0x01) ? "yes" : "no ",
			(reg54 & 0x02) ? "yes" : "no ",
			(reg54 & 0x04) ? "yes" : "no ",
			(reg54 & 0x08) ? "yes" : "no " );
		p += sprintf(p, "UDMA enabled:   %s                %s"
				"               %s                 %s\n",
			((reg56&0x0005)==0x0005)?"5":
				((reg56&0x0004)==0x0004)?"4":
				((reg56&0x0003)==0x0003)?"3":
				((reg56&0x0002)==0x0002)?"2":
				((reg56&0x0001)==0x0001)?"1":
				((reg56&0x000F))?"?":"0",
			((reg56&0x0050)==0x0050)?"5":
				((reg56&0x0040)==0x0040)?"4":
				((reg56&0x0030)==0x0030)?"3":
				((reg56&0x0020)==0x0020)?"2":
				((reg56&0x0010)==0x0010)?"1":
				((reg56&0x00F0))?"?":"0",
			((reg56&0x0500)==0x0500)?"5":
				((reg56&0x0400)==0x0400)?"4":
				((reg56&0x0300)==0x0300)?"3":
				((reg56&0x0200)==0x0200)?"2":
				((reg56&0x0100)==0x0100)?"1":
				((reg56&0x0F00))?"?":"0",
			((reg56&0x5000)==0x5000)?"5":
				((reg56&0x4000)==0x4000)?"4":
				((reg56&0x3000)==0x3000)?"3":
				((reg56&0x2000)==0x2000)?"2":
				((reg56&0x1000)==0x1000)?"1":
				((reg56&0xF000))?"?":"0");
		p += sprintf(p, "DMA enabled:    %s                %s"
				"               %s                 %s\n",
			((reg44&0x00002000)==0x00002000)?"2":
				((reg44&0x00002100)==0x00002100)?"1":
				((reg44&0x00007700)==0x00007700)?"0":
				((reg44&0x0000FF00)==0x0000FF00)?"X":"?",
			((reg44&0x00000020)==0x00000020)?"2":
				((reg44&0x00000021)==0x00000021)?"1":
				((reg44&0x00000077)==0x00000077)?"0":
				((reg44&0x000000FF)==0x000000FF)?"X":"?",
			((reg44&0x20000000)==0x20000000)?"2":
				((reg44&0x21000000)==0x21000000)?"1":
				((reg44&0x77000000)==0x77000000)?"0":
				((reg44&0xFF000000)==0xFF000000)?"X":"?",
			((reg44&0x00200000)==0x00200000)?"2":
				((reg44&0x00210000)==0x00210000)?"1":
				((reg44&0x00770000)==0x00770000)?"0":
				((reg44&0x00FF0000)==0x00FF0000)?"X":"?");

		p += sprintf(p, "PIO  enabled:   %s                %s"
				"               %s                 %s\n",
			((reg40&0x00002000)==0x00002000)?"4":
				((reg40&0x00002200)==0x00002200)?"3":
				((reg40&0x00003400)==0x00003400)?"2":
				((reg40&0x00004700)==0x00004700)?"1":
				((reg40&0x00005D00)==0x00005D00)?"0":"?",
			((reg40&0x00000020)==0x00000020)?"4":
				((reg40&0x00000022)==0x00000022)?"3":
				((reg40&0x00000034)==0x00000034)?"2":
				((reg40&0x00000047)==0x00000047)?"1":
				((reg40&0x0000005D)==0x0000005D)?"0":"?",
			((reg40&0x20000000)==0x20000000)?"4":
				((reg40&0x22000000)==0x22000000)?"3":
				((reg40&0x34000000)==0x34000000)?"2":
				((reg40&0x47000000)==0x47000000)?"1":
				((reg40&0x5D000000)==0x5D000000)?"0":"?",
			((reg40&0x00200000)==0x00200000)?"4":
				((reg40&0x00220000)==0x00220000)?"3":
				((reg40&0x00340000)==0x00340000)?"2":
				((reg40&0x00470000)==0x00470000)?"1":
				((reg40&0x005D0000)==0x005D0000)?"0":"?");

	}
	p += sprintf(p, "\n");

	return p-buffer;	 /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS) */

#define SVWKS_CSB5_REVISION_NEW	0x92 /* min PCI_REVISION_ID for UDMA5 (A2.0) */

#define SVWKS_CSB6_REVISION	0xa0 /* min PCI_REVISION_ID for UDMA4 (A1.0) */

byte svwks_proc = 0;

static struct pci_dev *isa_dev;

static byte svwks_ratemask (ide_drive_t *drive)
{
	struct pci_dev *dev     = HWIF(drive)->pci_dev;
	byte mode		= 0;

	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		u32 reg = 0;
		mode &= ~0x01;
		if (isa_dev)
			pci_read_config_dword(isa_dev, 0x64, &reg);
		if ((reg & 0x00004000) == 0x00004000)
			mode |= 0x01;
	} else if (svwks_revision < SVWKS_CSB5_REVISION_NEW) {
		mode |= 0x01;
	} else if (svwks_revision >= SVWKS_CSB5_REVISION_NEW) {
		u8 btr =0;
		pci_read_config_byte(dev, 0x5A, &btr);
		mode |= btr;
		if (!eighty_ninty_three(drive))
			mode &= ~0x02;
	}
	if ((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		mode = 0x02;
	mode &= ~0xFC;
	return (mode);
}

static byte svwks_ratefilter (ide_drive_t *drive, byte speed)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	byte mode = svwks_ratemask(drive);
	
	switch(mode) {
		case 0x04:	while (speed > XFER_UDMA_6) speed--; break;
		case 0x03:	while (speed > XFER_UDMA_5) speed--; break;
		case 0x02:	while (speed > XFER_UDMA_4) speed--; break;
		case 0x01:	while (speed > XFER_UDMA_2) speed--; break;
		case 0x00:
		default:	while (speed > XFER_MW_DMA_2) speed--; break;
			break;
	}
#else
	while (speed > XFER_PIO_4) speed--;
#endif /* CONFIG_BLK_DEV_IDEDMA */
//	printk("%s: mode == %02x speed == %02x\n", drive->name, mode, speed);
	return speed;
}

static byte svwks_csb_check (struct pci_dev *dev)
{
	switch (dev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_CSB5IDE:
		case PCI_DEVICE_ID_SERVERWORKS_CSB6IDE:
			return 1;
		default:
			break;
	}
	return 0;
}
static int svwks_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	byte udma_modes[]	= { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
	byte dma_modes[]	= { 0x77, 0x21, 0x20 };
	byte pio_modes[]	= { 0x5d, 0x47, 0x34, 0x22, 0x20 };

	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte unit		= (drive->select.b.unit & 0x01);
	byte csb5		= svwks_csb_check(dev);

	byte drive_pci		= 0x00;
	byte drive_pci2		= 0x00;
	byte drive_pci3		= hwif->channel ? 0x57 : 0x56;

	byte ultra_enable	= 0x00;
	byte ultra_timing	= 0x00;
	byte dma_timing		= 0x00;
	byte pio_timing		= 0x00;
	unsigned short csb5_pio	= 0x00;

	byte pio	= ide_get_best_pio_mode(drive, 255, 5, NULL);
	byte speed	= svwks_ratefilter(drive, xferspeed);

        switch (drive->dn) {
		case 0: drive_pci = 0x41; drive_pci2 = 0x45; break;
		case 1: drive_pci = 0x40; drive_pci2 = 0x44; break;
		case 2: drive_pci = 0x43; drive_pci2 = 0x47; break;
		case 3: drive_pci = 0x42; drive_pci2 = 0x46; break;
		default:
			return -1;
	}

	pci_read_config_byte(dev, drive_pci, &pio_timing);
	pci_read_config_byte(dev, drive_pci2, &dma_timing);
	pci_read_config_byte(dev, drive_pci3, &ultra_timing);
	pci_read_config_word(dev, 0x4A, &csb5_pio);
	pci_read_config_byte(dev, 0x54, &ultra_enable);

	pio_timing	&= ~0xFF;
	dma_timing	&= ~0xFF;
	ultra_timing	&= ~(0x0F << (4*unit));
	ultra_enable	&= ~(0x01 << drive->dn);
	csb5_pio	&= ~(0x0F << (4*drive->dn));

	switch(speed) {
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			pio_timing |= pio_modes[speed - XFER_PIO_0];
			csb5_pio   |= ((speed - XFER_PIO_0) << (4*drive->dn));
			break;

#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			pio_timing |= pio_modes[pio];
			csb5_pio   |= (pio << (4*drive->dn));
			dma_timing |= dma_modes[speed - XFER_MW_DMA_0];
			break;

		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			pio_timing   |= pio_modes[pio];
			csb5_pio     |= (pio << (4*drive->dn));
			dma_timing   |= dma_modes[2];
			ultra_timing |= ((udma_modes[speed - XFER_UDMA_0]) << (4*unit));
			ultra_enable |= (0x01 << drive->dn);
#endif
		default:
			break;
	}

	pci_write_config_byte(dev, drive_pci, pio_timing);
	if (csb5)
		pci_write_config_word(dev, 0x4A, csb5_pio);

#ifdef CONFIG_BLK_DEV_IDEDMA
	pci_write_config_byte(dev, drive_pci2, dma_timing);
	pci_write_config_byte(dev, drive_pci3, ultra_timing);
	pci_write_config_byte(dev, 0x54, ultra_enable);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	return (ide_config_drive_speed(drive, speed));
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;
	byte timing, speed, pio;

	pio = ide_get_best_pio_mode(drive, 255, 5, NULL);

	if (xfer_pio> 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0)
		for (xfer_pio = 5;
			xfer_pio>0 &&
			drive->id->eide_pio_iordy>eide_pio_timing[xfer_pio];
			xfer_pio--);
	else
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 :
			   (drive->id->tPIO & 2) ? 0x02 :
			   (drive->id->tPIO & 1) ? 0x01 : xfer_pio;

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

	switch(timing) {
		case 4: speed = XFER_PIO_4;break;
		case 3: speed = XFER_PIO_3;break;
		case 2: speed = XFER_PIO_2;break;
		case 1: speed = XFER_PIO_1;break;
		default:
			speed = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
			break;
	}
	(void) svwks_tune_chipset(drive, speed);
	drive->current_speed = speed;
}

static void svwks_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) svwks_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte mode		= svwks_ratemask(drive);
	byte speed, dma		= 1;

	if (HWIF(drive)->pci_dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE)
		mode = 0;

	switch(mode) {
		case 0x04:
			if (id->dma_ultra & 0x0040)
				{ speed = XFER_UDMA_6; break; }
		case 0x03:
			if (id->dma_ultra & 0x0020)
				{ speed = XFER_UDMA_5; break; }
		case 0x02:
			if (id->dma_ultra & 0x0010)
				{ speed = XFER_UDMA_4; break; }
			if (id->dma_ultra & 0x0008)
				{ speed = XFER_UDMA_3; break; }
		case 0x01:
			if (id->dma_ultra & 0x0004)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0002)
				{ speed = XFER_UDMA_1; break; }
			if (id->dma_ultra & 0x0001)
				{ speed = XFER_UDMA_0; break; }
			if (id->dma_mword & 0x0004)
				{ speed = XFER_MW_DMA_2; break; }
			if (id->dma_mword & 0x0002)
				{ speed = XFER_MW_DMA_1; break; }
			if (id->dma_mword & 0x0001)
				{ speed = XFER_MW_DMA_0; break; }
#if 0
			if (id->dma_1word & 0x0004)
				{ speed = XFER_SW_DMA_2; break; }
			if (id->dma_1word & 0x0002)
				{ speed = XFER_SW_DMA_1; break; }
			if (id->dma_1word & 0x0001)
				{ speed = XFER_SW_DMA_0; break; }
#endif
		default:
			speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);
			dma = 0;
			break;
	}

	(void) svwks_tune_chipset(drive, speed);

//	return ((int) (dma) ? ide_dma_on : ide_dma_off_quietly);
	return ((int)	((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_dma_action_t dma_func = ide_dma_on;

	drive->init_speed = 0;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x003F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		config_chipset_for_pio(drive);
		//	HWIF(drive)->tuneproc(drive, 5);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

static int svwks_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_end:
		{
			ide_hwif_t *hwif		= HWIF(drive);
			unsigned long dma_base		= hwif->dma_base;
	
			if(IN_BYTE(dma_base+0x02)&1)
			{
#if 0		
				int i;
				printk(KERN_ERR "Curious - OSB4 thinks the DMA is still running.\n");
				for(i=0;i<10;i++)
				{
					if(!(IN_BYTE(dma_base+0x02)&1))
					{
						printk(KERN_ERR "OSB4 now finished.\n");
						break;
					}
					udelay(5);
				}
#endif		
				printk(KERN_CRIT "Serverworks OSB4 in impossible state.\n");
				printk(KERN_CRIT "Disable UDMA or if you are using Seagate then try switching disk types\n");
				printk(KERN_CRIT "on this controller. Please report this event to osb4-bug@ide.cabal.tm\n");
#if 0		
				/* Panic might sys_sync -> death by corrupt disk */
				panic("OSB4: continuing might cause disk corruption.\n");
#else
				printk(KERN_CRIT "OSB4: continuing might cause disk corruption.\n");
				while(1)
					cpu_relax();
#endif				
			}
			/* and drop through */
		}
		default:
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

unsigned int __init pci_init_svwks (struct pci_dev *dev, const char *name)
{
	unsigned int reg;
	byte btr;

	/* save revision id to determine DMA capability */
	pci_read_config_byte(dev, PCI_REVISION_ID, &svwks_revision);

	/* force Master Latency Timer value to 64 PCICLKs */
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x40);

	/* OSB4 : South Bridge and IDE */
	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		isa_dev = pci_find_device(PCI_VENDOR_ID_SERVERWORKS,
			  PCI_DEVICE_ID_SERVERWORKS_OSB4, NULL);
		if (isa_dev) {
			pci_read_config_dword(isa_dev, 0x64, &reg);
			reg &= ~0x00002000; /* disable 600ns interrupt mask */
			reg |=  0x00004000; /* enable UDMA/33 support */
			pci_write_config_dword(isa_dev, 0x64, reg);
		}
	}

	/* setup CSB5/CSB6 : South Bridge and IDE option RAID */
	else if ((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE) ||
		 (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE)) {
		/* Third Channel Test */
		if (!(PCI_FUNC(dev->devfn) & 1)) {
#if 1
			struct pci_dev * findev = NULL;
			unsigned int reg4c = 0;
			findev = pci_find_device(PCI_VENDOR_ID_SERVERWORKS,
				PCI_DEVICE_ID_SERVERWORKS_CSB5, NULL);
			if (findev) {
				pci_read_config_dword(findev, 0x4C, &reg4c);
				reg4c &= ~0x000007FF;
				reg4c |=  0x00000040;
				reg4c |=  0x00000020;
				pci_write_config_dword(findev, 0x4C, reg4c);
			}
#endif
			outb_p(0x06, 0x0c00);
			dev->irq = inb_p(0x0c01);
#if 1
			/* WE need to figure out how to get the correct one */
			printk("%s: interrupt %d\n", name, dev->irq);
			if (dev->irq != 0x0B)
				dev->irq = 0x0B;
#endif
		} else {
			/*
			 * This is a device pin issue on CSB6.
			 * Since there will be a future raid mode,
			 * early versions of the chipset require the
			 * interrupt pin to be set, and it is a compatablity
			 * mode issue.
			 */
			dev->irq = 0;
		}
		pci_write_config_dword(dev, 0x40, 0x99999999);
		pci_write_config_dword(dev, 0x44, 0xFFFFFFFF);
		/* setup the UDMA Control register
		 *
		 * 1. clear bit 6 to enable DMA
		 * 2. enable DMA modes with bits 0-1
		 * 	00 : legacy
		 * 	01 : udma2
		 * 	10 : udma2/udma4
		 * 	11 : udma2/udma4/udma5
		 */
		pci_read_config_byte(dev, 0x5A, &btr);
		btr &= ~0x40;
		if (!(PCI_FUNC(dev->devfn) & 1))
			btr |= 0x2;
		else
			btr |= (svwks_revision >= SVWKS_CSB5_REVISION_NEW) ? 0x3 : 0x2;
		pci_write_config_byte(dev, 0x5A, btr);
	}

	svwks_devs[n_svwks_devs++] = dev;

#if defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!svwks_proc) {
		svwks_proc = 1;
		svwks_display_info = &svwks_get_info;
	}
#endif /* DISPLAY_SVWKS_TIMINGS && CONFIG_PROC_FS */

	return (dev->irq) ? dev->irq : 0;
}

static unsigned int __init ata66_svwks_svwks (ide_hwif_t *hwif)
{
//	struct pci_dev *dev = hwif->pci_dev;
//	return 0;
	return 1;
}

/* On Dell PowerEdge servers with a CSB5/CSB6, the top two bits
 * of the subsystem device ID indicate presence of an 80-pin cable.
 * Bit 15 clear = secondary IDE channel does not have 80-pin cable.
 * Bit 15 set   = secondary IDE channel has 80-pin cable.
 * Bit 14 clear = primary IDE channel does not have 80-pin cable.
 * Bit 14 set   = primary IDE channel has 80-pin cable.
 */
static unsigned int __init ata66_svwks_dell (ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL &&
	    dev->vendor	== PCI_VENDOR_ID_SERVERWORKS &&
	    (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE ||
	     dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE))
		return ((1 << (hwif->channel + 14)) &
			dev->subsystem_device) ? 1 : 0;
	return 0;
}

/* Sun Cobalt Alpine hardware avoids the 80-pin cable
 * detect issue by attaching the drives directly to the board.
 * This check follows the Dell precedent (how scary is that?!)
 *
 * WARNING: this only works on Alpine hardware!
 */
static unsigned int __init ata66_svwks_cobalt (ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SUN &&
	    dev->vendor	== PCI_VENDOR_ID_SERVERWORKS &&
	    dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE)
		return ((1 << (hwif->channel + 14)) &
			dev->subsystem_device) ? 1 : 0;
	return 0;
}

unsigned int __init ata66_svwks (ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;

	/* Server Works */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SERVERWORKS)
		return ata66_svwks_svwks (hwif);
	
	/* Dell PowerEdge */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL)
		return ata66_svwks_dell (hwif);

	/* Cobalt Alpine */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SUN)
		return ata66_svwks_cobalt (hwif);

	return 0;
}

void __init ide_init_svwks (ide_hwif_t *hwif)
{
	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->tuneproc = &svwks_tune_drive;
	hwif->speedproc = &svwks_tune_chipset;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;

	if (!hwif->dma_base)
		return;

#ifdef CONFIG_BLK_DEV_IDEDMA
	hwif->dmaproc = &svwks_dmaproc;
# ifdef CONFIG_IDEDMA_AUTO
	if (!noautodma)
		hwif->autodma = 1;
# endif /* CONFIG_IDEDMA_AUTO */
#endif /* !CONFIG_BLK_DEV_IDEDMA */
}

/*
 * We allow the BM-DMA driver to only work on enabled interfaces.
 */
void __init ide_dmacapable_svwks (ide_hwif_t *hwif, unsigned long dmabase)
{
	struct pci_dev *dev = hwif->pci_dev;
	if ((dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) &&
	    (!(PCI_FUNC(dev->devfn) & 1)) && (hwif->channel))
		return;
#if 0
	if (svwks_revision == (SVWKS_CSB5_REVISION_NEW + 1)) {
		if (hwif->mate && hwif->mate->dma_base) {
	                dmabase = hwif->mate->dma_base - (hwif->channel ? 0 : 8);
		} else {
			dmabase = pci_resource_start(dev, 4);
			if (!dmabase) {
				printk("%s: dma_base is invalid (0x%04lx)\n",
					hwif->name, dmabase);
				dmabase = 0;
			}
		}
	}
#endif
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device (struct pci_dev *dev, ide_pci_device_t *d);

void __init fixup_device_csb6 (struct pci_dev *dev, ide_pci_device_t *d)
{
	if (!(PCI_FUNC(dev->devfn) & 1)) {
		d->bootable = NEVER_BOARD;
	}

	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d->name, dev->bus->number, dev->devfn);
	ide_setup_pci_device(dev, d);
}
 
