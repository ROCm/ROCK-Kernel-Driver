/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * linux/drivers/ide/hpt366.c		Version 0.22	20 Sep 2001
 *
 * Copyright (C) 1999-2000		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Thanks to HighPoint Technologies for their assistance, and hardware.
 * Special Thanks to Jon Burchmore in SanDiego for the deep pockets, his
 * donation of an ABit BP6 mainboard, processor, and memory acellerated
 * development and support.
 *
 * Note that final HPT370 support was done by force extraction of GPL.
 *
 * - add function for getting/setting power status of drive
 * - the HPT370's state machine can get confused. reset it before each dma
 *   xfer to prevent that from happening.
 * - reset state engine whenever we get an error.
 * - check for busmaster state at end of dma.
 * - use new highpoint timings.
 * - detect bus speed using highpoint register.
 * - use pll if we don't have a clock table. added a 66MHz table that's
 *   just 2x the 33MHz table.
 * - removed turnaround. NOTE: we never want to switch between pll and
 *   pci clocks as the chip can glitch in those cases. the highpoint
 *   approved workaround slows everything down too much to be useful. in
 *   addition, we would have to serialize access to each chip.
 *	Adrian Sun <a.sun@sun.com>
 *
 * add drive timings for 66MHz PCI bus,
 * fix ATA Cable signal detection, fix incorrect /proc info
 * add /proc display for per-drive PIO/DMA/UDMA mode and
 * per-channel ATA-33/66 Cable detect.
 *	Duncan Laurie <void@sun.com>
 *
 * fixup /proc output for multiple controllers
 *	Tim Hockin <thockin@sun.com>
 *
 * On hpt366:
 * Reset the hpt366 on error, reset on dma
 * Fix disabling Fast Interrupt hpt366.
 *	Mike Waychison <crlf@sun.com>
 *
 * 02 May 2002 - HPT374 support (Andre Hedrick <andre@linux-ide.org>)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "ata-timing.h"
#include "pcihost.h"


/* various tuning parameters */
#define HPT_RESET_STATE_ENGINE
/*#define HPT_DELAY_INTERRUPT*/
/*#define HPT_SERIALIZE_IO*/

static const char *quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP LM20.5",
        NULL
};

static const char *bad_ata100_5[] = {
	"IBM-DTLA-307075",
	"IBM-DTLA-307060",
	"IBM-DTLA-307045",
	"IBM-DTLA-307030",
	"IBM-DTLA-307020",
	"IBM-DTLA-307015",
	"IBM-DTLA-305040",
	"IBM-DTLA-305030",
	"IBM-DTLA-305020",
	"IC35L010AVER07-0",
	"IC35L020AVER07-0",
	"IC35L030AVER07-0",
	"IC35L040AVER07-0",
	"IC35L060AVER07-0",
	"WDC AC310200R",
	NULL
};

static const char *bad_ata66_4[] = {
	"IBM-DTLA-307075",
	"IBM-DTLA-307060",
	"IBM-DTLA-307045",
	"IBM-DTLA-307030",
	"IBM-DTLA-307020",
	"IBM-DTLA-307015",
	"IBM-DTLA-305040",
	"IBM-DTLA-305030",
	"IBM-DTLA-305020",
	"IC35L010AVER07-0",
	"IC35L020AVER07-0",
	"IC35L030AVER07-0",
	"IC35L040AVER07-0",
	"IC35L060AVER07-0",
	"WDC AC310200R",
	NULL
};

static const char *bad_ata66_3[] = {
	"WDC AC310200R",
	NULL
};

static const char *bad_ata33[] = {
	"Maxtor 92720U8", "Maxtor 92040U6", "Maxtor 91360U4", "Maxtor 91020U3", "Maxtor 90845U3", "Maxtor 90650U2",
	"Maxtor 91360D8", "Maxtor 91190D7", "Maxtor 91020D6", "Maxtor 90845D5", "Maxtor 90680D4", "Maxtor 90510D3", "Maxtor 90340D2",
	"Maxtor 91152D8", "Maxtor 91008D7", "Maxtor 90845D6", "Maxtor 90840D6", "Maxtor 90720D5", "Maxtor 90648D5", "Maxtor 90576D4",
	"Maxtor 90510D4",
	"Maxtor 90432D3", "Maxtor 90288D2", "Maxtor 90256D2",
	"Maxtor 91000D8", "Maxtor 90910D8", "Maxtor 90875D7", "Maxtor 90840D7", "Maxtor 90750D6", "Maxtor 90625D5", "Maxtor 90500D4",
	"Maxtor 91728D8", "Maxtor 91512D7", "Maxtor 91303D6", "Maxtor 91080D5", "Maxtor 90845D4", "Maxtor 90680D4", "Maxtor 90648D3", "Maxtor 90432D2",
	NULL
};

struct chipset_bus_clock_list_entry {
	u8		xfer_speed;
	unsigned int	chipset_settings;
};

/* key for bus clock timings for HPT370
 * bit
 * 0:3    data_high_time. inactive time of DIOW_/DIOR_ for PIO and MW
 *        DMA. cycles = value + 1
 * 4:8    data_low_time. active time of DIOW_/DIOR_ for PIO and MW
 *        DMA. cycles = value + 1
 * 9:12   cmd_high_time. inactive time of DIOW_/DIOR_ during task file
 *        register access.
 * 13:17  cmd_low_time. active time of DIOW_/DIOR_ during task file
 *        register access.
 * 18:21  udma_cycle_time. clock freq and clock cycles for UDMA xfer.
 *        during task file register access.
 * 22:24  pre_high_time. time to initialize 1st cycle for PIO and MW DMA
 *        xfer.
 * 25:27  cmd_pre_high_time. time to initialize 1st PIO cycle for task
 *        register access.
 * 28     UDMA enable
 * 29     DMA enable
 * 30     PIO_MST enable. if set, the chip is in bus master mode during
 *        PIO.
 * 31     FIFO enable.
 */
