/*
 * Copyright (c) 2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "xfs.h"

#include "xfs_fs.h"
#include "xfs_buf.h"
#include "xfs_macros.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_imap.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bit.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_refcache.h"
#include "xfs_da_btree.h"
#include "xfs_dir_leaf.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_node.h"
#include "xfs_dir2_trace.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_inode_item.h"
#include "xfs_buf_item.h"
#include "xfs_extfree_item.h"
#include "xfs_log_priv.h"
#include "xfs_trans_priv.h"
#include "xfs_trans_space.h"
#include "xfs_utils.h"
#include "xfs_iomap.h"
#include "support/ktrace.h"


/*
 * Export symbols used for XFS tracing
 */

#if defined(CONFIG_XFS_TRACE)
EXPORT_SYMBOL(ktrace_enter);
EXPORT_SYMBOL(ktrace_free);
EXPORT_SYMBOL(ktrace_alloc);
EXPORT_SYMBOL(ktrace_skip);
EXPORT_SYMBOL(ktrace_nentries);
EXPORT_SYMBOL(ktrace_first);
EXPORT_SYMBOL(ktrace_next);
#endif

#ifdef XFS_ILOCK_TRACE
EXPORT_SYMBOL(xfs_ilock_trace_buf);
#endif
#ifdef XFS_ALLOC_TRACE
EXPORT_SYMBOL(xfs_alloc_trace_buf);
#endif
#ifdef XFS_BMAP_TRACE
EXPORT_SYMBOL(xfs_bmap_trace_buf);
#endif
#ifdef XFS_BMBT_TRACE
EXPORT_SYMBOL(xfs_bmbt_trace_buf);
#endif
#ifdef XFS_ATTR_TRACE
EXPORT_SYMBOL(xfs_attr_trace_buf);
#endif  
#ifdef XFS_DIR2_TRACE
EXPORT_SYMBOL(xfs_dir2_trace_buf);
#endif   
#ifdef XFS_DIR_TRACE
EXPORT_SYMBOL(xfs_dir_trace_buf);
#endif

#ifdef PAGEBUF_TRACE
extern ktrace_t *pagebuf_trace_buf;
EXPORT_SYMBOL(pagebuf_trace_buf);
#endif


/*
 * Export symbols used for XFS debugging
 */
EXPORT_SYMBOL(xfs_next_bit);
EXPORT_SYMBOL(xfs_contig_bits);
EXPORT_SYMBOL(xfs_bmbt_get_all);
EXPORT_SYMBOL(xfs_params);
#if ARCH_CONVERT != ARCH_NOCONVERT
EXPORT_SYMBOL(xfs_bmbt_disk_get_all);
#endif

#if defined(CONFIG_XFS_DEBUG)
extern struct list_head pbd_delwrite_queue;
EXPORT_SYMBOL(pbd_delwrite_queue);

EXPORT_SYMBOL(xfs_fsb_to_agbno);
EXPORT_SYMBOL(xfs_dir2_data_unused_tag_p_arch);
EXPORT_SYMBOL(xfs_attr_leaf_name_remote);
EXPORT_SYMBOL(xfs_lic_slot);
EXPORT_SYMBOL(xfs_dir2_sf_firstentry);
EXPORT_SYMBOL(xfs_ino_to_agno);
EXPORT_SYMBOL(xfs_dir2_sf_get_inumber_arch);
EXPORT_SYMBOL(xfs_dir2_data_entry_tag_p);
EXPORT_SYMBOL(xfs_dir2_sf_inumberp);
EXPORT_SYMBOL(xfs_dir2_data_entsize);
EXPORT_SYMBOL(xfs_lic_isfree);
EXPORT_SYMBOL(xfs_attr_leaf_name_local);
EXPORT_SYMBOL(xfs_bmap_broot_ptr_addr);
EXPORT_SYMBOL(xfs_dir_sf_get_dirino_arch);
EXPORT_SYMBOL(xfs_ino_to_agbno);
EXPORT_SYMBOL(xfs_dir2_leaf_bests_p_arch);
EXPORT_SYMBOL(xfs_dir2_sf_get_offset_arch);
EXPORT_SYMBOL(startblockval);
EXPORT_SYMBOL(xfs_attr_sf_nextentry);
EXPORT_SYMBOL(xfs_bmap_broot_key_addr);
EXPORT_SYMBOL(xfs_dir2_block_leaf_p_arch);
EXPORT_SYMBOL(xfs_dir_leaf_namestruct);
EXPORT_SYMBOL(xfs_ino_to_offset);
EXPORT_SYMBOL(isnullstartblock);
EXPORT_SYMBOL(xfs_lic_are_all_free);
EXPORT_SYMBOL(xfs_dir_sf_nextentry);
EXPORT_SYMBOL(xfs_dir2_sf_nextentry);

