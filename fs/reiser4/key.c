/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Key manipulations. */

#include "debug.h"
#include "key.h"
#include "super.h"
#include "reiser4.h"

#include <linux/types.h>	/* for __u??  */

/* Minimal possible key: all components are zero. It is presumed that this is
   independent of key scheme. */
static const reiser4_key MINIMAL_KEY = {
	.el = {
		{0ull},
		ON_LARGE_KEY({0ull},)
		{0ull},
		{0ull}
	}
};

/* Maximal possible key: all components are ~0. It is presumed that this is
   independent of key scheme. */
static const reiser4_key MAXIMAL_KEY = {
	.el = {
		{~0ull},
		ON_LARGE_KEY({~0ull},)
		{~0ull},
		{~0ull}
	}
};

/* Initialise key. */
reiser4_internal void
key_init(reiser4_key * key /* key to init */ )
{
	assert("nikita-1169", key != NULL);
	xmemset(key, 0, sizeof *key);
}

/* minimal possible key in the tree. Return pointer to the static storage. */
reiser4_internal const reiser4_key *
min_key(void)
{
	return &MINIMAL_KEY;
}

/* maximum possible key in the tree. Return pointer to the static storage. */
reiser4_internal const reiser4_key *
max_key(void)
{
	return &MAXIMAL_KEY;
}

#if REISER4_DEBUG_OUTPUT
/* debugging aid: print symbolic name of key type */
static const char *
type_name(unsigned int key_type /* key type */ )
{
	switch (key_type) {
	case KEY_FILE_NAME_MINOR:
		return "file name";
	case KEY_SD_MINOR:
		return "stat data";
	case KEY_ATTR_NAME_MINOR:
		return "attr name";
	case KEY_ATTR_BODY_MINOR:
		return "attr body";
	case KEY_BODY_MINOR:
		return "file body";
	default:
		return "unknown";
	}
}

extern char *unpack_string(__u64 value, char *buf);

/* debugging aid: print human readable information about key */
reiser4_internal void
print_key(const char *prefix /* prefix to print */ ,
	  const reiser4_key * key /* key to print */ )
{
	/* turn bold on */
	/* printf ("\033[1m"); */
	if (key == NULL)
		printk("%s: null key\n", prefix);
	else {
		if (REISER4_LARGE_KEY)
			printk("%s: (%Lx:%x:%Lx:%Lx:%Lx:%Lx)", prefix,
			       get_key_locality(key),
			       get_key_type(key),
			       get_key_ordering(key),
			       get_key_band(key),
			       get_key_objectid(key),
			       get_key_offset(key));
		else
			printk("%s: (%Lx:%x:%Lx:%Lx:%Lx)", prefix,
			       get_key_locality(key),
			       get_key_type(key),
			       get_key_band(key),
			       get_key_objectid(key),
			       get_key_offset(key));
		/*
		 * if this is a key of directory entry, try to decode part of
		 * a name stored in the key, and output it.
		 */
		if (get_key_type(key) == KEY_FILE_NAME_MINOR) {
			char buf[DE_NAME_BUF_LEN];
			char *c;

			c = buf;
			c = unpack_string(get_key_ordering(key), c);
			unpack_string(get_key_fulloid(key), c);
			printk("[%s", buf);
			if (is_longname_key(key))
				/*
				 * only part of the name is stored in the key.
				 */
				printk("...]\n");
			else {
				/*
				 * whole name is stored in the key.
				 */
				unpack_string(get_key_offset(key), buf);
				printk("%s]\n", buf);
			}
		} else {
			printk("[%s]\n", type_name(get_key_type(key)));
		}
	}
	/* turn bold off */
	/* printf ("\033[m\017"); */
}

#endif

/* like print_key() but outputs key representation into @buffer. */
reiser4_internal int
sprintf_key(char *buffer /* buffer to print key into */ ,
	    const reiser4_key * key /* key to print */ )
{
	if (REISER4_LARGE_KEY)
		return sprintf(buffer, "(%Lx:%x:%Lx:%Lx:%Lx:%Lx)",
			       (unsigned long long)get_key_locality(key),
			       get_key_type(key),
			       (unsigned long long)get_key_ordering(key),
			       (unsigned long long)get_key_band(key),
			       (unsigned long long)get_key_objectid(key),
			       (unsigned long long)get_key_offset(key));
	else
		return sprintf(buffer, "(%Lx:%x:%Lx:%Lx:%Lx)",
			       (unsigned long long)get_key_locality(key),
			       get_key_type(key),
			       (unsigned long long)get_key_band(key),
			       (unsigned long long)get_key_objectid(key),
			       (unsigned long long)get_key_offset(key));
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
