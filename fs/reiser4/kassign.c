/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Key assignment policy implementation */

/*
 * In reiser4 every piece of file system data and meta-data has a key. Keys
 * are used to store information in and retrieve it from reiser4 internal
 * tree. In addition to this, keys define _ordering_ of all file system
 * information: things having close keys are placed into the same or
 * neighboring (in the tree order) nodes of the tree. As our block allocator
 * tries to respect tree order (see flush.c), keys also define order in which
 * things are laid out on the disk, and hence, affect performance directly.
 *
 * Obviously, assignment of keys to data and meta-data should be consistent
 * across whole file system. Algorithm that calculates a key for a given piece
 * of data or meta-data is referred to as "key assignment".
 *
 * Key assignment is too expensive to be implemented as a plugin (that is,
 * with an ability to support different key assignment schemas in the same
 * compiled kernel image). As a compromise, all key-assignment functions and
 * data-structures are collected in this single file, so that modifications to
 * key assignment algorithm can be localized. Additional changes may be
 * required in key.[ch].
 *
 * Current default reiser4 key assignment algorithm is dubbed "Plan A". As one
 * may guess, there is "Plan B" too.
 *
 */

/*
 * Additional complication with key assignment implementation is a requirement
 * to support different key length.
 */

/*
 *                   KEY ASSIGNMENT: PLAN A, LONG KEYS.
 *
 * DIRECTORY ITEMS
 *
 *  |       60     | 4 | 7 |1|   56        |        64        |        64       |
 *  +--------------+---+---+-+-------------+------------------+-----------------+
 *  |    dirid     | 0 | F |H|  prefix-1   |    prefix-2      |  prefix-3/hash  |
 *  +--------------+---+---+-+-------------+------------------+-----------------+
 *  |                  |                   |                  |                 |
 *  |    8 bytes       |      8 bytes      |     8 bytes      |     8 bytes     |
 *
 * dirid         objectid of directory this item is for
 *
 * F             fibration, see fs/reiser4/plugin/fibration.[ch]
 *
 * H             1 if last 8 bytes of the key contain hash,
 *               0 if last 8 bytes of the key contain prefix-3
 *
 * prefix-1      first 7 characters of file name.
 *               Padded by zeroes if name is not long enough.
 *
 * prefix-2      next 8 characters of the file name.
 *
 * prefix-3      next 8 characters of the file name.
 *
 * hash          hash of the rest of file name (i.e., portion of file
 *               name not included into prefix-1 and prefix-2).
 *
 * File names shorter than 23 (== 7 + 8 + 8) characters are completely encoded
 * in the key. Such file names are called "short". They are distinguished by H
 * bit set 0 in the key.
 *
 * Other file names are "long". For long name, H bit is 1, and first 15 (== 7
 * + 8) characters are encoded in prefix-1 and prefix-2 portions of the
 * key. Last 8 bytes of the key are occupied by hash of the remaining
 * characters of the name.
 *
 * This key assignment reaches following important goals:
 *
 *     (1) directory entries are sorted in approximately lexicographical
 *     order.
 *
 *     (2) collisions (when multiple directory items have the same key), while
 *     principally unavoidable in a tree with fixed length keys, are rare.
 *
 * STAT DATA
 *
 *  |       60     | 4 |       64        | 4 |     60       |        64       |
 *  +--------------+---+-----------------+---+--------------+-----------------+
 *  |  locality id | 1 |    ordering     | 0 |  objectid    |        0        |
 *  +--------------+---+-----------------+---+--------------+-----------------+
 *  |                  |                 |                  |                 |
 *  |    8 bytes       |    8 bytes      |     8 bytes      |     8 bytes     |
 *
 * locality id     object id of a directory where first name was created for
 *                 the object
 *
 * ordering        copy of second 8-byte portion of the key of directory
 *                 entry for the first name of this object. Ordering has a form
 *                         {
 *                                 fibration :7;
 *                                 h         :1;
 *                                 prefix1   :56;
 *                         }
 *                 see description of key for directory entry above.
 *
 * objectid        object id for this object
 *
 * This key assignment policy is designed to keep stat-data in the same order
 * as corresponding directory items, thus speeding up readdir/stat types of
 * workload.
 *
 * FILE BODY
 *
 *  |       60     | 4 |       64        | 4 |     60       |        64       |
 *  +--------------+---+-----------------+---+--------------+-----------------+
 *  |  locality id | 4 |    ordering     | 0 |  objectid    |      offset     |
 *  +--------------+---+-----------------+---+--------------+-----------------+
 *  |                  |                 |                  |                 |
 *  |    8 bytes       |    8 bytes      |     8 bytes      |     8 bytes     |
 *
 * locality id     object id of a directory where first name was created for
 *                 the object
 *
 * ordering        the same as in the key of stat-data for this object
 *
 * objectid        object id for this object
 *
 * offset          logical offset from the beginning of this file.
 *                 Measured in bytes.
 *
 *
 *                   KEY ASSIGNMENT: PLAN A, SHORT KEYS.
 *
 * DIRECTORY ITEMS
 *
 *  |       60     | 4 | 7 |1|   56        |        64       |
 *  +--------------+---+---+-+-------------+-----------------+
 *  |    dirid     | 0 | F |H|  prefix-1   |  prefix-2/hash  |
 *  +--------------+---+---+-+-------------+-----------------+
 *  |                  |                   |                 |
 *  |    8 bytes       |      8 bytes      |     8 bytes     |
 *
 * dirid         objectid of directory this item is for
 *
 * F             fibration, see fs/reiser4/plugin/fibration.[ch]
 *
 * H             1 if last 8 bytes of the key contain hash,
 *               0 if last 8 bytes of the key contain prefix-2
 *
 * prefix-1      first 7 characters of file name.
 *               Padded by zeroes if name is not long enough.
 *
 * prefix-2      next 8 characters of the file name.
 *
 * hash          hash of the rest of file name (i.e., portion of file
 *               name not included into prefix-1).
 *
 * File names shorter than 15 (== 7 + 8) characters are completely encoded in
 * the key. Such file names are called "short". They are distinguished by H
 * bit set in the key.
 *
 * Other file names are "long". For long name, H bit is 0, and first 7
 * characters are encoded in prefix-1 portion of the key. Last 8 bytes of the
 * key are occupied by hash of the remaining characters of the name.
 *
 * STAT DATA
 *
 *  |       60     | 4 | 4 |     60       |        64       |
 *  +--------------+---+---+--------------+-----------------+
 *  |  locality id | 1 | 0 |  objectid    |        0        |
 *  +--------------+---+---+--------------+-----------------+
 *  |                  |                  |                 |
 *  |    8 bytes       |     8 bytes      |     8 bytes     |
 *
 * locality id     object id of a directory where first name was created for
 *                 the object
 *
 * objectid        object id for this object
 *
 * FILE BODY
 *
 *  |       60     | 4 | 4 |     60       |        64       |
 *  +--------------+---+---+--------------+-----------------+
 *  |  locality id | 4 | 0 |  objectid    |      offset     |
 *  +--------------+---+---+--------------+-----------------+
 *  |                  |                  |                 |
 *  |    8 bytes       |     8 bytes      |     8 bytes     |
 *
 * locality id     object id of a directory where first name was created for
 *                 the object
 *
 * objectid        object id for this object
 *
 * offset          logical offset from the beginning of this file.
 *                 Measured in bytes.
 *
 *
 */

