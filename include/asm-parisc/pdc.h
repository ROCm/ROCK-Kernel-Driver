#ifndef _PARISC_PDC_H
#define _PARISC_PDC_H

/*
    PDC entry points...
*/

#define PDC_POW_FAIL	1		/* perform a power-fail		*/
#define PDC_POW_FAIL_PREPARE	0	/* prepare for powerfail	*/

#define PDC_CHASSIS	2		/* PDC-chassis functions	*/
#define PDC_CHASSIS_DISP	0	/* update chassis display	*/
#define PDC_CHASSIS_WARN	1	/* return chassis warnings	*/
#define PDC_CHASSIS_DISPWARN	2	/* update&return chassis status */
#define PDC_RETURN_CHASSIS_INFO 128	/* HVERSION dependend: return chassis LED/LCD info  */

#define PDC_PIM         3               /* Get PIM data                 */
#define PDC_PIM_HPMC            0       /* Transfer HPMC data           */
#define PDC_PIM_RETURN_SIZE     1       /* Get Max buffer needed for PIM*/
#define PDC_PIM_LPMC            2       /* Transfer HPMC data           */
#define PDC_PIM_SOFT_BOOT       3       /* Transfer Soft Boot data      */
#define PDC_PIM_TOC             4       /* Transfer TOC data            */

#define PDC_MODEL	4		/* PDC model information call	*/
#define PDC_MODEL_INFO		0	/* returns information 		*/
#define PDC_MODEL_BOOTID	1	/* set the BOOT_ID		*/
#define PDC_MODEL_VERSIONS	2	/* returns cpu-internal versions*/
#define PDC_MODEL_SYSMODEL	3	/* return system model info	*/
#define PDC_MODEL_ENSPEC	4	/* ??? */
#define PDC_MODEL_DISPEC	5	/* ??? */
#define PDC_MODEL_CPU_ID	6	/* returns cpu-id (only newer machines!) */
#define PDC_MODEL_CAPABILITIES	7	/* returns OS32/OS64-flags	*/
#define PDC_MODEL_GET_BOOT__OP	8	/* returns boot test options	*/
#define PDC_MODEL_SET_BOOT__OP	9	/* set boot test options	*/

#define PDC_CACHE	5		/* return/set cache (& TLB) info*/
#define PDC_CACHE_INFO		0	/* returns information 		*/
#define PDC_CACHE_SET_COH	1	/* set coherence state		*/
#define PDC_CACHE_RET_SPID	2	/* returns space-ID bits	*/

#define PDC_HPA	 6       	/* return HPA of processor */
#define PDC_HPA_PROCESSOR       0
#define PDC_HPA_MODULES	 1

#define PDC_IODC	8       /* talk to IODC */
#define PDC_IODC_READ	   0       /* read IODC entry point */
/*      PDC_IODC_RI_*		      INDEX parameter of PDC_IODC_READ   */
#define PDC_IODC_RI_DATA_BYTES	0	/* IODC Data Bytes		    */
/*				1, 2	   obsolete - HVERSION dependent      */
#define PDC_IODC_RI_INIT	3	/* Initialize module		  */
#define PDC_IODC_RI_IO		4	/* Module input/output		*/
#define PDC_IODC_RI_SPA		5	/* Module input/output		*/
#define PDC_IODC_RI_CONFIG	6	/* Module input/output		*/
/*				7	  obsolete - HVERSION dependent      */
#define PDC_IODC_RI_TEST	8	/* Module input/output		*/
#define PDC_IODC_RI_TLB		9	/* Module input/output		*/
#define PDC_IODC_NINIT	  2       /* non-destructive init */
#define PDC_IODC_DINIT	  3       /* destructive init */
#define PDC_IODC_MEMERR	 4       /* check for memory errors */
#define PDC_IODC_INDEX_DATA     0       /* get first 16 bytes from mod IODC */
#define PDC_IODC_BUS_ERROR      -4      /* bus error return value */
#define PDC_IODC_INVALID_INDEX  -5      /* invalid index return value */
#define PDC_IODC_COUNT	  -6      /* count is too small */

