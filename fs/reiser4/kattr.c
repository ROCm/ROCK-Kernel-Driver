/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Interface to sysfs' attributes */

/*
 * Reiser4 exports some of its internal data through sysfs.
 *
 * For details on sysfs see fs/sysfs, include/linux/sysfs.h,
 * include/linux/kobject.h. Roughly speaking, one embeds struct kobject into
 * some kernel data type. Objects of this type will be represented as
 * _directories_ somewhere below /sys. Attributes can be registered for
 * kobject and they will be visible as files within corresponding
 * directory. Each attribute is represented by struct kattr. How given
 * attribute reacts to read and write is determined by ->show and ->store
 * operations that are properties of its parent kobject.
 *
 * Reiser4 exports following stuff through sysfs:
 *
 *    path                                              kobject or attribute
 *
 * /sys/fs/reiser4/
 *                 <dev>/                               sbinfo->kobj
 *                       sb-fields                      def_attrs[]
 *                       stats/                         sbinfo->stats_kobj
 *                             stat-cnts                reiser4_stat_defs[]
 *                             level-NN/                sbinfo->level[].kobj
 *                                      stat-cnts       reiser4_stat_level_defs[]
 *
 * (For some reasons we also add /sys/fs and /sys/fs/reiser4 manually, but
 * this is supposed to be done by core.)
 *
 * Our kattr.[ch] code depends on some additional functionality missing in the
 * core kernel. This functionality is added in kobject-umount-race.patch from
 * our core-patches repository. As it's obvious from its name this patch adds
 * protection against /sys/fs/reiser4/<dev>/ * accesses and concurrent umount
 * of <dev>. See commentary in this patch for more details.
 *
 * Shouldn't struct kobject be renamed to struct knobject?
 *
 */

#include "debug.h"
#include "super.h"
#include "kattr.h"
#include "prof.h"

#include <linux/kobject.h>     /* struct kobject */
#include <linux/fs.h>          /* struct super_block */

#if REISER4_USE_SYSFS

/*
 * Super-block fields exporting.
 *
 * Many fields of reiser4-private part of super-block
 * (fs/reiser4/super.h:reiser4_super_info_data) are exported through
 * sysfs. Code below tries to minimize code duplication for this common case.
 *
 * Specifically, all fields that are "scalars" (i.e., basically integers) of
 * 32 or 64 bits are handled by the same ->show() and ->store()
 * functions. Each such field is represented by two pieces of data:
 *
 *     1. super_field_cookie, and
 *
 *     2. reiser4_kattr.
 *
 * super_field_cookie contains "field description":
 *
 *     1. field offset in bytes from the beginning of reiser4-specific portion
 *     of super block, and
 *
 *     2. printf(3) format to show field content in ->show() function.
 *
 * reiser4_kattr is standard object we are using to embed struct fs_attribute
 * in. It stores pointer to the corresponding super_field_cookie. Also
 * reiser4_kattr contains ->store and ->show function pointers that are set
 * according to field width and desired access rights to
 * {show,store}_{ro,rw}_{32,64}().
 *
 * These functions use super_field_cookie (stored in ->cookie field of
 * reiser4_kattr) to obtain/store value of field involved and format it
 * properly.
 *
 */

/* convert @attr to reiser4_kattr object it is embedded in */
typedef struct {
	/* offset in bytes to the super-block field from the beginning of
	 * reiser4_super_info_data */
	ptrdiff_t   offset;
	/* desired printf(3) format for ->show() method. */
	const char *format;
} super_field_cookie;

/*
 * This macro defines super_field_cookie and reiser4_kattr for given
 * super-block field.
 */
#define DEFINE_SUPER_F(aname /* unique identifier used to generate variable \
			      * names */,				\
	  afield /* name of super-block field */,			\
	  aformat /* desired ->show() format */,			\
	  asize /* field size (as returned by sizeof()) */,		\
	  ashow /* show method */,					\
	  astore /* store method */,					\
	  amode /* access method */)					\
static super_field_cookie __cookie_ ## aname = {			\
	.offset = offsetof(reiser4_super_info_data, afield),		\
	.format = aformat "\n"						\
};									\
									\