#include "debug.h"
#include "key.h"
#include "kassign.h"
#include "vfs_ops.h"
#include "inode.h"
#include "super.h"
#include "dscale.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block, etc  */

#if REISER4_LARGE_KEY
#define ORDERING_CHARS (sizeof(__u64) - 1)
#define OID_CHARS (sizeof(__u64))
#else
#define ORDERING_CHARS (0)
#define OID_CHARS (sizeof(__u64) - 1)
#endif

#define OFFSET_CHARS (sizeof(__u64))

#define INLINE_CHARS (ORDERING_CHARS + OID_CHARS)

/* bitmask for H bit (see comment at the beginning of this file */
static const __u64 longname_mark =  0x0100000000000000ull;
/* bitmask for F and H portions of the key. */
static const __u64 fibration_mask = 0xff00000000000000ull;

/* return true if name is not completely encoded in @key */
reiser4_internal int
is_longname_key(const reiser4_key *key)
{
	__u64 highpart;

	assert("nikita-2863", key != NULL);
	if (get_key_type(key) != KEY_FILE_NAME_MINOR)
		print_key("oops", key);
	assert("nikita-2864", get_key_type(key) == KEY_FILE_NAME_MINOR);

	if (REISER4_LARGE_KEY)
		highpart = get_key_ordering(key);
	else
		highpart = get_key_objectid(key);

	return (highpart & longname_mark) ? 1 : 0;
}