static struct chipset_bus_clock_list_entry forty_base_hpt366[] = {
	{	XFER_UDMA_4,	0x900fd943	},
	{	XFER_UDMA_3,	0x900ad943	},
	{	XFER_UDMA_2,	0x900bd943	},
	{	XFER_UDMA_1,	0x9008d943	},
	{	XFER_UDMA_0,	0x9008d943	},

	{	XFER_MW_DMA_2,	0xa008d943	},
	{	XFER_MW_DMA_1,	0xa010d955	},
	{	XFER_MW_DMA_0,	0xa010d9fc	},

	{	XFER_PIO_4,	0xc008d963	},
	{	XFER_PIO_3,	0xc010d974	},
	{	XFER_PIO_2,	0xc010d997	},
	{	XFER_PIO_1,	0xc010d9c7	},
	{	XFER_PIO_0,	0xc018d9d9	},
	{	0,		0x0120d9d9	}
};

static struct chipset_bus_clock_list_entry thirty_three_base_hpt366[] = {
	{	XFER_UDMA_4,	0x90c9a731	},
	{	XFER_UDMA_3,	0x90cfa731	},
	{	XFER_UDMA_2,	0x90caa731	},
	{	XFER_UDMA_1,	0x90cba731	},
	{	XFER_UDMA_0,	0x90c8a731	},

	{	XFER_MW_DMA_2,	0xa0c8a731	},
	{	XFER_MW_DMA_1,	0xa0c8a732	},	/* 0xa0c8a733 */
	{	XFER_MW_DMA_0,	0xa0c8a797	},

	{	XFER_PIO_4,	0xc0c8a731	},
	{	XFER_PIO_3,	0xc0c8a742	},
	{	XFER_PIO_2,	0xc0d0a753	},
	{	XFER_PIO_1,	0xc0d0a7a3	},	/* 0xc0d0a793 */
	{	XFER_PIO_0,	0xc0d0a7aa	},	/* 0xc0d0a7a7 */
	{	0,		0x0120a7a7	}
};

static struct chipset_bus_clock_list_entry twenty_five_base_hpt366[] = {

	{	XFER_UDMA_4,	0x90c98521	},
	{	XFER_UDMA_3,	0x90cf8521	},
	{	XFER_UDMA_2,	0x90cf8521	},
	{	XFER_UDMA_1,	0x90cb8521	},
	{	XFER_UDMA_0,	0x90cb8521	},

	{	XFER_MW_DMA_2,	0xa0ca8521	},
	{	XFER_MW_DMA_1,	0xa0ca8532	},
	{	XFER_MW_DMA_0,	0xa0ca8575	},

	{	XFER_PIO_4,	0xc0ca8521	},
	{	XFER_PIO_3,	0xc0ca8532	},
	{	XFER_PIO_2,	0xc0ca8542	},
	{	XFER_PIO_1,	0xc0d08572	},
	{	XFER_PIO_0,	0xc0d08585	},
	{	0,		0x01208585	}
};

#if 1
/* these are the current (4 sep 2001) timings from highpoint */
static struct chipset_bus_clock_list_entry thirty_three_base_hpt370[] = {
        {       XFER_UDMA_5,    0x12446231      },
        {       XFER_UDMA_4,    0x12446231      },
        {       XFER_UDMA_3,    0x126c6231      },
        {       XFER_UDMA_2,    0x12486231      },
        {       XFER_UDMA_1,    0x124c6233      },
        {       XFER_UDMA_0,    0x12506297      },

        {       XFER_MW_DMA_2,  0x22406c31      },
        {       XFER_MW_DMA_1,  0x22406c33      },
        {       XFER_MW_DMA_0,  0x22406c97      },

        {       XFER_PIO_4,     0x06414e31      },
        {       XFER_PIO_3,     0x06414e42      },
        {       XFER_PIO_2,     0x06414e53      },
        {       XFER_PIO_1,     0x06814e93      },
        {       XFER_PIO_0,     0x06814ea7      },
        {       0,              0x06814ea7      }
};

/* 2x 33MHz timings */
static struct chipset_bus_clock_list_entry sixty_six_base_hpt370[] = {
	{       XFER_UDMA_5,    0x1488e673       },
	{       XFER_UDMA_4,    0x1488e673       },
	{       XFER_UDMA_3,    0x1498e673       },
	{       XFER_UDMA_2,    0x1490e673       },
	{       XFER_UDMA_1,    0x1498e677       },
	{       XFER_UDMA_0,    0x14a0e73f       },

	{       XFER_MW_DMA_2,  0x2480fa73       },
	{       XFER_MW_DMA_1,  0x2480fa77       },
	{       XFER_MW_DMA_0,  0x2480fb3f       },

	{       XFER_PIO_4,     0x0c82be73       },
	{       XFER_PIO_3,     0x0c82be95       },
	{       XFER_PIO_2,     0x0c82beb7       },
	{       XFER_PIO_1,     0x0d02bf37       },
	{       XFER_PIO_0,     0x0d02bf5f       },
	{       0,              0x0d02bf5f       }
};
#else
/* from highpoint documentation. these are old values */
static struct chipset_bus_clock_list_entry thirty_three_base_hpt370[] = {
	{	XFER_UDMA_5,	0x16454e31	},
	{	XFER_UDMA_4,	0x16454e31	},
	{	XFER_UDMA_3,	0x166d4e31	},
	{	XFER_UDMA_2,	0x16494e31	},
	{	XFER_UDMA_1,	0x164d4e31	},
	{	XFER_UDMA_0,	0x16514e31	},

	{	XFER_MW_DMA_2,	0x26514e21	},
	{	XFER_MW_DMA_1,	0x26514e33	},
	{	XFER_MW_DMA_0,	0x26514e97	},

