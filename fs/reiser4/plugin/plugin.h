/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Basic plugin data-types.
   see fs/reiser4/plugin/plugin.c for details */

#if !defined( __FS_REISER4_PLUGIN_TYPES_H__ )
#define __FS_REISER4_PLUGIN_TYPES_H__

#include "../forward.h"
#include "../debug.h"
#include "../dformat.h"
#include "../key.h"
#include "../type_safe_list.h"
#include "compress/compress.h"
#include "plugin_header.h"
#include "item/static_stat.h"
#include "item/internal.h"
#include "item/sde.h"
#include "item/cde.h"
/*#include "file/file.h"*/
#include "pseudo/pseudo.h"
#include "symlink.h"
#include "dir/hashed_dir.h"
#include "dir/dir.h"
#include "item/item.h"
#include "node/node.h"
#include "node/node40.h"
#include "security/perm.h"
#include "fibration.h"

#include "space/bitmap.h"
#include "space/space_allocator.h"

#include "disk_format/disk_format40.h"
#include "disk_format/disk_format.h"

#include <linux/fs.h>		/* for struct super_block, address_space  */
#include <linux/mm.h>		/* for struct page */
#include <linux/buffer_head.h>	/* for struct buffer_head */
#include <linux/dcache.h>	/* for struct dentry */
#include <linux/types.h>
#include <linux/crypto.h>

/* a flow is a sequence of bytes being written to or read from the tree.  The
   tree will slice the flow into items while storing it into nodes, but all of
   that is hidden from anything outside the tree.  */

struct flow {
	reiser4_key key;	/* key of start of flow's sequence of bytes */
	loff_t length;		/* length of flow's sequence of bytes */
	char *data;		/* start of flow's sequence of bytes */
	int user;		/* if 1 data is user space, 0 - kernel space */
	rw_op op;               /* NIKITA-FIXME-HANS: comment is where?  */
};

typedef ssize_t(*rw_f_type) (struct file * file, flow_t * a_flow, loff_t * off);

typedef struct reiser4_object_on_wire reiser4_object_on_wire;