#define	PDC_TOD		9		/* time-of-day clock (TOD) */
#define	PDC_TOD_READ		0	/* read TOD  */
#define	PDC_TOD_WRITE		1	/* write TOD */
#define	PDC_TOD_ITIMER		2	/* calibrate Interval Timer (CR16) */

#define PDC_ADD_VALID	12    		/* Memory validation PDC call */
#define PDC_ADD_VALID_VERIFY  0    	/* Make PDC_ADD_VALID verify region */

#define PDC_INSTR	15		/* get instr to invoke PDCE_CHECK() */

#define PDC_BLOCK_TLB	18		/* manage hardware block-TLB	*/
#define PDC_BTLB_INFO		0	/* returns parameter 		*/
#define PDC_BTLB_INSERT		1	/* insert BTLB entry		*/
#define PDC_BTLB_PURGE		2	/* purge BTLB entries 		*/
#define PDC_BTLB_PURGE_ALL	3	/* purge all BTLB entries 	*/

#define PDC_TLB		19		/* manage hardware TLB miss handling */
#define PDC_TLB_INFO		0	/* returns parameter 		*/
#define PDC_TLB_SETUP		1	/* set up miss handling 	*/

#define PDC_SYSTEM_MAP	22		/* find system modules */
#define PDC_FIND_MODULE 	0


/* HVERSION dependent */

#define PDC_IO			135	/* log error info, reset IO system  */

#define PDC_BROADCAST_RESET	136	/* reset all processors	     */
#define PDC_DO_RESET            0UL	/* option: perform a broadcast reset */
#define PDC_DO_FIRM_TEST_RESET  1UL	/* Do broadcast reset with bitmap */
#define PDC_BR_RECONFIGURATION  2UL	/* reset w/reconfiguration */
#define PDC_FIRM_TEST_MAGIC 	0xab9ec36fUL    /* for this reboot only */

#define PDC_LAN_STATION_ID      138     /* Hversion dependent mechanism for */
#define PDC_LAN_STATION_ID_READ 0       /* getting the lan station address  */

#define	PDC_LAN_STATION_ID_SIZE	6

/* Legacy PDC definitions for same stuff */
#define PDC_PCI_INDEX		   147UL
#define PDC_PCI_GET_INT_TBL_SIZE	13UL
#define PDC_PCI_GET_INT_TBL	     14UL

/* generic error codes returned by all PDC-functions */

#define PDC_WARN	    3  /* Call completed with a warning */
#define PDC_REQ_ERR_1       2  /* See above */
#define PDC_REQ_ERR_0       1  /* Call would generate a requestor error */
#define PDC_OK	      0  /* Call completed successfully */
#define PDC_BAD_PROC	   -1  /* Called non-existant procedure */
#define PDC_BAD_OPTION     -2  /* Called with non-existant option */
#define PDC_ERROR	  -3  /* Call could not complete without an error */
#define PDC_INVALID_ARG   -10  /* Called with an invalid argument */
#define PDC_BUS_POW_WARN  -12  /* Call could not complete in allowed power budget */


/* The following are from the HPUX .h files, and are just for
compatibility */

#define PDC_RET_OK       0L	/* Call completed successfully */
#define PDC_RET_NE_PROC -1L	/* Non-existent procedure */
#define PDC_RET_NE_OPT  -2L	/* non-existant option - arg1 */
#define PDC_RET_NE_MOD  -5L	/* Module not found */
#define PDC_RET_NE_CELL_MOD -7L	/* Cell module not found */
#define PDC_RET_INV_ARG	-10L	/* Invalid argument */
#define PDC_RET_NOT_NARROW -17L /* Narrow mode not supported */


