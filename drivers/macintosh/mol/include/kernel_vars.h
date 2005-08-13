/* 
 *   Creation Date: <97/07/14 15:53:06 samuel>
 *   Time-stamp: <2003/08/27 12:46:50 samuel>
 *   
 *	<kernel_vars.h>
 *	
 *	Variables used by the kernel
 *   
 *   Copyright (C) 1997-2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef H_KERNEL_VARS
#define H_KERNEL_VARS

#define MAX_NUM_SESSIONS	8

#include "mac_registers.h"

#ifdef PERFORMANCE_INFO
#define NUM_ASM_BUMP_CNTRS	64
#define NUM_ASM_TICK_CNTRS	6
#endif

#ifndef __ASSEMBLY__
#include "mmu_mappings.h"
#include "skiplist.h"

#ifndef DUMPVARS
#include "alloc.h"
#else
typedef int mol_spinlock_t;
typedef int atomic_t;
struct semaphore { int s; };
#endif

typedef struct {
	ulong		word[2];		/* upper, lower */
} ppc_bat_t;

typedef struct mac_bat {
	int		valid;			/* record in use */
	ulong		base;
	ulong		mbase;
	ulong		size;

	ulong		wimg:4;			/* WIMG-bits */
	ulong		vs:1;			/* valid supervisor mode */
	ulong		vp:1;			/* valid user mode */
	ulong		ks:1;			/* key superuser */
	ulong		ku:1;			/* key user */
	ulong		pp:2;			/* page protection */

	/* possibly we should track inserted PTEs here... */
} mac_bat_t;

typedef struct {
	struct vsid_ent	*vsid[16];		/* entries might be NULL */
	struct vsid_ent *unmapped_vsid[16];	/* entries might be NULL, linux_vsid_sv used */

	ulong		emulator_sr[16];	/* segment registers used by the userspace process */
	
	ulong		user_sr[16];		/* segment registers for MSR=user */
	ulong		sv_sr[16];		/* segment registers for MSR=sv */
	ulong		unmapped_sr[16];	/* segment registers for unmapped mode */
	ulong		split_sr[16];		/* segment registers used in split mode */

	ulong		cur_sr_base;		/* (physical) pointer to user_sr or sv_sr */
	ulong		sr_inst;		/* (physical) pointer to us user_sr or sv_sr */
	ulong		sr_data;		/* (physical) pointer to us user_sr or sv_sr */

	ulong		illegal_sr;		/* used for the lazy segment register impl. */ 

	ppc_bat_t	split_dbat0;		/* loaded to DBAT0 (used in splitmode) */
	ppc_bat_t	transl_dbat0;		/* DBAT0 mapping the framebuffer */

	ulong		emulator_context;	/* context of emulator (equals VSID >> 4) in Linux */

	ulong		mac_ram_base;		/* macram, mac-virtual base */
	char		*linux_ram_base;	/* linux-virtual base */
	size_t		ram_size;

	mac_bat_t	bats[8];		/* 4 IBAT + 4 DBAT */
	ulong		bat_hack_count;		/* HACK to speed up MacOS 9.1 */
#ifdef EMULATE_603
	ulong		ptes_d_ea_603[64];	/* EA4-EA19 of dPTE */
	mPTE_t		ptes_d_603[64];		/* Data on-chip PTEs (603-emulation) */
	ulong		ptes_i_ea_603[64];	/* EA4-EA19 of iPTE */
	mPTE_t		ptes_i_603[64];		/* Instruction on-chip PTEs (603-emulation) */
#endif
	/* Emulated PTE hash */
	ulong		hash_mbase;		/* mac physical base address of hash */
	ulong		*hash_base;		/* linux virtual base address of hash */
	ulong		hash_mask;		/* hash mask (0x000fffff etc) */
	ulong		hw_sdr1;		/* Hardware SDR1 */

	ulong		pthash_sr;		/* segment register corresponding to */ 
	ulong		pthash_ea_base;		/* pthash_ea_base */
	void		*pthash_inuse_bits;	/* bitvector (one bit per PTE) */
	ulong		pthash_inuse_bits_ph;	/* physical base address */

	/* context number allocation */
	int		next_mol_context;	/* in the range FIRST .. LAST_MOL_CONTEXT(n) */
	int		first_mol_context;	/* first context number this session may use */
	int		last_mol_context;	/* last context number this session may use */

	/* various tables */
	struct io_data 		*io_data;	/* translation info */
	struct fb_data		*fb_data;	/* ea -> PTE table */
	struct tracker_data	*tracker_data;	/* Keeps track of modified pages */

	/* mtable stuff */
	skiplist_t		vsid_sl;	/* skiplist (with vsid_ent_t objects) */
	struct vsid_info	*vsid_info;	/* mtable data */

	char   		*lvptr_reservation;	/* lvptr associated with PTE to be inserted */
	int		lvptr_reservation_lost;	/* set if reservation is lost (page out) */
} mmu_vars_t;


