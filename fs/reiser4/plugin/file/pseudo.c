/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/*
 * Pseudo file plugin. This contains helper functions used by pseudo files.
 */

#include "pseudo.h"
#include "../plugin.h"

#include "../../inode.h"

#include <linux/seq_file.h>
#include <linux/fs.h>

struct seq_operations pseudo_seq_op;

/* extract pseudo file plugin, stored in @file */
static pseudo_plugin *
get_pplug(struct file * file)
{
	struct inode  *inode;

	inode = file->f_dentry->d_inode;
	return reiser4_inode_data(inode)->file_plugin_data.pseudo_info.plugin;
}

/* common routine to open pseudo file. */
reiser4_internal int open_pseudo(struct inode * inode, struct file * file)
{
	int result;
	pseudo_plugin *pplug;

	pplug = get_pplug(file);

	/* for pseudo files based on seq_file interface */
	if (pplug->read_type == PSEUDO_READ_SEQ) {
		result = seq_open(file, &pplug->read.ops);
		if (result == 0) {
			struct seq_file *m;

			m = file->private_data;
			m->private = file;
		}
	} else if (pplug->read_type == PSEUDO_READ_SINGLE)
		/* for pseudo files containing one record */
		result = single_open(file, pplug->read.single_show, file);
	else
		result = 0;

	return result;
}

/* common read method for pseudo files */
reiser4_internal ssize_t read_pseudo(struct file *file,
				     char __user *buf, size_t size, loff_t *ppos)
{
	switch (get_pplug(file)->read_type) {
	case PSEUDO_READ_SEQ:
	case PSEUDO_READ_SINGLE:
		/* seq_file behaves like pipe, requiring @ppos to always be
		 * address of file->f_pos */
		return seq_read(file, buf, size, &file->f_pos);
	case PSEUDO_READ_FORWARD:
		return get_pplug(file)->read.read(file, buf, size, ppos);
	default:
		return 0;
	}
}

/* common seek method for pseudo files */
reiser4_internal loff_t seek_pseudo(struct file *file, loff_t offset, int origin)
{
	switch (get_pplug(file)->read_type) {
	case PSEUDO_READ_SEQ:
	case PSEUDO_READ_SINGLE:
		return seq_lseek(file, offset, origin);
	default:
		return 0;
	}
}

/* common release method for pseudo files */
reiser4_internal int release_pseudo(struct inode *inode, struct file *file)
{
	int result;

	switch (get_pplug(file)->read_type) {
	case PSEUDO_READ_SEQ:
	case PSEUDO_READ_SINGLE:
		result = seq_release(inode, file);
		file->private_data = NULL;
		break;
	default:
		result = 0;
	}
	return result;
}

/* pseudo files need special ->drop() method, because they don't have nlink
 * and only exist while host object does. */
reiser4_internal void drop_pseudo(struct inode * object)
{
	/* pseudo files are not protected from deletion by their ->i_nlink */
	generic_delete_inode(object);
}

/* common write method for pseudo files */
reiser4_internal ssize_t
write_pseudo(struct file *file,
	     const char __user *buf, size_t size, loff_t *ppos)
{
	ssize_t result;

	switch (get_pplug(file)->write_type) {
	case PSEUDO_WRITE_STRING: {
		char * inkernel;

		inkernel = getname(buf);
		if (!IS_ERR(inkernel)) {
			result = get_pplug(file)->write.gets(file, inkernel);
			putname(inkernel);
			if (result == 0)
				result = size;
		} else
			result = PTR_ERR(inkernel);
		break;
	}
	case PSEUDO_WRITE_FORWARD:
		result = get_pplug(file)->write.write(file, buf, size, ppos);
		break;
	default:
		result = size;
	}
	return result;
}

/* on-wire serialization of pseudo files. */

/* this is not implemented so far (and, hence, pseudo files are not accessible
 * over NFS, closing remote exploits a fortiori */

reiser4_internal int
wire_size_pseudo(struct inode *inode)
{
	return RETERR(-ENOTSUPP);
}

reiser4_internal char *
wire_write_pseudo(struct inode *inode, char *start)
{
	return ERR_PTR(RETERR(-ENOTSUPP));
}

reiser4_internal char *
wire_read_pseudo(char *addr, reiser4_object_on_wire *obj)
{
	return ERR_PTR(RETERR(-ENOTSUPP));
}

reiser4_internal void
wire_done_pseudo(reiser4_object_on_wire *obj)
{
	/* nothing to do */
}

reiser4_internal struct dentry *
wire_get_pseudo(struct super_block *sb, reiser4_object_on_wire *obj)
{
	return ERR_PTR(RETERR(-ENOTSUPP));
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
