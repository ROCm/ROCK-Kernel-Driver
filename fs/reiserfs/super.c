/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 *
 * Trivial changes by Alan Cox to add the LFS fixes
 *
 * Trivial Changes:
 * Rights granted to Hans Reiser to redistribute under other terms providing
 * he accepts all liability including but not limited to patent, fitness
 * for purpose, and direct or indirect claims arising from failure to perform.
 *
 * NO WARRANTY
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/reiserfs_fs.h>
#include <linux/smp_lock.h>
#include <linux/locks.h>
#include <linux/init.h>

#define REISERFS_OLD_BLOCKSIZE 4096
#define REISERFS_SUPER_MAGIC_STRING_OFFSET_NJ 20



//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
void reiserfs_write_super (struct super_block * s)
{

  int dirty = 0 ;
  lock_kernel() ;
  if (!(s->s_flags & MS_RDONLY)) {
    dirty = flush_old_commits(s, 1) ;
  }
  s->s_dirt = dirty;
  unlock_kernel() ;
}

//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
void reiserfs_write_super_lockfs (struct super_block * s)
{

  int dirty = 0 ;
  struct reiserfs_transaction_handle th ;
  lock_kernel() ;
  if (!(s->s_flags & MS_RDONLY)) {
    journal_begin(&th, s, 1) ;
    reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s), 1);
    journal_mark_dirty(&th, s, SB_BUFFER_WITH_SB (s));
    reiserfs_block_writes(&th) ;
    journal_end(&th, s, 1) ;
  }
  s->s_dirt = dirty;
  unlock_kernel() ;
}

void reiserfs_unlockfs(struct super_block *s) {
  reiserfs_allow_writes(s) ;
}

//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
/* there should be no suspected recipients already. True and cautious
   bitmaps should not differ. We only have to free preserve list and
   write both bitmaps */
void reiserfs_put_super (struct super_block * s)
{
  int i;
  struct reiserfs_transaction_handle th ;
  
  /* change file system state to current state if it was mounted with read-write permissions */
  if (!(s->s_flags & MS_RDONLY)) {
    journal_begin(&th, s, 10) ;
    reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s), 1) ;
    s->u.reiserfs_sb.s_rs->s_state = le16_to_cpu (s->u.reiserfs_sb.s_mount_state);
    journal_mark_dirty(&th, s, SB_BUFFER_WITH_SB (s));
  }

  /* note, journal_release checks for readonly mount, and can decide not
  ** to do a journal_end
  */
  journal_release(&th, s) ;

  for (i = 0; i < SB_BMAP_NR (s); i ++)
    brelse (SB_AP_BITMAP (s)[i]);

  reiserfs_kfree (SB_AP_BITMAP (s), sizeof (struct buffer_head *) * SB_BMAP_NR (s), s);

  brelse (SB_BUFFER_WITH_SB (s));

  print_statistics (s);

  if (s->u.reiserfs_sb.s_kmallocs != 0) {
    reiserfs_warning ("vs-2004: reiserfs_put_super: aloocated memory left %d\n",
		      s->u.reiserfs_sb.s_kmallocs);
  }

  return;
}

struct super_operations reiserfs_sops = 
{
  read_inode: reiserfs_read_inode,
  read_inode2: reiserfs_read_inode2,
  write_inode: reiserfs_write_inode,
  dirty_inode: reiserfs_dirty_inode,
  delete_inode: reiserfs_delete_inode,
  put_super: reiserfs_put_super,
  write_super: reiserfs_write_super,
  write_super_lockfs: reiserfs_write_super_lockfs,
  unlockfs: reiserfs_unlockfs,
  statfs: reiserfs_statfs,
  remount_fs: reiserfs_remount,

  fh_to_dentry: reiserfs_fh_to_dentry,
  dentry_to_fh: reiserfs_dentry_to_fh,

};