/* variables which are private to the low level assembly code */
typedef struct {
	ulong		spr_hooks[NUM_SPRS];	/* hooks */

	ppc_bat_t 	ibat_save[4];		/* kernel values of the BAT-registers */
	ppc_bat_t 	dbat_save[4];

	ulong		_msr;			/* MSR used in mac-mode (_not_ the emulated msr) */

	/* saved kernel/emulator registers */
	ulong		emulator_nip;
	ulong		emulator_msr;
	ulong		emulator_sprg2;
	ulong		emulator_sprg3;
	ulong		emulator_kcall_nip;
	ulong 		emulator_stack;
	ulong 		emulator_toc;		/* == r2 on certain systems */

	/* DEC and timebase */
	ulong		dec_stamp;		/* linux DEC = dec_stamp - tbl */
	ulong		int_stamp;		/* next DEC event = int_stamp - tbl */

	/* splitmode */
	int		split_nip_segment;	/* segment (top 4) used for inst. fetches */

	/* segment register offset table */
	ulong		msr_sr_table[ 4*8 ];	/* see emulation.S */

	ulong		tmp_scratch[4];		/* temporary storage */
} base_private_t;


#ifdef PERFORMANCE_INFO
#define MAX_ACC_CNTR_DEPTH	8
typedef struct acc_counter {
	ulong 		stamp;
	ulong		subticks;
} acc_counter_t;
#endif

typedef struct kernel_vars {
	struct mac_regs		mregs;			/* must go first */
	char 			page_filler[0x1000 - (sizeof(mac_regs_t)&0xfff) ];

	base_private_t	 	_bp;
	char 			aligner[32 - (sizeof(base_private_t)&0x1f) ];
	mmu_vars_t		mmu;
	char 			aligner2[16 - (sizeof(mmu_vars_t)&0xf) ];

	ulong			emuaccel_mphys;		/* mphys address of emuaccel_page */
	int			emuaccel_size;		/* size used */
	ulong			emuaccel_page_phys;	/* phys address of page */
	ulong			emuaccel_page;		/* page used for instruction acceleration */

	int			break_flags;
	struct kernel_vars	*kvars_virt;		/* me */
	int			session_index;

	struct semaphore	ioctl_sem;		/* ioctl lock */

#ifdef PERFORMANCE_INFO
	ulong			asm_bump_cntr[NUM_ASM_BUMP_CNTRS];
	ulong			asm_tick_stamp[NUM_ASM_TICK_CNTRS];
	int			num_acntrs;
	acc_counter_t		acntrs[MAX_ACC_CNTR_DEPTH];
#endif
} kernel_vars_t;

#define NUM_KVARS_PAGES		((sizeof( kernel_vars_t )+0xfff)/0x1000)


typedef struct {
	kernel_vars_t		*kvars[MAX_NUM_SESSIONS];
	int			magic;
	ulong	 		kvars_ph[MAX_NUM_SESSIONS];
	struct semaphore	mutex;
	atomic_t		external_thread_cnt;
} session_table_t;

#define SESSION_LOCK		down( &g_sesstab->mutex )
#define SESSION_UNLOCK		up( &g_sesstab->mutex )

extern session_table_t		*g_sesstab;


#endif /* __ASSEMBLY__ */
#endif
