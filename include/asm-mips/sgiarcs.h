/* $Id: sgiarcs.h,v 1.3 1999/02/25 20:55:08 tsbogend Exp $
 *
 * SGI ARCS firmware interface defines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#ifndef _ASM_SGIARCS_H
#define _ASM_SGIARCS_H

#include <asm/arc/types.h>

/* Various ARCS error codes. */
#define PROM_ESUCCESS                   0x00
#define PROM_E2BIG                      0x01
#define PROM_EACCESS                    0x02
#define PROM_EAGAIN                     0x03
#define PROM_EBADF                      0x04
#define PROM_EBUSY                      0x05
#define PROM_EFAULT                     0x06
#define PROM_EINVAL                     0x07
#define PROM_EIO                        0x08
#define PROM_EISDIR                     0x09
#define PROM_EMFILE                     0x0a
#define PROM_EMLINK                     0x0b
#define PROM_ENAMETOOLONG               0x0c
#define PROM_ENODEV                     0x0d
#define PROM_ENOENT                     0x0e
#define PROM_ENOEXEC                    0x0f
#define PROM_ENOMEM                     0x10
#define PROM_ENOSPC                     0x11
#define PROM_ENOTDIR                    0x12
#define PROM_ENOTTY                     0x13
#define PROM_ENXIO                      0x14
#define PROM_EROFS                      0x15
/* SGI ARCS specific errno's. */
#define PROM_EADDRNOTAVAIL              0x1f
#define PROM_ETIMEDOUT                  0x20
#define PROM_ECONNABORTED               0x21
#define PROM_ENOCONNECT                 0x22

/* Device classes, types, and identifiers for prom
 * device inventory queries.
 */
enum linux_devclass {
	system, processor, cache, adapter, controller, peripheral, memory
};

enum linux_devtypes {
	/* Generic stuff. */
	Arc, Cpu, Fpu,

	/* Primary insn and data caches. */
	picache, pdcache,

	/* Secondary insn, data, and combined caches. */
	sicache, sdcache, sccache,

	memdev, eisa_adapter, tc_adapter, scsi_adapter, dti_adapter,
	multifunc_adapter, dsk_controller, tp_controller, cdrom_controller,
	worm_controller, serial_controller, net_controller, disp_controller,
	parallel_controller, ptr_controller, kbd_controller, audio_controller,
	misc_controller, disk_peripheral, flpy_peripheral, tp_peripheral,
	modem_peripheral, monitor_peripheral, printer_peripheral,
	ptr_peripheral, kbd_peripheral, term_peripheral, line_peripheral,
	net_peripheral, misc_peripheral, anon
};

enum linux_identifier {
	bogus, ronly, removable, consin, consout, input, output
};

/* A prom device tree component. */
struct linux_component {
	enum linux_devclass     class;   /* node class */
	enum linux_devtypes     type;    /* node type */
	enum linux_identifier   iflags;  /* node flags */
	unsigned short          vers;    /* node version */
	unsigned short          rev;     /* node revision */
	unsigned long           key;     /* completely magic */
	unsigned long           amask;   /* XXX affinity mask??? */
	unsigned long           cdsize;  /* size of configuration data */
	unsigned long           ilen;    /* length of string identifier */
	char                   *iname;   /* string identifier */
};
typedef struct linux_component pcomponent;

struct linux_sysid {
	char vend[8], prod[8];
};

/* ARCS prom memory descriptors. */
enum arcs_memtypes {
	arcs_eblock,  /* exception block */
	arcs_rvpage,  /* ARCS romvec page */
	arcs_fcontig, /* Contiguous and free */
	arcs_free,    /* Generic free memory */
	arcs_bmem,    /* Borken memory, don't use */
	arcs_prog,    /* A loaded program resides here */
	arcs_atmp,    /* ARCS temporary storage area, wish Sparc OpenBoot told this */
	arcs_aperm,   /* ARCS permanent storage... */
};

/* ARC has slightly different types than ARCS */
enum arc_memtypes {
	arc_eblock,  /* exception block */
	arc_rvpage,  /* romvec page */
	arc_free,    /* Generic free memory */
	arc_bmem,    /* Borken memory, don't use */
	arc_prog,    /* A loaded program resides here */
	arc_atmp,    /* temporary storage area */
	arc_aperm,   /* permanent storage */
	arc_fcontig, /* Contiguous and free */    
};

union linux_memtypes {
    enum arcs_memtypes arcs;
    enum arc_memtypes arc;
};

struct linux_mdesc {
        union linux_memtypes type;
	unsigned long base;
	unsigned long pages;
};

