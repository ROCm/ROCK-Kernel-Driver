/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */
/* NIKITA-FIXME-HANS: you didn't discuss this with me before coding it did you?  Remove plugin-sets from code by March 15th, 2004 */
/* plugin-sets */

/*
 * Each inode comes with a whole set of plugins: file plugin, directory
 * plugin, hash plugin, tail policy plugin, security plugin, etc.
 *
 * Storing them (pointers to them, that is) in inode is a waste of
 * space. Especially, given that on average file system plugins of vast
 * majority of files will belong to few sets (e.g., one set for regular files,
 * another set for standard directory, etc.)
 *
 * Plugin set (pset) is an object containing pointers to all plugins required
 * by inode. Inode only stores a pointer to pset. psets are "interned", that
 * is, different inodes with the same set of plugins point to the same
 * pset. This is archived by storing psets in global hash table. Races are
 * avoided by simple (and efficient so far) solution of never recycling psets,
 * even when last inode pointing to it is destroyed.
 *
 */

#include "../debug.h"

#include "plugin_set.h"

#include <linux/slab.h>
#include <linux/stddef.h>

/* slab for plugin sets */
static kmem_cache_t *plugin_set_slab;

static spinlock_t plugin_set_lock[8] __cacheline_aligned_in_smp = {
	[0 ... 7] = SPIN_LOCK_UNLOCKED
};

/* hash table support */

#define PS_TABLE_SIZE (32)

static inline plugin_set *
cast_to(const unsigned long * a)
{
	return container_of(a, plugin_set, hashval);
}

static inline int
pseq(const unsigned long * a1, const unsigned long * a2)
{
	plugin_set *set1;
	plugin_set *set2;

	/* make sure fields are not missed in the code below */
	cassert(sizeof *set1 ==

		sizeof set1->hashval +
		sizeof set1->link +

		sizeof set1->file +
		sizeof set1->dir +
		sizeof set1->perm +
		sizeof set1->formatting +
		sizeof set1->hash +
		sizeof set1->fibration +
		sizeof set1->sd +
		sizeof set1->dir_item +
		sizeof set1->crypto +
		sizeof set1->digest +
		sizeof set1->compression);

	set1 = cast_to(a1);
	set2 = cast_to(a2);
	return
		set1->hashval == set2->hashval &&

		set1->file == set2->file &&
		set1->dir == set2->dir &&
		set1->perm == set2->perm &&
		set1->formatting == set2->formatting &&
		set1->hash == set2->hash &&
		set1->fibration == set2->fibration &&
		set1->sd == set2->sd &&
		set1->dir_item == set2->dir_item &&
		set1->crypto == set2->crypto &&
		set1->digest == set2->digest &&
		set1->compression == set2->compression;
}

#define HASH_FIELD(hash, set, field)		\
({						\
        (hash) += (unsigned long)(set)->field >> 2;	\
})

static inline unsigned long calculate_hash(const plugin_set *set)
{
	unsigned long result;

	result = 0;
	HASH_FIELD(result, set, file);
	HASH_FIELD(result, set, dir);
	HASH_FIELD(result, set, perm);
	HASH_FIELD(result, set, formatting);
	HASH_FIELD(result, set, hash);
	HASH_FIELD(result, set, fibration);
	HASH_FIELD(result, set, sd);
	HASH_FIELD(result, set, dir_item);
	HASH_FIELD(result, set, crypto);
	HASH_FIELD(result, set, digest);
	HASH_FIELD(result, set, compression);
	return result & (PS_TABLE_SIZE - 1);
}

static inline unsigned long
pshash(ps_hash_table *table, const unsigned long * a)
{
	return *a;
}

/* The hash table definition */
#define KMALLOC(size) kmalloc((size), GFP_KERNEL)
#define KFREE(ptr, size) kfree(ptr)
TYPE_SAFE_HASH_DEFINE(ps, plugin_set, unsigned long, hashval, link, pshash, pseq);
#undef KFREE
#undef KMALLOC

static ps_hash_table ps_table;
static plugin_set empty_set = {
	.hashval            = 0,
	.file               = NULL,
	.dir                = NULL,
	.perm               = NULL,
	.formatting         = NULL,
	.hash               = NULL,
	.fibration          = NULL,
	.sd                 = NULL,
	.dir_item           = NULL,
	.crypto             = NULL,
	.digest             = NULL,
	.compression        = NULL,
	.link               = { NULL }
};

reiser4_internal plugin_set *plugin_set_get_empty(void)
{
	return &empty_set;
}

reiser4_internal void plugin_set_put(plugin_set *set)
{
}

reiser4_internal plugin_set *plugin_set_clone(plugin_set *set)
{
	return set;
}

static inline unsigned long *
pset_field(plugin_set *set, int offset)
{
	return (unsigned long *)(((char *)set) + offset);
}

static int plugin_set_field(plugin_set **set, const unsigned long val, const int offset)
{
	unsigned long      *spot;
	spinlock_t *lock;
	plugin_set  replica;
	plugin_set *twin;
	plugin_set *psal;
	plugin_set *orig;

	assert("nikita-2902", set != NULL);
	assert("nikita-2904", *set != NULL);

	spot = pset_field(*set, offset);
	if (unlikely(*spot == val))
		return 0;

	replica = *(orig = *set);
	*pset_field(&replica, offset) = val;
	replica.hashval = calculate_hash(&replica);
	rcu_read_lock();
	twin = ps_hash_find(&ps_table, &replica.hashval);
	if (unlikely(twin == NULL)) {
		rcu_read_unlock();
		psal = kmem_cache_alloc(plugin_set_slab, GFP_KERNEL);
		if (psal == NULL)
			return RETERR(-ENOMEM);
		*psal = replica;
		lock = &plugin_set_lock[replica.hashval & 7];
		spin_lock(lock);
		twin = ps_hash_find(&ps_table, &replica.hashval);
		if (likely(twin == NULL)) {
			*set = psal;
			ps_hash_insert_rcu(&ps_table, psal);
		} else {
			*set = twin;
			kmem_cache_free(plugin_set_slab, psal);
		}
		spin_unlock(lock);
	} else {
		rcu_read_unlock();
		*set = twin;
	}
	return 0;
}

