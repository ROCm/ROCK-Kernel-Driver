/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Directory plugin's methods. See dir.c for more details. */

#if !defined( __REISER4_DIR_H__ )
#define __REISER4_DIR_H__

#include "../../forward.h"
#include "../../kassign.h"
#include "../../type_safe_hash.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct file  */
#include <linux/radix-tree.h>

/* locking: fields of per file descriptor readdir_pos and ->f_pos are
 * protected by ->i_sem on inode. Under this lock following invariant
 * holds:
 *
 *     file descriptor is "looking" at the entry_no-th directory entry from
 *     the beginning of directory. This entry has key dir_entry_key and is
 *     pos-th entry with duplicate-key sequence.
 *
 */

/* logical position within directory */
typedef struct {
	/* key of directory entry (actually, part of a key sufficient to
	   identify directory entry)  */
	de_id dir_entry_key;
	/* ordinal number of directory entry among all entries with the same
	   key. (Starting from 0.) */
	unsigned pos;
} dir_pos;

typedef struct {
	/* f_pos corresponding to this readdir position */
	__u64 fpos;
	/* logical position within directory */
	dir_pos position;
	/* logical number of directory entry within
	   directory  */
	__u64 entry_no;
} readdir_pos;

extern void adjust_dir_file(struct inode *dir, const struct dentry *de,
			    int offset, int adj);
extern loff_t seek_dir(struct file *file, loff_t off, int origin);

/* description of directory entry being created/destroyed/sought for

   It is passed down to the directory plugin and farther to the
   directory item plugin methods. Creation of new directory is done in
   several stages: first we search for an entry with the same name, then
   create new one. reiser4_dir_entry_desc is used to store some information
   collected at some stage of this process and required later: key of
   item that we want to insert/delete and pointer to an object that will
   be bound by the new directory entry. Probably some more fields will
   be added there.

*/
struct reiser4_dir_entry_desc {
	/* key of directory entry */
	reiser4_key key;
	/* object bound by this entry. */
	struct inode *obj;
};

int is_name_acceptable(const struct inode *inode, const char *name UNUSED_ARG, int len);
int is_dir_empty(const struct inode *dir);
int reiser4_update_dir(struct inode *dir);

void dispose_cursors(struct inode *inode);
void load_cursors(struct inode *inode);
void kill_cursors(struct inode *inode);

typedef struct dir_cursor dir_cursor;

TYPE_SAFE_HASH_DECLARE(d_cursor, dir_cursor);

int d_cursor_init_at(struct super_block *s);
void d_cursor_done_at(struct super_block *s);

/*
 * information about d_cursors (detached readdir state) maintained in reiser4
 * specific portion of reiser4 super-block. See dir.c for more information on
 * d_cursors.
 */
typedef struct d_cursor_info {
	d_cursor_hash_table    table;
	struct radix_tree_root tree;
} d_cursor_info;

/* __REISER4_DIR_H__ */
#endif

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
