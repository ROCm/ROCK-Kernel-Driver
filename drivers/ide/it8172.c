/*
 *
 * BRIEF MODULE DESCRIPTION
 *      IT8172 IDE controller support
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/it8172/it8172_int.h>

#include "ide_modes.h"

/*
 * Prototypes
 */
static byte it8172_ratemask (ide_drive_t *drive);
static byte it8172_ratefilter (ide_drive_t *drive, byte speed);
static void it8172_tune_drive (ide_drive_t *drive, byte pio);
static byte it8172_dma_2_pio (byte xfer_rate);
static int it8172_tune_chipset (ide_drive_t *drive, byte xferspeed);
#ifdef CONFIG_BLK_DEV_IDEDMA
static int it8172_config_chipset_for_dma (ide_drive_t *drive);
static int it8172_dmaproc(ide_dma_action_t func, ide_drive_t *drive);
#endif
unsigned int __init pci_init_it8172 (struct pci_dev *dev, const char *name);
void __init ide_init_it8172 (ide_hwif_t *hwif);

static byte it8172_ratemask (ide_drive_t *drive)
{
	byte mode	= 0x00;
#if 1
	mode |= 0x01;
#endif
	return (mode &= ~0xF8);
}

static byte it8172_ratefilter (ide_drive_t *drive, byte speed)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	byte mode = it8172_ratemask(drive);

	switch(mode) {
		case 0x04:	// while (speed > XFER_UDMA_6) speed--; break;
		case 0x03:	// while (speed > XFER_UDMA_5) speed--; break;
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

static void it8172_tune_drive (ide_drive_t *drive, byte pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= (hwif->drives[1] == drive);
	unsigned long flags;
	u16 drive_enables;
	u32 drive_timing;

	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	spin_lock_irqsave(&ide_lock, flags);
	pci_read_config_word(dev, 0x40, &drive_enables);
	pci_read_config_dword(dev, 0x44, &drive_timing);

	/*
	 * FIX! The DIOR/DIOW pulse width and recovery times in port 0x44
	 * are being left at the default values of 8 PCI clocks (242 nsec
	 * for a 33 MHz clock). These can be safely shortened at higher
	 * PIO modes. The DIOR/DIOW pulse width and recovery times only
	 * apply to PIO modes, not to the DMA modes.
	 */

	/*
	 * Enable port 0x44. The IT8172G spec is confused; it calls
	 * this register the "Slave IDE Timing Register", but in fact,
	 * it controls timing for both master and slave drives.
	 */
	drive_enables |= 0x4000;

	if (is_slave) {
		drive_enables &= 0xc006;
		if (pio > 1)
			/* enable prefetch and IORDY sample-point */
			drive_enables |= 0x0060;
	} else {
		drive_enables &= 0xc060;
		if (pio > 1)
			/* enable prefetch and IORDY sample-point */
			drive_enables |= 0x0006;
	}

	pci_write_config_word(dev, 0x40, drive_enables);
	spin_unlock_irqrestore(&ide_lock, flags)
}

static byte it8172_dma_2_pio (byte xfer_rate)
{
	switch(xfer_rate) {
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

static int it8172_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte speed		= it8172_ratefilter(drive, xferspeed);
	int a_speed		= 3 << (drive->dn * 4);
	int u_flag		= 1 << drive->dn;
	int u_speed		= 0;
	byte reg48, reg4a;

	pci_read_config_byte(dev, 0x48, &reg48);
	pci_read_config_byte(dev, 0x4a, &reg4a);

    /*
     * Setting the DMA cycle time to 2 or 3 PCI clocks (60 and 91 nsec
     * at 33 MHz PCI clock) seems to cause BadCRC errors during DMA
     * transfers on some drives, even though both numbers meet the minimum
     * ATAPI-4 spec of 73 and 54 nsec for UDMA 1 and 2 respectively.
     * So the faster times are just commented out here. The good news is
     * that the slower cycle time has very little affect on transfer
     * performance.
     */
    
	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_4:
		case XFER_UDMA_2:	//u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_5:
		case XFER_UDMA_3:
		case XFER_UDMA_1:	//u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive->dn * 4); break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:	break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_0:	break;
		default:		return -1;
	}

	if (speed >= XFER_UDMA_0) {
		pci_write_config_byte(dev, 0x48, reg48 | u_flag);
		reg4a &= ~a_speed;
		pci_write_config_byte(dev, 0x4a, reg4a | u_speed);
	} else {
		pci_write_config_byte(dev, 0x48, reg48 & ~u_flag);
		pci_write_config_byte(dev, 0x4a, reg4a & ~a_speed);
	}

	it8172_tune_drive(drive, it8172_dma_2_pio(speed));
	return (ide_config_drive_speed(drive, speed));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int it8172_config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte mode		= it8172_ratemask(drive);
	byte speed, tspeed, dma = 1;

	switch(mode) {
		case 0x01:
			if (id->dma_ultra & 0x0040)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0020)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0010)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0008)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0004)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0002)
				{ speed = XFER_UDMA_1; break; }
			if (id->dma_ultra & 0x0001)
				{ speed = XFER_UDMA_0; break; }
		case 0x00:
			if (id->dma_mword & 0x0004)
				{ speed = XFER_MW_DMA_2; break; }
			if (id->dma_mword & 0x0002)
				{ speed = XFER_MW_DMA_1; break; }
			if (id->dma_1word & 0x0004)
				{ speed = XFER_SW_DMA_2; break; }
		default:
			tspeed = ide_get_best_pio_mode(drive, 255, 4, NULL);
			speed = it8172_dma_2_pio(XFER_PIO_0 + tspeed);
			dma = 0;
			break;
	}

	(void) it8172_tune_chipset(drive, speed);