/* File plugin.  Defines the set of methods that file plugins implement, some of which are optional.

 A file plugin offers to the caller an interface for IO ( writing to and/or reading from) to what the caller sees as one
 sequence of bytes.  An IO to it may affect more than one physical sequence of bytes, or no physical sequence of bytes,
 it may affect sequences of bytes offered by other file plugins to the semantic layer, and the file plugin may invoke
 other plugins and delegate work to them, but its interface is structured for offering the caller the ability to read
 and/or write what the caller sees as being a single sequence of bytes.

 The file plugin must present a sequence of bytes to the caller, but it does not necessarily have to store a sequence of
 bytes, it does not necessarily have to support efficient tree traversal to any offset in the sequence of bytes (tail
 and extent items, whose keys contain offsets, do however provide efficient non-sequential lookup of any offset in the
 sequence of bytes).

 Directory plugins provide methods for selecting file plugins by resolving a name for them.

 The functionality other filesystems call an attribute, and rigidly tie together, we decompose into orthogonal
 selectable features of files.  Using the terminology we will define next, an attribute is a perhaps constrained,
 perhaps static length, file whose parent has a uni-count-intra-link to it, which might be grandparent-major-packed, and
 whose parent has a deletion method that deletes it.

 File plugins can implement constraints.

 Files can be of variable length (e.g. regular unix files), or of static length (e.g. static sized attributes).

 An object may have many sequences of bytes, and many file plugins, but, it has exactly one objectid.  It is usually
 desirable that an object has a deletion method which deletes every item with that objectid.  Items cannot in general be
 found by just their objectids.  This means that an object must have either a method built into its deletion plugin
 method for knowing what items need to be deleted, or links stored with the object that provide the plugin with a method
 for finding those items.  Deleting a file within an object may or may not have the effect of deleting the entire
 object, depending on the file plugin's deletion method.

 LINK TAXONOMY:

   Many objects have a reference count, and when the reference count reaches 0 the object's deletion method is invoked.
 Some links embody a reference count increase ("countlinks"), and others do not ("nocountlinks").

   Some links are bi-directional links ("bilinks"), and some are uni-directional("unilinks").

   Some links are between parts of the same object ("intralinks"), and some are between different objects ("interlinks").

 PACKING TAXONOMY:

   Some items of an object are stored with a major packing locality based on their object's objectid (e.g. unix directory
 items in plan A), and these are called "self-major-packed".

   Some items of an object are stored with a major packing locality based on their semantic parent object's objectid
 (e.g. unix file bodies in plan A), and these are called "parent-major-packed".

   Some items of an object are stored with a major packing locality based on their semantic grandparent, and these are
 called "grandparent-major-packed".  Now carefully notice that we run into trouble with key length if we have to store a
 8 byte major+minor grandparent based packing locality, an 8 byte parent objectid, an 8 byte attribute objectid, and an
 8 byte offset, all in a 24 byte key.  One of these fields must be sacrificed if an item is to be
 grandparent-major-packed, and which to sacrifice is left to the item author choosing to make the item
 grandparent-major-packed.  You cannot make tail items and extent items grandparent-major-packed, though you could make
 them self-major-packed (usually they are parent-major-packed).

 In the case of ACLs (which are composed of fixed length ACEs which consist of {subject-type,
 subject, and permission bitmask} triples), it makes sense to not have an offset field in the ACE item key, and to allow
 duplicate keys for ACEs.  Thus, the set of ACES for a given file is found by looking for a key consisting of the
 objectid of the grandparent (thus grouping all ACLs in a directory together), the minor packing locality of ACE, the
 objectid of the file, and 0.

 IO involves moving data from one location to another, which means that two locations must be specified, source and
 destination.

 This source and destination can be in the filesystem, or they can be a pointer in the user process address space plus a byte count.

 If both source and destination are in the filesystem, then at least one of them must be representable as a pure stream
 of bytes (which we call a flow, and define as a struct containing a key, a data pointer, and a length).  This may mean
 converting one of them into a flow.  We provide a generic cast_into_flow() method, which will work for any plugin
 supporting read_flow(), though it is inefficiently implemented in that it temporarily stores the flow in a buffer
 (Question: what to do with huge flows that cannot fit into memory?  Answer: we must not convert them all at once. )

 Performing a write requires resolving the write request into a flow defining the source, and a method that performs the write, and
 a key that defines where in the tree the write is to go.

 Performing a read requires resolving the read request into a flow defining the target, and a method that performs the
 read, and a key that defines where in the tree the read is to come from.

 There will exist file plugins which have no pluginid stored on the disk for them, and which are only invoked by other
 plugins.

*/
typedef struct file_plugin {

	/* generic fields */
	plugin_header h;

	/* file_operations->open is dispatched here */
	int (*open) (struct inode * inode, struct file * file);
				/* NIKITA-FIXME-HANS: comment all fields, even the ones every non-beginner FS developer knows.... */
	int (*truncate) (struct inode * inode, loff_t size);

	/* save inode cached stat-data onto disk. It was called
	    reiserfs_update_sd() in 3.x */
	int (*write_sd_by_inode) (struct inode * inode);
	int (*readpage) (void *, struct page *);
	int (*prepare_write) (struct file *, struct page *, unsigned, unsigned);

	/* captures passed page to current atom and takes care about extents handling.
	   This is needed for loop back devices support and used from ->commit_write()

*/				/* ZAM-FIXME-HANS: are you writing to yourself or the reader?  Bigger comment please. */
	int (*capturepage) (struct page *);
	/*
	 * add pages created through mmap into object.
	 */
	int (*capture) (struct inode *inode, const struct writeback_control *wbc, long *);
	/* these should be implemented using body_read_flow and body_write_flow
	   builtins */
	 ssize_t(*read) (struct file * file, char *buf, size_t size, loff_t * off);
	 ssize_t(*write) (struct file * file, const char *buf, size_t size, loff_t * off);

	int (*release) (struct inode *inode, struct file * file);
	int (*ioctl) (struct inode *, struct file *, unsigned int cmd, unsigned long arg);
	int (*mmap) (struct file * file, struct vm_area_struct * vma);
	int (*get_block) (struct inode * inode, sector_t block, struct buffer_head * bh_result, int create);
/* private methods: These are optional.  If used they will allow you to
   minimize the amount of code needed to implement a deviation from some other
   method that also uses them. */

	/* Construct flow into @flow according to user-supplied data.

	   This is used by read/write methods to construct a flow to
	   write/read. ->flow_by_inode() is plugin method, rather than single
	   global implementation, because key in a flow used by plugin may
	   depend on data in a @buf.

NIKITA-FIXME-HANS: please create statistics on what functions are
dereferenced how often for the mongo benchmark.  You can supervise
Elena doing this for you if that helps.  Email me the list of the top 10, with their counts, and an estimate of the total number of CPU cycles spent dereferencing as a percentage of CPU cycles spent processing (non-idle processing).  If the total percent is, say, less than 1%, it will make our coding discussions much easier, and keep me from questioning whether functions like the below are too frequently called to be dereferenced.  If the total percent is more than 1%, perhaps private methods should be listed in a "required" comment at the top of each plugin (with stern language about how if the comment is missing it will not be accepted by the maintainer), and implemented using macros not dereferenced functions.  How about replacing this whole private methods part of the struct with a thorough documentation of what the standard helper functions are for use in constructing plugins?  I think users have been asking for that, though not in so many words.
	*/
	int (*flow_by_inode) (struct inode *, char *buf, int user, loff_t size, loff_t off, rw_op op, flow_t *);

	/* Return the key used to retrieve an offset of a file. It is used by
	   default implementation of ->flow_by_inode() method
	   (common_build_flow()) and, among other things, to get to the extent
	   from jnode of unformatted node.
	*/
	int (*key_by_inode) (struct inode * inode, loff_t off, reiser4_key * key);

/* NIKITA-FIXME-HANS: this comment is not as clear to others as you think.... */
	/* set the plugin for a file.  Called during file creation in creat()
	   but not reiser4() unless an inode already exists for the file. */
	int (*set_plug_in_inode) (struct inode * inode, struct inode * parent, reiser4_object_create_data * data);

/* NIKITA-FIXME-HANS: comment and name seem to say different things, are you setting up the object itself also or just adjusting the parent?.... */
	/* set up plugins for new @object created in @parent. @root is root
	   directory. */
	int (*adjust_to_parent) (struct inode * object, struct inode * parent, struct inode * root);
	/* this does whatever is necessary to do when object is created. For
	   instance, for unix files stat data is inserted */
	int (*create) (struct inode * object, struct inode * parent,
		       reiser4_object_create_data * data);
	/* delete empty object. This method should check REISER4_NO_SD
	   and set REISER4_NO_SD on success. Deletion of empty object
	   at least includes removal of stat-data if any. For directories this
	   also includes removal of dot and dot-dot.
	*/
	int (*delete) (struct inode * object);

	/* method implementing f_op->fsync() */
	int (*sync)(struct inode *, int datasync);

	/* add link from @parent to @object */
	int (*add_link) (struct inode * object, struct inode * parent);

	/* remove link from @parent to @object */
	int (*rem_link) (struct inode * object, struct inode * parent);

	/* return true if item addressed by @coord belongs to @inode.
	    This is used by read/write to properly slice flow into items
	    in presence of multiple key assignment policies, because
	    items of a file are not necessarily contiguous in a key space,
	    for example, in a plan-b. */
	int (*owns_item) (const struct inode * inode, const coord_t * coord);

	/* checks whether yet another hard links to this object can be
	   added  */
	int (*can_add_link) (const struct inode * inode);
	/* checks whether hard links to this object can be removed */
	int (*can_rem_link) (const struct inode * inode);
	/* true if there is only one link (aka name) for this file */
	int (*not_linked) (const struct inode * inode);

	/* change inode attributes. */
	int (*setattr) (struct inode * inode, struct iattr * attr);

	/* obtain inode attributes */
	int (*getattr) (struct vfsmount * mnt UNUSED_ARG, struct dentry * dentry, struct kstat * stat);

	/* seek */
	 loff_t(*seek) (struct file * f, loff_t offset, int origin);

	int (*detach)(struct inode *child, struct inode *parent);

	/* called when @child was just looked up in the @parent */
	int (*bind) (struct inode * child, struct inode * parent);

	/* process safe-link during mount */
	int (*safelink)(struct inode *object, reiser4_safe_link_t link,
			__u64 value);

	/* The couple of estimate methods for all file operations */
	struct {
		reiser4_block_nr (*create) (struct inode *);
		reiser4_block_nr (*update) (const struct inode *);
		reiser4_block_nr (*unlink) (struct inode *, struct inode *);
	} estimate;
	void (*readpages)(struct file *file, struct address_space *mapping,
			  struct list_head *pages);
	/* reiser4 specific part of inode has a union of structures which are specific to a plugin. This method is
	   called when inode is read (read_inode) and when file is created (common_create_child) so that file plugin
	   could initialize its inode data */
	void (*init_inode_data)(struct inode *, reiser4_object_create_data *, int);

	/* truncate file to zero size. called by reiser4_drop_inode before truncate_inode_pages */
	int (*pre_delete)(struct inode *);

	/* called from reiser4_drop_inode() */
	void (*drop)(struct inode *);

	/* called from ->drop() when there are no links, and object should be
	 * garbage collected. */
	void (*delete_inode)(struct inode *);

	/* called from ->destroy_inode() */
	void (*destroy_inode)(struct inode *);
	void (*forget_inode)(struct inode *);
	ssize_t (*sendfile)(struct file *, loff_t *, size_t, read_actor_t, void __user *);
	/*
	 * methods to serialize object identify. This is used, for example, by
	 * reiser4_{en,de}code_fh().
	 */
	struct {
		/* store object's identity at @area */
		char *(*write)(struct inode *inode, char *area);
		/* parse object from wire to the @obj */
		char *(*read)(char *area, reiser4_object_on_wire *obj);
		/* given object identity in @obj, find or create its dentry */
		struct dentry *(*get)(struct super_block *s,
				      reiser4_object_on_wire *obj);
		/* how many bytes ->wire.write() consumes */
		int (*size)(struct inode *inode);
		/* finish with object identify */
		void (*done)(reiser4_object_on_wire *obj);
	} wire;
} file_plugin;

