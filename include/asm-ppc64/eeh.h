/* 
 * eeh.h
 * Copyright (C) 2001  Dave Engebretsen & Todd Inglett IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _PPC64_EEH_H
#define _PPC64_EEH_H

#include <linux/string.h>
#include <linux/init.h>

struct pci_dev;

/* I/O addresses are converted to EEH "tokens" such that a driver will cause
 * a bad page fault if the address is used directly (i.e. these addresses are
 * never actually mapped.  Translation between IO <-> EEH region is 1 to 1.
 */
#define IO_TOKEN_TO_ADDR(token) \
	(((unsigned long)(token) & ~(0xfUL << REGION_SHIFT)) | \
	(IO_REGION_ID << REGION_SHIFT))

#define IO_ADDR_TO_TOKEN(addr) \
	(((unsigned long)(addr) & ~(0xfUL << REGION_SHIFT)) | \
	(EEH_REGION_ID << REGION_SHIFT))

/* Values for eeh_mode bits in device_node */
#define EEH_MODE_SUPPORTED	(1<<0)
#define EEH_MODE_NOCHECK	(1<<1)

extern void __init eeh_init(void);
unsigned long eeh_check_failure(void *token, unsigned long val);
void *eeh_ioremap(unsigned long addr, void *vaddr);
void __init pci_addr_cache_build(void);

/**
 * eeh_add_device - perform EEH initialization for the indicated pci device
 * @dev: pci device for which to set up EEH
 *
 * This routine can be used to perform EEH initialization for PCI
 * devices that were added after system boot (e.g. hotplug, dlpar).
 * Whether this actually enables EEH or not for this device depends
 * on the type of the device, on earlier boot command-line
 * arguments & etc.
 */
void eeh_add_device(struct pci_dev *);

/**
 * eeh_remove_device - undo EEH setup for the indicated pci device
 * @dev: pci device to be removed
 *
 * This routine should be when a device is removed from a running
 * system (e.g. by hotplug or dlpar).
 */
void eeh_remove_device(struct pci_dev *);

#define EEH_DISABLE		0
#define EEH_ENABLE		1
#define EEH_RELEASE_LOADSTORE	2
#define EEH_RELEASE_DMA		3
int eeh_set_option(struct pci_dev *dev, int options);

/*
 * EEH_POSSIBLE_ERROR() -- test for possible MMIO failure.
 *
 * Order this macro for performance.
 * If EEH is off for a device and it is a memory BAR, ioremap will
 * map it to the IOREGION.  In this case addr == vaddr and since these
 * should be in registers we compare them first.  Next we check for
 * ff's which indicates a (very) possible failure.
 *
 * If this macro yields TRUE, the caller relays to eeh_check_failure()
 * which does further tests out of line.
 */
#define EEH_POSSIBLE_IO_ERROR(val, type)	((val) == (type)~0)

/* The vaddr will equal the addr if EEH checking is disabled for
 * this device.  This is because eeh_ioremap() will not have
 * remapped to 0xA0, and thus both vaddr and addr will be 0xE0...
 */
#define EEH_POSSIBLE_ERROR(addr, vaddr, val, type) \
		((vaddr) != (addr) && EEH_POSSIBLE_IO_ERROR(val, type))

/* 
 * MMIO read/write operations with EEH support.
 */
static inline u8 eeh_readb(void *addr) {
	volatile u8 *vaddr = (volatile u8 *)IO_TOKEN_TO_ADDR(addr);
	u8 val = in_8(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val, u8))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writeb(u8 val, void *addr) {
	volatile u8 *vaddr = (volatile u8 *)IO_TOKEN_TO_ADDR(addr);
	out_8(vaddr, val);
}

static inline u16 eeh_readw(void *addr) {
	volatile u16 *vaddr = (volatile u16 *)IO_TOKEN_TO_ADDR(addr);
	u16 val = in_le16(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val, u16))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writew(u16 val, void *addr) {
	volatile u16 *vaddr = (volatile u16 *)IO_TOKEN_TO_ADDR(addr);
	out_le16(vaddr, val);
}
static inline u16 eeh_raw_readw(void *addr) {
	volatile u16 *vaddr = (volatile u16 *)IO_TOKEN_TO_ADDR(addr);
	u16 val = in_be16(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val, u16))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_raw_writew(u16 val, void *addr) {
	volatile u16 *vaddr = (volatile u16 *)IO_TOKEN_TO_ADDR(addr);
	out_be16(vaddr, val);
}

static inline u32 eeh_readl(void *addr) {
	volatile u32 *vaddr = (volatile u32 *)IO_TOKEN_TO_ADDR(addr);
	u32 val = in_le32(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val, u32))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writel(u32 val, void *addr) {
	volatile u32 *vaddr = (volatile u32 *)IO_TOKEN_TO_ADDR(addr);
	out_le32(vaddr, val);
}
static inline u32 eeh_raw_readl(void *addr) {
	volatile u32 *vaddr = (volatile u32 *)IO_TOKEN_TO_ADDR(addr);
	u32 val = in_be32(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val, u32))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_raw_writel(u32 val, void *addr) {
	volatile u32 *vaddr = (volatile u32 *)IO_TOKEN_TO_ADDR(addr);
	out_be32(vaddr, val);
}