/* this was (ext2)parse_options */
static int parse_options (char * options, unsigned long * mount_options, unsigned long * blocks)
{
    char * this_char;
    char * value;
  
    *blocks = 0;
    if (!options)
	/* use default configuration: create tails, journaling on, no
           conversion to newest format */
	return 1;
    for (this_char = strtok (options, ","); this_char != NULL; this_char = strtok (NULL, ",")) {
	if ((value = strchr (this_char, '=')) != NULL)
	    *value++ = 0;
	if (!strcmp (this_char, "notail")) {
	    set_bit (NOTAIL, mount_options);
	} else if (!strcmp (this_char, "conv")) {
	    // if this is set, we update super block such that
	    // the partition will not be mounable by 3.5.x anymore
	    set_bit (REISERFS_CONVERT, mount_options);
	} else if (!strcmp (this_char, "noborder")) {
				/* this is used for benchmarking
                                   experimental variations, it is not
                                   intended for users to use, only for
                                   developers who want to casually
                                   hack in something to test */
	    set_bit (REISERFS_NO_BORDER, mount_options);
	} else if (!strcmp (this_char, "no_unhashed_relocation")) {
	    set_bit (REISERFS_NO_UNHASHED_RELOCATION, mount_options);
	} else if (!strcmp (this_char, "hashed_relocation")) {
	    set_bit (REISERFS_HASHED_RELOCATION, mount_options);
	} else if (!strcmp (this_char, "test4")) {
	    set_bit (REISERFS_TEST4, mount_options);
	} else if (!strcmp (this_char, "nolog")) {
	    reiserfs_warning("reiserfs: nolog mount option not supported yet\n");
	} else if (!strcmp (this_char, "replayonly")) {
	    set_bit (REPLAYONLY, mount_options);
	} else if (!strcmp (this_char, "resize")) {
	    if (value && *value){
		*blocks = simple_strtoul (value, &value, 0);
	    } else {
	  	printk("reiserfs: resize option requires a value\n");
		return 0;
	    }
	} else if (!strcmp (this_char, "hash")) {
	    if (value && *value) {
		/* if they specify any hash option, we force detection
		** to make sure they aren't using the wrong hash
		*/
	        if (!strcmp(value, "rupasov")) {
		    set_bit (FORCE_RUPASOV_HASH, mount_options);
		    set_bit (FORCE_HASH_DETECT, mount_options);
		} else if (!strcmp(value, "tea")) {
		    set_bit (FORCE_TEA_HASH, mount_options);
		    set_bit (FORCE_HASH_DETECT, mount_options);
		} else if (!strcmp(value, "r5")) {
		    set_bit (FORCE_R5_HASH, mount_options);
		    set_bit (FORCE_HASH_DETECT, mount_options);
		} else if (!strcmp(value, "detect")) {
		    set_bit (FORCE_HASH_DETECT, mount_options);
		} else {
		    printk("reiserfs: invalid hash function specified\n") ;
		    return 0 ;
		}
	    } else {
	  	printk("reiserfs: hash option requires a value\n");
		return 0 ;
	    }
	} else {
	    printk ("reiserfs: Unrecognized mount option %s\n", this_char);
	    return 0;
	}
    }
    return 1;
}


int reiserfs_is_super(struct super_block *s) {
   return (s->s_dev != 0 && s->s_op == &reiserfs_sops) ;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_remount (struct super_block * s, int * flags, char * data)
{
  struct reiserfs_super_block * rs;
  struct reiserfs_transaction_handle th ;
  unsigned long blocks;
  unsigned long mount_options;

  rs = SB_DISK_SUPER_BLOCK (s);

  if (!parse_options(data, &mount_options, &blocks))
  	return 0;

  if(blocks) {
      int rc = reiserfs_resize(s, blocks);
      if (rc != 0)
	  return rc;
  }

  if ((unsigned long)(*flags & MS_RDONLY) == (s->s_flags & MS_RDONLY)) {
    /* there is nothing to do to remount read-only fs as read-only fs */
    return 0;
  }
  
  if (*flags & MS_RDONLY) {
    /* try to remount file system with read-only permissions */
    if (le16_to_cpu (rs->s_state) == REISERFS_VALID_FS || s->u.reiserfs_sb.s_mount_state != REISERFS_VALID_FS) {
      return 0;
    }

    journal_begin(&th, s, 10) ;
    /* Mounting a rw partition read-only. */
    reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s), 1) ;
    rs->s_state = cpu_to_le16 (s->u.reiserfs_sb.s_mount_state);
    journal_mark_dirty(&th, s, SB_BUFFER_WITH_SB (s));
    s->s_dirt = 0;
  } else {
    s->u.reiserfs_sb.s_mount_state = le16_to_cpu(rs->s_state) ;
    s->s_flags &= ~MS_RDONLY ; /* now it is safe to call journal_begin */
    journal_begin(&th, s, 10) ;

    /* Mount a partition which is read-only, read-write */
    reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s), 1) ;
    s->u.reiserfs_sb.s_mount_state = le16_to_cpu (rs->s_state);
    s->s_flags &= ~MS_RDONLY;
    rs->s_state = cpu_to_le16 (REISERFS_ERROR_FS);
    /* mark_buffer_dirty (SB_BUFFER_WITH_SB (s), 1); */
    journal_mark_dirty(&th, s, SB_BUFFER_WITH_SB (s));
    s->s_dirt = 0;
    s->u.reiserfs_sb.s_mount_state = REISERFS_VALID_FS ;
  }
  /* this will force a full flush of all journal lists */
  SB_JOURNAL(s)->j_must_wait = 1 ;
  journal_end(&th, s, 10) ;
  return 0;
}


