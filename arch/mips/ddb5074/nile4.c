/*
 *  arch/mips/ddb5074/nile4.c -- NEC Vrc-5074 Nile 4 support routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/nile4.h>


/*
 *  Physical Device Address Registers
 *
 *  Note: 32 bit addressing only!
 */
void nile4_set_pdar(u32 pdar, u32 phys, u32 size, int width,
		    int on_memory_bus, int visible)
{
	u32 maskbits;
	u32 widthbits;

	if (pdar > NILE4_BOOTCS || (pdar & 7)) {
		printk("nile4_set_pdar: invalid pdar %d\n", pdar);
		return;
	}
	if (pdar == NILE4_INTCS && size != 0x00200000) {
		printk("nile4_set_pdar: INTCS size must be 2 MB\n");
		return;
	}
	switch (size) {
#if 0				/* We don't support 4 GB yet */
	case 0x100000000:	/* 4 GB */
		maskbits = 4;
		break;
#endif
	case 0x80000000:	/* 2 GB */
		maskbits = 5;
		break;
	case 0x40000000:	/* 1 GB */
		maskbits = 6;
		break;
	case 0x20000000:	/* 512 MB */
		maskbits = 7;
		break;
	case 0x10000000:	/* 256 MB */
		maskbits = 8;
		break;
	case 0x08000000:	/* 128 MB */
		maskbits = 9;
		break;
	case 0x04000000:	/* 64 MB */
		maskbits = 10;
		break;
	case 0x02000000:	/* 32 MB */
		maskbits = 11;
		break;
	case 0x01000000:	/* 16 MB */
		maskbits = 12;
		break;
	case 0x00800000:	/* 8 MB */
		maskbits = 13;
		break;
	case 0x00400000:	/* 4 MB */
		maskbits = 14;
		break;
	case 0x00200000:	/* 2 MB */
		maskbits = 15;
		break;
	case 0:		/* OFF */
		maskbits = 0;
		break;
	default:
		printk("nile4_set_pdar: unsupported size %p\n", (void *) size);
		return;
	}
	switch (width) {
	case 8:
		widthbits = 0;
		break;
	case 16:
		widthbits = 1;
		break;
	case 32:
		widthbits = 2;
		break;
	case 64:
		widthbits = 3;
		break;
	default:
		printk("nile4_set_pdar: unsupported width %d\n", width);
		return;
	}
	nile4_out32(pdar, maskbits | (on_memory_bus ? 0x10 : 0) |
		    (visible ? 0x20 : 0) | (widthbits << 6) |
		    (phys & 0xffe00000));
	nile4_out32(pdar + 4, 0);
	/*
	 * When programming a PDAR, the register should be read immediately
	 * after writing it. This ensures that address decoders are properly
	 * configured.
	 */
	nile4_in32(pdar);
	nile4_in32(pdar + 4);
}


/*
 *  PCI Master Registers
 *
 *  Note: 32 bit addressing only!
 */
void nile4_set_pmr(u32 pmr, u32 type, u32 addr)
{
	if (pmr != NILE4_PCIINIT0 && pmr != NILE4_PCIINIT1) {
		printk("nile4_set_pmr: invalid pmr %d\n", pmr);
		return;
	}
	switch (type) {
	case NILE4_PCICMD_IACK:	/* PCI Interrupt Acknowledge */
	case NILE4_PCICMD_IO:	/* PCI I/O Space */
	case NILE4_PCICMD_MEM:	/* PCI Memory Space */
	case NILE4_PCICMD_CFG:	/* PCI Configuration Space */
		break;
	default:
		printk("nile4_set_pmr: invalid type %d\n", type);
		return;
	}
	nile4_out32(pmr, (type << 1) | 0x10 | (addr & 0xffe00000));
	nile4_out32(pmr + 4, 0);
}


/*
 *  Interrupt Programming
 */
void nile4_map_irq(int nile4_irq, int cpu_irq)
{
	u32 offset, t;

	offset = NILE4_INTCTRL;
	if (nile4_irq >= 8) {
		offset += 4;
		nile4_irq -= 8;
	}
	t = nile4_in32(offset);
	t &= ~(7 << (nile4_irq * 4));
	t |= cpu_irq << (nile4_irq * 4);
	nile4_out32(offset, t);
}

