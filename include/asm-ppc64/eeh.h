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

/* Start Change Log
 * 2001/10/27 : engebret : Created.
 * End Change Log 
 */

#ifndef _EEH_H
#define _EEH_H

struct pci_dev;

#define IO_UNMAPPED_REGION_ID 0xaUL

#define IO_TOKEN_TO_ADDR(token) ((((unsigned long)(token)) & 0xFFFFFFFF) | (0xEUL << 60))
/* Flag bits encoded in the 3 unused function bits of devfn */
#define EEH_TOKEN_DISABLED (1UL << 34UL)	/* eeh is disabled for this token */
#define IS_EEH_TOKEN_DISABLED(token) ((unsigned long)(token) & EEH_TOKEN_DISABLED)

#define EEH_STATE_OVERRIDE 1   /* IOA does not require eeh traps */
#define EEH_STATE_FAILURE  16  /* */

/* This is for profiling only */
extern unsigned long eeh_total_mmio_ffs;

extern int eeh_implemented;

void eeh_init(void);
static inline int is_eeh_implemented(void) { return eeh_implemented; }
int eeh_get_state(unsigned long ea);
unsigned long eeh_check_failure(void *token, unsigned long val);

#define EEH_DISABLE		0
#define EEH_ENABLE		1
#define EEH_RELEASE_LOADSTORE	2
#define EEH_RELEASE_DMA		3
int eeh_set_option(struct pci_dev *dev, int options);

/* Given a PCI device check if eeh should be configured or not.
 * This may look at firmware properties and/or kernel cmdline options.
 */
int is_eeh_configured(struct pci_dev *dev);

/* Generate an EEH token.
 * The high nibble of the offset is cleared, otherwise bounds checking is performed.
 * Use IO_TOKEN_TO_ADDR(token) to translate this token back to a mapped virtual addr.
 * Do NOT do this to perform IO -- use the read/write macros!
 */
unsigned long eeh_token(unsigned long phb,
			unsigned long bus,
			unsigned long devfn,
			unsigned long offset);

extern void *memcpy(void *, const void *, unsigned long);
extern void *memset(void *,int, unsigned long);

/* EEH_POSSIBLE_ERROR() -- test for possible MMIO failure.
 *
 * Order this macro for performance.
 * If EEH is off for a device and it is a memory BAR, ioremap will
 * map it to the IOREGION.  In this case addr == vaddr and since these
 * should be in registers we compare them first.  Next we check for
 * all ones which is perhaps fastest as ~val.  Finally we weed out
 * EEH disabled IO BARs.
 *
 * If this macro yields TRUE, the caller relays to eeh_check_failure()
 * which does further tests out of line.
 */
/* #define EEH_POSSIBLE_ERROR(addr, vaddr, val) ((vaddr) != (addr) && ~(val) == 0 && !IS_EEH_TOKEN_DISABLED(addr)) */
/* This version is rearranged to collect some profiling data */
#define EEH_POSSIBLE_ERROR(addr, vaddr, val) (~(val) == 0 && (++eeh_total_mmio_ffs, (vaddr) != (addr) && !IS_EEH_TOKEN_DISABLED(addr)))

/* 
 * MMIO read/write operations with EEH support.
 *
 * addr: 64b token of the form 0xA0PPBBDDyyyyyyyy
 *       0xA0     : Unmapped MMIO region
 *       PP       : PHB index (starting at zero)
 *	 BB	  : PCI Bus number under given PHB
 *	 DD	  : PCI devfn under given bus
 *       yyyyyyyy : Virtual address offset
 * 
 * An actual virtual address is produced from this token
 * by masking into the form:
 *   0xE0000000yyyyyyyy
 */
static inline u8 eeh_readb(void *addr) {
	volatile u8 *vaddr = (volatile u8 *)IO_TOKEN_TO_ADDR(addr);
	u8 val = in_8(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val))
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
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writew(u16 val, void *addr) {
	volatile u16 *vaddr = (volatile u16 *)IO_TOKEN_TO_ADDR(addr);
	out_le16(vaddr, val);
}
static inline u32 eeh_readl(void *addr) {
	volatile u32 *vaddr = (volatile u32 *)IO_TOKEN_TO_ADDR(addr);
	u32 val = in_le32(vaddr);
	if (EEH_POSSIBLE_ERROR(addr, vaddr, val))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writel(u32 val, void *addr) {
	volatile u32 *vaddr = (volatile u32 *)IO_TOKEN_TO_ADDR(addr);
	out_le32(vaddr, val);
}

static inline void eeh_memset_io(void *addr, int c, unsigned long n) {
	void *vaddr = (void *)IO_TOKEN_TO_ADDR(addr);
	memset(vaddr, c, n);
}
static inline void eeh_memcpy_fromio(void *dest, void *src, unsigned long n) {
	void *vsrc = (void *)IO_TOKEN_TO_ADDR(src);
	memcpy(dest, vsrc, n);
	/* look for ffff's here at dest[n] */
}
static inline void eeh_memcpy_toio(void *dest, void *src, unsigned long n) {
	void *vdest = (void *)IO_TOKEN_TO_ADDR(dest);
	memcpy(vdest, src, n);
}

#endif /* _EEH_H */
