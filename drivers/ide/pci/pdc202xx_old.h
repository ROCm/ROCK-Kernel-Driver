#ifndef PDC202XX_H
#define PDC202XX_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#ifndef SPLIT_BYTE
#define SPLIT_BYTE(B,H,L)	((H)=(B>>4), (L)=(B-((B>>4)<<4)))
#endif

#define PDC202XX_DEBUG_DRIVE_INFO		0

static const char *pdc_quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP KA9.1",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP KX13.6",
	"QUANTUM FIREBALLP KX20.5",
	"QUANTUM FIREBALLP KX27.3",
	"QUANTUM FIREBALLP LM20.5",
	NULL
};

/* A Register */
#define	SYNC_ERRDY_EN	0xC0

#define	SYNC_IN		0x80	/* control bit, different for master vs. slave drives */
#define	ERRDY_EN	0x40	/* control bit, different for master vs. slave drives */
#define	IORDY_EN	0x20	/* PIO: IOREADY */
#define	PREFETCH_EN	0x10	/* PIO: PREFETCH */

#define	PA3		0x08	/* PIO"A" timing */
#define	PA2		0x04	/* PIO"A" timing */
#define	PA1		0x02	/* PIO"A" timing */
#define	PA0		0x01	/* PIO"A" timing */

/* B Register */

#define	MB2		0x80	/* DMA"B" timing */
#define	MB1		0x40	/* DMA"B" timing */
#define	MB0		0x20	/* DMA"B" timing */

#define	PB4		0x10	/* PIO_FORCE 1:0 */

#define	PB3		0x08	/* PIO"B" timing */	/* PIO flow Control mode */
#define	PB2		0x04	/* PIO"B" timing */	/* PIO 4 */
#define	PB1		0x02	/* PIO"B" timing */	/* PIO 3 half */
#define	PB0		0x01	/* PIO"B" timing */	/* PIO 3 other half */

/* C Register */
#define	IORDYp_NO_SPEED	0x4F
#define	SPEED_DIS	0x0F

#define	DMARQp		0x80
#define	IORDYp		0x40
#define	DMAR_EN		0x20
#define	DMAW_EN		0x10

#define	MC3		0x08	/* DMA"C" timing */
#define	MC2		0x04	/* DMA"C" timing */
#define	MC1		0x02	/* DMA"C" timing */
#define	MC0		0x01	/* DMA"C" timing */

static void init_setup_pdc202ata4(struct pci_dev *dev, ide_pci_device_t *d);
static void init_setup_pdc20265(struct pci_dev *, ide_pci_device_t *);
static void init_setup_pdc202xx(struct pci_dev *, ide_pci_device_t *);
static unsigned int init_chipset_pdc202xx(struct pci_dev *, const char *);
static void init_hwif_pdc202xx(ide_hwif_t *);
static void init_dma_pdc202xx(ide_hwif_t *, unsigned long);

static ide_pci_device_t pdc202xx_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "PDC20246",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
#ifndef CONFIG_PDC202XX_FORCE
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
#endif
		.bootable	= OFF_BOARD,
		.extra		= 16,
	},{	/* 1 */
		.name		= "PDC20262",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
#ifndef CONFIG_PDC202XX_FORCE
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
#endif
		.bootable	= OFF_BOARD,
		.extra		= 48,
		.flags		= IDEPCI_FLAG_FORCE_PDC,
	},{	/* 2 */
		.name		= "PDC20263",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
#ifndef CONFIG_PDC202XX_FORCE
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
#endif
		.bootable	= OFF_BOARD,
		.extra		= 48,
	},{	/* 3 */
		.name		= "PDC20265",
		.init_setup	= init_setup_pdc20265,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
#ifndef CONFIG_PDC202XX_FORCE
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
#endif
		.bootable	= OFF_BOARD,
		.extra		= 48,
		.flags		= IDEPCI_FLAG_FORCE_PDC,
	},{	/* 4 */
		.name		= "PDC20267",
		.init_setup	= init_setup_pdc202xx,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
#ifndef CONFIG_PDC202XX_FORCE
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x04,0x04}},
#endif
		.bootable	= OFF_BOARD,
		.extra		= 48,
	}
};

#endif /* PDC202XX_H */
