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
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/it8172/it8172_int.h>

#include "ata-timing.h"
#include "pcihost.h"


/* FIXME: fix locking  --bkz */
static void it8172_tune_drive (struct ata_device *drive, u8 pio)
{
	struct pci_dev *dev = drive->channel->pci_dev;
    unsigned long flags;
    u16 drive_enables;
    u32 drive_timing;
    int is_slave	= (&drive->channel->drives[1] == drive);
    
	if (pio == 255)
		pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO) - XFER_PIO_0;
	else
		pio = min_t(byte, pio, 4);

	pci_read_config_word(dev, master_port, &master_data);
	pci_read_config_dword(dev, slave_port, &slave_data);

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

    save_flags(flags);
    cli();
	pci_write_config_word(dev, master_port, master_data);
    restore_flags(flags);
}

#if defined(CONFIG_BLK_DEV_IDEDMA) && defined(CONFIG_IT8172_TUNING)
/*
 *
 */
static u8 it8172_dma_2_pio(u8 xfer_rate)
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

static int it8172_tune_chipset(struct ata_device *drive, u8 speed)
{
    struct ata_channel *hwif = drive->channel;
    struct pci_dev *dev	= hwif->pci_dev;
    int a_speed		= 3 << (drive->dn * 4);
    int u_flag		= 1 << drive->dn;
    int u_speed		= 0;
    int err		= 0;
	u8 reg48, reg4a;

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

	return ide_config_drive_speed(drive, speed);
}
#endif /* defined(CONFIG_BLK_DEV_IDEDMA) && (CONFIG_IT8172_TUNING) */


static unsigned int __init pci_init_it8172(struct pci_dev *dev)
{
	u8 progif;

    /*
     * Place both IDE interfaces into PCI "native" mode
     */
	pci_read_config_byte(dev, PCI_CLASS_PROG, &progif);
	pci_write_config_byte(dev, PCI_CLASS_PROG, progif | 0x05);

    return IT8172_IDE_IRQ;
}


static void __init ide_init_it8172(struct ata_channel *hwif)
{
    struct pci_dev* dev = hwif->pci_dev;
    unsigned long cmdBase, ctrlBase;

    hwif->tuneproc = &it8172_tune_drive;
    hwif->drives[0].autotune = 1;
    hwif->drives[1].autotune = 1;

    if (!hwif->dma_base)
	return;

# ifdef CONFIG_IT8172_TUNING
	hwif->modes_map = XFER_EPIO | XFER_SWDMA | XFER_MWDMA | XFER_UDMA;
	hwif->udma_setup = udma_generic_setup;
    hwif->speedproc = &it8172_tune_chipset;
# endif

    cmdBase = dev->resource[0].start;
    ctrlBase = dev->resource[1].start;

    ide_init_hwif_ports(&hwif->hw, cmdBase, ctrlBase | 2, NULL);
    memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
    hwif->noprobe = 0;
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
        .vendor = PCI_VENDOR_ID_ITE,
	.device = PCI_DEVICE_ID_ITE_IT8172G,
	.init_chipset = pci_init_it8172,
	.init_channel = ide_init_it8172,
	.exnablebits = {{0x00,0x00,0x00}, {0x40,0x00,0x01} },
	.bootable = ON_BOARD
};

int __init init_it8172(void)
{
	ata_register_chipset(&chipset);

        return 0;
}