/* Time of day descriptor. */
struct linux_tinfo {
	unsigned short yr;
	unsigned short mnth;
	unsigned short day;
	unsigned short hr;
	unsigned short min;
	unsigned short sec;
	unsigned short msec;
};

/* ARCS virtual dirents. */
struct linux_vdirent {
	unsigned long namelen;
	unsigned char attr;
	char fname[32]; /* XXX imperical, should be a define */
};

/* Other stuff for files. */
enum linux_omode {
	rdonly, wronly, rdwr, wronly_creat, rdwr_creat,
	wronly_ssede, rdwr_ssede, dirent, dirent_creat
};

enum linux_seekmode {
	absolute, relative
};

enum linux_mountops {
	media_load, media_unload
};

/* This prom has a bolixed design. */
struct linux_bigint {
#ifdef __MIPSEL__
	unsigned long lo;
	long hi;
#else /* !(__MIPSEL__) */
	long hi;
	unsigned long lo;
#endif
};

struct linux_finfo {
	struct linux_bigint   begin;
	struct linux_bigint   end;
	struct linux_bigint   cur;
	enum linux_devtypes dtype;
	unsigned long         namelen;
	unsigned char         attr;
	char                  name[32]; /* XXX imperical, should be define */
};

struct linux_romvec {
	/* Load an executable image. */
	long (*load)(char *file, unsigned long end,
		     unsigned long *start_pc,
		     unsigned long *end_addr);

	/* Invoke a standalong image. */
	long (*invoke)(unsigned long startpc, unsigned long sp,
		       long argc, char **argv, char **envp);

	/* Load and begin execution of a standalong image. */
	long (*exec)(char *file, long argc, char **argv, char **envp);

	void (*halt)(void) __attribute__((noreturn)); 	/* Halt the machine. */
	void (*pdown)(void) __attribute__((noreturn));    /* Power down the machine. */
	void (*restart)(void) __attribute__((noreturn));  /* XXX soft reset??? */
	void (*reboot)(void) __attribute__((noreturn));   /* Reboot the machine. */
	void (*imode)(void) __attribute__((noreturn));    /* Enter PROM interactive mode. */
	int _unused1; /* padding */

	/* PROM device tree interface. */
	pcomponent *(*next_component)(pcomponent *this);
	pcomponent *(*child_component)(pcomponent *this);
	pcomponent *(*parent_component)(pcomponent *this);
	long (*component_data)(void *opaque_data, pcomponent *this);
	pcomponent *(*child_add)(pcomponent *this,
				 pcomponent *tmp,
				 void *opaque_data);
	long (*comp_del)(pcomponent *this);
	pcomponent *(*component_by_path)(char *file);

	/* Misc. stuff. */
	long (*cfg_save)(void);
	struct linux_sysid *(*get_sysid)(void);

	/* Probing for memory. */
	struct linux_mdesc *(*get_mdesc)(struct linux_mdesc *curr);
	long _unused2; /* padding */

	struct linux_tinfo *(*get_tinfo)(void);
	unsigned long (*get_rtime)(void);

	/* File type operations. */
	long (*get_vdirent)(unsigned long fd, struct linux_vdirent *entry,
			    unsigned long num, unsigned long *count);
	long (*open)(char *file, enum linux_omode mode, unsigned long *fd);
	long (*close)(unsigned long fd);
	long (*read)(unsigned long fd, void *buffer, unsigned long num,
		     unsigned long *count);
	long (*get_rstatus)(unsigned long fd);
	long (*write)(unsigned long fd, void *buffer, unsigned long num,
		      unsigned long *count);
	long (*seek)(unsigned long fd, struct linux_bigint *offset,
		     enum linux_seekmode smode);
	long (*mount)(char *file, enum linux_mountops op);

	/* Dealing with firmware environment variables. */
	PCHAR (*get_evar)(CHAR *name);
	LONG (*set_evar)(PCHAR name, PCHAR value);

	long (*get_finfo)(unsigned long fd, struct linux_finfo *buf);
	long (*set_finfo)(unsigned long fd, unsigned long flags,
			  unsigned long mask);

	/* Miscellaneous. */
	void (*cache_flush)(void);
};

/* The SGI ARCS parameter block is in a fixed location for standalone
 * programs to access PROM facilities easily.
 */
struct linux_promblock {
	long                 magic;       /* magic cookie */
#define PROMBLOCK_MAGIC      0x53435241

