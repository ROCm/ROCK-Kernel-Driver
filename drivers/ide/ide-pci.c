/*
 *  linux/drivers/ide/ide-pci.c		Version 1.05	June 9, 2000
 *
 *  Copyright (c) 1998-2000  Andre Hedrick <andre@linux-ide.org>
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 *  This module provides support for automatic detection and
 *  configuration of all PCI IDE interfaces present in a system.
 */

/*
 * Chipsets that are on the IDE_IGNORE list because of problems of not being
 * set at compile time.
 *
 * CONFIG_BLK_DEV_PDC202XX
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

/* Missing PCI device IDs: */
#define PCI_VENDOR_ID_HINT 0x3388
#define PCI_DEVICE_ID_HINT 0x8013

#define	IDE_IGNORE	((void *)-1)
#define IDE_NO_DRIVER	((void *)-2)

#ifdef CONFIG_BLK_DEV_AEC62XX
extern unsigned int pci_init_aec62xx(struct pci_dev *);
extern unsigned int ata66_aec62xx(ide_hwif_t *);
extern void ide_init_aec62xx(ide_hwif_t *);
extern void ide_dmacapable_aec62xx(ide_hwif_t *, unsigned long);
#endif

#ifdef CONFIG_BLK_DEV_ALI15X3
extern unsigned int pci_init_ali15x3(struct pci_dev *);
extern unsigned int ata66_ali15x3(ide_hwif_t *);
extern void ide_init_ali15x3(ide_hwif_t *);
extern void ide_dmacapable_ali15x3(ide_hwif_t *, unsigned long);
#endif

#ifdef CONFIG_BLK_DEV_AMD74XX
extern unsigned int pci_init_amd74xx(struct pci_dev *);
extern unsigned int ata66_amd74xx(ide_hwif_t *);
extern void ide_init_amd74xx(ide_hwif_t *);
extern void ide_dmacapable_amd74xx(ide_hwif_t *, unsigned long);
#endif

#ifdef CONFIG_BLK_DEV_CMD64X
extern unsigned int pci_init_cmd64x(struct pci_dev *);
extern unsigned int ata66_cmd64x(ide_hwif_t *);
extern void ide_init_cmd64x(ide_hwif_t *);
extern void ide_dmacapable_cmd64x(ide_hwif_t *, unsigned long);
#endif