	{	XFER_PIO_4,	0x06514e21	},
	{	XFER_PIO_3,	0x06514e22	},
	{	XFER_PIO_2,	0x06514e33	},
	{	XFER_PIO_1,	0x06914e43	},
	{	XFER_PIO_0,	0x06914e57	},
	{	0,		0x06514e57	}
};

static struct chipset_bus_clock_list_entry sixty_six_base_hpt370[] = {
	{       XFER_UDMA_5,    0x14846231      },
	{       XFER_UDMA_4,    0x14886231      },
	{       XFER_UDMA_3,    0x148c6231      },
	{       XFER_UDMA_2,    0x148c6231      },
	{       XFER_UDMA_1,    0x14906231      },
	{       XFER_UDMA_0,    0x14986231      },

	{       XFER_MW_DMA_2,  0x26514e21      },
	{       XFER_MW_DMA_1,  0x26514e33      },
	{       XFER_MW_DMA_0,  0x26514e97      },

	{       XFER_PIO_4,     0x06514e21      },
	{       XFER_PIO_3,     0x06514e22      },
	{       XFER_PIO_2,     0x06514e33      },
	{       XFER_PIO_1,     0x06914e43      },
	{       XFER_PIO_0,     0x06914e57      },
	{       0,              0x06514e57      }
};
#endif

static struct chipset_bus_clock_list_entry fifty_base_hpt370[] = {
	{       XFER_UDMA_5,    0x12848242      },
	{       XFER_UDMA_4,    0x12ac8242      },
	{       XFER_UDMA_3,    0x128c8242      },
	{       XFER_UDMA_2,    0x120c8242      },
	{       XFER_UDMA_1,    0x12148254      },
	{       XFER_UDMA_0,    0x121882ea      },

	{       XFER_MW_DMA_2,  0x22808242      },
	{       XFER_MW_DMA_1,  0x22808254      },
	{       XFER_MW_DMA_0,  0x228082ea      },

	{       XFER_PIO_4,     0x0a81f442      },
	{       XFER_PIO_3,     0x0a81f443      },
	{       XFER_PIO_2,     0x0a81f454      },
	{       XFER_PIO_1,     0x0ac1f465      },
	{       XFER_PIO_0,     0x0ac1f48a      },
	{       0,              0x0ac1f48a      }
};

static struct chipset_bus_clock_list_entry thirty_three_base_hpt372[] = {
	{	XFER_UDMA_6,	0x1c81dc62	},
	{	XFER_UDMA_5,	0x1c6ddc62	},
	{	XFER_UDMA_4,	0x1c8ddc62	},
	{	XFER_UDMA_3,	0x1c8edc62	},	/* checkme */
	{	XFER_UDMA_2,	0x1c91dc62	},
	{	XFER_UDMA_1,	0x1c9adc62	},	/* checkme */
	{	XFER_UDMA_0,	0x1c82dc62	},	/* checkme */

	{	XFER_MW_DMA_2,	0x2c829262	},
	{	XFER_MW_DMA_1,	0x2c829266	},	/* checkme */
	{	XFER_MW_DMA_0,	0x2c82922e	},	/* checkme */

	{	XFER_PIO_4,	0x0c829c62	},
	{	XFER_PIO_3,	0x0c829c84	},
	{	XFER_PIO_2,	0x0c829ca6	},
	{	XFER_PIO_1,	0x0d029d26	},
	{	XFER_PIO_0,	0x0d029d5e	},
	{	0,		0x0d029d5e	}
};

static struct chipset_bus_clock_list_entry fifty_base_hpt372[] = {
	{	XFER_UDMA_5,	0x12848242	},
	{	XFER_UDMA_4,	0x12ac8242	},
	{	XFER_UDMA_3,	0x128c8242	},
	{	XFER_UDMA_2,	0x120c8242	},
	{	XFER_UDMA_1,	0x12148254	},
	{	XFER_UDMA_0,	0x121882ea	},

	{	XFER_MW_DMA_2,	0x22808242	},
	{	XFER_MW_DMA_1,	0x22808254	},
	{	XFER_MW_DMA_0,	0x228082ea	},

	{	XFER_PIO_4,	0x0a81f442	},
	{	XFER_PIO_3,	0x0a81f443	},
	{	XFER_PIO_2,	0x0a81f454	},
	{	XFER_PIO_1,	0x0ac1f465	},
	{	XFER_PIO_0,	0x0ac1f48a	},
	{	0,		0x0a81f443	}
};

static struct chipset_bus_clock_list_entry sixty_six_base_hpt372[] = {
	{	XFER_UDMA_6,	0x1c869c62	},
	{	XFER_UDMA_5,	0x1cae9c62	},
	{	XFER_UDMA_4,	0x1c8a9c62	},
	{	XFER_UDMA_3,	0x1c8e9c62	},
	{	XFER_UDMA_2,	0x1c929c62	},
	{	XFER_UDMA_1,	0x1c9a9c62	},
	{	XFER_UDMA_0,	0x1c829c62	},

	{	XFER_MW_DMA_2,	0x2c829c62	},
	{	XFER_MW_DMA_1,	0x2c829c66	},
	{	XFER_MW_DMA_0,	0x2c829d2e	},

	{	XFER_PIO_4,	0x0c829c62	},
	{	XFER_PIO_3,	0x0c829c84	},
	{	XFER_PIO_2,	0x0c829ca6	},
	{	XFER_PIO_1,	0x0d029d26	},
	{	XFER_PIO_0,	0x0d029d5e	},
	{	0,		0x0d029d26	}
};

