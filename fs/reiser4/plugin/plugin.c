/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Basic plugin infrastructure, lookup etc. */

/* PLUGINS:

   Plugins are internal Reiser4 "modules" or "objects" used to increase
   extensibility and allow external users to easily adapt reiser4 to
   their needs.

   Plugins are classified into several disjoint "types". Plugins
   belonging to the particular plugin type are termed "instances" of
   this type. Currently the following types are present:

    . object plugin
    . hash plugin
    . tail plugin
    . perm plugin
    . item plugin
    . node layout plugin

NIKITA-FIXME-HANS: update this list, and review this entire comment for currency

   Object (file) plugin determines how given file-system object serves
   standard VFS requests for read, write, seek, mmap etc. Instances of
   file plugins are: regular file, directory, symlink. Another example
   of file plugin is audit plugin, that optionally records accesses to
   underlying object and forwards requests to it.

   Hash plugins compute hashes used by reiser4 to store and locate
   files within directories. Instances of hash plugin type are: r5,
   tea, rupasov.

   Tail plugins (or, more precisely, tail policy plugins) determine
   when last part of the file should be stored in a formatted item.

   Perm plugins control permissions granted for a process accessing a file.

   Scope and lookup:

   label such that pair ( type_label, plugin_label ) is unique.  This
   pair is a globally persistent and user-visible plugin
   identifier. Internally kernel maintains plugins and plugin types in
   arrays using an index into those arrays as plugin and plugin type
   identifiers. File-system in turn, also maintains persistent
   "dictionary" which is mapping from plugin label to numerical
   identifier which is stored in file-system objects.  That is, we
   store the offset into the plugin array for that plugin type as the
   plugin id in the stat data of the filesystem object.

   plugin_labels have meaning for the user interface that assigns
   plugins to files, and may someday have meaning for dynamic loading of
   plugins and for copying of plugins from one fs instance to
   another by utilities like cp and tar.

   Internal kernel plugin type identifier (index in plugins[] array) is
   of type reiser4_plugin_type. Set of available plugin types is
   currently static, but dynamic loading doesn't seem to pose
   insurmountable problems.

   Within each type plugins are addressed by the identifiers of type
   reiser4_plugin_id (indices in
   reiser4_plugin_type_data.builtin[]). Such identifiers are only
   required to be unique within one type, not globally.

   Thus, plugin in memory is uniquely identified by the pair (type_id,
   id).

   Usage:

   There exists only one instance of each plugin instance, but this
   single instance can be associated with many entities (file-system
   objects, items, nodes, transactions, file-descriptors etc.). Entity
   to which plugin of given type is termed (due to the lack of
   imagination) "subject" of this plugin type and, by abuse of
   terminology, subject of particular instance of this type to which
   it's attached currently. For example, inode is subject of object
   plugin type. Inode representing directory is subject of directory
   plugin, hash plugin type and some particular instance of hash plugin
   type. Inode, representing regular file is subject of "regular file"
   plugin, tail-policy plugin type etc.

   With each subject the plugin possibly stores some state. For example,
   the state of a directory plugin (instance of object plugin type) is pointer
   to hash plugin (if directories always use hashing that is). State of
   audit plugin is file descriptor (struct file) of log file or some
   magic value to do logging through printk().

   Interface:

   In addition to a scalar identifier, each plugin type and plugin
   proper has a "label": short string and a "description"---longer
   descriptive string. Labels and descriptions of plugin types are
   hard-coded into plugins[] array, declared and defined in
   plugin.c. Label and description of plugin are stored in .label and
   .desc fields of reiser4_plugin_header respectively. It's possible to
   locate plugin by the pair of labels.

   Features:

    . user-level plugin manipulations:
      + reiser4("filename/..file_plugin<='audit'");
      + write(open("filename/..file_plugin"), "audit", 8);

    . user level utilities lsplug and chplug to manipulate plugins.
      Utilities are not of primary priority. Possibly they will be not
      working on v4.0

NIKITA-FIXME-HANS: this should be a mkreiserfs option not a mount option, do you agree?  I don't think that specifying it at mount time, and then changing it with each mount, is a good model for usage.

    . mount option "plug" to set-up plugins of root-directory.
      "plug=foo:bar" will set "bar" as default plugin of type "foo".

   Limitations:

    . each plugin type has to provide at least one builtin
      plugin. This is technical limitation and it can be lifted in the
      future.

   TODO:

   New plugin types/plugings:
   Things we should be able to separately choose to inherit:

   security plugins

   stat data

   file bodies

   file plugins

   dir plugins

    . perm:acl

    d audi---audit plugin intercepting and possibly logging all
      accesses to object. Requires to put stub functions in file_operations
      in stead of generic_file_*.

NIKITA-FIXME-HANS: why make overflows a plugin?
    . over---handle hash overflows

    . sqnt---handle different access patterns and instruments read-ahead

NIKITA-FIXME-HANS: describe the line below in more detail.

    . hier---handle inheritance of plugins along file-system hierarchy

   Different kinds of inheritance: on creation vs. on access.
   Compatible/incompatible plugins.
   Inheritance for multi-linked files.
   Layered plugins.
   Notion of plugin context is abandoned.

Each file is associated
   with one plugin and dependant plugins (hash, etc.) are stored as
   main plugin state. Now, if we have plugins used for regular files
   but not for directories, how such plugins would be inherited?
    . always store them with directories also

NIKTIA-FIXME-HANS: Do the line above.  It is not exclusive of doing the line below which is also useful.

    . use inheritance hierarchy, independent of file-system namespace

*/