struct reiser4_object_on_wire {
	file_plugin *plugin;
	union {
		struct {
			obj_key_id key_id;
		} std;
		void *generic;
	} u;
};

typedef struct dir_plugin {
	/* generic fields */
	plugin_header h;
	/* for use by open call, based on name supplied will install
	   appropriate plugin and state information, into the inode such that
	   subsequent VFS operations that supply a pointer to that inode
	   operate in a manner appropriate.  Note that this may require storing
	   some state for the plugin, and that this state might even include
	   the name used by open.  */
	int (*lookup) (struct inode * parent_inode, struct dentry **dentry);
	/* VFS required/defined operations below this line */
	int (*unlink) (struct inode * parent, struct dentry * victim);
	int (*link) (struct inode * parent, struct dentry * existing, struct dentry * where);
	/* rename object named by @old entry in @old_dir to be named by @new
	   entry in @new_dir */
	int (*rename) (struct inode * old_dir, struct dentry * old, struct inode * new_dir, struct dentry * new);

	/* create new object described by @data and add it to the @parent
	   directory under the name described by @dentry */
	int (*create_child) (reiser4_object_create_data * data,
			     struct inode ** retobj);

	/* readdir implementation */
	int (*readdir) (struct file * f, void *cookie, filldir_t filldir);

	/* private methods: These are optional.  If used they will allow you to
	   minimize the amount of code needed to implement a deviation from
	   some other method that uses them.  You could logically argue that
	   they should be a separate type of plugin. */

	/* check whether "name" is acceptable name to be inserted into
	    this object. Optionally implemented by directory-like objects.
	    Can check for maximal length, reserved symbols etc */
	int (*is_name_acceptable) (const struct inode * inode, const char *name, int len);

	void (*build_entry_key) (const struct inode * dir /* directory where
							 * entry is (or will
							 * be) in.*/ ,
			  const struct qstr * name	/* name of file referenced
							 * by this entry */ ,
			  reiser4_key * result	/* resulting key of directory
						 * entry */ );
	int (*build_readdir_key) (struct file * dir, reiser4_key * result);
	int (*add_entry) (struct inode * object, struct dentry * where,
			  reiser4_object_create_data * data, reiser4_dir_entry_desc * entry);

	int (*rem_entry) (struct inode * object, struct dentry * where, reiser4_dir_entry_desc * entry);

	/* initialize directory structure for newly created object. For normal
	   unix directories, insert dot and dotdot. */
	int (*init) (struct inode * object, struct inode * parent, reiser4_object_create_data * data);
	/* destroy directory */
	int (*done) (struct inode * child);

	/* called when @subdir was just looked up in the @dir */
	int (*attach) (struct inode * subdir, struct inode * dir);
	int (*detach)(struct inode * subdir, struct inode * dir);

	struct dentry *(*get_parent)(struct inode *childdir);

	struct {
		reiser4_block_nr (*add_entry) (struct inode *node);
		reiser4_block_nr (*rem_entry) (struct inode *node);
		reiser4_block_nr (*unlink) (struct inode *, struct inode *);
	} estimate;
} dir_plugin;

