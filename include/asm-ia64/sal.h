#ifndef _ASM_IA64_SAL_H
#define _ASM_IA64_SAL_H

/*
 * System Abstraction Layer definitions.
 *
 * This is based on version 2.5 of the manual "IA-64 System
 * Abstraction Layer".
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Srinivasa Prasad Thirumalachar <sprasad@sprasad.engr.sgi.com>
 *
 * 99/09/29 davidm	Updated for SAL 2.6.
 * 00/03/29 cfleck      Updated SAL Error Logging info for processor (SAL 2.6) 
 *                      (plus examples of platform error info structures from smariset @ Intel)
 */

#include <linux/config.h>
#include <linux/spinlock.h>

#include <asm/pal.h>
#include <asm/system.h>

extern spinlock_t sal_lock;

/* SAL spec _requires_ eight args for each call. */
#define __SAL_CALL(result,a0,a1,a2,a3,a4,a5,a6,a7)	\
	result = (*ia64_sal)(a0,a1,a2,a3,a4,a5,a6,a7)

#ifdef CONFIG_SMP
# define SAL_CALL(result,args...) do {		\
	  spin_lock(&sal_lock);			\
	  __SAL_CALL(result,args);		\
	  spin_unlock(&sal_lock);		\
} while (0)
#else
# define SAL_CALL(result,args...)	__SAL_CALL(result,args)
#endif

#define SAL_SET_VECTORS			0x01000000
#define SAL_GET_STATE_INFO		0x01000001
#define SAL_GET_STATE_INFO_SIZE		0x01000002
#define SAL_CLEAR_STATE_INFO		0x01000003
#define SAL_MC_RENDEZ			0x01000004
#define SAL_MC_SET_PARAMS		0x01000005
#define SAL_REGISTER_PHYSICAL_ADDR	0x01000006

#define SAL_CACHE_FLUSH			0x01000008
#define SAL_CACHE_INIT			0x01000009
#define SAL_PCI_CONFIG_READ		0x01000010
#define SAL_PCI_CONFIG_WRITE		0x01000011
#define SAL_FREQ_BASE			0x01000012

#define SAL_UPDATE_PAL			0x01000020

struct ia64_sal_retval {
	/*
	 * A zero status value indicates call completed without error.
	 * A negative status value indicates reason of call failure.
	 * A positive status value indicates success but an
	 * informational value should be printed (e.g., "reboot for
	 * change to take effect").
	 */
	s64 status;
	u64 v0;
	u64 v1;
	u64 v2;
};

typedef struct ia64_sal_retval (*ia64_sal_handler) (u64, ...);

enum {
	SAL_FREQ_BASE_PLATFORM = 0,
	SAL_FREQ_BASE_INTERVAL_TIMER = 1,
	SAL_FREQ_BASE_REALTIME_CLOCK = 2
};

/*
 * The SAL system table is followed by a variable number of variable
 * length descriptors.  The structure of these descriptors follows
 * below.
 * The defininition follows SAL specs from July 2000
 */
struct ia64_sal_systab {
	u8 signature[4];	/* should be "SST_" */
	u32 size;		/* size of this table in bytes */
	u8 sal_rev_minor;
	u8 sal_rev_major;
	u16 entry_count;	/* # of entries in variable portion */
	u8 checksum;
	u8 reserved1[7];
	u8 sal_a_rev_minor;
	u8 sal_a_rev_major;
	u8 sal_b_rev_minor;
	u8 sal_b_rev_major;
	/* oem_id & product_id: terminating NUL is missing if string is exactly 32 bytes long. */
	u8 oem_id[32];
	u8 product_id[32];	/* ASCII product id  */
	u8 reserved2[8];
};

enum sal_systab_entry_type {
	SAL_DESC_ENTRY_POINT = 0,
	SAL_DESC_MEMORY = 1,
	SAL_DESC_PLATFORM_FEATURE = 2,
	SAL_DESC_TR = 3,
	SAL_DESC_PTC = 4,
	SAL_DESC_AP_WAKEUP = 5
};

/*
 * Entry type:	Size:
 *	0	48
 *	1	32
 *	2	16
 *	3	32
 *	4	16
 *	5	16
 */