static struct {
	int                 offset;
	reiser4_plugin_type type;
} pset_descr[PSET_LAST] = {
	[PSET_FILE] = {
		.offset = offsetof(plugin_set, file),
		.type   = REISER4_FILE_PLUGIN_TYPE
	},
	[PSET_DIR] = {
		.offset = offsetof(plugin_set, dir),
		.type   = REISER4_DIR_PLUGIN_TYPE
	},
	[PSET_PERM] = {
		.offset = offsetof(plugin_set, perm),
		.type   = REISER4_PERM_PLUGIN_TYPE
	},
	[PSET_FORMATTING] = {
		.offset = offsetof(plugin_set, formatting),
		.type   = REISER4_FORMATTING_PLUGIN_TYPE
	},
	[PSET_HASH] = {
		.offset = offsetof(plugin_set, hash),
		.type   = REISER4_HASH_PLUGIN_TYPE
	},
	[PSET_FIBRATION] = {
		.offset = offsetof(plugin_set, fibration),
		.type   = REISER4_FIBRATION_PLUGIN_TYPE
	},
	[PSET_SD] = {
		.offset = offsetof(plugin_set, sd),
		.type   = REISER4_ITEM_PLUGIN_TYPE
	},
	[PSET_DIR_ITEM] = {
		.offset = offsetof(plugin_set, dir_item),
		.type   = REISER4_ITEM_PLUGIN_TYPE
	},
	[PSET_CRYPTO] = {
		.offset = offsetof(plugin_set, crypto),
		.type   = REISER4_CRYPTO_PLUGIN_TYPE
	},
	[PSET_DIGEST] = {
		.offset = offsetof(plugin_set, digest),
		.type   = REISER4_DIGEST_PLUGIN_TYPE
	},
	[PSET_COMPRESSION] = {
		.offset = offsetof(plugin_set, compression),
		.type   = REISER4_COMPRESSION_PLUGIN_TYPE
	}
};

int pset_set(plugin_set **set, pset_member memb, reiser4_plugin *plugin)
{
	assert("nikita-3492", set != NULL);
	assert("nikita-3493", *set != NULL);
	assert("nikita-3494", plugin != NULL);
	assert("nikita-3495", 0 <= memb && memb < PSET_LAST);
	assert("nikita-3496", plugin->h.type_id == pset_member_to_type(memb));

	return plugin_set_field(set,
				(unsigned long)plugin, pset_descr[memb].offset);
}

reiser4_plugin *pset_get(plugin_set *set, pset_member memb)
{
	assert("nikita-3497", set != NULL);
	assert("nikita-3498", 0 <= memb && memb < PSET_LAST);

	return *(reiser4_plugin **)(((char *)set) + pset_descr[memb].offset);
}

reiser4_plugin_type pset_member_to_type(pset_member memb)
{
	assert("nikita-3501", 0 <= memb && memb < PSET_LAST);
	return pset_descr[memb].type;
}

reiser4_plugin_type pset_member_to_type_unsafe(pset_member memb)
{
	if (0 <= memb && memb < PSET_LAST)
		return pset_descr[memb].type;
	else
		return REISER4_PLUGIN_TYPES;
}

#define DEFINE_PLUGIN_SET(type, field)					\
reiser4_internal int plugin_set_ ## field(plugin_set **set, type *val)	\
{									\
	cassert(sizeof val == sizeof(unsigned long));			\
	return plugin_set_field(set, (unsigned long)val,		\
				offsetof(plugin_set, field));		\
}

DEFINE_PLUGIN_SET(file_plugin, file)
DEFINE_PLUGIN_SET(dir_plugin, dir)
DEFINE_PLUGIN_SET(perm_plugin, perm)
DEFINE_PLUGIN_SET(formatting_plugin, formatting)
DEFINE_PLUGIN_SET(hash_plugin, hash)
DEFINE_PLUGIN_SET(fibration_plugin, fibration)
DEFINE_PLUGIN_SET(item_plugin, sd)
DEFINE_PLUGIN_SET(item_plugin, dir_item)
DEFINE_PLUGIN_SET(crypto_plugin, crypto)
DEFINE_PLUGIN_SET(digest_plugin, digest)
DEFINE_PLUGIN_SET(compression_plugin, compression)

reiser4_internal int plugin_set_init(void)
{
	int result;

	result = ps_hash_init(&ps_table, PS_TABLE_SIZE, NULL);
	if (result == 0) {
		plugin_set_slab = kmem_cache_create("plugin_set",
						    sizeof (plugin_set), 0,
						    SLAB_HWCACHE_ALIGN,
						    NULL, NULL);
		if (plugin_set_slab == NULL)
			result = RETERR(-ENOMEM);
	}
	return result;
}

reiser4_internal void plugin_set_done(void)
{
	/* NOTE: scan hash table and recycle all objects. */
	kmem_cache_destroy(plugin_set_slab);
	ps_hash_done(&ps_table);
}


/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