typedef struct formatting_plugin {
	/* generic fields */
	plugin_header h;
	/* returns non-zero iff file's tail has to be stored
	    in a direct item. */
	int (*have_tail) (const struct inode * inode, loff_t size);
} formatting_plugin;

typedef struct hash_plugin {
	/* generic fields */
	plugin_header h;
	/* computes hash of the given name */
	 __u64(*hash) (const unsigned char *name, int len);
} hash_plugin;

typedef struct crypto_plugin {
	/* generic fields */
	plugin_header h;
	int (*alloc) (struct inode * inode);
	void (*free) (struct inode * inode);
	/* number of cpu expkey words */
	unsigned nr_keywords;
	/* Offset translator. For each offset this returns (k * offset), where
	   k (k >= 1) is a coefficient of expansion of the crypto algorithm.
	   For all symmetric algorithms k == 1. For asymmetric algorithms (which
	   inflate data) offset translation guarantees that all disk cluster's
	   units will have keys smaller then next cluster's one.
	*/
	loff_t (*scale)(struct inode * inode, size_t blocksize, loff_t src);
	/* Crypto algorithms can accept data only by chunks of crypto block
	   size. This method is to align any flow up to crypto block size when
	   we pass it to crypto algorithm. To align means to append padding of
	   special format specific to the crypto algorithm */
	int (*align_cluster)(__u8 *tail, int clust_size, int blocksize);
	/* low-level key manager (check, install, etc..) */
	int (*setkey) (struct crypto_tfm *tfm, const __u8 *key, unsigned int keylen);
	/* main text processing procedures */
	void (*encrypt) (__u32 *expkey, __u8 *dst, const __u8 *src);
	void (*decrypt) (__u32 *expkey, __u8 *dst, const __u8 *src);
} crypto_plugin;