static struct chipset_bus_clock_list_entry thirty_three_base_hpt374[] = {
	{	XFER_UDMA_6,	0x12808242	},
	{	XFER_UDMA_5,	0x12848242	},
	{	XFER_UDMA_4,	0x12ac8242	},
	{	XFER_UDMA_3,	0x128c8242	},
	{	XFER_UDMA_2,	0x120c8242	},
	{	XFER_UDMA_1,	0x12148254	},
	{	XFER_UDMA_0,	0x121882ea	},

	{	XFER_MW_DMA_2,	0x22808242	},
	{	XFER_MW_DMA_1,	0x22808254	},
	{	XFER_MW_DMA_0,	0x228082ea	},

	{	XFER_PIO_4,	0x0a81f442	},
	{	XFER_PIO_3,	0x0a81f443	},
	{	XFER_PIO_2,	0x0a81f454	},
	{	XFER_PIO_1,	0x0ac1f465	},
	{	XFER_PIO_0,	0x0ac1f48a	},
	{	0,		0x06814e93	}
};

#if 0
static struct chipset_bus_clock_list_entry sixty_six_base_hpt374[] = {
	{	XFER_UDMA_6,	0x12406231	},	/* checkme */
	{	XFER_UDMA_5,	0x12446231	},
				0x14846231
	{	XFER_UDMA_4,		0x16814ea7	},
				0x14886231
	{	XFER_UDMA_3,		0x16814ea7	},
				0x148c6231
	{	XFER_UDMA_2,		0x16814ea7	},
				0x148c6231
	{	XFER_UDMA_1,		0x16814ea7	},
				0x14906231
	{	XFER_UDMA_0,		0x16814ea7	},
				0x14986231
	{	XFER_MW_DMA_2,		0x16814ea7	},
				0x26514e21
	{	XFER_MW_DMA_1,		0x16814ea7	},
				0x26514e97
	{	XFER_MW_DMA_0,		0x16814ea7	},
				0x26514e97
	{	XFER_PIO_4,		0x06814ea7	},
				0x06514e21
	{	XFER_PIO_3,		0x06814ea7	},
				0x06514e22
	{	XFER_PIO_2,		0x06814ea7	},
				0x06514e33
	{	XFER_PIO_1,		0x06814ea7	},
				0x06914e43
	{	XFER_PIO_0,		0x06814ea7	},
				0x06914e57
	{	0,		0x06814ea7	}
};
#endif

#define HPT366_DEBUG_DRIVE_INFO		0
#define HPT374_ALLOW_ATA133_6		0
#define HPT372_ALLOW_ATA133_6		1
#define HPT370_ALLOW_ATA100_5		1
#define HPT366_ALLOW_ATA66_4		1
#define HPT366_ALLOW_ATA66_3		1
#define HPT366_MAX_DEVS			8

#define F_LOW_PCI_33      0x23
#define F_LOW_PCI_40      0x29
#define F_LOW_PCI_50      0x2d
#define F_LOW_PCI_66      0x42

static int check_in_drive_lists(struct ata_device *drive, const char **list)
{
	struct hd_driveid *id = drive->id;

	if (quirk_drives == list) {
		while (*list) {
			if (strstr(id->model, *list++)) {
				return 1;
			}
		}
	} else {
		while (*list) {
			if (!strcmp(*list++,id->model)) {
				return 1;
			}
		}
	}
	return 0;
}

static unsigned int hpt_revision(struct pci_dev *dev)
{
	u32 class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	switch(dev->device) {
		case PCI_DEVICE_ID_TTI_HPT374:
			class_rev = PCI_DEVICE_ID_TTI_HPT374; break;
		case PCI_DEVICE_ID_TTI_HPT372:
			class_rev = PCI_DEVICE_ID_TTI_HPT372; break;
		default:
			break;
	}
	return class_rev;
}

static int __init hpt3xx_modes_map(struct ata_channel *ch)
{
	u32 rev = hpt_revision(ch->pci_dev);
	int map = XFER_EPIO | XFER_MWDMA | XFER_UDMA | XFER_UDMA_66;

	if (rev >= 8) {					/* HPT374 */
		if (HPT374_ALLOW_ATA133_6)
			map |= XFER_UDMA_133;
		map |= XFER_UDMA_100;
	} else if (rev >= 5) {				/* HPT372 */
		if (HPT372_ALLOW_ATA133_6)
			map |= XFER_UDMA_133;
		map |= XFER_UDMA_100;
	} else if (rev >= 3) {				/* HPT370A / HPT370 */
		if (HPT370_ALLOW_ATA100_5)
			map |= XFER_UDMA_100;
	}						/* HPT366 / HPT368 */

	return map;
}


static unsigned int pci_bus_clock_list(u8 speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return chipset_table->chipset_settings;
		}
	return chipset_table->chipset_settings;
}

static void hpt366_tune_chipset(struct ata_device *drive, u8 speed)
{
	struct pci_dev *dev	= drive->channel->pci_dev;
	u8 regtime = (drive->select.b.unit & 0x01) ? 0x44 : 0x40;
	u8 regfast = (drive->channel->unit) ? 0x55 : 0x51;
			/*
			 * since the channel is always 0 it does not matter.
			 */

	u32 reg1, reg2;
	u8 drive_fast;

	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	if (drive_fast & 0x80)
		pci_write_config_byte(dev, regfast, drive_fast & ~0x80);

	pci_read_config_dword(dev, regtime, &reg1);
	reg2 = pci_bus_clock_list(speed,
		(struct chipset_bus_clock_list_entry *) dev->sysdata);
	/*
	 * Disable on-chip PIO FIFO/buffer (to avoid problems handling I/O errors later)
	 */
	if (speed >= XFER_MW_DMA_0) {
		reg2 = (reg2 & ~0xc0000000) | (reg1 & 0xc0000000);
	} else {
		reg2 = (reg2 & ~0x30070000) | (reg1 & 0x30070000);
	}
	reg2 &= ~0x80000000;

	pci_write_config_dword(dev, regtime, reg2);
}

