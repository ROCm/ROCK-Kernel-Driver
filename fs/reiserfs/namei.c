/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 *
 * Trivial changes by Alan Cox to remove EHASHCOLLISION for compatibility
 *
 * Trivial Changes:
 * Rights granted to Hans Reiser to redistribute under other terms providing
 * he accepts all liability including but not limited to patent, fitness
 * for purpose, and direct or indirect claims arising from failure to perform.
 *
 * NO WARRANTY
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/reiserfs_fs.h>
#include <linux/smp_lock.h>

				/* there should be an overview right
                                   here, as there should be in every
                                   conceptual grouping of code.  This
                                   should be combined with dir.c and
                                   called dir.c (naming will become
                                   too large to be called one file in
                                   a few years), stop senselessly
                                   imitating the incoherent
                                   structuring of code used by other
                                   filesystems.  */

#define INC_DIR_INODE_NLINK(i) if (i->i_nlink != 1) { i->i_nlink++; if (i->i_nlink >= REISERFS_LINK_MAX) i->i_nlink=1; }
#define DEC_DIR_INODE_NLINK(i) if (i->i_nlink != 1) i->i_nlink--;

// directory item contains array of entry headers. This performs
// binary search through that array
static int bin_search_in_dir_item (struct reiserfs_dir_entry * de, loff_t off)
{
    struct item_head * ih = de->de_ih;
    struct reiserfs_de_head * deh = de->de_deh;
    int rbound, lbound, j;

    lbound = 0;
    rbound = I_ENTRY_COUNT (ih) - 1;

    for (j = (rbound + lbound) / 2; lbound <= rbound; j = (rbound + lbound) / 2) {
	if (off < deh_offset (deh + j)) {
	    rbound = j - 1;
	    continue;
	}
	if (off > deh_offset (deh + j)) {
	    lbound = j + 1;
	    continue;
	}
	// this is not name found, but matched third key component
	de->de_entry_num = j;
	return NAME_FOUND;
    }

    de->de_entry_num = lbound;
    return NAME_NOT_FOUND;
}


// comment?  maybe something like set de to point to what the path points to?
static inline void set_de_item_location (struct reiserfs_dir_entry * de, struct path * path)
{
    de->de_bh = get_last_bh (path);
    de->de_ih = get_ih (path);
    de->de_deh = B_I_DEH (de->de_bh, de->de_ih);
    de->de_item_num = PATH_LAST_POSITION (path);
} 


// de_bh, de_ih, de_deh (points to first element of array), de_item_num is set
inline void set_de_name_and_namelen (struct reiserfs_dir_entry * de)
{
    struct reiserfs_de_head * deh = de->de_deh + de->de_entry_num;

    if (de->de_entry_num >= ih_entry_count (de->de_ih))
	BUG ();

    de->de_entrylen = entry_length (de->de_bh, de->de_ih, de->de_entry_num);
    de->de_namelen = de->de_entrylen - (de_with_sd (deh) ? SD_SIZE : 0);
    de->de_name = B_I_PITEM (de->de_bh, de->de_ih) + le16_to_cpu (deh->deh_location);
    if (de->de_name[de->de_namelen - 1] == 0)
	de->de_namelen = strlen (de->de_name);
}


// what entry points to
static inline void set_de_object_key (struct reiserfs_dir_entry * de)
{
    if (de->de_entry_num >= ih_entry_count (de->de_ih))
	BUG ();
    de->de_dir_id = le32_to_cpu (de->de_deh[de->de_entry_num].deh_dir_id);
    de->de_objectid = le32_to_cpu (de->de_deh[de->de_entry_num].deh_objectid);
}


static inline void store_de_entry_key (struct reiserfs_dir_entry * de)
{
    struct reiserfs_de_head * deh = de->de_deh + de->de_entry_num;

    if (de->de_entry_num >= ih_entry_count (de->de_ih))
	BUG ();

    /* store key of the found entry */
    de->de_entry_key.version = ITEM_VERSION_1;
    de->de_entry_key.on_disk_key.k_dir_id = le32_to_cpu (de->de_ih->ih_key.k_dir_id);
    de->de_entry_key.on_disk_key.k_objectid = le32_to_cpu (de->de_ih->ih_key.k_objectid);
    set_cpu_key_k_offset (&(de->de_entry_key), deh_offset (deh));
    set_cpu_key_k_type (&(de->de_entry_key), TYPE_DIRENTRY);
}


/* We assign a key to each directory item, and place multiple entries
in a single directory item.  A directory item has a key equal to the
key of the first directory entry in it.

This function first calls search_by_key, then, if item whose first
entry matches is not found it looks for the entry inside directory
item found by search_by_key. Fills the path to the entry, and to the
entry position in the item 

*/

/* The function is NOT SCHEDULE-SAFE! */
int search_by_entry_key (struct super_block * sb, struct cpu_key * key,
			 struct path * path, struct reiserfs_dir_entry * de)
{
    int retval;