#include "../debug.h"
#include "../dformat.h"
#include "plugin_header.h"
#include "item/static_stat.h"
#include "node/node.h"
#include "security/perm.h"
#include "space/space_allocator.h"
#include "disk_format/disk_format.h"
#include "plugin.h"
#include "../reiser4.h"
#include "../jnode.h"
#include "../inode.h"

#include <linux/fs.h>		/* for struct super_block  */

/* public interface */

/* initialise plugin sub-system. Just call this once on reiser4 startup. */
int init_plugins(void);
int setup_plugins(struct super_block *super, reiser4_plugin ** area);
reiser4_plugin *lookup_plugin(const char *type_label, const char *plug_label);
reiser4_plugin *lookup_plugin_name(char *plug_label);
int locate_plugin(struct inode *inode, plugin_locator * loc);

/* internal functions. */

static reiser4_plugin_type find_type(const char *label);
static reiser4_plugin *find_plugin(reiser4_plugin_type_data * ptype, const char *label);

/* initialise plugin sub-system. Just call this once on reiser4 startup. */
reiser4_internal int
init_plugins(void)
{
	reiser4_plugin_type type_id;

	ON_TRACE(TRACE_PLUGINS, "Builtin plugins:\n");
	for (type_id = 0; type_id < REISER4_PLUGIN_TYPES; ++type_id) {
		reiser4_plugin_type_data *ptype;
		int i;

		ptype = &plugins[type_id];
		assert("nikita-3508", ptype->label != NULL);
		assert("nikita-3509", ptype->type_id == type_id);

		plugin_list_init(&ptype->plugins_list);
		ON_TRACE(TRACE_PLUGINS,
			 "Of type %s (%s):\n", ptype->label, ptype->desc);
/* NIKITA-FIXME-HANS: change builtin_num to some other name lacking the term builtin. */
		for (i = 0; i < ptype->builtin_num; ++i) {
			reiser4_plugin *plugin;

			plugin = plugin_at(ptype, i);

			if (plugin->h.label == NULL)
				/* uninitialized slot encountered */
				continue;
			assert("nikita-3445", plugin->h.type_id == type_id);
			plugin->h.id = i;
			IF_TRACE(TRACE_PLUGINS, print_plugin("\t", plugin));
			if (plugin->h.pops != NULL &&
			    plugin->h.pops->init != NULL) {
				int result;

				result = plugin->h.pops->init(plugin);
				if (result != 0)
					return result;
			}
			plugin_list_clean(plugin);
			plugin_list_push_back(&ptype->plugins_list, plugin);
		}
	}
	return 0;
}

/* lookup plugin name by scanning tables */
reiser4_internal reiser4_plugin *
lookup_plugin_name(char *plug_label /* label to search for */ )
{
	reiser4_plugin_type type_id;
	reiser4_plugin *plugin;

/* DEMIDOV-FIXME-HANS: did you get Saveliev to agree that his name is not Vova?  If not, change to DEMIDOV-001 */
	assert("vova-001", plug_label != NULL);

	plugin = NULL;

	dinfo("lookup_plugin_name: %s\n", plug_label);

	for (type_id = 0; type_id < REISER4_PLUGIN_TYPES; ++type_id) {
		plugin = find_plugin(&plugins[type_id], plug_label);
		if (plugin != NULL)
			break;
	}
	return plugin;
}