static void hpt370_tune_chipset(struct ata_device *drive, u8 speed)
{
	u8 regfast = (drive->channel->unit) ? 0x55 : 0x51;
	u32 list_conf, drive_conf;
	u32 conf_mask = (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;
	u8 drive_pci = 0x40 + (drive->dn * 4);
	u8 new_fast, drive_fast;
	struct pci_dev *dev	= drive->channel->pci_dev;

	/*
	 * Disable the "fast interrupt" prediction.
	 * don't holdoff on interrupts. (== 0x01 despite what the docs say)
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	new_fast = drive_fast;
	if (new_fast & 0x02)
		new_fast &= ~0x02;

#ifdef HPT_DELAY_INTERRUPT
	if (new_fast & 0x01)
		new_fast &= ~0x01;
#else
	if ((new_fast & 0x01) == 0)
		new_fast |= 0x01;
#endif
	if (new_fast != drive_fast)
		pci_write_config_byte(drive->channel->pci_dev, regfast, new_fast);

	list_conf = pci_bus_clock_list(speed,
				       (struct chipset_bus_clock_list_entry *)
				       dev->sysdata);

	pci_read_config_dword(dev, drive_pci, &drive_conf);
	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);

	if (speed < XFER_MW_DMA_0) {
		list_conf &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	}

	pci_write_config_dword(dev, drive_pci, list_conf);
}

static void hpt372_tune_chipset(struct ata_device *drive, u8 speed)
{
	u8 regfast = (drive->channel->unit) ? 0x55 : 0x51;
	u32 list_conf, drive_conf;
	u32 conf_mask = (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;
	u8 drive_pci = 0x40 + (drive->dn * 4);
	u8 drive_fast;
	struct pci_dev *dev	= drive->channel->pci_dev;

	/*
	 * Disable the "fast interrupt" prediction.
	 * don't holdoff on interrupts. (== 0x01 despite what the docs say)
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	drive_fast &= ~0x07;
	pci_write_config_byte(drive->channel->pci_dev, regfast, drive_fast);

	list_conf = pci_bus_clock_list(speed,
			(struct chipset_bus_clock_list_entry *)
					dev->sysdata);
	pci_read_config_dword(dev, drive_pci, &drive_conf);
	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);
	if (speed < XFER_MW_DMA_0)
		list_conf &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	pci_write_config_dword(dev, drive_pci, list_conf);
}

static int hpt3xx_tune_chipset(struct ata_device *drive, u8 speed)
{
	u32 rev;

	if ((drive->type != ATA_DISK) && (speed < XFER_SW_DMA_0))
		return -1;

	rev = hpt_revision(drive->channel->pci_dev);

	if (rev >= 5) {
		hpt372_tune_chipset(drive, speed);
	} else if (rev >= 3) {
		hpt370_tune_chipset(drive, speed);
	} else {
                hpt366_tune_chipset(drive, speed);
        }

	return ide_config_drive_speed(drive, speed);
}

/* FIXME: pio == 255 -> ata_best_pio_mode(drive)  --bkz */
static void hpt3xx_tune_drive(struct ata_device *drive, u8 pio)
{
	(void) hpt3xx_tune_chipset(drive, XFER_PIO_0 + min_t(u8, pio, 4));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int hpt3xx_udma_setup(struct ata_device *drive, int map)
{
	u32 rev;

	if (drive->type != ATA_DISK)
		return 0;

	rev = hpt_revision(drive->channel->pci_dev);

	/* FIXME: badlists need futher investigation  --bkz */

	/* bad_ata100_5 is for HPT370/370A,
	   bad_ata66_4, bad_ata66_3 and bad_ata33 are for HPT366/368 */

	if (rev < 5 && check_in_drive_lists(drive, bad_ata100_5))
		map &= ~XFER_UDMA_100;

	if (rev < 3) {
		if (check_in_drive_lists(drive, bad_ata66_4))
			map &= ~XFER_UDMA_66_4;

		if (check_in_drive_lists(drive, bad_ata66_3))
			map &= ~XFER_UDMA_66_3;

		if (check_in_drive_lists(drive, bad_ata33))
			map &= ~XFER_UDMA_ALL;
	}

	return udma_generic_setup(drive, map);
}

static int hpt3xx_quirkproc(struct ata_device *drive)
{
	return ((int) check_in_drive_lists(drive, quirk_drives));
}

static void hpt3xx_intrproc(struct ata_device *drive)
{
	if (drive->quirk_list) {
		/* drives in the quirk_list may not like intr setups/cleanups */
	} else {
		OUT_BYTE(0x02, drive->channel->io_ports[IDE_CONTROL_OFFSET]);
	}
}

static void hpt3xx_maskproc(struct ata_device *drive)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	struct ata_channel *ch = drive->channel;

	if (drive->quirk_list) {
		u32 rev = hpt_revision(dev);
		if (rev >= 3) {
			u8 reg5a;
			pci_read_config_byte(dev, 0x5a, &reg5a);
			if ((reg5a & 0x10) >> 4)
				pci_write_config_byte(dev, 0x5a, reg5a & ~0x10);
		} else
			enable_irq(drive->channel->irq);
	} else {
		if (ch->io_ports[IDE_CONTROL_OFFSET])
			OUT_BYTE(0x00, ch->io_ports[IDE_CONTROL_OFFSET]);
	}
}

static void hpt366_udma_irq_lost(struct ata_device *drive)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	u8 reg50h, reg52h, reg5ah;

	pci_read_config_byte(dev, 0x50, &reg50h);
	pci_read_config_byte(dev, 0x52, &reg52h);
	pci_read_config_byte(dev, 0x5a, &reg5ah);
	printk("%s: (%s)  reg50h=0x%02x, reg52h=0x%02x, reg5ah=0x%02x\n",
			drive->name, __FUNCTION__, reg50h, reg52h, reg5ah);
	if (reg5ah & 0x10)
		pci_write_config_byte(dev, 0x5a, reg5ah & ~0x10);
}

static void do_udma_start(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;

	u8 regstate = ch->unit ? 0x54 : 0x50;
	pci_write_config_byte(ch->pci_dev, regstate, 0x37);
	udelay(10);
}

static void hpt370_udma_start(struct ata_device *drive, struct request *__rq)
{
	struct ata_channel *ch = drive->channel;

	do_udma_start(drive);

	/* Note that this is done *after* the cmd has been issued to the drive,
	 * as per the BM-IDE spec.  The Promise Ultra33 doesn't work correctly
	 * when we do this part before issuing the drive cmd.
	 */

	outb(inb(ch->dma_base) | 1, ch->dma_base);	/* start DMA */
}

static void do_timeout_irq(struct ata_device *drive)
{
	u8 dma_stat;
	u8 regstate = drive->channel->unit ? 0x54 : 0x50;
	u8 reginfo = drive->channel->unit ? 0x56 : 0x52;
	unsigned long dma_base = drive->channel->dma_base;

	pci_read_config_byte(drive->channel->pci_dev, reginfo, &dma_stat);
	printk(KERN_INFO "%s: %d bytes in FIFO\n", drive->name, dma_stat);
	pci_write_config_byte(drive->channel->pci_dev, regstate, 0x37);
	udelay(10);
	dma_stat = inb(dma_base);
	outb(dma_stat & ~0x1, dma_base); /* stop dma */
	dma_stat = inb(dma_base + 2);
	outb(dma_stat | 0x6, dma_base+2); /* clear errors */

}

static void hpt370_udma_timeout(struct ata_device *drive)
{
	do_timeout_irq(drive);
	do_udma_start(drive);
}

static void hpt370_udma_irq_lost(struct ata_device *drive)
{
	do_timeout_irq(drive);
	do_udma_start(drive);
}

static int hpt370_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;
	u8 dma_stat;

	dma_stat = inb(dma_base + 2);
	if (dma_stat & 0x01) {
		udelay(20); /* wait a little */
		dma_stat = inb(dma_base + 2);
	}
	if ((dma_stat & 0x01) != 0) {
		do_timeout_irq(drive);
		do_udma_start(drive);
	}

	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	udma_destroy_table(ch);			/* purge DMA mappings */

	return (dma_stat & 7) != 4 ? (0x10 | dma_stat) : 0;	/* verify good DMA status */
}

