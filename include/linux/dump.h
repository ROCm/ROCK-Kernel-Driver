/*
 * Kernel header file for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sgi.com)
 * Copyright 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 *
 * vmdump.h to dump.h by: Matt D. Robinson (yakker@sourceforge.net)
 * Copyright 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 Free Software Foundation, Inc. All rights reserved.
 *
 * Most of this is the same old stuff from vmdump.h, except now we're
 * actually a stand-alone driver plugged into the block layer interface,
 * with the exception that we now allow for compression modes externally
 * loaded (e.g., someone can come up with their own).
 *
 * This code is released under version 2 of the GNU GPL.
 */

/* This header file includes all structure definitions for crash dumps. */
#ifndef _DUMP_H
#define _DUMP_H

#if defined(CONFIG_LKCD_DUMP) || defined (CONFIG_LKCD_DUMP_MODULE)

#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/dumpdev.h>
#include <asm/ioctl.h>
#include <linux/autoconf.h>

/* 
 * Predefine default DUMP_PAGE constants, asm header may override.
 *
 * On ia64 discontinuous memory systems it's possible for the memory
 * banks to stop at 2**12 page alignments, the smallest possible page
 * size. But the system page size, PAGE_SIZE, is in fact larger.
 */
#define DUMP_PAGE_SHIFT 	PAGE_SHIFT
#define DUMP_PAGE_MASK		PAGE_MASK
#define DUMP_PAGE_ALIGN(addr)	PAGE_ALIGN(addr)

/*
 * Dump offset changed from 4Kb to 64Kb to support multiple PAGE_SIZE 
 * (kernel page size). Assumption goes that 64K is the highest page size 
 * supported 
 */

#define DUMP_HEADER_OFFSET	(1ULL << 16)

#define OLDMINORBITS	8
#define OLDMINORMASK	((1U << OLDMINORBITS) -1)

/* Making DUMP_PAGE_SIZE = PAGE_SIZE, to support dumping on architectures 
 * which support page sizes (PAGE_SIZE) greater than 4KB.
 * Will it affect ia64 discontinuous memory systems ????
 */
#define DUMP_PAGE_SIZE		PAGE_SIZE

/* thread_info lies at the bottom of stack, (Except IA64). */
#define STACK_START_POSITION(tsk)               (tsk->thread_info)
/* 
 * Predefined default memcpy() to use when copying memory to the dump buffer.
 *
 * On ia64 there is a heads up function that can be called to let the prom
 * machine check monitor know that the current activity is risky and it should
 * ignore the fault (nofault). In this case the ia64 header will redefine this
 * macro to __dump_memcpy() and use it's arch specific version.
 */
#define DUMP_memcpy		memcpy
#define bzero(a,b)              memset(a, 0, b)

/* necessary header files */
#include <asm/dump.h>			/* for architecture-specific header */

/* 
 * Size of the buffer that's used to hold:
 *
 *	1. the dump header (padded to fill the complete buffer)
 *	2. the possibly compressed page headers and data
 *   
 *  = 256k for page size >= 64k
 *  = 64k  for page size < 64k
 *  If DUMP_BUFFER_SIZE_MIN (defined in drivers/dump/Makefile) is larger than
 *  DUMP_BUFFER_SIZE then use DUMP_BUFFER_SIZE_MIN instead.
 */

#ifdef CONFIG_NR_CPUS
  #define CONFIG_NR__CPUS CONFIG_NR_CPUS
#elif CONFIG_SMP
  #define CONFIG_NR__CPUS (32)
#else
  #define CONFIG_NR__CPUS (1)
#endif

#ifdef CONFIG_IA64
#ifdef CONFIG_IA64_PAGE_SIZE_8KB
  #define  CONFIG_PAGESIZE  (8 * 1024)
#elif CONFIG_IA64_PAGE_SIZE_16KB
  #define  CONFIG_PAGESIZE  (16 * 1024)
#elif CONFIG_IA64_PAGE_SIZE_64KB
  #define  CONFIG_PAGESIZE  (64 * 1024)
#endif
#else 
  #define  CONFIG_PAGESIZE  (4 * 1024)
#endif

// ia64 asm header needs ~160 bytes + sizeof(struct pt_regs) per cpu.  Call it
// 900 bytes to be safe.
#define DUMP_BUFFER_SIZE_MIN (((CONFIG_NR__CPUS*900+CONFIG_PAGESIZE-1)/CONFIG_PAGESIZE)*CONFIG_PAGESIZE)