static int read_bitmaps (struct super_block * s)
{
    int i, bmp, dl ;
    struct reiserfs_super_block * rs = SB_DISK_SUPER_BLOCK(s);

    SB_AP_BITMAP (s) = reiserfs_kmalloc (sizeof (struct buffer_head *) * le16_to_cpu (rs->s_bmap_nr), GFP_NOFS, s);
    if (SB_AP_BITMAP (s) == 0)
	return 1;
    memset (SB_AP_BITMAP (s), 0, sizeof (struct buffer_head *) * le16_to_cpu (rs->s_bmap_nr));

    /* reiserfs leaves the first 64k unused so that any partition
       labeling scheme currently used will have enough space. Then we
       need one block for the super.  -Hans */
    bmp = (REISERFS_DISK_OFFSET_IN_BYTES / s->s_blocksize) + 1;	/* first of bitmap blocks */
    SB_AP_BITMAP (s)[0] = reiserfs_bread (s->s_dev, bmp, s->s_blocksize);
    if(!SB_AP_BITMAP(s)[0])
	return 1;
    for (i = 1, bmp = dl = rs->s_blocksize * 8; i < le16_to_cpu (rs->s_bmap_nr); i ++) {
	SB_AP_BITMAP (s)[i] = reiserfs_bread (s->s_dev, bmp, s->s_blocksize);
	if (!SB_AP_BITMAP (s)[i])
	    return 1;
	bmp += dl;
    }

    return 0;
}

static int read_old_bitmaps (struct super_block * s)
{
  int i ;
  struct reiserfs_super_block * rs = SB_DISK_SUPER_BLOCK(s);
  int bmp1 = (REISERFS_OLD_DISK_OFFSET_IN_BYTES / s->s_blocksize) + 1;  /* first of bitmap blocks */

  /* read true bitmap */
  SB_AP_BITMAP (s) = reiserfs_kmalloc (sizeof (struct buffer_head *) * le16_to_cpu (rs->s_bmap_nr), GFP_NOFS, s);
  if (SB_AP_BITMAP (s) == 0)
    return 1;

  memset (SB_AP_BITMAP (s), 0, sizeof (struct buffer_head *) * le16_to_cpu (rs->s_bmap_nr));

  for (i = 0; i < le16_to_cpu (rs->s_bmap_nr); i ++) {
    SB_AP_BITMAP (s)[i] = reiserfs_bread (s->s_dev, bmp1 + i, s->s_blocksize);
    if (!SB_AP_BITMAP (s)[i])
      return 1;
  }

  return 0;
}

void check_bitmap (struct super_block * s)
{
  int i = 0;
  int free = 0;
  char * buf;

  while (i < SB_BLOCK_COUNT (s)) {
    buf = SB_AP_BITMAP (s)[i / (s->s_blocksize * 8)]->b_data;
    if (!reiserfs_test_le_bit (i % (s->s_blocksize * 8), buf))
      free ++;
    i ++;
  }

  if (free != SB_FREE_BLOCKS (s))
    reiserfs_warning ("vs-4000: check_bitmap: %d free blocks, must be %d\n",
		      free, SB_FREE_BLOCKS (s));
}



