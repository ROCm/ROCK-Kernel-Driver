/* -*-c-*-
 *   Creation Date: <2001/10/20 17:51:10 samuel>
 *   Time-stamp: <2003/08/20 14:00:27 samuel>
 *   
 *	<asm_offsets.inc>
 *	
 *	Extra offsets (included from asm_offsets.c)
 *   
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

extern void extra( void );

void
extra( void )
{
	DEFINE(SIZE_OF_KERNEL_VARS, sizeof( kernel_vars_t ));

	ST_DEF( ST_MAGIC, magic );
	ST_DEF( ST_KVARS_PH, kvars_ph );

	K_DEF( K_EMUACCEL_PAGE, emuaccel_page );
	K_DEF( K_EMUACCEL_PAGE_PHYS, emuaccel_page_phys );
	K_DEF( K_EMUACCEL_MPHYS, emuaccel_mphys );
	K_DEF( K_EMUACCEL_SIZE, emuaccel_size );

	K_DEF( K_KERNEL_VARS, kvars_virt );
	K_DEF( K_BREAK_FLAGS, break_flags );
#ifdef PERFORMANCE_INFO
	K_DEF( K_ASM_BUMP_CNTRS, asm_bump_cntr );
	K_DEF( K_ASM_TICK_STAMPS, asm_tick_stamp );
#endif
	/* MMU */
	K_DEF( K_ILLEGAL_SR, mmu.illegal_sr );
	K_DEF( K_CUR_SR_BASE, mmu.cur_sr_base );
	K_DEF( K_SR_DATA, mmu.sr_data );
	K_DEF( K_SR_INST, mmu.sr_inst );

	K_DEF( K_UNMAPPED_SR_BASE, mmu.unmapped_sr[0] );
	K_DEF( K_USER_SR_BASE, mmu.user_sr[0] );
	K_DEF( K_SV_SR_BASE, mmu.sv_sr[0] );
	K_DEF( K_SPLIT_SR_BASE, mmu.split_sr[0] );
	K_DEF( K_VSID_ENT_BASE, mmu.vsid[0] );

	DEFINE( SKIPLIST_NEXT, offsetof(skiplist_el_t, next) );
	DEFINE( SKIPLIST_KEY, offsetof(skiplist_el_t, key) );
	DEFINE( K_VSID_SL_ROOT_ELEM, offsetof(kernel_vars_t, mmu.vsid_sl.root) 
				     - offsetof(skiplist_el_t, next));
	K_DEF( K_VSID_SL_SLEVEL, mmu.vsid_sl.slevel );

	K_DEF( K_IBAT0U_SAVE, _bp.ibat_save[0].word[0] );
	K_DEF( K_IBAT0L_SAVE, _bp.ibat_save[0].word[1] );
	K_DEF( K_IBAT1U_SAVE, _bp.ibat_save[1].word[0] );
	K_DEF( K_IBAT1L_SAVE, _bp.ibat_save[1].word[1] );
	K_DEF( K_IBAT2U_SAVE, _bp.ibat_save[2].word[0] );
	K_DEF( K_IBAT2L_SAVE, _bp.ibat_save[2].word[1] );
	K_DEF( K_IBAT3U_SAVE, _bp.ibat_save[3].word[0] );
	K_DEF( K_IBAT3L_SAVE, _bp.ibat_save[3].word[1] );

	K_DEF( K_DBAT0U_SAVE, _bp.dbat_save[0].word[0] );
	K_DEF( K_DBAT0L_SAVE, _bp.dbat_save[0].word[1] );
	K_DEF( K_DBAT1U_SAVE, _bp.dbat_save[1].word[0] );
	K_DEF( K_DBAT1L_SAVE, _bp.dbat_save[1].word[1] );
	K_DEF( K_DBAT2U_SAVE, _bp.dbat_save[2].word[0] );
	K_DEF( K_DBAT2L_SAVE, _bp.dbat_save[2].word[1] );
	K_DEF( K_DBAT3U_SAVE, _bp.dbat_save[3].word[0] );
	K_DEF( K_DBAT3L_SAVE, _bp.dbat_save[3].word[1] );

	K_DEF( K_SPLIT_DBAT0U, mmu.split_dbat0.word[0] );
	K_DEF( K_SPLIT_DBAT0L, mmu.split_dbat0.word[1] );
	K_DEF( K_TRANSL_DBAT0U, mmu.transl_dbat0.word[0] );
	K_DEF( K_TRANSL_DBAT0L, mmu.transl_dbat0.word[1] );

	K_DEF( K_TLBHASH_SR, mmu.pthash_sr );
	K_DEF( K_TLBHASH_BASE_EA, mmu.pthash_ea_base );
	K_DEF( K_HASH_MASK, mmu.hash_mask );
	K_DEF( K_PTHASH_INUSE_PH, mmu.pthash_inuse_bits_ph );

	/* fields private to the assembly files */
	K_DEF( K_MSR, _bp._msr );

	K_DEF( K_DEC_STAMP, _bp.dec_stamp );
	K_DEF( K_INT_STAMP, _bp.int_stamp);

	K_DEF( K_TMP_SCRATCH0, _bp.tmp_scratch[0] );
	K_DEF( K_TMP_SCRATCH1, _bp.tmp_scratch[1] );
	K_DEF( K_TMP_SCRATCH2, _bp.tmp_scratch[2] );
	K_DEF( K_TMP_SCRATCH3, _bp.tmp_scratch[3] );

	K_DEF( K_SPLIT_NIP_SEGMENT, _bp.split_nip_segment );

	K_DEF( K_SPR_HOOKS, _bp.spr_hooks );

	/* private to emulation.S */
	K_DEF( K_MSR_SR_TABLE, _bp.msr_sr_table[0] );

	/* io_page_t type */
	IO_DEF( IOP_MAGIC, magic );
	IO_DEF( IOP_MAGIC2, magic2 );
	IO_DEF( IOP_MPHYS, mphys );
	IO_DEF( IOP_ME_PHYS, me_phys );
	IO_DEF( IOP_NEXT, next );
	IO_DEF( IOP_USR_DATA, usr_data );

	/****** New Low-level assembly implementation ***********/

	K_DEF( K_EMULATOR_STACK, _bp.emulator_stack );
	K_DEF( K_EMULATOR_TOC, _bp.emulator_toc );
	K_DEF( K_EMULATOR_NIP, _bp.emulator_nip );
	K_DEF( K_EMULATOR_MSR, _bp.emulator_msr );
	K_DEF( K_EMULATOR_SPRG2, _bp.emulator_sprg2 );
	K_DEF( K_EMULATOR_SPRG3, _bp.emulator_sprg3 );
	K_DEF( K_EMULATOR_KCALL_NIP, _bp.emulator_kcall_nip );

	K_DEF( K_EMULATOR_SR, mmu.emulator_sr );
}