#if (PAGE_SHIFT >= 16)
#undef	DUMP_BUFFER_SIZE
#define DUMP_BUFFER_SIZE	(256 * 1024)  /* size of dump buffer         */
#else
#define DUMP_BUFFER_SIZE	(64 * 1024)  /* size of dump buffer         */
#endif
#if DUMP_BUFFER_SIZE < DUMP_BUFFER_SIZE_MIN
#undef	DUMP_BUFFER_SIZE
#define	DUMP_BUFFER_SIZE DUMP_BUFFER_SIZE_MIN
#endif

#define DUMP_HEADER_SIZE	DUMP_BUFFER_SIZE

/* standard header definitions */
#define DUMP_MAGIC_NUMBER	0xa8190173618f23edULL  /* dump magic number */
#define DUMP_MAGIC_LIVE		0xa8190173618f23cdULL  /* live magic number */
#define DUMP_VERSION_NUMBER	0x9	/* dump version number              */
#define DUMP_PANIC_LEN		0x100	/* dump panic string length         */

/* dump levels - type specific stuff added later -- add as necessary */
#define DUMP_LEVEL_NONE		0x0	/* no dumping at all -- just bail   */
#define DUMP_LEVEL_HEADER	0x1	/* kernel dump header only          */
#define DUMP_LEVEL_KERN		0x2	/* dump header and kernel pages     */
#define DUMP_LEVEL_USED		0x4	/* dump header, kernel/user pages   */
#define DUMP_LEVEL_ALL_RAM	0x8	/* dump header, all RAM pages       */
#define DUMP_LEVEL_ALL		0x10	/* dump all memory RAM and firmware */


/* dump compression options -- add as necessary */
#define DUMP_COMPRESS_NONE	0x0	/* don't compress this dump         */
#define DUMP_COMPRESS_RLE	0x1	/* use RLE compression              */
#define DUMP_COMPRESS_GZIP	0x2	/* use GZIP compression             */

/* dump types -- add as necessary */
#define DUMP_TYPE_BLOCKDEV	0x1	/* disk based dump    */
#define DUMP_TYPE_NETDEV	0x2	/* network based dump */
#define DUMP_TYPE_RELOAD	0x9	/* reload the current dump type*/

/* dump header flags -- add as necessary */
#define DUMP_DH_FLAGS_NONE	0x0	/* no flags set (error condition!)  */
#define DUMP_DH_RAW		0x1	/* raw page (no compression)        */
#define DUMP_DH_COMPRESSED	0x2	/* page is compressed               */
#define DUMP_DH_END		0x4	/* end marker on a full dump        */
#define DUMP_DH_TRUNCATED	0x8	/* dump is incomplete               */
#define DUMP_DH_TEST_PATTERN	0x10	/* dump page is a test pattern      */
#define DUMP_DH_NOT_USED	0x20	/* 1st bit not used in flags        */

/* names for various dump parameters in /proc/kernel */
#define DUMP_ROOT_NAME		"sys/dump"
#define DUMP_DEVICE_NAME	"device"
#define DUMP_COMPRESS_NAME	"compress"
#define DUMP_LEVEL_NAME		"level"
#define DUMP_FLAGS_NAME		"flags"
#define DUMP_ADDR_NAME		"addr"

#define DUMP_SYSRQ_KEY		'd'	/* key to use for MAGIC_SYSRQ key   */

/* CTL_DUMP names: */
enum
{
	CTL_DUMP_DEVICE=1,
	CTL_DUMP_COMPRESS=3,
	CTL_DUMP_LEVEL=3,
	CTL_DUMP_FLAGS=4,
	CTL_DUMP_ADDR=5,
	CTL_DUMP_TEST=6,
};


/* page size for gzip compression -- buffered slightly beyond hardware PAGE_SIZE used by DUMP */
#define DUMP_DPC_PAGE_SIZE	(DUMP_PAGE_SIZE + 512)

/* dump ioctl() control options */
#define DIOSDUMPDEV     _IOW('p', 0xA0, unsigned int)  /* set the dump device              */
#define DIOGDUMPDEV     _IOR('p', 0xA1, unsigned int)  /* get the dump device              */
#define DIOSDUMPLEVEL   _IOW('p', 0xA2, unsigned int)  /* set the dump level               */
#define DIOGDUMPLEVEL   _IOR('p', 0xA3, unsigned int)  /* get the dump level               */
#define DIOSDUMPFLAGS   _IOW('p', 0xA4, unsigned int)  /* set the dump flag parameters     */
#define DIOGDUMPFLAGS   _IOR('p', 0xA5, unsigned int)  /* get the dump flag parameters     */
#define DIOSDUMPCOMPRESS _IOW('p', 0xA6, unsigned int) /* set the dump compress level      */
#define DIOGDUMPCOMPRESS _IOR('p', 0xA7, unsigned int) /* get the dump compress level      */