static int read_super_block (struct super_block * s, int size, int offset)
{
    struct buffer_head * bh;
    struct reiserfs_super_block * rs;


    bh = bread (s->s_dev, offset / size, size);
    if (!bh) {
	printk ("read_super_block: "
		"bread failed (dev %s, block %d, size %d)\n", 
		kdevname (s->s_dev), offset / size, size);
	return 1;
    }

    rs = (struct reiserfs_super_block *)bh->b_data;
    if (!is_reiserfs_magic_string (rs)) {
	printk ("read_super_block: "
		"can't find a reiserfs filesystem on (dev %s, block %lu, size %d)\n",
		kdevname(s->s_dev), bh->b_blocknr, size);
	brelse (bh);
	return 1;
    }

    //
    // ok, reiserfs signature (old or new) found in at the given offset
    //
    s->s_blocksize = le16_to_cpu (rs->s_blocksize);
    s->s_blocksize_bits = 0;
    while ((1 << s->s_blocksize_bits) != s->s_blocksize)
	s->s_blocksize_bits ++;

    brelse (bh);
    
    if (s->s_blocksize != size)
	set_blocksize (s->s_dev, s->s_blocksize);

    bh = bread (s->s_dev, offset / s->s_blocksize, s->s_blocksize);
    if (!bh) {
	printk ("read_super_block: "
		"bread failed (dev %s, block %d, size %d)\n", 
		kdevname (s->s_dev), offset / size, size);
	return 1;
    }
    
    rs = (struct reiserfs_super_block *)bh->b_data;
    if (!is_reiserfs_magic_string (rs) ||
	le16_to_cpu (rs->s_blocksize) != s->s_blocksize) {
	printk ("read_super_block: "
		"can't find a reiserfs filesystem on (dev %s, block %lu, size %d)\n",
		kdevname(s->s_dev), bh->b_blocknr, size);
	brelse (bh);
	return 1;
    }
    /* must check to be sure we haven't pulled an old format super out
    ** of the old format's log.  This is a kludge of a check, but it
    ** will work.  If block we've just read in is inside the
    ** journal for that super, it can't be valid.  
    */
    if (bh->b_blocknr >= le32_to_cpu(rs->s_journal_block) && 
	bh->b_blocknr < (le32_to_cpu(rs->s_journal_block) + JOURNAL_BLOCK_COUNT)) {
	brelse(bh) ;
	printk("super-459: read_super_block: "
	       "super found at block %lu is within its own log. "
	       "It must not be of this format type.\n", bh->b_blocknr) ;
	return 1 ;
    }
    SB_BUFFER_WITH_SB (s) = bh;
    SB_DISK_SUPER_BLOCK (s) = rs;
    s->s_op = &reiserfs_sops;

    /* new format is limited by the 32 bit wide i_blocks field, want to
    ** be one full block below that.
    */
    s->s_maxbytes = (512LL << 32) - s->s_blocksize ;
    return 0;
}



/* after journal replay, reread all bitmap and super blocks */
static int reread_meta_blocks(struct super_block *s) {
  int i ;
  ll_rw_block(READ, 1, &(SB_BUFFER_WITH_SB(s))) ;
  wait_on_buffer(SB_BUFFER_WITH_SB(s)) ;
  if (!buffer_uptodate(SB_BUFFER_WITH_SB(s))) {
    printk("reread_meta_blocks, error reading the super\n") ;
    return 1 ;
  }

  for (i = 0; i < SB_BMAP_NR(s) ; i++) {
    ll_rw_block(READ, 1, &(SB_AP_BITMAP(s)[i])) ;
    wait_on_buffer(SB_AP_BITMAP(s)[i]) ;
    if (!buffer_uptodate(SB_AP_BITMAP(s)[i])) {
      printk("reread_meta_blocks, error reading bitmap block number %d at %ld\n", i, SB_AP_BITMAP(s)[i]->b_blocknr) ;
      return 1 ;
    }
  }
  return 0 ;

}


/////////////////////////////////////////////////////
// hash detection stuff