/* Error codes for PDC_ADD_VALID */

#define PDC_ADD_VALID_WARN	    3  /* Call completed with a warning */
#define PDC_ADD_VALID_REQ_ERR_1       2  /* See above */
#define PDC_ADD_VALID_REQ_ERR_0       1  /* Call would generate a requestor error */
#define PDC_ADD_VALID_OK	      0  /* Call completed successfully */
#define PDC_ADD_VALID_BAD_OPTION     -2  /* Called with non-existant option */
#define PDC_ADD_VALID_ERROR	  -3  /* Call could not complete without an error */
#define PDC_ADD_VALID_INVALID_ARG   -10  /* Called with an invalid argument */
#define PDC_ADD_VALID_BUS_POW_WARN  -12  /* Call could not complete in allowed power budget */

/* The PDC_MEM_MAP calls */

#define PDC_MEM_MAP	    128
#define PDC_MEM_MAP_HPA		0

/* constants for OS (NVM...) */
#define OS_ID_NONE	0
#define OS_ID_HPUX	1
#define OS_ID_MPEXL	2
#define OS_ID_OSF	3
#define OS_ID_LINUX	OS_ID_HPUX

/* constants for PDC_CHASSIS */
#define OSTAT_OFF		      0
#define OSTAT_FLT		      1 
#define OSTAT_TEST		     2
#define OSTAT_INIT		     3
#define OSTAT_SHUT		     4
#define OSTAT_WARN		     5
#define OSTAT_RUN		      6
#define OSTAT_ON		       7

#ifndef __ASSEMBLY__

#include <linux/types.h>

struct pdc_model {		/* for PDC_MODEL */
	unsigned long hversion;
	unsigned long sversion;
	unsigned long hw_id;
	unsigned long boot_id;
	unsigned long sw_id;
	unsigned long sw_cap;
	unsigned long arch_rev;
	unsigned long pot_key;
	unsigned long curr_key;
	unsigned long pad[32-9];
} __attribute__((aligned(8))) ;


#if 0
struct pdc_chassis_warn {		/* for PDC_CHASSIS */
	unsigned long warn;
	unsigned long pad[32-1];
} __attribute__((aligned(8))) ;
#endif

struct pdc_model_sysmodel {	/* for PDC_MODEL_SYSMODEL */
	unsigned long mod_len;
	unsigned long pad[32-1];
} __attribute__((aligned(8))) ;

struct pdc_model_cpuid	 {	/* for PDC_MODEL_CPU_ID */
	unsigned long cpuid;
	unsigned long pad[32-1];
} __attribute__((aligned(8))) ;

struct pdc_cache_cf {		/* for PDC_CACHE  (I/D-caches) */
    unsigned long
#ifdef __LP64__
		cc_padW:32,
#endif
		cc_alias:4,	/* alias boundaries for virtual adresses   */
		cc_block: 4,	/* to determine most efficient stride */
		cc_line	: 3,	/* maximum amount written back as a result of store (multiple of 16 bytes) */
		cc_pad0 : 2,	/* reserved */
		cc_wt	: 1,	/* 0 = WT-Dcache, 1 = WB-Dcache */
		cc_sh	: 2,	/* 0 = separate I/D-cache, else shared I/D-cache */
		cc_cst  : 3,	/* 0 = incoherent D-cache, 1=coherent D-cache */
		cc_pad1 : 5,	/* reserved */
		cc_assoc: 8;	/* associativity of I/D-cache */
};

struct pdc_tlb_cf {		/* for PDC_CACHE (I/D-TLB's) */
    unsigned long tc_pad0:12,	/* reserved */
#ifdef __LP64__
		tc_padW:32,
#endif
		tc_sh	: 2,	/* 0 = separate I/D-TLB, else shared I/D-TLB */
		tc_hv   : 1,	/* HV */
		tc_page : 1,	/* 0 = 2K page-size-machine, 1 = 4k page size */
		tc_cst  : 3,	/* 0 = incoherent operations, else coherent operations */
		tc_aid  : 5,	/* ITLB: width of access ids of processor (encoded!) */
		tc_pad1 : 8;	/* ITLB: width of space-registers (encoded) */
};

