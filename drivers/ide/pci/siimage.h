#ifndef SIIMAGE_H
#define SIIMAGE_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>

#define DISPLAY_SIIMAGE_TIMINGS

#define CONFIG_TRY_MMIO_SIIMAGE
//#undef CONFIG_TRY_MMIO_SIIMAGE
#undef SIIMAGE_VIRTUAL_DMAPIO
#undef SIIMAGE_BUFFERED_TASKFILE
#undef SIIMAGE_LARGE_DMA

#if 0
typedef struct ide_io_ops_s siimage_iops {

}
#endif

#define SII_DEBUG 0

#if SII_DEBUG
#define siiprintk(x...)	printk(x)
#else
#define siiprintk(x...)
#endif

#define ADJREG(B,R)	((B)|(R)|((hwif->channel)<<(4+(2*(hwif->mmio)))))
#define SELREG(R)	ADJREG((0xA0),(R))
#define SELADDR(R)	((((u32)hwif->hwif_data)*(hwif->mmio))|SELREG((R)))
#define HWIFADDR(R)	((((u32)hwif->hwif_data)*(hwif->mmio))|(R))
#define DEVADDR(R)	(((u32) pci_get_drvdata(dev))|(R))


inline u8 sii_inb (u32 port)
{
	return (u8) readb(port);
}

inline u16 sii_inw (u32 port)
{
	return (u16) readw(port);
}

inline void sii_insw (u32 port, void *addr, u32 count)
{
	while (count--) { *(u16 *)addr = readw(port); addr += 2; }
}

inline u32 sii_inl (u32 port)
{
	return (u32) readl(port);
}

inline void sii_insl (u32 port, void *addr, u32 count)
{
	sii_insw(port, addr, (count)<<1);
//	while (count--) { *(u32 *)addr = readl(port); addr += 4; }
}

inline void sii_outb (u8 addr, u32 port)
{
	writeb(addr, port);
}

inline void sii_outw (u16 addr, u32 port)
{
	writew(addr, port);
}

inline void sii_outsw (u32 port, void *addr, u32 count)
{
	while (count--) { writew(*(u16 *)addr, port); addr += 2; }
}

inline void sii_outl (u32 addr, u32 port)
{
	writel(addr, port);
}

inline void sii_outsl (u32 port, void *addr, u32 count)
{
	sii_outsw(port, addr, (count)<<1);
//	while (count--) { writel(*(u32 *)addr, port); addr += 4; }
}

#if defined(DISPLAY_SIIMAGE_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static char * print_siimage_get_info(char *, struct pci_dev *, int);
static int siimage_get_info(char *, char **, off_t, int);

static u8 siimage_proc;

static ide_pci_host_proc_t siimage_procs[] __initdata = {
	{
		name:		"siimage",
		set:		1,
		get_info:	siimage_get_info,
		parent:		NULL,
	},
};
#endif /* DISPLAY_SIIMAGE_TIMINGS && CONFIG_PROC_FS */	

static unsigned int init_chipset_siimage(struct pci_dev *, const char *);
static void init_iops_siimage(ide_hwif_t *);
static void init_hwif_siimage(ide_hwif_t *);
static void init_dma_siimage(ide_hwif_t *, unsigned long);

static ide_pci_device_t siimage_chipsets[] __devinitdata = {
	{	/* 0 */
		vendor:		PCI_VENDOR_ID_CMD,
		device:		PCI_DEVICE_ID_SII_680,
		name:		"SiI680",
		init_chipset:	init_chipset_siimage,
		init_iops:	init_iops_siimage,
		init_hwif:	init_hwif_siimage,
		init_dma:	init_dma_siimage,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x00,0x00,0x00}, {0x00,0x00,0x00}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 1 */
		vendor:		PCI_VENDOR_ID_CMD,
		device:		PCI_DEVICE_ID_SII_3112,
		name:		"SiI3112 Serial ATA",
		init_chipset:	init_chipset_siimage,
		init_iops:	init_iops_siimage,
		init_hwif:	init_hwif_siimage,
		init_dma:	init_dma_siimage,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x00,0x00,0x00}, {0x00,0x00,0x00}},
		bootable:	ON_BOARD,
		extra:		0,
	},{
		vendor:		0,
		device:		0,
		channels:	0,
		bootable:	EOL,
	}
};

#endif /* SIIMAGE_H */