    retval = search_item (sb, key, path);
    switch (retval) {
    case ITEM_NOT_FOUND:
	if (!PATH_LAST_POSITION (path)) {
	    reiserfs_warning ("vs-7000: search_by_entry_key: search_by_key returned item position == 0");
	    pathrelse(path) ;
	    return IO_ERROR ;
	}
	PATH_LAST_POSITION (path) --;

    case ITEM_FOUND:
	break;

    case IO_ERROR:
	return retval;

    default:
	pathrelse (path);
	reiserfs_warning ("vs-7002: search_by_entry_key: no path to here");
	return IO_ERROR;
    }

    set_de_item_location (de, path);

#ifdef CONFIG_REISERFS_CHECK
    if (!is_direntry_le_ih (de->de_ih) || 
	COMP_SHORT_KEYS (&(de->de_ih->ih_key), key)) {
	print_block (de->de_bh, 0, -1, -1);
	reiserfs_panic (sb, "vs-7005: search_by_entry_key: found item %h is not directory item or "
			"does not belong to the same directory as key %k", de->de_ih, key);
    }
#endif /* CONFIG_REISERFS_CHECK */

    /* binary search in directory item by third componen t of the
       key. sets de->de_entry_num of de */
    retval = bin_search_in_dir_item (de, cpu_key_k_offset (key));
    path->pos_in_item = de->de_entry_num;
    if (retval != NAME_NOT_FOUND) {
	// ugly, but rename needs de_bh, de_deh, de_name, de_namelen, de_objectid set
	set_de_name_and_namelen (de);
	set_de_object_key (de);
    }
    return retval;
}



/* Keyed 32-bit hash function using TEA in a Davis-Meyer function */

/* The third component is hashed, and you can choose from more than
   one hash function.  Per directory hashes are not yet implemented
   but are thought about. This function should be moved to hashes.c
   Jedi, please do so.  -Hans */