static inline u64 eeh_readq(void *addr) {
	volatile u64 *vaddr = (volatile u64 *)IO_TOKEN_TO_ADDR(addr);
	u64 val = in_le64(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val, u64))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writeq(u64 val, void *addr) {
	volatile u64 *vaddr = (volatile u64 *)IO_TOKEN_TO_ADDR(addr);
	out_le64(vaddr, val);
}
static inline u64 eeh_raw_readq(void *addr) {
	volatile u64 *vaddr = (volatile u64 *)IO_TOKEN_TO_ADDR(addr);
	u64 val = in_be64(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val, u64))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_raw_writeq(u64 val, void *addr) {
	volatile u64 *vaddr = (volatile u64 *)IO_TOKEN_TO_ADDR(addr);
	out_be64(vaddr, val);
}

static inline void eeh_memset_io(void *addr, int c, unsigned long n) {
	void *vaddr = (void *)IO_TOKEN_TO_ADDR(addr);
	memset(vaddr, c, n);
}
static inline void eeh_memcpy_fromio(void *dest, void *src, unsigned long n) {
	void *vsrc = (void *)IO_TOKEN_TO_ADDR(src);
	memcpy(dest, vsrc, n);
	/* Look for ffff's here at dest[n].  Assume that at least 4 bytes
	 * were copied. Check all four bytes.
	 */
	if ((n >= 4) &&
		(EEH_POSSIBLE_ERROR(src, vsrc, (*((u32 *) dest+n-4)), u32))) {
		eeh_check_failure(src, (*((u32 *) dest+n-4)));
	}
}

static inline void eeh_memcpy_toio(void *dest, void *src, unsigned long n) {
	void *vdest = (void *)IO_TOKEN_TO_ADDR(dest);
	memcpy(vdest, src, n);
}

#define MAX_ISA_PORT 0x10000
extern unsigned long io_page_mask;
#define _IO_IS_VALID(port) ((port) >= MAX_ISA_PORT || (1 << (port>>PAGE_SHIFT)) & io_page_mask)

static inline u8 eeh_inb(unsigned long port) {
	u8 val;
	if (!_IO_IS_VALID(port))
		return ~0;
	val = in_8((u8 *)(port+pci_io_base));
	if (EEH_POSSIBLE_IO_ERROR(val, u8))
		return eeh_check_failure((void*)(port), val);
	return val;
}

static inline void eeh_outb(u8 val, unsigned long port) {
	if (_IO_IS_VALID(port))
		return out_8((u8 *)(port+pci_io_base), val);
}

static inline u16 eeh_inw(unsigned long port) {
	u16 val;
	if (!_IO_IS_VALID(port))
		return ~0;
	val = in_le16((u16 *)(port+pci_io_base));
	if (EEH_POSSIBLE_IO_ERROR(val, u16))
		return eeh_check_failure((void*)(port), val);
	return val;
}

static inline void eeh_outw(u16 val, unsigned long port) {
	if (_IO_IS_VALID(port))
		return out_le16((u16 *)(port+pci_io_base), val);
}

static inline u32 eeh_inl(unsigned long port) {
	u32 val;
	if (!_IO_IS_VALID(port))
		return ~0;
	val = in_le32((u32 *)(port+pci_io_base));
	if (EEH_POSSIBLE_IO_ERROR(val, u32))
		return eeh_check_failure((void*)(port), val);
	return val;
}

static inline void eeh_outl(u32 val, unsigned long port) {
	if (_IO_IS_VALID(port))
		return out_le32((u32 *)(port+pci_io_base), val);
}

/* in-string eeh macros */
static inline void eeh_insb(unsigned long port, void * buf, int ns) {
	_insb((u8 *)(port+pci_io_base), buf, ns);
	if (EEH_POSSIBLE_IO_ERROR((*(((u8*)buf)+ns-1)), u8))
		eeh_check_failure((void*)(port), *(u8*)buf);
}

static inline void eeh_insw_ns(unsigned long port, void * buf, int ns) {
	_insw_ns((u16 *)(port+pci_io_base), buf, ns);
	if (EEH_POSSIBLE_IO_ERROR((*(((u16*)buf)+ns-1)), u16))
		eeh_check_failure((void*)(port), *(u16*)buf);
}

static inline void eeh_insl_ns(unsigned long port, void * buf, int nl) {
	_insl_ns((u32 *)(port+pci_io_base), buf, nl);
	if (EEH_POSSIBLE_IO_ERROR((*(((u32*)buf)+nl-1)), u32))
		eeh_check_failure((void*)(port), *(u32*)buf);
}

#endif /* _PPC64_EEH_H */
