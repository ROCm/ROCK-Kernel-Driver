/* 
 *   Creation Date: <2002/05/26 15:52:50 samuel>
 *   Time-stamp: <2003/08/16 05:25:16 samuel>
 *   
 *	<mtable.h>
 *	
 *	Keeps track of the PTEs
 *   
 *   Copyright (C) 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_MTABLE
#define _H_MTABLE

#ifndef __ASSEMBLY__
typedef struct pte_lvrange	pte_lvrange_t;
typedef struct vsid_info	vsid_info_t;
typedef struct vsid_ent		vsid_ent_t;

extern int		init_mtable( kernel_vars_t *kv );
extern void		cleanup_mtable( kernel_vars_t *kv );

extern pte_lvrange_t 	*register_lvrange( kernel_vars_t *kv, char *lvbase, int size );
extern void		free_lvrange( kernel_vars_t *kv, pte_lvrange_t *pte_range );

extern vsid_ent_t	*vsid_get_user_sv( kernel_vars_t *kv, int mac_vsid, ulong *user_sr, ulong *sv_sr );

extern int 		mtable_memory_check( kernel_vars_t *kv );
extern void		pte_inserted( kernel_vars_t *kv, ulong ea, char *lvptr, 
				      pte_lvrange_t *lvrange, ulong *pte, vsid_ent_t *r,
				      int segreg );

extern void		flush_vsid_ea( kernel_vars_t *kv, int vsid, ulong ea );
extern void		flush_ea_range( kernel_vars_t *kv, ulong ea, int size );
extern void		flush_lvptr( kernel_vars_t *kv, ulong lvptr );
extern void		flush_lv_range( kernel_vars_t *kv, ulong lvbase, int size );

extern void		clear_all_vsids( kernel_vars_t *kv );
extern void		clear_pte_hash_table( kernel_vars_t *kv );

extern void		mtable_reclaim( kernel_vars_t *kv );
extern void		mtable_tune_alloc_limit( kernel_vars_t *kv, int ramsize_mb );

static inline void
make_lvptr_reservation( kernel_vars_t *kv, char *lvptr ) {
	kv->mmu.lvptr_reservation = lvptr;
	kv->mmu.lvptr_reservation_lost = 0;
}


#endif /* __ASSEMBLY__ */

/* negativ offsets to linux_vsid and linux_vsid_sv from 
 * the end of the vsid_ent_t struct.
 */
#define	VSID_USER_OFFS		0
#define	VSID_SV_OFFS		4
#define SIZEOF_VSID_ENT		(64*4 + 8)

#define VSID_OFFSETS_OK	\
	((offsetof(vsid_ent_t, linux_vsid) == VSID_USER_OFFS ) || \
	 (offsetof(vsid_ent_t, linux_vsid_sv) == VSID_SV_OFFS ) || \
	 (sizeof(vsid_ent_t) == SIZEOF_VSID_ENT))

#endif   /* _H_MTABLE */


