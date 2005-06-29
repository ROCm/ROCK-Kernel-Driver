/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* plugin-sets. see fs/reiser4/plugin/plugin_set.c for details */

#if !defined( __PLUGIN_SET_H__ )
#define __PLUGIN_SET_H__

#include "../type_safe_hash.h"
#include "plugin.h"

#include <linux/rcupdate.h>

struct plugin_set;
typedef struct plugin_set plugin_set;

TYPE_SAFE_HASH_DECLARE(ps, plugin_set);

struct plugin_set {
	unsigned long               hashval;
	/* plugin of file */
	file_plugin        *file;
	/* plugin of dir */
	dir_plugin         *dir;
	/* perm plugin for this file */
	perm_plugin        *perm;
	/* tail policy plugin. Only meaningful for regular files */
	formatting_plugin  *formatting;
	/* hash plugin. Only meaningful for directories. */
	hash_plugin        *hash;
	/* fibration plugin. Only meaningful for directories. */
	fibration_plugin   *fibration;
	/* plugin of stat-data */
	item_plugin        *sd;
	/* plugin of items a directory is built of */
	item_plugin        *dir_item;
	/* crypto plugin */
	crypto_plugin      *crypto;
	/* digest plugin */
	digest_plugin      *digest;
	/* compression plugin */
	compression_plugin *compression;
	/* compression mode plugin */
	compression_mode_plugin * compression_mode;
	/* cluster plugin */
	cluster_plugin     *cluster;
	/* plugin of regular child should be created */
	regular_plugin     *regular_entry;
	ps_hash_link        link;
};

extern plugin_set *plugin_set_get_empty(void);
extern void        plugin_set_put(plugin_set *set);

extern int plugin_set_file            (plugin_set **set, file_plugin *plug);
extern int plugin_set_dir             (plugin_set **set, dir_plugin *plug);
extern int plugin_set_formatting      (plugin_set **set, formatting_plugin *plug);
extern int plugin_set_hash            (plugin_set **set, hash_plugin *plug);
extern int plugin_set_fibration       (plugin_set **set, fibration_plugin *plug);
extern int plugin_set_sd              (plugin_set **set, item_plugin *plug);
extern int plugin_set_crypto          (plugin_set **set, crypto_plugin *plug);
extern int plugin_set_digest          (plugin_set **set, digest_plugin *plug);
extern int plugin_set_compression     (plugin_set **set, compression_plugin *plug);
extern int plugin_set_compression_mode(plugin_set **set, compression_mode_plugin *plug);
extern int plugin_set_cluster         (plugin_set **set, cluster_plugin *plug);
extern int plugin_set_regular         (plugin_set **set, regular_plugin *plug);

extern int  plugin_set_init(void);
extern void plugin_set_done(void);

extern int pset_set(plugin_set **set, pset_member memb, reiser4_plugin *plugin);
extern reiser4_plugin *pset_get(plugin_set *set, pset_member memb);

extern reiser4_plugin_type pset_member_to_type_unsafe(pset_member memb);

/* __PLUGIN_SET_H__ */
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
