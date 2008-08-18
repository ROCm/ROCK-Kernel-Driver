/*
 * $Id: kl_dump.h 1336 2006-10-23 23:27:06Z tjm $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2005 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_DUMP_H
#define __KL_DUMP_H

#if 0
cpw: dont need:
#include <klib.h>
#include <asm/ioctl.h>
#endif

/*
 * DUMP_DEBUG: a debug level for the kernel dump code and
 *             the supporting lkcd libraries in user space.
 *
 * 0: FALSE: No Debug Added
 * 1: TRUE:  Break Points
 * .
 * .
 * .
 * 6: Add Debug Data to Structures
 * .
 * .
 * 9: Max
 */
#define DUMP_DEBUG FALSE

#if DUMP_DEBUG
void dump_bp(void);                     /* Called when something exceptional occures */
# define DUMP_BP() dump_bp()            /* BreakPoint */
#else
# define DUMP_BP()
#endif


#define KL_UTS_LEN 65     /* do not change ... */

extern int SN2_24X;

/*
 * Size of the buffer that's used to hold:
 *
 *	1. the dump header (paded to fill the complete buffer)
 *	2. the possibly compressed page headers and data
 */
extern uint64_t KL_DUMP_BUFFER_SIZE;
extern uint64_t KL_DUMP_HEADER_SIZE;

#if 0
/* Variables that contain page size, mask etc. used in dump format
 * (this is not the system page size stored in the dump header)
 */
uint64_t KL_DUMP_PAGE_SIZE;
uint64_t KL_DUMP_PAGE_MASK;
uint64_t KL_DUMP_PAGE_SHIFT;
#endif

/* Dump header offset changed from 4k to 64k to support multiple page sizes */
#define KL_DUMP_HEADER_OFFSET  (1ULL << 16)


/* header definitions for dumps from s390 standalone dump tools */
#define KL_DUMP_MAGIC_S390SA     0xa8190173618f23fdULL /* s390sa magic number */
#define KL_DUMP_HEADER_SZ_S390SA 4096

/* standard header definitions */
#define KL_DUMP_MAGIC_NUMBER  0xa8190173618f23edULL  /* dump magic number  */
#define KL_DUMP_MAGIC_LIVE    0xa8190173618f23cdULL  /* live magic number  */
#define KL_DUMP_MAGIC_ASM     0xdeaddeadULL  /* generic arch magic number  */
#define KL_DUMP_VERSION_NUMBER 0x8      /* dump version number             */
#define KL_DUMP_PANIC_LEN      0x100    /* dump panic string length        */

/* dump levels - type specific stuff added later -- add as necessary */
#define KL_DUMP_LEVEL_NONE        0x0   /* no dumping at all -- just bail   */
#define KL_DUMP_LEVEL_HEADER      0x1   /* kernel dump header only          */
#define KL_DUMP_LEVEL_KERN        0x2   /* dump header and kernel pages     */
#define KL_DUMP_LEVEL_USED        0x4   /* dump header, kernel/user pages   */
#define KL_DUMP_LEVEL_ALL_RAM     0x8   /* dump header, all RAM pages       */
#define KL_DUMP_LEVEL_ALL         0x10  /* dump all memory RAM and firmware */

/* dump compression options -- add as necessary */
#define KL_DUMP_COMPRESS_NONE     0x0      /* don't compress this dump      */
#define KL_DUMP_COMPRESS_RLE      0x1      /* use RLE compression           */
#define KL_DUMP_COMPRESS_GZIP     0x2      /* use GZIP compression          */

/* dump flags - any dump-type specific flags -- add as necessary */
#define KL_DUMP_FLAGS_NONE        0x0   /* no flags are set for this dump   */
#define KL_DUMP_FLAGS_NONDISRUPT  0x1   /* try to keep running after dump   */
#define KL_DUMP_FLAGS_DISKDUMP	  0x80000000 /* dump to local disk 	    */
#define KL_DUMP_FLAGS_NETDUMP	  0x40000000 /* dump to network device      */

