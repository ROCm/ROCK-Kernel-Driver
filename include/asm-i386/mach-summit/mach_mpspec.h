#ifndef __ASM_MACH_MPSPEC_H
#define __ASM_MACH_MPSPEC_H

/*
 * a maximum of 256 APICs with the current APIC ID architecture.
 */
#define MAX_APICS 256

#define MAX_IRQ_SOURCES 256

/* Maximum 256 PCI busses, plus 1 ISA bus in each of 4 cabinets. */
#define MAX_MP_BUSSES 260

#endif /* __ASM_MACH_MPSPEC_H */