void nile4_map_irq_all(int cpu_irq)
{
	u32 all, t;

	all = cpu_irq;
	all |= all << 4;
	all |= all << 8;
	all |= all << 16;
	t = nile4_in32(NILE4_INTCTRL);
	t &= 0x88888888;
	t |= all;
	nile4_out32(NILE4_INTCTRL, t);
	t = nile4_in32(NILE4_INTCTRL + 4);
	t &= 0x88888888;
	t |= all;
	nile4_out32(NILE4_INTCTRL + 4, t);
}

void nile4_enable_irq(int nile4_irq)
{
	u32 offset, t;

	offset = NILE4_INTCTRL;
	if (nile4_irq >= 8) {
		offset += 4;
		nile4_irq -= 8;
	}
	t = nile4_in32(offset);
	t |= 8 << (nile4_irq * 4);
	nile4_out32(offset, t);
}

void nile4_disable_irq(int nile4_irq)
{
	u32 offset, t;

	offset = NILE4_INTCTRL;
	if (nile4_irq >= 8) {
		offset += 4;
		nile4_irq -= 8;
	}
	t = nile4_in32(offset);
	t &= ~(8 << (nile4_irq * 4));
	nile4_out32(offset, t);
}

void nile4_disable_irq_all(void)
{
	nile4_out32(NILE4_INTCTRL, 0);
	nile4_out32(NILE4_INTCTRL + 4, 0);
}

u16 nile4_get_irq_stat(int cpu_irq)
{
	return nile4_in16(NILE4_INTSTAT0 + cpu_irq * 2);
}

void nile4_enable_irq_output(int cpu_irq)
{
	u32 t;

	t = nile4_in32(NILE4_INTSTAT1 + 4);
	t |= 1 << (16 + cpu_irq);
	nile4_out32(NILE4_INTSTAT1, t);
}

void nile4_disable_irq_output(int cpu_irq)
{
	u32 t;

	t = nile4_in32(NILE4_INTSTAT1 + 4);
	t &= ~(1 << (16 + cpu_irq));
	nile4_out32(NILE4_INTSTAT1, t);
}

void nile4_set_pci_irq_polarity(int pci_irq, int high)
{
	u32 t;

	t = nile4_in32(NILE4_INTPPES);
	if (high)
		t &= ~(1 << (pci_irq * 2));
	else
		t |= 1 << (pci_irq * 2);
	nile4_out32(NILE4_INTPPES, t);
}

void nile4_set_pci_irq_level_or_edge(int pci_irq, int level)
{
	u32 t;

	t = nile4_in32(NILE4_INTPPES);
	if (level)
		t |= 2 << (pci_irq * 2);
	else
		t &= ~(2 << (pci_irq * 2));
	nile4_out32(NILE4_INTPPES, t);
}

void nile4_clear_irq(int nile4_irq)
{
	nile4_out32(NILE4_INTCLR, 1 << nile4_irq);
}

void nile4_clear_irq_mask(u32 mask)
{
	nile4_out32(NILE4_INTCLR, mask);
}

u8 nile4_i8259_iack(void)
{
	u8 irq;

	/* Set window 0 for interrupt acknowledge */
	nile4_set_pmr(NILE4_PCIINIT0, NILE4_PCICMD_IACK, 0);
	irq = *(volatile u8 *) NILE4_PCI_IACK_BASE;
	/* Set window 0 for PCI I/O space */
	nile4_set_pmr(NILE4_PCIINIT0, NILE4_PCICMD_IO, 0);
	return irq;
}

#if 0
void nile4_dump_irq_status(void)
{
	printk("CPUSTAT = %p:%p\n", (void *) nile4_in32(NILE4_CPUSTAT + 4),
	       (void *) nile4_in32(NILE4_CPUSTAT));
	printk("INTCTRL = %p:%p\n", (void *) nile4_in32(NILE4_INTCTRL + 4),
	       (void *) nile4_in32(NILE4_INTCTRL));
	printk("INTSTAT0 = %p:%p\n",
	       (void *) nile4_in32(NILE4_INTSTAT0 + 4),
	       (void *) nile4_in32(NILE4_INTSTAT0));
	printk("INTSTAT1 = %p:%p\n",
	       (void *) nile4_in32(NILE4_INTSTAT1 + 4),
	       (void *) nile4_in32(NILE4_INTSTAT1));
	printk("INTCLR = %p:%p\n", (void *) nile4_in32(NILE4_INTCLR + 4),
	       (void *) nile4_in32(NILE4_INTCLR));
	printk("INTPPES = %p:%p\n", (void *) nile4_in32(NILE4_INTPPES + 4),
	       (void *) nile4_in32(NILE4_INTPPES));
}
#endif