/* dump header flags -- add as necessary */
#define KL_DUMP_DH_FLAGS_NONE     0x0   /* no flags set (error condition!)  */
#define KL_DUMP_DH_RAW            0x1   /* raw page (no compression)        */
#define KL_DUMP_DH_COMPRESSED     0x2   /* page is compressed               */
#define KL_DUMP_DH_END            0x4   /* end marker on a full dump        */
#define KL_DUMP_DH_TRUNCATED      0x8   /* dump is incomplete               */
#define KL_DUMP_DH_TEST_PATTERN   0x10  /* dump page is a test pattern      */
#define KL_DUMP_DH_NOT_USED       0x20  /* 1st bit not used in flags        */

/*  dump ioctl() control options */
#ifdef IOCTL26
#define DIOSDUMPDEV     _IOW('p', 0xA0, unsigned int)	/* set the dump device              */
#define DIOGDUMPDEV     _IOR('p', 0xA1, unsigned int)	/* get the dump device              */
#define DIOSDUMPLEVEL   _IOW('p', 0xA2, unsigned int)	/* set the dump level               */
#define DIOGDUMPLEVEL   _IOR('p', 0xA3, unsigned int)	/* get the dump level               */
#define DIOSDUMPFLAGS   _IOW('p', 0xA4, unsigned int)	/* set the dump flag parameters     */
#define DIOGDUMPFLAGS   _IOR('p', 0xA5, unsigned int)	/* get the dump flag parameters     */
#define DIOSDUMPCOMPRESS _IOW('p', 0xA6, unsigned int)	/* set the dump compress level      */
#define DIOGDUMPCOMPRESS _IOR('p', 0xA7, unsigned int)	/* get the dump compress level      */

/* these ioctls are used only by netdump module */
#define DIOSTARGETIP    _IOW('p', 0xA8, unsigned int)	/* set the target m/c's ip          */
#define DIOGTARGETIP    _IOR('p', 0xA9, unsigned int)  /* get the target m/c's ip           */
#define DIOSTARGETPORT  _IOW('p', 0xAA, unsigned int) /* set the target m/c's port          */
#define DIOGTARGETPORT  _IOR('p', 0xAB, unsigned int) /* get the target m/c's port          */
#define DIOSSOURCEPORT  _IOW('p', 0xAC, unsigned int) /* set the source m/c's port          */
#define DIOGSOURCEPORT  _IOR('p', 0xAD, unsigned int) /* get the source m/c's port          */
#define DIOSETHADDR     _IOW('p', 0xAE, unsigned int) /* set ethernet address       */
#define DIOGETHADDR     _IOR('p', 0xAF, unsigned int) /* get ethernet address       */
#define DIOGDUMPOKAY	_IOR('p', 0xB0, unsigned int) /* check if dump is configured      */
#define DIOSDUMPTAKE	_IOW('p', 0xB1, unsigned int) /* take a manual dump               */
#else
#define DIOSDUMPDEV		1       /* set the dump device              */
#define DIOGDUMPDEV		2       /* get the dump device              */
#define DIOSDUMPLEVEL		3       /* set the dump level               */
#define DIOGDUMPLEVEL		4       /* get the dump level               */
#define DIOSDUMPFLAGS		5       /* set the dump flag parameters     */
#define DIOGDUMPFLAGS		6       /* get the dump flag parameters     */
#define DIOSDUMPCOMPRESS	7       /* set the dump compress level      */
#define DIOGDUMPCOMPRESS	8       /* get the dump compress level      */
#define DIOSTARGETIP		9	/* set the target m/c's ip	    */
#define DIOGTARGETIP		10	/* get the target m/c's ip	    */
#define DIOSTARGETPORT		11	/* set the target m/c's port	    */
#define DIOGTARGETPORT		12	/* get the target m/c's port	    */
#define DIOSSOURCEPORT		13	/* set the source m/c's port	    */
#define DIOGSOURCEPORT		14	/* get the source m/c's port	    */
#define DIOSETHADDR		15	/* set ethernet address		    */
#define DIOGETHADDR		16	/* get ethernet address		    */
#define DIOGDUMPOKAY		17	/* check if dump is configured      */
#define DIOSDUMPTAKE		18	/* take a manual dump		    */
#endif