EXPORT_SYMBOL(xfs_da_cookie_entry);
EXPORT_SYMBOL(xfs_da_cookie_bno);
EXPORT_SYMBOL(xfs_da_cookie_hash);
#endif


/*
 * Export symbols used by XFS behavior modules.
 */

EXPORT_SYMBOL(assfail);
EXPORT_SYMBOL(cmn_err);
EXPORT_SYMBOL(bhv_base);
EXPORT_SYMBOL(bhv_head_destroy);
EXPORT_SYMBOL(bhv_insert);
EXPORT_SYMBOL(bhv_insert_initial);
EXPORT_SYMBOL(bhv_lookup);
EXPORT_SYMBOL(bhv_lookup_range);
EXPORT_SYMBOL(bhv_remove_vfsops);
EXPORT_SYMBOL(bhv_remove_all_vfsops);
EXPORT_SYMBOL(bhv_remove_not_first);
EXPORT_SYMBOL(doass);
EXPORT_SYMBOL(fs_flush_pages);
EXPORT_SYMBOL(fs_flushinval_pages);
EXPORT_SYMBOL(fs_tosspages);
EXPORT_SYMBOL(fs_noval);
#if ((defined(DEBUG) || defined(INDUCE_IO_ERROR)) && !defined(NO_WANT_RANDOM))
EXPORT_SYMBOL(get_thread_id);
#endif
EXPORT_SYMBOL(icmn_err);
EXPORT_SYMBOL(kmem_alloc);
EXPORT_SYMBOL(kmem_free);
EXPORT_SYMBOL(kmem_realloc);
EXPORT_SYMBOL(kmem_shake_deregister);
EXPORT_SYMBOL(kmem_shake_register);
EXPORT_SYMBOL(kmem_zalloc);
EXPORT_SYMBOL(kmem_zone_alloc);
EXPORT_SYMBOL(kmem_zone_free);
EXPORT_SYMBOL(kmem_zone_init);
EXPORT_SYMBOL(kmem_zone_zalloc);
EXPORT_SYMBOL(linvfs_aops);
EXPORT_SYMBOL(linvfs_dir_inode_operations);
EXPORT_SYMBOL(linvfs_dir_operations);
EXPORT_SYMBOL(linvfs_file_inode_operations);
EXPORT_SYMBOL(linvfs_file_operations);
EXPORT_SYMBOL(linvfs_invis_file_operations);
EXPORT_SYMBOL(linvfs_symlink_inode_operations);
EXPORT_SYMBOL(pagebuf_delwri_dequeue);
EXPORT_SYMBOL(pagebuf_find);
EXPORT_SYMBOL(pagebuf_iostart);
EXPORT_SYMBOL(pagebuf_ispin);
EXPORT_SYMBOL(pagebuf_lock_value);
EXPORT_SYMBOL(pagebuf_offset);
EXPORT_SYMBOL(pagebuf_rele);
EXPORT_SYMBOL(pagebuf_readahead);
EXPORT_SYMBOL(pagebuf_unlock);
#if ((defined(DEBUG) || defined(INDUCE_IO_ERROR)) && !defined(NO_WANT_RANDOM))
EXPORT_SYMBOL(random);
#endif
EXPORT_SYMBOL(sys_cred);
EXPORT_SYMBOL(uuid_create_nil);
EXPORT_SYMBOL(uuid_equal);
EXPORT_SYMBOL(uuid_getnodeuniq);
EXPORT_SYMBOL(uuid_hash64);
EXPORT_SYMBOL(uuid_is_nil);
EXPORT_SYMBOL(uuid_table_remove);
EXPORT_SYMBOL(vfs_mount);
EXPORT_SYMBOL(vfs_parseargs);
EXPORT_SYMBOL(vfs_showargs);
EXPORT_SYMBOL(vfs_unmount);
EXPORT_SYMBOL(vfs_mntupdate);
EXPORT_SYMBOL(vfs_root);
EXPORT_SYMBOL(vfs_statvfs);
EXPORT_SYMBOL(vfs_sync);
EXPORT_SYMBOL(vfs_vget);
EXPORT_SYMBOL(vfs_dmapiops);
EXPORT_SYMBOL(vfs_quotactl);
EXPORT_SYMBOL(vfs_get_inode);
EXPORT_SYMBOL(vfs_init_vnode);
EXPORT_SYMBOL(vfs_force_shutdown);
EXPORT_SYMBOL(vn_wait);
EXPORT_SYMBOL(vn_get);
EXPORT_SYMBOL(vn_hold);
EXPORT_SYMBOL(vn_initialize);
EXPORT_SYMBOL(vn_revalidate);
EXPORT_SYMBOL(vn_purge);
EXPORT_SYMBOL(vttoif_tab);