typedef struct digest_plugin {
	/* generic fields */
	plugin_header h;
	/* digest size */
	int dsize;
	int (*alloc) (struct inode * inode);
	void (*free) (struct inode * inode);
} digest_plugin;


typedef void * tfm_info_t;
#define SQUEEZE_TFM_INFO_SIZE (LAST_COMPRESSION_ID * sizeof(tfm_info_t))

typedef struct compression_plugin {
	/* generic fields */
	plugin_header h;
	/* the maximum number of bytes the size of the "compressed" data can
	 * exceed the uncompressed data. */
	int (*overrun) (unsigned src_len);
	int (*alloc) (tfm_info_t * ctx, tfm_action act);
	void (*free) (tfm_info_t * ctx, tfm_action act);
	/* main text processing procedures */
	void (*compress)   (tfm_info_t ctx, __u8 *src_first, unsigned src_len,
			    __u8 *dst_first, unsigned *dst_len);
	void (*decompress) (tfm_info_t ctx, __u8 *src_first, unsigned src_len,
			    __u8 *dst_first, unsigned *dst_len);
}compression_plugin;

typedef struct sd_ext_plugin {
	/* generic fields */
	plugin_header h;
	int (*present) (struct inode * inode, char **area, int *len);
	int (*absent) (struct inode * inode);
	int (*save_len) (struct inode * inode);
	int (*save) (struct inode * inode, char **area);
#if REISER4_DEBUG_OUTPUT
	void (*print) (const char *prefix, char **area, int *len);
#endif
	/* alignment requirement for this stat-data part */
	int alignment;
} sd_ext_plugin;

/* this plugin contains methods to allocate objectid for newly created files,
   to deallocate objectid when file gets removed, to report number of used and
   free objectids */
typedef struct oid_allocator_plugin {
	/* generic fields */
	plugin_header h;
	int (*init_oid_allocator) (reiser4_oid_allocator * map, __u64 nr_files, __u64 oids);
	/* used to report statfs->f_files */
	 __u64(*oids_used) (reiser4_oid_allocator * map);
	/* get next oid to use */
	 __u64(*next_oid) (reiser4_oid_allocator * map);
	/* used to report statfs->f_ffree */
	 __u64(*oids_free) (reiser4_oid_allocator * map);
	/* allocate new objectid */
	int (*allocate_oid) (reiser4_oid_allocator * map, oid_t *);
	/* release objectid */
	int (*release_oid) (reiser4_oid_allocator * map, oid_t);
	/* how many pages to reserve in transaction for allocation of new
	   objectid */
	int (*oid_reserve_allocate) (reiser4_oid_allocator * map);
	/* how many pages to reserve in transaction for freeing of an
	   objectid */
	int (*oid_reserve_release) (reiser4_oid_allocator * map);
	void (*print_info) (const char *, reiser4_oid_allocator *);
} oid_allocator_plugin;

/* disk layout plugin: this specifies super block, journal, bitmap (if there
   are any) locations, etc */