	unsigned long        len;          /* length of parm block */
	unsigned short       ver;          /* ARCS firmware version */
	unsigned short       rev;          /* ARCS firmware revision */
	long                *rs_block;     /* Restart block. */
	long                *dbg_block;    /* Debug block. */
	long                *gevect;       /* XXX General vector??? */
	long                *utlbvect;     /* XXX UTLB vector??? */
	unsigned long        rveclen;      /* Size of romvec struct. */
	struct linux_romvec *romvec;       /* Function interface. */
	unsigned long        pveclen;      /* Length of private vector. */
	long                *pvector;      /* Private vector. */
	long                 adap_cnt;     /* Adapter count. */
	long                 adap_typ0;    /* First adapter type. */
	long                 adap_vcnt0;   /* Adapter 0 vector count. */
	long                *adap_vector;  /* Adapter 0 vector ptr. */
	long                 adap_typ1;    /* Second adapter type. */
	long                 adap_vcnt1;   /* Adapter 1 vector count. */
	long                *adap_vector1; /* Adapter 1 vector ptr. */
	/* More adapter vectors go here... */
};

#define PROMBLOCK ((struct linux_promblock *)0xA0001000UL)
#define ROMVECTOR ((PROMBLOCK)->romvec)

/* Cache layout parameter block. */
union linux_cache_key {
	struct param {
#ifdef __MIPSEL__
		unsigned short size;
		unsigned char lsize;
		unsigned char bsize;
#else /* !(__MIPSEL__) */
		unsigned char bsize;
		unsigned char lsize;
		unsigned short size;
#endif
	} info;
	unsigned long allinfo;
};

/* Configuration data. */
struct linux_cdata {
	char *name;
	int mlen;
	enum linux_devtypes type;
};

/* Common SGI ARCS firmware file descriptors. */
#define SGIPROM_STDIN     0
#define SGIPROM_STDOUT    1

/* Common SGI ARCS firmware file types. */
#define SGIPROM_ROFILE    0x01  /* read-only file */
#define SGIPROM_HFILE     0x02  /* hidden file */
#define SGIPROM_SFILE     0x04  /* System file */
#define SGIPROM_AFILE     0x08  /* Archive file */
#define SGIPROM_DFILE     0x10  /* Directory file */
#define SGIPROM_DELFILE   0x20  /* Deleted file */

/* SGI ARCS boot record information. */
struct sgi_partition {
	unsigned char flag;
#define SGIPART_UNUSED 0x00
#define SGIPART_ACTIVE 0x80

	unsigned char shead, ssect, scyl; /* unused */
	unsigned char systype; /* OS type, Irix or NT */
	unsigned char ehead, esect, ecyl; /* unused */
	unsigned char rsect0, rsect1, rsect2, rsect3;
	unsigned char tsect0, tsect1, tsect2, tsect3;
};

#define SGIBBLOCK_MAGIC   0xaa55
#define SGIBBLOCK_MAXPART 0x0004

struct sgi_bootblock {
	unsigned char _unused[446];
	struct sgi_partition partitions[SGIBBLOCK_MAXPART];
	unsigned short magic;
};

/* BIOS parameter block. */
struct sgi_bparm_block {
	unsigned short bytes_sect;    /* bytes per sector */
	unsigned char  sect_clust;    /* sectors per cluster */
	unsigned short sect_resv;     /* reserved sectors */
	unsigned char  nfats;         /* # of allocation tables */
	unsigned short nroot_dirents; /* # of root directory entries */
	unsigned short sect_volume;   /* sectors in volume */
	unsigned char  media_type;    /* media descriptor */
	unsigned short sect_fat;      /* sectors per allocation table */
	unsigned short sect_track;    /* sectors per track */
	unsigned short nheads;        /* # of heads */
	unsigned short nhsects;       /* # of hidden sectors */
};

struct sgi_bsector {
	unsigned char   jmpinfo[3];
	unsigned char   manuf_name[8];
	struct sgi_bparm_block info;
};

/* Debugging block used with SGI symmon symbolic debugger. */
#define SMB_DEBUG_MAGIC   0xfeeddead
struct linux_smonblock {
	unsigned long   magic;
	void            (*handler)(void);  /* Breakpoint routine. */
	unsigned long   dtable_base;       /* Base addr of dbg table. */
	int             (*printf)(const char *fmt, ...);
	unsigned long   btable_base;       /* Breakpoint table. */
	unsigned long   mpflushreqs;       /* SMP cache flush request list. */
	unsigned long   ntab;              /* Name table. */
	unsigned long   stab;              /* Symbol table. */
	int             smax;              /* Max # of symbols. */
};

#endif /* _ASM_SGIARCS_H */