// if root directory is empty - we set default - Yura's - hash and
// warn about it
// FIXME: we look for only one name in a directory. If tea and yura
// bith have the same value - we ask user to send report to the
// mailing list
__u32 find_hash_out (struct super_block * s)
{
    int retval;
    struct inode * inode;
    struct cpu_key key;
    INITIALIZE_PATH (path);
    struct reiserfs_dir_entry de;
    __u32 hash = DEFAULT_HASH;

    inode = s->s_root->d_inode;

    while (1) {
	make_cpu_key (&key, inode, ~0, TYPE_DIRENTRY, 3);
	retval = search_by_entry_key (s, &key, &path, &de);
	if (retval == IO_ERROR) {
	    pathrelse (&path);
	    return UNSET_HASH ;
	}
	if (retval == NAME_NOT_FOUND)
	    de.de_entry_num --;
	set_de_name_and_namelen (&de);
	if (le32_to_cpu (de.de_deh[de.de_entry_num].deh_offset) == DOT_DOT_OFFSET) {
	    /* allow override in this case */
	    if (reiserfs_rupasov_hash(s)) {
		hash = YURA_HASH ;
	    }
	    reiserfs_warning("reiserfs: FS seems to be empty, autodetect "
	                     "is using the default hash\n");
	    break;
	}
	if (GET_HASH_VALUE(yura_hash (de.de_name, de.de_namelen)) == 
	    GET_HASH_VALUE(keyed_hash (de.de_name, de.de_namelen))) {
	    reiserfs_warning ("reiserfs: Could not detect hash function "
			      "please mount with -o hash={tea,rupasov,r5}\n") ;
	    hash = UNSET_HASH ;
	    break;
	}
	if (GET_HASH_VALUE(le32_to_cpu(de.de_deh[de.de_entry_num].deh_offset))==
	    GET_HASH_VALUE (yura_hash (de.de_name, de.de_namelen)))
	    hash = YURA_HASH;
	else
	    hash = TEA_HASH;
	break;
    }

    pathrelse (&path);
    return hash;
}

// finds out which hash names are sorted with
static int what_hash (struct super_block * s)
{
    __u32 code;

    code = le32_to_cpu (s->u.reiserfs_sb.s_rs->s_hash_function_code);

    /* reiserfs_hash_detect() == true if any of the hash mount options
    ** were used.  We must check them to make sure the user isn't
    ** using a bad hash value
    */
    if (code == UNSET_HASH || reiserfs_hash_detect(s))
	code = find_hash_out (s);

    if (code != UNSET_HASH && reiserfs_hash_detect(s)) {
	/* detection has found the hash, and we must check against the 
	** mount options 
	*/
	if (reiserfs_rupasov_hash(s) && code != YURA_HASH) {
	    printk("REISERFS: Error, tea hash detected, "
		   "unable to force rupasov hash\n") ;
	    code = UNSET_HASH ;
	} else if (reiserfs_tea_hash(s) && code != TEA_HASH) {
	    printk("REISERFS: Error, rupasov hash detected, "
		   "unable to force tea hash\n") ;
	    code = UNSET_HASH ;
	} else if (reiserfs_r5_hash(s) && code != R5_HASH) {
	    printk("REISERFS: Error, r5 hash detected, "
		   "unable to force r5 hash\n") ;
	    code = UNSET_HASH ;
	} 
    } else { 
        /* find_hash_out was not called or could not determine the hash */
	if (reiserfs_rupasov_hash(s)) {
	    code = YURA_HASH ;
	} else if (reiserfs_tea_hash(s)) {
	    code = TEA_HASH ;
	} else if (reiserfs_r5_hash(s)) {
	    code = R5_HASH ;
	} 
    }

    /* if we are mounted RW, and we have a new valid hash code, update 
    ** the super
    */
    if (code != UNSET_HASH && 
	!(s->s_flags & MS_RDONLY) && 
        code != le32_to_cpu (s->u.reiserfs_sb.s_rs->s_hash_function_code)) {
        s->u.reiserfs_sb.s_rs->s_hash_function_code = cpu_to_le32(code) ;
    }
    return code;
}

// return pointer to appropriate function
static hashf_t hash_function (struct super_block * s)
{
    switch (what_hash (s)) {
    case TEA_HASH:
	reiserfs_warning ("Using tea hash to sort names\n");
	return keyed_hash;
    case YURA_HASH:
	reiserfs_warning ("Using rupasov hash to sort names\n");
	return yura_hash;
    case R5_HASH:
	reiserfs_warning ("Using r5 hash to sort names\n");
	return r5_hash;
    }
    return NULL;
}

