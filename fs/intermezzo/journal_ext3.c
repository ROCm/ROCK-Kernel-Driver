
/*
 * Intermezzo. (C) 1998 Peter J. Braam
 * Intermezzo. (C) 2000 Red Hat, Inc.
 * Intermezzo. (C) 2000 Los Alamos National Laboratory
 * Intermezzo. (C) 2000 TurboLinux, Inc.
 * Intermezzo. (C) 2001 Mountain View Data, Inc.
 */

#include <linux/types.h>
#include <linux/param.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#if defined(CONFIG_EXT3_FS) || defined (CONFIG_EXT3_FS_MODULE)
#include <linux/jbd.h>
#include <linux/ext3_fs.h>
#include <linux/ext3_jbd.h>
#endif

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>
#include <linux/intermezzo_kml.h>

#if defined(CONFIG_EXT3_FS) || defined (CONFIG_EXT3_FS_MODULE)

#define MAX_PATH_BLOCKS(inode) (PATH_MAX >> EXT3_BLOCK_SIZE_BITS((inode)->i_sb))
#define MAX_NAME_BLOCKS(inode) (NAME_MAX >> EXT3_BLOCK_SIZE_BITS((inode)->i_sb))

/* space requirements: 
   presto_do_truncate: 
        used to truncate the KML forward to next fset->chunksize boundary
          - zero partial block
          - update inode
   presto_write_record: 
        write header (< one block) 
        write one path (< MAX_PATHLEN) 
        possibly write another path (< MAX_PATHLEN)
        write suffix (< one block) 
   presto_update_last_rcvd
        write one block
*/

static loff_t presto_e3_freespace(struct presto_cache *cache,
                                         struct super_block *sb)
{
        loff_t freebl = le32_to_cpu(sb->u.ext3_sb.s_es->s_free_blocks_count);
        loff_t avail =   freebl - 
                le32_to_cpu(sb->u.ext3_sb.s_es->s_r_blocks_count);
        return (avail <<  EXT3_BLOCK_SIZE_BITS(sb));
}