//	return ((int)(dma) ? ide_dma_on : ide_dma_off_quietly);
	return ((int)((id->dma_ultra >> 11) & 7) ? ide_dma_on :
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
			if (id->dma_ultra & 0x007F) {
				/* Force if Capable UltraDMA */
				dma_func = it8172_config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x007)) {
				/* Force if Capable regular DMA modes */
				dma_func = it8172_config_chipset_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = it8172_config_chipset_for_dma(drive);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		it8172_tune_drive(drive, 5);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

static int it8172_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default :
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */


unsigned int __init pci_init_it8172 (struct pci_dev *dev, const char *name)
{
	unsigned char progif;
    
	/*
	 * Place both IDE interfaces into PCI "native" mode
	 */
	pci_read_config_byte(dev, PCI_CLASS_PROG, &progif);
	pci_write_config_byte(dev, PCI_CLASS_PROG, progif | 0x05);    

	return IT8172_IDE_IRQ;
}


void __init ide_init_it8172 (ide_hwif_t *hwif)
{
	struct pci_dev* dev = hwif->pci_dev;
	unsigned long cmdBase, ctrlBase;
    
	hwif->autodma = 0;
	hwif->tuneproc = &it8172_tune_drive;
	hwif->speedproc = &it8172_tune_chipset;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	cmdBase = dev->resource[0].start;
	ctrlBase = dev->resource[1].start;
    
	ide_init_hwif_ports(&hwif->hw, cmdBase, ctrlBase | 2, NULL);
	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
	hwif->noprobe = 0;

	if (!hwif->dma_base)
		return;

#ifdef CONFIG_BLK_DEV_IDEDMA
	hwif->dmaproc = &it8172_dmaproc;
# ifdef CONFIG_IDEDMA_AUTO
	if (!noautodma)
		hwif->autodma = 1;
# endif /* CONFIG_IDEDMA_AUTO */
#endif /* !CONFIG_BLK_DEV_IDEDMA */
}

extern void ide_setup_pci_device (struct pci_dev *dev, ide_pci_device_t *d);

void __init fixup_device_it8172 (struct pci_dev *dev, ide_pci_device_t *d)
{
        if ((!(PCI_FUNC(dev->devfn) & 1) ||
            (!((dev->class >> 8) == PCI_CLASS_STORAGE_IDE))))
                return; /* IT8172 is more than only a IDE controller */

	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d->name, dev->bus->number, dev->devfn);
	ide_setup_pci_device(dev, d);
}

