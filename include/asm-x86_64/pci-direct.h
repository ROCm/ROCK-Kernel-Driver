#ifndef ASM_PCI_DIRECT_H
#define ASM_PCI_DIRECT_H 1

#include <linux/types.h>
#include <asm/io.h>

/* Direct PCI access. This is used for PCI accesses in early boot before
   the PCI subsystem works. */ 

#define PDprintk(x...)

static inline u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset)
{
	u32 v; 
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	v = inl(0xcfc); 
	PDprintk("%x reading from %x: %x\n", slot, offset, v);
	return v;
}

static inline void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset,
				    u32 val)
{
	PDprintk("%x writing to %x: %x\n", slot, offset, val); 
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	outl(val, 0xcfc); 
}

#endif