static __u32 get_third_component (struct super_block * s, 
				  const char * name, int len)
{
    __u32 res;

    if (!len || (len == 1 && name[0] == '.'))
	return DOT_OFFSET;
    if (len == 2 && name[0] == '.' && name[1] == '.')
	return DOT_DOT_OFFSET;

    res = s->u.reiserfs_sb.s_hash_function (name, len);

    // take bits from 7-th to 30-th including both bounds
    res = GET_HASH_VALUE(res);
    if (res == 0)
	// needed to have no names before "." and ".." those have hash
	// value == 0 and generation conters 1 and 2 accordingly
	res = 128;
    return res + MAX_GENERATION_NUMBER;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
static int reiserfs_match (struct reiserfs_dir_entry * de, 
			   const char * name, int namelen)
{
    int retval = NAME_NOT_FOUND;

    if ((namelen == de->de_namelen) &&
	!memcmp(de->de_name, name, de->de_namelen))
	retval = (de_visible (de->de_deh + de->de_entry_num) ? NAME_FOUND : NAME_FOUND_INVISIBLE);

    return retval;
}


/* de's de_bh, de_ih, de_deh, de_item_num, de_entry_num are set already */

				/* used when hash collisions exist */


static int linear_search_in_dir_item (struct cpu_key * key, struct reiserfs_dir_entry * de,
				      const char * name, int namelen)
{
    struct reiserfs_de_head * deh = de->de_deh;
    int retval;
    int i;

    i = de->de_entry_num;

    if (i == I_ENTRY_COUNT (de->de_ih) ||
	GET_HASH_VALUE (deh_offset (deh + i)) != GET_HASH_VALUE (cpu_key_k_offset (key))) {
	i --;
    }

    RFALSE( de->de_deh != B_I_DEH (de->de_bh, de->de_ih),
	    "vs-7010: array of entry headers not found");

    deh += i;

    for (; i >= 0; i --, deh --) {
	if (GET_HASH_VALUE (deh_offset (deh)) !=
	    GET_HASH_VALUE (cpu_key_k_offset (key))) {
	    // hash value does not match, no need to check whole name
	    return NAME_NOT_FOUND;
	}
   
	/* mark, that this generation number is used */
	if (de->de_gen_number_bit_string)
	    set_bit (GET_GENERATION_NUMBER (deh_offset (deh)), de->de_gen_number_bit_string);

	// calculate pointer to name and namelen
	de->de_entry_num = i;
	set_de_name_and_namelen (de);

	if ((retval = reiserfs_match (de, name, namelen)) != NAME_NOT_FOUND) {
	    // de's de_name, de_namelen, de_recordlen are set. Fill the rest:

	    // key of pointed object
	    set_de_object_key (de);

	    store_de_entry_key (de);

	    // retval can be NAME_FOUND or NAME_FOUND_INVISIBLE
	    return retval;
	}
    }

    if (GET_GENERATION_NUMBER (le_ih_k_offset (de->de_ih)) == 0)
	/* we have reached left most entry in the node. In common we
           have to go to the left neighbor, but if generation counter
           is 0 already, we know for sure, that there is no name with
           the same hash value */
	// FIXME: this work correctly only because hash value can not
	// be 0. Btw, in case of Yura's hash it is probably possible,
	// so, this is a bug
	return NAME_NOT_FOUND;

    RFALSE( de->de_item_num,
	    "vs-7015: two diritems of the same directory in one node?");

    return GOTO_PREVIOUS_ITEM;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
// may return NAME_FOUND, NAME_FOUND_INVISIBLE, NAME_NOT_FOUND
// FIXME: should add something like IOERROR
static int reiserfs_find_entry (struct inode * dir, const char * name, int namelen, 
				struct path * path_to_entry, struct reiserfs_dir_entry * de)
{
    struct cpu_key key_to_search;
    int retval;


    if (namelen > REISERFS_MAX_NAME_LEN (dir->i_sb->s_blocksize))
	return NAME_NOT_FOUND;

    /* we will search for this key in the tree */
    make_cpu_key (&key_to_search, dir, 
		  get_third_component (dir->i_sb, name, namelen), TYPE_DIRENTRY, 3);

    while (1) {
	retval = search_by_entry_key (dir->i_sb, &key_to_search, path_to_entry, de);
	if (retval == IO_ERROR)
	    // FIXME: still has to be dealt with

				/* I want you to conform to our error
                                   printing standard.  How many times
                                   do I have to ask? -Hans */

	    BUG ();

	/* compare names for all entries having given hash value */
	retval = linear_search_in_dir_item (&key_to_search, de, name, namelen);
	if (retval != GOTO_PREVIOUS_ITEM) {
	    /* there is no need to scan directory anymore. Given entry found or does not exist */
	    path_to_entry->pos_in_item = de->de_entry_num;
	    return retval;
	}

	/* there is left neighboring item of this directory and given entry can be there */
	set_cpu_key_k_offset (&key_to_search, le_ih_k_offset (de->de_ih) - 1);
	pathrelse (path_to_entry);

    } /* while (1) */
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
struct dentry * reiserfs_lookup (struct inode * dir, struct dentry * dentry)
{
    int retval;
    struct inode * inode = 0;
    struct reiserfs_dir_entry de;
    INITIALIZE_PATH (path_to_entry);

    reiserfs_check_lock_depth("lookup") ;

    if (dentry->d_name.len > REISERFS_MAX_NAME_LEN (dir->i_sb->s_blocksize))
	return ERR_PTR(-ENAMETOOLONG);

    de.de_gen_number_bit_string = 0;
    retval = reiserfs_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &path_to_entry, &de);
    pathrelse (&path_to_entry);
    if (retval == NAME_FOUND) {
	inode = reiserfs_iget (dir->i_sb, (struct cpu_key *)&(de.de_dir_id));
	if (!inode || IS_ERR(inode)) {
	    return ERR_PTR(-EACCES);
        }
    }

    d_add(dentry, inode);
    return NULL;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//

/* add entry to the directory (entry can be hidden). 

insert definition of when hidden directories are used here -Hans

 Does not mark dir   inode dirty, do it after successesfull call to it */

static int reiserfs_add_entry (struct reiserfs_transaction_handle *th, struct inode * dir,
                               const char * name, int namelen, struct inode * inode,
			       int visible)
{
    struct cpu_key entry_key;
    struct reiserfs_de_head * deh;
    INITIALIZE_PATH (path);
    struct reiserfs_dir_entry de;
    int bit_string [MAX_GENERATION_NUMBER / (sizeof(int) * 8) + 1];
    int gen_number;
    char small_buf[32+DEH_SIZE] ; /* 48 bytes now and we avoid kmalloc
                                     if we create file with short name */
    char * buffer;
    int buflen, paste_size;
    int retval;


    /* cannot allow items to be added into a busy deleted directory */
    if (!namelen)
	return -EINVAL;

    if (namelen > REISERFS_MAX_NAME_LEN (dir->i_sb->s_blocksize))
	return -ENAMETOOLONG;

    /* each entry has unique key. compose it */
    make_cpu_key (&entry_key, dir, 
		  get_third_component (dir->i_sb, name, namelen), TYPE_DIRENTRY, 3);

    /* get memory for composing the entry */
    buflen = DEH_SIZE + ROUND_UP (namelen);
    if (buflen > sizeof (small_buf)) {
	buffer = reiserfs_kmalloc (buflen, GFP_NOFS, dir->i_sb);
	if (buffer == 0)
	    return -ENOMEM;
    } else
	buffer = small_buf;

    paste_size = (old_format_only (dir->i_sb)) ? (DEH_SIZE + namelen) : buflen;

    /* fill buffer : directory entry head, name[, dir objectid | , stat data | ,stat data, dir objectid ] */
    deh = (struct reiserfs_de_head *)buffer;
    deh->deh_location = 0;
    deh->deh_offset = cpu_to_le32 (cpu_key_k_offset (&entry_key));
    deh->deh_state = 0;
    /* put key (ino analog) to de */
    deh->deh_dir_id = INODE_PKEY (inode)->k_dir_id;
    deh->deh_objectid = INODE_PKEY (inode)->k_objectid;

    /* copy name */
    memcpy ((char *)(deh + 1), name, namelen);
    /* padd by 0s to the 4 byte boundary */
    padd_item ((char *)(deh + 1), ROUND_UP (namelen), namelen);

    /* entry is ready to be pasted into tree, set 'visibility' and 'stat data in entry' attributes */
    mark_de_without_sd (deh);
    visible ? mark_de_visible (deh) : mark_de_hidden (deh);

    /* find the proper place for the new entry */
    memset (bit_string, 0, sizeof (bit_string));
    de.de_gen_number_bit_string = (char *)bit_string;
    retval = reiserfs_find_entry (dir, name, namelen, &path, &de);
    if (retval != NAME_NOT_FOUND) {
	if (buffer != small_buf)
	    reiserfs_kfree (buffer, buflen, dir->i_sb);
	pathrelse (&path);
	
	if (retval != NAME_FOUND) {
	    reiserfs_warning ("zam-7002:" __FUNCTION__ ": \"reiserfs_find_entry\" has returned"
			      " unexpected value (%d)\n", retval);
	}
	
	return -EEXIST;
    }

    gen_number = find_first_zero_bit (bit_string, MAX_GENERATION_NUMBER + 1);
    if (gen_number > MAX_GENERATION_NUMBER) {
	/* there is no free generation number */
	reiserfs_warning ("reiserfs_add_entry: Congratulations! we have got hash function screwed up\n");
	if (buffer != small_buf)
	    reiserfs_kfree (buffer, buflen, dir->i_sb);
	pathrelse (&path);
	return -EBUSY;
    }
    /* adjust offset of directory enrty */
    deh->deh_offset = cpu_to_le32 (SET_GENERATION_NUMBER (deh_offset (deh), gen_number));
    set_cpu_key_k_offset (&entry_key, le32_to_cpu (deh->deh_offset));

    if (gen_number != 0) {	/* we need to re-search for the insertion point */
	if (search_by_entry_key (dir->i_sb, &entry_key, &path, &de) != NAME_NOT_FOUND) {
	    reiserfs_warning ("vs-7032: reiserfs_add_entry: "
			      "entry with this key (%k) already exists\n", &entry_key);
	    if (buffer != small_buf)
		reiserfs_kfree (buffer, buflen, dir->i_sb);
	    pathrelse (&path);
	    return -EBUSY;
	}
    }
  
    /* perform the insertion of the entry that we have prepared */
    retval = reiserfs_paste_into_item (th, &path, &entry_key, buffer, paste_size);
    if (buffer != small_buf)
	reiserfs_kfree (buffer, buflen, dir->i_sb);
    if (retval) {
	reiserfs_check_path(&path) ;
	return retval;
    }

    dir->i_size += paste_size;
    dir->i_blocks = ((dir->i_size + 511) >> 9);
    dir->i_mtime = dir->i_ctime = CURRENT_TIME;
    if (!S_ISDIR (inode->i_mode) && visible)
	// reiserfs_mkdir or reiserfs_rename will do that by itself
	reiserfs_update_sd (th, dir);

    reiserfs_check_path(&path) ;
    return 0;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_create (struct inode * dir, struct dentry *dentry, int mode)
{
    int retval;
    struct inode * inode;
    int windex ;
    int jbegin_count = JOURNAL_PER_BALANCE_CNT * 2 ;
    struct reiserfs_transaction_handle th ;


    inode = new_inode(dir->i_sb) ;
    if (!inode) {
	return -ENOMEM ;
    }
    journal_begin(&th, dir->i_sb, jbegin_count) ;
    th.t_caller = "create" ;
    windex = push_journal_writer("reiserfs_create") ;
    inode = reiserfs_new_inode (&th, dir, mode, 0, 0/*i_size*/, dentry, inode, &retval);
    if (!inode) {
	pop_journal_writer(windex) ;
	journal_end(&th, dir->i_sb, jbegin_count) ;
	return retval;
    }
	
    inode->i_op = &reiserfs_file_inode_operations;
    inode->i_fop = &reiserfs_file_operations;
    inode->i_mapping->a_ops = &reiserfs_address_space_operations ;

    retval = reiserfs_add_entry (&th, dir, dentry->d_name.name, dentry->d_name.len, 
				inode, 1/*visible*/);
    if (retval) {
	inode->i_nlink--;
	reiserfs_update_sd (&th, inode);
	pop_journal_writer(windex) ;
	// FIXME: should we put iput here and have stat data deleted
	// in the same transactioin
	journal_end(&th, dir->i_sb, jbegin_count) ;
	iput (inode);
	return retval;
    }

    d_instantiate(dentry, inode);
    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    return 0;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_mknod (struct inode * dir, struct dentry *dentry, int mode, int rdev)
{
    int retval;
    struct inode * inode;
    int windex ;
    struct reiserfs_transaction_handle th ;
    int jbegin_count = JOURNAL_PER_BALANCE_CNT * 3; 

    inode = new_inode(dir->i_sb) ;
    if (!inode) {
	return -ENOMEM ;
    }
    journal_begin(&th, dir->i_sb, jbegin_count) ;
    windex = push_journal_writer("reiserfs_mknod") ;

    inode = reiserfs_new_inode (&th, dir, mode, 0, 0/*i_size*/, dentry, inode, &retval);
    if (!inode) {
	pop_journal_writer(windex) ;
	journal_end(&th, dir->i_sb, jbegin_count) ;
	return retval;
    }

    init_special_inode(inode, mode, rdev) ;

    //FIXME: needed for block and char devices only
    reiserfs_update_sd (&th, inode);

    retval = reiserfs_add_entry (&th, dir, dentry->d_name.name, dentry->d_name.len, 
				 inode, 1/*visible*/);
    if (retval) {
	inode->i_nlink--;
	reiserfs_update_sd (&th, inode);
	pop_journal_writer(windex) ;
	journal_end(&th, dir->i_sb, jbegin_count) ;
	iput (inode);
	return retval;
    }

    d_instantiate(dentry, inode);
    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    return 0;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_mkdir (struct inode * dir, struct dentry *dentry, int mode)
{
    int retval;
    struct inode * inode;
    int windex ;
    struct reiserfs_transaction_handle th ;
    int jbegin_count = JOURNAL_PER_BALANCE_CNT * 3; 

    inode = new_inode(dir->i_sb) ;
    if (!inode) {
	return -ENOMEM ;
    }
    journal_begin(&th, dir->i_sb, jbegin_count) ;
    windex = push_journal_writer("reiserfs_mkdir") ;

    /* inc the link count now, so another writer doesn't overflow it while
    ** we sleep later on.
    */
    INC_DIR_INODE_NLINK(dir)

    mode = S_IFDIR | mode;
    inode = reiserfs_new_inode (&th, dir, mode, 0/*symlink*/,
				old_format_only (dir->i_sb) ? EMPTY_DIR_SIZE_V1 : EMPTY_DIR_SIZE,
				dentry, inode, &retval);
    if (!inode) {
	pop_journal_writer(windex) ;
	dir->i_nlink-- ;
	journal_end(&th, dir->i_sb, jbegin_count) ;
	return retval;
    }

    inode->i_op = &reiserfs_dir_inode_operations;
    inode->i_fop = &reiserfs_dir_operations;

    // note, _this_ add_entry will not update dir's stat data
    retval = reiserfs_add_entry (&th, dir, dentry->d_name.name, dentry->d_name.len, 
				inode, 1/*visible*/);
    if (retval) {
	inode->i_nlink = 0;
	DEC_DIR_INODE_NLINK(dir);
	reiserfs_update_sd (&th, inode);
	pop_journal_writer(windex) ;
	journal_end(&th, dir->i_sb, jbegin_count) ;
	iput (inode);
	return retval;
    }

    // the above add_entry did not update dir's stat data
    reiserfs_update_sd (&th, dir);

    d_instantiate(dentry, inode);
    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    return 0;
}

static inline int reiserfs_empty_dir(struct inode *inode) {
    /* we can cheat because an old format dir cannot have
    ** EMPTY_DIR_SIZE, and a new format dir cannot have
    ** EMPTY_DIR_SIZE_V1.  So, if the inode is either size, 
    ** regardless of disk format version, the directory is empty.
    */
    if (inode->i_size != EMPTY_DIR_SIZE &&
        inode->i_size != EMPTY_DIR_SIZE_V1) {
        return 0 ;
    }
    return 1 ;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_rmdir (struct inode * dir, struct dentry *dentry)
{
    int retval;
    struct inode * inode;
    int windex ;
    struct reiserfs_transaction_handle th ;
    int jbegin_count = JOURNAL_PER_BALANCE_CNT * 3; 
    INITIALIZE_PATH (path);
    struct reiserfs_dir_entry de;


    journal_begin(&th, dir->i_sb, jbegin_count) ;
    windex = push_journal_writer("reiserfs_rmdir") ;

    de.de_gen_number_bit_string = 0;
    if (reiserfs_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &path, &de) == NAME_NOT_FOUND) {
	retval = -ENOENT;
	goto end_rmdir;
    }
    inode = dentry->d_inode;

    if (de.de_objectid != inode->i_ino) {
	// FIXME: compare key of an object and a key found in the
	// entry
	retval = -EIO;
	goto end_rmdir;
    }
    if (!reiserfs_empty_dir(inode)) {
	retval = -ENOTEMPTY;
	goto end_rmdir;
    }

    /* cut entry from dir directory */
    retval = reiserfs_cut_from_item (&th, &path, &(de.de_entry_key), dir, 
                                     NULL, /* page */ 
				     0/*new file size - not used here*/);
    if (retval < 0)
	goto end_rmdir;

    if ( inode->i_nlink != 2 && inode->i_nlink != 1 )
	printk ("reiserfs_rmdir: empty directory has nlink != 2 (%d)\n", inode->i_nlink);

    inode->i_nlink = 0;
    inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
    reiserfs_update_sd (&th, inode);

    DEC_DIR_INODE_NLINK(dir)
    dir->i_size -= (DEH_SIZE + de.de_entrylen);
    dir->i_blocks = ((dir->i_size + 511) >> 9);
    reiserfs_update_sd (&th, dir);

    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    reiserfs_check_path(&path) ;
    return 0;
	
 end_rmdir:
    /* we must release path, because we did not call
       reiserfs_cut_from_item, or reiserfs_cut_from_item does not
       release path if operation was not complete */
    pathrelse (&path);
    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    return retval;	
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_unlink (struct inode * dir, struct dentry *dentry)
{
    int retval;
    struct inode * inode;
    struct reiserfs_dir_entry de;
    INITIALIZE_PATH (path);
    int windex ;
    struct reiserfs_transaction_handle th ;
    int jbegin_count = JOURNAL_PER_BALANCE_CNT * 3; 

    journal_begin(&th, dir->i_sb, jbegin_count) ;
    windex = push_journal_writer("reiserfs_unlink") ;
	
    de.de_gen_number_bit_string = 0;
    if (reiserfs_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &path, &de) == NAME_NOT_FOUND) {
	retval = -ENOENT;
	goto end_unlink;
    }
    inode = dentry->d_inode;

    if (de.de_objectid != inode->i_ino) {
	// FIXME: compare key of an object and a key found in the
	// entry
	retval = -EIO;
	goto end_unlink;
    }
  
    if (!inode->i_nlink) {
	printk("reiserfs_unlink: deleting nonexistent file (%s:%lu), %d\n",
	       kdevname(inode->i_dev), inode->i_ino, inode->i_nlink);
	inode->i_nlink = 1;
    }

    retval = reiserfs_cut_from_item (&th, &path, &(de.de_entry_key), dir, NULL, 0);
    if (retval < 0)
	goto end_unlink;

    inode->i_nlink--;
    inode->i_ctime = CURRENT_TIME;
    reiserfs_update_sd (&th, inode);

    dir->i_size -= (de.de_entrylen + DEH_SIZE);
    dir->i_blocks = ((dir->i_size + 511) >> 9);
    dir->i_ctime = dir->i_mtime = CURRENT_TIME;
    reiserfs_update_sd (&th, dir);

    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    reiserfs_check_path(&path) ;
    return 0;

 end_unlink:
    pathrelse (&path);
    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    reiserfs_check_path(&path) ;
    return retval;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_symlink (struct inode * dir, struct dentry * dentry, const char * symname)
{
    int retval;
    struct inode * inode;
    char * name;
    int item_len;
    int windex ;
    struct reiserfs_transaction_handle th ;
    int jbegin_count = JOURNAL_PER_BALANCE_CNT * 3; 


    inode = new_inode(dir->i_sb) ;
    if (!inode) {
	return -ENOMEM ;
    }

    item_len = ROUND_UP (strlen (symname));
    if (item_len > MAX_ITEM_LEN (dir->i_sb->s_blocksize)) {
	iput(inode) ;
	return -ENAMETOOLONG;
    }
  
    name = kmalloc (item_len, GFP_NOFS);
    if (!name) {
	iput(inode) ;
	return -ENOMEM;
    }
    memcpy (name, symname, strlen (symname));
    padd_item (name, item_len, strlen (symname));

    journal_begin(&th, dir->i_sb, jbegin_count) ;
    windex = push_journal_writer("reiserfs_symlink") ;

    inode = reiserfs_new_inode (&th, dir, S_IFLNK | S_IRWXUGO, name, strlen (symname), dentry,
				inode, &retval);
    kfree (name);
    if (inode == 0) { /* reiserfs_new_inode iputs for us */
	pop_journal_writer(windex) ;
	journal_end(&th, dir->i_sb, jbegin_count) ;
	return retval;
    }

    inode->i_op = &page_symlink_inode_operations;
    inode->i_mapping->a_ops = &reiserfs_address_space_operations;

    // must be sure this inode is written with this transaction
    //
    //reiserfs_update_sd (&th, inode, READ_BLOCKS);

    retval = reiserfs_add_entry (&th, dir, dentry->d_name.name, dentry->d_name.len, 
				 inode, 1/*visible*/);
    if (retval) {
	inode->i_nlink--;
	reiserfs_update_sd (&th, inode);
	pop_journal_writer(windex) ;
	journal_end(&th, dir->i_sb, jbegin_count) ;
	iput (inode);
	return retval;
    }

    d_instantiate(dentry, inode);
    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    return 0;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//
int reiserfs_link (struct dentry * old_dentry, struct inode * dir, struct dentry * dentry)
{
    int retval;
    struct inode *inode = old_dentry->d_inode;
    int windex ;
    struct reiserfs_transaction_handle th ;
    int jbegin_count = JOURNAL_PER_BALANCE_CNT * 3; 


    if (S_ISDIR(inode->i_mode))
	return -EPERM;
  
    if (inode->i_nlink >= REISERFS_LINK_MAX) {
	//FIXME: sd_nlink is 32 bit for new files
	return -EMLINK;
    }

    journal_begin(&th, dir->i_sb, jbegin_count) ;
    windex = push_journal_writer("reiserfs_link") ;

    /* create new entry */
    retval = reiserfs_add_entry (&th, dir, dentry->d_name.name, dentry->d_name.len,
				 inode, 1/*visible*/);
    if (retval) {
	pop_journal_writer(windex) ;
	journal_end(&th, dir->i_sb, jbegin_count) ;
	return retval;
    }

    inode->i_nlink++;
    inode->i_ctime = CURRENT_TIME;
    reiserfs_update_sd (&th, inode);

    atomic_inc(&inode->i_count) ;
    d_instantiate(dentry, inode);
    pop_journal_writer(windex) ;
    journal_end(&th, dir->i_sb, jbegin_count) ;
    return 0;
}


// de contains information pointing to an entry which 
static int de_still_valid (const char * name, int len, struct reiserfs_dir_entry * de)
{
    struct reiserfs_dir_entry tmp = *de;
    
    // recalculate pointer to name and name length
    set_de_name_and_namelen (&tmp);
    // FIXME: could check more
    if (tmp.de_namelen != len || memcmp (name, de->de_name, len))
	return 0;
    return 1;
}


static int entry_points_to_object (const char * name, int len, struct reiserfs_dir_entry * de, struct inode * inode)
{
    if (!de_still_valid (name, len, de))
	return 0;

    if (inode) {
	if (!de_visible (de->de_deh + de->de_entry_num))
	    reiserfs_panic (0, "vs-7042: entry_points_to_object: entry must be visible");
	return (de->de_objectid == inode->i_ino) ? 1 : 0;
    }

    /* this must be added hidden entry */
    if (de_visible (de->de_deh + de->de_entry_num))
	reiserfs_panic (0, "vs-7043: entry_points_to_object: entry must be visible");

    return 1;
}


/* sets key of objectid the entry has to point to */
static void set_ino_in_dir_entry (struct reiserfs_dir_entry * de, struct key * key)
{
    de->de_deh[de->de_entry_num].deh_dir_id = key->k_dir_id;
    de->de_deh[de->de_entry_num].deh_objectid = key->k_objectid;
}


//
// a portion of this function, particularly the VFS interface portion,
// was derived from minix or ext2's analog and evolved as the
// prototype did. You should be able to tell which portion by looking
// at the ext2 code and comparing. It's subfunctions contain no code
// used as a template unless they are so labeled.
//

/* 
 * process, that is going to call fix_nodes/do_balance must hold only
 * one path. If it holds 2 or more, it can get into endless waiting in
 * get_empty_nodes or its clones 
 */
int reiserfs_rename (struct inode * old_dir, struct dentry *old_dentry,
		     struct inode * new_dir, struct dentry *new_dentry)
{
    int retval;
    INITIALIZE_PATH (old_entry_path);
    INITIALIZE_PATH (new_entry_path);
    INITIALIZE_PATH (dot_dot_entry_path);
    struct item_head new_entry_ih, old_entry_ih ;
    struct reiserfs_dir_entry old_de, new_de, dot_dot_de;
    struct inode * old_inode, * new_inode;
    int windex ;
    struct reiserfs_transaction_handle th ;
    int jbegin_count = JOURNAL_PER_BALANCE_CNT * 3; 


    old_inode = old_dentry->d_inode;
    new_inode = new_dentry->d_inode;

    // make sure, that oldname still exists and points to an object we
    // are going to rename
    old_de.de_gen_number_bit_string = 0;
    retval = reiserfs_find_entry (old_dir, old_dentry->d_name.name, old_dentry->d_name.len,
				  &old_entry_path, &old_de);
    pathrelse (&old_entry_path);
    if (retval != NAME_FOUND || old_de.de_objectid != old_inode->i_ino) {
	// FIXME: IO error is possible here
	return -ENOENT;
    }

    if (S_ISDIR(old_inode->i_mode)) {
	// make sure, that directory being renamed has correct ".." 
	// and that its new parent directory has not too many links
	// already

	if (new_inode) {
	    if (!reiserfs_empty_dir(new_inode)) {
		return -ENOTEMPTY;
	    }
	}
	
	/* directory is renamed, its parent directory will be changed, 
	** so find ".." entry 
	*/
	dot_dot_de.de_gen_number_bit_string = 0;
	retval = reiserfs_find_entry (old_inode, "..", 2, &dot_dot_entry_path, &dot_dot_de);
	pathrelse (&dot_dot_entry_path);
	if (retval != NAME_FOUND)
	    return -EIO;

	/* inode number of .. must equal old_dir->i_ino */
	if (dot_dot_de.de_objectid != old_dir->i_ino)
	    return -EIO;
    }

    journal_begin(&th, old_dir->i_sb, jbegin_count) ;
    windex = push_journal_writer("reiserfs_rename") ;

    /* add new entry (or find the existing one) */
    retval = reiserfs_add_entry (&th, new_dir, new_dentry->d_name.name, new_dentry->d_name.len, 
				 old_inode, 0);
    if (retval == -EEXIST) {
	// FIXME: is it possible, that new_inode == 0 here? If yes, it
	// is not clear how does ext2 handle that
	if (!new_inode) {
	    printk ("reiserfs_rename: new entry is found, new inode == 0\n");
	    BUG ();
	}
    } else if (retval) {
	pop_journal_writer(windex) ;
	journal_end(&th, old_dir->i_sb, jbegin_count) ;
	return retval;
    }


    while (1) {
	// look for old name using corresponding entry key (found by reiserfs_find_entry)
	if (search_by_entry_key (new_dir->i_sb, &old_de.de_entry_key, &old_entry_path, &old_de) != NAME_FOUND)
	    BUG ();

	copy_item_head(&old_entry_ih, get_ih(&old_entry_path)) ;

	// look for new name by reiserfs_find_entry
	new_de.de_gen_number_bit_string = 0;
	retval = reiserfs_find_entry (new_dir, new_dentry->d_name.name, new_dentry->d_name.len, 
				      &new_entry_path, &new_de);
	if (retval != NAME_FOUND_INVISIBLE && retval != NAME_FOUND)
	    BUG ();

	copy_item_head(&new_entry_ih, get_ih(&new_entry_path)) ;

	reiserfs_prepare_for_journal(old_inode->i_sb, new_de.de_bh, 1) ;

	if (S_ISDIR(old_inode->i_mode)) {
	    if (search_by_entry_key (new_dir->i_sb, &dot_dot_de.de_entry_key, &dot_dot_entry_path, &dot_dot_de) != NAME_FOUND)
		BUG ();
	    // node containing ".." gets into transaction
	    reiserfs_prepare_for_journal(old_inode->i_sb, dot_dot_de.de_bh, 1) ;
	}
				/* we should check seals here, not do
                                   this stuff, yes? Then, having
                                   gathered everything into RAM we
                                   should lock the buffers, yes?  -Hans */
				/* probably.  our rename needs to hold more 
				** than one path at once.  The seals would 
				** have to be written to deal with multi-path 
				** issues -chris
				*/
	/* sanity checking before doing the rename - avoid races many
	** of the above checks could have scheduled.  We have to be
	** sure our items haven't been shifted by another process.
	*/
	if (!entry_points_to_object(new_dentry->d_name.name, 
	                            new_dentry->d_name.len,
				    &new_de, new_inode) ||
	    item_moved(&new_entry_ih, &new_entry_path) ||
	    item_moved(&old_entry_ih, &old_entry_path) || 
	    !entry_points_to_object (old_dentry->d_name.name, 
	                             old_dentry->d_name.len,
				     &old_de, old_inode)) {
	    reiserfs_restore_prepared_buffer (old_inode->i_sb, new_de.de_bh);
	    if (S_ISDIR(old_inode->i_mode))
		reiserfs_restore_prepared_buffer (old_inode->i_sb, dot_dot_de.de_bh);
#if 0
	    // FIXME: do we need this? shouldn't we simply continue?
	    run_task_queue(&tq_disk);
	    current->policy |= SCHED_YIELD;
	    /*current->counter = 0;*/
	    schedule();
#endif
	    continue;
	}

	RFALSE( S_ISDIR(old_inode->i_mode) && 
		(!entry_points_to_object ("..", 2, &dot_dot_de, old_dir) || 
		 !reiserfs_buffer_prepared(dot_dot_de.de_bh)), "" );

	break;
    }

    /* ok, all the changes can be done in one fell swoop when we
       have claimed all the buffers needed.*/
    
    mark_de_visible (new_de.de_deh + new_de.de_entry_num);
    set_ino_in_dir_entry (&new_de, INODE_PKEY (old_inode));
    journal_mark_dirty (&th, old_dir->i_sb, new_de.de_bh);

    mark_de_hidden (old_de.de_deh + old_de.de_entry_num);
    old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
    new_dir->i_ctime = new_dir->i_mtime = CURRENT_TIME;

    if (new_inode) {
	// adjust link number of the victim
	if (S_ISDIR(new_inode->i_mode)) {
	  DEC_DIR_INODE_NLINK(new_inode)
	} else {
	  new_inode->i_nlink--;
	}
	new_inode->i_ctime = CURRENT_TIME;
    }

    if (S_ISDIR(old_inode->i_mode)) {
      //if (dot_dot_de.de_bh) {
	// adjust ".." of renamed directory
	set_ino_in_dir_entry (&dot_dot_de, INODE_PKEY (new_dir));
	journal_mark_dirty (&th, new_dir->i_sb, dot_dot_de.de_bh);

	DEC_DIR_INODE_NLINK(old_dir)
	if (new_inode) {
	    if (S_ISDIR(new_inode->i_mode)) {
		DEC_DIR_INODE_NLINK(new_inode)
	    } else {
	        new_inode->i_nlink--;
	    }
	} else {
	    INC_DIR_INODE_NLINK(new_dir)
	}
    }

    // looks like in 2.3.99pre3 brelse is atomic. so we can use pathrelse
    pathrelse (&new_entry_path);
    pathrelse (&dot_dot_entry_path);

    // FIXME: this reiserfs_cut_from_item's return value may screw up
    // anybody, but it will panic if will not be able to find the
    // entry. This needs one more clean up
    if (reiserfs_cut_from_item (&th, &old_entry_path, &(old_de.de_entry_key), old_dir, NULL, 0) < 0)
	reiserfs_warning ("vs-: reiserfs_rename: coudl not cut old name. Fsck later?\n");

    old_dir->i_size -= DEH_SIZE + old_de.de_entrylen;
    old_dir->i_blocks = ((old_dir->i_size + 511) >> 9);

    reiserfs_update_sd (&th, old_dir);
    reiserfs_update_sd (&th, new_dir);
    if (new_inode)
	reiserfs_update_sd (&th, new_inode);

    pop_journal_writer(windex) ;
    journal_end(&th, old_dir->i_sb, jbegin_count) ;
    return 0;
}