static int hpt374_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	struct pci_dev *dev = drive->channel->pci_dev;
	unsigned long dma_base = ch->dma_base;
	u8 mscreg = ch->unit ? 0x54 : 0x50;
	u8 dma_stat;
	u8 bwsr_mask = ch->unit ? 0x02 : 0x01;
	u8 bwsr_stat, msc_stat;
	pci_read_config_byte(dev, 0x6a, &bwsr_stat);
	pci_read_config_byte(dev, mscreg, &msc_stat);
	if ((bwsr_stat & bwsr_mask) == bwsr_mask)
	        pci_write_config_byte(dev, mscreg, msc_stat|0x30);

	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	udma_destroy_table(ch);			/* purge DMA mappings */

	return (dma_stat & 7) != 4 ? (0x10 | dma_stat) : 0;	/* verify good DMA status */
}
#endif

/*
 * Since SUN Cobalt is attempting to do this operation, I should disclose
 * this has been a long time ago Thu Jul 27 16:40:57 2000 was the patch date
 * HOTSWAP ATA Infrastructure.
 */
static void hpt3xx_reset(struct ata_device *drive)
{
#if 0
	unsigned long high_16	= pci_resource_start(drive->channel->pci_dev, 4);
	u8 reset		= (drive->channel->unit) ? 0x80 : 0x40;
	u8 reg59h;

	pci_read_config_byte(drive->channel->pci_dev, 0x59, &reg59h);
	pci_write_config_byte(drive->channel->pci_dev, 0x59, reg59h|reset);
	pci_write_config_byte(drive->channel->pci_dev, 0x59, reg59h);
#endif
}

#if 0
static int hpt3xx_tristate(struct ata_device * drive, int state)
{
	struct ata_channel *ch	= drive->channel;
	struct pci_dev *dev	= ch->pci_dev;
	u8 reset = (ch->unit) ? 0x80 : 0x40;
	u8 state_reg = (ch->unit) ? 0x57 : 0x53;
	u8 reg59h, regXXh;

	if (!ch)
		return -EINVAL;

//	ch->bus_state = state;

	pci_read_config_byte(dev, 0x59, &reg59h);
	pci_read_config_byte(dev, state_reg, &regXXh);

	if (state) {
		// reset drives...
		pci_write_config_byte(dev, state_reg, regXXh|0x80);
		pci_write_config_byte(dev, 0x59, reg59h|reset);
	} else {
		pci_write_config_byte(dev, 0x59, reg59h & ~(reset));
		pci_write_config_byte(dev, state_reg, regXXh & ~(0x80));
		// reset drives...
	}
	return 0;
}
#endif

/*
 * set/get power state for a drive.
 * turning the power off does the following things:
 *   1) soft-reset the drive
 *   2) tri-states the ide bus
 *
 * when we turn things back on, we need to re-initialize things.
 */