#define SAL_DESC_SIZE(type)	"\060\040\020\040\020\020"[(unsigned) type]

typedef struct ia64_sal_desc_entry_point {
	u8 type;
	u8 reserved1[7];
	u64 pal_proc;
	u64 sal_proc;
	u64 gp;
	u8 reserved2[16];
}ia64_sal_desc_entry_point_t;

typedef struct ia64_sal_desc_memory {
	u8 type;
	u8 used_by_sal;	/* needs to be mapped for SAL? */
	u8 mem_attr;		/* current memory attribute setting */
	u8 access_rights;	/* access rights set up by SAL */
	u8 mem_attr_mask;	/* mask of supported memory attributes */
	u8 reserved1;
	u8 mem_type;		/* memory type */
	u8 mem_usage;		/* memory usage */
	u64 addr;		/* physical address of memory */
	u32 length;	/* length (multiple of 4KB pages) */
	u32 reserved2;
	u8 oem_reserved[8];
} ia64_sal_desc_memory_t;

#define IA64_SAL_PLATFORM_FEATURE_BUS_LOCK		(1 << 0)
#define IA64_SAL_PLATFORM_FEATURE_IRQ_REDIR_HINT	(1 << 1)
#define IA64_SAL_PLATFORM_FEATURE_IPI_REDIR_HINT	(1 << 2)

typedef struct ia64_sal_desc_platform_feature {
	u8 type;
	u8 feature_mask;
	u8 reserved1[14];
} ia64_sal_desc_platform_feature_t;

typedef struct ia64_sal_desc_tr {
	u8 type;
	u8 tr_type;		/* 0 == instruction, 1 == data */
	u8 regnum;		/* translation register number */
	u8 reserved1[5];
	u64 addr;		/* virtual address of area covered */
	u64 page_size;		/* encoded page size */
	u8 reserved2[8];
} ia64_sal_desc_tr_t;

typedef struct ia64_sal_desc_ptc {
	u8 type;
	u8 reserved1[3];
	u32 num_domains;	/* # of coherence domains */
	u64 domain_info;	/* physical address of domain info table */
} ia64_sal_desc_ptc_t;

typedef struct ia64_sal_ptc_domain_info {
	u64 proc_count;		/* number of processors in domain */
	u64 proc_list;		/* physical address of LID array */
} ia64_sal_ptc_domain_info_t;

typedef struct ia64_sal_ptc_domain_proc_entry {
	u64 reserved : 16;
	u64 eid : 8;		/* eid of processor */
	u64 id  : 8;		/* id of processor */
	u64 ignored : 32;
} ia64_sal_ptc_domain_proc_entry_t;


#define IA64_SAL_AP_EXTERNAL_INT 0

typedef struct ia64_sal_desc_ap_wakeup {
	u8 type;
	u8 mechanism;		/* 0 == external interrupt */
	u8 reserved1[6];
	u64 vector;		/* interrupt vector in range 0x10-0xff */
} ia64_sal_desc_ap_wakeup_t ;

extern ia64_sal_handler ia64_sal;
extern struct ia64_sal_desc_ptc *ia64_ptc_domain_info;

extern const char *ia64_sal_strerror (long status);
extern void ia64_sal_init (struct ia64_sal_systab *sal_systab);

/* SAL information type encodings */
enum {
	SAL_INFO_TYPE_MCA	=		0,	/* Machine check abort information */
        SAL_INFO_TYPE_INIT	=		1,	/* Init information */
        SAL_INFO_TYPE_CMC	=		2 	/* Corrected machine check information */
};

/* Sub information type encodings */
enum {
        SAL_SUB_INFO_TYPE_PROCESSOR	=	0,	/* Processor information */
        SAL_SUB_INFO_TYPE_PLATFORM	=	1	/* Platform information */
};

/* Encodings for machine check parameter types */
enum {
        SAL_MC_PARAM_RENDEZ_INT		=	1,	/* Rendezevous interrupt */
        SAL_MC_PARAM_RENDEZ_WAKEUP	=	2	/* Wakeup */
};

/* Encodings for rendezvous mechanisms */
enum {
        SAL_MC_PARAM_MECHANISM_INT	=	1,	/* Use interrupt */
        SAL_MC_PARAM_MECHANISM_MEM	=	2	/* Use memory synchronization variable*/
};