// this is used to set up correct value for old partitions
int function2code (hashf_t func)
{
    if (func == keyed_hash)
	return TEA_HASH;
    if (func == yura_hash)
	return YURA_HASH;
    if (func == r5_hash)
	return R5_HASH;

    BUG() ; // should never happen 

    return 0;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
struct super_block * reiserfs_read_super (struct super_block * s, void * data, int silent)
{
    int size;
    struct inode *root_inode;
    kdev_t dev = s->s_dev;
    int j;
    extern int *blksize_size[];
    struct reiserfs_transaction_handle th ;
    int old_format = 0;
    unsigned long blocks;
    int jinit_done = 0 ;
    struct reiserfs_iget4_args args ;


    memset (&s->u.reiserfs_sb, 0, sizeof (struct reiserfs_sb_info));

    if (parse_options ((char *) data, &(s->u.reiserfs_sb.s_mount_opt), &blocks) == 0) {
	return NULL;
    }

    if (blocks) {
  	printk("reserfs: resize option for remount only\n");
	return NULL;
    }	

    if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)] != 0) {
	/* as blocksize is set for partition we use it */
	size = blksize_size[MAJOR(dev)][MINOR(dev)];
    } else {
	size = BLOCK_SIZE;
	set_blocksize (s->s_dev, BLOCK_SIZE);
    }

    /* read block (64-th 1k block), which can contain reiserfs super block */
    if (read_super_block (s, size, REISERFS_DISK_OFFSET_IN_BYTES)) {
	// try old format (undistributed bitmap, super block in 8-th 1k block of a device)
	if (read_super_block (s, size, REISERFS_OLD_DISK_OFFSET_IN_BYTES)) 
	    goto error;
	else
	    old_format = 1;
    }

    s->u.reiserfs_sb.s_mount_state = le16_to_cpu (SB_DISK_SUPER_BLOCK (s)->s_state); /* journal victim */
    s->u.reiserfs_sb.s_mount_state = REISERFS_VALID_FS ;

    if (old_format ? read_old_bitmaps(s) : read_bitmaps(s)) { 
	printk ("reiserfs_read_super: unable to read bitmap\n");
	goto error;
    }
#ifdef CONFIG_REISERFS_CHECK
    printk("reiserfs:warning: CONFIG_REISERFS_CHECK is set ON\n");
    printk("reiserfs:warning: - it is slow mode for debugging.\n");