#if defined(CONFIG_XFS_POSIX_ACL)
EXPORT_SYMBOL(xfs_acl_vtoacl);
EXPORT_SYMBOL(xfs_acl_inherit);
#endif
EXPORT_SYMBOL(xfs_alloc_buftarg);
EXPORT_SYMBOL(xfs_flush_buftarg);
EXPORT_SYMBOL(xfs_bdstrat_cb);
EXPORT_SYMBOL(xfs_bmap_cancel);
EXPORT_SYMBOL(xfs_bmap_do_search_extents);
EXPORT_SYMBOL(xfs_bmap_finish);
EXPORT_SYMBOL(xfs_bmapi);
EXPORT_SYMBOL(xfs_bmapi_single);
EXPORT_SYMBOL(xfs_bmbt_get_blockcount);
EXPORT_SYMBOL(xfs_bmbt_get_state);
EXPORT_SYMBOL(xfs_bmbt_get_startoff);
EXPORT_SYMBOL(xfs_bmbt_set_all);
EXPORT_SYMBOL(xfs_bmbt_set_allf);
EXPORT_SYMBOL(xfs_bmbt_set_blockcount);
EXPORT_SYMBOL(xfs_bmbt_set_startblock);
EXPORT_SYMBOL(xfs_bmbt_set_startoff);
EXPORT_SYMBOL(xfs_bmbt_set_state);
EXPORT_SYMBOL(xfs_buf_attach_iodone);
EXPORT_SYMBOL(xfs_bulkstat);
EXPORT_SYMBOL(xfs_bunmapi);
EXPORT_SYMBOL(xfs_bwrite);
EXPORT_SYMBOL(xfs_change_file_space);
EXPORT_SYMBOL(xfs_chashlist_zone);
EXPORT_SYMBOL(xfs_dev_is_read_only);
EXPORT_SYMBOL(xfs_dir_ialloc);
EXPORT_SYMBOL(xfs_error_report);
#ifdef DEBUG
EXPORT_SYMBOL(xfs_error_trap);
#endif
EXPORT_SYMBOL(xfs_file_last_byte);
EXPORT_SYMBOL(xfs_freesb);
EXPORT_SYMBOL(xfs_fs_cmn_err);
EXPORT_SYMBOL(xfs_highbit32);
EXPORT_SYMBOL(xfs_highbit64);
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IALLOC_BLOCKS)
EXPORT_SYMBOL(xfs_ialloc_blocks);
#endif
EXPORT_SYMBOL(xfs_idestroy);
EXPORT_SYMBOL(xfs_iextract);
EXPORT_SYMBOL(xfs_iflock);
EXPORT_SYMBOL(xfs_iflock_nowait);
EXPORT_SYMBOL(xfs_iflush);
EXPORT_SYMBOL(xfs_iflush_all);
EXPORT_SYMBOL(xfs_ifunlock);
EXPORT_SYMBOL(xfs_iget);
EXPORT_SYMBOL(xfs_igrow_start);
EXPORT_SYMBOL(xfs_igrow_finish);
EXPORT_SYMBOL(xfs_ilock);
EXPORT_SYMBOL(xfs_ilock_map_shared);
EXPORT_SYMBOL(xfs_ilock_nowait);
EXPORT_SYMBOL(xfs_inode_lock_init);
EXPORT_SYMBOL(xfs_iocore_inode_init);
EXPORT_SYMBOL(xfs_iocore_xfs);
EXPORT_SYMBOL(xfs_iomap);
EXPORT_SYMBOL(xfs_iput);
EXPORT_SYMBOL(xfs_iput_new);
EXPORT_SYMBOL(xfs_iread);
EXPORT_SYMBOL(xfs_iread_extents);
EXPORT_SYMBOL(xfs_itruncate_start);
EXPORT_SYMBOL(xfs_iunlock);
EXPORT_SYMBOL(xfs_iunlock_map_shared);
EXPORT_SYMBOL(xfs_itruncate_finish);
EXPORT_SYMBOL(xfs_log_force);
EXPORT_SYMBOL(xfs_log_force_umount);
EXPORT_SYMBOL(xfs_log_unmount_dealloc);
EXPORT_SYMBOL(xfs_log_unmount_write);
EXPORT_SYMBOL(xfs_mod_sb);
EXPORT_SYMBOL(xfs_mount_free);
EXPORT_SYMBOL(xfs_mount_init);
EXPORT_SYMBOL(xfs_mountfs);
EXPORT_SYMBOL(xfs_qm_dqcheck);
EXPORT_SYMBOL(xfs_readsb);
EXPORT_SYMBOL(xfs_read_buf);
EXPORT_SYMBOL(xfs_rwlock);
EXPORT_SYMBOL(xfs_rwunlock);
EXPORT_SYMBOL(xfs_setsize_buftarg);
EXPORT_SYMBOL(xfs_syncsub);
EXPORT_SYMBOL(xfs_trans_add_item);
EXPORT_SYMBOL(xfs_trans_alloc);
EXPORT_SYMBOL(xfs_trans_brelse);
EXPORT_SYMBOL(xfs_trans_cancel);
EXPORT_SYMBOL(xfs_trans_commit);
EXPORT_SYMBOL(xfs_trans_delete_ail);
EXPORT_SYMBOL(xfs_trans_dquot_buf);
EXPORT_SYMBOL(xfs_trans_find_item);
EXPORT_SYMBOL(xfs_trans_get_buf);
EXPORT_SYMBOL(xfs_trans_iget);
EXPORT_SYMBOL(xfs_trans_ihold);
EXPORT_SYMBOL(xfs_trans_ijoin);
EXPORT_SYMBOL(xfs_trans_log_buf);
EXPORT_SYMBOL(xfs_trans_log_inode);
EXPORT_SYMBOL(xfs_trans_mod_sb);
EXPORT_SYMBOL(xfs_trans_read_buf);
EXPORT_SYMBOL(xfs_trans_reserve);
EXPORT_SYMBOL(xfs_trans_unlocked_item);
EXPORT_SYMBOL(xfs_truncate_file);
EXPORT_SYMBOL(xfs_unmount_flush);
EXPORT_SYMBOL(xfs_unmountfs_writesb);
EXPORT_SYMBOL(xfs_vfsops);
EXPORT_SYMBOL(xfs_vnodeops);
EXPORT_SYMBOL(xfs_write_clear_setuid);
EXPORT_SYMBOL(xfs_zero_eof);
EXPORT_SYMBOL(xlog_recover_process_iunlinks);


#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BHVTOI)
EXPORT_SYMBOL(xfs_bhvtoi);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_VFSTOM)
EXPORT_SYMBOL(xfs_vfstom);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BM_MAXLEVELS)
EXPORT_SYMBOL(xfs_bm_maxlevels);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_INIT)
EXPORT_SYMBOL(xfs_bmap_init);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FILBLKS_MIN)
EXPORT_SYMBOL(xfs_filblks_min);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FSB_TO_DADDR)
EXPORT_SYMBOL(xfs_fsb_to_daddr);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_PTR)
EXPORT_SYMBOL(xfs_ifork_ptr);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_Q)
EXPORT_SYMBOL(xfs_ifork_q);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IN_MAXLEVELS)
EXPORT_SYMBOL(xfs_in_maxlevels);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ITOBHV)
EXPORT_SYMBOL(xfs_itobhv);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ITOV)
EXPORT_SYMBOL(xfs_itov);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MTOVFS)
EXPORT_SYMBOL(xfs_mtovfs);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_ADDQUOTA)
EXPORT_SYMBOL(xfs_sb_version_addquota);
#endif
#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASQUOTA)
EXPORT_SYMBOL(xfs_sb_version_hasquota);
#endif