/* these ioctls are used only by netdump module */
#define DIOSTARGETIP    _IOW('p', 0xA8, unsigned int)  /* set the target m/c's ip           */
#define DIOGTARGETIP    _IOR('p', 0xA9, unsigned int)  /* get the target m/c's ip           */
#define DIOSTARGETPORT  _IOW('p', 0xAA, unsigned int) /* set the target m/c's port          */
#define DIOGTARGETPORT  _IOR('p', 0xAB, unsigned int) /* get the target m/c's port          */
#define DIOSSOURCEPORT  _IOW('p', 0xAC, unsigned int) /* set the source m/c's port          */
#define DIOGSOURCEPORT  _IOR('p', 0xAD, unsigned int) /* get the source m/c's port          */
#define DIOSETHADDR     _IOW('p', 0xAE, unsigned int) /* set ethernet address      */
#define DIOGETHADDR     _IOR('p', 0xAF, unsigned int) /* get ethernet address       */
#define DIOGDUMPOKAY	_IOR('p', 0xB0, unsigned int) /* check if dump is configured      */
#define DIOSDUMPTAKE    _IOW('p', 0xB1, unsigned int) /* Take a manual dump               */
#define DIOGKERNELADDR	_IOR('p', 0xB2, unsigned int) /* get the physical kernel load address */

/*
 * Structure: __dump_header
 *  Function: This is the header dumped at the top of every valid crash
 *            dump.  
 */
struct __dump_header {
	/* the dump magic number -- unique to verify dump is valid */
	u64	dh_magic_number;

	/* the version number of this dump */
	u32	dh_version;

	/* the size of this header (in case we can't read it) */
	u32	dh_header_size;

	/* the level of this dump (just a header?) */
	u32	dh_dump_level;

	/* 
	 * We assume dump_page_size to be 4K in every case.
	 * Store here the configurable system page size (4K, 8K, 16K, etc.) 
	 */
	u32	dh_page_size;

	/* the size of all physical memory */
	u64	dh_memory_size;

	/* the start of physical memory */
	u64	dh_memory_start;

	/* the end of physical memory */
	u64	dh_memory_end;

	/* the number of hardware/physical pages in this dump specifically */
	u32	dh_num_dump_pages;

	/* the panic string, if available */
	char	dh_panic_string[DUMP_PANIC_LEN];

	/* timeval depends on architecture, two long values */
	struct {
		u64 tv_sec;
		u64 tv_usec;
	} dh_time; /* the time of the system crash */

	/* the NEW utsname (uname) information -- in character form */
	/* we do this so we don't have to include utsname.h         */
	/* plus it helps us be more architecture independent        */
	/* now maybe one day soon they'll make the [65] a #define!  */
	char	dh_utsname_sysname[65];
	char	dh_utsname_nodename[65];
	char	dh_utsname_release[65];
	char	dh_utsname_version[65];
	char	dh_utsname_machine[65];
	char	dh_utsname_domainname[65];

	/* the address of current task (OLD = void *, NEW = u64) */
	u64	dh_current_task;

	/* what type of compression we're using in this dump (if any) */
	u32	dh_dump_compress;

	/* polling */
	u32	dh_dump_polling;

	/* any additional flags */
	u32	dh_dump_device;

	/* size of dump buffer */
	u64	dh_dump_buffer_size;
} __attribute__((packed));

/*
 * Structure: __dump_page
 *  Function: To act as the header associated to each physical page of
 *            memory saved in the system crash dump.  This allows for
 *            easy reassembly of each crash dump page.  The address bits
 *            are split to make things easier for 64-bit/32-bit system
 *            conversions.
 *
 * dp_byte_offset and dp_page_index are landmarks that are helpful when
 * looking at a hex dump of /dev/vmdump,
 */
struct __dump_page {
	/* the address of this dump page */
	u64	dp_address;

	/* the size of this dump page */
	u32	dp_size;

	/* flags (currently DUMP_COMPRESSED, DUMP_RAW or DUMP_END) */
	u32	dp_flags;
} __attribute__((packed));

