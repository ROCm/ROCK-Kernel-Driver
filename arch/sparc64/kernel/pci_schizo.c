/* $Id: pci_schizo.c,v 1.24 2002/01/23 11:27:32 davem Exp $
 * pci_schizo.c: SCHIZO/TOMATILLO specific PCI controller support.
 *
 * Copyright (C) 2001, 2002, 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <asm/pbm.h>
#include <asm/iommu.h>
#include <asm/irq.h>
#include <asm/upa.h>

#include "pci_impl.h"
#include "iommu_common.h"

/* All SCHIZO registers are 64-bits.  The following accessor
 * routines are how they are accessed.  The REG parameter
 * is a physical address.
 */
#define schizo_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define schizo_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory")

/* This is a convention that at least Excalibur and Merlin
 * follow.  I suppose the SCHIZO used in Starcat and friends
 * will do similar.
 *
 * The only way I could see this changing is if the newlink
 * block requires more space in Schizo's address space than
 * they predicted, thus requiring an address space reorg when
 * the newer Schizo is taped out.
 *
 * These offsets look weird because I keep in p->controller_regs
 * the second PROM register property minus 0x10000 which is the
 * base of the Safari and UPA64S registers of SCHIZO.
 */
#define SCHIZO_PBM_A_REGS_OFF	(0x600000UL - 0x400000UL)
#define SCHIZO_PBM_B_REGS_OFF	(0x700000UL - 0x400000UL)

/* Streaming buffer control register. */
#define SCHIZO_STRBUF_CTRL_LPTR    0x00000000000000f0UL /* LRU Lock Pointer */
#define SCHIZO_STRBUF_CTRL_LENAB   0x0000000000000008UL /* LRU Lock Enable */
#define SCHIZO_STRBUF_CTRL_RRDIS   0x0000000000000004UL /* Rerun Disable */
#define SCHIZO_STRBUF_CTRL_DENAB   0x0000000000000002UL /* Diagnostic Mode Enable */
#define SCHIZO_STRBUF_CTRL_ENAB    0x0000000000000001UL /* Streaming Buffer Enable */

/* IOMMU control register. */
#define SCHIZO_IOMMU_CTRL_RESV     0xfffffffff9000000 /* Reserved                      */
#define SCHIZO_IOMMU_CTRL_XLTESTAT 0x0000000006000000 /* Translation Error Status      */
#define SCHIZO_IOMMU_CTRL_XLTEERR  0x0000000001000000 /* Translation Error encountered */
#define SCHIZO_IOMMU_CTRL_LCKEN    0x0000000000800000 /* Enable translation locking    */
#define SCHIZO_IOMMU_CTRL_LCKPTR   0x0000000000780000 /* Translation lock pointer      */
#define SCHIZO_IOMMU_CTRL_TSBSZ    0x0000000000070000 /* TSB Size                      */
#define SCHIZO_IOMMU_TSBSZ_1K      0x0000000000000000 /* TSB Table 1024 8-byte entries */
#define SCHIZO_IOMMU_TSBSZ_2K      0x0000000000010000 /* TSB Table 2048 8-byte entries */
#define SCHIZO_IOMMU_TSBSZ_4K      0x0000000000020000 /* TSB Table 4096 8-byte entries */
#define SCHIZO_IOMMU_TSBSZ_8K      0x0000000000030000 /* TSB Table 8192 8-byte entries */
#define SCHIZO_IOMMU_TSBSZ_16K     0x0000000000040000 /* TSB Table 16k 8-byte entries  */
#define SCHIZO_IOMMU_TSBSZ_32K     0x0000000000050000 /* TSB Table 32k 8-byte entries  */
#define SCHIZO_IOMMU_TSBSZ_64K     0x0000000000060000 /* TSB Table 64k 8-byte entries  */
#define SCHIZO_IOMMU_TSBSZ_128K    0x0000000000070000 /* TSB Table 128k 8-byte entries */
#define SCHIZO_IOMMU_CTRL_RESV2    0x000000000000fff8 /* Reserved                      */
#define SCHIZO_IOMMU_CTRL_TBWSZ    0x0000000000000004 /* Assumed page size, 0=8k 1=64k */
#define SCHIZO_IOMMU_CTRL_DENAB    0x0000000000000002 /* Diagnostic mode enable        */
#define SCHIZO_IOMMU_CTRL_ENAB     0x0000000000000001 /* IOMMU Enable                  */

/* Schizo config space address format is nearly identical to
 * that of PSYCHO:
 *
 *  32             24 23 16 15    11 10       8 7   2  1 0
 * ---------------------------------------------------------
 * |0 0 0 0 0 0 0 0 0| bus | device | function | reg | 0 0 |
 * ---------------------------------------------------------
 */
#define SCHIZO_CONFIG_BASE(PBM)	((PBM)->config_space)
#define SCHIZO_CONFIG_ENCODE(BUS, DEVFN, REG)	\
	(((unsigned long)(BUS)   << 16) |	\
	 ((unsigned long)(DEVFN) << 8)  |	\
	 ((unsigned long)(REG)))

static void *schizo_pci_config_mkaddr(struct pci_pbm_info *pbm,
				      unsigned char bus,
				      unsigned int devfn,
				      int where)
{
	if (!pbm)
		return NULL;
	bus -= pbm->pci_first_busno;
	return (void *)
		(SCHIZO_CONFIG_BASE(pbm) |
		 SCHIZO_CONFIG_ENCODE(bus, devfn, where));
}

/* Just make sure the bus number is in range.  */
static int schizo_out_of_range(struct pci_pbm_info *pbm,
			       unsigned char bus,
			       unsigned char devfn)
{
	if (bus < pbm->pci_first_busno ||
	    bus > pbm->pci_last_busno)
		return 1;
	return 0;
}

/* SCHIZO PCI configuration space accessors. */