/* return true if @name is too long to be completely encoded in the key */
reiser4_internal int
is_longname(const char *name UNUSED_ARG, int len)
{
	return len > ORDERING_CHARS + OID_CHARS + OFFSET_CHARS;
}

/* code ascii string into __u64.

   Put characters of @name into result (@str) one after another starting
   from @start_idx-th highest (arithmetically) byte. This produces
   endian-safe encoding. memcpy(2) will not do.

*/
static __u64
pack_string(const char *name /* string to encode */ ,
	    int start_idx	/* highest byte in result from
				 * which to start encoding */ )
{
	unsigned i;
	__u64 str;

	str = 0;
	for (i = 0; (i < sizeof str - start_idx) && name[i]; ++i) {
		str <<= 8;
		str |= (unsigned char) name[i];
	}
	str <<= (sizeof str - i - start_idx) << 3;
	return str;
}

#if !REISER4_DEBUG_OUTPUT
static
#endif
/* opposite to pack_string(). Takes value produced by pack_string(), restores
 * string encoded in it and stores result in @buf */
reiser4_internal char *
unpack_string(__u64 value, char *buf)
{
	do {
		*buf = value >> (64 - 8);
		if (*buf)
			++ buf;
		value <<= 8;
	} while(value != 0);
	*buf = 0;
	return buf;
}

/* obtain name encoded in @key and store it in @buf */
reiser4_internal char *
extract_name_from_key(const reiser4_key *key, char *buf)
{
	char *c;

	assert("nikita-2868", !is_longname_key(key));

	c = buf;
	if (REISER4_LARGE_KEY) {
		c = unpack_string(get_key_ordering(key) & ~fibration_mask, c);
		c = unpack_string(get_key_fulloid(key), c);
	} else
		c = unpack_string(get_key_fulloid(key) & ~fibration_mask, c);
	unpack_string(get_key_offset(key), c);
	return buf;
}

/* build key for directory entry.
   ->build_entry_key() for directory plugin */
reiser4_internal void
build_entry_key_common(const struct inode *dir	/* directory where entry is
						 * (or will be) in.*/ ,
		       const struct qstr *qname	/* name of file referenced
						 * by this entry */ ,
		       reiser4_key * result	/* resulting key of directory
						 * entry */ )
{
	__u64 ordering;
	__u64 objectid;
	__u64 offset;
	const char *name;
	int len;

#if REISER4_LARGE_KEY
#define second_el ordering
#else
#define second_el objectid
#endif

	assert("nikita-1139", dir != NULL);
	assert("nikita-1140", qname != NULL);
	assert("nikita-1141", qname->name != NULL);
	assert("nikita-1142", result != NULL);

	name = qname->name;
	len  = qname->len;

	assert("nikita-2867", strlen(name) == len);

	key_init(result);
	/* locality of directory entry's key is objectid of parent
	   directory */
	set_key_locality(result, get_inode_oid(dir));
	/* minor packing locality is constant */
	set_key_type(result, KEY_FILE_NAME_MINOR);
	/* dot is special case---we always want it to be first entry in
	   a directory. Actually, we just want to have smallest
	   directory entry.
	*/
	if (len == 1 && name[0] == '.')
		return;

	/* This is our brand new proposed key allocation algorithm for
	   directory entries:

	   If name is shorter than 7 + 8 = 15 characters, put first 7
	   characters into objectid field and remaining characters (if
	   any) into offset field. Dream long dreamt came true: file
	   name as a key!

	   If file name is longer than 15 characters, put first 7
	   characters into objectid and hash of remaining characters
	   into offset field.

	   To distinguish above cases, in latter set up unused high bit
	   in objectid field.


	   With large keys (REISER4_LARGE_KEY) algorithm is updated
	   appropriately.
	*/

	/* objectid of key is composed of seven first characters of
	   file's name. This imposes global ordering on directory
	   entries.
	*/
	second_el = pack_string(name, 1);
	if (REISER4_LARGE_KEY) {
		if (len > ORDERING_CHARS)
			objectid = pack_string(name + ORDERING_CHARS, 0);
		else
			objectid = 0ull;
	}

	if (!is_longname(name, len)) {
		if (len > INLINE_CHARS)
			offset = pack_string(name + INLINE_CHARS, 0);
		else
			offset = 0ull;
	} else {
		/* note in a key the fact that offset contains hash. */
		second_el |= longname_mark;

		/* offset is the hash of the file name. */
		offset = inode_hash_plugin(dir)->hash(name + INLINE_CHARS,
						      len - INLINE_CHARS);
	}

	assert("nikita-3480", inode_fibration_plugin(dir) != NULL);
	second_el |= inode_fibration_plugin(dir)->fibre(dir, name, len);

	if (REISER4_LARGE_KEY) {
		set_key_ordering(result, ordering);
		set_key_fulloid(result, objectid);
	} else {
		/* objectid is 60 bits */
		assert("nikita-1405", !(objectid & ~KEY_OBJECTID_MASK));
		set_key_objectid(result, objectid);
	}
	set_key_offset(result, offset);
	return;
}