/* true if plugin type id is valid */
reiser4_internal int
is_type_id_valid(reiser4_plugin_type type_id /* plugin type id */)
{
	/* "type_id" is unsigned, so no comparison with 0 is
	   necessary */
	return (type_id < REISER4_PLUGIN_TYPES);
}

/* true if plugin id is valid */
reiser4_internal int
is_plugin_id_valid(reiser4_plugin_type type_id /* plugin type id */ ,
		   reiser4_plugin_id id /* plugin id */)
{
	assert("nikita-1653", is_type_id_valid(type_id));
	return ((id < plugins[type_id].builtin_num) && (id >= 0));
}

/* lookup plugin by scanning tables */
reiser4_internal reiser4_plugin *
lookup_plugin(const char *type_label /* plugin type label */ ,
	      const char *plug_label /* plugin label */ )
{
	reiser4_plugin *result;
	reiser4_plugin_type type_id;

	assert("nikita-546", type_label != NULL);
	assert("nikita-547", plug_label != NULL);

	type_id = find_type(type_label);
	if (is_type_id_valid(type_id))
		result = find_plugin(&plugins[type_id], plug_label);
	else
		result = NULL;
	return result;
}

/* return plugin by its @type_id and @id.

   Both arguments are checked for validness: this is supposed to be called
   from user-level.

NIKITA-FIXME-HANS: Do you instead mean that this checks ids created in
user space, and passed to the filesystem by use of method files? Your
comment really confused me on the first reading....

*/
reiser4_internal reiser4_plugin *
plugin_by_unsafe_id(reiser4_plugin_type type_id	/* plugin
						 * type id,
						 * unchecked */ ,
		    reiser4_plugin_id id	/* plugin id,
						 * unchecked */ )
{
	if (is_type_id_valid(type_id)) {
		if (is_plugin_id_valid(type_id, id))
			return plugin_at(&plugins[type_id], id);
		else
			/* id out of bounds */
			warning("nikita-2913",
				"Invalid plugin id: [%i:%i]", type_id, id);
	} else
		/* type_id out of bounds */
		warning("nikita-2914", "Invalid type_id: %i", type_id);
	return NULL;
}

/* convert plugin id to the disk format */
reiser4_internal int
save_plugin_id(reiser4_plugin * plugin /* plugin to convert */ ,
	       d16 * area /* where to store result */ )
{
	assert("nikita-1261", plugin != NULL);
	assert("nikita-1262", area != NULL);

	cputod16((__u16) plugin->h.id, area);
	return 0;
}

/* list of all plugins of given type */
reiser4_internal plugin_list_head *
get_plugin_list(reiser4_plugin_type type_id	/* plugin type
						 * id */ )
{
	assert("nikita-1056", is_type_id_valid(type_id));
	return &plugins[type_id].plugins_list;
}

#if REISER4_DEBUG_OUTPUT
/* print human readable plugin information */
reiser4_internal void
print_plugin(const char *prefix /* prefix to print */ ,
	     reiser4_plugin * plugin /* plugin to print */ )
{
	if (plugin != NULL) {
		printk("%s: %s (%s:%i)\n", prefix, plugin->h.desc, plugin->h.label, plugin->h.id);
	} else
		printk("%s: (nil)\n", prefix);
}

#endif

/* find plugin type by label */
static reiser4_plugin_type
find_type(const char *label	/* plugin type
				 * label */ )
{
	reiser4_plugin_type type_id;

	assert("nikita-550", label != NULL);

	for (type_id = 0; type_id < REISER4_PLUGIN_TYPES &&
		     strcmp(label, plugins[type_id].label); ++type_id) {
		;
	}
	return type_id;
}

/* given plugin label find it within given plugin type by scanning
    array. Used to map user-visible symbolic name to internal kernel
    id */
static reiser4_plugin *
find_plugin(reiser4_plugin_type_data * ptype	/* plugin
						 * type to
						 * find
						 * plugin
						 * within */ ,
	    const char *label /* plugin label */ )
{
	int i;
	reiser4_plugin *result;

	assert("nikita-551", ptype != NULL);
	assert("nikita-552", label != NULL);

	for (i = 0; i < ptype->builtin_num; ++i) {
		result = plugin_at(ptype, i);
		if (result->h.label == NULL)
			continue;
		if (!strcmp(result->h.label, label))
			return result;
	}
	return NULL;
}

