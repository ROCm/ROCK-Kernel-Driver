/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "../forward.h"
#include "../debug.h"
#include "item/static_stat.h"
#include "plugin.h"
#include "../tree.h"
#include "../vfs_ops.h"
#include "../inode.h"
#include "object.h"

#include <linux/types.h>
#include <linux/fs.h>		/* for struct inode */

/* symlink plugin's specific functions */

reiser4_internal int
create_symlink(struct inode *symlink,	/* inode of symlink */
	       struct inode *dir UNUSED_ARG,	/* parent directory */
	       reiser4_object_create_data * data	/* info passed
							   * to us, this
							   * is filled by
							   * reiser4()
							   * syscall in
							   * particular */ )
{
	int result;

	assert("nikita-680", symlink != NULL);
	assert("nikita-681", S_ISLNK(symlink->i_mode));
	assert("nikita-685", inode_get_flag(symlink, REISER4_NO_SD));
	assert("nikita-682", dir != NULL);
	assert("nikita-684", data != NULL);
	assert("nikita-686", data->id == SYMLINK_FILE_PLUGIN_ID);

	/*
	 * stat data of symlink has symlink extension in which we store
	 * symlink content, that is, path symlink is pointing to.
	 */
	reiser4_inode_data(symlink)->extmask |= (1 << SYMLINK_STAT);

	assert("vs-838", symlink->u.generic_ip == 0);
	symlink->u.generic_ip = (void *) data->name;

	assert("vs-843", symlink->i_size == 0);
	INODE_SET_FIELD(symlink, i_size, strlen(data->name));

	/* insert stat data appended with data->name */
	result = write_sd_by_inode_common(symlink);
	if (result) {
		/* FIXME-VS: Make sure that symlink->u.generic_ip is not attached
		   to kmalloced data */
		INODE_SET_FIELD(symlink, i_size, 0);
	} else {
		assert("vs-849", symlink->u.generic_ip && inode_get_flag(symlink, REISER4_GENERIC_PTR_USED));
		assert("vs-850", !memcmp((char *) symlink->u.generic_ip, data->name, (size_t) symlink->i_size + 1));
	}
	return result;
}

/* plugin->destroy_inode() */
reiser4_internal void
destroy_inode_symlink(struct inode * inode)
{
	assert("edward-799", inode_file_plugin(inode) == file_plugin_by_id(SYMLINK_FILE_PLUGIN_ID));
	assert("edward-800", !is_bad_inode(inode) && is_inode_loaded(inode));
	assert("edward-801", inode_get_flag(inode, REISER4_GENERIC_PTR_USED));
	assert("vs-839", S_ISLNK(inode->i_mode));

	reiser4_kfree_in_sb(inode->u.generic_ip, inode->i_sb);
	inode->u.generic_ip = 0;
	inode_clr_flag(inode, REISER4_GENERIC_PTR_USED);
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/

