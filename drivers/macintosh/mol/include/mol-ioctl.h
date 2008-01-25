/*
 *   Creation Date: <2003/08/26 10:53:07 samuel>
 *   Time-stamp: <2004/02/08 20:17:58 samuel>
 *
 *	<mol-ioctl.h>
 *
 *
 *
 *   Copyright (C) 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#ifndef _H_MOL_IOCTL
#define _H_MOL_IOCTL

#include <asm/ioctl.h>

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
	int		arg1, arg2, arg3;
#ifdef __darwin__
	int		ret;
#endif
} mol_ioctl_pb_t;

#endif /* __ASSEMBLY__ */

/* ioctls that do not use the mol_ioctl_pb arg */
#define MOL_IOCTL_SMP_SEND_IPI		_IO('M', 1) 			/* void ( void ) */
#ifdef __darwin__
#define MOL_IOCTL_CALL_KERNEL		_IO('M', 2)			/* void ( void ) */
#endif

/* debugger ioctls */
#define MOL_IOCTL_DEBUGGER_OP		_IOWR('M', 10, mol_ioctl_pb_t)	/* int ( dbg_op_params *p ) */
#define  DBG_OP_EMULATE_TLBIE		  1				/* void ( ulong pageindex ) */
#define  DBG_OP_EMULATE_TLBIA		  2				/* void ( void ) */
#define  DBG_OP_GET_PTE			  3				/* lvptr, context, int ( ulong vsid, ulong va, PTE *retpte ) */
#define  DBG_OP_GET_PHYS_PAGE		  4				/* int ( ulong lvptr, ulong *retptr ) */
#define	 DBG_OP_BREAKPOINT_FLAGS	  5				/* void ( ulong flags ) */
#define  DBG_OP_TRANSLATE_EA		  6				/* ea, context, is_data -- mphys */
#define MOL_IOCTL_CLEAR_PERF_INFO	_IOWR('M', 11, mol_ioctl_pb_t)	/* void ( void ) */
#define MOL_IOCTL_GET_PERF_INFO		_IOWR('M', 12, mol_ioctl_pb_t)	/* int ( int index, perf_ctr_t *ctr ) */

/* config selectors */
#define	MOL_IOCTL_CREATE_SESSION	_IOWR('M', 30, mol_ioctl_pb_t)	/* int ( int session_index ) */
#define MOL_IOCTL_GET_INFO		_IOWR('M', 31, mol_ioctl_pb_t)	/* int ( mol_kmod_info_t *retinfo, int size ) */
#define MOL_IOCTL_SET_RAM		_IOWR('M', 33, mol_ioctl_pb_t)	/* void ( ulong ram_start, ulong ram_end ) */
#define MOL_IOCTL_COPY_LAST_ROMPAGE	_IOWR('M', 34, mol_ioctl_pb_t)	/* void ( char *destpage ) */
#define MOL_IOCTL_SPR_CHANGED		_IOWR('M', 35, mol_ioctl_pb_t)	/* void ( void ) */
#define MOL_IOCTL_IDLE_RECLAIM_MEMORY	_IOWR('M', 36, mol_ioctl_pb_t)	/* void ( void ) */
#define MOL_IOCTL_MMU_MAP		_IOWR('M', 37, mol_ioctl_pb_t)	/* void ( struct mmu_mapping *m, int add_map ) */
#define MOL_IOCTL_ADD_IORANGE		_IOWR('M', 39, mol_ioctl_pb_t)	/* void ( ulong mbase, size_t size, io_ops_t *) */
#define MOL_IOCTL_REMOVE_IORANGE	_IOWR('M', 40, mol_ioctl_pb_t)	/* void ( ulong mbase, size_t size ) */
#define MOL_IOCTL_SETUP_FBACCEL		_IOWR('M', 41, mol_ioctl_pb_t)	/* void * ( char *lvbase, int bytes_per_row, int height ) */
#define MOL_IOCTL_GET_DIRTY_FBLINES	_IOWR('M', 42, mol_ioctl_pb_t)	/* int ( short *rettable, int table_size_in_bytes ) */
#define MOL_IOCTL_TRACK_DIRTY_RAM	_IOWR('M', 43, mol_ioctl_pb_t)	/* int ( char *lvbase, size_t size ) */
#define MOL_IOCTL_GET_DIRTY_RAM		_IOWR('M', 44, mol_ioctl_pb_t)	/* size_t ( char *retbuf ) */
#define MOL_IOCTL_SET_DIRTY_RAM		_IOWR('M', 45, mol_ioctl_pb_t)	/* void ( char *dirtybuf ) */
#define MOL_IOCTL_GET_MREGS_PHYS	_IOWR('M', 46, mol_ioctl_pb_t)	/* ulong ( void ) */
#define MOL_IOCTL_ALLOC_EMUACCEL_SLOT	_IOWR('M', 47, mol_ioctl_pb_t)	/* int ( int inst_flags, int param, int inst_addr ) */
#define MOL_IOCTL_MAPIN_EMUACCEL_PAGE	_IOWR('M', 48, mol_ioctl_pb_t)	/* int ( int mphys ) */
#define MOL_IOCTL_TUNE_SPR		_IOWR('M', 49, mol_ioctl_pb_t)	/* int ( int spr, int action ) */
#define MOL_IOCTL_GET_SESSION_MAGIC	_IOWR('M', 50, mol_ioctl_pb_t)	/* uint ( uint new_random_magic ) */

#define MOL_IOCTL_DBG_COPY_KVARS	_IOWR('M', 51, mol_ioctl_pb_t)	/* int ( int session, kernel_vars_t *dest ) */

#ifdef __darwin__
#define MOL_IOCTL_GET_MREGS_VIRT	_IOWR('M', 52, mol_ioctl_pb_t)	/* int ( mac_regs_t **ret ) */
#endif

#define MOL_IOCTL_GRAB_IRQ		_IOWR('M', 53, mol_ioctl_pb_t)	/* int ( int irq ) */
#define MOL_IOCTL_RELEASE_IRQ		_IOWR('M', 54, mol_ioctl_pb_t)	/* int ( int irq ) */
#define MOL_IOCTL_GET_IRQS		_IOWR('M', 55, mol_ioctl_pb_t)	/* int ( irq_bitfield_t * ) */


/* MOL error codes */
#define EMOLGENERAL			100
#define EMOLINUSE			101
#define EMOLINVAL			102
#define EMOLSECURITY			103

#endif   /* _H_MOL_IOCTL */