typedef struct disk_format_plugin {
	/* generic fields */
	plugin_header h;
	/* replay journal, initialize super_info_data, etc */
	int (*get_ready) (struct super_block *, void *data);

	/* key of root directory stat data */
	const reiser4_key *(*root_dir_key) (const struct super_block *);

	int (*release) (struct super_block *);
	jnode *(*log_super) (struct super_block *);
	void (*print_info) (const struct super_block *);
	int (*check_open) (const struct inode *object);
} disk_format_plugin;

struct jnode_plugin {
	/* generic fields */
	plugin_header h;
	int (*init) (jnode * node);
	int (*parse) (jnode * node);
	struct address_space *(*mapping) (const jnode * node);
	unsigned long (*index) (const jnode * node);
	jnode *(*clone) (jnode * node);
};

/* plugin instance.                                                         */
/*                                                                          */
/* This is "wrapper" union for all types of plugins. Most of the code uses  */
/* plugins of particular type (file_plugin, dir_plugin, etc.)  rather than  */
/* operates with pointers to reiser4_plugin. This union is only used in     */
/* some generic code in plugin/plugin.c that operates on all                */
/* plugins. Technically speaking purpose of this union is to add type       */
/* safety to said generic code: each plugin type (file_plugin, for          */
/* example), contains plugin_header as its first memeber. This first member */
/* is located at the same place in memory as .h member of                   */
/* reiser4_plugin. Generic code, obtains pointer to reiser4_plugin and      */
/* looks in the .h which is header of plugin type located in union. This    */
/* allows to avoid type-casts.                                              */
union reiser4_plugin {
	/* generic fields */
	plugin_header h;
	/* file plugin */
	file_plugin file;
	/* directory plugin */
	dir_plugin dir;
	/* hash plugin, used by directory plugin */
	hash_plugin hash;
	/* fibration plugin used by directory plugin */
	fibration_plugin fibration;
	/* crypto plugin, used by file plugin */
	crypto_plugin crypto;
	/* digest plugin, used by file plugin */
	digest_plugin digest;
	/* compression plugin, used by file plugin */
	compression_plugin compression;
	/* tail plugin, used by file plugin */
	formatting_plugin formatting;
	/* permission plugin */
	perm_plugin perm;
	/* node plugin */
	node_plugin node;
	/* item plugin */
	item_plugin item;
	/* stat-data extension plugin */
	sd_ext_plugin sd_ext;
	/* disk layout plugin */
	disk_format_plugin format;
	/* object id allocator plugin */
	oid_allocator_plugin oid_allocator;
	/* plugin for different jnode types */
	jnode_plugin jnode;
	/* plugin for pseudo files */
	pseudo_plugin pseudo;
	/* place-holder for new plugin types that can be registered
	   dynamically, and used by other dynamically loaded plugins.  */
	void *generic;
};

struct reiser4_plugin_ops {
	/* called when plugin is initialized */
	int (*init) (reiser4_plugin * plugin);
	/* called when plugin is unloaded */
	int (*done) (reiser4_plugin * plugin);
	/* load given plugin from disk */
	int (*load) (struct inode * inode,
		     reiser4_plugin * plugin, char **area, int *len);
	/* how many space is required to store this plugin's state
	    in stat-data */
	int (*save_len) (struct inode * inode, reiser4_plugin * plugin);
	/* save persistent plugin-data to disk */
	int (*save) (struct inode * inode, reiser4_plugin * plugin, char **area);
	/* alignment requirement for on-disk state of this plugin
	    in number of bytes */
	int alignment;
	/* install itself into given inode. This can return error
	    (e.g., you cannot change hash of non-empty directory). */
	int (*change) (struct inode * inode, reiser4_plugin * plugin);
	/* install itself into given inode. This can return error
	    (e.g., you cannot change hash of non-empty directory). */
	int (*inherit) (struct inode * inode, struct inode * parent,
			reiser4_plugin * plugin);
};

/* functions implemented in fs/reiser4/plugin/plugin.c */

/* stores plugin reference in reiser4-specific part of inode */
extern int set_object_plugin(struct inode *inode, reiser4_plugin_id id);
extern int setup_plugins(struct super_block *super, reiser4_plugin ** area);
extern reiser4_plugin *lookup_plugin(const char *type_label, const char *plug_label);
extern int init_plugins(void);

/* functions implemented in fs/reiser4/plugin/object.c */
void move_flow_forward(flow_t * f, unsigned count);

/* builtin plugins */

