#ifndef __ASM_PARISC_PCI_H
#define __ASM_PARISC_PCI_H

#include <asm/scatterlist.h>

#define MIN_PCI_PORT 0x000000
#define MAX_PCI_PORT 0xffffff

/*
** HP PCI platforms generally support multiple bus adapters.
**    (workstations 1-~4, servers 2-~32)
**
** Newer platforms number the busses across PCI bus adapters *sparsely*.
** E.g. 0, 8, 16, ...
**
** Under a PCI bus, most HP platforms support PPBs up to two or three
** levels deep. See "Bit3" product line. 
*/
#define PCI_MAX_BUSSES	256

/* [soapbox on]
** Who the hell can develope stuff without ASSERT or VASSERT?
** No one understands all the modules across all platforms.
** For linux add another dimension - processor architectures.
**
** This should be a standard/global macro used liberally
** in all code. Every respectable engineer I know in HP
** would support this argument. - grant
** [soapbox off]
*/
#ifdef PCI_DEBUG
#define ASSERT(expr) \
	if(!(expr)) { \
		printk( "\n" __FILE__ ":%d: Assertion " #expr " failed!\n",__LINE__); \
		panic(#expr); \
	}
#else
#define ASSERT(expr)
#endif


/*
** pci_hba_data (aka H2P_OBJECT in HP/UX)
**
** This is the "common" or "base" data structure which HBA drivers
** (eg Dino or LBA) are required to place at the top of their own
** dev->sysdata structure.  I've heard this called "C inheritance" too.
**
** Data needed by pcibios layer belongs here.
*/
struct pci_hba_data {
	struct pci_hba_data *next;	/* global chain of HBAs */
	char           *base_addr;	/* aka Host Physical Address */
	struct hp_device *iodc_info;	/* Info from PA bus walk */
	struct pci_bus *hba_bus;	/* primary PCI bus below HBA */
	int		hba_num;	/* I/O port space access "key" */
	struct resource bus_num;	/* PCI bus numbers */
	struct resource io_space;	/* PIOP */
	struct resource mem_space;	/* LMMIO */
	unsigned long   mem_space_offset;  /* VCLASS support */
	/* REVISIT - spinlock to protect resources? */
};


/*
** KLUGE: linux/pci.h include asm/pci.h BEFORE declaring struct pci_bus
** (This eliminates some of the warnings).
*/
struct pci_bus;
struct pci_dev;

/*
** Most PCI devices (eg Tulip, NCR720) also export the same registers
** to both MMIO and I/O port space.  Due to poor performance of I/O Port
** access under HP PCI bus adapters, strongly reccomend use of MMIO
** address space.
**
** While I'm at it more PA programming notes:
**
** 1) MMIO stores (writes) are posted operations. This means the processor
**    gets an "ACK" before the write actually gets to the device. A read
**    to the same device (or typically the bus adapter above it) will
**    force in-flight write transaction(s) out to the targeted device
**    before the read can complete.
**
** 2) The Programmed I/O (PIO) data may not always be strongly ordered with
**    respect to DMA on all platforms. Ie PIO data can reach the processor
**    before in-flight DMA reaches memory. Since most SMP PA platforms
**    are I/O coherent, it generally doesn't matter...but sometimes
**    it does.
**
** I've helped device driver writers debug both types of problems.
*/
struct pci_port_ops {
	  u8 (*inb)  (struct pci_hba_data *hba, u16 port);
	 u16 (*inw)  (struct pci_hba_data *hba, u16 port);
	 u32 (*inl)  (struct pci_hba_data *hba, u16 port);
	void (*outb) (struct pci_hba_data *hba, u16 port,  u8 data);
	void (*outw) (struct pci_hba_data *hba, u16 port, u16 data);
	void (*outl) (struct pci_hba_data *hba, u16 port, u32 data);
};


struct pci_bios_ops {
	void (*init)(void);
	void (*fixup_bus)(struct pci_bus *bus);
};

extern void pcibios_size_bridge(struct pci_bus *, struct pbus_set_ranges_data *);


/*
** See Documentation/DMA-mapping.txt
*/
struct pci_dma_ops {
	int  (*dma_supported)(struct pci_dev *dev, dma_addr_t mask);
	void *(*alloc_consistent)(struct pci_dev *dev, size_t size, dma_addr_t *iova);
	void (*free_consistent)(struct pci_dev *dev, size_t size, void *vaddr, dma_addr_t iova);
	dma_addr_t (*map_single)(struct pci_dev *dev, void *addr, size_t size, int direction);
	void (*unmap_single)(struct pci_dev *dev, dma_addr_t iova, size_t size, int direction);
	int  (*map_sg)(struct pci_dev *dev, struct scatterlist *sg, int nents, int direction);
	void (*unmap_sg)(struct pci_dev *dev, struct scatterlist *sg, int nhwents, int direction);
	void (*dma_sync_single)(struct pci_dev *dev, dma_addr_t iova, size_t size, int direction);
	void (*dma_sync_sg)(struct pci_dev *dev, struct scatterlist *sg, int nelems, int direction);
};


/*
** We could live without the hppa_dma_ops indirection if we didn't want
** to support 4 different dma models with one binary or they were
** all loadable modules:
**     I/O MMU        consistent method           dma_sync behavior
**  =============   ======================       =======================
**  a) PA-7x00LC    uncachable host memory          flush/purge
**  b) U2/Uturn      cachable host memory              NOP
**  c) Ike/Astro     cachable host memory              NOP
**  d) EPIC/SAGA     memory on EPIC/SAGA         flush/reset DMA channel
**
** PA-7[13]00LC processors have a GSC bus interface and no I/O MMU.
**
** Systems (eg PCX-T workstations) that don't fall into the above
** categories will need to modify the needed drivers to perform
** flush/purge and allocate "regular" cacheable pages for everything.
*/

extern struct pci_dma_ops *hppa_dma_ops;
extern struct pci_dma_ops pcxl_dma_ops;
extern struct pci_dma_ops pcx_dma_ops;

/*
** Oops hard if we haven't setup hppa_dma_ops by the time the first driver
** attempts to initialize.
** Since panic() is a (void)(), pci_dma_panic() is needed to satisfy
** the (int)() required by pci_dma_supported() interface.
*/
static inline int pci_dma_panic(char *msg)
{
	panic(msg);
	return -1;
}

#define pci_dma_supported(p, m)	( \
	(NULL == hppa_dma_ops) \
	?  pci_dma_panic("Dynamic DMA support missing...OOPS!\n(Hint: was Astro/Ike/U2/Uturn not claimed?)\n") \
	: hppa_dma_ops->dma_supported(p,m) \
)

#define pci_alloc_consistent(p, s, a)	hppa_dma_ops->alloc_consistent(p,s,a)
#define pci_free_consistent(p, s, v, a)	hppa_dma_ops->free_consistent(p,s,v,a)
#define pci_map_single(p, v, s, d)	hppa_dma_ops->map_single(p, v, s, d)
#define pci_unmap_single(p, a, s, d)	hppa_dma_ops->unmap_single(p, a, s, d)
#define pci_map_sg(p, sg, n, d)		hppa_dma_ops->map_sg(p, sg, n, d)
#define pci_unmap_sg(p, sg, n, d)	hppa_dma_ops->unmap_sg(p, sg, n, d)

/* For U2/Astro/Ike based platforms (which are fully I/O coherent)
** dma_sync is a NOP. Let's keep the performance path short here.
*/
#define pci_dma_sync_single(p, a, s, d)	{ if (hppa_dma_ops->dma_sync_single) \
	hppa_dma_ops->dma_sync_single(p, a, s, d); \
	}