/*
 * structures
 */

/* This is the header dumped at the top of every valid crash dump.
 */
typedef struct kl_dump_header_s {
	uint64_t magic_number; /* dump magic number, unique to verify dump */
	uint32_t version;      /* version number of this dump */
	uint32_t header_size;  /* size of this header */
	uint32_t dump_level;   /* level of this dump */
	/* FIXME: rename page_size to dump_page_size
	 * The size of a hardware/physical memory page (DUMP_PAGE_SIZE).
	 * NB: Not the configurable system page (PAGE_SIZE) (4K, 8K, 16K, etc.)
	 */
/* 	uint32_t             dh_dump_page_size; */
	uint32_t page_size;    /* page size (e.g. 4K, 8K, 16K, etc.) */
	uint64_t memory_size;  /* size of entire physical memory */
	uint64_t memory_start; /* start of physical memory */
	uint64_t memory_end;	  /* end of physical memory */
#if DUMP_DEBUG >= 6
        uint64_t num_bytes; /* number of bytes in this dump */
#endif
	/* the number of dump pages in this dump specifically */
	uint32_t num_dump_pages;
	char panic_string[KL_DUMP_PANIC_LEN]; /* panic string, if available*/

	/* timeval depends on machine, two long values */
	struct {uint64_t tv_sec;
		uint64_t tv_usec;
	} time; /* the time of the system crash */

	/* the NEW utsname (uname) information -- in character form */
	/* we do this so we don't have to include utsname.h         */
	/* plus it helps us be more architecture independent        */
	char utsname_sysname[KL_UTS_LEN];
	char utsname_nodename[KL_UTS_LEN];
	char utsname_release[KL_UTS_LEN];
	char utsname_version[KL_UTS_LEN];
	char utsname_machine[KL_UTS_LEN];
	char utsname_domainname[KL_UTS_LEN];

	uint64_t current_task; /* fixme: better use uint64_t here */
	uint32_t dump_compress; /* compression type used in this dump */
	uint32_t dump_flags;	   /* any additional flags */
	uint32_t dump_device;   /* any additional flags */
	uint64_t dump_buffer_size; /* version >= 9 */
} __attribute__((packed)) kl_dump_header_t;

/* This is the header used by the s390 standalone dump tools
 */
typedef struct kl_dump_header_s390sa_s {
	uint64_t magic_number; /* magic number for this dump (unique)*/
	uint32_t version;      /* version number of this dump */
	uint32_t header_size;  /* size of this header */
	uint32_t dump_level;   /* the level of this dump (just a header?) */
	uint32_t page_size;    /* page size of dumped Linux (4K,8K,16K etc.) */
	uint64_t memory_size;  /* the size of all physical memory */
	uint64_t memory_start; /* the start of physical memory */
	uint64_t memory_end;   /* the end of physical memory */
	uint32_t num_pages;    /* number of pages in this dump */
	uint32_t pad;	       /* ensure 8 byte alignment for tod and cpu_id */
	uint64_t tod;	       /* the time of the dump generation */
	uint64_t cpu_id;       /* cpu id */
	uint32_t arch_id;
	uint32_t build_arch_id;
#define KL_DH_ARCH_ID_S390X 2
#define KL_DH_ARCH_ID_S390  1
} __attribute__((packed))  kl_dump_header_s390sa_t;

/* Header associated to each physical page of memory saved in the system
 * crash dump.
 */
