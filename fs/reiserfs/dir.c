/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/reiserfs_fs.h>
#include <linux/stat.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>

extern struct key  MIN_KEY;

static int reiserfs_readdir (struct file *, void *, filldir_t);
int reiserfs_dir_fsync(struct file *filp, struct dentry *dentry, int datasync) ;

struct file_operations reiserfs_dir_operations = {
    read:	generic_read_dir,
    readdir:	reiserfs_readdir,
    fsync:	reiserfs_dir_fsync,
};

/*
 * directories can handle most operations...
 */
struct inode_operations reiserfs_dir_inode_operations = {
  //&reiserfs_dir_operations,	/* default_file_ops */
    create:	reiserfs_create,
    lookup:	reiserfs_lookup,
    link:	reiserfs_link,
    unlink:	reiserfs_unlink,
    symlink:	reiserfs_symlink,
    mkdir:	reiserfs_mkdir,
    rmdir:	reiserfs_rmdir,
    mknod:	reiserfs_mknod,
    rename:	reiserfs_rename,
};

int reiserfs_dir_fsync(struct file *filp, struct dentry *dentry, int datasync) {
  int ret = 0 ;
  int windex ;
  struct reiserfs_transaction_handle th ;

  lock_kernel();

  journal_begin(&th, dentry->d_inode->i_sb, 1) ;
  windex = push_journal_writer("dir_fsync") ;
  reiserfs_prepare_for_journal(th.t_super, SB_BUFFER_WITH_SB(th.t_super), 1) ;
  journal_mark_dirty(&th, dentry->d_inode->i_sb, SB_BUFFER_WITH_SB (dentry->d_inode->i_sb)) ;
  pop_journal_writer(windex) ;
  journal_end_sync(&th, dentry->d_inode->i_sb, 1) ;

  unlock_kernel();

  return ret ;
}


#define store_ih(where,what) copy_item_head (where, what)

//
static int reiserfs_readdir (struct file * filp, void * dirent, filldir_t filldir)
{
    struct inode *inode = filp->f_dentry->d_inode;
    struct cpu_key pos_key;	/* key of current position in the directory (key of directory entry) */
    INITIALIZE_PATH (path_to_entry);
    struct buffer_head * bh;
    int item_num, entry_num;
    struct key * rkey;
    struct item_head * ih, tmp_ih;
    int search_res;
    char * local_buf;
    loff_t next_pos;
    char small_buf[32] ; /* avoid kmalloc if we can */
    struct reiserfs_dir_entry de;


    reiserfs_check_lock_depth("readdir") ;

    /* form key for search the next directory entry using f_pos field of
       file structure */
    make_cpu_key (&pos_key, inode, (filp->f_pos) ? (filp->f_pos) : DOT_OFFSET,
		  TYPE_DIRENTRY, 3);
    next_pos = cpu_key_k_offset (&pos_key);

    /*  reiserfs_warning ("reiserfs_readdir 1: f_pos = %Ld\n", filp->f_pos);*/

    while (1) {
    research:
	/* search the directory item, containing entry with specified key */
	search_res = search_by_entry_key (inode->i_sb, &pos_key, &path_to_entry, &de);
	if (search_res == IO_ERROR) {
	    // FIXME: we could just skip part of directory which could
	    // not be read
	    return -EIO;
	}
	entry_num = de.de_entry_num;
	bh = de.de_bh;
	item_num = de.de_item_num;
	ih = de.de_ih;
	store_ih (&tmp_ih, ih);
		
	/* we must have found item, that is item of this directory, */
	RFALSE( COMP_SHORT_KEYS (&(ih->ih_key), &pos_key),
		"vs-9000: found item %h does not match to dir we readdir %k",
		ih, &pos_key);
	RFALSE( item_num > B_NR_ITEMS (bh) - 1,
		"vs-9005 item_num == %d, item amount == %d", 
		item_num, B_NR_ITEMS (bh));
      
	/* and entry must be not more than number of entries in the item */
	RFALSE( I_ENTRY_COUNT (ih) < entry_num,
		"vs-9010: entry number is too big %d (%d)", 
		entry_num, I_ENTRY_COUNT (ih));

	if (search_res == POSITION_FOUND || entry_num < I_ENTRY_COUNT (ih)) {
	    /* go through all entries in the directory item beginning from the entry, that has been found */
	    struct reiserfs_de_head * deh = B_I_DEH (bh, ih) + entry_num;

	    for (; entry_num < I_ENTRY_COUNT (ih); entry_num ++, deh ++) {
		int d_reclen;
		char * d_name;
		off_t d_off;
		ino_t d_ino;

		if (!de_visible (deh))
		    /* it is hidden entry */
		    continue;
		d_reclen = entry_length (bh, ih, entry_num);
		d_name = B_I_DEH_ENTRY_FILE_NAME (bh, ih, deh);
		if (!d_name[d_reclen - 1])
		    d_reclen = strlen (d_name);
	
		if (d_reclen > REISERFS_MAX_NAME_LEN(inode->i_sb->s_blocksize)){
		    /* too big to send back to VFS */
		    continue ;
		}
		d_off = deh_offset (deh);
		filp->f_pos = d_off ;
		d_ino = deh_objectid (deh);
		if (d_reclen <= 32) {
		  local_buf = small_buf ;
		} else {
		    local_buf = kmalloc(d_reclen, GFP_NOFS) ;
		    if (!local_buf) {
			pathrelse (&path_to_entry);
			return -ENOMEM ;
		    }
		    if (item_moved (&tmp_ih, &path_to_entry)) {
			kfree(local_buf) ;
			goto research;
		    }
		}
		// Note, that we copy name to user space via temporary
		// buffer (local_buf) because filldir will block if
		// user space buffer is swapped out. At that time
		// entry can move to somewhere else
		memcpy (local_buf, d_name, d_reclen);
		if (filldir (dirent, local_buf, d_reclen, d_off, d_ino, 
		             DT_UNKNOWN) < 0) {
		    if (local_buf != small_buf) {
			kfree(local_buf) ;
		    }
		    goto end;
		}
		if (local_buf != small_buf) {
		    kfree(local_buf) ;
		}

		// next entry should be looked for with such offset
		next_pos = deh_offset (deh) + 1;

		if (item_moved (&tmp_ih, &path_to_entry)) {
		    goto research;
		}
	    } /* for */
	}

	if (item_num != B_NR_ITEMS (bh) - 1)
	    // end of directory has been reached
	    goto end;

	/* item we went through is last item of node. Using right
	   delimiting key check is it directory end */
	rkey = get_rkey (&path_to_entry, inode->i_sb);
	if (! comp_le_keys (rkey, &MIN_KEY)) {
	    /* set pos_key to key, that is the smallest and greater
	       that key of the last entry in the item */
	    set_cpu_key_k_offset (&pos_key, next_pos);
	    continue;
	}

	if ( COMP_SHORT_KEYS (rkey, &pos_key)) {
	    // end of directory has been reached
	    goto end;
	}
	
	/* directory continues in the right neighboring block */
	set_cpu_key_k_offset (&pos_key, le_key_k_offset (ITEM_VERSION_1, rkey));

    } /* while */


 end:
    // FIXME: ext2_readdir does not reset f_pos
    filp->f_pos = next_pos;
    pathrelse (&path_to_entry);
    reiserfs_check_path(&path_to_entry) ;
    return 0;
}





