#ifdef CONFIG_BLK_DEV_CY82C693
extern unsigned int pci_init_cy82c693(struct pci_dev *);
extern void ide_init_cy82c693(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_CS5530
extern unsigned int pci_init_cs5530(struct pci_dev *);
extern void ide_init_cs5530(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_HPT34X
extern unsigned int pci_init_hpt34x(struct pci_dev *);
extern void ide_init_hpt34x(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_HPT366
extern byte hpt363_shared_irq;
extern byte hpt363_shared_pin;

extern unsigned int pci_init_hpt366(struct pci_dev *);
extern unsigned int ata66_hpt366(ide_hwif_t *);
extern void ide_init_hpt366(ide_hwif_t *);
extern void ide_dmacapable_hpt366(ide_hwif_t *, unsigned long);
#else
/* FIXME: those have to be killed */
static byte hpt363_shared_irq;
static byte hpt363_shared_pin;
#endif

#ifdef CONFIG_BLK_DEV_NS87415
extern void ide_init_ns87415(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_OPTI621
extern void ide_init_opti621(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_PDC_ADMA
extern unsigned int pci_init_pdcadma(struct pci_dev *);
extern unsigned int ata66_pdcadma(ide_hwif_t *);
extern void ide_init_pdcadma(ide_hwif_t *);
extern void ide_dmacapable_pdcadma(ide_hwif_t *, unsigned long);
#endif

#ifdef CONFIG_BLK_DEV_PDC202XX
extern unsigned int pci_init_pdc202xx(struct pci_dev *);
extern unsigned int ata66_pdc202xx(ide_hwif_t *);
extern void ide_init_pdc202xx(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_PIIX
extern unsigned int pci_init_piix(struct pci_dev *);
extern unsigned int ata66_piix(ide_hwif_t *);
extern void ide_init_piix(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_IT8172
extern unsigned int pci_init_it8172(struct pci_dev *);
extern void ide_init_it8172(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_RZ1000
extern void ide_init_rz1000(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_SVWKS
extern unsigned int pci_init_svwks(struct pci_dev *);
extern unsigned int ata66_svwks(ide_hwif_t *);
extern void ide_init_svwks(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_SIS5513
extern unsigned int pci_init_sis5513(struct pci_dev *);
extern unsigned int ata66_sis5513(ide_hwif_t *);
extern void ide_init_sis5513(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_SLC90E66
extern unsigned int pci_init_slc90e66(struct pci_dev *);
extern unsigned int ata66_slc90e66(ide_hwif_t *);
extern void ide_init_slc90e66(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_SL82C105
extern unsigned int pci_init_sl82c105(struct pci_dev *);
extern void dma_init_sl82c105(ide_hwif_t *, unsigned long);
extern void ide_init_sl82c105(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_TRM290
extern void ide_init_trm290(ide_hwif_t *);
#endif

#ifdef CONFIG_BLK_DEV_VIA82CXXX
extern unsigned int pci_init_via82cxxx(struct pci_dev *);
extern unsigned int ata66_via82cxxx(ide_hwif_t *);
extern void ide_init_via82cxxx(ide_hwif_t *);
extern void ide_dmacapable_via82cxxx(ide_hwif_t *, unsigned long);
#endif

typedef struct ide_pci_enablebit_s {
	byte	reg;	/* byte pci reg holding the enable-bit */
	byte	mask;	/* mask to isolate the enable-bit */
	byte	val;	/* value of masked reg when "enabled" */
} ide_pci_enablebit_t;

/* Flags used to untangle quirk handling.
 */
#define ATA_F_DMA	0x01
#define ATA_F_NODMA	0x02	/* no DMA mode supported at all */
#define ATA_F_NOADMA	0x04	/* DMA has to be enabled explicitely */
#define ATA_F_FIXIRQ	0x08	/* fixed irq wiring */
#define ATA_F_SER	0x10	/* serialize on first and second channel interrupts */
#define ATA_F_IRQ	0x20	/* trust IRQ information from config */
#define ATA_F_PHACK	0x40	/* apply PROMISE hacks */
#define ATA_F_HPTHACK	0x80	/* apply HPT366 hacks */

typedef struct ide_pci_device_s {
	unsigned short		vendor;
	unsigned short		device;
	unsigned int		(*init_chipset)(struct pci_dev *dev);
	unsigned int		(*ata66_check)(ide_hwif_t *hwif);
	void			(*init_hwif)(ide_hwif_t *hwif);
	void			(*dma_init)(ide_hwif_t *hwif, unsigned long dmabase);
	ide_pci_enablebit_t	enablebits[2];
	unsigned int		bootable;
	unsigned int		extra;
	unsigned int		flags;
} ide_pci_device_t;

static ide_pci_device_t pci_chipsets[] __initdata = {
#ifdef CONFIG_BLK_DEV_PIIX
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371FB_0, NULL, NULL, ide_init_piix,	NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371FB_1, NULL, NULL, ide_init_piix,	NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371MX, NULL, NULL, ide_init_piix, NULL, {{0x6D,0x80,0x80}, {0x6F,0x80,0x80}}, ON_BOARD, 0, ATA_F_NODMA },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_1, pci_init_piix, NULL, ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB, pci_init_piix, NULL,	ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AB_1, pci_init_piix, NULL, ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443MX_1, pci_init_piix, NULL, ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AA_1, pci_init_piix, ata66_piix, ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82372FB_1, pci_init_piix, ata66_piix, ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82451NX, pci_init_piix, NULL,	ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, ATA_F_NOADMA },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_9, pci_init_piix, ata66_piix,	ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_8, pci_init_piix, ata66_piix,	ide_init_piix, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_10, pci_init_piix, ata66_piix, ide_init_piix,	NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_VIA82CXXX
	{PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C576_1,	pci_init_via82cxxx, ata66_via82cxxx, ide_init_via82cxxx, ide_dmacapable_via82cxxx, {{0x40,0x02,0x02}, {0x40,0x01,0x01}}, ON_BOARD, 0, ATA_F_NOADMA },
	{PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_1,	pci_init_via82cxxx, ata66_via82cxxx, ide_init_via82cxxx, ide_dmacapable_via82cxxx, {{0x40,0x02,0x02}, {0x40,0x01,0x01}}, ON_BOARD, 0, ATA_F_NOADMA },
#endif
#ifdef CONFIG_BLK_DEV_PDC202XX
# ifdef CONFIG_PDC202XX_FORCE
        {PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20246, pci_init_pdc202xx, NULL, ide_init_pdc202xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD,	16, ATA_F_IRQ | ATA_F_DMA },
        {PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20262, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 48, ATA_F_IRQ | ATA_F_PHACK | ATA_F_DMA},
        {PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20265, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 48, ATA_F_IRQ | ATA_F_PHACK | ATA_F_DMA},
        {PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20267, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 48, ATA_F_IRQ | ATA_F_DMA },
# else
	{PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20246, pci_init_pdc202xx, NULL, ide_init_pdc202xx, NULL, {{0x50,0x02,0x02}, {0x50,0x04,0x04}}, OFF_BOARD, 16, ATA_F_IRQ | ATA_F_DMA },
	{PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20262, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x50,0x02,0x02}, {0x50,0x04,0x04}}, OFF_BOARD, 48, ATA_F_IRQ | ATA_F_PHACK | ATA_F_DMA },
	{PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20265, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x50,0x02,0x02}, {0x50,0x04,0x04}}, OFF_BOARD, 48, ATA_F_IRQ | ATA_F_PHACK  | ATA_F_DMA },
	{PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20267, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x50,0x02,0x02}, {0x50,0x04,0x04}}, OFF_BOARD, 48, ATA_F_IRQ  | ATA_F_DMA },
# endif
	{PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20268, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 0, ATA_F_IRQ | ATA_F_DMA },
	/* Promise used a different PCI ident for the raid card apparently to try and
	   prevent Linux detecting it and using our own raid code. We want to detect
	   it for the ataraid drivers, so we have to list both here.. */
	{PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20268R, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 0, ATA_F_IRQ  | ATA_F_DMA },
	{PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20269, pci_init_pdc202xx, ata66_pdc202xx, ide_init_pdc202xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 0, ATA_F_IRQ | ATA_F_DMA },
	{PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20275, pci_init_pdc202xx, ata66_pdc202xx,	ide_init_pdc202xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 0, ATA_F_IRQ | ATA_F_DMA },
#endif
#ifdef CONFIG_BLK_DEV_RZ1000
	{PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1000, NULL, NULL,	ide_init_rz1000, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1001, NULL, NULL,	ide_init_rz1000, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_SIS5513
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5513, pci_init_sis5513, ata66_sis5513, ide_init_sis5513, NULL, {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}}, ON_BOARD, 0, ATA_F_NOADMA },
#endif
#ifdef CONFIG_BLK_DEV_CMD64X
	{PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_643, pci_init_cmd64x, NULL, ide_init_cmd64x, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_646, pci_init_cmd64x, NULL, ide_init_cmd64x, NULL, {{0x00,0x00,0x00}, {0x51,0x80,0x80}}, ON_BOARD, 0, ATA_F_DMA },
	{PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_648, pci_init_cmd64x, ata66_cmd64x, ide_init_cmd64x, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_DMA },
	{PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_649, pci_init_cmd64x, ata66_cmd64x, ide_init_cmd64x, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_DMA },
	{PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_680, pci_init_cmd64x, ata66_cmd64x, ide_init_cmd64x, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_DMA },
#endif
#ifdef CONFIG_BLK_DEV_OPTI621
	{PCI_VENDOR_ID_OPTI, PCI_DEVICE_ID_OPTI_82C621, NULL, NULL, ide_init_opti621, NULL, {{0x45,0x80,0x00}, {0x40,0x08,0x00}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_OPTI, PCI_DEVICE_ID_OPTI_82C825, NULL, NULL, ide_init_opti621, NULL, {{0x45,0x80,0x00}, {0x40,0x08,0x00}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_TRM290
	{PCI_VENDOR_ID_TEKRAM, PCI_DEVICE_ID_TEKRAM_DC290, NULL, NULL, ide_init_trm290,	NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_NS87415
	{PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_87415, NULL, NULL, ide_init_ns87415, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_AEC62XX
	{PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_ATP850UF, pci_init_aec62xx, NULL, ide_init_aec62xx, ide_dmacapable_aec62xx, {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}}, OFF_BOARD, 0, ATA_F_SER | ATA_F_IRQ | ATA_F_DMA },
	{PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_ATP860, pci_init_aec62xx, ata66_aec62xx, ide_init_aec62xx, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, NEVER_BOARD, 0, ATA_F_IRQ | ATA_F_NOADMA | ATA_F_DMA },
	{PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_ATP860R, pci_init_aec62xx, ata66_aec62xx, ide_init_aec62xx, NULL, {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}}, OFF_BOARD, 0, ATA_F_IRQ | ATA_F_DMA },
#endif
#ifdef CONFIG_BLK_DEV_SL82C105
	{PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105, pci_init_sl82c105, NULL, ide_init_sl82c105, dma_init_sl82c105, {{0x40,0x01,0x01}, {0x40,0x10,0x10}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_HPT34X
	{PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT343, pci_init_hpt34x, NULL, ide_init_hpt34x, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, NEVER_BOARD, 16, ATA_F_NOADMA | ATA_F_DMA },
#endif
#ifdef CONFIG_BLK_DEV_HPT366
	{PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT366, pci_init_hpt366, ata66_hpt366, ide_init_hpt366, ide_dmacapable_hpt366, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 240, ATA_F_IRQ | ATA_F_HPTHACK | ATA_F_DMA },
#endif
#ifdef CONFIG_BLK_DEV_ALI15X3
	{PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M5229, pci_init_ali15x3, ata66_ali15x3, ide_init_ali15x3, ide_dmacapable_ali15x3, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_CY82C693
	{PCI_VENDOR_ID_CONTAQ, PCI_DEVICE_ID_CONTAQ_82C693, pci_init_cy82c693, NULL, ide_init_cy82c693,	NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_DMA },
#endif
#ifdef CONFIG_BLK_DEV_CS5530
	{PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5530_IDE, pci_init_cs5530, NULL, ide_init_cs5530, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_DMA },
#endif
#ifdef CONFIG_BLK_DEV_AMD74XX
	{PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_COBRA_7401, NULL, NULL, NULL, ide_dmacapable_amd74xx, {{0x40,0x01,0x01}, {0x40,0x02,0x02}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7409, pci_init_amd74xx, ata66_amd74xx, ide_init_amd74xx, ide_dmacapable_amd74xx, {{0x40,0x01,0x01}, {0x40,0x02,0x02}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7411, pci_init_amd74xx, ata66_amd74xx, ide_init_amd74xx, ide_dmacapable_amd74xx, {{0x40,0x01,0x01}, {0x40,0x02,0x02}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7441, pci_init_amd74xx, ata66_amd74xx, ide_init_amd74xx, ide_dmacapable_amd74xx, {{0x40,0x01,0x01}, {0x40,0x02,0x02}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_PDC_ADMA
	{PCI_VENDOR_ID_PDC, PCI_DEVICE_ID_PDC_1841, pci_init_pdcadma, ata66_pdcadma, ide_init_pdcadma, ide_dmacapable_pdcadma, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 0, ATA_F_NODMA },
#endif
#ifdef CONFIG_BLK_DEV_SLC90E66
	{PCI_VENDOR_ID_EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_1, pci_init_slc90e66, ata66_slc90e66, ide_init_slc90e66, NULL, {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_SVWKS
        {PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_OSB4IDE, pci_init_svwks, ata66_svwks, ide_init_svwks, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_DMA },
	{PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB5IDE, pci_init_svwks, ata66_svwks, ide_init_svwks, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
#endif
#ifdef CONFIG_BLK_DEV_IT8172
	{PCI_VENDOR_ID_ITE, PCI_DEVICE_ID_ITE_IT8172G, pci_init_it8172,	NULL, ide_init_it8172, NULL, {{0x00,0x00,0x00}, {0x40,0x00,0x01}}, ON_BOARD, 0, 0 },
#endif
	/* Those are id's of chips we don't deal currently with,
	 * but which still need some generic quirk handling.
	 */
	{PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_SAMURAI_IDE, NULL, NULL, NULL, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_640, NULL, NULL, IDE_IGNORE, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_87410, NULL, NULL, NULL, NULL, {{0x43,0x08,0x08}, {0x47,0x08,0x08}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_HINT, PCI_DEVICE_ID_HINT, NULL, NULL, NULL, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_HOLTEK, PCI_DEVICE_ID_HOLTEK_6565, NULL, NULL, NULL, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, 0 },
	{PCI_VENDOR_ID_UMC, PCI_DEVICE_ID_UMC_UM8673F, NULL, NULL, NULL, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_FIXIRQ },
	{PCI_VENDOR_ID_UMC, PCI_DEVICE_ID_UMC_UM8886A, NULL, NULL, NULL, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_FIXIRQ },
	{PCI_VENDOR_ID_UMC, PCI_DEVICE_ID_UMC_UM8886BF, NULL, NULL, NULL, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_FIXIRQ },
	{PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C561, NULL, NULL, NULL, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0, ATA_F_NOADMA },
	{PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT366, NULL, NULL, IDE_NO_DRIVER, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, OFF_BOARD, 240, ATA_F_IRQ | ATA_F_HPTHACK },
	{0, 0, NULL, NULL, NULL, NULL, {{0x00,0x00,0x00}, {0x00,0x00,0x00}}, ON_BOARD, 0 }};

/*
 * This allows offboard ide-pci cards the enable a BIOS, verify interrupt
 * settings of split-mirror pci-config space, place chipset into init-mode,
 * and/or preserve an interrupt if the card is not native ide support.
 */
static unsigned int __init trust_pci_irq(ide_pci_device_t *d, struct pci_dev *dev)
{
	if (d->flags & ATA_F_IRQ)
		return dev->irq;

	return 0;
}

/*
 * Match a PCI IDE port against an entry in ide_hwifs[],
 * based on io_base port if possible.
 */
static ide_hwif_t __init *lookup_hwif (unsigned long io_base, byte bootable, const char *name)
{
	int h;
	ide_hwif_t *hwif;

	/*
	 * Look for a hwif with matching io_base specified using
	 * parameters to ide_setup().
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		hwif = &ide_hwifs[h];
		if (hwif->io_ports[IDE_DATA_OFFSET] == io_base) {
			if (hwif->chipset == ide_generic)
				return hwif; /* a perfect match */
		}
	}
	/*
	 * Look for a hwif with matching io_base default value.
	 * If chipset is "ide_unknown", then claim that hwif slot.
	 * Otherwise, some other chipset has already claimed it..  :(
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		hwif = &ide_hwifs[h];
		if (hwif->io_ports[IDE_DATA_OFFSET] == io_base) {
			if (hwif->chipset == ide_unknown)
				return hwif; /* match */
			printk("%s: port 0x%04lx already claimed by %s\n", name, io_base, hwif->name);
			return NULL;	/* already claimed */
		}
	}
	/*
	 * Okay, there is no hwif matching our io_base,
	 * so we'll just claim an unassigned slot.
	 * Give preference to claiming other slots before claiming ide0/ide1,
	 * just in case there's another interface yet-to-be-scanned
	 * which uses ports 1f0/170 (the ide0/ide1 defaults).
	 *
	 * Unless there is a bootable card that does not use the standard
	 * ports 1f0/170 (the ide0/ide1 defaults). The (bootable) flag.
	 */
	if (bootable) {
		for (h = 0; h < MAX_HWIFS; ++h) {
			hwif = &ide_hwifs[h];
			if (hwif->chipset == ide_unknown)
				return hwif;	/* pick an unused entry */
		}
	} else {
		for (h = 2; h < MAX_HWIFS; ++h) {
			hwif = ide_hwifs + h;
			if (hwif->chipset == ide_unknown)
				return hwif;	/* pick an unused entry */
		}
	}
	for (h = 0; h < 2; ++h) {
		hwif = ide_hwifs + h;
		if (hwif->chipset == ide_unknown)
			return hwif;	/* pick an unused entry */
	}
	printk("%s: too many IDE interfaces, no room in table\n", name);
	return NULL;
}

static int __init setup_pci_baseregs (struct pci_dev *dev, const char *name)
{
	byte reg, progif = 0;

	/*
	 * Place both IDE interfaces into PCI "native" mode:
	 */
	if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) || (progif & 5) != 5) {
		if ((progif & 0xa) != 0xa) {
			printk("%s: device not capable of full native PCI mode\n", name);
			return 1;
		}
		printk("%s: placing both ports into native PCI mode\n", name);
		pci_write_config_byte(dev, PCI_CLASS_PROG, progif|5);
		if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) || (progif & 5) != 5) {
			printk("%s: rewrite of PROGIF failed, wanted 0x%04x, got 0x%04x\n", name, progif|5, progif);
			return 1;
		}
	}
	/*
	 * Setup base registers for IDE command/control spaces for each interface:
	 */
	for (reg = 0; reg < 4; reg++) {
		struct resource *res = dev->resource + reg;
		if ((res->flags & IORESOURCE_IO) == 0)
			continue;
		if (!res->start) {
			printk("%s: Missing I/O address #%d\n", name, reg);
			return 1;
		}
	}
	return 0;
}

/*
 * Setup a particular port on an ATA host controller.
 *
 * This get's called once for the master and for the slave interface.
 */
static int __init setup_host_channel(struct pci_dev *dev,
		ide_pci_device_t *d,
		int port,
		u8 class_rev,
		int pciirq, ide_hwif_t **mate,
		int autodma,
		unsigned short *pcicmd)
{
	unsigned long base = 0;
	unsigned long ctl = 0;
	ide_pci_enablebit_t *e = &(d->enablebits[port]);
	ide_hwif_t *hwif;

	u8 tmp;
	if (port == 1) {

		/* If this is a Promise FakeRaid controller, the 2nd controller
		 * will be marked as disabled while it is actually there and
		 * enabled by the bios for raid purposes.  Skip the normal "is
		 * it enabled" test for those.
		 */
		if (d->flags & ATA_F_PHACK)
			goto controller_ok;
	}

	/* Test whatever the port is enabled.
	 */
	if (e->reg) {
		if (pci_read_config_byte(dev, e->reg, &tmp))
			return 0; /* error! */
		if ((tmp & e->mask) != e->val)
			return 0;
	}

	/* Nothing to be done for the second port.
	 */
	if (port == 1) {
		if ((d->flags & ATA_F_HPTHACK) && (class_rev < 0x03))
			return 0;
	}
controller_ok:
	if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE || (dev->class & (port ? 4 : 1)) != 0) {
		ctl  = dev->resource[(2 * port) + 1].start;
		base = dev->resource[2 * port].start;
		if (!(ctl & PCI_BASE_ADDRESS_IO_MASK) || !(base & PCI_BASE_ADDRESS_IO_MASK)) {
			printk(KERN_WARNING "%s: error: IO reported as MEM by BIOS!\n", dev->name);
			/* try it with the default values */
			ctl = 0;
			base = 0;
		}
	}
	if (ctl && !base) {
		printk(KERN_WARNING "%s: error: missing MEM base info from BIOS!\n", dev->name);
		/* we will still try to get along with the default */
	}
	if (base && !ctl) {
		printk(KERN_WARNING "%s: error: missing IO base info from BIOS!\n", dev->name);
		/* we will still try to get along with the default */
	}

	/* Fill in the default values: */
	if (!ctl)
		ctl = port ? 0x374 : 0x3f4;
	if (!base)
		base = port ? 0x170 : 0x1f0;

	if ((hwif = lookup_hwif(base, d->bootable, dev->name)) == NULL)
		return -ENOMEM;	/* no room in ide_hwifs[] */

	if (hwif->io_ports[IDE_DATA_OFFSET] != base) {
		ide_init_hwif_ports(&hwif->hw, base, (ctl | 2), NULL);
		memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
		hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET];
	}

	hwif->chipset = ide_pci;
	hwif->pci_dev = dev;
	hwif->channel = port;
	if (!hwif->irq)
		hwif->irq = pciirq;

	/* Setup the mate interface if we have two channels.
	 */
	if (*mate) {
		hwif->mate = *mate;
		(*mate)->mate = hwif;
		if (d->flags & ATA_F_SER) {
			hwif->serialized = 1;
			(*mate)->serialized = 1;
		}
	}

	/* Hard wired IRQ lines on UMC chips and no DMA transfers.*/
	if (d->flags & ATA_F_FIXIRQ) {
		hwif->irq = hwif->channel ? 15 : 14;
		goto no_dma;
	}
	if (d->flags & ATA_F_NODMA)
		goto no_dma;

	/* Check whatever this interface is UDMA4 mode capable. */
	if (hwif->udma_four) {
		printk("%s: warning: ATA-66/100 forced bit set!\n", dev->name);
	} else {
		if (d->ata66_check)
			hwif->udma_four = d->ata66_check(hwif);
	}
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (d->flags & ATA_F_NOADMA)
		autodma = 0;

	if (autodma)
		hwif->autodma = 1;

	if ((d->flags & ATA_F_DMA) || ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE && (dev->class & 0x80))) {
		unsigned long dma_base;

		dma_base = ide_get_or_set_dma_base(hwif, (!*mate && d->extra) ? d->extra : 0, dev->name);
		if (dma_base && !(*pcicmd & PCI_COMMAND_MASTER)) {

			/*
			 * Set up BM-DMA capability (PnP BIOS should have done this already)
			 */
			if (!(d->vendor == PCI_VENDOR_ID_CYRIX && d->device == PCI_DEVICE_ID_CYRIX_5530_IDE))
				hwif->autodma = 0;	/* default DMA off if we had to configure it here */
			pci_write_config_word(dev, PCI_COMMAND, *pcicmd | PCI_COMMAND_MASTER);
			if (pci_read_config_word(dev, PCI_COMMAND, pcicmd) || !(*pcicmd & PCI_COMMAND_MASTER)) {
				printk("%s: %s error updating PCICMD\n", hwif->name, dev->name);
				dma_base = 0;
			}
		}
		if (dma_base) {
			if (d->dma_init)
				d->dma_init(hwif, dma_base);
			else	/* FIXME: use a generic device descriptor instead */
				ide_setup_dma(hwif, dma_base, 8);
		} else {
			printk("%s: %s Bus-Master DMA was disabled by BIOS\n", hwif->name, dev->name);
		}
	}
#endif
no_dma:
	if (d->init_hwif)  /* Call chipset-specific routine for each enabled hwif */
		d->init_hwif(hwif);

	*mate = hwif;

	/* we are done */

	return 0;
}

/*
 * Looks at the primary/secondary chanells on a PCI IDE device and, if they
 * are enabled, prepares the IDE driver for use with them.  This generic code
 * works for most PCI chipsets.
 *
 * One thing that is not standardized is the location of the primary/secondary
 * interface "enable/disable" bits.  For chipsets that we "know" about, this
 * information is in the ide_pci_device_t struct; for all other chipsets, we
 * just assume both interfaces are enabled.
 */

static void __init setup_pci_device(struct pci_dev *dev, ide_pci_device_t *d)
{
	int autodma = 0;
	int pciirq = 0;
	unsigned short pcicmd = 0;
	unsigned short tried_config = 0;
	ide_hwif_t *mate = NULL;
	unsigned int class_rev;

#ifdef CONFIG_IDEDMA_AUTO
	if (!noautodma)
		autodma = 1;
#endif

	if (d->init_hwif == IDE_NO_DRIVER) {
		printk(KERN_WARNING "%s: detected chipset, but driver not compiled in!\n", dev->name);
		d->init_hwif = NULL;
	}

	if (pci_enable_device(dev)) {
		printk(KERN_WARNING "%s: Could not enable PCI device.\n", dev->name);
		return;
	}

check_if_enabled:
	if (pci_read_config_word(dev, PCI_COMMAND, &pcicmd)) {
		printk("%s: error accessing PCI regs\n", dev->name);
		return;
	}
	if (!(pcicmd & PCI_COMMAND_IO)) {	/* is device disabled? */
		/*
		 * PnP BIOS was *supposed* to have set this device up for us,
		 * but we can do it ourselves, so long as the BIOS has assigned an IRQ
		 *  (or possibly the device is using a "legacy header" for IRQs).
		 * Maybe the user deliberately *disabled* the device,
		 * but we'll eventually ignore it again if no drives respond.
		 */
		if (tried_config++
		 || setup_pci_baseregs(dev, dev->name)
		 || pci_write_config_word(dev, PCI_COMMAND, pcicmd | PCI_COMMAND_IO)) {
			printk("%s: device disabled (BIOS)\n", dev->name);
			return;
		}
		autodma = 0;	/* default DMA off if we had to configure it here */
		goto check_if_enabled;
	}
	if (tried_config)
		printk("%s: device enabled (Linux)\n", dev->name);

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	if (d->vendor == PCI_VENDOR_ID_TTI && PCI_DEVICE_ID_TTI_HPT343) {
		/* see comments in hpt34x.c to see why... */
		d->bootable = (pcicmd & PCI_COMMAND_MEMORY) ? OFF_BOARD : NEVER_BOARD;
	}

	printk("%s: chipset revision %d\n", dev->name, class_rev);

	/*
	 * Can we trust the reported IRQ?
	 */
	pciirq = dev->irq;

	if (dev->class >> 8 == PCI_CLASS_STORAGE_RAID) {
		/* By rights we want to ignore these, but the Promise Fastrak
		   people have some strange ideas about proprietary so we have
		   to act otherwise on those. The supertrak however we need
		   to skip */
		if (d->vendor == PCI_VENDOR_ID_PROMISE && d->device == PCI_DEVICE_ID_PROMISE_20265) {
			printk(KERN_INFO "ide: Found promise 20265 in RAID mode.\n");
			if(dev->bus->self && dev->bus->self->vendor == PCI_VENDOR_ID_INTEL &&
				dev->bus->self->device == PCI_DEVICE_ID_INTEL_I960)
			{
				printk(KERN_INFO "ide: Skipping Promise PDC20265 attached to I2O RAID controller.\n");
				return;
			}
		}
		/* Its attached to something else, just a random bridge.
		   Suspect a fastrak and fall through */
	}
	if ((dev->class & ~(0xfa)) != ((PCI_CLASS_STORAGE_IDE << 8) | 5)) {
		printk("%s: not 100%% native mode: will probe irqs later\n", dev->name);
		/*
		 * This allows offboard ide-pci cards the enable a BIOS,
		 * verify interrupt settings of split-mirror pci-config
		 * space, place chipset into init-mode, and/or preserve
		 * an interrupt if the card is not native ide support.
		 */
		if (d->init_chipset)
			pciirq = d->init_chipset(dev);
		else
			pciirq = trust_pci_irq(d, dev);
	} else if (tried_config) {
		printk("%s: will probe irqs later\n", dev->name);
		pciirq = 0;
	} else if (!pciirq) {
		printk("%s: bad irq (%d): will probe later\n", dev->name, pciirq);
		pciirq = 0;
	} else {
		if (d->init_chipset)
			d->init_chipset(dev);
#ifdef __sparc__
		printk("%s: 100%% native mode on irq %s\n",
		       dev->name, __irq_itoa(pciirq));
#else
		printk("%s: 100%% native mode on irq %d\n", dev->name, pciirq);
#endif
	}

	/*
	 * Set up IDE chanells. First the primary, then the secondary.
	 */
	setup_host_channel(dev, d, 0, class_rev, pciirq, &mate, autodma, &pcicmd);
	setup_host_channel(dev, d, 1, class_rev, pciirq, &mate, autodma, &pcicmd);
}

static void __init pdc20270_device_order_fixup (struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *dev2 = NULL, *findev;
	ide_pci_device_t *d2;

	if (dev->bus->self &&
	    dev->bus->self->vendor == PCI_VENDOR_ID_DEC &&
	    dev->bus->self->device == PCI_DEVICE_ID_DEC_21150) {
		if (PCI_SLOT(dev->devfn) & 2) {
			return;
		}
		d->extra = 0;
		pci_for_each_dev(findev) {
			if ((findev->vendor == dev->vendor) &&
			    (findev->device == dev->device) &&
			    (PCI_SLOT(findev->devfn) & 2)) {
				byte irq = 0, irq2 = 0;
				dev2 = findev;
				pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
				pci_read_config_byte(dev2, PCI_INTERRUPT_LINE, &irq2);
                                if (irq != irq2) {
					dev2->irq = dev->irq;
                                        pci_write_config_byte(dev2, PCI_INTERRUPT_LINE, irq);
                                }

			}
		}
	}

	printk("%s: IDE controller on PCI bus %02x dev %02x\n", dev->name, dev->bus->number, dev->devfn);
	setup_pci_device(dev, d);
	if (!dev2)
		return;
	d2 = d;
	printk("%s: IDE controller on PCI bus %02x dev %02x\n", dev2->name, dev2->bus->number, dev2->devfn);
	setup_pci_device(dev2, d2);
}

static void __init hpt366_device_order_fixup (struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *dev2 = NULL, *findev;
	ide_pci_device_t *d2;
	unsigned char pin1 = 0, pin2 = 0;
	unsigned int class_rev;

	if (PCI_FUNC(dev->devfn) & 1)
		return;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	switch(class_rev) {
		case 4:
		case 3:	printk("%s: IDE controller on PCI slot %s\n", dev->name, dev->slot_name);
			setup_pci_device(dev, d);
			return;
		default:	break;
	}

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin1);
	pci_for_each_dev(findev) {
		if (findev->vendor == dev->vendor &&
		    findev->device == dev->device &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			dev2 = findev;
			pci_read_config_byte(dev2, PCI_INTERRUPT_PIN, &pin2);
			hpt363_shared_pin = (pin1 != pin2) ? 1 : 0;
			hpt363_shared_irq = (dev->irq == dev2->irq) ? 1 : 0;
			if (hpt363_shared_pin && hpt363_shared_irq) {
				d->bootable = ON_BOARD;
				printk("%s: onboard version of chipset, pin1=%d pin2=%d\n", dev->name, pin1, pin2);
#if 0
				/* I forgot why I did this once, but it fixed something. */
				pci_write_config_byte(dev2, PCI_INTERRUPT_PIN, dev->irq);
				printk("PCI: %s: Fixing interrupt %d pin %d to ZERO \n", d->name, dev2->irq, pin2);
				pci_write_config_byte(dev2, PCI_INTERRUPT_LINE, 0);
#endif
			}
			break;
		}
	}
	printk("%s: IDE controller on PCI slot %s\n", dev->name, dev->slot_name);
	setup_pci_device(dev, d);
	if (!dev2)
		return;
	d2 = d;
	printk("%s: IDE controller on PCI slot %s\n", dev2->name, dev2->slot_name);
	setup_pci_device(dev2, d2);
}

/*
 * This finds all PCI IDE controllers and calls appriopriate initialization
 * functions for them.
 */
static void __init ide_scan_pcidev(struct pci_dev *dev)
{
	unsigned short vendor;
	unsigned short device;
	ide_pci_device_t	*d;

	vendor = dev->vendor;
	device = dev->device;

	/* Look up the chipset information.
	 */
	d = pci_chipsets;
	while (d->vendor && !(d->vendor == vendor && d->device == device))
		++d;

	if (d->init_hwif == IDE_IGNORE)
		printk("%s: has been ignored by PCI bus scan\n", dev->name);
	else if ((d->vendor == PCI_VENDOR_ID_OPTI && d->device == PCI_DEVICE_ID_OPTI_82C558) && !(PCI_FUNC(dev->devfn) & 1))
		return;
	else if ((d->vendor == PCI_VENDOR_ID_CONTAQ && d->device == PCI_DEVICE_ID_CONTAQ_82C693) && (!(PCI_FUNC(dev->devfn) & 1) || !((dev->class >> 8) == PCI_CLASS_STORAGE_IDE)))
		return;	/* CY82C693 is more than only a IDE controller */
	else if ((d->vendor == PCI_VENDOR_ID_ITE && d->device == PCI_DEVICE_ID_ITE_IT8172G) && (!(PCI_FUNC(dev->devfn) & 1) || !((dev->class >> 8) == PCI_CLASS_STORAGE_IDE)))
		return;	/* IT8172G is also more than only an IDE controller */
	else if ((d->vendor == PCI_VENDOR_ID_UMC && d->device == PCI_DEVICE_ID_UMC_UM8886A) && !(PCI_FUNC(dev->devfn) & 1))
		return;	/* UM8886A/BF pair */
	else if (d->flags & ATA_F_HPTHACK)
		hpt366_device_order_fixup(dev, d);
	else if (d->vendor == PCI_VENDOR_ID_PROMISE && d->device == PCI_DEVICE_ID_PROMISE_20268R)
		pdc20270_device_order_fixup(dev, d);
	else if (!(d->vendor == 0 && d->device == 0) || (dev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		if (d->vendor == 0 && d->device == 0)
			printk("%s: unknown IDE controller on PCI slot %s, vendor=%04x, device=%04x\n",
			       dev->name, dev->slot_name, vendor, device);
		else
			printk("%s: IDE controller on PCI slot %s\n", dev->name, dev->slot_name);
		setup_pci_device(dev, d);
	}
}

void __init ide_scan_pcibus (int scan_direction)
{
	struct pci_dev *dev;

	if (!scan_direction) {
		pci_for_each_dev(dev) {
			ide_scan_pcidev(dev);
		}
	} else {
		pci_for_each_dev_reverse(dev) {
			ide_scan_pcidev(dev);
		}
	}
}