typedef struct kl_dump_page_s {
#if DUMP_DEBUG >= 6
	uint64_t byte_offset; /* byte offset */
	uint64_t page_index;  /* page index */
#endif
	uint64_t address; /* the address of this dump page */
	uint32_t size;  /* the size of this dump page */
	uint32_t flags; /* flags (DUMP_COMPRESSED, DUMP_RAW or DUMP_END) */
} __attribute__((packed)) kl_dump_page_t;

/* CORE_TYPE indicating type of dump
 */
typedef enum {
	dev_kmem,   /* image of /dev/kmem, a running kernel */
	reg_core,   /* Regular (uncompressed) core file */
	s390_core,  /* s390 core file */
	cmp_core,   /* compressed core file */
	unk_core    /* unknown core type */
} CORE_TYPE;

/* function to determine kernel stack for task */
typedef kaddr_t(*kl_kernelstack_t) (kaddr_t);
/* map virtual address to physical one */
typedef int(*kl_virtop_t)(kaddr_t, void*, kaddr_t*);
/* function to perform page-table traversal */
typedef kaddr_t(*kl_mmap_virtop_t)(kaddr_t, void*);
/* XXX description */
typedef int(*kl_valid_physmem_t)(kaddr_t, int);
/* XXX description */
typedef kaddr_t(*kl_next_valid_physaddr_t)(kaddr_t);
/* write a dump-header-asm, if analyzing a live system */
typedef int(*kl_write_dump_header_asm_t)(void*);
/* redirect addresses pointing into task_union areas for running tasks */
typedef kaddr_t(*kl_fix_vaddr_t)(kaddr_t, size_t);
/* initialize mapping of virtual to physical addresses */
typedef int (*kl_init_virtop_t)(void);

/* struct storing dump architecture specific values
 */
typedef struct kl_dumparch_s {
	int             arch;           /* KL_ARCH_ */
	int             ptrsz;          /* 32 or 64 bit */
	int             byteorder;      /* KL_LITTLE_ENDIAN or KL_BIG_ENDIAN */
	uint64_t        pageoffset;     /* PAGE_OFFSET */
	uint64_t        kstacksize;     /* size of kernel stack */
	uint64_t        pgdshift;       /* PGDIR_SHIFT */
	uint64_t        pgdsize;        /* PGDIR_SIZE */
	uint64_t        pgdmask;        /* PGDIR_MASK */
	uint64_t        pmdshift;       /* PMD_SHIFT */
	uint64_t        pmdsize;        /* PMD_SIZE */
	uint64_t        pmdmask;        /* PMD_MASK */
	uint64_t        pageshift;      /* PAGE_SHIFT */
	uint64_t        pagesize;       /* PAGE_SIZE */
	uint64_t        pagemask;       /* PAGE_MASK */
	uint32_t        ptrsperpgd;     /* PTRS_PER_PGD */
	uint32_t        ptrsperpmd;     /* PTRS_PER_PMD */
	uint32_t        ptrsperpte;     /* PTRS_PER_PTE */
	kl_kernelstack_t   kernelstack; /* determine kernel stack for task */
	kl_virtop_t     virtop;         /* map virtual address to physical */
	kl_mmap_virtop_t   mmap_virtop; /* traverse page table */
	kl_valid_physmem_t valid_physmem; /* XXX description */
	kl_next_valid_physaddr_t next_valid_physaddr; /* XXX description */
	kl_fix_vaddr_t  fix_vaddr;      /* XXX description */
	uint32_t        dha_size;       /* size of kl_dump_header_xxx_t */
	kl_write_dump_header_asm_t write_dha; /* XXX description */
	kl_init_virtop_t init_virtop;   /* init address translation */
} kl_dumparch_t;