struct pdc_cache_info {		/* main-PDC_CACHE-structure (caches & TLB's) */
	/* I-cache */
	unsigned long	ic_size;	/* size in bytes */
	struct pdc_cache_cf ic_conf;	/* configuration */
	unsigned long	ic_base;	/* base-addr */
	unsigned long	ic_stride;
	unsigned long	ic_count;
	unsigned long	ic_loop;
	/* D-cache */
	unsigned long	dc_size;	/* size in bytes */
	struct pdc_cache_cf dc_conf;	/* configuration */
	unsigned long	dc_base;	/* base-addr */
	unsigned long	dc_stride;
	unsigned long	dc_count;
	unsigned long	dc_loop;
	/* Instruction-TLB */
	unsigned long	it_size;	/* number of entries in I-TLB */
	struct pdc_tlb_cf it_conf;	/* I-TLB-configuration */
	unsigned long	it_sp_base;
	unsigned long	it_sp_stride;
	unsigned long	it_sp_count;
	unsigned long	it_off_base;
	unsigned long	it_off_stride;
	unsigned long	it_off_count;
	unsigned long	it_loop;
	/* data-TLB */
	unsigned long	dt_size;	/* number of entries in D-TLB */
	struct pdc_tlb_cf dt_conf;	/* D-TLB-configuration */
	unsigned long	dt_sp_base;
	unsigned long	dt_sp_stride;
	unsigned long	dt_sp_count;
	unsigned long	dt_off_base;
	unsigned long	dt_off_stride;
	unsigned long	dt_off_count;
	unsigned long	dt_loop;
	/* padded to 32 entries... */
	unsigned long 	pad[32-30];
} __attribute__((aligned(8))) ;

struct pdc_hpa {      /* PDC_HPA */
	unsigned long	hpa;
	unsigned long	filler[31];
} __attribute__((aligned(8))) ;

#if 0
/* If you start using the next struct, you'll have to adjust it to
 * work with 64-bit firmware I think -PB
 */
struct pdc_iodc {     /* PDC_IODC */
	unsigned char   hversion_model;
	unsigned char 	hversion;
	unsigned char 	spa;
	unsigned char 	type;
	unsigned int	sversion_rev:4;
	unsigned int	sversion_model:19;
	unsigned int	sversion_opt:8;
	unsigned char	rev;
	unsigned char	dep;
	unsigned char	features;
	unsigned char	filler1;
	unsigned int	checksum:16;
	unsigned int	length:16;
	unsigned int    filler[15];
} __attribute__((aligned(8))) ;
#endif

#ifndef __LP64__
/* no BLTBs in pa2.0 processors */
struct pdc_btlb_info_range {
	__u8 res00;
	__u8 num_i;
	__u8 num_d;
	__u8 num_comb;
};

struct pdc_btlb_info {	/* PDC_BLOCK_TLB, return of PDC_BTLB_INFO */
	unsigned int min_size;	/* minimum size of BTLB in pages */
	unsigned int max_size;	/* maximum size of BTLB in pages */
	struct pdc_btlb_info_range fixed_range_info;
	struct pdc_btlb_info_range variable_range_info;
	unsigned int pad[32-4];
} __attribute__((aligned(8))) ;
#endif

struct pdc_tlb {		/* for PDC_TLB */
	unsigned long min_size;
	unsigned long max_size;
	unsigned long pad[32-2];
} __attribute__((aligned(8))) ;

struct pdc_system_map { /* PDC_SYTEM_MAP/FIND_MODULE */
	void * mod_addr;
	unsigned long mod_pgs;
	unsigned long add_addrs;
	unsigned long filler[29];
} __attribute__((aligned(8))) ;

