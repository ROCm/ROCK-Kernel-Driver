#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

/*
 * The layout of the per-CPU GDT under Linux:
 *
 *   0 - null
 *   1 - reserved
 *   2 - reserved
 *   3 - reserved
 *
 *   4 - default user CS		<==== new cacheline
 *   5 - default user DS
 *
 *  ------- start of TLS (Thread-Local Storage) segments:
 *
 *   6 - TLS segment #1			[ glibc's TLS segment ]
 *   7 - TLS segment #2			[ Wine's %fs Win32 segment ]
 *   8 - TLS segment #3
 *   9 - reserved
 *  10 - reserved
 *  11 - reserved
 *
 *  ------- start of kernel segments:
 *
 *  12 - kernel code segment		<==== new cacheline
 *  13 - kernel data segment
 *  14 - TSS
 *  15 - LDT
 *  16 - PNPBIOS support (16->32 gate)
 *  17 - PNPBIOS support
 *  18 - PNPBIOS support
 *  19 - PNPBIOS support
 *  20 - PNPBIOS support
 *  21 - APM BIOS support
 *  22 - APM BIOS support
 *  23 - APM BIOS support 
 */
#define GDT_ENTRY_TLS_ENTRIES	3
#define GDT_ENTRY_TLS_MIN	6
#define GDT_ENTRY_TLS_MAX 	(GDT_ENTRY_TLS_MIN + GDT_ENTRY_TLS_ENTRIES - 1)

#define TLS_SIZE (GDT_ENTRY_TLS_ENTRIES * 8)

#define GDT_ENTRY_DEFAULT_USER_CS	4
#define __USER_CS (GDT_ENTRY_DEFAULT_USER_CS * 8 + 3)

#define GDT_ENTRY_DEFAULT_USER_DS	5
#define __USER_DS (GDT_ENTRY_DEFAULT_USER_DS * 8 + 3)

#define GDT_ENTRY_KERNEL_BASE	12

#define GDT_ENTRY_KERNEL_CS		(GDT_ENTRY_KERNEL_BASE + 0)
#define __KERNEL_CS (GDT_ENTRY_KERNEL_CS * 8)

#define GDT_ENTRY_KERNEL_DS		(GDT_ENTRY_KERNEL_BASE + 1)
#define __KERNEL_DS (GDT_ENTRY_KERNEL_DS * 8)

#define GDT_ENTRY_TSS			(GDT_ENTRY_KERNEL_BASE + 2)
#define GDT_ENTRY_LDT			(GDT_ENTRY_KERNEL_BASE + 3)

#define GDT_ENTRY_PNPBIOS_BASE		(GDT_ENTRY_KERNEL_BASE + 4)
#define GDT_ENTRY_APMBIOS_BASE		(GDT_ENTRY_KERNEL_BASE + 9)

/*
 * The GDT has 21 entries but we pad it to cacheline boundary:
 */
#define GDT_ENTRIES 24

#define GDT_SIZE (GDT_ENTRIES * 8)

/*
 * The interrupt descriptor table has room for 256 idt's,
 * the global descriptor table is dependent on the number
 * of tasks we can have..
 */
#define IDT_ENTRIES 256

#endif