/* Encodings for vectors which can be registered by the OS with SAL */
enum {
	SAL_VECTOR_OS_MCA		= 0,
	SAL_VECTOR_OS_INIT		= 1,
	SAL_VECTOR_OS_BOOT_RENDEZ	= 2
};

/* Definition of the SAL Error Log from the SAL spec */

/* Definition of timestamp according to SAL spec for logging purposes */

typedef struct sal_log_timestamp {
	u8 slh_century;		/* Century (19, 20, 21, ...) */
	u8 slh_year;		/* Year (00..99) */
	u8 slh_month;		/* Month (1..12) */
	u8 slh_day;		/* Day (1..31) */
	u8 slh_reserved;					
	u8 slh_hour;		/* Hour (0..23)	*/
	u8 slh_minute;		/* Minute (0..59) */
	u8 slh_second;		/* Second (0..59) */
} sal_log_timestamp_t;


#define MAX_CACHE_ERRORS			6
#define MAX_TLB_ERRORS				6
#define MAX_BUS_ERRORS				1

typedef struct sal_log_processor_info {
	struct	{
		u64 slpi_psi		: 1,
		    slpi_cache_check: MAX_CACHE_ERRORS,
		    slpi_tlb_check	: MAX_TLB_ERRORS,
		    slpi_bus_check	: MAX_BUS_ERRORS,
		    slpi_reserved2	: (31 - (MAX_TLB_ERRORS + MAX_CACHE_ERRORS
		    			 + MAX_BUS_ERRORS)),
		    slpi_minstate	: 1,
		    slpi_bank1_gr	: 1,
		    slpi_br		: 1,
		    slpi_cr		: 1,
		    slpi_ar		: 1,
		    slpi_rr		: 1,
		    slpi_fr		: 1,
		    slpi_reserved1	: 25;
	} slpi_valid;

	pal_processor_state_info_t slpi_processor_state_info;

	struct {
		pal_cache_check_info_t slpi_cache_check;
		u64 slpi_target_address;
	} slpi_cache_check_info[MAX_CACHE_ERRORS];
		
	pal_tlb_check_info_t slpi_tlb_check_info[MAX_TLB_ERRORS];

	struct {
		pal_bus_check_info_t slpi_bus_check;
		u64 slpi_requestor_addr;	
		u64 slpi_responder_addr;	
		u64 slpi_target_addr;
	} slpi_bus_check_info[MAX_BUS_ERRORS];

	pal_min_state_area_t slpi_min_state_area;
	u64 slpi_br[8];
	u64 slpi_cr[128];
	u64 slpi_ar[128];
	u64 slpi_rr[8];
	u64 slpi_fr[128];
} sal_log_processor_info_t;

/* platform error log structures */
typedef struct platerr_logheader {
	u64 nextlog;		/* next log offset if present */
	u64 loglength;		/* log length */
	u64 logsubtype;		/* log subtype memory/bus/component */
	u64 eseverity;		/* error severity */
} ehdr_t;

typedef struct sysmem_errlog {
	ehdr_t lhdr;		/* header */
	u64 vflag;		/* valid bits for each field in the log */
	u64 addr;		/* memory address */
	u64 data;		/* memory data */
	u64 cmd;		/* command bus value if any */
	u64 ctrl;		/* control bus value if any */
	u64 addrsyndrome;	/* memory address ecc/parity syndrome bits */
	u64 datasyndrome;	/* data ecc/parity syndrome */
	u64 cacheinfo;		/* platform cache info as defined in pal spec. table 7-34 */
} merrlog_t;

typedef struct sysbus_errlog {
	ehdr_t lhdr;		/* linkded list header */
	u64 vflag;		/* valid bits for each field in the log */
	u64 busnum;		/* bus number in error */
	u64 reqaddr;		/* requestor address */
	u64 resaddr;		/* responder address */
	u64 taraddr;		/* target address */
	u64 data;		/* requester r/w data */
	u64 cmd;		/* bus commands */
	u64 ctrl;		/* bus controls (be# &-0) */
	u64 addrsyndrome;	/* addr bus ecc/parity bits */
	u64 datasyndrome;	/* data bus ecc/parity bits */
	u64 cmdsyndrome;	/* command bus ecc/parity bits */
	u64 ctrlsyndrome;	/* control bus ecc/parity bits */
} berrlog_t;