/* build key for directory entry.
   ->build_entry_key() for directory plugin

   This is for directories where we want repeatable and restartable readdir()
   even in case 32bit user level struct dirent (readdir(3)).
*/
reiser4_internal void
build_entry_key_stable_entry(const struct inode *dir	/* directory where
							 * entry is (or
							 * will be) in. */ ,
			     const struct qstr *name	/* name of file
							 * referenced by
							 * this entry */ ,
			     reiser4_key * result	/* resulting key of
							 * directory entry */ )
{
	oid_t objectid;

	assert("nikita-2283", dir != NULL);
	assert("nikita-2284", name != NULL);
	assert("nikita-2285", name->name != NULL);
	assert("nikita-2286", result != NULL);

	key_init(result);
	/* locality of directory entry's key is objectid of parent
	   directory */
	set_key_locality(result, get_inode_oid(dir));
	/* minor packing locality is constant */
	set_key_type(result, KEY_FILE_NAME_MINOR);
	/* dot is special case---we always want it to be first entry in
	   a directory. Actually, we just want to have smallest
	   directory entry.
	*/
	if ((name->len == 1) && (name->name[0] == '.'))
		return;

	/* objectid of key is 31 lowest bits of hash. */
	objectid = inode_hash_plugin(dir)->hash(name->name, (int) name->len) & 0x7fffffff;

	assert("nikita-2303", !(objectid & ~KEY_OBJECTID_MASK));
	set_key_objectid(result, objectid);

	/* offset is always 0. */
	set_key_offset(result, (__u64) 0);
	return;
}

/* build key to be used by ->readdir() method.

   See reiser4_readdir() for more detailed comment.
   Common implementation of dir plugin's method build_readdir_key
*/
reiser4_internal int
build_readdir_key_common(struct file *dir /* directory being read */ ,
			 reiser4_key * result /* where to store key */ )
{
	reiser4_file_fsdata *fdata;
	struct inode *inode;

	assert("nikita-1361", dir != NULL);
	assert("nikita-1362", result != NULL);
	assert("nikita-1363", dir->f_dentry != NULL);
	inode = dir->f_dentry->d_inode;
	assert("nikita-1373", inode != NULL);

	fdata = reiser4_get_file_fsdata(dir);
	if (IS_ERR(fdata))
		return PTR_ERR(fdata);
	assert("nikita-1364", fdata != NULL);
	return extract_key_from_de_id(get_inode_oid(inode), &fdata->dir.readdir.position.dir_entry_key, result);

}

/* true, if @key is the key of "." */
reiser4_internal int
is_dot_key(const reiser4_key * key /* key to check */ )
{
	assert("nikita-1717", key != NULL);
	assert("nikita-1718", get_key_type(key) == KEY_FILE_NAME_MINOR);
	return
		(get_key_ordering(key) == 0ull) &&
		(get_key_objectid(key) == 0ull) &&
		(get_key_offset(key) == 0ull);
}