/*
 * Device path specifications used by PDC.
 */
struct pdc_module_path {
	char  flags;	/* see bit definitions below */
	char  bc[6];	/* Bus Converter routing info to a specific */
			/* I/O adaptor (< 0 means none, > 63 resvd) */
	char  mod;	/* fixed field of specified module */
	unsigned int layers[6]; /* device-specific info (ctlr #, unit # ...) */
} __attribute__((aligned(8))) ;

#ifndef __LP64__
/* Probably needs 64-bit porting -PB */
struct pdc_memory_map {	/* PDC_MEMORY_MAP */
	unsigned int hpa;	/* mod's register set address */
	unsigned int more_pgs;	/* number of additional I/O pgs */
} __attribute__((aligned(8))) ;

struct pdc_lan_station_id {	/* PDC_LAN_STATION_ID */
	unsigned char addr[PDC_LAN_STATION_ID_SIZE];
	unsigned char pad0[2];
	int pad1[30];
};
#endif

struct pdc_tod {
	unsigned long tod_sec; 
	unsigned long tod_usec;
	long pad[30];
} __attribute__((aligned(8))) ;

/* architected results from PDC_PIM/transfer hpmc on a PA1.1 machine */

struct pdc_hpmc_pim_11 { /* PDC_PIM */
	__u32 gr[32];
	__u32 cr[32];
	__u32 sr[8];
	__u32 iasq_back;
	__u32 iaoq_back;
	__u32 check_type;
	__u32 cpu_state;
	__u32 rsvd1;
	__u32 cache_check;
	__u32 tlb_check;
	__u32 bus_check;
	__u32 assists_check;
	__u32 rsvd2;
	__u32 assist_state;
	__u32 responder_addr;
	__u32 requestor_addr;
	__u32 path_info;
	__u64 fr[32];
};

/*
 * architected results from PDC_PIM/transfer hpmc on a PA2.0 machine
 *
 * Note that PDC_PIM doesn't care whether or not wide mode was enabled
 * so the results are different on  PA1.1 vs. PA2.0 when in narrow mode.
 *
 * Note also that there are unarchitected results available, which
 * are hversion dependent. Do a "ser pim 0 hpmc" after rebooting, since
 * the firmware is probably the best way of printing hversion dependent
 * data.
 */

struct pdc_hpmc_pim_20 { /* PDC_PIM */
	__u64 gr[32];
	__u64 cr[32];
	__u64 sr[8];
	__u64 iasq_back;
	__u64 iaoq_back;
	__u32 check_type;
	__u32 cpu_state;
	__u32 cache_check;
	__u32 tlb_check;
	__u32 bus_check;
	__u32 assists_check;
	__u32 assist_state;
	__u32 path_info;
	__u64 responder_addr;
	__u64 requestor_addr;
	__u64 fr[32];
};

#endif /* __ASSEMBLY__ */

/* flags of the device_path (see below) */
#define	PF_AUTOBOOT	0x80
#define	PF_AUTOSEARCH	0x40
#define	PF_TIMER	0x0F

#ifndef __ASSEMBLY__

struct device_path {		/* page 1-69 */
	unsigned char flags;	/* flags see above! */
	unsigned char bc[6];	/* bus converter routing info */
	unsigned char mod;
	unsigned int  layers[6];/* device-specific layer-info */
} __attribute__((aligned(8))) ;

struct pz_device {
	struct	device_path dp;	/* see above */
	/* struct	iomod *hpa; */
	unsigned int hpa;	/* HPA base address */
	/* char	*spa; */
	unsigned int spa;	/* SPA base address */
	/* int	(*iodc_io)(struct iomod*, ...); */
	unsigned int iodc_io;	/* device entry point */
	short	pad;		/* reserved */
	unsigned short cl_class;/* see below */
} __attribute__((aligned(8))) ;