/* platform error log structures */
typedef struct syserr_chdr {	/* one header per component */
	u64 busnum;		/* bus number on which the component resides */
	u64 devnum;		/* same as device select */
	u64 funcid;		/* function id of the device */
	u64 devid;		/* pci device id */
	u64 classcode;		/* pci class code for the device */
	u64 cmdreg;		/* pci command reg value */
	u64 statreg;		/* pci status reg value */
} chdr_t;

typedef struct cfginfo {
	u64 cfgaddr;
	u64 cfgval;
} cfginfo_t;

typedef struct sys_comperr {	/* per component */
	ehdr_t lhdr;		/* linked list header */
	u64 vflag;		/* valid bits for each field in the log */
	chdr_t scomphdr;	
	u64 numregpair;		/* number of reg addr/value pairs */
	cfginfo_t cfginfo;
} cerrlog_t;

typedef struct sel_records {
	ehdr_t lhdr;
	u64 seldata;
} isel_t;

typedef struct plat_errlog {
	u64 mbcsvalid;		/* valid bits for each type of log */
	merrlog_t smemerrlog;	/* platform memory error logs */
	berrlog_t sbuserrlog;	/* platform bus error logs */
	cerrlog_t scomperrlog;	/* platform chipset error logs */
	isel_t selrecord;	/* ipmi sel record */
} platforminfo_t;

/* over all log structure (processor+platform) */

typedef union udev_specific_log {
	sal_log_processor_info_t proclog;
	platforminfo_t platlog;
} devicelog_t;


#define sal_log_processor_info_psi_valid		slpi_valid.spli_psi
#define sal_log_processor_info_cache_check_valid	slpi_valid.spli_cache_check
#define sal_log_processor_info_tlb_check_valid		slpi_valid.spli_tlb_check
#define sal_log_processor_info_bus_check_valid		slpi_valid.spli_bus_check
#define sal_log_processor_info_minstate_valid		slpi_valid.spli_minstate
#define sal_log_processor_info_bank1_gr_valid		slpi_valid.slpi_bank1_gr
#define sal_log_processor_info_br_valid			slpi_valid.slpi_br
#define sal_log_processor_info_cr_valid			slpi_valid.slpi_cr
#define sal_log_processor_info_ar_valid			slpi_valid.slpi_ar
#define sal_log_processor_info_rr_valid			slpi_valid.slpi_rr
#define sal_log_processor_info_fr_valid			slpi_valid.slpi_fr

typedef struct sal_log_header {
	u64 slh_next_log;	/* Offset of the next log from the beginning of this structure */
	u32 slh_log_len;	/* Length of this error log in bytes */
	u16 slh_log_type;	/* Type of log (0 - cpu ,1 - platform) */
	u16 slh_log_sub_type;	/* SGI specific sub type */
	sal_log_timestamp_t slh_log_timestamp;	/* Timestamp */
} sal_log_header_t;

/* SAL PSI log structure */
typedef struct psilog {
	sal_log_header_t sal_elog_header;
	devicelog_t devlog;
} ia64_psilog_t;

/*
 * Now define a couple of inline functions for improved type checking
 * and convenience.
 */
static inline long
ia64_sal_freq_base (unsigned long which, unsigned long *ticks_per_second,
		    unsigned long *drift_info)
{
	struct ia64_sal_retval isrv;

	SAL_CALL(isrv, SAL_FREQ_BASE, which, 0, 0, 0, 0, 0, 0);
	*ticks_per_second = isrv.v0;
	*drift_info = isrv.v1;
	return isrv.status;
}

/* Flush all the processor and platform level instruction and/or data caches */
static inline s64
ia64_sal_cache_flush (u64 cache_type)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_CACHE_FLUSH, cache_type, 0, 0, 0, 0, 0, 0);
	return isrv.status;
}


	
/* Initialize all the processor and platform level instruction and data caches */
static inline s64
ia64_sal_cache_init (void)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_CACHE_INIT, 0, 0, 0, 0, 0, 0, 0);
	return isrv.status;
}