/* builtin file-plugins */
typedef enum {
	/* regular file */
	UNIX_FILE_PLUGIN_ID,
	/* directory */
	DIRECTORY_FILE_PLUGIN_ID,
	/* symlink */
	SYMLINK_FILE_PLUGIN_ID,
	/* for objects completely handled by the VFS: fifos, devices,
	   sockets  */
	SPECIAL_FILE_PLUGIN_ID,
	/* Plugin id for crypto-compression objects */
	CRC_FILE_PLUGIN_ID,
	/* pseudo file */
	PSEUDO_FILE_PLUGIN_ID,
        /* number of file plugins. Used as size of arrays to hold
	   file plugins. */
	LAST_FILE_PLUGIN_ID
} reiser4_file_id;

/* builtin dir-plugins */
typedef enum {
	HASHED_DIR_PLUGIN_ID,
	SEEKABLE_HASHED_DIR_PLUGIN_ID,
	PSEUDO_DIR_PLUGIN_ID,
	LAST_DIR_ID
} reiser4_dir_id;

/* builtin hash-plugins */

typedef enum {
	RUPASOV_HASH_ID,
	R5_HASH_ID,
	TEA_HASH_ID,
	FNV1_HASH_ID,
	DEGENERATE_HASH_ID,
	LAST_HASH_ID
} reiser4_hash_id;

/* builtin crypto-plugins */

typedef enum {
	NONE_CRYPTO_ID,
	LAST_CRYPTO_ID
} reiser4_crypto_id;

/* builtin digest plugins */

typedef enum {
	NONE_DIGEST_ID,
	LAST_DIGEST_ID
} reiser4_digest_id;

/* builtin compression plugins */

typedef enum {
	NONE_COMPRESSION_ID,
	NULL_COMPRESSION_ID,
	LZO1_COMPRESSION_ID,
	GZIP1_COMPRESSION_ID,
	LAST_COMPRESSION_ID
} reiser4_compression_id;

/* builtin tail-plugins */

typedef enum {
	NEVER_TAILS_FORMATTING_ID,
	ALWAYS_TAILS_FORMATTING_ID,
	SMALL_FILE_FORMATTING_ID,
	LAST_TAIL_FORMATTING_ID
} reiser4_formatting_id;

/* Encapsulations of crypto specific data */
typedef struct crypto_data {
        reiser4_crypto_id      cra; /* id of the crypto algorithm */
	reiser4_digest_id      dia; /* id of the digest algorithm */
	__u8 * key;                 /* secret key */
	__u16 keysize;              /* key size, bits */
	__u8 * keyid;               /* keyid */
	__u16 keyid_size;           /* keyid size, bytes */
} crypto_data_t;

/* compression/clustering specific data */
typedef struct compression_data {
	reiser4_compression_id coa; /* id of the compression algorithm */
} compression_data_t;

typedef __u8 cluster_data_t;        /* cluster info */

/* data type used to pack parameters that we pass to vfs
    object creation function create_object() */
struct reiser4_object_create_data {
	/* plugin to control created object */
	reiser4_file_id id;
	/* mode of regular file, directory or special file */
/* what happens if some other sort of perm plugin is in use? */
	int mode;
	/* rdev of special file */
	dev_t rdev;
	/* symlink target */
	const char *name;
	/* add here something for non-standard objects you invent, like
	   query for interpolation file etc. */
	crypto_data_t * crypto;
	compression_data_t * compression;
	cluster_data_t * cluster;

	struct inode  *parent;
	struct dentry *dentry;
};

#define MAX_PLUGIN_TYPE_LABEL_LEN  32
#define MAX_PLUGIN_PLUG_LABEL_LEN  32

/* used for interface with user-land: table-driven parsing in
    reiser4(). */
typedef struct plugin_locator {
	reiser4_plugin_type type_id;
	reiser4_plugin_id id;
	char type_label[MAX_PLUGIN_TYPE_LABEL_LEN];
	char plug_label[MAX_PLUGIN_PLUG_LABEL_LEN];
} plugin_locator;

extern int locate_plugin(struct inode *inode, plugin_locator * loc);

static inline reiser4_plugin *
plugin_by_id(reiser4_plugin_type type_id, reiser4_plugin_id id);

static inline reiser4_plugin *
plugin_by_disk_id(reiser4_tree * tree, reiser4_plugin_type type_id, d16 * did);