int
grab_plugin(struct inode *self, struct inode *ancestor, pset_member memb)
{
	reiser4_plugin *plug;
	reiser4_inode *parent;

	parent = reiser4_inode_data(ancestor);
	plug = pset_get(parent->hset, memb) ? : pset_get(parent->pset, memb);
	return grab_plugin_from(self, memb, plug);
}

static void
update_plugin_mask(reiser4_inode *info, pset_member memb)
{
	struct dentry *rootdir;
	reiser4_inode *root;

	rootdir = inode_by_reiser4_inode(info)->i_sb->s_root;
	if (rootdir != NULL) {
		root = reiser4_inode_data(rootdir->d_inode);
		/*
		 * if inode is different from the default one, or we are
		 * changing plugin of root directory, update plugin_mask
		 */
		if (pset_get(info->pset, memb) != pset_get(root->pset, memb) ||
		    info == root)
			info->plugin_mask |= (1 << memb);
	}
}

int
grab_plugin_from(struct inode *self, pset_member memb, reiser4_plugin *plug)
{
	reiser4_inode *info;
	int            result = 0;

	info = reiser4_inode_data(self);
	if (pset_get(info->pset, memb) == NULL) {
		result = pset_set(&info->pset, memb, plug);
		if (result == 0)
			update_plugin_mask(info, memb);
	}
	return result;
}

int
force_plugin(struct inode *self, pset_member memb, reiser4_plugin *plug)
{
	reiser4_inode *info;
	int            result = 0;

	info = reiser4_inode_data(self);
	if (plug->h.pops != NULL && plug->h.pops->change != NULL)
		result = plug->h.pops->change(self, plug);
	else
		result = pset_set(&info->pset, memb, plug);
	if (result == 0)
		update_plugin_mask(info, memb);
	return result;
}

/* defined in fs/reiser4/plugin/file.c */
extern file_plugin file_plugins[LAST_FILE_PLUGIN_ID];
/* defined in fs/reiser4/plugin/dir.c */
extern dir_plugin dir_plugins[LAST_DIR_ID];
/* defined in fs/reiser4/plugin/item/static_stat.c */
extern sd_ext_plugin sd_ext_plugins[LAST_SD_EXTENSION];
/* defined in fs/reiser4/plugin/hash.c */
extern hash_plugin hash_plugins[LAST_HASH_ID];
/* defined in fs/reiser4/plugin/fibration.c */
extern fibration_plugin fibration_plugins[LAST_FIBRATION_ID];
/* defined in fs/reiser4/plugin/crypt.c */
extern crypto_plugin crypto_plugins[LAST_CRYPTO_ID];
/* defined in fs/reiser4/plugin/digest.c */
extern digest_plugin digest_plugins[LAST_DIGEST_ID];
/* defined in fs/reiser4/plugin/compress.c */
extern compression_plugin compression_plugins[LAST_COMPRESSION_ID];
/* defined in fs/reiser4/plugin/tail.c */
extern formatting_plugin formatting_plugins[LAST_TAIL_FORMATTING_ID];
/* defined in fs/reiser4/plugin/security/security.c */
extern perm_plugin perm_plugins[LAST_PERM_ID];
/* defined in fs/reiser4/plugin/item/item.c */
extern item_plugin item_plugins[LAST_ITEM_ID];
/* defined in fs/reiser4/plugin/node/node.c */
extern node_plugin node_plugins[LAST_NODE_ID];
/* defined in fs/reiser4/plugin/disk_format/disk_format.c */
extern disk_format_plugin format_plugins[LAST_FORMAT_ID];
/* defined in jnode.c */
extern jnode_plugin jnode_plugins[LAST_JNODE_TYPE];
/* defined in plugin/pseudo.c */
extern pseudo_plugin pseudo_plugins[LAST_PSEUDO_ID];