/*
 * Structure: __lkcdinfo
 * Function:  This structure contains information needed for the lkcdutils
 *            package (particularly lcrash) to determine what information is
 *            associated to this kernel, specifically.
 */
struct __lkcdinfo {
	int	arch;
	int	ptrsz;
	int	byte_order;
	int	linux_release;
	int	page_shift;
	int	page_size;
	u64	page_mask;
	u64	page_offset;
	int	stack_offset;
};

#ifdef __KERNEL__

/*
 * Structure: __dump_compress
 *  Function: This is what an individual compression mechanism can use
 *            to plug in their own compression techniques.  It's always
 *            best to build these as individual modules so that people
 *            can put in whatever they want.
 */
struct __dump_compress {
	/* the list_head structure for list storage */
	struct list_head list;

	/* the type of compression to use (DUMP_COMPRESS_XXX) */
	int compress_type;
	const char *compress_name;

	/* the compression function to call */
	u32 (*compress_func)(const u8 *, u32, u8 *, u32, unsigned long);
};

/* functions for dump compression registration */
extern void dump_register_compression(struct __dump_compress *);
extern void dump_unregister_compression(int);

extern int dump_compress_level;

/*
 * Structure dump_mbank[]:
 *
 * For CONFIG_DISCONTIGMEM systems this array specifies the
 * memory banks/chunks that need to be dumped after a panic.
 *
 * For classic systems it specifies a single set of pages from
 * 0 to max_mapnr.
 */
struct __dump_mbank {
	u64 	start;
	u64 	end;
	int	type;
	int	pad1;
	long	pad2;
};

#define DUMP_MBANK_TYPE_CONVENTIONAL_MEMORY		1
#define DUMP_MBANK_TYPE_OTHER				2

#define MAXCHUNKS 256
extern int dump_mbanks;
extern struct __dump_mbank dump_mbank[MAXCHUNKS];

/* notification event codes */
#define DUMP_BEGIN		0x0001	/* dump beginning */
#define DUMP_END		0x0002	/* dump ending */

/* Scheduler soft spin control.
 *
 * 0 - no dump in progress
 * 1 - cpu0 is dumping, ...
 */
extern unsigned long dump_oncpu;
extern void dump_execute(const char *, const struct pt_regs *);

/*
 *	Notifier list for kernel code which wants to be called
 *	at kernel dump. 
 */
extern struct notifier_block *dump_notifier_list;
static inline int register_dump_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&dump_notifier_list, nb);
}
static inline int unregister_dump_notifier(struct notifier_block * nb)
{
	return notifier_chain_unregister(&dump_notifier_list, nb);
}

extern void (*dump_function_ptr)(const char *, const struct pt_regs *);
static inline void dump(char * str, struct pt_regs * regs)
{
	if (dump_function_ptr)
		dump_function_ptr(str, regs);
}

/*
 * Common Arch Specific Functions should be declared here.
 * This allows the C compiler to detect discrepancies.
 */
extern void	__dump_open(void);
extern void	__dump_cleanup(void);
extern void	__dump_clean_irq_state(void);
extern void	__dump_init(u64);
extern void	__dump_save_regs(struct pt_regs *, const struct pt_regs *);
extern void	__dump_save_context(int cpu, const struct pt_regs *, struct task_struct *tsk);
extern int	__dump_configure_header(const struct pt_regs *);
extern int	__dump_irq_enable(void);
extern void	__dump_irq_restore(void);
extern int	__dump_page_valid(unsigned long index);
#ifdef CONFIG_SMP
extern void 	__dump_save_other_cpus(void);
#else
#define 	__dump_save_other_cpus()
#endif
extern unsigned long __dump_kernel_addr(void);

extern int manual_handle_crashdump(void);

/* to track all used (compound + zero order) pages */
#define PageInuse(p)   (PageCompound(p) || page_count(p))

#endif /* __KERNEL__ */

#else	/* !CONFIG_LKCD_DUMP */

/* If not configured then make code disappear! */
#define register_dump_watchdog(x) 	do { } while(0)
#define unregister_dump_watchdog(x) 	do { } while(0)
#define register_dump_notifier(x)	do { } while(0)
#define unregister_dump_notifier(x)	do { } while(0)
#define dump_in_progress() 		0
#define dump(x, y)			do { } while(0)

#endif	/* !CONFIG_LKCD_DUMP */

#endif /* _DUMP_H */