/* build key for stat-data.

   return key of stat-data of this object. This should became sd plugin
   method in the future. For now, let it be here.

*/
reiser4_internal reiser4_key *
build_sd_key(const struct inode * target /* inode of an object */ ,
	     reiser4_key * result	/* resulting key of @target
					   stat-data */ )
{
	assert("nikita-261", result != NULL);

	key_init(result);
	set_key_locality(result, reiser4_inode_data(target)->locality_id);
	set_key_ordering(result, get_inode_ordering(target));
	set_key_objectid(result, get_inode_oid(target));
	set_key_type(result, KEY_SD_MINOR);
	set_key_offset(result, (__u64) 0);
	return result;
}

/* encode part of key into &obj_key_id

   This encodes into @id part of @key sufficient to restore @key later,
   given that latter is key of object (key of stat-data).

   See &obj_key_id
*/
reiser4_internal int
build_obj_key_id(const reiser4_key * key /* key to encode */ ,
		 obj_key_id * id /* id where key is encoded in */ )
{
	assert("nikita-1151", key != NULL);
	assert("nikita-1152", id != NULL);

	xmemcpy(id, key, sizeof *id);
	return 0;
}

/* encode reference to @obj in @id.

   This is like build_obj_key_id() above, but takes inode as parameter. */
reiser4_internal int
build_inode_key_id(const struct inode *obj /* object to build key of */ ,
		   obj_key_id * id /* result */ )
{
	reiser4_key sdkey;

	assert("nikita-1166", obj != NULL);
	assert("nikita-1167", id != NULL);

	build_sd_key(obj, &sdkey);
	build_obj_key_id(&sdkey, id);
	return 0;
}

/* decode @id back into @key

   Restore key of object stat-data from @id. This is dual to
   build_obj_key_id() above.
*/
reiser4_internal int
extract_key_from_id(const obj_key_id * id	/* object key id to extract key
						 * from */ ,
		    reiser4_key * key /* result */ )
{
	assert("nikita-1153", id != NULL);
	assert("nikita-1154", key != NULL);

	key_init(key);
	xmemcpy(key, id, sizeof *id);
	return 0;
}

/* extract objectid of directory from key of directory entry within said
   directory.
   */
reiser4_internal oid_t
extract_dir_id_from_key(const reiser4_key * de_key	/* key of
							 * directory
							 * entry */ )
{
	assert("nikita-1314", de_key != NULL);
	return get_key_locality(de_key);
}

/* encode into @id key of directory entry.

   Encode into @id information sufficient to later distinguish directory
   entries within the same directory. This is not whole key, because all
   directory entries within directory item share locality which is equal
   to objectid of their directory.

*/
reiser4_internal int
build_de_id(const struct inode *dir /* inode of directory */ ,
	    const struct qstr *name	/* name to be given to @obj by
					 * directory entry being
					 * constructed */ ,
	    de_id * id /* short key of directory entry */ )
{
	reiser4_key key;

	assert("nikita-1290", dir != NULL);
	assert("nikita-1292", id != NULL);

	/* NOTE-NIKITA this is suboptimal. */
	inode_dir_plugin(dir)->build_entry_key(dir, name, &key);
	return build_de_id_by_key(&key, id);
}

/* encode into @id key of directory entry.

   Encode into @id information sufficient to later distinguish directory
   entries within the same directory. This is not whole key, because all
   directory entries within directory item share locality which is equal
   to objectid of their directory.

*/
reiser4_internal int
build_de_id_by_key(const reiser4_key * entry_key	/* full key of directory
							 * entry */ ,
		   de_id * id /* short key of directory entry */ )
{
	xmemcpy(id, ((__u64 *) entry_key) + 1, sizeof *id);
	return 0;
}

/* restore from @id key of directory entry.

   Function dual to build_de_id(): given @id and locality, build full
   key of directory entry within directory item.

*/
reiser4_internal int
extract_key_from_de_id(const oid_t locality	/* locality of directory
						 * entry */ ,
		       const de_id * id /* directory entry id */ ,
		       reiser4_key * key /* result */ )
{
	/* no need to initialise key here: all fields are overwritten */
	xmemcpy(((__u64 *) key) + 1, id, sizeof *id);
	set_key_locality(key, locality);
	set_key_type(key, KEY_FILE_NAME_MINOR);
	return 0;
}