reiser4_plugin_type_data plugins[REISER4_PLUGIN_TYPES] = {
	/* C90 initializers */
	[REISER4_FILE_PLUGIN_TYPE] = {
		.type_id = REISER4_FILE_PLUGIN_TYPE,
		.label = "file",
		.desc = "Object plugins",
		.builtin_num = sizeof_array(file_plugins),
		.builtin = file_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (file_plugin)
	},
	[REISER4_DIR_PLUGIN_TYPE] = {
		.type_id = REISER4_DIR_PLUGIN_TYPE,
		.label = "dir",
		.desc = "Directory plugins",
		.builtin_num = sizeof_array(dir_plugins),
		.builtin = dir_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (dir_plugin)
	},
	[REISER4_HASH_PLUGIN_TYPE] = {
		.type_id = REISER4_HASH_PLUGIN_TYPE,
		.label = "hash",
		.desc = "Directory hashes",
		.builtin_num = sizeof_array(hash_plugins),
		.builtin = hash_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (hash_plugin)
	},
	[REISER4_FIBRATION_PLUGIN_TYPE] = {
		.type_id = REISER4_FIBRATION_PLUGIN_TYPE,
		.label = "fibration",
		.desc = "Directory fibrations",
		.builtin_num = sizeof_array(fibration_plugins),
		.builtin = fibration_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (fibration_plugin)
	},
	[REISER4_CRYPTO_PLUGIN_TYPE] = {
		.type_id = REISER4_CRYPTO_PLUGIN_TYPE,
		.label = "crypto",
		.desc = "Crypto plugins",
		.builtin_num = sizeof_array(crypto_plugins),
		.builtin = crypto_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (crypto_plugin)
	},
	[REISER4_DIGEST_PLUGIN_TYPE] = {
		.type_id = REISER4_DIGEST_PLUGIN_TYPE,
		.label = "digest",
		.desc = "Digest plugins",
		.builtin_num = sizeof_array(digest_plugins),
		.builtin = digest_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (digest_plugin)
	},
	[REISER4_COMPRESSION_PLUGIN_TYPE] = {
		.type_id = REISER4_COMPRESSION_PLUGIN_TYPE,
		.label = "compression",
		.desc = "Compression plugins",
		.builtin_num = sizeof_array(compression_plugins),
		.builtin = compression_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (compression_plugin)
	},

	[REISER4_FORMATTING_PLUGIN_TYPE] = {
		.type_id = REISER4_FORMATTING_PLUGIN_TYPE,
		.label = "formatting",
		.desc = "Tail inlining policies",
		.builtin_num = sizeof_array(formatting_plugins),
		.builtin = formatting_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (formatting_plugin)
	},
	[REISER4_PERM_PLUGIN_TYPE] = {
		.type_id = REISER4_PERM_PLUGIN_TYPE,
		.label = "perm",
		.desc = "Permission checks",
		.builtin_num = sizeof_array(perm_plugins),
		.builtin = perm_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (perm_plugin)
	},
	[REISER4_ITEM_PLUGIN_TYPE] = {
		.type_id = REISER4_ITEM_PLUGIN_TYPE,
		.label = "item",
		.desc = "Item handlers",
		.builtin_num = sizeof_array(item_plugins),
		.builtin = item_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (item_plugin)
	},
	[REISER4_NODE_PLUGIN_TYPE] = {
		.type_id = REISER4_NODE_PLUGIN_TYPE,
		.label = "node",
		.desc = "node layout handlers",
		.builtin_num = sizeof_array(node_plugins),
		.builtin = node_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (node_plugin)
	},
	[REISER4_SD_EXT_PLUGIN_TYPE] = {
		.type_id = REISER4_SD_EXT_PLUGIN_TYPE,
		.label = "sd_ext",
		.desc = "Parts of stat-data",
		.builtin_num = sizeof_array(sd_ext_plugins),
		.builtin = sd_ext_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (sd_ext_plugin)
	},
	[REISER4_FORMAT_PLUGIN_TYPE] = {
		.type_id = REISER4_FORMAT_PLUGIN_TYPE,
		.label = "disk_layout",
		.desc = "defines filesystem on disk layout",
		.builtin_num = sizeof_array(format_plugins),
		.builtin = format_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (disk_format_plugin)
	},
	[REISER4_JNODE_PLUGIN_TYPE] = {
		.type_id = REISER4_JNODE_PLUGIN_TYPE,
		.label = "jnode",
		.desc = "defines kind of jnode",
		.builtin_num = sizeof_array(jnode_plugins),
		.builtin = jnode_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (jnode_plugin)
	},
	[REISER4_PSEUDO_PLUGIN_TYPE] = {
		.type_id = REISER4_PSEUDO_PLUGIN_TYPE,
		.label = "pseudo_file",
		.desc = "pseudo file",
		.builtin_num = sizeof_array(pseudo_plugins),
		.builtin = pseudo_plugins,
		.plugins_list = TYPE_SAFE_LIST_HEAD_ZERO,
		.size = sizeof (pseudo_plugin)
	}
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