#define PLUGIN_BY_ID(TYPE,ID,FIELD)					\
static inline TYPE *TYPE ## _by_id( reiser4_plugin_id id )		\
{									\
	reiser4_plugin *plugin = plugin_by_id ( ID, id );		\
	return plugin ? & plugin -> FIELD : NULL;			\
}									\
static inline TYPE *TYPE ## _by_disk_id( reiser4_tree *tree, d16 *id )	\
{									\
	reiser4_plugin *plugin = plugin_by_disk_id ( tree, ID, id );	\
	return plugin ? & plugin -> FIELD : NULL;			\
}									\
static inline TYPE *TYPE ## _by_unsafe_id( reiser4_plugin_id id )	\
{									\
	reiser4_plugin *plugin = plugin_by_unsafe_id ( ID, id );	\
	return plugin ? & plugin -> FIELD : NULL;			\
}									\
static inline reiser4_plugin* TYPE ## _to_plugin( TYPE* plugin )	\
{									\
	return ( reiser4_plugin * ) plugin;				\
}									\
static inline reiser4_plugin_id TYPE ## _id( TYPE* plugin )		\
{									\
	return TYPE ## _to_plugin (plugin) -> h.id;			\
}									\
typedef struct { int foo; } TYPE ## _plugin_dummy

PLUGIN_BY_ID(item_plugin, REISER4_ITEM_PLUGIN_TYPE, item);
PLUGIN_BY_ID(file_plugin, REISER4_FILE_PLUGIN_TYPE, file);
PLUGIN_BY_ID(dir_plugin, REISER4_DIR_PLUGIN_TYPE, dir);
PLUGIN_BY_ID(node_plugin, REISER4_NODE_PLUGIN_TYPE, node);
PLUGIN_BY_ID(sd_ext_plugin, REISER4_SD_EXT_PLUGIN_TYPE, sd_ext);
PLUGIN_BY_ID(perm_plugin, REISER4_PERM_PLUGIN_TYPE, perm);
PLUGIN_BY_ID(hash_plugin, REISER4_HASH_PLUGIN_TYPE, hash);
PLUGIN_BY_ID(fibration_plugin, REISER4_FIBRATION_PLUGIN_TYPE, fibration);
PLUGIN_BY_ID(crypto_plugin, REISER4_CRYPTO_PLUGIN_TYPE, crypto);
PLUGIN_BY_ID(digest_plugin, REISER4_DIGEST_PLUGIN_TYPE, digest);
PLUGIN_BY_ID(compression_plugin, REISER4_COMPRESSION_PLUGIN_TYPE, compression);
PLUGIN_BY_ID(formatting_plugin, REISER4_FORMATTING_PLUGIN_TYPE, formatting);
PLUGIN_BY_ID(disk_format_plugin, REISER4_FORMAT_PLUGIN_TYPE, format);
PLUGIN_BY_ID(jnode_plugin, REISER4_JNODE_PLUGIN_TYPE, jnode);
PLUGIN_BY_ID(pseudo_plugin, REISER4_PSEUDO_PLUGIN_TYPE, pseudo);

extern int save_plugin_id(reiser4_plugin * plugin, d16 * area);

#if REISER4_DEBUG_OUTPUT
extern void print_plugin(const char *prefix, reiser4_plugin * plugin);
#else
#define print_plugin( pr, pl ) noop
#endif

TYPE_SAFE_LIST_DEFINE(plugin, reiser4_plugin, h.linkage);

extern plugin_list_head *get_plugin_list(reiser4_plugin_type type_id);

#define for_all_plugins( ptype, plugin )			\
for( plugin = plugin_list_front( get_plugin_list( ptype ) ) ;	\
     ! plugin_list_end( get_plugin_list( ptype ), plugin ) ;	\
     plugin = plugin_list_next( plugin ) )

/* enumeration of fields within plugin_set */
typedef enum {
	PSET_FILE,
	PSET_DIR, /* PSET_FILE and PSET_DIR should be first elements:
		   * inode.c:read_inode() depends on this. */
	PSET_PERM,
	PSET_FORMATTING,
	PSET_HASH,
	PSET_FIBRATION,
	PSET_SD,
	PSET_DIR_ITEM,
	PSET_CRYPTO,
	PSET_DIGEST,
	PSET_COMPRESSION,
	PSET_LAST
} pset_member;

int grab_plugin(struct inode *self, struct inode *ancestor, pset_member memb);
int grab_plugin_from(struct inode *self, pset_member memb, reiser4_plugin *plug);
int force_plugin(struct inode *self, pset_member memb, reiser4_plugin *plug);

/* __FS_REISER4_PLUGIN_TYPES_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