/* Clear the processor and platform information logged by SAL with respect to the 
 * machine state at the time of MCA's, INITs or CMCs 
 */
static inline s64
ia64_sal_clear_state_info (u64 sal_info_type, u64 sal_info_sub_type)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_CLEAR_STATE_INFO, sal_info_type, sal_info_sub_type,
	         0, 0, 0, 0, 0);
	return isrv.status;
}


/* Get the processor and platform information logged by SAL with respect to the machine
 * state at the time of the MCAs, INITs or CMCs.
 */
static inline u64
ia64_sal_get_state_info (u64 sal_info_type, u64 sal_info_sub_type, u64 *sal_info)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_GET_STATE_INFO, sal_info_type, sal_info_sub_type,
	         sal_info, 0, 0, 0, 0);
	if (isrv.status)
		return 0;
	return isrv.v0;
}	
/* Get the maximum size of the information logged by SAL with respect to the machine 
 * state at the time of MCAs, INITs or CMCs
 */
static inline u64
ia64_sal_get_state_info_size (u64 sal_info_type, u64 sal_info_sub_type)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_GET_STATE_INFO_SIZE, sal_info_type, sal_info_sub_type,
	         0, 0, 0, 0, 0);
	if (isrv.status)
		return 0;
	return isrv.v0;
}

/* Causes the processor to go into a spin loop within SAL where SAL awaits a wakeup
 * from the monarch processor.
 */
static inline s64
ia64_sal_mc_rendez (void)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_MC_RENDEZ, 0, 0, 0, 0, 0, 0, 0);
	return isrv.status;
}

/* Allow the OS to specify the interrupt number to be used by SAL to interrupt OS during
 * the machine check rendezvous sequence as well as the mechanism to wake up the 
 * non-monarch processor at the end of machine check processing.
 */
static inline s64
ia64_sal_mc_set_params (u64 param_type, u64 i_or_m, u64 i_or_m_val, u64 timeout)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_MC_SET_PARAMS, param_type, i_or_m, i_or_m_val, timeout,
	         0, 0, 0);
	return isrv.status;
}

/* Read from PCI configuration space */
static inline s64
ia64_sal_pci_config_read (u64 pci_config_addr, u64 size, u64 *value)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_PCI_CONFIG_READ, pci_config_addr, size, 0, 0, 0, 0, 0);
	if (value)
		*value = isrv.v0;
	return isrv.status;
}

/* Write to PCI configuration space */
static inline s64
ia64_sal_pci_config_write (u64 pci_config_addr, u64 size, u64 value)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_PCI_CONFIG_WRITE, pci_config_addr, size, value,
	         0, 0, 0, 0);
	return isrv.status;
}

/*
 * Register physical addresses of locations needed by SAL when SAL
 * procedures are invoked in virtual mode.
 */
static inline s64
ia64_sal_register_physical_addr (u64 phys_entry, u64 phys_addr)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_REGISTER_PHYSICAL_ADDR, phys_entry, phys_addr,
	         0, 0, 0, 0, 0);
	return isrv.status;
}

/* Register software dependent code locations within SAL. These locations are handlers
 * or entry points where SAL will pass control for the specified event. These event
 * handlers are for the bott rendezvous, MCAs and INIT scenarios.
 */
static inline s64
ia64_sal_set_vectors (u64 vector_type,
		      u64 handler_addr1, u64 gp1, u64 handler_len1,
		      u64 handler_addr2, u64 gp2, u64 handler_len2)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_SET_VECTORS, vector_type,
			handler_addr1, gp1, handler_len1,
			handler_addr2, gp2, handler_len2);			

	return isrv.status;
}		
/* Update the contents of PAL block in the non-volatile storage device */
static inline s64
ia64_sal_update_pal (u64 param_buf, u64 scratch_buf, u64 scratch_buf_size,
		     u64 *error_code, u64 *scratch_buf_size_needed)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_UPDATE_PAL, param_buf, scratch_buf, scratch_buf_size,
	         0, 0, 0, 0);
	if (error_code)
		*error_code = isrv.v0;
	if (scratch_buf_size_needed)
		*scratch_buf_size_needed = isrv.v1;
	return isrv.status;
}

#endif /* _ASM_IA64_PAL_H */