#define TRISTATE_BIT  0x8000
static int hpt370_busproc(struct ata_device * drive, int state)
{
	struct ata_channel *ch = drive->channel;
	u8 tristate, resetmask, bus_reg;
	u16 tri_reg;

	if (!ch)
		return -EINVAL;

	ch->bus_state = state;

	if (ch->unit) {
		/* secondary channel */
		tristate = 0x56;
		resetmask = 0x80;
	} else {
		/* primary channel */
		tristate = 0x52;
		resetmask = 0x40;
	}

	/* grab status */
	pci_read_config_word(ch->pci_dev, tristate, &tri_reg);
	pci_read_config_byte(ch->pci_dev, 0x59, &bus_reg);

	/* set the state. we don't set it if we don't need to do so.
	 * make sure that the drive knows that it has failed if it's off */
	switch (state) {
	case BUSSTATE_ON:
		ch->drives[0].failures = 0;
		ch->drives[1].failures = 0;
		if ((bus_reg & resetmask) == 0)
			return 0;
		tri_reg &= ~TRISTATE_BIT;
		bus_reg &= ~resetmask;
		break;
	case BUSSTATE_OFF:
		ch->drives[0].failures = ch->drives[0].max_failures + 1;
		ch->drives[1].failures = ch->drives[1].max_failures + 1;
		if ((tri_reg & TRISTATE_BIT) == 0 && (bus_reg & resetmask))
			return 0;
		tri_reg &= ~TRISTATE_BIT;
		bus_reg |= resetmask;
		break;
	case BUSSTATE_TRISTATE:
		ch->drives[0].failures = ch->drives[0].max_failures + 1;
		ch->drives[1].failures = ch->drives[1].max_failures + 1;
		if ((tri_reg & TRISTATE_BIT) && (bus_reg & resetmask))
			return 0;
		tri_reg |= TRISTATE_BIT;
		bus_reg |= resetmask;
		break;
	}
	pci_write_config_byte(ch->pci_dev, 0x59, bus_reg);
	pci_write_config_word(ch->pci_dev, tristate, tri_reg);

	return 0;
}

static void __init hpt37x_init(struct pci_dev *dev)
{
	int adjust, i;
	u16 freq;
	u32 pll, rev = hpt_revision(dev);
	u8 reg5bh;

	/*
	 * default to pci clock. make sure MA15/16 are set to output
	 * to prevent drives having problems with 40-pin cables.
	 */
	pci_write_config_byte(dev, 0x5b, 0x23);

	/*
	 * set up the PLL. we need to adjust it so that it's stable.
	 * freq = Tpll * 192 / Tpci
	 */
	pci_read_config_word(dev, 0x78, &freq);
	freq &= 0x1FF;
	if (freq < 0x9c) {
		pll = F_LOW_PCI_33;
		if (rev >= 7) {
			dev->sysdata = (void *) thirty_three_base_hpt374;
		} else if (rev >= 5) {
			dev->sysdata = (void *) thirty_three_base_hpt372;
		} else {
			dev->sysdata = (void *) thirty_three_base_hpt370;
		}
		printk("HPT37X: using 33MHz PCI clock\n");
	} else if (freq < 0xb0) {
		pll = F_LOW_PCI_40;
	} else if (freq < 0xc8) {
		pll = F_LOW_PCI_50;
		if (rev >= 7) {
	//		dev->sysdata = (void *) fifty_base_hpt374;
			BUG();
		} else if (rev >= 5) {
			dev->sysdata = (void *) fifty_base_hpt372;
		} else {
			dev->sysdata = (void *) fifty_base_hpt370;
		}
		printk("HPT37X: using 50MHz PCI clock\n");
	} else {
		pll = F_LOW_PCI_66;
		if (rev >= 7) {
	//		dev->sysdata = (void *) sixty_six_base_hpt374;
			BUG();
		} else if (rev >= 5) {
			dev->sysdata = (void *) sixty_six_base_hpt372;
		} else {
			dev->sysdata = (void *) sixty_six_base_hpt370;
		}
		printk("HPT37X: using 66MHz PCI clock\n");
	}

	/*
	 * only try the pll if we don't have a table for the clock
	 * speed that we're running at. NOTE: the internal PLL will
	 * result in slow reads when using a 33MHz PCI clock. we also
	 * don't like to use the PLL because it will cause glitches
	 * on PRST/SRST when the HPT state engine gets reset.
	 */
	if (dev->sysdata)
		goto init_hpt37X_done;

	/*
	 * adjust PLL based upon PCI clock, enable it, and wait for
	 * stabilization.
	 */
	adjust = 0;
	freq = (pll < F_LOW_PCI_50) ? 2 : 4;
	while (adjust++ < 6) {
		pci_write_config_dword(dev, 0x5c, (freq + pll) << 16 |
				       pll | 0x100);

		/* wait for clock stabilization */
		for (i = 0; i < 0x50000; i++) {
			pci_read_config_byte(dev, 0x5b, &reg5bh);
			if (reg5bh & 0x80) {
				/* spin looking for the clock to destabilize */
				for (i = 0; i < 0x1000; ++i) {
					pci_read_config_byte(dev, 0x5b,
							     &reg5bh);
					if ((reg5bh & 0x80) == 0)
						goto pll_recal;
				}
				pci_read_config_dword(dev, 0x5c, &pll);
				pci_write_config_dword(dev, 0x5c,
						       pll & ~0x100);
				pci_write_config_byte(dev, 0x5b, 0x21);
				if (rev >= 7) {
	//	dev->sysdata = (void *) fifty_base_hpt374;
					BUG();
				} else if (rev >= 5) {
					dev->sysdata = (void *) fifty_base_hpt372;
				} else {
					dev->sysdata = (void *) fifty_base_hpt370;
				}
				printk("HPT37X: using 50MHz internal PLL\n");
				goto init_hpt37X_done;
			}
		}
pll_recal:
		if (adjust & 1)
			pll -= (adjust >> 1);
		else
			pll += (adjust >> 1);
	}

init_hpt37X_done:
	/* reset state engine */
	pci_write_config_byte(dev, 0x50, 0x37);
	pci_write_config_byte(dev, 0x54, 0x37);
	udelay(100);
}