#endif

    // set_device_ro(s->s_dev, 1) ;
    if (journal_init(s)) {
	printk("reiserfs_read_super: unable to initialize journal space\n") ;
	goto error ;
    } else {
	jinit_done = 1 ; /* once this is set, journal_release must be called
			 ** if we error out of the mount 
			 */
    }
    if (reread_meta_blocks(s)) {
	printk("reiserfs_read_super: unable to reread meta blocks after journal init\n") ;
	goto error ;
    }

    if (replay_only (s))
	goto error;

    if (is_read_only(s->s_dev) && !(s->s_flags & MS_RDONLY)) {
        printk("clm-7000: Detected readonly device, marking FS readonly\n") ;
	s->s_flags |= MS_RDONLY ;
    }
    args.objectid = REISERFS_ROOT_PARENT_OBJECTID ;
    root_inode = iget4 (s, REISERFS_ROOT_OBJECTID, 0, (void *)(&args));
    if (!root_inode) {
	printk ("reiserfs_read_super: get root inode failed\n");
	goto error;
    }

    s->s_root = d_alloc_root(root_inode);  
    if (!s->s_root) {
	iput(root_inode);
	goto error;
    }

    // define and initialize hash function
    s->u.reiserfs_sb.s_hash_function = hash_function (s);
    if (s->u.reiserfs_sb.s_hash_function == NULL) {
      dput(s->s_root) ;
      s->s_root = NULL ;
      goto error ;
    }

    if (!(s->s_flags & MS_RDONLY)) {
	struct reiserfs_super_block * rs = SB_DISK_SUPER_BLOCK (s);
	int old_magic;

	old_magic = strncmp (rs->s_magic,  REISER2FS_SUPER_MAGIC_STRING, 
			     strlen ( REISER2FS_SUPER_MAGIC_STRING));
	if( old_magic && le16_to_cpu(rs->s_version) != 0 ) {
	  dput(s->s_root) ;
	  s->s_root = NULL ;
	  reiserfs_warning("reiserfs: wrong version/magic combination in the super-block\n") ;
	  goto error ;
	}

	journal_begin(&th, s, 1) ;
	reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s), 1) ;

	rs->s_state = cpu_to_le16 (REISERFS_ERROR_FS);

        if ( old_magic ) {
	    // filesystem created under 3.5.x found
	    if (!old_format_only (s)) {
		reiserfs_warning("reiserfs: converting 3.5.x filesystem to the new format\n") ;
		// after this 3.5.x will not be able to mount this partition
		memcpy (rs->s_magic, REISER2FS_SUPER_MAGIC_STRING, 
			sizeof (REISER2FS_SUPER_MAGIC_STRING));

		reiserfs_convert_objectid_map_v1(s) ;
	    } else {
		reiserfs_warning("reiserfs: using 3.5.x disk format\n") ;
	    }
	} else {
	    // new format found
	    set_bit (REISERFS_CONVERT, &(s->u.reiserfs_sb.s_mount_opt));
	}

	// mark hash in super block: it could be unset. overwrite should be ok
	rs->s_hash_function_code = cpu_to_le32 (function2code (s->u.reiserfs_sb.s_hash_function));

	journal_mark_dirty(&th, s, SB_BUFFER_WITH_SB (s));
	journal_end(&th, s, 1) ;
	s->s_dirt = 0;
    } else {
	struct reiserfs_super_block * rs = SB_DISK_SUPER_BLOCK (s);
	if (strncmp (rs->s_magic,  REISER2FS_SUPER_MAGIC_STRING, 
		     strlen ( REISER2FS_SUPER_MAGIC_STRING))) {
	    reiserfs_warning("reiserfs: using 3.5.x disk format\n") ;
	}
    }

    init_waitqueue_head (&(s->u.reiserfs_sb.s_wait));

    printk("%s\n", reiserfs_get_version_string()) ;
    return s;

 error:
    if (jinit_done) { /* kill the commit thread, free journal ram */
	journal_release_error(NULL, s) ;
    }
    if (SB_DISK_SUPER_BLOCK (s)) {
	for (j = 0; j < SB_BMAP_NR (s); j ++) {
	    if (SB_AP_BITMAP (s))
		brelse (SB_AP_BITMAP (s)[j]);
	}
	if (SB_AP_BITMAP (s))
	    reiserfs_kfree (SB_AP_BITMAP (s), sizeof (struct buffer_head *) * SB_BMAP_NR (s), s);
    }
    if (SB_BUFFER_WITH_SB (s))
	brelse(SB_BUFFER_WITH_SB (s));

    return NULL;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_statfs (struct super_block * s, struct statfs * buf)
{
  struct reiserfs_super_block * rs = SB_DISK_SUPER_BLOCK (s);
  
				/* changed to accomodate gcc folks.*/
  buf->f_type =  REISERFS_SUPER_MAGIC;
  buf->f_bsize = le32_to_cpu (s->s_blocksize);
  buf->f_blocks = le32_to_cpu (rs->s_block_count) - le16_to_cpu (rs->s_bmap_nr) - 1;
  buf->f_bfree = le32_to_cpu (rs->s_free_blocks);
  buf->f_bavail = buf->f_bfree;
  buf->f_files = -1;
  buf->f_ffree = -1;
  buf->f_namelen = (REISERFS_MAX_NAME_LEN (s->s_blocksize));
  return 0;
}

static DECLARE_FSTYPE_DEV(reiserfs_fs_type,"reiserfs",reiserfs_read_super);

//
// this is exactly what 2.3.99-pre9's init_ext2_fs is
//
static int __init init_reiserfs_fs (void)
{
        return register_filesystem(&reiserfs_fs_type);
}

EXPORT_NO_SYMBOLS;

//
// this is exactly what 2.3.99-pre9's init_ext2_fs is
//
static void __exit exit_reiserfs_fs(void)
{
        unregister_filesystem(&reiserfs_fs_type);
}

module_init(init_reiserfs_fs) ;
module_exit(exit_reiserfs_fs) ;