#define pci_dma_sync_sg(p, sg, n, d)	{ if (hppa_dma_ops->dma_sync_sg) \
	hppa_dma_ops->dma_sync_sg(p, sg, n, d); \
	}

/*
** Stuff declared in arch/parisc/kernel/pci.c
*/
extern struct pci_port_ops *pci_port;
extern struct pci_bios_ops *pci_bios;
extern int pci_post_reset_delay;	/* delay after de-asserting #RESET */

extern void pcibios_register_hba(struct pci_hba_data *);
extern void pcibios_assign_unassigned_resources(struct pci_bus *);


/*
** used by drivers/pci/pci.c:pci_do_scan_bus()
**   0 == check if bridge is numbered before re-numbering.
**   1 == pci_do_scan_bus() should automatically number all PCI-PCI bridges.
**
** REVISIT:
**   To date, only alpha sets this to one. We'll need to set this
**   to zero for legacy platforms and one for PAT platforms.
*/
#ifdef __LP64__
extern int pdc_pat;  /* arch/parisc/kernel/inventory.c */
#define pcibios_assign_all_busses()	pdc_pat
#else
#define pcibios_assign_all_busses()	0
#endif

#define PCIBIOS_MIN_IO          0x10
#define PCIBIOS_MIN_MEM         0x1000 /* NBPG - but pci/setup-res.c dies */

#endif /* __ASM_PARISC_PCI_H */