static void __init hpt366_init(struct pci_dev *dev)
{
	u32 reg1;
	u8 drive_fast;

	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(dev, 0x51, &drive_fast);
	if (drive_fast & 0x80)
		pci_write_config_byte(dev, 0x51, drive_fast & ~0x80);
	pci_read_config_dword(dev, 0x40, &reg1);

	/* detect bus speed by looking at control reg timing: */
	switch((reg1 >> 8) & 7) {
		case 5:
			dev->sysdata = (void *) forty_base_hpt366;
			break;
		case 9:
			dev->sysdata = (void *) twenty_five_base_hpt366;
			break;
		case 7:
		default:
			dev->sysdata = (void *) thirty_three_base_hpt366;
			break;
	}
}

static unsigned int __init hpt366_init_chipset(struct pci_dev *dev)
{
	u32 rev = hpt_revision(dev);
	u8 test;

	if (dev->resource[PCI_ROM_RESOURCE].start)
		pci_write_config_byte(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);

	pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &test);
	if (test != (L1_CACHE_BYTES / 4))
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES / 4));

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &test);
	if (test != 0x78)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);

	pci_read_config_byte(dev, PCI_MIN_GNT, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);

	pci_read_config_byte(dev, PCI_MAX_LAT, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

	if (rev >= 3)
		hpt37x_init(dev);
	else
		hpt366_init(dev);

	return dev->irq;
}

static unsigned int __init hpt366_ata66_check(struct ata_channel *ch)
{
	u8 ata66;
	u8 regmask = (ch->unit) ? 0x01 : 0x02;

	pci_read_config_byte(ch->pci_dev, 0x5a, &ata66);
#ifdef DEBUG
	printk("HPT366: reg5ah=0x%02x ATA-%s Cable Port%d\n",
		ata66, (ata66 & regmask) ? "33" : "66",
		PCI_FUNC(ch->pci_dev->devfn));
#endif
	return ((ata66 & regmask) ? 0 : 1);
}

static void __init hpt366_init_channel(struct ata_channel *ch)
{
	struct pci_dev *dev = ch->pci_dev;
	u32 rev = hpt_revision(dev);

	ch->udma_four = hpt366_ata66_check(ch);

	ch->tuneproc = hpt3xx_tune_drive;
	ch->speedproc = hpt3xx_tune_chipset;
	ch->quirkproc = hpt3xx_quirkproc;
	ch->intrproc = hpt3xx_intrproc;
	ch->maskproc = hpt3xx_maskproc;

#ifdef HPT_SERIALIZE_IO
	/* serialize access to this device */
	if (ch->mate)
		ch->serialized = ch->mate->serialized = 1;
#endif

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (ch->dma_base) {
		if (rev >= 3) {
			u8 reg5ah;
			pci_read_config_byte(dev, 0x5a, &reg5ah);
			if (reg5ah & 0x10)	/* interrupt force enable */
				pci_write_config_byte(dev, 0x5a, reg5ah & ~0x10);
			/*
			 * set up ioctl for power status.
			 * note: power affects both
			 * drives on each channel
			 */
			ch->resetproc	= hpt3xx_reset;
			ch->busproc	= hpt370_busproc;

			if (rev >= 5) {
				ch->udma_stop = hpt374_udma_stop;
			} else { /* rev >= 3 */
			        ch->udma_start = hpt370_udma_start;
				ch->udma_stop = hpt370_udma_stop;
				ch->udma_timeout = hpt370_udma_timeout;
				ch->udma_irq_lost = hpt370_udma_irq_lost;
			}
		} else {
			ch->udma_irq_lost = hpt366_udma_irq_lost;
//			ch->resetproc = hpt3xx_reset;
//			ch->busproc = hpt3xx_tristate;
		}
		ch->modes_map = hpt3xx_modes_map(ch);
		ch->udma_setup = hpt3xx_udma_setup;
		ch->highmem = 1;
	} else
#endif
	{
		ch->drives[0].autotune = 1;
		ch->drives[1].autotune = 1;
	}
}

static void __init hpt366_init_dma(struct ata_channel *ch, unsigned long dmabase)
{
	u8 masterdma, slavedma;
	u8 dma_old = inb(dmabase + 2);
	u8 dma_new = dma_old;
	u8 primary = ch->unit ? 0x4b : 0x43;

	pci_read_config_byte(ch->pci_dev, primary, &masterdma);
	pci_read_config_byte(ch->pci_dev, primary + 4, &slavedma);

	if (masterdma & 0x30)
		dma_new |= 0x20;
	if (slavedma & 0x30)
		dma_new |= 0x40;
	if (dma_new != dma_old)
		outb(dma_new, dmabase+2);

	ata_init_dma(ch, dmabase);
}


/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_TTI,
		device: PCI_DEVICE_ID_TTI_HPT366,
		init_chipset: hpt366_init_chipset,
		init_channel: hpt366_init_channel,
		init_dma: hpt366_init_dma,
		bootable: OFF_BOARD,
		extra: 240,
		flags: ATA_F_IRQ | ATA_F_HPTHACK | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_TTI,
		device: PCI_DEVICE_ID_TTI_HPT372,
		init_chipset: hpt366_init_chipset,
		init_channel: hpt366_init_channel,
		init_dma: hpt366_init_dma,
		bootable: OFF_BOARD,
		extra: 0,
		flags: ATA_F_IRQ | ATA_F_HPTHACK | ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_TTI,
		device: PCI_DEVICE_ID_TTI_HPT374,
		init_chipset: hpt366_init_chipset,
		init_channel: hpt366_init_channel,
		init_dma: hpt366_init_dma,
		bootable: OFF_BOARD,
		extra: 0,
		flags: ATA_F_IRQ | ATA_F_HPTHACK | ATA_F_DMA
	},
};

int __init init_hpt366(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i)
		ata_register_chipset(&chipsets[i]);

	return 0;
}
