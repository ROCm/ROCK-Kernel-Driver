/*
 * linux/drivers/ide/hpt366.c		Version 0.33	April 17, 2002
 *
 * Copyright (C) 1999-2002		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
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
 * 	Adrian Sun <a.sun@sun.com>
 *
 * add drive timings for 66MHz PCI bus,
 * fix ATA Cable signal detection, fix incorrect /proc info
 * add /proc display for per-drive PIO/DMA/UDMA mode and
 * per-channel ATA-33/66 Cable detect.
 * 	Duncan Laurie <void@sun.com>
 *
 * fixup /proc output for multiple controllers
 *	Tim Hockin <thockin@sun.com>
 *
 * On hpt366: 
 * Reset the hpt366 on error, reset on dma
 * Fix disabling Fast Interrupt hpt366.
 * 	Mike Waychison <crlf@sun.com>
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

#include "ide_modes.h"

#define DISPLAY_HPT366_TIMINGS

/* various tuning parameters */
#define HPT_RESET_STATE_ENGINE
#undef HPT_DELAY_INTERRUPT
#undef HPT_SERIALIZE_IO

#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>
#endif  /* defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS) */

const char *quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP LM20.5",
        NULL
};

const char *bad_ata100_5[] = {
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

const char *bad_ata66_4[] = {
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

const char *bad_ata66_3[] = {
	"WDC AC310200R",
	NULL
};

const char *bad_ata33[] = {
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
	byte		xfer_speed;
	unsigned int	chipset_settings;
};

/* key for bus clock timings
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
struct chipset_bus_clock_list_entry forty_base_hpt366[] = {
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

struct chipset_bus_clock_list_entry thirty_three_base_hpt366[] = {
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

struct chipset_bus_clock_list_entry twenty_five_base_hpt366[] = {

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

/* from highpoint documentation. these are old values */
struct chipset_bus_clock_list_entry thirty_three_base_hpt370[] = {
/*	{	XFER_UDMA_5,	0x1A85F442,	0x16454e31	}, */
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

struct chipset_bus_clock_list_entry sixty_six_base_hpt370[] = {
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

/* these are the current (4 sep 2001) timings from highpoint */
struct chipset_bus_clock_list_entry thirty_three_base_hpt370a[] = {
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
struct chipset_bus_clock_list_entry sixty_six_base_hpt370a[] = {
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

struct chipset_bus_clock_list_entry fifty_base_hpt370a[] = {
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

struct chipset_bus_clock_list_entry thirty_three_base_hpt372[] = {
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

struct chipset_bus_clock_list_entry fifty_base_hpt372[] = {
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

struct chipset_bus_clock_list_entry sixty_six_base_hpt372[] = {
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

struct chipset_bus_clock_list_entry thirty_three_base_hpt374[] = {
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
struct chipset_bus_clock_list_entry fifty_base_hpt374[] = {
	{	XFER_UDMA_6,	},
	{	XFER_UDMA_5,	},
	{	XFER_UDMA_4,	},
	{	XFER_UDMA_3,	},
	{	XFER_UDMA_2,	},
	{	XFER_UDMA_1,	},
	{	XFER_UDMA_0,	},
	{	XFER_MW_DMA_2,	},
	{	XFER_MW_DMA_1,	},
	{	XFER_MW_DMA_0,	},
	{	XFER_PIO_4,	},
	{	XFER_PIO_3,	},
	{	XFER_PIO_2,	},
	{	XFER_PIO_1,	},
	{	XFER_PIO_0,	},
	{	0,	}
};
#endif
#if 0
struct chipset_bus_clock_list_entry sixty_six_base_hpt374[] = {
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
#define HPT371_ALLOW_ATA133_6		0
#define HPT302_ALLOW_ATA133_6		0
#define HPT372_ALLOW_ATA133_6		1
#define HPT370_ALLOW_ATA100_5		1
#define HPT366_ALLOW_ATA66_4		1
#define HPT366_ALLOW_ATA66_3		1
#define HPT366_MAX_DEVS			8

#define F_LOW_PCI_33      0x23
#define F_LOW_PCI_40      0x29
#define F_LOW_PCI_50      0x2d
#define F_LOW_PCI_66      0x42

static struct pci_dev *hpt_devs[HPT366_MAX_DEVS];
static int n_hpt_devs;

static unsigned int hpt_revision(struct pci_dev *dev);
static unsigned int hpt_minimum_revision(struct pci_dev *dev, int revision);

byte hpt366_proc = 0;
byte hpt363_shared_irq;
byte hpt363_shared_pin;

#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
static int hpt366_get_info(char *, char **, off_t, int);
extern int (*hpt366_display_info)(char *, char **, off_t, int); /* ide-proc.c */

static int hpt366_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p	= buffer;
	char *chipset_nums[] = {"366", "366",  "368",
				"370", "370A", "372",
				"302", "371",  "374" };
	int i;

	p += sprintf(p, "\n                             "
		"HighPoint HPT366/368/370/372/374\n");
	for (i = 0; i < n_hpt_devs; i++) {
		struct pci_dev *dev = hpt_devs[i];
		unsigned long iobase = dev->resource[4].start;
		u32 class_rev = hpt_revision(dev);
		u8 c0, c1;

		p += sprintf(p, "\nController: %d\n", i);
		p += sprintf(p, "Chipset: HPT%s\n", chipset_nums[class_rev]);
		p += sprintf(p, "--------------- Primary Channel "
				"--------------- Secondary Channel "
				"--------------\n");

		/* get the bus master status registers */
		c0 = inb_p(iobase + 0x2);
		c1 = inb_p(iobase + 0xa);
		p += sprintf(p, "Enabled:        %s"
				"                             %s\n",
			(c0 & 0x80) ? "no" : "yes",
			(c1 & 0x80) ? "no" : "yes");

		if (hpt_minimum_revision(dev, 3)) {
			u8 cbl;
			cbl = inb_p(iobase + 0x7b);
			outb_p(cbl | 1, iobase + 0x7b);
			outb_p(cbl & ~1, iobase + 0x7b);
			cbl = inb_p(iobase + 0x7a);
			p += sprintf(p, "Cable:          ATA-%d"
					"                          ATA-%d\n",
				(cbl & 0x02) ? 33 : 66,
				(cbl & 0x01) ? 33 : 66);
			p += sprintf(p, "\n");
		}

		p += sprintf(p, "--------------- drive0 --------- drive1 "
				"------- drive0 ---------- drive1 -------\n");
		p += sprintf(p, "DMA capable:    %s              %s" 
				"            %s               %s\n",
			(c0 & 0x20) ? "yes" : "no ", 
			(c0 & 0x40) ? "yes" : "no ",
			(c1 & 0x20) ? "yes" : "no ", 
			(c1 & 0x40) ? "yes" : "no ");

		{
			u8 c2, c3;
			/* older revs don't have these registers mapped 
			 * into io space */
			pci_read_config_byte(dev, 0x43, &c0);
			pci_read_config_byte(dev, 0x47, &c1);
			pci_read_config_byte(dev, 0x4b, &c2);
			pci_read_config_byte(dev, 0x4f, &c3);

			p += sprintf(p, "Mode:           %s             %s"
					"           %s              %s\n",
				(c0 & 0x10) ? "UDMA" : (c0 & 0x20) ? "DMA " : 
					(c0 & 0x80) ? "PIO " : "off ",
				(c1 & 0x10) ? "UDMA" : (c1 & 0x20) ? "DMA " :
					(c1 & 0x80) ? "PIO " : "off ",
				(c2 & 0x10) ? "UDMA" : (c2 & 0x20) ? "DMA " :
					(c2 & 0x80) ? "PIO " : "off ",
				(c3 & 0x10) ? "UDMA" : (c3 & 0x20) ? "DMA " :
					(c3 & 0x80) ? "PIO " : "off ");
		}
	}
	p += sprintf(p, "\n");
	
	return p-buffer;/* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS) */

static unsigned int hpt_revision (struct pci_dev *dev)
{
	unsigned int class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	switch(dev->device) {
		case PCI_DEVICE_ID_TTI_HPT374:
			class_rev = PCI_DEVICE_ID_TTI_HPT374; break;
		case PCI_DEVICE_ID_TTI_HPT371:
			class_rev = PCI_DEVICE_ID_TTI_HPT371; break;
		case PCI_DEVICE_ID_TTI_HPT302:
			class_rev = PCI_DEVICE_ID_TTI_HPT302; break;
		case PCI_DEVICE_ID_TTI_HPT372:
			class_rev = PCI_DEVICE_ID_TTI_HPT372; break;
		default:
			break;
	}
	return class_rev;
}

static unsigned int hpt_minimum_revision (struct pci_dev *dev, int revision)
{
	unsigned int class_rev = hpt_revision(dev);
	revision--;
	return ((int) (class_rev > revision) ? 1 : 0);
}

static int check_in_drive_lists(ide_drive_t *drive, const char **list);

static byte hpt3xx_ratemask (ide_drive_t *drive)
{
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	byte mode = 0x00;

	if (hpt_minimum_revision(dev, 8)) {		/* HPT374 */
		mode |= (HPT374_ALLOW_ATA133_6) ? 0x04 : 0x03;
	} else if (hpt_minimum_revision(dev, 7)) {	/* HPT371 */
		mode |= (HPT371_ALLOW_ATA133_6) ? 0x04 : 0x03;
	} else if (hpt_minimum_revision(dev, 6)) {	/* HPT302 */
		mode |= (HPT302_ALLOW_ATA133_6) ? 0x04 : 0x03;
	} else if (hpt_minimum_revision(dev, 5)) {	/* HPT372 */
		mode |= (HPT372_ALLOW_ATA133_6) ? 0x04 : 0x03;
	} else if (hpt_minimum_revision(dev, 4)) {	/* HPT370A */
		mode |= (HPT370_ALLOW_ATA100_5) ? 0x03 : 0x02;
	} else if (hpt_minimum_revision(dev, 3)) {	/* HPT370 */
		mode |= (HPT370_ALLOW_ATA100_5) ? 0x03 : 0x02;
		if (check_in_drive_lists(drive, bad_ata33))
			return (mode &= ~0xFF);
	} else {					/* HPT366 and HPT368 */
		mode |= 0x02;
		if (check_in_drive_lists(drive, bad_ata33))
			return (mode &= ~0xFF);
	}

	if (!eighty_ninty_three(drive)) {
		mode &= ~0xFE;
		mode |= 0x01;
	}
	return (mode &= ~0xF8);
}

static byte hpt3xx_ratefilter (ide_drive_t *drive, byte speed)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	byte mode		= hpt3xx_ratemask(drive);

	if (drive->media != ide_disk)
		while (speed > XFER_PIO_4) speed--;

	switch(mode) {
		case 0x04:
			while (speed > XFER_UDMA_6) speed--;
			break;
		case 0x03:
			while (speed > XFER_UDMA_5) speed--;
			if (hpt_minimum_revision(dev, 5))
				break;
			if (check_in_drive_lists(drive, bad_ata100_5))
				while (speed > XFER_UDMA_4) speed--;
			break;
		case 0x02:
			while (speed > XFER_UDMA_4) speed--;
	/*
	 * CHECK ME, Does this need to be set to 5 ??
	 */
			if (hpt_minimum_revision(dev, 3))
				break;
			if ((check_in_drive_lists(drive, bad_ata66_4)) ||
			    (!(HPT366_ALLOW_ATA66_4)))
				while (speed > XFER_UDMA_3) speed--;
			if ((check_in_drive_lists(drive, bad_ata66_3)) ||
			    (!(HPT366_ALLOW_ATA66_3)))
				while (speed > XFER_UDMA_2) speed--;
			break;
		case 0x01:
			while (speed > XFER_UDMA_2) speed--;
	/*
	 * CHECK ME, Does this need to be set to 5 ??
	 */
			if (hpt_minimum_revision(dev, 3))
				break;
			if (check_in_drive_lists(drive, bad_ata33))
				while (speed > XFER_MW_DMA_2) speed--;
			break;
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

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
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

static unsigned int pci_bus_clock_list (byte speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return chipset_table->chipset_settings;
		}
	return chipset_table->chipset_settings;
}

static void hpt366_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	byte speed		= hpt3xx_ratefilter(drive, xferspeed);
	byte regtime		= (drive->select.b.unit & 0x01) ? 0x44 : 0x40;
	byte regfast		= (HWIF(drive)->channel) ? 0x55 : 0x51;
	byte drive_fast		= 0;
	unsigned int reg1	= 0;
	unsigned int reg2	= 0;

	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
#if 0
	if (drive_fast & 0x02)
		pci_write_config_byte(dev, regfast, drive_fast & ~0x20);
#else
	if (drive_fast & 0x80)
		pci_write_config_byte(dev, regfast, drive_fast & ~0x80);
#endif

	reg2 = pci_bus_clock_list(speed,
		(struct chipset_bus_clock_list_entry *) dev->driver_data);
	/*
	 * Disable on-chip PIO FIFO/buffer
	 *  (to avoid problems handling I/O errors later)
	 */
	pci_read_config_dword(dev, regtime, &reg1);
	if (speed >= XFER_MW_DMA_0) {
		reg2 = (reg2 & ~0xc0000000) | (reg1 & 0xc0000000);
	} else {
		reg2 = (reg2 & ~0x30070000) | (reg1 & 0x30070000);
	}	
	reg2 &= ~0x80000000;

	pci_write_config_dword(dev, regtime, reg2);
}

static void hpt368_tune_chipset (ide_drive_t *drive, byte speed)
{
	hpt366_tune_chipset(drive, speed);
}

static void hpt370_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	byte speed		= hpt3xx_ratefilter(drive, xferspeed);
	byte regfast		= (HWIF(drive)->channel) ? 0x55 : 0x51;
	unsigned int list_conf	= 0;
	unsigned int drive_conf = 0;
	unsigned int conf_mask	= (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;
	byte drive_pci		= 0x40 + (drive->dn * 4);
	byte new_fast, drive_fast		= 0;
	struct pci_dev *dev 	= HWIF(drive)->pci_dev;

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
		pci_write_config_byte(dev, regfast, new_fast);

	list_conf = pci_bus_clock_list(speed, 
				       (struct chipset_bus_clock_list_entry *)
				       dev->driver_data);

	pci_read_config_dword(dev, drive_pci, &drive_conf);
	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);
	
	if (speed < XFER_MW_DMA_0) {
		list_conf &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	}

	pci_write_config_dword(dev, drive_pci, list_conf);
}

static void hpt372_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	byte speed		= hpt3xx_ratefilter(drive, xferspeed);
	byte regfast		= (HWIF(drive)->channel) ? 0x55 : 0x51;
	unsigned int list_conf	= 0;
	unsigned int drive_conf	= 0;
	unsigned int conf_mask	= (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;
	byte drive_pci		= 0x40 + (drive->dn * 4);
	byte drive_fast		= 0;
	struct pci_dev *dev	= HWIF(drive)->pci_dev;

	/*
	 * Disable the "fast interrupt" prediction.
	 * don't holdoff on interrupts. (== 0x01 despite what the docs say)
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	drive_fast &= ~0x07;
	pci_write_config_byte(dev, regfast, drive_fast);
					
	list_conf = pci_bus_clock_list(speed,
			(struct chipset_bus_clock_list_entry *)
					dev->driver_data);
	pci_read_config_dword(dev, drive_pci, &drive_conf);
	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);
	if (speed < XFER_MW_DMA_0)
		list_conf &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	pci_write_config_dword(dev, drive_pci, list_conf);
}

static void hpt374_tune_chipset (ide_drive_t *drive, byte speed)
{
	hpt372_tune_chipset(drive, speed);
}

static int hpt3xx_tune_chipset (ide_drive_t *drive, byte speed)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;

	if (hpt_minimum_revision(dev, 8))
		hpt374_tune_chipset(drive, speed);
#if 0
	else if (hpt_minimum_revision(dev, 7))
		hpt371_tune_chipset(drive, speed);
	else if (hpt_minimum_revision(dev, 6))
		hpt302_tune_chipset(drive, speed);
#endif
	else if (hpt_minimum_revision(dev, 5))
		hpt372_tune_chipset(drive, speed);
	else if (hpt_minimum_revision(dev, 3))
		hpt370_tune_chipset(drive, speed);
	else if (hpt_minimum_revision(dev, 2))
		hpt368_tune_chipset(drive, speed);
	else
                hpt366_tune_chipset(drive, speed);

	return ((int) ide_config_drive_speed(drive, speed));
}

static void hpt3xx_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;

	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
	switch(pio) {
		case 4:		speed = XFER_PIO_4; break;
		case 3:		speed = XFER_PIO_3; break;
		case 2:		speed = XFER_PIO_2; break;
		case 1:		speed = XFER_PIO_1; break;
		default:	speed = XFER_PIO_0; break;
	}
	(void) hpt3xx_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.  Initally for designed for
 * HPT366 UDMA chipset by HighPoint|Triones Technologies, Inc.
 *
 * check_in_drive_lists(drive, bad_ata66_4)
 * check_in_drive_lists(drive, bad_ata66_3)
 * check_in_drive_lists(drive, bad_ata33)
 *
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte mode		= hpt3xx_ratemask(drive);
	byte speed		= 0x00;

	if (drive->media != ide_disk)
		mode |= 0x08;

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
		case 0x00:
			if (id->dma_mword & 0x0004)
				{ speed = XFER_MW_DMA_2; break; }
			if (id->dma_mword & 0x0002)
				{ speed = XFER_MW_DMA_1; break; }
			if (id->dma_mword & 0x0001)
				{ speed = XFER_MW_DMA_0; break; }
		default:
			return ((int) ide_dma_off_quietly);
	}

	(void) hpt3xx_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 14) & 3) ? ide_dma_on :
			((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

int hpt3xx_quirkproc (ide_drive_t *drive)
{
	return ((int) check_in_drive_lists(drive, quirk_drives));
}

void hpt3xx_intrproc (ide_drive_t *drive)
{
	if (drive->quirk_list)
		return;
	/* drives in the quirk_list may not like intr setups/cleanups */
	OUT_BYTE((drive)->ctl|2, HWIF(drive)->io_ports[IDE_CONTROL_OFFSET]);
}

void hpt3xx_maskproc (ide_drive_t *drive, int mask)
{
	struct pci_dev *dev = HWIF(drive)->pci_dev;

	if (drive->quirk_list) {
		if (hpt_minimum_revision(dev,3)) {
			byte reg5a = 0;
			pci_read_config_byte(dev, 0x5a, &reg5a);
			if (((reg5a & 0x10) >> 4) != mask)
				pci_write_config_byte(dev, 0x5a, mask ? (reg5a | 0x10) : (reg5a & ~0x10));
		} else {
			if (mask) {
				disable_irq(HWIF(drive)->irq);
			} else {
				enable_irq(HWIF(drive)->irq);
			}
		}
	} else {
		if (IDE_CONTROL_REG)
			OUT_BYTE(mask ? (drive->ctl | 2) : (drive->ctl & ~2), IDE_CONTROL_REG);
	}
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
				dma_func = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if (id->dma_mword & 0x0007) {
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

		hpt3xx_tune_drive(drive, 5);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * hpt366_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 *
 * This is specific to the HPT366 UDMA bios chipset
 * by HighPoint|Triones Technologies, Inc.
 */
int hpt366_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	unsigned long dma_base	= HWIF(drive)->dma_base;
	byte reg50h = 0, reg52h = 0, reg5ah = 0, dma_stat = 0;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_test_irq:
				/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = IN_BYTE(dma_base+2);
			/* return 1 if INTR asserted */
			return (dma_stat & 4) == 4;
		case ide_dma_lostirq:
			pci_read_config_byte(dev, 0x50, &reg50h);
			pci_read_config_byte(dev, 0x52, &reg52h);
			pci_read_config_byte(dev, 0x5a, &reg5ah);
			printk("%s: (%s)  reg50h=0x%02x, reg52h=0x%02x,"
				" reg5ah=0x%02x\n",
				drive->name,
				ide_dmafunc_verbose(func),
				reg50h, reg52h, reg5ah);
			if (reg5ah & 0x10)
				pci_write_config_byte(dev, 0x5a, reg5ah & ~0x10);
#if 0
			/* how about we flush and reset, mmmkay? */
			pci_write_config_byte(dev, 0x51, 0x1F);
			/* fall through to a reset */
		case ide_dma_begin:
		case ide_dma_end:
			/* reset the chips state over and over.. */
			pci_write_config_byte(dev, 0x51, 0x13);
#endif
			break;
		case ide_dma_timeout:
		default:
			break;
	}
	/* use standard DMA stuff */
	return ide_dmaproc(func, drive);
}

int hpt370_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long dma_base	= hwif->dma_base;
	byte regstate		= hwif->channel ? 0x54 : 0x50;
	byte reginfo		= hwif->channel ? 0x56 : 0x52;
	byte dma_stat;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_test_irq:
				/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = IN_BYTE(dma_base+2);
			/* return 1 if INTR asserted */
			return (dma_stat & 4) == 4;

		case ide_dma_end:
			dma_stat = IN_BYTE(dma_base + 2);
			if (dma_stat & 0x01) {
				/* wait a little */
				udelay(20);
				dma_stat = IN_BYTE(dma_base + 2);
			}
			if ((dma_stat & 0x01) == 0) 
				break;

			func = ide_dma_timeout;
			/* fallthrough */

		case ide_dma_timeout:
		case ide_dma_lostirq:
			pci_read_config_byte(dev, reginfo, &dma_stat);
			printk("%s: %d bytes in FIFO\n", drive->name, 
			       dma_stat);
			pci_write_config_byte(dev, regstate, 0x37);
			udelay(10);
			dma_stat = IN_BYTE(dma_base);
			/* stop dma */
			OUT_BYTE(dma_stat & ~0x1, dma_base);
			dma_stat = IN_BYTE(dma_base + 2);
			/* clear errors */
			OUT_BYTE(dma_stat | 0x6, dma_base+2);
			/* fallthrough */

#ifdef HPT_RESET_STATE_ENGINE
	        case ide_dma_begin:
#endif
			pci_write_config_byte(dev, regstate, 0x37);
			udelay(10);
			break;

		default:
			break;
	}
	/* use standard DMA stuff */
	return ide_dmaproc(func, drive);
}

int hpt374_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long dma_base	= hwif->dma_base;
	byte mscreg		= hwif->channel ? 0x54 : 0x50;
//	byte reginfo		= hwif->channel ? 0x56 : 0x52;
	byte dma_stat;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_test_irq:
				/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = IN_BYTE(dma_base+2);
#if 0  /* do not set unless you know what you are doing */
			if (dma_stat & 4) {
				byte stat = GET_STAT();
				OUT_BYTE(dma_base+2, dma_stat & 0xE4);
			}
#endif
			/* return 1 if INTR asserted */
			return (dma_stat & 4) == 4;
		case ide_dma_end:
		{
			byte bwsr_mask = hwif->channel ? 0x02 : 0x01;
			byte bwsr_stat, msc_stat;
			pci_read_config_byte(dev, 0x6a, &bwsr_stat);
			pci_read_config_byte(dev, mscreg, &msc_stat);
			if ((bwsr_stat & bwsr_mask) == bwsr_mask)
				pci_write_config_byte(dev, mscreg, msc_stat|0x30);
		}
		default:
			break;
	}
	/* use standard DMA stuff */
	return ide_dmaproc(func, drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

/*
 * Since SUN Cobalt is attempting to do this operation, I should disclose
 * this has been a long time ago Thu Jul 27 16:40:57 2000 was the patch date
 * HOTSWAP ATA Infrastructure.
 */
void hpt3xx_reset (ide_drive_t *drive)
{
#if 0
	unsigned long high_16	= pci_resource_start(HWIF(drive)->pci_dev, 4);
	byte reset		= (HWIF(drive)->channel) ? 0x80 : 0x40;
	byte reg59h		= 0;

	pci_read_config_byte(HWIF(drive)->pci_dev, 0x59, &reg59h);
	pci_write_config_byte(HWIF(drive)->pci_dev, 0x59, reg59h|reset);
	pci_write_config_byte(HWIF(drive)->pci_dev, 0x59, reg59h);
#endif
}

static int hpt3xx_tristate (ide_drive_t * drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte reset		= (hwif->channel) ? 0x80 : 0x40;
	byte state_reg		= (hwif->channel) ? 0x57 : 0x53;
	byte reg59h		= 0;
	byte regXXh		= 0;

	if (!hwif)
		return -EINVAL;

//	hwif->bus_state = state;

	pci_read_config_byte(dev, 0x59, &reg59h);
	pci_read_config_byte(dev, state_reg, &regXXh);

	if (state) {
		(void) ide_do_reset(drive);
		pci_write_config_byte(dev, state_reg, regXXh|0x80);
		pci_write_config_byte(dev, 0x59, reg59h|reset);
	} else {
		pci_write_config_byte(dev, 0x59, reg59h & ~(reset));
		pci_write_config_byte(dev, state_reg, regXXh & ~(0x80));
		(void) ide_do_reset(drive);
	}
	return 0;
}

/* 
 * set/get power state for a drive.
 * turning the power off does the following things:
 *   1) soft-reset the drive
 *   2) tri-states the ide bus
 *
 * when we turn things back on, we need to re-initialize things.
 */
#define TRISTATE_BIT  0x8000
static int hpt370_busproc(ide_drive_t * drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte tristate, resetmask, bus_reg;
	u16 tri_reg;

	if (!hwif)
		return -EINVAL;

	hwif->bus_state = state;

	if (hwif->channel) { 
		/* secondary channel */
		tristate = 0x56;
		resetmask = 0x80; 
	} else { 
		/* primary channel */
		tristate = 0x52;
		resetmask = 0x40;
	}

	/* grab status */
	pci_read_config_word(dev, tristate, &tri_reg);
	pci_read_config_byte(dev, 0x59, &bus_reg);

	/* set the state. we don't set it if we don't need to do so.
	 * make sure that the drive knows that it has failed if it's off */
	switch (state) {
	case BUSSTATE_ON:
		hwif->drives[0].failures = 0;
		hwif->drives[1].failures = 0;
		if ((bus_reg & resetmask) == 0)
			return 0;
		tri_reg &= ~TRISTATE_BIT;
		bus_reg &= ~resetmask;
		break;
	case BUSSTATE_OFF:
		hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
		hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
		if ((tri_reg & TRISTATE_BIT) == 0 && (bus_reg & resetmask))
			return 0;
		tri_reg &= ~TRISTATE_BIT;
		bus_reg |= resetmask;
		break;
	case BUSSTATE_TRISTATE:
		hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
		hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
		if ((tri_reg & TRISTATE_BIT) && (bus_reg & resetmask))
			return 0;
		tri_reg |= TRISTATE_BIT;
		bus_reg |= resetmask;
		break;
	}
	pci_write_config_byte(dev, 0x59, bus_reg);
	pci_write_config_word(dev, tristate, tri_reg);

	return 0;
}

static void __init init_hpt37x(struct pci_dev *dev)
{
	int adjust, i;
	u16 freq;
	u32 pll;
	byte reg5bh;

#if 1
	byte reg5ah = 0;
	pci_read_config_byte(dev, 0x5a, &reg5ah);
	/* interrupt force enable */
	pci_write_config_byte(dev, 0x5a, (reg5ah & ~0x10));
#endif

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
		if (hpt_minimum_revision(dev,8))
			dev->driver_data = (void *) thirty_three_base_hpt374;
		else if (hpt_minimum_revision(dev,5))
			dev->driver_data = (void *) thirty_three_base_hpt372;
		else if (hpt_minimum_revision(dev,4))
			dev->driver_data = (void *) thirty_three_base_hpt370a;
		else
			dev->driver_data = (void *) thirty_three_base_hpt370;
		printk("HPT37X: using 33MHz PCI clock\n");
	} else if (freq < 0xb0) {
		pll = F_LOW_PCI_40;
	} else if (freq < 0xc8) {
		pll = F_LOW_PCI_50;
		if (hpt_minimum_revision(dev,8))
			BUG();
		else if (hpt_minimum_revision(dev,5))
			dev->driver_data = (void *) fifty_base_hpt372;
		else if (hpt_minimum_revision(dev,4))
			dev->driver_data = (void *) fifty_base_hpt370a;
		else
			dev->driver_data = (void *) fifty_base_hpt370a;
		printk("HPT37X: using 50MHz PCI clock\n");
	} else {
		pll = F_LOW_PCI_66;
		if (hpt_minimum_revision(dev,8))
			BUG();
		else if (hpt_minimum_revision(dev,5))
			dev->driver_data = (void *) sixty_six_base_hpt372;
		else if (hpt_minimum_revision(dev,4))
			dev->driver_data = (void *) sixty_six_base_hpt370a;
		else
			dev->driver_data = (void *) sixty_six_base_hpt370;
		printk("HPT37X: using 66MHz PCI clock\n");
	}
	
	/*
	 * only try the pll if we don't have a table for the clock
	 * speed that we're running at. NOTE: the internal PLL will
	 * result in slow reads when using a 33MHz PCI clock. we also
	 * don't like to use the PLL because it will cause glitches
	 * on PRST/SRST when the HPT state engine gets reset.
	 */
	if (dev->driver_data) 
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
				if (hpt_minimum_revision(dev,8))
					BUG();
				else if (hpt_minimum_revision(dev,5))
					dev->driver_data = (void *) fifty_base_hpt372;
				else if (hpt_minimum_revision(dev,4))
					dev->driver_data = (void *) fifty_base_hpt370a;
				else
					dev->driver_data = (void *) fifty_base_hpt370a;
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

static void __init init_hpt366 (struct pci_dev *dev)
{
	unsigned int reg1	= 0;
	byte drive_fast		= 0;

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
			dev->driver_data = (void *) forty_base_hpt366;
			break;
		case 9:
			dev->driver_data = (void *) twenty_five_base_hpt366;
			break;
		case 7:
		default:
			dev->driver_data = (void *) thirty_three_base_hpt366;
			break;
	}

	if (!dev->driver_data)
		BUG();
}

unsigned int __init pci_init_hpt366 (struct pci_dev *dev, const char *name)
{
	byte test = 0;

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

	if (hpt_minimum_revision(dev, 3)) {
		init_hpt37x(dev);
		hpt_devs[n_hpt_devs++] = dev;
	} else {
		init_hpt366(dev);
		hpt_devs[n_hpt_devs++] = dev;
	}
	
#if defined(DISPLAY_HPT366_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!hpt366_proc) {
		hpt366_proc = 1;
		hpt366_display_info = &hpt366_get_info;
	}
#endif /* DISPLAY_HPT366_TIMINGS && CONFIG_PROC_FS */

	return dev->irq;
}

unsigned int __init ata66_hpt366 (ide_hwif_t *hwif)
{
	byte ata66 = 0;
	byte regmask	= (hwif->channel) ? 0x01 : 0x02;

	pci_read_config_byte(hwif->pci_dev, 0x5a, &ata66);
#ifdef DEBUG
	printk("HPT366: reg5ah=0x%02x ATA-%s Cable Port%d\n",
		ata66, (ata66 & regmask) ? "33" : "66",
		PCI_FUNC(hwif->pci_dev->devfn));
#endif /* DEBUG */
	return ((ata66 & regmask) ? 0 : 1);
}

void __init ide_init_hpt366 (ide_hwif_t *hwif)
{
	struct pci_dev *dev		= hwif->pci_dev;
	hwif->tuneproc			= &hpt3xx_tune_drive;
	hwif->speedproc			= &hpt3xx_tune_chipset;
	hwif->quirkproc			= &hpt3xx_quirkproc;
	hwif->intrproc			= &hpt3xx_intrproc;
	hwif->maskproc			= &hpt3xx_maskproc;

#ifdef HPT_SERIALIZE_IO
	/* serialize access to this device */
	if (hwif->mate)
		hwif->serialized = hwif->mate->serialized = 1;
#endif

	if (hpt_minimum_revision(dev,3)) {
		byte reg5ah = 0;
			pci_write_config_byte(dev, 0x5a, reg5ah & ~0x10);
		/*
		 * set up ioctl for power status.
		 * note: power affects both
		 * drives on each channel
		 */
		hwif->resetproc	= &hpt3xx_reset;
		hwif->busproc	= &hpt370_busproc;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	} else if (hpt_minimum_revision(dev,2)) {
		hwif->resetproc	= &hpt3xx_reset;
		hwif->busproc	= &hpt3xx_tristate;
	} else {
		hwif->resetproc = &hpt3xx_reset;
		hwif->busproc   = &hpt3xx_tristate;
	}

	if (!hwif->dma_base)
		return;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hpt_minimum_revision(dev,8))
		hwif->dmaproc   = &hpt374_dmaproc;
	else if (hpt_minimum_revision(dev,5))
		hwif->dmaproc   = &hpt374_dmaproc;
	else if (hpt_minimum_revision(dev,3))
		hwif->dmaproc   = &hpt370_dmaproc;
	else if (hpt_minimum_revision(dev,2))
		hwif->dmaproc   = &hpt366_dmaproc;
	else
		hwif->dmaproc   = &hpt366_dmaproc;


#ifdef CONFIG_IDEDMA_AUTO
	if (!noautodma)
		hwif->autodma = 1;
#endif /* CONFIG_IDEDMA_AUTO */
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

void __init ide_dmacapable_hpt366 (ide_hwif_t *hwif, unsigned long dmabase)
{
	byte masterdma = 0, slavedma = 0;
	byte dma_new = 0, dma_old = IN_BYTE(dmabase+2);
	byte primary	= hwif->channel ? 0x4b : 0x43;
	byte secondary	= hwif->channel ? 0x4f : 0x47;
	unsigned long flags;

	local_irq_save(flags);

	dma_new = dma_old;
	pci_read_config_byte(hwif->pci_dev, primary, &masterdma);
	pci_read_config_byte(hwif->pci_dev, secondary, &slavedma);

	if (masterdma & 0x30)	dma_new |= 0x20;
	if (slavedma & 0x30)	dma_new |= 0x40;
	if (dma_new != dma_old) OUT_BYTE(dma_new, dmabase+2);

	local_irq_restore(flags);

	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device (struct pci_dev *dev, ide_pci_device_t *d);

void __init fixup_device_hpt374 (struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *dev2 = NULL, *findev;
	ide_pci_device_t *d2;

	if (PCI_FUNC(dev->devfn) & 1)
		return;

	pci_for_each_dev(findev) {
		if ((findev->vendor == dev->vendor) &&
		    (findev->device == dev->device) &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			dev2 = findev;
			break;
		}
	}

	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d->name, dev->bus->number, dev->devfn);
	ide_setup_pci_device(dev, d);
	if (!dev2) {
		return;
	} else {
		byte irq = 0, irq2 = 0;
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
		pci_read_config_byte(dev2, PCI_INTERRUPT_LINE, &irq2);
		if (irq != irq2) {
			pci_write_config_byte(dev2, PCI_INTERRUPT_LINE, irq);
			dev2->irq = dev->irq;
			printk("%s: pci-config space interrupt fixed.\n",
				d->name);
		}
	}
	d2 = d;
	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d2->name, dev2->bus->number, dev2->devfn);
	ide_setup_pci_device(dev2, d2);

}

void __init fixup_device_hpt366 (struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *dev2 = NULL, *findev;
	ide_pci_device_t *d2;
	unsigned char pin1 = 0, pin2 = 0;
	unsigned int class_rev;
	char *chipset_names[] = {"HPT366", "HPT366",  "HPT368",
				 "HPT370", "HPT370A", "HPT372"};

	if (PCI_FUNC(dev->devfn) & 1)
		return;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	strcpy(d->name, chipset_names[class_rev]);

	switch(class_rev) {
		case 5:
		case 4:
		case 3:	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
				d->name, dev->bus->number, dev->devfn);
			ide_setup_pci_device(dev, d);
			return;
		default:	break;
	}

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin1);
	pci_for_each_dev(findev) {
		if ((findev->vendor == dev->vendor) &&
		    (findev->device == dev->device) &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			dev2 = findev;
			pci_read_config_byte(dev2, PCI_INTERRUPT_PIN, &pin2);
			hpt363_shared_pin = (pin1 != pin2) ? 1 : 0;
			hpt363_shared_irq = (dev->irq == dev2->irq) ? 1 : 0;
			if (hpt363_shared_pin && hpt363_shared_irq) {
				d->bootable = ON_BOARD;
				printk("%s: onboard version of chipset, "
					"pin1=%d pin2=%d\n", d->name,
					pin1, pin2);
#if 0
				/*
				 * This is the third undocumented detection
				 * method and is generally required for the
				 * ABIT-BP6 boards.
				 */
				pci_write_config_byte(dev2, PCI_INTERRUPT_PIN, dev->irq);
				printk("PCI: %s: Fixing interrupt %d pin %d "
					"to ZERO \n", d->name, dev2->irq, pin2);
				pci_write_config_byte(dev2, PCI_INTERRUPT_LINE, 0);
#endif
			}
			break;
		}
	}
	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d->name, dev->bus->number, dev->devfn);
	ide_setup_pci_device(dev, d);
	if (!dev2)
		return;
	d2 = d;
	printk("%s: IDE controller on PCI bus %02x dev %02x\n",
		d2->name, dev2->bus->number, dev2->devfn);
	ide_setup_pci_device(dev2, d2);
}