/* function types for dumpaccess */
typedef kaddr_t (*kl_get_ptr_t)   (void*);
typedef uint8_t (*kl_get_uint8_t) (void*);
typedef uint16_t(*kl_get_uint16_t)(void*);
typedef uint32_t(*kl_get_uint32_t)(void*);
typedef uint64_t(*kl_get_uint64_t)(void*);
/* function types for dumpaccess */
typedef	kaddr_t  (*kl_read_ptr_t)   (kaddr_t);
typedef	uint8_t  (*kl_read_uint8_t) (kaddr_t);
typedef	uint16_t (*kl_read_uint16_t)(kaddr_t);
typedef	uint32_t (*kl_read_uint32_t)(kaddr_t);
typedef	uint64_t (*kl_read_uint64_t)(kaddr_t);

/* struct to store dump architecture specific functions
 */
typedef struct kl_dumpaccess_s {
	/* get integer value from memory, previously read from dump */
        kl_get_ptr_t    get_ptr;
	kl_get_uint8_t  get_uint8;
	kl_get_uint16_t get_uint16;
	kl_get_uint32_t get_uint32;
	kl_get_uint64_t get_uint64;
	/* read integer value from dump (from physical address) */
        kl_read_ptr_t    read_ptr;
	kl_read_uint8_t  read_uint8;
	kl_read_uint16_t read_uint16;
	kl_read_uint32_t read_uint32;
	kl_read_uint64_t read_uint64;
	/* read integer value from dump (from virtual address) */
        kl_read_ptr_t    vread_ptr;
	kl_read_uint8_t  vread_uint8;
	kl_read_uint16_t vread_uint16;
	kl_read_uint32_t vread_uint32;
	kl_read_uint64_t vread_uint64;
} kl_dumpaccess_t;

/* Struct containing sizes of frequently used kernel structures.
 */
typedef struct struct_sizes_s {
	int     task_struct_sz;
	int     mm_struct_sz;
	int	page_sz;
	int	module_sz;
	int	new_utsname_sz;
	int	switch_stack_sz;
	int	pt_regs_sz;
	int	pglist_data_sz;
	int	runqueue_sz;
} struct_sizes_t;

/* struct storing memory specifc values of the dumped Linux system
 */
typedef struct kl_kerninfo_s{
	kaddr_t         num_physpages;  /* number of physical pages */
	kaddr_t         mem_map;        /* XXX description */
	kaddr_t         high_memory;    /* physical memory size */
	kaddr_t         init_mm;        /* address of mm_struct init_mm */
	uint64_t        kernel_flags;   /* to indicate kernel features
					 * e.g. KL_IS_PAE_I386 on i386 */
	int             num_cpus;       /* number of cpus */
	kaddr_t         pgdat_list;     /* pgdat_list value. used as MEM_MAP */
					/* not defined for DISCONTIG memory */
	int             linux_release;  /* kernel release of dump */
	struct_sizes_t  struct_sizes;   /* frequently needed struct sizes */
} kl_kerninfo_t;

/* various flags to indicate Linux kernel compile switches */
#define KL_IS_PAE_I386  0x0020 /* i386 kernel with PAE support */

/* struct where to keep whole information about the dump
 */
typedef struct kl_dumpinfo_s {
	CORE_TYPE	core_type;      /* type of core file   */
	char	        *dump;          /* pathname for dump  */
	char	        *map;           /* pathname for map file */
	int		core_fd;        /* file descriptor for dump file  */
	int		rw_flag;        /* O_RDONLY/O_RDWR (/dev/kmem only) */
	kl_dumparch_t   arch;           /* dump arch info */
	kl_dumpaccess_t func;           /* dump access functions */
	kl_kerninfo_t   mem;            /* mem info for dump */
} kl_dumpinfo_t;

/* External declarations
 */
extern char *dh_typename;
extern char *dha_typename;
extern void *G_dump_header;
extern void *G_dump_header_asm;
extern kl_dump_header_t *KL_DUMP_HEADER;
extern void *KL_DUMP_HEADER_ASM;

/* function declarations
 */

/* open dump */
int  kl_open_dump(void);

/* init sizes for some structures */
void kl_init_struct_sizes(void);

