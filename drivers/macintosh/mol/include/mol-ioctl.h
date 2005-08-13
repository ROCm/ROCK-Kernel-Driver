/* 
 *   Creation Date: <2003/08/26 10:53:07 samuel>
 *   Time-stamp: <2003/08/26 17:32:07 samuel>
 *   
 *	<mol-ioctl.h>
 *	
 *	
 *   
 *   Copyright (C) 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *   
 */

#ifndef _H_MOL_IOCTL
#define _H_MOL_IOCTL

#ifndef __ASSEMBLY__
#include "mmutypes.h"

typedef struct {
	int		version;	/* MOL version */
	int		smp_kernel;	/* compiled with CONFIG_SMP */
	int		pvr;		/* cpu version/revision (PVR) */
	int		rombase;
	int		romsize;
	unsigned int	tb_freq;
} mol_kmod_info_t;

typedef struct perf_ctr {
	unsigned int	ctr;
	char		name[40];
} perf_ctr_t;

typedef struct dbg_page_info {
	int		phys;
	int		mflags;		/* M_PAGE_XXX */
} dbg_page_info_t;

typedef struct dbg_op_params {
	/* input */
	int		operation;	/* DBG_OP_xxx */
	int		ea;
	int		context;
	int		param;

	/* output */
	union {
		int		phys;
		dbg_page_info_t	page;
		mPTE_t		pte;
	} ret;
} dbg_op_params_t;

typedef struct mol_ioctl_pb {
	int		arg1;
	int		arg2;
	int		arg3;
} mol_ioctl_pb_t;

#endif /* __ASSEMBLY__ */

/* ioctls that do not use the mol_ioctl_pb arg */
#define MOL_IOCTL_SMP_SEND_IPI		1	/* void ( void ) */
#define	MOL_IOCTL_CREATE_SESSION	2	/* int ( int session_index ) */

/* debugger ioctls */
#define MOL_IOCTL_DEBUGGER_OP		10	/* int ( dbg_op_params *p ) */
#define  DBG_OP_EMULATE_TLBIE		 1	/* void ( ulong pageindex ) */
#define  DBG_OP_EMULATE_TLBIA		 2	/* void ( void ) */
#define  DBG_OP_GET_PTE			 3	/* lvptr, context, int ( ulong vsid, ulong va, PTE *retpte ) */
#define  DBG_OP_GET_PHYS_PAGE		 4	/* int ( ulong lvptr, ulong *retptr ) */
#define	 DBG_OP_BREAKPOINT_FLAGS	 5	/* void ( ulong flags ) */
#define  DBG_OP_TRANSLATE_EA		 6	/* ea, context, is_data -- mphys */
#define MOL_IOCTL_CLEAR_PERF_INFO	11	/* void ( void ) */
#define MOL_IOCTL_GET_PERF_INFO		12	/* int ( int index, perf_ctr_t *ctr ) */

/* config selectors */
#define MOL_IOCTL_GET_INFO		31	/* int ( mol_kmod_info_t *retinfo, int size ) */
#define MOL_IOCTL_SET_RAM		33	/* void ( ulong ram_start, ulong ram_end ) */
#define MOL_IOCTL_COPY_LAST_ROMPAGE	34	/* void ( char *destpage ) */
#define MOL_IOCTL_SPR_CHANGED		35	/* void ( void ) */
#define MOL_IOCTL_IDLE_RECLAIM_MEMORY	36	/* void ( void ) */
#define MOL_IOCTL_MMU_MAP		37	/* void ( struct mmu_mapping *m, int add_map ) */
#define MOL_IOCTL_ADD_IORANGE		39	/* void ( ulong mbase, size_t size, io_ops_t *) */ 
#define MOL_IOCTL_REMOVE_IORANGE	40	/* void ( ulong mbase, size_t size ) */ 
#define MOL_IOCTL_SETUP_FBACCEL		41	/* void * ( char *lvbase, int bytes_per_row, int height ) */
#define MOL_IOCTL_GET_DIRTY_FBLINES	42	/* int ( short *rettable, int table_size_in_bytes ) */
#define MOL_IOCTL_TRACK_DIRTY_RAM	43	/* int ( char *lvbase, size_t size ) */
#define MOL_IOCTL_GET_DIRTY_RAM		44	/* size_t ( char *retbuf ) */
#define MOL_IOCTL_SET_DIRTY_RAM		45	/* void ( char *dirtybuf ) */
#define MOL_IOCTL_GET_MREGS_PHYS	46	/* ulong ( void ) */
#define MOL_IOCTL_ALLOC_EMUACCEL_SLOT	47	/* int ( int inst_flags, int param, int inst_addr ) */
#define MOL_IOCTL_MAPIN_EMUACCEL_PAGE	48	/* int ( int mphys ) */
#define MOL_IOCTL_TUNE_SPR		49	/* int ( int spr, int action ) */
#define MOL_IOCTL_GET_SESSION_MAGIC	50	/* uint ( uint new_random_magic ) */

#define MOL_IOCTL_DBG_GET_KVARS_PHYS	51	/* int ( int session ) */

/* MOL error codes */
#define EMOLGENERAL			100
#define EMOLINUSE			101
#define EMOLINVAL			102
#define EMOLSECURITY			103

#endif   /* _H_MOL_IOCTL */