#endif /* __ASSEMBLY__ */

/* cl_class
 * page 3-33 of IO-Firmware ARS
 * IODC ENTRY_INIT(Search first) RET[1]
 */
#define	CL_NULL		0	/* invalid */
#define	CL_RANDOM	1	/* random access (as disk) */
#define	CL_SEQU		2	/* sequential access (as tape) */
#define	CL_DUPLEX	7	/* full-duplex point-to-point (RS-232, Net) */
#define	CL_KEYBD	8	/* half-duplex console (HIL Keyboard) */
#define	CL_DISPL	9	/* half-duplex console (display) */
#define	CL_FC		10	/* FiberChannel access media */

#if 0
/* FIXME: DEVCLASS_* duplicates CL_* (above).  Delete DEVCLASS_*? */
#define DEVCLASS_RANDOM		1
#define DEVCLASS_SEQU		2
#define DEVCLASS_DUPLEX		7
#define DEVCLASS_KEYBD		8
#define DEVCLASS_DISP		9
#endif

/* IODC ENTRY_INIT() */
#define ENTRY_INIT_SRCH_FRST	2
#define ENTRY_INIT_SRCH_NEXT	3
#define ENTRY_INIT_MOD_DEV	4
#define ENTRY_INIT_DEV		5
#define ENTRY_INIT_MOD		6
#define ENTRY_INIT_MSG		9

/* IODC ENTRY_IO() */
#define ENTRY_IO_BOOTIN		0
#define ENTRY_IO_CIN		2
#define ENTRY_IO_COUT		3
#define ENTRY_IO_CLOSE		4
#define ENTRY_IO_GETMSG		9

/* IODC ENTRY_SPA() */

/* IODC ENTRY_CONFIG() */

/* IODC ENTRY_TEST() */

/* IODC ENTRY_TLB() */


/* DEFINITION OF THE ZERO-PAGE (PAG0) */
/* based on work by Jason Eckhardt (jason@equator.com) */

#ifndef __ASSEMBLY__

#define	PAGE0	((struct zeropage *)0xc0000000)

struct zeropage {
	/* [0x000] initialize vectors (VEC) */
	unsigned int	vec_special;		/* must be zero */
	/* int	(*vec_pow_fail)(void);*/
	unsigned int	vec_pow_fail; /* power failure handler */
	/* int	(*vec_toc)(void); */
	unsigned int	vec_toc;
	unsigned int	vec_toclen;
	/* int	(*vec_rendz)(void); */
	unsigned int vec_rendz;
	int	vec_pow_fail_flen;
	int	vec_pad[10];		
	
	/* [0x040] reserved processor dependent */
	int	pad0[112];

	/* [0x200] reserved */
	int	pad1[84];

	/* [0x350] memory configuration (MC) */
	int	memc_cont;		/* contiguous mem size (bytes) */
	int	memc_phsize;		/* physical memory size */
	int	memc_adsize;		/* additional mem size, bytes of SPA space used by PDC */
	unsigned int mem_pdc_hi;	/* used for 64-bit */

	/* [0x360] various parameters for the boot-CPU */
	/* unsigned int *mem_booterr[8]; */
	unsigned int mem_booterr[8];	/* ptr to boot errors */
	unsigned int mem_free;		/* first location, where OS can be loaded */
	/* struct iomod *mem_hpa; */
	unsigned int mem_hpa;		/* HPA of the boot-CPU */
	/* int (*mem_pdc)(int, ...); */
	unsigned int mem_pdc;		/* PDC entry point */
	unsigned int mem_10msec;	/* number of clock ticks in 10msec */

	/* [0x390] initial memory module (IMM) */
	/* struct iomod *imm_hpa; */
	unsigned int imm_hpa;		/* HPA of the IMM */
	int	imm_soft_boot;		/* 0 = was hard boot, 1 = was soft boot */
	unsigned int	imm_spa_size;		/* SPA size of the IMM in bytes */
	unsigned int	imm_max_mem;		/* bytes of mem in IMM */