static int schizo_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
			       int where, int size, u32 *value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;
	u16 tmp16;
	u8 tmp8;

	switch (size) {
	case 1:
		*value = 0xff;
		break;
	case 2:
		*value = 0xffff;
		break;
	case 4:
		*value = 0xffffffff;
		break;
	}

	addr = schizo_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (schizo_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;
	switch (size) {
	case 1:
		pci_config_read8((u8 *)addr, &tmp8);
		*value = tmp8;
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_read_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_read16((u16 *)addr, &tmp16);
		*value = tmp16;
		break;

	case 4:
		if (where & 0x03) {
			printk("pci_read_config_dword: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_read32(addr, value);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int schizo_write_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
				int where, int size, u32 value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;

	addr = schizo_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (schizo_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case 1:
		pci_config_write8((u8 *)addr, value);
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_write_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_write16((u16 *)addr, value);
		break;

	case 4:
		if (where & 0x03) {
			printk("pci_write_config_dword: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}

		pci_config_write32(addr, value);
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops schizo_ops = {
	.read =		schizo_read_pci_cfg,
	.write =	schizo_write_pci_cfg,
};

/* SCHIZO interrupt mapping support.  Unlike Psycho, for this controller the
 * imap/iclr registers are per-PBM.
 */
#define SCHIZO_IMAP_BASE	0x1000UL
#define SCHIZO_ICLR_BASE	0x1400UL

static unsigned long schizo_imap_offset(unsigned long ino)
{
	return SCHIZO_IMAP_BASE + (ino * 8UL);
}

static unsigned long schizo_iclr_offset(unsigned long ino)
{
	return SCHIZO_ICLR_BASE + (ino * 8UL);
}

/* PCI SCHIZO INO number to Sparc PIL level.  This table only matters for
 * INOs which will not have an associated PCI device struct, ie. onboard
 * EBUS devices and PCI controller internal error interrupts.
 */
static unsigned char schizo_pil_table[] = {
/*0x00*/0, 0, 0, 0,	/* PCI slot 0  Int A, B, C, D	*/
/*0x04*/0, 0, 0, 0,	/* PCI slot 1  Int A, B, C, D	*/
/*0x08*/0, 0, 0, 0,	/* PCI slot 2  Int A, B, C, D	*/
/*0x0c*/0, 0, 0, 0,	/* PCI slot 3  Int A, B, C, D	*/
/*0x10*/0, 0, 0, 0,	/* PCI slot 4  Int A, B, C, D	*/
/*0x14*/0, 0, 0, 0,	/* PCI slot 5  Int A, B, C, D	*/
/*0x18*/4,		/* SCSI				*/
/*0x19*/4,		/* second SCSI			*/
/*0x1a*/0,		/* UNKNOWN			*/
/*0x1b*/0,		/* UNKNOWN			*/
/*0x1c*/8,		/* Parallel			*/
/*0x1d*/5,		/* Ethernet			*/
/*0x1e*/8,		/* Firewire-1394		*/
/*0x1f*/9,		/* USB				*/
/*0x20*/13,		/* Audio Record			*/
/*0x21*/14,		/* Audio Playback		*/
/*0x22*/12,		/* Serial			*/
/*0x23*/4,		/* EBUS I2C 			*/
/*0x24*/10,		/* RTC Clock			*/
/*0x25*/11,		/* Floppy			*/
/*0x26*/0,		/* UNKNOWN			*/
/*0x27*/0,		/* UNKNOWN			*/
/*0x28*/0,		/* UNKNOWN			*/
/*0x29*/0,		/* UNKNOWN			*/
/*0x2a*/10,		/* UPA 1			*/
/*0x2b*/10,		/* UPA 2			*/
/*0x2c*/0,		/* UNKNOWN			*/
/*0x2d*/0,		/* UNKNOWN			*/
/*0x2e*/0,		/* UNKNOWN			*/
/*0x2f*/0,		/* UNKNOWN			*/
/*0x30*/15,		/* Uncorrectable ECC		*/
/*0x31*/15,		/* Correctable ECC		*/
/*0x32*/15,		/* PCI Bus A Error		*/
/*0x33*/15,		/* PCI Bus B Error		*/
/*0x34*/15,		/* Safari Bus Error		*/
/*0x35*/0,		/* Reserved			*/
/*0x36*/0,		/* Reserved			*/
/*0x37*/0,		/* Reserved			*/
/*0x38*/0,		/* Reserved for NewLink		*/
/*0x39*/0,		/* Reserved for NewLink		*/
/*0x3a*/0,		/* Reserved for NewLink		*/
/*0x3b*/0,		/* Reserved for NewLink		*/
/*0x3c*/0,		/* Reserved for NewLink		*/
/*0x3d*/0,		/* Reserved for NewLink		*/
/*0x3e*/0,		/* Reserved for NewLink		*/
/*0x3f*/0,		/* Reserved for NewLink		*/
};

static int __init schizo_ino_to_pil(struct pci_dev *pdev, unsigned int ino)
{
	int ret;

	if (pdev &&
	    pdev->vendor == PCI_VENDOR_ID_SUN &&
	    pdev->device == PCI_DEVICE_ID_SUN_RIO_USB)
		return 9;

	ret = schizo_pil_table[ino];
	if (ret == 0 && pdev == NULL) {
		ret = 4;
	} else if (ret == 0) {
		switch ((pdev->class >> 16) & 0xff) {
		case PCI_BASE_CLASS_STORAGE:
			ret = 4;
			break;

		case PCI_BASE_CLASS_NETWORK:
			ret = 6;
			break;

		case PCI_BASE_CLASS_DISPLAY:
			ret = 9;
			break;

		case PCI_BASE_CLASS_MULTIMEDIA:
		case PCI_BASE_CLASS_MEMORY:
		case PCI_BASE_CLASS_BRIDGE:
		case PCI_BASE_CLASS_SERIAL:
			ret = 10;
			break;

		default:
			ret = 4;
			break;
		};
	}

	return ret;
}

static unsigned int __init schizo_irq_build(struct pci_pbm_info *pbm,
					    struct pci_dev *pdev,
					    unsigned int ino)
{
	struct pci_controller_info *p = pbm->parent;
	struct ino_bucket *bucket;
	unsigned long imap, iclr, pbm_off;
	unsigned long imap_off, iclr_off;
	int pil;

	if (pbm == &p->pbm_A)
		pbm_off = SCHIZO_PBM_A_REGS_OFF;
	else
		pbm_off = SCHIZO_PBM_B_REGS_OFF;

	ino &= PCI_IRQ_INO;
	imap_off = schizo_imap_offset(ino);

	/* Now build the IRQ bucket. */
	pil = schizo_ino_to_pil(pdev, ino);

	if (PIL_RESERVED(pil))
		BUG();

	imap = p->controller_regs + pbm_off + imap_off;
	imap += 4;

	iclr_off = schizo_iclr_offset(ino);
	iclr = p->controller_regs + pbm_off + iclr_off;
	iclr += 4;

	/* On Schizo, no inofixup occurs.  This is because each
	 * INO has it's own IMAP register.  On Psycho and Sabre
	 * there is only one IMAP register for each PCI slot even
	 * though four different INOs can be generated by each
	 * PCI slot.
	 */
	bucket = __bucket(build_irq(pil, 0, iclr, imap));
	bucket->flags |= IBF_PCI;

	return __irq(bucket);
}

/* SCHIZO error handling support. */
enum schizo_error_type {
	UE_ERR, CE_ERR, PCI_ERR, SAFARI_ERR
};

static spinlock_t stc_buf_lock = SPIN_LOCK_UNLOCKED;
static unsigned long stc_error_buf[128];
static unsigned long stc_tag_buf[16];
static unsigned long stc_line_buf[16];

static void schizo_clear_other_err_intr(int irq)
{
	struct ino_bucket *bucket = __bucket(irq);
	unsigned long iclr = bucket->iclr;

	iclr += (SCHIZO_PBM_B_REGS_OFF - SCHIZO_PBM_A_REGS_OFF);
	upa_writel(ICLR_IDLE, iclr);
}

#define SCHIZO_STC_ERR	0xb800UL /* --> 0xba00 */
#define SCHIZO_STC_TAG	0xba00UL /* --> 0xba80 */
#define SCHIZO_STC_LINE	0xbb00UL /* --> 0xbb80 */

#define SCHIZO_STCERR_WRITE	0x2UL
#define SCHIZO_STCERR_READ	0x1UL

#define SCHIZO_STCTAG_PPN	0x3fffffff00000000UL
#define SCHIZO_STCTAG_VPN	0x00000000ffffe000UL
#define SCHIZO_STCTAG_VALID	0x8000000000000000UL
#define SCHIZO_STCTAG_READ	0x4000000000000000UL

#define SCHIZO_STCLINE_LINDX	0x0000000007800000UL
#define SCHIZO_STCLINE_SPTR	0x000000000007e000UL
#define SCHIZO_STCLINE_LADDR	0x0000000000001fc0UL
#define SCHIZO_STCLINE_EPTR	0x000000000000003fUL
#define SCHIZO_STCLINE_VALID	0x0000000000600000UL
#define SCHIZO_STCLINE_FOFN	0x0000000000180000UL

static void __schizo_check_stc_error_pbm(struct pci_pbm_info *pbm,
					 enum schizo_error_type type)
{
	struct pci_controller_info *p = pbm->parent;
	struct pci_strbuf *strbuf = &pbm->stc;
	unsigned long regbase = p->controller_regs;
	unsigned long err_base, tag_base, line_base;
	u64 control;
	int i;

	if (pbm == &p->pbm_A)
		regbase += SCHIZO_PBM_A_REGS_OFF;
	else
		regbase += SCHIZO_PBM_B_REGS_OFF;

	err_base = regbase + SCHIZO_STC_ERR;
	tag_base = regbase + SCHIZO_STC_TAG;
	line_base = regbase + SCHIZO_STC_LINE;

	spin_lock(&stc_buf_lock);

	/* This is __REALLY__ dangerous.  When we put the
	 * streaming buffer into diagnostic mode to probe
	 * it's tags and error status, we _must_ clear all
	 * of the line tag valid bits before re-enabling
	 * the streaming buffer.  If any dirty data lives
	 * in the STC when we do this, we will end up
	 * invalidating it before it has a chance to reach
	 * main memory.
	 */
	control = schizo_read(strbuf->strbuf_control);
	schizo_write(strbuf->strbuf_control,
		     (control | SCHIZO_STRBUF_CTRL_DENAB));
	for (i = 0; i < 128; i++) {
		unsigned long val;

		val = schizo_read(err_base + (i * 8UL));
		schizo_write(err_base + (i * 8UL), 0UL);
		stc_error_buf[i] = val;
	}
	for (i = 0; i < 16; i++) {
		stc_tag_buf[i] = schizo_read(tag_base + (i * 8UL));
		stc_line_buf[i] = schizo_read(line_base + (i * 8UL));
		schizo_write(tag_base + (i * 8UL), 0UL);
		schizo_write(line_base + (i * 8UL), 0UL);
	}

	/* OK, state is logged, exit diagnostic mode. */
	schizo_write(strbuf->strbuf_control, control);

	for (i = 0; i < 16; i++) {
		int j, saw_error, first, last;

		saw_error = 0;
		first = i * 8;
		last = first + 8;
		for (j = first; j < last; j++) {
			unsigned long errval = stc_error_buf[j];
			if (errval != 0) {
				saw_error++;
				printk("%s: STC_ERR(%d)[wr(%d)rd(%d)]\n",
				       pbm->name,
				       j,
				       (errval & SCHIZO_STCERR_WRITE) ? 1 : 0,
				       (errval & SCHIZO_STCERR_READ) ? 1 : 0);
			}
		}
		if (saw_error != 0) {
			unsigned long tagval = stc_tag_buf[i];
			unsigned long lineval = stc_line_buf[i];
			printk("%s: STC_TAG(%d)[PA(%016lx)VA(%08lx)V(%d)R(%d)]\n",
			       pbm->name,
			       i,
			       ((tagval & SCHIZO_STCTAG_PPN) >> 19UL),
			       (tagval & SCHIZO_STCTAG_VPN),
			       ((tagval & SCHIZO_STCTAG_VALID) ? 1 : 0),
			       ((tagval & SCHIZO_STCTAG_READ) ? 1 : 0));

			/* XXX Should spit out per-bank error information... -DaveM */
			printk("%s: STC_LINE(%d)[LIDX(%lx)SP(%lx)LADDR(%lx)EP(%lx)"
			       "V(%d)FOFN(%d)]\n",
			       pbm->name,
			       i,
			       ((lineval & SCHIZO_STCLINE_LINDX) >> 23UL),
			       ((lineval & SCHIZO_STCLINE_SPTR) >> 13UL),
			       ((lineval & SCHIZO_STCLINE_LADDR) >> 6UL),
			       ((lineval & SCHIZO_STCLINE_EPTR) >> 0UL),
			       ((lineval & SCHIZO_STCLINE_VALID) ? 1 : 0),
			       ((lineval & SCHIZO_STCLINE_FOFN) ? 1 : 0));
		}
	}

	spin_unlock(&stc_buf_lock);
}

/* IOMMU is per-PBM in Schizo, so interrogate both for anonymous
 * controller level errors.
 */

#define SCHIZO_IOMMU_TAG	0xa580UL
#define SCHIZO_IOMMU_DATA	0xa600UL

#define SCHIZO_IOMMU_TAG_CTXT	0x0000001ffe000000UL
#define SCHIZO_IOMMU_TAG_ERRSTS	0x0000000001800000UL
#define SCHIZO_IOMMU_TAG_ERR	0x0000000000400000UL
#define SCHIZO_IOMMU_TAG_WRITE	0x0000000000200000UL
#define SCHIZO_IOMMU_TAG_STREAM	0x0000000000100000UL
#define SCHIZO_IOMMU_TAG_SIZE	0x0000000000080000UL
#define SCHIZO_IOMMU_TAG_VPAGE	0x000000000007ffffUL

#define SCHIZO_IOMMU_DATA_VALID	0x0000000100000000UL
#define SCHIZO_IOMMU_DATA_CACHE	0x0000000040000000UL
#define SCHIZO_IOMMU_DATA_PPAGE	0x000000003fffffffUL

static void schizo_check_iommu_error_pbm(struct pci_pbm_info *pbm,
					 enum schizo_error_type type)
{
	struct pci_controller_info *p = pbm->parent;
	struct pci_iommu *iommu = pbm->iommu;
	unsigned long iommu_tag[16];
	unsigned long iommu_data[16];
	unsigned long flags;
	u64 control;
	int i;

	spin_lock_irqsave(&iommu->lock, flags);
	control = schizo_read(iommu->iommu_control);
	if (control & SCHIZO_IOMMU_CTRL_XLTEERR) {
		unsigned long base;
		char *type_string;

		/* Clear the error encountered bit. */
		control &= ~SCHIZO_IOMMU_CTRL_XLTEERR;
		schizo_write(iommu->iommu_control, control);

		switch((control & SCHIZO_IOMMU_CTRL_XLTESTAT) >> 25UL) {
		case 0:
			type_string = "Protection Error";
			break;
		case 1:
			type_string = "Invalid Error";
			break;
		case 2:
			type_string = "TimeOut Error";
			break;
		case 3:
		default:
			type_string = "ECC Error";
			break;
		};
		printk("%s: IOMMU Error, type[%s]\n",
		       pbm->name, type_string);

		/* Put the IOMMU into diagnostic mode and probe
		 * it's TLB for entries with error status.
		 *
		 * It is very possible for another DVMA to occur
		 * while we do this probe, and corrupt the system
		 * further.  But we are so screwed at this point
		 * that we are likely to crash hard anyways, so
		 * get as much diagnostic information to the
		 * console as we can.
		 */
		schizo_write(iommu->iommu_control,
			     control | SCHIZO_IOMMU_CTRL_DENAB);

		base = p->controller_regs;
		if (pbm == &p->pbm_A)
			base += SCHIZO_PBM_A_REGS_OFF;
		else
			base += SCHIZO_PBM_B_REGS_OFF;

		for (i = 0; i < 16; i++) {
			iommu_tag[i] =
				schizo_read(base + SCHIZO_IOMMU_TAG + (i * 8UL));
			iommu_data[i] =
				schizo_read(base + SCHIZO_IOMMU_DATA + (i * 8UL));

			/* Now clear out the entry. */
			schizo_write(base + SCHIZO_IOMMU_TAG + (i * 8UL), 0);
			schizo_write(base + SCHIZO_IOMMU_DATA + (i * 8UL), 0);
		}

		/* Leave diagnostic mode. */
		schizo_write(iommu->iommu_control, control);

		for (i = 0; i < 16; i++) {
			unsigned long tag, data;

			tag = iommu_tag[i];
			if (!(tag & SCHIZO_IOMMU_TAG_ERR))
				continue;

			data = iommu_data[i];
			switch((tag & SCHIZO_IOMMU_TAG_ERRSTS) >> 23UL) {
			case 0:
				type_string = "Protection Error";
				break;
			case 1:
				type_string = "Invalid Error";
				break;
			case 2:
				type_string = "TimeOut Error";
				break;
			case 3:
			default:
				type_string = "ECC Error";
				break;
			};
			printk("%s: IOMMU TAG(%d)[error(%s) ctx(%x) wr(%d) str(%d) "
			       "sz(%dK) vpg(%08lx)]\n",
			       pbm->name, i, type_string,
			       (int)((tag & SCHIZO_IOMMU_TAG_CTXT) >> 25UL),
			       ((tag & SCHIZO_IOMMU_TAG_WRITE) ? 1 : 0),
			       ((tag & SCHIZO_IOMMU_TAG_STREAM) ? 1 : 0),
			       ((tag & SCHIZO_IOMMU_TAG_SIZE) ? 64 : 8),
			       (tag & SCHIZO_IOMMU_TAG_VPAGE) << IOMMU_PAGE_SHIFT);
			printk("%s: IOMMU DATA(%d)[valid(%d) cache(%d) ppg(%016lx)]\n",
			       pbm->name, i,
			       ((data & SCHIZO_IOMMU_DATA_VALID) ? 1 : 0),
			       ((data & SCHIZO_IOMMU_DATA_CACHE) ? 1 : 0),
			       (data & SCHIZO_IOMMU_DATA_PPAGE) << IOMMU_PAGE_SHIFT);
		}
	}
	if (pbm->stc.strbuf_enabled)
		__schizo_check_stc_error_pbm(pbm, type);
	spin_unlock_irqrestore(&iommu->lock, flags);
}

static void schizo_check_iommu_error(struct pci_controller_info *p,
				     enum schizo_error_type type)
{
	schizo_check_iommu_error_pbm(&p->pbm_A, type);
	schizo_check_iommu_error_pbm(&p->pbm_B, type);
}

/* Uncorrectable ECC error status gathering. */
#define SCHIZO_UE_AFSR	0x10030UL
#define SCHIZO_UE_AFAR	0x10038UL

#define SCHIZO_UEAFSR_PPIO	0x8000000000000000UL /* Safari */
#define SCHIZO_UEAFSR_PDRD	0x4000000000000000UL /* Safari/Tomatillo */
#define SCHIZO_UEAFSR_PDWR	0x2000000000000000UL /* Safari */
#define SCHIZO_UEAFSR_SPIO	0x1000000000000000UL /* Safari */
#define SCHIZO_UEAFSR_SDMA	0x0800000000000000UL /* Safari/Tomatillo */
#define SCHIZO_UEAFSR_ERRPNDG	0x0300000000000000UL /* Safari */
#define SCHIZO_UEAFSR_BMSK	0x000003ff00000000UL /* Safari */
#define SCHIZO_UEAFSR_QOFF	0x00000000c0000000UL /* Safari/Tomatillo */
#define SCHIZO_UEAFSR_AID	0x000000001f000000UL /* Safari/Tomatillo */
#define SCHIZO_UEAFSR_PARTIAL	0x0000000000800000UL /* Safari */
#define SCHIZO_UEAFSR_OWNEDIN	0x0000000000400000UL /* Safari */
#define SCHIZO_UEAFSR_MTAGSYND	0x00000000000f0000UL /* Safari */
#define SCHIZO_UEAFSR_MTAG	0x000000000000e000UL /* Safari */
#define SCHIZO_UEAFSR_ECCSYND	0x00000000000001ffUL /* Safari */

static irqreturn_t schizo_ue_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	unsigned long afsr_reg = p->controller_regs + SCHIZO_UE_AFSR;
	unsigned long afar_reg = p->controller_regs + SCHIZO_UE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported, limit;

	/* Latch uncorrectable error status. */
	afar = schizo_read(afar_reg);

	/* If either of the error pending bits are set in the
	 * AFSR, the error status is being actively updated by
	 * the hardware and we must re-read to get a clean value.
	 */
	limit = 1000;
	do {
		afsr = schizo_read(afsr_reg);
	} while ((afsr & SCHIZO_UEAFSR_ERRPNDG) != 0 && --limit);

	/* Clear the primary/secondary error status bits. */
	error_bits = afsr &
		(SCHIZO_UEAFSR_PPIO | SCHIZO_UEAFSR_PDRD | SCHIZO_UEAFSR_PDWR |
		 SCHIZO_UEAFSR_SPIO | SCHIZO_UEAFSR_SDMA);
	if (!error_bits)
		return IRQ_NONE;
	schizo_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PCI%d: Uncorrectable Error, primary error type[%s]\n",
	       p->index,
	       (((error_bits & SCHIZO_UEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & SCHIZO_UEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & SCHIZO_UEAFSR_PDWR) ?
		   "DMA Write" : "???")))));
	printk("PCI%d: bytemask[%04lx] qword_offset[%lx] SAFARI_AID[%02lx]\n",
	       p->index,
	       (afsr & SCHIZO_UEAFSR_BMSK) >> 32UL,
	       (afsr & SCHIZO_UEAFSR_QOFF) >> 30UL,
	       (afsr & SCHIZO_UEAFSR_AID) >> 24UL);
	printk("PCI%d: partial[%d] owned_in[%d] mtag[%lx] mtag_synd[%lx] ecc_sync[%lx]\n",
	       p->index,
	       (afsr & SCHIZO_UEAFSR_PARTIAL) ? 1 : 0,
	       (afsr & SCHIZO_UEAFSR_OWNEDIN) ? 1 : 0,
	       (afsr & SCHIZO_UEAFSR_MTAG) >> 13UL,
	       (afsr & SCHIZO_UEAFSR_MTAGSYND) >> 16UL,
	       (afsr & SCHIZO_UEAFSR_ECCSYND) >> 0UL);
	printk("PCI%d: UE AFAR [%016lx]\n", p->index, afar);
	printk("PCI%d: UE Secondary errors [", p->index);
	reported = 0;
	if (afsr & SCHIZO_UEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & SCHIZO_UEAFSR_SDMA) {
		reported++;
		printk("(DMA)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* Interrogate IOMMU for error status. */
	schizo_check_iommu_error(p, UE_ERR);

	schizo_clear_other_err_intr(irq);

	return IRQ_HANDLED;
}

#define SCHIZO_CE_AFSR	0x10040UL
#define SCHIZO_CE_AFAR	0x10048UL

#define SCHIZO_CEAFSR_PPIO	0x8000000000000000UL
#define SCHIZO_CEAFSR_PDRD	0x4000000000000000UL
#define SCHIZO_CEAFSR_PDWR	0x2000000000000000UL
#define SCHIZO_CEAFSR_SPIO	0x1000000000000000UL
#define SCHIZO_CEAFSR_SDMA	0x0800000000000000UL
#define SCHIZO_CEAFSR_ERRPNDG	0x0300000000000000UL
#define SCHIZO_CEAFSR_BMSK	0x000003ff00000000UL
#define SCHIZO_CEAFSR_QOFF	0x00000000c0000000UL
#define SCHIZO_CEAFSR_AID	0x000000001f000000UL
#define SCHIZO_CEAFSR_PARTIAL	0x0000000000800000UL
#define SCHIZO_CEAFSR_OWNEDIN	0x0000000000400000UL
#define SCHIZO_CEAFSR_MTAGSYND	0x00000000000f0000UL
#define SCHIZO_CEAFSR_MTAG	0x000000000000e000UL
#define SCHIZO_CEAFSR_ECCSYND	0x00000000000001ffUL

static irqreturn_t schizo_ce_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	unsigned long afsr_reg = p->controller_regs + SCHIZO_CE_AFSR;
	unsigned long afar_reg = p->controller_regs + SCHIZO_CE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported, limit;

	/* Latch error status. */
	afar = schizo_read(afar_reg);

	/* If either of the error pending bits are set in the
	 * AFSR, the error status is being actively updated by
	 * the hardware and we must re-read to get a clean value.
	 */
	limit = 1000;
	do {
		afsr = schizo_read(afsr_reg);
	} while ((afsr & SCHIZO_UEAFSR_ERRPNDG) != 0 && --limit);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SCHIZO_CEAFSR_PPIO | SCHIZO_CEAFSR_PDRD | SCHIZO_CEAFSR_PDWR |
		 SCHIZO_CEAFSR_SPIO | SCHIZO_CEAFSR_SDMA);
	if (!error_bits)
		return IRQ_NONE;
	schizo_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PCI%d: Correctable Error, primary error type[%s]\n",
	       p->index,
	       (((error_bits & SCHIZO_CEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & SCHIZO_CEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & SCHIZO_CEAFSR_PDWR) ?
		   "DMA Write" : "???")))));

	/* XXX Use syndrome and afar to print out module string just like
	 * XXX UDB CE trap handler does... -DaveM
	 */
	printk("PCI%d: bytemask[%04lx] qword_offset[%lx] SAFARI_AID[%02lx]\n",
	       p->index,
	       (afsr & SCHIZO_UEAFSR_BMSK) >> 32UL,
	       (afsr & SCHIZO_UEAFSR_QOFF) >> 30UL,
	       (afsr & SCHIZO_UEAFSR_AID) >> 24UL);
	printk("PCI%d: partial[%d] owned_in[%d] mtag[%lx] mtag_synd[%lx] ecc_sync[%lx]\n",
	       p->index,
	       (afsr & SCHIZO_UEAFSR_PARTIAL) ? 1 : 0,
	       (afsr & SCHIZO_UEAFSR_OWNEDIN) ? 1 : 0,
	       (afsr & SCHIZO_UEAFSR_MTAG) >> 13UL,
	       (afsr & SCHIZO_UEAFSR_MTAGSYND) >> 16UL,
	       (afsr & SCHIZO_UEAFSR_ECCSYND) >> 0UL);
	printk("PCI%d: CE AFAR [%016lx]\n", p->index, afar);
	printk("PCI%d: CE Secondary errors [", p->index);
	reported = 0;
	if (afsr & SCHIZO_CEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & SCHIZO_CEAFSR_SDMA) {
		reported++;
		printk("(DMA)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	schizo_clear_other_err_intr(irq);

	return IRQ_HANDLED;
}

#define SCHIZO_PCI_AFSR	0x2010UL
#define SCHIZO_PCI_AFAR	0x2018UL

#define SCHIZO_PCIAFSR_PMA	0x8000000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PTA	0x4000000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PRTRY	0x2000000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PPERR	0x1000000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PTTO	0x0800000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_PUNUS	0x0400000000000000UL /* Schizo */
#define SCHIZO_PCIAFSR_SMA	0x0200000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_STA	0x0100000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_SRTRY	0x0080000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_SPERR	0x0040000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_STTO	0x0020000000000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_SUNUS	0x0010000000000000UL /* Schizo */
#define SCHIZO_PCIAFSR_BMSK	0x000003ff00000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_BLK	0x0000000080000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_CFG	0x0000000040000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_MEM	0x0000000020000000UL /* Schizo/Tomatillo */
#define SCHIZO_PCIAFSR_IO	0x0000000010000000UL /* Schizo/Tomatillo */

static irqreturn_t schizo_pcierr_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_pbm_info *pbm = dev_id;
	struct pci_controller_info *p = pbm->parent;
	unsigned long afsr_reg, afar_reg, base;
	unsigned long afsr, afar, error_bits;
	int reported;

	base = p->controller_regs;
	if (pbm == &pbm->parent->pbm_A) {
		base += SCHIZO_PBM_A_REGS_OFF;
	} else {
		base += SCHIZO_PBM_B_REGS_OFF;
	}

	afsr_reg = base + SCHIZO_PCI_AFSR;
	afar_reg = base + SCHIZO_PCI_AFAR;

	/* Latch error status. */
	afar = schizo_read(afar_reg);
	afsr = schizo_read(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_PTA |
		 SCHIZO_PCIAFSR_PRTRY | SCHIZO_PCIAFSR_PPERR |
		 SCHIZO_PCIAFSR_PTTO | SCHIZO_PCIAFSR_PUNUS |
		 SCHIZO_PCIAFSR_SMA | SCHIZO_PCIAFSR_STA |
		 SCHIZO_PCIAFSR_SRTRY | SCHIZO_PCIAFSR_SPERR |
		 SCHIZO_PCIAFSR_STTO | SCHIZO_PCIAFSR_SUNUS);
	if (!error_bits)
		return IRQ_NONE;
	schizo_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("%s: PCI Error, primary error type[%s]\n",
	       pbm->name,
	       (((error_bits & SCHIZO_PCIAFSR_PMA) ?
		 "Master Abort" :
		 ((error_bits & SCHIZO_PCIAFSR_PTA) ?
		  "Target Abort" :
		  ((error_bits & SCHIZO_PCIAFSR_PRTRY) ?
		   "Excessive Retries" :
		   ((error_bits & SCHIZO_PCIAFSR_PPERR) ?
		    "Parity Error" :
		    ((error_bits & SCHIZO_PCIAFSR_PTTO) ?
		     "Timeout" :
		     ((error_bits & SCHIZO_PCIAFSR_PUNUS) ?
		      "Bus Unusable" : "???"))))))));
	printk("%s: bytemask[%04lx] was_block(%d) space(%s)\n",
	       pbm->name,
	       (afsr & SCHIZO_PCIAFSR_BMSK) >> 32UL,
	       (afsr & SCHIZO_PCIAFSR_BLK) ? 1 : 0,
	       ((afsr & SCHIZO_PCIAFSR_CFG) ?
		"Config" :
		((afsr & SCHIZO_PCIAFSR_MEM) ?
		 "Memory" :
		 ((afsr & SCHIZO_PCIAFSR_IO) ?
		  "I/O" : "???"))));
	printk("%s: PCI AFAR [%016lx]\n",
	       pbm->name, afar);
	printk("%s: PCI Secondary errors [",
	       pbm->name);
	reported = 0;
	if (afsr & SCHIZO_PCIAFSR_SMA) {
		reported++;
		printk("(Master Abort)");
	}
	if (afsr & SCHIZO_PCIAFSR_STA) {
		reported++;
		printk("(Target Abort)");
	}
	if (afsr & SCHIZO_PCIAFSR_SRTRY) {
		reported++;
		printk("(Excessive Retries)");
	}
	if (afsr & SCHIZO_PCIAFSR_SPERR) {
		reported++;
		printk("(Parity Error)");
	}
	if (afsr & SCHIZO_PCIAFSR_STTO) {
		reported++;
		printk("(Timeout)");
	}
	if (afsr & SCHIZO_PCIAFSR_SUNUS) {
		reported++;
		printk("(Bus Unusable)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* For the error types shown, scan PBM's PCI bus for devices
	 * which have logged that error type.
	 */

	/* If we see a Target Abort, this could be the result of an
	 * IOMMU translation error of some sort.  It is extremely
	 * useful to log this information as usually it indicates
	 * a bug in the IOMMU support code or a PCI device driver.
	 */
	if (error_bits & (SCHIZO_PCIAFSR_PTA | SCHIZO_PCIAFSR_STA)) {
		schizo_check_iommu_error(p, PCI_ERR);
		pci_scan_for_target_abort(p, pbm, pbm->pci_bus);
	}
	if (error_bits & (SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_SMA))
		pci_scan_for_master_abort(p, pbm, pbm->pci_bus);

	/* For excessive retries, PSYCHO/PBM will abort the device
	 * and there is no way to specifically check for excessive
	 * retries in the config space status registers.  So what
	 * we hope is that we'll catch it via the master/target
	 * abort events.
	 */

	if (error_bits & (SCHIZO_PCIAFSR_PPERR | SCHIZO_PCIAFSR_SPERR))
		pci_scan_for_parity_error(p, pbm, pbm->pci_bus);

	schizo_clear_other_err_intr(irq);

	return IRQ_HANDLED;
}

#define SCHIZO_SAFARI_ERRLOG	0x10018UL

#define SAFARI_ERRLOG_ERROUT	0x8000000000000000UL

#define BUS_ERROR_BADCMD	0x4000000000000000UL /* Schizo/Tomatillo */
#define BUS_ERROR_SSMDIS	0x2000000000000000UL /* Safari */
#define BUS_ERROR_BADMA		0x1000000000000000UL /* Safari */
#define BUS_ERROR_BADMB		0x0800000000000000UL /* Safari */
#define BUS_ERROR_BADMC		0x0400000000000000UL /* Safari */
#define BUS_ERROR_SNOOP_GR	0x0000000000200000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_PCI	0x0000000000100000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_RD	0x0000000000080000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_RDS	0x0000000000020000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_RDSA	0x0000000000010000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_OWN	0x0000000000008000UL /* Tomatillo */
#define BUS_ERROR_SNOOP_RDO	0x0000000000004000UL /* Tomatillo */
#define BUS_ERROR_CPU1PS	0x0000000000002000UL /* Safari */
#define BUS_ERROR_WDATA_PERR	0x0000000000002000UL /* Tomatillo */
#define BUS_ERROR_CPU1PB	0x0000000000001000UL /* Safari */
#define BUS_ERROR_CTRL_PERR	0x0000000000001000UL /* Tomatillo */
#define BUS_ERROR_CPU0PS	0x0000000000000800UL /* Safari */
#define BUS_ERROR_SNOOP_ERR	0x0000000000000800UL /* Tomatillo */
#define BUS_ERROR_CPU0PB	0x0000000000000400UL /* Safari */
#define BUS_ERROR_JBUS_ILL_B	0x0000000000000400UL /* Tomatillo */
#define BUS_ERROR_CIQTO		0x0000000000000200UL /* Safari */
#define BUS_ERROR_LPQTO		0x0000000000000100UL /* Safari */
#define BUS_ERROR_JBUS_ILL_C	0x0000000000000100UL /* Tomatillo */
#define BUS_ERROR_SFPQTO	0x0000000000000080UL /* Safari */
#define BUS_ERROR_UFPQTO	0x0000000000000040UL /* Safari */
#define BUS_ERROR_RD_PERR	0x0000000000000040UL /* Tomatillo */
#define BUS_ERROR_APERR		0x0000000000000020UL /* Safari/Tomatillo */
#define BUS_ERROR_UNMAP		0x0000000000000010UL /* Safari/Tomatillo */
#define BUS_ERROR_BUSERR	0x0000000000000004UL /* Safari/Tomatillo */
#define BUS_ERROR_TIMEOUT	0x0000000000000002UL /* Safari/Tomatillo */
#define BUS_ERROR_ILL		0x0000000000000001UL /* Safari */

/* We only expect UNMAP errors here.  The rest of the Safari errors
 * are marked fatal and thus cause a system reset.
 */
static irqreturn_t schizo_safarierr_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	u64 errlog;

	errlog = schizo_read(p->controller_regs + SCHIZO_SAFARI_ERRLOG);
	schizo_write(p->controller_regs + SCHIZO_SAFARI_ERRLOG,
		     errlog & ~(SAFARI_ERRLOG_ERROUT));

	if (!(errlog & BUS_ERROR_UNMAP)) {
		printk("PCI%d: Unexpected Safari/JBUS error interrupt, errlog[%016lx]\n",
		       p->index, errlog);

		schizo_clear_other_err_intr(irq);
		return IRQ_HANDLED;
	}

	printk("PCI%d: Safari/JBUS interrupt, UNMAPPED error, interrogating IOMMUs.\n",
	       p->index);
	schizo_check_iommu_error(p, SAFARI_ERR);

	schizo_clear_other_err_intr(irq);
	return IRQ_HANDLED;
}

/* Nearly identical to PSYCHO equivalents... */
#define SCHIZO_ECC_CTRL		0x10020UL
#define  SCHIZO_ECCCTRL_EE	 0x8000000000000000 /* Enable ECC Checking */
#define  SCHIZO_ECCCTRL_UE	 0x4000000000000000 /* Enable UE Interrupts */
#define  SCHIZO_ECCCTRL_CE	 0x2000000000000000 /* Enable CE INterrupts */

#define SCHIZO_SAFARI_ERRCTRL	0x10008UL
#define  SCHIZO_SAFERRCTRL_EN	 0x8000000000000000UL
#define SCHIZO_SAFARI_IRQCTRL	0x10010UL
#define  SCHIZO_SAFIRQCTRL_EN	 0x8000000000000000UL

#define SCHIZO_UE_INO		0x30 /* Uncorrectable ECC error */
#define SCHIZO_CE_INO		0x31 /* Correctable ECC error */
#define SCHIZO_PCIERR_A_INO	0x32 /* PBM A PCI bus error */
#define SCHIZO_PCIERR_B_INO	0x33 /* PBM B PCI bus error */
#define SCHIZO_SERR_INO		0x34 /* Safari interface error */

#define SCHIZO_PCIA_CTRL	(SCHIZO_PBM_A_REGS_OFF + 0x2000UL)
#define SCHIZO_PCIB_CTRL	(SCHIZO_PBM_B_REGS_OFF + 0x2000UL)
#define SCHIZO_PCICTRL_BUS_UNUS	(1UL << 63UL) /* Safari */
#define SCHIZO_PCICTRL_ARB_PRIO (0x1ff << 52UL) /* Tomatillo */
#define SCHIZO_PCICTRL_ESLCK	(1UL << 51UL) /* Safari */
#define SCHIZO_PCICTRL_ERRSLOT	(7UL << 48UL) /* Safari */
#define SCHIZO_PCICTRL_TTO_ERR	(1UL << 38UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_RTRY_ERR	(1UL << 37UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_DTO_ERR	(1UL << 36UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_SBH_ERR	(1UL << 35UL) /* Safari */
#define SCHIZO_PCICTRL_SERR	(1UL << 34UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_PCISPD	(1UL << 33UL) /* Safari */
#define SCHIZO_PCICTRL_MRM_PREF	(1UL << 28UL) /* Tomatillo */
#define SCHIZO_PCICTRL_RDO_PREF	(1UL << 27UL) /* Tomatillo */
#define SCHIZO_PCICTRL_RDL_PREF	(1UL << 26UL) /* Tomatillo */
#define SCHIZO_PCICTRL_PTO	(3UL << 24UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_PTO_SHIFT 24UL
#define SCHIZO_PCICTRL_TRWSW	(7UL << 21UL) /* Tomatillo */
#define SCHIZO_PCICTRL_F_TGT_A	(1UL << 20UL) /* Tomatillo */
#define SCHIZO_PCICTRL_S_DTO_INT (1UL << 19UL) /* Safari */
#define SCHIZO_PCICTRL_F_TGT_RT	(1UL << 19UL) /* Tomatillo */
#define SCHIZO_PCICTRL_SBH_INT	(1UL << 18UL) /* Safari */
#define SCHIZO_PCICTRL_T_DTO_INT (1UL << 18UL) /* Tomatillo */
#define SCHIZO_PCICTRL_EEN	(1UL << 17UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_PARK	(1UL << 16UL) /* Safari/Tomatillo */
#define SCHIZO_PCICTRL_PCIRST	(1UL <<  8UL) /* Safari */
#define SCHIZO_PCICTRL_ARB_S	(0x3fUL << 0UL) /* Safari */
#define SCHIZO_PCICTRL_ARB_T	(0xffUL << 0UL) /* Tomatillo */

/* How the Tomatillo IRQs are routed around is pure guesswork here.
 *
 * All the Tomatillo devices I see in prtconf dumps seem to have only
 * a single PCI bus unit attached to it.  It would seem they are seperate
 * devices because their PortID (ie. JBUS ID) values are all different
 * and thus the registers are mapped to totally different locations.
 *
 * However, two Tomatillo's look "similar" in that the only difference
 * in their PortID is the lowest bit.
 *
 * So if we were to ignore this lower bit, it certainly looks like two
 * PCI bus units of the same Tomatillo.  I still have not really
 * figured this out...
 */
static void __init tomatillo_register_error_handlers(struct pci_controller_info *p)
{
	struct pci_pbm_info *pbm;
	unsigned long base = p->controller_regs;
	unsigned int irq, portid = p->portid;
	struct ino_bucket *bucket;
	u64 tmp, err_mask;
	int is_pbm_a;

	pbm = &p->pbm_B;
	is_pbm_a = 0;

	/* Build IRQs and register handlers. */
	irq = schizo_irq_build(pbm, NULL, (portid << 6) | SCHIZO_UE_INO);
	if (request_irq(irq, schizo_ue_intr,
			SA_SHIRQ, "TOMATILLO UE", p) < 0) {
		prom_printf("TOMATILLO%d: Cannot register UE interrupt.\n",
			    p->index);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = readl(bucket->imap);
	upa_writel(tmp, (base +
			 (is_pbm_a ?
			  SCHIZO_PBM_A_REGS_OFF :
			  SCHIZO_PBM_B_REGS_OFF) +
			 schizo_imap_offset(SCHIZO_UE_INO) + 4));

	irq = schizo_irq_build(pbm, NULL, (portid << 6) | SCHIZO_CE_INO);
	if (request_irq(irq, schizo_ce_intr,
			SA_SHIRQ, "TOMATILLO CE", p) < 0) {
		prom_printf("TOMATILLO%d: Cannot register CE interrupt.\n",
			    p->index);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = upa_readl(bucket->imap);
	upa_writel(tmp, (base +
			 (is_pbm_a ?
			  SCHIZO_PBM_A_REGS_OFF :
			  SCHIZO_PBM_B_REGS_OFF) +
			 schizo_imap_offset(SCHIZO_CE_INO) + 4));

	pbm = &p->pbm_A;
	is_pbm_a = 1;

	irq = schizo_irq_build(pbm, NULL, ((portid << 6) |
					   SCHIZO_PCIERR_A_INO));
	if (request_irq(irq, schizo_pcierr_intr,
			SA_SHIRQ, "TOMATILLO PCIERR", pbm) < 0) {
		prom_printf("%s: Cannot register PciERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = upa_readl(bucket->imap);
	upa_writel(tmp, (base +
			 (is_pbm_a ?
			  SCHIZO_PBM_A_REGS_OFF :
			  SCHIZO_PBM_B_REGS_OFF) +
			 schizo_imap_offset(SCHIZO_PCIERR_A_INO) + 4));

	pbm = &p->pbm_B;
	is_pbm_a = 0;

	irq = schizo_irq_build(pbm, NULL, ((portid << 6) |
					    SCHIZO_PCIERR_B_INO));
	if (request_irq(irq, schizo_pcierr_intr,
			SA_SHIRQ, "TOMATILLO PCIERR", pbm) < 0) {
		prom_printf("%s: Cannot register PciERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = upa_readl(bucket->imap);
	upa_writel(tmp, (base +
			 (is_pbm_a ?
			  SCHIZO_PBM_A_REGS_OFF :
			  SCHIZO_PBM_B_REGS_OFF) +
			 schizo_imap_offset(SCHIZO_PCIERR_B_INO) + 4));

	irq = schizo_irq_build(pbm, NULL, (portid << 6) | SCHIZO_SERR_INO);
	if (request_irq(irq, schizo_safarierr_intr,
			SA_SHIRQ, "TOMATILLO SERR", p) < 0) {
		prom_printf("%s: Cannot register SafariERR interrupt.\n",
			    pbm->name);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = upa_readl(bucket->imap);
	upa_writel(tmp, (base +
			 (is_pbm_a ?
			  SCHIZO_PBM_A_REGS_OFF :
			  SCHIZO_PBM_B_REGS_OFF) +
			 schizo_imap_offset(SCHIZO_SERR_INO) + 4));

	/* Enable UE and CE interrupts for controller. */
	schizo_write(base + SCHIZO_ECC_CTRL,
		     (SCHIZO_ECCCTRL_EE |
		      SCHIZO_ECCCTRL_UE |
		      SCHIZO_ECCCTRL_CE));

	/* Enable PCI Error interrupts and clear error
	 * bits.
	 */
	err_mask = (SCHIZO_PCICTRL_BUS_UNUS |
		    SCHIZO_PCICTRL_TTO_ERR |
		    SCHIZO_PCICTRL_RTRY_ERR |
		    SCHIZO_PCICTRL_DTO_ERR |
		    SCHIZO_PCICTRL_SERR |
		    SCHIZO_PCICTRL_EEN);

	tmp = schizo_read(base +
			  (is_pbm_a ?
			   SCHIZO_PCIA_CTRL :
			   SCHIZO_PCIB_CTRL));
	tmp |= err_mask;
	schizo_write(base +
		     (is_pbm_a ?
		      SCHIZO_PCIA_CTRL :
		      SCHIZO_PCIB_CTRL), tmp);

	schizo_write(base +
		     (is_pbm_a ?
		      SCHIZO_PBM_A_REGS_OFF :
		      SCHIZO_PBM_B_REGS_OFF) +
		     SCHIZO_PCI_AFSR,
		     (SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_PTA |
		      SCHIZO_PCIAFSR_PRTRY | SCHIZO_PCIAFSR_PPERR |
		      SCHIZO_PCIAFSR_PTTO |
		      SCHIZO_PCIAFSR_SMA | SCHIZO_PCIAFSR_STA |
		      SCHIZO_PCIAFSR_SRTRY | SCHIZO_PCIAFSR_SPERR |
		      SCHIZO_PCIAFSR_STTO));

	err_mask = (BUS_ERROR_BADCMD | BUS_ERROR_SNOOP_GR |
		    BUS_ERROR_SNOOP_PCI | BUS_ERROR_SNOOP_RD |
		    BUS_ERROR_SNOOP_RDS | BUS_ERROR_SNOOP_RDSA |
		    BUS_ERROR_SNOOP_OWN | BUS_ERROR_SNOOP_RDO |
		    BUS_ERROR_WDATA_PERR | BUS_ERROR_CTRL_PERR |
		    BUS_ERROR_SNOOP_ERR | BUS_ERROR_JBUS_ILL_B |
		    BUS_ERROR_JBUS_ILL_C | BUS_ERROR_RD_PERR |
		    BUS_ERROR_APERR | BUS_ERROR_UNMAP |
		    BUS_ERROR_BUSERR | BUS_ERROR_TIMEOUT);

	schizo_write(base + SCHIZO_SAFARI_ERRCTRL,
		     (SCHIZO_SAFERRCTRL_EN | err_mask));

	schizo_write(base + SCHIZO_SAFARI_IRQCTRL,
		     (SCHIZO_SAFIRQCTRL_EN | (BUS_ERROR_UNMAP)));
}

static void __init schizo_register_error_handlers(struct pci_controller_info *p)
{
	struct pci_pbm_info *pbm_a = &p->pbm_A;
	struct pci_pbm_info *pbm_b = &p->pbm_B;
	unsigned long base = p->controller_regs;
	unsigned int irq, portid = p->portid;
	struct ino_bucket *bucket;
	u64 tmp, err_mask;

	/* Build IRQs and register handlers. */
	irq = schizo_irq_build(pbm_b, NULL, (portid << 6) | SCHIZO_UE_INO);
	if (request_irq(irq, schizo_ue_intr,
			SA_SHIRQ, "SCHIZO UE", p) < 0) {
		prom_printf("SCHIZO%d: Cannot register UE interrupt.\n",
			    p->index);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = readl(bucket->imap);
	upa_writel(tmp, (base + SCHIZO_PBM_B_REGS_OFF + schizo_imap_offset(SCHIZO_UE_INO) + 4));

	irq = schizo_irq_build(pbm_b, NULL, (portid << 6) | SCHIZO_CE_INO);
	if (request_irq(irq, schizo_ce_intr,
			SA_SHIRQ, "SCHIZO CE", p) < 0) {
		prom_printf("SCHIZO%d: Cannot register CE interrupt.\n",
			    p->index);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = upa_readl(bucket->imap);
	upa_writel(tmp, (base + SCHIZO_PBM_B_REGS_OFF + schizo_imap_offset(SCHIZO_CE_INO) + 4));

	irq = schizo_irq_build(pbm_a, NULL, (portid << 6) | SCHIZO_PCIERR_A_INO);
	if (request_irq(irq, schizo_pcierr_intr,
			SA_SHIRQ, "SCHIZO PCIERR", pbm_a) < 0) {
		prom_printf("SCHIZO%d(PBMA): Cannot register PciERR interrupt.\n",
			    p->index);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = upa_readl(bucket->imap);
	upa_writel(tmp, (base + SCHIZO_PBM_A_REGS_OFF + schizo_imap_offset(SCHIZO_PCIERR_A_INO) + 4));

	irq = schizo_irq_build(pbm_b, NULL, (portid << 6) | SCHIZO_PCIERR_B_INO);
	if (request_irq(irq, schizo_pcierr_intr,
			SA_SHIRQ, "SCHIZO PCIERR", pbm_b) < 0) {
		prom_printf("SCHIZO%d(PBMB): Cannot register PciERR interrupt.\n",
			    p->index);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = upa_readl(bucket->imap);
	upa_writel(tmp, (base + SCHIZO_PBM_B_REGS_OFF + schizo_imap_offset(SCHIZO_PCIERR_B_INO) + 4));

	irq = schizo_irq_build(pbm_b, NULL, (portid << 6) | SCHIZO_SERR_INO);
	if (request_irq(irq, schizo_safarierr_intr,
			SA_SHIRQ, "SCHIZO SERR", p) < 0) {
		prom_printf("SCHIZO%d(PBMB): Cannot register SafariERR interrupt.\n",
			    p->index);
		prom_halt();
	}
	bucket = __bucket(irq);
	tmp = upa_readl(bucket->imap);
	upa_writel(tmp, (base + SCHIZO_PBM_B_REGS_OFF + schizo_imap_offset(SCHIZO_SERR_INO) + 4));

	/* Enable UE and CE interrupts for controller. */
	schizo_write(base + SCHIZO_ECC_CTRL,
		     (SCHIZO_ECCCTRL_EE |
		      SCHIZO_ECCCTRL_UE |
		      SCHIZO_ECCCTRL_CE));

	err_mask = (SCHIZO_PCICTRL_BUS_UNUS |
		    SCHIZO_PCICTRL_ESLCK |
		    SCHIZO_PCICTRL_TTO_ERR |
		    SCHIZO_PCICTRL_RTRY_ERR |
		    SCHIZO_PCICTRL_DTO_ERR |
		    SCHIZO_PCICTRL_SBH_ERR |
		    SCHIZO_PCICTRL_SERR |
		    SCHIZO_PCICTRL_SBH_INT |
		    SCHIZO_PCICTRL_EEN);

	/* Enable PCI Error interrupts and clear error
	 * bits for each PBM.
	 */
	tmp = schizo_read(base + SCHIZO_PCIA_CTRL);
	tmp |= err_mask;
	schizo_write(base + SCHIZO_PCIA_CTRL, tmp);

	schizo_write(base + SCHIZO_PBM_A_REGS_OFF + SCHIZO_PCI_AFSR,
		     (SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_PTA |
		      SCHIZO_PCIAFSR_PRTRY | SCHIZO_PCIAFSR_PPERR |
		      SCHIZO_PCIAFSR_PTTO | SCHIZO_PCIAFSR_PUNUS |
		      SCHIZO_PCIAFSR_SMA | SCHIZO_PCIAFSR_STA |
		      SCHIZO_PCIAFSR_SRTRY | SCHIZO_PCIAFSR_SPERR |
		      SCHIZO_PCIAFSR_STTO | SCHIZO_PCIAFSR_SUNUS));

	tmp = schizo_read(base + SCHIZO_PCIB_CTRL);
	tmp |= err_mask;
	schizo_write(base + SCHIZO_PCIB_CTRL, tmp);

	schizo_write(base + SCHIZO_PBM_B_REGS_OFF + SCHIZO_PCI_AFSR,
		     (SCHIZO_PCIAFSR_PMA | SCHIZO_PCIAFSR_PTA |
		      SCHIZO_PCIAFSR_PRTRY | SCHIZO_PCIAFSR_PPERR |
		      SCHIZO_PCIAFSR_PTTO | SCHIZO_PCIAFSR_PUNUS |
		      SCHIZO_PCIAFSR_SMA | SCHIZO_PCIAFSR_STA |
		      SCHIZO_PCIAFSR_SRTRY | SCHIZO_PCIAFSR_SPERR |
		      SCHIZO_PCIAFSR_STTO | SCHIZO_PCIAFSR_SUNUS));

	/* Make all Safari error conditions fatal except unmapped
	 * errors which we make generate interrupts.
	 */
	err_mask = (BUS_ERROR_BADCMD | BUS_ERROR_SSMDIS |
		    BUS_ERROR_BADMA | BUS_ERROR_BADMB |
		    BUS_ERROR_BADMC |
		    BUS_ERROR_CPU1PS | BUS_ERROR_CPU1PB |
		    BUS_ERROR_CPU0PS | BUS_ERROR_CPU0PB |
		    BUS_ERROR_CIQTO |
		    BUS_ERROR_LPQTO | BUS_ERROR_SFPQTO |
		    BUS_ERROR_UFPQTO | BUS_ERROR_APERR |
		    BUS_ERROR_BUSERR | BUS_ERROR_TIMEOUT |
		    BUS_ERROR_ILL);
#if 1
	/* XXX Something wrong with some Excalibur systems
	 * XXX Sun is shipping.  The behavior on a 2-cpu
	 * XXX machine is that both CPU1 parity error bits
	 * XXX are set and are immediately set again when
	 * XXX their error status bits are cleared.  Just
	 * XXX ignore them for now.  -DaveM
	 */
	err_mask &= ~(BUS_ERROR_CPU1PS | BUS_ERROR_CPU1PB |
		      BUS_ERROR_CPU0PS | BUS_ERROR_CPU0PB);
#endif

	schizo_write(base + SCHIZO_SAFARI_ERRCTRL,
		     (SCHIZO_SAFERRCTRL_EN | err_mask));

	schizo_write(base + SCHIZO_SAFARI_IRQCTRL,
		     (SCHIZO_SAFIRQCTRL_EN | (BUS_ERROR_UNMAP)));
}

static void __init pbm_config_busmastering(struct pci_pbm_info *pbm)
{
	u8 *addr;

	/* Set cache-line size to 64 bytes, this is actually
	 * a nop but I do it for completeness.
	 */
	addr = schizo_pci_config_mkaddr(pbm, pbm->pci_first_busno,
					0, PCI_CACHE_LINE_SIZE);
	pci_config_write8(addr, 64 / sizeof(u32));

	/* Set PBM latency timer to 64 PCI clocks. */
	addr = schizo_pci_config_mkaddr(pbm, pbm->pci_first_busno,
					0, PCI_LATENCY_TIMER);
	pci_config_write8(addr, 64);
}

static void __init pbm_scan_bus(struct pci_controller_info *p,
				struct pci_pbm_info *pbm)
{
	struct pcidev_cookie *cookie = kmalloc(sizeof(*cookie), GFP_KERNEL);

	if (!cookie) {
		prom_printf("%s: Critical allocation failure.\n", pbm->name);
		prom_halt();
	}

	/* All we care about is the PBM. */
	memset(cookie, 0, sizeof(*cookie));
	cookie->pbm = pbm;

	pbm->pci_bus = pci_scan_bus(pbm->pci_first_busno,
				    p->pci_ops,
				    pbm);
	pci_fixup_host_bridge_self(pbm->pci_bus);
	pbm->pci_bus->self->sysdata = cookie;

	pci_fill_in_pbm_cookies(pbm->pci_bus, pbm, pbm->prom_node);
	pci_record_assignments(pbm, pbm->pci_bus);
	pci_assign_unassigned(pbm, pbm->pci_bus);
	pci_fixup_irq(pbm, pbm->pci_bus);
	pci_determine_66mhz_disposition(pbm, pbm->pci_bus);
	pci_setup_busmastering(pbm, pbm->pci_bus);
}

static void __init __schizo_scan_bus(struct pci_controller_info *p,
				     int is_tomatillo)
{
	if (!p->pbm_B.prom_node || !p->pbm_A.prom_node) {
		printk("PCI: One PCI bus module of controller found.\n");
		printk("PCI: Ignoring entire controller.\n");
		return;
	}

	pbm_config_busmastering(&p->pbm_B);
	p->pbm_B.is_66mhz_capable =
		prom_getbool(p->pbm_B.prom_node, "66mhz-capable");
	pbm_config_busmastering(&p->pbm_A);
	p->pbm_A.is_66mhz_capable =
		prom_getbool(p->pbm_A.prom_node, "66mhz-capable");
	pbm_scan_bus(p, &p->pbm_B);
	pbm_scan_bus(p, &p->pbm_A);

	/* After the PCI bus scan is complete, we can register
	 * the error interrupt handlers.
	 */
	if (is_tomatillo)
		tomatillo_register_error_handlers(p);
	else
		schizo_register_error_handlers(p);
}

static void __init schizo_scan_bus(struct pci_controller_info *p)
{
	__schizo_scan_bus(p, 0);
}

static void __init tomatillo_scan_bus(struct pci_controller_info *p)
{
	__schizo_scan_bus(p, 1);
}

static void __init schizo_base_address_update(struct pci_dev *pdev, int resource)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = pcp->pbm;
	struct resource *res, *root;
	u32 reg;
	int where, size, is_64bit;

	res = &pdev->resource[resource];
	if (resource < 6) {
		where = PCI_BASE_ADDRESS_0 + (resource * 4);
	} else if (resource == PCI_ROM_RESOURCE) {
		where = pdev->rom_base_reg;
	} else {
		/* Somebody might have asked allocation of a non-standard resource */
		return;
	}

	is_64bit = 0;
	if (res->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	else {
		root = &pbm->mem_space;
		if ((res->flags & PCI_BASE_ADDRESS_MEM_TYPE_MASK)
		    == PCI_BASE_ADDRESS_MEM_TYPE_64)
			is_64bit = 1;
	}

	size = res->end - res->start;
	pci_read_config_dword(pdev, where, &reg);
	reg = ((reg & size) |
	       (((u32)(res->start - root->start)) & ~size));
	if (resource == PCI_ROM_RESOURCE) {
		reg |= PCI_ROM_ADDRESS_ENABLE;
		res->flags |= PCI_ROM_ADDRESS_ENABLE;
	}
	pci_write_config_dword(pdev, where, reg);

	/* This knows that the upper 32-bits of the address
	 * must be zero.  Our PCI common layer enforces this.
	 */
	if (is_64bit)
		pci_write_config_dword(pdev, where + 4, 0);
}

static void __init schizo_resource_adjust(struct pci_dev *pdev,
					  struct resource *res,
					  struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

/* Interrogate Safari match/mask registers to figure out where
 * PCI MEM, I/O, and Config space are for this PCI bus module.
 */

#define SCHIZO_PCI_A_MEM_MATCH		0x00040UL
#define SCHIZO_PCI_A_MEM_MASK		0x00048UL
#define SCHIZO_PCI_A_IO_MATCH		0x00050UL
#define SCHIZO_PCI_A_IO_MASK		0x00058UL
#define SCHIZO_PCI_B_MEM_MATCH		0x00060UL
#define SCHIZO_PCI_B_MEM_MASK		0x00068UL
#define SCHIZO_PCI_B_IO_MATCH		0x00070UL
#define SCHIZO_PCI_B_IO_MASK		0x00078UL

static void schizo_determine_mem_io_space(struct pci_pbm_info *pbm,
					  int is_pbm_a, unsigned long reg_base,
					  int is_tomatillo)
{
	u64 mem_match;
	u64 io_match;
	u64 a;

	if (is_pbm_a) {
		mem_match = reg_base + SCHIZO_PCI_A_MEM_MATCH;
		io_match = reg_base + SCHIZO_PCI_A_IO_MATCH;
	} else {
		mem_match = reg_base + SCHIZO_PCI_B_MEM_MATCH;
		io_match = reg_base + SCHIZO_PCI_B_IO_MATCH;
	}

	a = schizo_read(mem_match) & ~0x8000000000000000UL;

	if (is_tomatillo) {
		a += ((0x3UL << 41UL) |
		      ((u64)pbm->parent->portid << 36UL));
	}

	/* It should be 2GB in size but the decode is set for the full
	 * 4GB so we have to add the 2G by hand.
	 */
	pbm->mem_space.start = a;
	pbm->mem_space.end = a + 0x80000000;
	pbm->mem_space.flags = IORESOURCE_MEM;

	/* This 32MB area is divided into two pieces.  The first
	 * 16MB is Config space, the next 16MB is I/O space.
	 */

	a = schizo_read(io_match) & ~0x8000000000000000UL;

	if (is_tomatillo) {
		a += ((0x3UL << 41UL) |
		      ((u64)pbm->parent->portid << 36UL));
	}

	pbm->config_space = a;
	printk("%s: Local PCI config space at %016lx\n",
	       pbm->name, pbm->config_space);

	a += (16UL * 1024UL * 1024UL);
	pbm->io_space.start = a;
	pbm->io_space.end = a + ((16UL * 1024UL * 1024UL) - 1UL);
	pbm->io_space.flags = IORESOURCE_IO;
}

static void __init pbm_register_toplevel_resources(struct pci_controller_info *p,
						   struct pci_pbm_info *pbm,
						   int is_tomatillo)
{
	pbm->io_space.name = pbm->mem_space.name = pbm->name;

	request_resource(&ioport_resource, &pbm->io_space);
	request_resource(&iomem_resource, &pbm->mem_space);
	pci_register_legacy_regions(&pbm->io_space,
				    &pbm->mem_space);
}

#define SCHIZO_STRBUF_CONTROL_A		(SCHIZO_PBM_A_REGS_OFF + 0x02800UL)
#define SCHIZO_STRBUF_FLUSH_A		(SCHIZO_PBM_A_REGS_OFF + 0x02808UL)
#define SCHIZO_STRBUF_FSYNC_A		(SCHIZO_PBM_A_REGS_OFF + 0x02810UL)
#define SCHIZO_STRBUF_CTXFLUSH_A	(SCHIZO_PBM_A_REGS_OFF + 0x02818UL)
#define SCHIZO_STRBUF_CTXMATCH_A	(SCHIZO_PBM_A_REGS_OFF + 0x10000UL)

#define SCHIZO_STRBUF_CONTROL_B		(SCHIZO_PBM_B_REGS_OFF + 0x02800UL)
#define SCHIZO_STRBUF_FLUSH_B		(SCHIZO_PBM_B_REGS_OFF + 0x02808UL)
#define SCHIZO_STRBUF_FSYNC_B		(SCHIZO_PBM_B_REGS_OFF + 0x02810UL)
#define SCHIZO_STRBUF_CTXFLUSH_B	(SCHIZO_PBM_B_REGS_OFF + 0x02818UL)
#define SCHIZO_STRBUF_CTXMATCH_B	(SCHIZO_PBM_B_REGS_OFF + 0x10000UL)

static void schizo_pbm_strbuf_init(struct pci_controller_info *p,
				   struct pci_pbm_info *pbm,
				   int is_pbm_a,
				   int is_tomatillo)
{
	unsigned long base = p->controller_regs;
	u64 control;

	if (is_tomatillo) {
		/* TOMATILLO lacks streaming cache.  */
		return;
	}

	/* SCHIZO has context flushing. */
	if (is_pbm_a) {
		pbm->stc.strbuf_control		= base + SCHIZO_STRBUF_CONTROL_A;
		pbm->stc.strbuf_pflush		= base + SCHIZO_STRBUF_FLUSH_A;
		pbm->stc.strbuf_fsync		= base + SCHIZO_STRBUF_FSYNC_A;
		pbm->stc.strbuf_ctxflush	= base + SCHIZO_STRBUF_CTXFLUSH_A;
		pbm->stc.strbuf_ctxmatch_base	= base + SCHIZO_STRBUF_CTXMATCH_A;
	} else {
		pbm->stc.strbuf_control		= base + SCHIZO_STRBUF_CONTROL_B;
		pbm->stc.strbuf_pflush		= base + SCHIZO_STRBUF_FLUSH_B;
		pbm->stc.strbuf_fsync		= base + SCHIZO_STRBUF_FSYNC_B;
		pbm->stc.strbuf_ctxflush	= base + SCHIZO_STRBUF_CTXFLUSH_B;
		pbm->stc.strbuf_ctxmatch_base	= base + SCHIZO_STRBUF_CTXMATCH_B;
	}

	pbm->stc.strbuf_flushflag = (volatile unsigned long *)
		((((unsigned long)&pbm->stc.__flushflag_buf[0])
		  + 63UL)
		 & ~63UL);
	pbm->stc.strbuf_flushflag_pa = (unsigned long)
		__pa(pbm->stc.strbuf_flushflag);

	/* Turn off LRU locking and diag mode, enable the
	 * streaming buffer and leave the rerun-disable
	 * setting however OBP set it.
	 */
	control = schizo_read(pbm->stc.strbuf_control);
	control &= ~(SCHIZO_STRBUF_CTRL_LPTR |
		     SCHIZO_STRBUF_CTRL_LENAB |
		     SCHIZO_STRBUF_CTRL_DENAB);
	control |= SCHIZO_STRBUF_CTRL_ENAB;
	schizo_write(pbm->stc.strbuf_control, control);

	pbm->stc.strbuf_enabled = 1;
}

#define SCHIZO_IOMMU_CONTROL_A		(SCHIZO_PBM_A_REGS_OFF + 0x00200UL)
#define SCHIZO_IOMMU_TSBBASE_A		(SCHIZO_PBM_A_REGS_OFF + 0x00208UL)
#define SCHIZO_IOMMU_FLUSH_A		(SCHIZO_PBM_A_REGS_OFF + 0x00210UL)
#define SCHIZO_IOMMU_CTXFLUSH_A		(SCHIZO_PBM_A_REGS_OFF + 0x00218UL)
#define SCHIZO_IOMMU_TAG_A		(SCHIZO_PBM_A_REGS_OFF + 0x0a580UL)
#define SCHIZO_IOMMU_DATA_A		(SCHIZO_PBM_A_REGS_OFF + 0x0a600UL)
#define SCHIZO_IOMMU_CONTROL_B		(SCHIZO_PBM_B_REGS_OFF + 0x00200UL)
#define SCHIZO_IOMMU_TSBBASE_B		(SCHIZO_PBM_B_REGS_OFF + 0x00208UL)
#define SCHIZO_IOMMU_FLUSH_B		(SCHIZO_PBM_B_REGS_OFF + 0x00210UL)
#define SCHIZO_IOMMU_CTXFLUSH_B		(SCHIZO_PBM_B_REGS_OFF + 0x00218UL)
#define SCHIZO_IOMMU_TAG_B		(SCHIZO_PBM_B_REGS_OFF + 0x0a580UL)
#define SCHIZO_IOMMU_DATA_B		(SCHIZO_PBM_B_REGS_OFF + 0x0a600UL)

static void schizo_pbm_iommu_init(struct pci_controller_info *p,
				  struct pci_pbm_info *pbm,
				  int is_pbm_a,
				  int is_tomatillo)
{
	struct pci_iommu *iommu = pbm->iommu;
	unsigned long tsbbase, i, tagbase, database;
	u64 control;

	/* Setup initial software IOMMU state. */
	spin_lock_init(&iommu->lock);
	iommu->iommu_cur_ctx = 0;

	/* Register addresses, SCHIZO has iommu ctx flushing. */
	if (is_pbm_a) {
		iommu->iommu_control  = p->controller_regs + SCHIZO_IOMMU_CONTROL_A;
		iommu->iommu_tsbbase  = p->controller_regs + SCHIZO_IOMMU_TSBBASE_A;
		iommu->iommu_flush    = p->controller_regs + SCHIZO_IOMMU_FLUSH_A;
		iommu->iommu_ctxflush = p->controller_regs + SCHIZO_IOMMU_CTXFLUSH_A;
	} else {
		iommu->iommu_control  = p->controller_regs + SCHIZO_IOMMU_CONTROL_B;
		iommu->iommu_tsbbase  = p->controller_regs + SCHIZO_IOMMU_TSBBASE_B;
		iommu->iommu_flush    = p->controller_regs + SCHIZO_IOMMU_FLUSH_B;
		iommu->iommu_ctxflush = p->controller_regs + SCHIZO_IOMMU_CTXFLUSH_B;
	}

	/* We use the main control/status register of SCHIZO as the write
	 * completion register.
	 */
	iommu->write_complete_reg = p->controller_regs + 0x10000UL;

	/*
	 * Invalidate TLB Entries.
	 */
	control = schizo_read(iommu->iommu_control);
	control |= SCHIZO_IOMMU_CTRL_DENAB;
	schizo_write(iommu->iommu_control, control);

	if (is_pbm_a)
		tagbase = SCHIZO_IOMMU_TAG_A, database = SCHIZO_IOMMU_DATA_A;
	else
		tagbase = SCHIZO_IOMMU_TAG_B, database = SCHIZO_IOMMU_DATA_B;
	for(i = 0; i < 16; i++) {
		schizo_write(p->controller_regs + tagbase + (i * 8UL), 0);
		schizo_write(p->controller_regs + database + (i * 8UL), 0);
	}

	/* Leave diag mode enabled for full-flushing done
	 * in pci_iommu.c
	 */

	/* Using assumed page size 8K with 128K entries we need 1MB iommu page
	 * table (128K ioptes * 8 bytes per iopte).  This is
	 * page order 7 on UltraSparc.
	 */
	tsbbase = __get_free_pages(GFP_KERNEL, get_order(IO_TSB_SIZE));
	if (!tsbbase) {
		prom_printf("%s: Error, gfp(tsb) failed.\n", pbm->name);
		prom_halt();
	}
	iommu->page_table = (iopte_t *)tsbbase;
	iommu->page_table_sz_bits = 17;
	iommu->page_table_map_base = 0xc0000000;
	iommu->dma_addr_mask = 0xffffffff;
	memset((char *)tsbbase, 0, IO_TSB_SIZE);

	/* We start with no consistent mappings. */
	iommu->lowest_consistent_map =
		1 << (iommu->page_table_sz_bits - PBM_LOGCLUSTERS);

	for (i = 0; i < PBM_NCLUSTERS; i++) {
		iommu->alloc_info[i].flush = 0;
		iommu->alloc_info[i].next = 0;
	}

	schizo_write(iommu->iommu_tsbbase, __pa(tsbbase));

	control = schizo_read(iommu->iommu_control);
	control &= ~(SCHIZO_IOMMU_CTRL_TSBSZ | SCHIZO_IOMMU_CTRL_TBWSZ);
	control |= (SCHIZO_IOMMU_TSBSZ_128K | SCHIZO_IOMMU_CTRL_ENAB);
	schizo_write(iommu->iommu_control, control);
}

#define SCHIZO_PCIA_IRQ_RETRY	(SCHIZO_PBM_A_REGS_OFF + 0x1a00UL)
#define SCHIZO_PCIB_IRQ_RETRY	(SCHIZO_PBM_B_REGS_OFF + 0x1a00UL)
#define  SCHIZO_IRQ_RETRY_INF	 0xffUL

#define SCHIZO_PCIA_DIAG	(SCHIZO_PBM_A_REGS_OFF + 0x2020UL)
#define SCHIZO_PCIB_DIAG	(SCHIZO_PBM_B_REGS_OFF + 0x2020UL)
#define  SCHIZO_PCIDIAG_D_BADECC	(1UL << 10UL) /* Disable BAD ECC errors (Schizo) */
#define  SCHIZO_PCIDIAG_D_BYPASS	(1UL <<  9UL) /* Disable MMU bypass mode (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_D_TTO		(1UL <<  8UL) /* Disable TTO errors (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_D_RTRYARB	(1UL <<  7UL) /* Disable retry arbitration (Schizo) */
#define  SCHIZO_PCIDIAG_D_RETRY		(1UL <<  6UL) /* Disable retry limit (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_D_INTSYNC	(1UL <<  5UL) /* Disable interrupt/DMA synch (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_I_DMA_PARITY	(1UL <<  3UL) /* Invert DMA parity (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_I_PIOD_PARITY	(1UL <<  2UL) /* Invert PIO data parity (Schizo/Tomatillo) */
#define  SCHIZO_PCIDIAG_I_PIOA_PARITY	(1UL <<  1UL) /* Invert PIO address parity (Schizo/Tomatillo) */

static void __init schizo_pbm_hw_init(struct pci_controller_info *p,
				      int is_pbm_a, int is_tomatillo)
{
	u64 tmp;

	/* Set IRQ retry to infinity. */
	schizo_write(p->controller_regs +
		     (is_pbm_a ?
		      SCHIZO_PCIA_IRQ_RETRY :
		      SCHIZO_PCIB_IRQ_RETRY),
		     SCHIZO_IRQ_RETRY_INF);

	/* Enable arbiter for all PCI slots.  Also, disable PCI interval
	 * timer so that DTO (Discard TimeOuts) are not reported because
	 * some Schizo revisions report them erroneously.
	 */
	tmp = schizo_read(p->controller_regs +
			  (is_pbm_a ?
			   SCHIZO_PCIA_CTRL :
			   SCHIZO_PCIB_CTRL));
	if (is_tomatillo)
		tmp |= SCHIZO_PCICTRL_ARB_T;
	else
		tmp |= SCHIZO_PCICTRL_ARB_S;
	if (is_tomatillo) {
#if 0
		/* This is the recommended setting, but we'll just
		 * leave the value alone for now.
		 */
		tmp &= ~SCHIZO_PCICTRL_PTO;
		tmp |= 0x1UL << SCHIZO_PCICTRL_PTO_SHIFT;
#endif
		/* XXX Prefetch settings? */
	} else {
		tmp &= ~SCHIZO_PCICTRL_PTO;
	}
	schizo_write(p->controller_regs +
		     (is_pbm_a ?
		      SCHIZO_PCIA_CTRL :
		      SCHIZO_PCIB_CTRL), tmp);

	/* Leave this at OBP initialized value on Tomatillo.  */
	if (!is_tomatillo) {
		/* Disable TTO error reporting (won't happen anyway since we
		 * disabled the PCI interval timer above) and retry arbitration
		 * (can cause hangs in some Schizo revisions).
		 */
		tmp = schizo_read(p->controller_regs +
				  (is_pbm_a ?
				   SCHIZO_PCIA_DIAG :
				   SCHIZO_PCIB_DIAG));
		tmp |= (SCHIZO_PCIDIAG_D_TTO | SCHIZO_PCIDIAG_D_RTRYARB);
		schizo_write(p->controller_regs +
			     (is_pbm_a ?
			      SCHIZO_PCIA_DIAG :
			      SCHIZO_PCIB_DIAG), tmp);
	}
}

static void __init schizo_pbm_init(struct pci_controller_info *p,
				   int prom_node, int is_pbm_a,
				   int is_tomatillo)
{
	unsigned int busrange[2];
	struct pci_pbm_info *pbm;
	int err;

	schizo_pbm_hw_init(p, is_pbm_a, is_tomatillo);

	if (is_pbm_a)
		pbm = &p->pbm_A;
	else
		pbm = &p->pbm_B;

	pbm->parent = p;
	pbm->prom_node = prom_node;
	pbm->pci_first_slot = 1;

	sprintf(pbm->name,
		(is_tomatillo ?
		 "TOMATILLO%d PBM%c" :
		 "SCHIZO%d PBM%c"),
		p->index,
		(pbm == &p->pbm_A ? 'A' : 'B'));

	schizo_determine_mem_io_space(pbm, is_pbm_a,
				      p->controller_regs, is_tomatillo);
	pbm_register_toplevel_resources(p, pbm, is_tomatillo);

	prom_getstring(prom_node, "name",
		       pbm->prom_name,
		       sizeof(pbm->prom_name));

	err = prom_getproperty(prom_node, "ranges",
			       (char *) pbm->pbm_ranges,
			       sizeof(pbm->pbm_ranges));
	if (err != -1)
		pbm->num_pbm_ranges =
			(err / sizeof(struct linux_prom_pci_ranges));
	else
		pbm->num_pbm_ranges = 0;

	err = prom_getproperty(prom_node, "interrupt-map",
			       (char *)pbm->pbm_intmap,
			       sizeof(pbm->pbm_intmap));
	if (err != -1) {
		pbm->num_pbm_intmap = (err / sizeof(struct linux_prom_pci_intmap));
		err = prom_getproperty(prom_node, "interrupt-map-mask",
				       (char *)&pbm->pbm_intmask,
				       sizeof(pbm->pbm_intmask));
		if (err == -1) {
			prom_printf("%s: Fatal error, no "
				    "interrupt-map-mask.\n", pbm->name);
			prom_halt();
		}
	} else {
		pbm->num_pbm_intmap = 0;
		memset(&pbm->pbm_intmask, 0, sizeof(pbm->pbm_intmask));
	}

	err = prom_getproperty(prom_node, "bus-range",
			       (char *)&busrange[0],
			       sizeof(busrange));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no bus-range.\n", pbm->name);
		prom_halt();
	}
	pbm->pci_first_busno = busrange[0];
	pbm->pci_last_busno = busrange[1];

	schizo_pbm_iommu_init(p, pbm, is_pbm_a, is_tomatillo);
	schizo_pbm_strbuf_init(p, pbm, is_pbm_a, is_tomatillo);
}

static inline int portid_compare(u32 x, u32 y, int is_tomatillo)
{
	if (is_tomatillo) {
		if (x == (y ^ 1))
			return 1;
		return 0;
	}
	return (x == y);
}

static void __init __schizo_init(int node, char *model_name, int is_tomatillo)
{
	struct linux_prom64_registers pr_regs[4];
	struct pci_controller_info *p;
	struct pci_iommu *iommu;
	unsigned long flags;
	u32 portid;
	int is_pbm_a, err;
	const char *chipset_name;

	if (is_tomatillo)
		chipset_name = "TOMATILLO";
	else
		chipset_name = "SCHIZO";

	portid = prom_getintdefault(node, "portid", 0xff);

	spin_lock_irqsave(&pci_controller_lock, flags);
	for(p = pci_controller_root; p; p = p->next) {
		if (portid_compare(p->portid, portid, is_tomatillo)) {
			spin_unlock_irqrestore(&pci_controller_lock, flags);
			is_pbm_a = (p->pbm_A.prom_node == 0);
			schizo_pbm_init(p, node, is_pbm_a, is_tomatillo);
			return;
		}
	}
	spin_unlock_irqrestore(&pci_controller_lock, flags);

	p = kmalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p) {
		prom_printf("%s: Fatal memory allocation error.\n",
			    chipset_name);
		prom_halt();
	}
	memset(p, 0, sizeof(*p));

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("%s: Fatal memory allocation error.\n",
			    chipset_name);
		prom_halt();
	}
	memset(iommu, 0, sizeof(*iommu));
	p->pbm_A.iommu = iommu;

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("%s: Fatal memory allocation error.\n",
			    chipset_name);
		prom_halt();
	}
	memset(iommu, 0, sizeof(*iommu));
	p->pbm_B.iommu = iommu;

	spin_lock_irqsave(&pci_controller_lock, flags);
	p->next = pci_controller_root;
	pci_controller_root = p;
	spin_unlock_irqrestore(&pci_controller_lock, flags);

	p->portid = portid;
	p->index = pci_num_controllers++;
	p->pbms_same_domain = 0;
	p->scan_bus = (is_tomatillo ? tomatillo_scan_bus : schizo_scan_bus);
	p->irq_build = schizo_irq_build;
	p->base_address_update = schizo_base_address_update;
	p->resource_adjust = schizo_resource_adjust;
	p->pci_ops = &schizo_ops;

	/* For SCHIZO, three OBP regs:
	 * 1) PBM controller regs
	 * 2) Schizo front-end controller regs (same for both PBMs)
	 * 3) PBM PCI config space
	 *
	 * For TOMATILLO, four OBP regs:
	 * 1) PBM controller regs
	 * 2) Tomatillo front-end controller regs (same for both PBMs)
	 * 3) PBM PCI config space
	 * 4) Ichip regs
	 */
	err = prom_getproperty(node, "reg",
			       (char *)&pr_regs[0],
			       sizeof(pr_regs));
	if (err == 0 || err == -1) {
		prom_printf("%s: Fatal error, no reg property.\n",
			    chipset_name);
		prom_halt();
	}

	p->controller_regs = pr_regs[1].phys_addr - 0x10000UL;
	printk("PCI: Found %s, Port ID %x, control regs at %016lx\n",
	       chipset_name,
	       p->portid,
	       p->controller_regs);

	/* Like PSYCHO we have a 2GB aligned area for memory space. */
	pci_memspace_mask = 0x7fffffffUL;

	is_pbm_a = ((pr_regs[0].phys_addr & 0x00700000) == 0x00600000);
	schizo_pbm_init(p, node, is_pbm_a, is_tomatillo);
}

void __init schizo_init(int node, char *model_name)
{
	__schizo_init(node, model_name, 0);
}

void __init tomatillo_init(int node, char *model_name)
{
	__schizo_init(node, model_name, 1);
}