/* init host architecture information */
int  kl_setup_hostinfo(void);

/* init dumpinfo structure */
int  kl_setup_dumpinfo(char *		/* map file */,
		      char *		/* dump */,
		      int 		/* rwflag */);


/* init dumpinfo structure */
int  kl_set_dumpinfo(char *		/* map file */,
		     char *		/* dump */,
		     int 		/* arch of dump */,
		     int 		/* rwflag */);

/* free dumpinfo structure */
void kl_free_dumpinfo(kl_dumpinfo_t *);

/* set memory related characteristics of dump */
int  kl_set_kerninfo(void);

/* set function pointers for dump access (depends on host and dump arch) */
int  kl_set_dumpaccess(void);

/* print contents of kl_dumpinfo_t etc. */
int  kl_print_dumpinfo(int);
#define KL_INFO_ALL      0
#define KL_INFO_ENDIAN   1
#define KL_INFO_ARCH     2
#define KL_INFO_PTRSZ    3
#define KL_INFO_KRELEASE 4
#define KL_INFO_MEMSIZE  5
#define KL_INFO_NUMCPUS  6

/* Functions that read data from generic dump_header */
int kl_valid_dump_magic(uint64_t);
int kl_header_swap(void *);
uint64_t kl_header_magic(void *);
int kl_valid_header(void *);
uint32_t kl_header_version(void *);
int kl_header_size(void *);
void *kl_read_header(int fd, void *);

/* init common lkcd dump header from dump */
void kl_init_dump_header(int);

/* try to evalutate arch from lkcd 4.1 (version <= 7) dump header */
int kl_dump_arch_4_1(void *);

/* swap dump header values if necessary */
void kl_swap_dump_header_reg(kl_dump_header_t* dh);
void kl_swap_dump_header_s390sa(kl_dump_header_s390sa_t* dh);

/* Read dump header in from dump */
int kl_read_dump_header(void);
int kl_read_dump_header_asm(void);

/* Determine the architecure of dump */
int kl_set_dumparch(int);

/* Finish setting up for access to dump */
int kl_setup_dumpaccess(int);

/* get the raw dump header */
int kl_get_raw_dh(int);
int kl_get_raw_asm_dh(int);

/* get common lkcd dump header */
int kl_get_dump_header(kl_dump_header_t*);

/* get older style dump headers */
kl_dump_header_t *get_dump_header_4_1(void *);
kl_dump_header_t *get_dump_header_SN2_24X(void *);

/* get task that was running when dump was started */
kaddr_t kl_dumptask(void);

/* Print dump header */
int kl_print_dump_header(const char* dump);

/* Print dump regular header */
void kl_print_dump_header_reg(kl_dump_header_t *);

/* Print s390 dump header */
void kl_print_dump_header_s390(char*);

/* Convert s390 to reg header */
void kl_s390sa_to_reg_header(kl_dump_header_s390sa_t*, kl_dump_header_t*);

/* Byte swapping functions needed for Xclrash */
/* get integer value from buffer and swap bytes */
kaddr_t  kl_get_swap_ptr(void*);
uint16_t kl_get_swap_uint16(void*);
uint32_t kl_get_swap_uint32(void*);
uint64_t kl_get_swap_uint64(void*);

/* read integer value from dump (physical address) and swap bytes */
kaddr_t  kl_read_swap_ptr(kaddr_t);
uint16_t kl_read_swap_uint16(kaddr_t);
uint32_t kl_read_swap_uint32(kaddr_t);
uint64_t kl_read_swap_uint64(kaddr_t);

/* read integer value from dump (virtual address) and swap bytes */
kaddr_t  kl_vread_swap_ptr(kaddr_t);
uint16_t kl_vread_swap_uint16(kaddr_t);
uint32_t kl_vread_swap_uint32(kaddr_t);
uint64_t kl_vread_swap_uint64(kaddr_t);

#endif /* __KL_DUMP_H */
