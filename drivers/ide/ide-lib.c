#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

/*
 *	IDE library routines. These are plug in code that most 
 *	drivers can use but occasionally may be weird enough
 *	to want to do their own thing with
 *
 *	Add common non I/O op stuff here. Make sure it has proper
 *	kernel-doc function headers or your patch will be rejected
 */
 

/**
 *	ide_xfer_verbose	-	return IDE mode names
 *	@xfer_rate: rate to name
 *
 *	Returns a constant string giving the name of the mode
 *	requested.
 */

char *ide_xfer_verbose (u8 xfer_rate)
{
        switch(xfer_rate) {
                case XFER_UDMA_7:	return("UDMA 7");
                case XFER_UDMA_6:	return("UDMA 6");
                case XFER_UDMA_5:	return("UDMA 5");
                case XFER_UDMA_4:	return("UDMA 4");
                case XFER_UDMA_3:	return("UDMA 3");
                case XFER_UDMA_2:	return("UDMA 2");
                case XFER_UDMA_1:	return("UDMA 1");
                case XFER_UDMA_0:	return("UDMA 0");
                case XFER_MW_DMA_2:	return("MW DMA 2");
                case XFER_MW_DMA_1:	return("MW DMA 1");
                case XFER_MW_DMA_0:	return("MW DMA 0");
                case XFER_SW_DMA_2:	return("SW DMA 2");
                case XFER_SW_DMA_1:	return("SW DMA 1");
                case XFER_SW_DMA_0:	return("SW DMA 0");
                case XFER_PIO_4:	return("PIO 4");
                case XFER_PIO_3:	return("PIO 3");
                case XFER_PIO_2:	return("PIO 2");
                case XFER_PIO_1:	return("PIO 1");
                case XFER_PIO_0:	return("PIO 0");
                case XFER_PIO_SLOW:	return("PIO SLOW");
                default:		return("XFER ERROR");
        }
}

EXPORT_SYMBOL(ide_xfer_verbose);

/**
 *	ide_dma_speed	-	compute DMA speed
 *	@drive: drive
 *	@mode; intended mode
 *
 *	Checks the drive capabilities and returns the speed to use
 *	for the transfer. Returns -1 if the requested mode is unknown
 *	(eg PIO)
 */
 
u8 ide_dma_speed(ide_drive_t *drive, u8 mode)
{
	struct hd_driveid *id   = drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	u8 speed = 0;

	if (drive->media != ide_disk && hwif->atapi_dma == 0)
		return 0;

	switch(mode) {
		case 0x04:
			if ((id->dma_ultra & 0x0040) &&
			    (id->dma_ultra & hwif->ultra_mask))
				{ speed = XFER_UDMA_6; break; }
		case 0x03:
			if ((id->dma_ultra & 0x0020) &&
			    (id->dma_ultra & hwif->ultra_mask))
				{ speed = XFER_UDMA_5; break; }
		case 0x02:
			if ((id->dma_ultra & 0x0010) &&
			    (id->dma_ultra & hwif->ultra_mask))
				{ speed = XFER_UDMA_4; break; }
			if ((id->dma_ultra & 0x0008) &&
			    (id->dma_ultra & hwif->ultra_mask))
				{ speed = XFER_UDMA_3; break; }
		case 0x01:
			if ((id->dma_ultra & 0x0004) &&
			    (id->dma_ultra & hwif->ultra_mask))
				{ speed = XFER_UDMA_2; break; }
			if ((id->dma_ultra & 0x0002) &&
			    (id->dma_ultra & hwif->ultra_mask))
				{ speed = XFER_UDMA_1; break; }
			if ((id->dma_ultra & 0x0001) &&
			    (id->dma_ultra & hwif->ultra_mask))
				{ speed = XFER_UDMA_0; break; }
		case 0x00:
			if ((id->dma_mword & 0x0004) &&
			    (id->dma_mword & hwif->mwdma_mask))
				{ speed = XFER_MW_DMA_2; break; }
			if ((id->dma_mword & 0x0002) &&
			    (id->dma_mword & hwif->mwdma_mask))
				{ speed = XFER_MW_DMA_1; break; }
			if ((id->dma_mword & 0x0001) &&
			    (id->dma_mword & hwif->mwdma_mask))
				{ speed = XFER_MW_DMA_0; break; }
			if ((id->dma_1word & 0x0004) &&
			    (id->dma_1word & hwif->swdma_mask))
				{ speed = XFER_SW_DMA_2; break; }
			if ((id->dma_1word & 0x0002) &&
			    (id->dma_1word & hwif->swdma_mask))
				{ speed = XFER_SW_DMA_1; break; }
			if ((id->dma_1word & 0x0001) &&
			    (id->dma_1word & hwif->swdma_mask))
				{ speed = XFER_SW_DMA_0; break; }
	}

//	printk("%s: %s: mode 0x%02x, speed 0x%02x\n",
//		__FUNCTION__, drive->name, mode, speed);

	return speed;
}

EXPORT_SYMBOL(ide_dma_speed);


/**
 *	ide_rate_filter		-	return best speed for mode
 *	@mode: modes available
 *	@speed: desired speed
 *
 *	Given the available DMA/UDMA mode this function returns
 *	the best available speed at or below the speed requested.
 */

u8 ide_rate_filter (u8 mode, u8 speed) 
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	static u8 speed_max[] = {
		XFER_MW_DMA_2, XFER_UDMA_2, XFER_UDMA_4,
		XFER_UDMA_5, XFER_UDMA_6
	};

//	printk("%s: mode 0x%02x, speed 0x%02x\n", __FUNCTION__, mode, speed);

	/* So that we remember to update this if new modes appear */
	if (mode > 4)
		BUG();
	return min(speed, speed_max[mode]);
#else /* !CONFIG_BLK_DEV_IDEDMA */
	return min(speed, XFER_PIO_4);
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

EXPORT_SYMBOL(ide_rate_filter);

int ide_dma_enable (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	return ((int)	((((id->dma_ultra >> 8) & hwif->ultra_mask) ||
			  ((id->dma_mword >> 8) & hwif->mwdma_mask) ||
			  ((id->dma_1word >> 8) & hwif->swdma_mask)) ? 1 : 0));
}

EXPORT_SYMBOL(ide_dma_enable);