	/* [0x3A0] boot console, display device and keyboard */
	struct pz_device mem_cons;	/* description of console device */
	struct pz_device mem_boot;	/* description of boot device */
	struct pz_device mem_kbd;	/* description of keyboard device */

	/* [0x430] reserved */
	int	pad430[116];

	/* [0x600] processor dependent */
	__u32	pad600[1];
	__u32	proc_sti;		/* pointer to STI ROM */
	__u32	pad608[126];
};

#endif /* __ASSEMBLY__ */

/* Page Zero constant offsets used by the HPMC handler */

#define BOOT_CONSOLE_HPA_OFFSET  0x3c0
#define BOOT_CONSOLE_SPA_OFFSET  0x3c4
#define BOOT_CONSOLE_PATH_OFFSET 0x3a8

#ifndef __ASSEMBLY__

struct pdc_pat_io_num {
	unsigned long num;
	unsigned long reserved[31];
};



extern void pdc_console_init(void);
extern int  pdc_getc(void);	/* wait for char */
extern void pdc_putc(unsigned char);	/* print char */


/* wrapper-functions from pdc.c */

int pdc_add_valid(void *address);
int pdc_hpa_processor(void *address);
#if 0
int pdc_hpa_modules(void *address);
#endif
int pdc_iodc_read(void *address, void *hpa, unsigned int index,
		  void *iodc_data, unsigned int iodc_data_size);
int pdc_system_map_find_mods(void *pdc_mod_info, void *mod_path, int index);
int pdc_model_info(struct pdc_model *model);
int pdc_model_sysmodel(char  *name);
int pdc_model_cpuid(struct pdc_model_cpuid *cpu_id);
int pdc_model_versions(struct pdc_model_cpuid *cpu_id, int id);
int pdc_cache_info(struct pdc_cache_info *cache);
#ifndef __LP64__
int pdc_btlb_info( struct pdc_btlb_info *btlb);
int pdc_lan_station_id( char *lan_addr, void *net_hpa);
#endif
int pdc_mem_map_hpa(void *r_addr, void *mod_path);

extern int pdc_chassis_disp(unsigned long disp);
extern int pdc_chassis_info(void *pdc_result, void *chassis_info, unsigned long len);

#ifdef __LP64__
int pdc_pat_get_irt_size(void *r_addr, unsigned long cell_num);
int pdc_pat_get_irt(void *r_addr, unsigned long cell_num);
#else
/* No PAT support for 32-bit kernels...sorry */
#define pdc_pat_get_irt_size(r_addr, cell_numn)	PDC_RET_NE_PROC
#define pdc_pat_get_irt(r_addr, cell_num)	PDC_RET_NE_PROC
#endif
int pdc_pci_irt_size(void *r_addr, void *hpa);
int pdc_pci_irt(void *r_addr, void *hpa, void *tbl);

int pdc_tod_read(struct pdc_tod *tod);
int pdc_tod_set(unsigned long sec, unsigned long usec);

/* on all currently-supported platforms, IODC I/O calls are always
 * 32-bit calls, and MEM_PDC calls are always the same width as the OS.
 * This means Cxxx boxes can't run wide kernels right now. -PB
 *
 * Note that some PAT boxes may have 64-bit IODC I/O...
 */
#ifdef __LP64__
#   define mem_pdc_call(args...) real64_call(0L, ##args)
#else
#   define mem_pdc_call(args...) real32_call(0L, ##args)
#endif
/* yes 'int', not 'long' -- IODC I/O is always 32-bit stuff */
extern long real64_call(unsigned long function, ...);
extern long real32_call(unsigned long function, ...);
extern void pdc_init(void);

#endif /* __ASSEMBLY__ */

#endif /* _PARISC_PDC_H */