/* start the filesystem journal operations */
static void *presto_e3_trans_start(struct presto_file_set *fset, 
                                   struct inode *inode, 
                                   int op)
{
        int jblocks;
        int trunc_blks, one_path_blks, extra_path_blks, 
                extra_name_blks, lml_blks; 
        __u32 avail_kmlblocks;
        handle_t *handle;

        if ( presto_no_journal(fset) ||
             strcmp(fset->fset_cache->cache_type, "ext3"))
          {
            CDEBUG(D_JOURNAL, "got cache_type \"%s\"\n",
                   fset->fset_cache->cache_type);
            return NULL;
          }

        avail_kmlblocks = inode->i_sb->u.ext3_sb.s_es->s_free_blocks_count;
        
        if ( avail_kmlblocks < 3 ) {
                return ERR_PTR(-ENOSPC);
        }
        
        if (  (op != PRESTO_OP_UNLINK && op != PRESTO_OP_RMDIR)
              && avail_kmlblocks < 6 ) {
                return ERR_PTR(-ENOSPC);
        }            

        /* Need journal space for:
             at least three writes to KML (two one block writes, one a path) 
             possibly a second name (unlink, rmdir)
             possibly a second path (symlink, rename)
             a one block write to the last rcvd file 
        */

        trunc_blks = EXT3_DATA_TRANS_BLOCKS + 1; 
        one_path_blks = 4*EXT3_DATA_TRANS_BLOCKS + MAX_PATH_BLOCKS(inode) + 3;
        lml_blks = 4*EXT3_DATA_TRANS_BLOCKS + MAX_PATH_BLOCKS(inode) + 2;
        extra_path_blks = EXT3_DATA_TRANS_BLOCKS + MAX_PATH_BLOCKS(inode); 
        extra_name_blks = EXT3_DATA_TRANS_BLOCKS + MAX_NAME_BLOCKS(inode); 

        /* additional blocks appear for "two pathname" operations
           and operations involving the LML records 
        */
        switch (op) {
        case PRESTO_OP_TRUNC:
                jblocks = one_path_blks + extra_name_blks + trunc_blks
                        + EXT3_DELETE_TRANS_BLOCKS; 
                break;
        case PRESTO_OP_RELEASE:
                /* 
                jblocks = one_path_blks + lml_blks + 2*trunc_blks; 
                */
                jblocks = one_path_blks; 
                break;
        case PRESTO_OP_SETATTR:
                jblocks = one_path_blks + trunc_blks + 1 ; 
                break;
        case PRESTO_OP_CREATE:
                jblocks = one_path_blks + trunc_blks 
                        + EXT3_DATA_TRANS_BLOCKS + 3 + 2; 
                break;
        case PRESTO_OP_LINK:
                jblocks = one_path_blks + trunc_blks 
                        + EXT3_DATA_TRANS_BLOCKS + 2; 
                break;
        case PRESTO_OP_UNLINK:
                jblocks = one_path_blks + extra_name_blks + trunc_blks
                        + EXT3_DELETE_TRANS_BLOCKS + 2; 
                break;
        case PRESTO_OP_SYMLINK:
                jblocks = one_path_blks + extra_path_blks + trunc_blks
                        + EXT3_DATA_TRANS_BLOCKS + 5; 
                break;
        case PRESTO_OP_MKDIR:
                jblocks = one_path_blks + trunc_blks
                        + EXT3_DATA_TRANS_BLOCKS + 4 + 2;
                break;
        case PRESTO_OP_RMDIR:
                jblocks = one_path_blks + extra_name_blks + trunc_blks
                        + EXT3_DELETE_TRANS_BLOCKS + 1; 
                break;
        case PRESTO_OP_MKNOD:
                jblocks = one_path_blks + trunc_blks + 
                        EXT3_DATA_TRANS_BLOCKS + 3 + 2;
                break;
        case PRESTO_OP_RENAME:
                jblocks = one_path_blks + extra_path_blks + trunc_blks + 
                        2 * EXT3_DATA_TRANS_BLOCKS + 2 + 3;
                break;
        case PRESTO_OP_WRITE:
                jblocks = one_path_blks; 
                /*  add this when we can wrap our transaction with 
                    that of ext3_file_write (ordered writes)
                    +  EXT3_DATA_TRANS_BLOCKS;
                */
                break;
        default:
                CDEBUG(D_JOURNAL, "invalid operation %d for journal\n", op);
                return NULL;
        }

        CDEBUG(D_JOURNAL, "creating journal handle (%d blocks)\n", jblocks);
        /* journal_start/stop does not do its own locking while updating
         * the handle/transaction information. Hence we create our own
         * critical section to protect these calls. -SHP
         */
        lock_kernel();
        handle = journal_start(EXT3_JOURNAL(inode), jblocks);
        unlock_kernel();
        return handle;
}

void presto_e3_trans_commit(struct presto_file_set *fset, void *handle)
{
        if ( presto_no_journal(fset) || !handle)
                return;

        /* See comments before journal_start above. -SHP */
        lock_kernel();
        journal_stop(handle);
        unlock_kernel();
}

void presto_e3_journal_file_data(struct inode *inode)
{
#ifdef EXT3_JOURNAL_DATA_FL
        inode->u.ext3_i.i_flags |= EXT3_JOURNAL_DATA_FL;
#else
#warning You must have a facility to enable journaled writes for recovery!
#endif
}

struct journal_ops presto_ext3_journal_ops = {
        tr_avail: presto_e3_freespace,
        tr_start:  presto_e3_trans_start,
        tr_commit: presto_e3_trans_commit,
        tr_journal_data: presto_e3_journal_file_data
};

#endif /* CONFIG_EXT3_FS */