static reiser4_kattr kattr_super_ ## aname = {				\
	.attr = {							\
		.kattr = {						\
			.name = (char *) #afield,			\
			.mode = amode					\
		},							\
		.show = ashow,						\
		.store = astore						\
	},								\
	.cookie = &__cookie_ ## aname					\
}

/*
 * Specialized version of DEFINE_SUPER_F() used to generate description of
 * read-only fields
 */
#define DEFINE_SUPER_RO(aname, afield, aformat, asize)			\
	DEFINE_SUPER_F(aname,						\
		       afield, aformat, asize, show_ro_ ## asize, NULL, 0440)

/*
 * Specialized version of DEFINE_SUPER_F() used to generate description of
 * read-write fields
 */
#define DEFINE_SUPER_RW(aname, afield, aformat, asize)			\
	DEFINE_SUPER_F(aname,						\
		       afield, aformat, asize, show_ro_ ## asize,	\
		       store_rw_ ## asize, 0660)

/* helper macro: return field of type @type stored at the offset of @offset
 * bytes from the @ptr. */
#define getat(ptr, offset, type) *(type *)(((char *)(ptr)) + (offset))

/* helper macro: modify value of field to @value. See getat() above for the
 * meaning of other arguments */
#define setat(ptr, offset, type, val)			\
	({ *(type *)(((char *)(ptr)) + (offset)) = (val); })

/* return cookie contained in reiser4_kattr that @attr is embedded into */
static inline void *
getcookie(struct fs_kattr *attr)
{
	return container_of(attr, reiser4_kattr, attr)->cookie;
}

/*
 * ->show method for read-only 32bit scalar super block fields.
 */
static ssize_t
show_ro_32(struct super_block * s /* super-block field belongs to */,
	   struct fs_kobject *o /* object attribute of which @kattr is. */,
	   struct fs_kattr * kattr /* file-system attribute that is
				    * exported */,
	   char * buf /* buffer to store field representation into */)
{
	char *p;
	super_field_cookie *cookie;
	__u32 val;

	cookie = getcookie(kattr);
	/* obtain field value from super-block, ... */
	val = getat(get_super_private(s), cookie->offset, __u32);
	p = buf;
	/* and print it according to the format string specified in the
	 * cookie */
	KATTR_PRINT(p, buf, cookie->format, (unsigned long long)val);
	return (p - buf);
}

/*
 * ->store method for read-write 32bit scalar super-block fields.
 */
static ssize_t
store_rw_32(struct super_block * s /* super-block field belongs to */,
	    struct fs_kobject *o /* object attribute of which @kattr is. */,
	    struct fs_kattr * kattr /* file-system attribute that is
				    * exported */,
	    const char * buf /* buffer to read field value from */,
	    size_t size /* buffer size */)
{
	super_field_cookie *cookie;
	__u32 val;

	cookie = getcookie(kattr);
	/* read value from the buffer */
	if (sscanf(buf, "%i", &val) == 1)
		/* if buffer contains well-formed value, update super-block
		 * field. */
		setat(get_super_private(s), cookie->offset, __u32, val);
	else
		size = RETERR(-EINVAL);
	return size;
}

/*
 * ->show method for read-only 64bit scalar super block fields.
 *
 * It's exactly like show_ro_32, mutatis mutandis.
 */
static ssize_t show_ro_64(struct super_block * s, struct fs_kobject *o,
			  struct fs_kattr * kattr, char * buf)
{
	char *p;
	super_field_cookie *cookie;
	__u64 val;

	cookie = getcookie(kattr);
	val = getat(get_super_private(s), cookie->offset, __u64);
	p = buf;
	KATTR_PRINT(p, buf, cookie->format, (unsigned long long)val);
	return (p - buf);
}

#if 0
/* We don't have writable 64bit attributes yet. */
static ssize_t
store_rw_64(struct super_block * s,
	    struct fs_kobject *o, struct fs_kattr * kattr,
	    char * buf, size_t size)
{
	super_field_cookie *cookie;
	__u64 val;

	cookie = getcookie(kattr);
	if (sscanf(buf, "%lli", &val) == 1)
		setat(get_super_private(s), cookie->offset, __u64, val);
	else
		size = RETERR(-EINVAL);
	return size;
}
#endif

#undef getat
#undef setat

/*
 * Exporting reiser4 compilation options.
 *
 * reiser4 compilation options are exported through
 * /sys/fs/<dev>/options. Read-only for now. :)
 *
 */

#define SHOW_OPTION(p, buf, option)			\
	if (option)					\
		KATTR_PRINT((p), (buf), #option "\n")

static ssize_t
show_options(struct super_block * s,
	     struct fs_kobject *o, struct fs_kattr * kattr, char * buf)
{
	char *p;

	p = buf;

	/*
	 * PLEASE update this when adding new compilation option
	 */

	SHOW_OPTION(p, buf, REISER4_DEBUG);
	SHOW_OPTION(p, buf, REISER4_DEBUG_MODIFY);
	SHOW_OPTION(p, buf, REISER4_DEBUG_MEMCPY);
	SHOW_OPTION(p, buf, REISER4_DEBUG_NODE);
	SHOW_OPTION(p, buf, REISER4_ZERO_NEW_NODE);
	SHOW_OPTION(p, buf, REISER4_TRACE);
	SHOW_OPTION(p, buf, REISER4_LOG);
	SHOW_OPTION(p, buf, REISER4_STATS);
	SHOW_OPTION(p, buf, REISER4_DEBUG_OUTPUT);
	SHOW_OPTION(p, buf, REISER4_LOCKPROF);
	SHOW_OPTION(p, buf, REISER4_LARGE_KEY);
	SHOW_OPTION(p, buf, REISER4_PROF);
	SHOW_OPTION(p, buf, REISER4_COPY_ON_CAPTURE);
	SHOW_OPTION(p, buf, REISER4_ALL_IN_ONE);
	SHOW_OPTION(p, buf, REISER4_DEBUG_NODE_INVARIANT);
	SHOW_OPTION(p, buf, REISER4_DEBUG_SPIN_LOCKS);
	SHOW_OPTION(p, buf, REISER4_DEBUG_CONTEXTS);
	SHOW_OPTION(p, buf, REISER4_DEBUG_SIBLING_LIST);

	return (p - buf);
}

static reiser4_kattr compile_options = {
	.attr = {
		.kattr = {
			 .name = (char *) "options",
			 .mode = 0444   /* r--r--r-- */
		 },
		.show = show_options,
	},
	.cookie = NULL
};

/*
 * show a name of device on top of which reiser4 file system exists in
 * /sys/fs/reiser4/<dev>/device.
 */

static ssize_t
show_device(struct super_block * s,
	    struct fs_kobject *o, struct fs_kattr * kattr, char * buf)
{
	char *p;

	p = buf;
	KATTR_PRINT(p, buf, "%d:%d\n", MAJOR(s->s_dev), MINOR(s->s_dev));
	return (p - buf);
}

static reiser4_kattr device = {
	.attr = {
		.kattr = {
			 .name = (char *) "device",
			 .mode = 0444   /* r--r--r-- */
		 },
		.show = show_device,
	},
	.cookie = NULL
};

#if REISER4_DEBUG

/*
 * debugging code: break into debugger on each write into this file. Useful
 * when event of importance can be detected in the user space, but not in the
 * kernel.
 */

ssize_t store_bugme(struct super_block * s, struct fs_kobject *o,
		    struct fs_kattr *ka, const char *buf, size_t size)
{
	DEBUGON(1);
	return size;
}

static reiser4_kattr bugme = {
	.attr = {
		.kattr = {
			 .name = (char *) "bugme",
			 .mode = 0222   /* -w--w--w- */
		 },
		.store = store_bugme,
	},
	.cookie = NULL
};

/* REISER4_DEBUG */
#endif

/*
 * Declare all super-block fields we want to export
 */

DEFINE_SUPER_RO(01, mkfs_id, "%#llx", 32);
DEFINE_SUPER_RO(02, block_count, "%llu", 64);
DEFINE_SUPER_RO(03, blocks_used, "%llu", 64);
DEFINE_SUPER_RO(04, blocks_free_committed, "%llu", 64);
DEFINE_SUPER_RO(05, blocks_grabbed, "%llu", 64);
DEFINE_SUPER_RO(06, blocks_fake_allocated_unformatted, "%llu", 64);
DEFINE_SUPER_RO(07, blocks_fake_allocated, "%llu", 64);
DEFINE_SUPER_RO(08, blocks_flush_reserved, "%llu", 64);
DEFINE_SUPER_RO(09, fsuid, "%#llx", 32);
#if REISER4_DEBUG
DEFINE_SUPER_RO(10, eflushed, "%llu", 32);
#endif
DEFINE_SUPER_RO(11, blocknr_hint_default, "%lli", 64);
DEFINE_SUPER_RO(12, nr_files_committed, "%llu", 64);
DEFINE_SUPER_RO(13, tmgr.atom_count, "%llu", 32);
DEFINE_SUPER_RO(14, tmgr.id_count, "%llu", 32);
DEFINE_SUPER_RO(15, tmgr.atom_max_size, "%llu", 32);
DEFINE_SUPER_RO(16, tmgr.atom_max_age, "%llu", 32);

/* tree fields */
DEFINE_SUPER_RO(17, tree.root_block, "%llu", 64);
DEFINE_SUPER_RO(18, tree.height, "%llu", 32);
DEFINE_SUPER_RO(19, tree.znode_epoch, "%llu", 64);
DEFINE_SUPER_RO(20, tree.carry.new_node_flags, "%#llx", 32);
DEFINE_SUPER_RO(21, tree.carry.new_extent_flags, "%#llx", 32);
DEFINE_SUPER_RO(22, tree.carry.paste_flags, "%#llx", 32);
DEFINE_SUPER_RO(23, tree.carry.insert_flags, "%#llx", 32);

/* not very good. Should be done by the plugin in stead */
DEFINE_SUPER_RO(24, next_to_use, "%llu", 64);
DEFINE_SUPER_RO(25, oids_in_use, "%llu", 64);

DEFINE_SUPER_RO(26, entd.flushers, "%llu", 32);

DEFINE_SUPER_RW(27, trace_flags, "%#llx", 32);
DEFINE_SUPER_RW(28, log_flags, "%#llx", 32);

#define ATTR_NO(n) &kattr_super_ ## n .attr.kattr

static struct attribute * kattr_def_attrs[] = {
	ATTR_NO(01),
	ATTR_NO(02),
	ATTR_NO(03),
	ATTR_NO(04),
	ATTR_NO(05),
	ATTR_NO(06),
	ATTR_NO(07),
	ATTR_NO(08),
	ATTR_NO(09),
#if REISER4_DEBUG
	ATTR_NO(10),
#endif
	ATTR_NO(11),
	ATTR_NO(12),
	ATTR_NO(13),
	ATTR_NO(14),
	ATTR_NO(15),
	ATTR_NO(16),
	ATTR_NO(17),
	ATTR_NO(18),
	ATTR_NO(19),
	ATTR_NO(20),
	ATTR_NO(21),
	ATTR_NO(22),
	ATTR_NO(23),
	ATTR_NO(24),
	ATTR_NO(25),
	ATTR_NO(26),
	ATTR_NO(27),
	ATTR_NO(28),
/*
	ATTR_NO(29),
	ATTR_NO(30),
*/
	&compile_options.attr.kattr,
	&device.attr.kattr,
#if REISER4_DEBUG
	&bugme.attr.kattr,
#endif
	NULL
};

struct kobj_type ktype_reiser4 = {
	.sysfs_ops	= &fs_attr_ops,
	.default_attrs	= kattr_def_attrs,
	.release	= NULL
};

#if REISER4_STATS

/*
 * Statistical counters exporting.
 *
 * When REISER4_STATS mode is on, reiser4 collects a lot of statistics. See
 * stat.[ch] for more details. All these stat-counters are exported through
 * sysfs in /sys/fs/reiser4/<dev>/stats/ directory. This directory contains
 * "global" stat-counters and also level-* sub-directories for per-level
 * counters (that is counters collected for specific levels of reiser4
 * internal tree).
 *
 */

static struct kobj_type ktype_noattr = {
	.sysfs_ops	= &fs_attr_ops,
	.default_attrs	= NULL,
	.release        = NULL
};

/*
 * register stat-counters for the level @i with sysfs. This is called during
 * mount.
 */
static int register_level_attrs(reiser4_super_info_data *sbinfo, int i)
{
	struct fs_kobject *level   /* file system kobject representing @i-th
				    * level*/;
	struct fs_kobject *parent; /* it's parent in sysfs tree */
	int result;

	/* first, setup @level */
	parent = &sbinfo->stats_kobj;
	sbinfo->level[i].level = i;
	level = &sbinfo->level[i].kobj;
	level->kobj.parent = kobject_get(&parent->kobj);
	if (level->kobj.parent != NULL) {
		snprintf(level->kobj.name, KOBJ_NAME_LEN, "level-%2.2i", i);
		level->kobj.ktype = &ktype_noattr;
		/* register @level with sysfs */
		result = fs_kobject_register(sbinfo->tree.super, level);
		if (result == 0)
			/* and ultimately populate it with attributes, that
			 * is, stat-counters */
			result = reiser4_populate_kattr_level_dir(&level->kobj);
	} else
		result = RETERR(-EBUSY);
	return result;
}
#endif

static decl_subsys(fs, NULL, NULL);
decl_subsys(reiser4, &ktype_reiser4, NULL);

/*
 * initialization function called once during kernel boot-up, or reiser4
 * module loading.
 */
reiser4_internal int
reiser4_sysfs_init_once(void)
{
	int result;

	/* add /sys/fs */
	result = subsystem_register(&fs_subsys);
	if (result == 0) {
		kset_set_kset_s(&reiser4_subsys, fs_subsys);
		/* add /sys/fs/reiser4 */
		result = subsystem_register(&reiser4_subsys);
		if (result == 0)
			result = init_prof_kobject();
	}
	return result;
}

/*
 * shutdown function dual to reiser4_sysfs_init_once(). Called during module
 * unload
 */
reiser4_internal void
reiser4_sysfs_done_once(void)
{
	subsystem_unregister(&reiser4_subsys);
	subsystem_unregister(&fs_subsys);
	done_prof_kobject();
}

/*
 * initialization function called during mount of @super
 */
reiser4_internal int
reiser4_sysfs_init(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	struct fs_kobject *kobj;
	int result;
	ON_STATS(struct fs_kobject *stats_kobj);

	sbinfo = get_super_private(super);

	kobj = &sbinfo->kobj;

	/*
	 * setup and register /sys/fs/reiser4/<dev> object
	 */
	snprintf(kobj->kobj.name, KOBJ_NAME_LEN, "%s", super->s_id);
	kobj_set_kset_s(&sbinfo->kobj, reiser4_subsys);
	result = fs_kobject_register(super, kobj);
	if (result != 0)
		return result;
#if REISER4_STATS
	/* add attributes representing statistical counters */
	stats_kobj = &sbinfo->stats_kobj;
	stats_kobj->kobj.parent = kobject_get(&kobj->kobj);
	snprintf(stats_kobj->kobj.name, KOBJ_NAME_LEN, "stats");
	stats_kobj->kobj.ktype = &ktype_noattr;
	result = fs_kobject_register(super, stats_kobj);
	if (result != 0)
		return result;
	result = reiser4_populate_kattr_dir(&stats_kobj->kobj);
	if (result == 0) {
		int i;

		for (i = 0; i < sizeof_array(sbinfo->level); ++i) {
			result = register_level_attrs(sbinfo, i);
			if (result != 0)
				break;
		}
	}
#else
	result = reiser4_populate_kattr_dir(&kobj->kobj);
#endif

	return result;
}

reiser4_internal void
reiser4_sysfs_done(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	ON_STATS(int i);

	sbinfo = get_super_private(super);
#if REISER4_STATS
	for (i = 0; i < sizeof_array(sbinfo->level); ++i)
		fs_kobject_unregister(&sbinfo->level[i].kobj);
	fs_kobject_unregister(&sbinfo->stats_kobj);
#endif
	fs_kobject_unregister(&sbinfo->kobj);
}

/* REISER4_USE_SYSFS */
#else

/*
 * Below are stubs for !REISER4_USE_SYSFS case. Do nothing.
 */

reiser4_internal int
reiser4_sysfs_init(struct super_block *super)
{
	return 0;
}

reiser4_internal void
reiser4_sysfs_done(struct super_block *super)
{}

reiser4_internal int
reiser4_sysfs_init_once(void)
{
	return 0;
}

reiser4_internal void
reiser4_sysfs_done_once(void)
{}

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