/* compare two &obj_key_id */
reiser4_internal cmp_t
key_id_cmp(const obj_key_id * i1 /* first object key id to compare */ ,
	   const obj_key_id * i2 /* second object key id to compare */ )
{
	reiser4_key k1;
	reiser4_key k2;

	extract_key_from_id(i1, &k1);
	extract_key_from_id(i2, &k2);
	return keycmp(&k1, &k2);
}

/* compare &obj_key_id with full key */
reiser4_internal cmp_t
key_id_key_cmp(const obj_key_id * id /* object key id to compare */ ,
	       const reiser4_key * key /* key to compare */ )
{
	reiser4_key k1;

	extract_key_from_id(id, &k1);
	return keycmp(&k1, key);
}

/* compare two &de_id's */
reiser4_internal cmp_t
de_id_cmp(const de_id * id1 /* first &de_id to compare */ ,
	  const de_id * id2 /* second &de_id to compare */ )
{
	/* NOTE-NIKITA ugly implementation */
	reiser4_key k1;
	reiser4_key k2;

	extract_key_from_de_id((oid_t) 0, id1, &k1);
	extract_key_from_de_id((oid_t) 0, id2, &k2);
	return keycmp(&k1, &k2);
}

/* compare &de_id with key */
reiser4_internal cmp_t
de_id_key_cmp(const de_id * id /* directory entry id to compare */ ,
	      const reiser4_key * key /* key to compare */ )
{
	cmp_t        result;
	reiser4_key *k1;

	k1 = (reiser4_key *)(((unsigned long)id) - sizeof key->el[0]);
	result = KEY_DIFF_EL(k1, key, 1);
	if (result == EQUAL_TO) {
		result = KEY_DIFF_EL(k1, key, 2);
		if (REISER4_LARGE_KEY && result == EQUAL_TO) {
			result = KEY_DIFF_EL(k1, key, 3);
		}
	}
	return result;
}

/* true if key of root directory sd */
reiser4_internal int
is_root_dir_key(const struct super_block *super /* super block to check */ ,
		const reiser4_key * key /* key to check */ )
{
	assert("nikita-1819", super != NULL);
	assert("nikita-1820", key != NULL);
	/* call disk plugin's root_dir_key method if it exists */
	if (get_super_private(super)->df_plug && get_super_private(super)->df_plug->root_dir_key)
		return keyeq(key, get_super_private(super)->df_plug->root_dir_key(super));
	return 0;
}

/*
 * return number of bytes necessary to encode @inode identity.
 */
int inode_onwire_size(const struct inode *inode)
{
	int result;

	result  = dscale_bytes(get_inode_oid(inode));
	result += dscale_bytes(get_inode_locality(inode));

	/*
	 * ordering is large (it usually has highest bits set), so it makes
	 * little sense to dscale it.
	 */
	if (REISER4_LARGE_KEY)
		result += sizeof(get_inode_ordering(inode));
	return result;
}

/*
 * encode @inode identity at @start
 */
char *build_inode_onwire(const struct inode *inode, char *start)
{
	start += dscale_write(start, get_inode_locality(inode));
	start += dscale_write(start, get_inode_oid(inode));

	if (REISER4_LARGE_KEY) {
		cputod64(get_inode_ordering(inode), (d64 *)start);
		start += sizeof(get_inode_ordering(inode));
	}
	return start;
}

/*
 * extract key that was previously encoded by build_inode_onwire() at @addr
 */
char *extract_obj_key_id_from_onwire(char *addr, obj_key_id *key_id)
{
	__u64 val;

	addr += dscale_read(addr, &val);
	val = (val << KEY_LOCALITY_SHIFT) | KEY_SD_MINOR;
	cputod64(val, (d64 *)key_id->locality);
	addr += dscale_read(addr, &val);
	cputod64(val, (d64 *)key_id->objectid);
#if REISER4_LARGE_KEY
	memcpy(&key_id->ordering, addr, sizeof key_id->ordering);
	addr += sizeof key_id->ordering;
#endif
	return addr;
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
