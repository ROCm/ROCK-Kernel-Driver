/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * Copyright (C) 2002 Marcin Dalecki <martin@dalecki.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/*
 * Declarations needed for the handling of PCI (mostly) based host chip set
 * interfaces.
 */

#ifdef CONFIG_BLK_DEV_PIIX
extern int init_piix(void);
#endif
#ifdef CONFIG_BLK_DEV_VIA82CXXX
extern int init_via82cxxx(void);
#endif
#ifdef CONFIG_BLK_DEV_PDC202XX
extern int init_pdc202xx(void);
#endif
#ifdef CONFIG_BLK_DEV_RZ1000
extern int init_rz1000(void);
#endif
#ifdef CONFIG_BLK_DEV_SIS5513
extern int init_sis5513(void);
#endif
#ifdef CONFIG_BLK_DEV_CMD64X
extern int init_cmd64x(void);
#endif
#ifdef CONFIG_BLK_DEV_OPTI621
extern int init_opti621(void);
#endif
#ifdef CONFIG_BLK_DEV_TRM290
extern int init_trm290(void);
#endif
#ifdef CONFIG_BLK_DEV_NS87415
extern int init_ns87415(void);
#endif
#ifdef CONFIG_BLK_DEV_AEC62XX
extern int init_aec62xx(void);
#endif
#ifdef CONFIG_BLK_DEV_SL82C105
extern int init_sl82c105(void);
#endif
#ifdef CONFIG_BLK_DEV_HPT34X
extern int init_hpt34x(void);
#endif
#ifdef CONFIG_BLK_DEV_HPT366
extern int init_hpt366(void);
#endif
#ifdef CONFIG_BLK_DEV_ALI15X3
extern int init_ali15x3(void);
#endif
#ifdef CONFIG_BLK_DEV_CY82C693
extern int init_cy82c693(void);
#endif
#ifdef CONFIG_BLK_DEV_CS5530
extern int init_cs5530(void);
#endif
#ifdef CONFIG_BLK_DEV_AMD74XX
extern int init_amd74xx(void);
#endif
#ifdef CONFIG_BLK_DEV_SVWKS
extern int init_svwks(void);
#endif
#ifdef CONFIG_BLK_DEV_IT8172
extern int init_it8172(void);
#endif
extern int init_ata_pci_misc(void);

/*
 * Some combi chips, which can be used on the PCI bus or the VL bus can be in
 * some systems acessed either through the PCI config space or through the
 * hosts IO bus.  If the corresponding initialization driver is using the host
 * IO space to deal with them please define the following.
 */

#define	ATA_PCI_IGNORE	((void *)-1)

/*
 * Just to prevent us from having too many tinny headers we have consolidated
 * all those declarations here.
 */

#ifdef CONFIG_BLK_DEV_RZ1000
extern void ide_probe_for_rz100x(void);
#endif

typedef struct ide_pci_enablebit_s {
	u8	reg;	/* pci configuration register holding the enable-bit */
	u8	mask;	/* mask used to isolate the enable-bit */
	u8	val;	/* expected value of masked register when "enabled" */
} ide_pci_enablebit_t;

/* Flags used to untangle quirk handling.
 */
#define ATA_F_DMA	0x001
#define ATA_F_NODMA	0x002	/* no DMA mode supported at all */
#define ATA_F_NOADMA	0x004	/* DMA has to be enabled explicitely */
#define ATA_F_FIXIRQ	0x008	/* fixed irq wiring */
#define ATA_F_SER	0x010	/* serialize on first and second channel interrupts */
#define ATA_F_IRQ	0x020	/* trust IRQ information from config */
#define ATA_F_PHACK	0x040	/* apply PROMISE hacks */
#define ATA_F_HPTHACK	0x080	/* apply HPT366 hacks */
#define ATA_F_SIMPLEX	0x100	/* force treatment as simple device */


struct ata_pci_device {
	unsigned short		vendor;
	unsigned short		device;
	unsigned int		(*init_chipset)(struct pci_dev *);
	void			(*init_channel)(struct ata_channel *);
	void			(*init_dma)(struct ata_channel *, unsigned long);
	ide_pci_enablebit_t	enablebits[2];
	unsigned int		bootable;
	unsigned int		extra;
	unsigned int		flags;
	struct ata_pci_device *next;	/* beware we link the netries in pleace */
};

extern void ata_register_chipset(struct ata_pci_device *d);
