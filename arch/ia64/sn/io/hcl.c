/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *  hcl - SGI's Hardware Graph compatibility layer.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/sn/sgi.h>
#include <linux/devfs_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>

#define HCL_NAME "SGI-HWGRAPH COMPATIBILITY DRIVER"
#define HCL_TEMP_NAME "HCL_TEMP_NAME_USED_FOR_HWGRAPH_VERTEX_CREATE"
#define HCL_TEMP_NAME_LEN 44 
#define HCL_VERSION "1.0"
devfs_handle_t hwgraph_root = NULL;

/*
 * Debug flag definition.
 */
#define OPTION_NONE             0x00
#define HCL_DEBUG_NONE 0x00000
#define HCL_DEBUG_ALL  0x0ffff
#if defined(CONFIG_HCL_DEBUG)
static unsigned int hcl_debug_init __initdata = HCL_DEBUG_NONE;
#endif
static unsigned int hcl_debug = HCL_DEBUG_NONE;
static unsigned int boot_options = OPTION_NONE;

/*
 * Some Global definitions.
 */
spinlock_t hcl_spinlock;
devfs_handle_t hcl_handle = NULL;

/*
 * HCL device driver.
 * The purpose of this device driver is to provide a facility 
 * for User Level Apps e.g. hinv, ioconfig etc. an ioctl path 
 * to manipulate label entries without having to implement
 * system call interfaces.  This methodology will enable us to 
 * make this feature module loadable.
 */
static int hcl_open(struct inode * inode, struct file * filp)
{
	if (hcl_debug) {
        	printk("HCL: hcl_open called.\n");
	}

        return(0);

}

static int hcl_close(struct inode * inode, struct file * filp)
{

	if (hcl_debug) {
        	printk("HCL: hcl_close called.\n");
	}

        return(0);

}

static int hcl_ioctl(struct inode * inode, struct file * file,
        unsigned int cmd, unsigned long arg)
{

	if (hcl_debug) {
		printk("HCL: hcl_ioctl called.\n");
	}

	switch (cmd) {
		default:
			if (hcl_debug) {
				printk("HCL: hcl_ioctl cmd = 0x%x\n", cmd);
			}
	}

	return(0);

}

struct file_operations hcl_fops = {
	NULL,		/* lseek - default */
	NULL,		/* read - general block-dev read */
	NULL,		/* write - general block-dev write */
	NULL,		/* readdir - bad */
	NULL,		/* poll */
	hcl_ioctl,      /* ioctl */
	NULL,		/* mmap */
	hcl_open,	/* open */
	NULL,		/* flush */
	hcl_close,	/* release */
	NULL,		/* fsync */
	NULL,		/* fasync */
	NULL,		/* check_media_change */
	NULL,		/* revalidate */
	NULL		/* lock */
};


/*
 * init_hcl() - Boot time initialization.  Ensure that it is called 
 *	after devfs has been initialized.
 *
 * For now this routine is being called out of devfs/base.c.  Actually 
 * Not a bad place to be ..
 *
 */
#ifdef MODULE
int init_module (void)
#else
int __init init_hcl(void)
#endif
{
	extern void string_table_init(struct string_table *);
	extern struct string_table label_string_table;
	int rv = 0;

	printk ("\n%s: v%s Colin Ngam (cngam@sgi.com)\n",
		HCL_NAME, HCL_VERSION);
#if defined(CONFIG_HCL_DEBUG) && !defined(MODULE)
	hcl_debug = hcl_debug_init;
	printk ("%s: hcl_debug: 0x%0x\n", HCL_NAME, hcl_debug);
#endif
	printk ("\n%s: boot_options: 0x%0x\n", HCL_NAME, boot_options);
	spin_lock_init(&hcl_spinlock);

	/*
	 * Create the hwgraph_root on devfs.
	 */
	rv = hwgraph_path_add(NULL, "hw", &hwgraph_root);
	if (rv)
		printk ("init_hcl: Failed to create hwgraph_root. Error = %d.\n", rv);

	/*
	 * Create the hcl driver to support inventory entry manipulations.
	 * By default, it is expected that devfs is mounted on /dev.
	 *
	 */
	hcl_handle = hwgraph_register(hwgraph_root, ".hcl",
			0, DEVFS_FL_AUTO_DEVNUM,
			0, 0,
			S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP, 0, 0,
			&hcl_fops, NULL);

	if (hcl_handle == NULL) {
		panic("HCL: Unable to create HCL Driver in init_hcl().\n");
		return(0);
	}

	/*
	 * Initialize the HCL string table.
	 */
	string_table_init(&label_string_table);

	return(0);

}


/*
 * hcl_setup() - Process boot time parameters if given.
 *	"hcl="
 *	This routine gets called only if "hcl=" is given in the 
 *	boot line and before init_hcl().
 *
 *	We currently do not have any boot options .. when we do, 
 *	functionalities can be added here.
 *
 */
static int __init hcl_setup(char *str)
{
    while ( (*str != '\0') && !isspace (*str) )
    {
	printk("HCL: Boot time parameter %s\n", str);
#ifdef CONFIG_HCL_DEBUG
        if (strncmp (str, "all", 3) == 0) {
            hcl_debug_init |= HCL_DEBUG_ALL;
            str += 3;
        } else 
        	return 0;
#endif
        if (*str != ',') return 0;
        ++str;
    }

    return 1;

}

__setup("hcl=", hcl_setup);


/*
 * Set device specific "fast information".
 *
 */
void
hwgraph_fastinfo_set(devfs_handle_t de, arbitrary_info_t fastinfo)
{

	if (hcl_debug) {
		printk("HCL: hwgraph_fastinfo_set handle 0x%p fastinfo %ld\n",
			de, fastinfo);
	}
		
	labelcl_info_replace_IDX(de, HWGRAPH_FASTINFO, fastinfo, NULL);

}


/*
 * Get device specific "fast information".
 *
 */
arbitrary_info_t
hwgraph_fastinfo_get(devfs_handle_t de)
{
	arbitrary_info_t fastinfo;
	int rv;

	if (!de) {
		printk(KERN_WARNING "HCL: hwgraph_fastinfo_get handle given is NULL.\n");
		return(-1);
	}

	rv = labelcl_info_get_IDX(de, HWGRAPH_FASTINFO, &fastinfo);
	if (rv == 0)
		return(fastinfo);

	return(0);
}


/*
 * hwgraph_connectpt_set - Sets the connect point handle in de to the 
 *	given connect_de handle.  By default, the connect point of the 
 *	devfs node is the parent.  This effectively changes this assumption.
 */
int
hwgraph_connectpt_set(devfs_handle_t de, devfs_handle_t connect_de)
{
	int rv;

	if (!de)
		return(-1);

	rv = labelcl_info_connectpt_set(de, connect_de);

	return(rv);
}


/*
 * hwgraph_connectpt_get: Returns the entry's connect point  in the devfs 
 *	tree.
 */
devfs_handle_t
hwgraph_connectpt_get(devfs_handle_t de)
{
	int rv;
	arbitrary_info_t info;
	devfs_handle_t connect;

	rv = labelcl_info_get_IDX(de, HWGRAPH_CONNECTPT, &info);
	if (rv != 0) {
		return(NULL);
	}

	connect = (devfs_handle_t)info;
	return(connect);

}


/*
 * hwgraph_mk_dir - Creates a directory entry with devfs.
 *	Note that a directory entry in devfs can have children 
 *	but it cannot be a char|block special file.
 */
devfs_handle_t
hwgraph_mk_dir(devfs_handle_t de, const char *name,
                unsigned int namelen, void *info)
{

	int rv;
	labelcl_info_t *labelcl_info = NULL;
	devfs_handle_t new_devfs_handle = NULL;
	devfs_handle_t parent = NULL;

	/*
	 * Create the device info structure for hwgraph compatiblity support.
	 */
	labelcl_info = labelcl_info_create();
	if (!labelcl_info)
		return(NULL);

	/*
	 * Create a devfs entry.
	 */
	new_devfs_handle = devfs_mk_dir(de, name, (void *)labelcl_info);
	if (!new_devfs_handle) {
		labelcl_info_destroy(labelcl_info);
		return(NULL);
	}

	/*
	 * Get the parent handle.
	 */
	parent = devfs_get_parent (new_devfs_handle);

	/*
	 * To provide the same semantics as the hwgraph, set the connect point.
	 */
	rv = hwgraph_connectpt_set(new_devfs_handle, parent);
	if (!rv) {
		/*
		 * We need to clean up!
		 */
	}

	/*
	 * If the caller provides a private data pointer, save it in the 
	 * labelcl info structure(fastinfo).  This can be retrieved via
	 * hwgraph_fastinfo_get()
	 */
	if (info)
		hwgraph_fastinfo_set(new_devfs_handle, (arbitrary_info_t)info);
		
	return(new_devfs_handle);

}

/*
 * hwgraph_vertex_create - Create a vertex by giving it a temp name.
 */

/*
 * hwgraph_path_add - Create a directory node with the given path starting 
 * from the given devfs_handle_t.
 */
extern char * dev_to_name(devfs_handle_t, char *, uint);
int
hwgraph_path_add(devfs_handle_t  fromv,
		 char *path,
		 devfs_handle_t *new_de)
{

	unsigned int	namelen = strlen(path);
	int		rv;

	/*
	 * We need to handle the case when fromv is NULL ..
	 * in this case we need to create the path from the 
	 * hwgraph root!
	 */
	if (fromv == NULL)
		fromv = hwgraph_root;

	/*
	 * check the entry doesn't already exist, if it does
	 * then we simply want new_de to point to it (otherwise
	 * we'll overwrite the existing labelcl_info struct)
	 */
	rv = hwgraph_edge_get(fromv, path, new_de);
	if (rv)	{	/* couldn't find entry so we create it */
		*new_de = hwgraph_mk_dir(fromv, path, namelen, NULL);
		if (new_de == NULL)
			return(-1);
		else
			return(0);
	}
	else 
 		return(0);

}

/*
 * hwgraph_register  - Creates a file entry with devfs.
 *	Note that a file entry cannot have children .. it is like a 
 *	char|block special vertex in hwgraph.
 */
devfs_handle_t
hwgraph_register(devfs_handle_t de, const char *name,
                unsigned int namelen, unsigned int flags, 
		unsigned int major, unsigned int minor,
                umode_t mode, uid_t uid, gid_t gid, 
		struct file_operations *fops,
                void *info)
{

	int rv;
        void *labelcl_info = NULL;
        devfs_handle_t new_devfs_handle = NULL;
	devfs_handle_t parent = NULL;

        /*
         * Create the labelcl info structure for hwgraph compatiblity support.
         */
        labelcl_info = labelcl_info_create();
        if (!labelcl_info)
                return(NULL);

        /*
         * Create a devfs entry.
         */
        new_devfs_handle = devfs_register(de, name, flags, major,
				minor, mode, fops, labelcl_info);
        if (!new_devfs_handle) {
                labelcl_info_destroy((labelcl_info_t *)labelcl_info);
                return(NULL);
        }

	/*
	 * Get the parent handle.
	 */
	if (de == NULL)
		parent = devfs_get_parent (new_devfs_handle);
	else
		parent = de;
		
	/*
	 * To provide the same semantics as the hwgraph, set the connect point.
	 */
	rv = hwgraph_connectpt_set(new_devfs_handle, parent);
	if (rv) {
		/*
		 * We need to clean up!
		 */
		printk("HCL: Unable to set the connect point to it's parent 0x%p\n",
			new_devfs_handle);
	}

        /*
         * If the caller provides a private data pointer, save it in the 
         * labelcl info structure(fastinfo).  This can be retrieved via
         * hwgraph_fastinfo_get()
         */
        if (info)
                hwgraph_fastinfo_set(new_devfs_handle, (arbitrary_info_t)info);

        return(new_devfs_handle);

}


/*
 * hwgraph_mk_symlink - Create a symbolic link.
 */
int
hwgraph_mk_symlink(devfs_handle_t de, const char *name, unsigned int namelen,
                unsigned int flags, const char *link, unsigned int linklen, 
		devfs_handle_t *handle, void *info)
{

	void *labelcl_info = NULL;
	int status = 0;
	devfs_handle_t new_devfs_handle = NULL;

	/*
	 * Create the labelcl info structure for hwgraph compatiblity support.
	 */
	labelcl_info = labelcl_info_create();
	if (!labelcl_info)
		return(-1);

	/*
	 * Create a symbolic link devfs entry.
	 */
	status = devfs_mk_symlink(de, name, flags, link,
				&new_devfs_handle, labelcl_info);
	if ( (!new_devfs_handle) || (!status) ){
		labelcl_info_destroy((labelcl_info_t *)labelcl_info);
		return(-1);
	}

	/*
	 * If the caller provides a private data pointer, save it in the 
	 * labelcl info structure(fastinfo).  This can be retrieved via
	 * hwgraph_fastinfo_get()
	 */
	if (info)
		hwgraph_fastinfo_set(new_devfs_handle, (arbitrary_info_t)info);

	*handle = new_devfs_handle;
	return(0);

}

/*
 * hwgraph_vertex_get_next - this routine returns the next sibbling for the 
 *	device entry given in de.  If there are no more sibbling, NULL 
 * 	is returned in next_sibbling.
 *
 *	Currently we do not have any protection against de being deleted 
 *	while it's handle is being held.
 */
int
hwgraph_vertex_get_next(devfs_handle_t *next_sibbling, devfs_handle_t *de)
{
	*next_sibbling = devfs_get_next_sibling (*de);

	if (*next_sibbling != NULL)
		*de = *next_sibbling;
	return (0);
}


/*
 * hwgraph_vertex_destroy - Destroy the devfs entry
 */
int
hwgraph_vertex_destroy(devfs_handle_t de)
{

	void *labelcl_info = NULL;

	labelcl_info = devfs_get_info(de);
	devfs_unregister(de);

	if (labelcl_info)
		labelcl_info_destroy((labelcl_info_t *)labelcl_info);

	return(0);
}

/*
** See if a vertex has an outgoing edge with a specified name.
** Vertices in the hwgraph *implicitly* contain these edges:
**	"." 	refers to "current vertex"
**	".." 	refers to "connect point vertex"
**	"char"	refers to current vertex (character device access)
**	"block"	refers to current vertex (block device access)
*/

/*
 * hwgraph_edge_add - This routines has changed from the original conext.
 * All it does now is to create a symbolic link from "from" to "to".
 */
/* ARGSUSED */
int
hwgraph_edge_add(devfs_handle_t from, devfs_handle_t to, char *name)
{

	char *path;
	int name_start;
	devfs_handle_t handle = NULL;
	int rv;

	path = kmalloc(1024, GFP_KERNEL);
	name_start = devfs_generate_path (to, path, 1024);

	/*
	 * Otherwise, just create a symlink to the vertex.
	 * In this case the vertex was previous created with a REAL pathname.
	 */
	rv = devfs_mk_symlink (from, (const char *)name, 
			       DEVFS_FL_DEFAULT, (const char *)&path[name_start],
			       &handle, NULL);

	name_start = devfs_generate_path (handle, path, 1024);
	return(rv);

	
}
/* ARGSUSED */
int
hwgraph_edge_get(devfs_handle_t from, char *name, devfs_handle_t *toptr)
{

	int namelen = 0;
	devfs_handle_t target_handle = NULL;

	if (name == NULL)
		return(-1);

	if (toptr == NULL)
		return(-1);

	/*
	 * If the name is "." just return the current devfs entry handle.
	 */
	if (!strcmp(name, HWGRAPH_EDGELBL_DOT)) {
		if (toptr) {
			*toptr = from;
		}
	} else if (!strcmp(name, HWGRAPH_EDGELBL_DOTDOT)) {
		/*
		 * Hmmm .. should we return the connect point or parent ..
		 * see in hwgraph, the concept of parent is the connectpt!
		 *
		 * Maybe we should see whether the connectpt is set .. if 
		 * not just return the parent!
		 */
		target_handle = hwgraph_connectpt_get(from);
		if (target_handle) {
			/*
			 * Just return the connect point.
			 */
			*toptr = target_handle;
			return(0);
		}
		target_handle = devfs_get_parent(from);
		*toptr = target_handle;

	} else {
		/*
		 * Call devfs to get the devfs entry.
		 */
		namelen = (int) strlen(name);
		target_handle = devfs_find_handle (from, name, 0, 0,
					0, 1); /* Yes traverse symbolic links */
		if (target_handle == NULL)
			return(-1);
		else
		*toptr = target_handle;
	}

	return(0);
}


/*
 * hwgraph_edge_get_next - Retrieves the next sibbling given the current
 *	entry number "placeptr".
 *
 * 	Allow the caller to retrieve walk through the sibblings of "source" 
 * 	devfs_handle_t.  The implicit edges "." and ".." is returned first 
 * 	followed by each of the real children.
 *
 *	We may end up returning garbage if another thread perform any deletion 
 *	in this directory before "placeptr".
 *
 */
/* ARGSUSED */
int
hwgraph_edge_get_next(devfs_handle_t source, char *name, devfs_handle_t *target,
                              uint *placeptr)

{

        uint which_place;
	unsigned int namelen = 0;
	const char *tempname = NULL;

        if (placeptr == NULL)
                return(-1);

        which_place = *placeptr;

again:
        if (which_place <= HWGRAPH_RESERVED_PLACES) {
                if (which_place == EDGE_PLACE_WANT_CURRENT) {
			/*
			 * Looking for "."
			 * Return the current devfs handle.
			 */
                        if (name != NULL)
                                strcpy(name, HWGRAPH_EDGELBL_DOT);

                        if (target != NULL) {
                                *target = source; 
				/* XXX should incr "source" ref count here if we
				 * ever implement ref counts */
                        }

                } else if (which_place == EDGE_PLACE_WANT_CONNECTPT) {
			/*
			 * Looking for the connect point or parent.
			 * If the connect point is set .. it returns the connect point.
			 * Otherwise, it returns the parent .. will we support 
			 * connect point?
			 */
                        devfs_handle_t connect_point = hwgraph_connectpt_get(source);

                        if (connect_point == NULL) {
				/*
				 * No connectpoint set .. either the User
				 * explicitly NULL it or this node was not 
				 * created via hcl.
				 */
                                which_place++;
                                goto again;
                        }

                        if (name != NULL)
                                strcpy(name, HWGRAPH_EDGELBL_DOTDOT);

                        if (target != NULL)
                                *target = connect_point;

                } else if (which_place == EDGE_PLACE_WANT_REAL_EDGES) {
			/* 
			 * return first "real" entry in directory, and increment
			 * placeptr.  Next time around we should have 
			 * which_place > HWGRAPH_RESERVED_EDGES so we'll fall through
			 * this nested if block.
			 */
			*target = devfs_get_first_child(source);
			if (*target && name) {
				tempname = devfs_get_name(*target, &namelen);
				if (tempname && namelen)
					strcpy(name, tempname);
			}
					
			*placeptr = which_place + 1;
			return (0);
                }

                *placeptr = which_place+1;
                return(0);
        }

	/*
	 * walk linked list, (which_place - HWGRAPH_RESERVED_PLACES) times
	 */
	{
		devfs_handle_t	curr;
		int		i = 0;

		for (curr=devfs_get_first_child(source), i= i+HWGRAPH_RESERVED_PLACES; 
			curr!=NULL && i<which_place; 
			curr=devfs_get_next_sibling(curr), i++)
			;
		*target = curr;
		*placeptr = which_place + 1;
		if (curr && name) {
			tempname = devfs_get_name(*target, &namelen);
			printk("hwgraph_edge_get_next: Component name = %s, length = %d\n", tempname, namelen);
			if (tempname && namelen)
				strcpy(name, tempname);
		}
	}
	if (target == NULL)
		return(-1);
	else
        	return(0);
}

/*
 * hwgraph_info_add_LBL - Adds a new label for the device.  Mark the info_desc
 *	of the label as INFO_DESC_PRIVATE and store the info in the label.
 */
/* ARGSUSED */
int
hwgraph_info_add_LBL(	devfs_handle_t de,
			char *name,
			arbitrary_info_t info)
{
	return(labelcl_info_add_LBL(de, name, INFO_DESC_PRIVATE, info));
}

/*
 * hwgraph_info_remove_LBL - Remove the label entry for the device.
 */
/* ARGSUSED */
int
hwgraph_info_remove_LBL(	devfs_handle_t de,
				char *name,
				arbitrary_info_t *old_info)
{
	return(labelcl_info_remove_LBL(de, name, NULL, old_info));
}

/*
 * hwgraph_info_replace_LBL - replaces an existing label with 
 *	a new label info value.
 */
/* ARGSUSED */
int
hwgraph_info_replace_LBL(	devfs_handle_t de,
				char *name,
				arbitrary_info_t info,
				arbitrary_info_t *old_info)
{
	return(labelcl_info_replace_LBL(de, name,
			INFO_DESC_PRIVATE, info,
			NULL, old_info));
}
/*
 * hwgraph_info_get_LBL - Get and return the info value in the label of the 
 * 	device.
 */
/* ARGSUSED */
int
hwgraph_info_get_LBL(	devfs_handle_t de,
			char *name,
			arbitrary_info_t *infop)
{
	return(labelcl_info_get_LBL(de, name, NULL, infop));
}

/*
 * hwgraph_info_get_exported_LBL - Retrieve the info_desc and info pointer 
 *	of the given label for the device.  The weird thing is that the label 
 *	that matches the name is return irrespective of the info_desc value!
 *	Do not understand why the word "exported" is used!
 */
/* ARGSUSED */
int
hwgraph_info_get_exported_LBL(	devfs_handle_t de,
				char *name,
				int *export_info,
				arbitrary_info_t *infop)
{
	int rc;
	arb_info_desc_t info_desc;

	rc = labelcl_info_get_LBL(de, name, &info_desc, infop);
	if (rc == 0)
		*export_info = (int)info_desc;

	return(rc);
}

/*
 * hwgraph_info_get_next_LBL - Returns the next label info given the 
 *	current label entry in place.
 *
 *	Once again this has no locking or reference count for protection.
 *
 */
/* ARGSUSED */
int
hwgraph_info_get_next_LBL(	devfs_handle_t de,
				char *buf,
				arbitrary_info_t *infop,
				labelcl_info_place_t *place)
{
	return(labelcl_info_get_next_LBL(de, buf, NULL, infop, place));
}

/*
 * hwgraph_info_export_LBL - Retrieve the specified label entry and modify 
 *	the info_desc field with the given value in nbytes.
 */
/* ARGSUSED */
int
hwgraph_info_export_LBL(devfs_handle_t de, char *name, int nbytes)
{
	arbitrary_info_t info;
	int rc;

	if (nbytes == 0)
		nbytes = INFO_DESC_EXPORT;

	if (nbytes < 0)
		return(-1);

	rc = labelcl_info_get_LBL(de, name, NULL, &info);
	if (rc != 0)
		return(rc);

	rc = labelcl_info_replace_LBL(de, name,
				nbytes, info, NULL, NULL);

	return(rc);
}

/*
 * hwgraph_info_unexport_LBL - Retrieve the given label entry and change the 
 * label info_descr filed to INFO_DESC_PRIVATE.
 */
/* ARGSUSED */
int
hwgraph_info_unexport_LBL(devfs_handle_t de, char *name)
{
	arbitrary_info_t info;
	int rc;

	rc = labelcl_info_get_LBL(de, name, NULL, &info);
	if (rc != 0)
		return(rc);

	rc = labelcl_info_replace_LBL(de, name,
				INFO_DESC_PRIVATE, info, NULL, NULL);

	return(rc);
}

/*
 * hwgraph_path_lookup - return the handle for the given path.
 *
 */
int
hwgraph_path_lookup(	devfs_handle_t start_vertex_handle,
			char *lookup_path,
			devfs_handle_t *vertex_handle_ptr,
			char **remainder)
{
	*vertex_handle_ptr = devfs_find_handle(start_vertex_handle,	/* start dir */
					lookup_path,		/* path */
					0,			/* major */
					0,			/* minor */
					0,			/* char | block */
					1);			/* traverse symlinks */
	if (*vertex_handle_ptr == NULL)
		return(-1);
	else
		return(0);
}

/*
 * hwgraph_traverse - Find and return the devfs handle starting from de.
 *
 */
graph_error_t
hwgraph_traverse(devfs_handle_t de, char *path, devfs_handle_t *found)
{
	/* 
	 * get the directory entry (path should end in a directory)
	 */

	*found = devfs_find_handle(de,	/* start dir */
			    path,	/* path */
			    0,		/* major */
			    0,		/* minor */
			    0,		/* char | block */
			    1);		/* traverse symlinks */
	if (*found == NULL)
		return(GRAPH_NOT_FOUND);
	else
		return(GRAPH_SUCCESS);
}

/*
 * hwgraph_path_to_vertex - Return the devfs entry handle for the given 
 *	pathname .. assume traverse symlinks too!.
 */
devfs_handle_t
hwgraph_path_to_vertex(char *path)
{
	return(devfs_find_handle(NULL,	/* start dir */
			path,		/* path */
		    	0,		/* major */
		    	0,		/* minor */
		    	0,		/* char | block */
		    	1));		/* traverse symlinks */
}

/*
 * hwgraph_path_to_dev - Returns the devfs_handle_t of the given path ..
 *	We only deal with devfs handle and not devfs_handle_t.
*/
devfs_handle_t
hwgraph_path_to_dev(char *path)
{
	devfs_handle_t  de;

	de = hwgraph_path_to_vertex(path);
	return(de);
}

/*
 * hwgraph_block_device_get - return the handle of the block device file.
 *	The assumption here is that de is a directory.
*/
devfs_handle_t
hwgraph_block_device_get(devfs_handle_t de)
{
	return(devfs_find_handle(de,		/* start dir */
			"block",		/* path */
		    	0,			/* major */
		    	0,			/* minor */
		    	DEVFS_SPECIAL_BLK,	/* char | block */
		    	1));			/* traverse symlinks */
}

/*
 * hwgraph_char_device_get - return the handle of the char device file.
 *      The assumption here is that de is a directory.
*/
devfs_handle_t
hwgraph_char_device_get(devfs_handle_t de)
{
	return(devfs_find_handle(de,		/* start dir */
			"char",			/* path */
		    	0,			/* major */
		    	0,			/* minor */
		    	DEVFS_SPECIAL_CHR,	/* char | block */
		    	1));			/* traverse symlinks */
}

/*
 * hwgraph_cdevsw_get - returns the fops of the given devfs entry.
 */
struct file_operations *
hwgraph_cdevsw_get(devfs_handle_t de)
{
	return(devfs_get_ops(de));
}

/*
 * hwgraph_bdevsw_get - returns the fops of the given devfs entry.
*/
struct file_operations *
hwgraph_bdevsw_get(devfs_handle_t de)
{
	return(devfs_get_ops(de));
}

/*
** Inventory is now associated with a vertex in the graph.  For items that
** belong in the inventory but have no vertex 
** (e.g. old non-graph-aware drivers), we create a bogus vertex under the 
** INFO_LBL_INVENT name.
**
** For historical reasons, we prevent exact duplicate entries from being added
** to a single vertex.
*/

/*
 * hwgraph_inventory_add - Adds an inventory entry into de.
 */
int
hwgraph_inventory_add(	devfs_handle_t de,
			int class,
			int type,
			major_t controller,
			minor_t unit,
			int state)
{
	inventory_t *pinv = NULL, *old_pinv = NULL, *last_pinv = NULL;
	int rv;

	/*
	 * Add our inventory data to the list of inventory data
	 * associated with this vertex.
	 */
again:
	/* GRAPH_LOCK_UPDATE(&invent_lock); */
	rv = labelcl_info_get_LBL(de,
			INFO_LBL_INVENT,
			NULL, (arbitrary_info_t *)&old_pinv);
	if ((rv != LABELCL_SUCCESS) && (rv != LABELCL_NOT_FOUND))
		goto failure;

	/*
	 * Seek to end of inventory items associated with this
	 * vertex.  Along the way, make sure we're not duplicating
	 * an inventory item (for compatibility with old add_to_inventory)
	 */
	for (;old_pinv; last_pinv = old_pinv, old_pinv = old_pinv->inv_next) {
		if ((int)class != -1 && old_pinv->inv_class != class)
			continue;
		if ((int)type != -1 && old_pinv->inv_type != type)
			continue;
		if ((int)state != -1 && old_pinv->inv_state != state)
			continue;
		if ((int)controller != -1
		    && old_pinv->inv_controller != controller)
			continue;
		if ((int)unit != -1 && old_pinv->inv_unit != unit)
			continue;

		/* exact duplicate of previously-added inventory item */
		rv = LABELCL_DUP;
		goto failure;
	}

	/* Not a duplicate, so we know that we need to add something. */
	if (pinv == NULL) {
		/* Release lock while we wait for memory. */
		/* GRAPH_LOCK_DONE_UPDATE(&invent_lock); */
		pinv = (inventory_t *)kmalloc(sizeof(inventory_t), GFP_KERNEL);
		replace_in_inventory(pinv, class, type, controller, unit, state);
		goto again;
	}

	pinv->inv_next = NULL;
	if (last_pinv) {
		last_pinv->inv_next = pinv;
	} else {
		rv = labelcl_info_add_LBL(de, INFO_LBL_INVENT, 
			sizeof(inventory_t), (arbitrary_info_t)pinv);

		if (!rv)
			goto failure;
	}

	/* GRAPH_LOCK_DONE_UPDATE(&invent_lock); */
	return(0);

failure:
	/* GRAPH_LOCK_DONE_UPDATE(&invent_lock); */
	if (pinv)
		kfree(pinv);
	return(rv);
}


/*
 * hwgraph_inventory_remove - Removes an inventory entry.
 *
 *	Remove an inventory item associated with a vertex.   It is the caller's
 *	responsibility to make sure that there are no races between removing
 *	inventory from a vertex and simultaneously removing that vertex.
*/
int
hwgraph_inventory_remove(	devfs_handle_t de,
				int class,
				int type,
				major_t controller,
				minor_t unit,
				int state)
{
	inventory_t *pinv = NULL, *last_pinv = NULL, *next_pinv = NULL;
	labelcl_error_t rv;

	/*
	 * We never remove stuff from ".invent" ..
	 */
	if (!de)
		return (-1);

	/*
	 * Remove our inventory data to the list of inventory data
	 * associated with this vertex.
	 */
	/* GRAPH_LOCK_UPDATE(&invent_lock); */
	rv = labelcl_info_get_LBL(de,
			INFO_LBL_INVENT,
			NULL, (arbitrary_info_t *)&pinv);
	if (rv != LABELCL_SUCCESS)
		goto failure;

	/*
	 * Search through inventory items associated with this
	 * vertex, looking for a match.
	 */
	for (;pinv; pinv = next_pinv) {
		next_pinv = pinv->inv_next;

		if(((int)class == -1 || pinv->inv_class == class) &&
		   ((int)type == -1 || pinv->inv_type == type) &&
		   ((int)state == -1 || pinv->inv_state == state) &&
		   ((int)controller == -1 || pinv->inv_controller == controller) &&
		   ((int)unit == -1 || pinv->inv_unit == unit)) {

			/* Found a matching inventory item. Remove it. */
			if (last_pinv) {
				last_pinv->inv_next = pinv->inv_next;
			} else {
				rv = hwgraph_info_replace_LBL(de, INFO_LBL_INVENT, (arbitrary_info_t)pinv->inv_next, NULL);
				if (rv != LABELCL_SUCCESS)
					goto failure;
			}

			pinv->inv_next = NULL; /* sanity */
			kfree(pinv);
		} else
			last_pinv = pinv;
	}

	if (last_pinv == NULL) {
		rv = hwgraph_info_remove_LBL(de, INFO_LBL_INVENT, NULL);
		if (rv != LABELCL_SUCCESS)
			goto failure;
	}

	rv = LABELCL_SUCCESS;

failure:
	/* GRAPH_LOCK_DONE_UPDATE(&invent_lock); */
	return(rv);
}

/*
 * hwgraph_inventory_get_next - Get next inventory item associated with the 
 *	specified vertex.
 *
 *	No locking is really needed.  We don't yet have the ability
 *	to remove inventory items, and new items are always added to
 *	the end of a vertex' inventory list.
 *
 * 	However, a devfs entry can be removed!
*/
int
hwgraph_inventory_get_next(devfs_handle_t de, invplace_t *place, inventory_t **ppinv)
{
	inventory_t *pinv;
	labelcl_error_t rv;

	if (de == NULL)
		return(LABELCL_BAD_PARAM);

	if (place->invplace_vhdl == NULL) {
		place->invplace_vhdl = de;
		place->invplace_inv = NULL;
	}

	if (de != place->invplace_vhdl)
		return(LABELCL_BAD_PARAM);

	if (place->invplace_inv == NULL) {
		/* Just starting on this vertex */
		rv = labelcl_info_get_LBL(de, INFO_LBL_INVENT,
						NULL, (arbitrary_info_t *)&pinv);
		if (rv != LABELCL_SUCCESS)
			return(LABELCL_NOT_FOUND);

	} else {
		/* Advance to next item on this vertex */
		pinv = place->invplace_inv->inv_next;
	}
	place->invplace_inv = pinv;
	*ppinv = pinv;

	return(LABELCL_SUCCESS);
}

/*
 * hwgraph_controller_num_get - Returns the controller number in the inventory 
 *	entry.
 */
int
hwgraph_controller_num_get(devfs_handle_t device)
{
	inventory_t *pinv;
	invplace_t invplace = { NULL, NULL, NULL };
	int val = -1;
	if ((pinv = device_inventory_get_next(device, &invplace)) != NULL) {
		val = (pinv->inv_class == INV_NETWORK)? pinv->inv_unit: pinv->inv_controller;
	}
#ifdef DEBUG
	/*
	 * It does not make any sense to call this on vertexes with multiple
	 * inventory structs chained together
	 */
	if ( device_inventory_get_next(device, &invplace) != NULL ) {
		printk("Should panic here ... !\n");
#endif
	return (val);	
}

/*
 * hwgraph_controller_num_set - Sets the controller number in the inventory 
 *	entry.
 */
void
hwgraph_controller_num_set(devfs_handle_t device, int contr_num)
{
	inventory_t *pinv;
	invplace_t invplace = { NULL, NULL, NULL };
	if ((pinv = device_inventory_get_next(device, &invplace)) != NULL) {
		if (pinv->inv_class == INV_NETWORK)
			pinv->inv_unit = contr_num;
		else {
			if (pinv->inv_class == INV_FCNODE)
				pinv = device_inventory_get_next(device, &invplace);
			if (pinv != NULL)
				pinv->inv_controller = contr_num;
		}
	}
#ifdef DEBUG
	/*
	 * It does not make any sense to call this on vertexes with multiple
	 * inventory structs chained together
	 */
	if(pinv != NULL)
		ASSERT(device_inventory_get_next(device, &invplace) == NULL);
#endif
}

/*
 * Find the canonical name for a given vertex by walking back through
 * connectpt's until we hit the hwgraph root vertex (or until we run
 * out of buffer space or until something goes wrong).
 *
 *	COMPATIBILITY FUNCTIONALITY
 * Walks back through 'parents', not necessarily the same as connectpts.
 *
 * Need to resolve the fact that devfs does not return the path from 
 * "/" but rather it just stops right before /dev ..
 */
int
hwgraph_vertex_name_get(devfs_handle_t vhdl, char *buf, uint buflen)
{
	char *locbuf;
	int   pos;

	if (buflen < 1)
		return(-1);	/* XXX should be GRAPH_BAD_PARAM ? */

	locbuf = kmalloc(buflen, GFP_KERNEL);

	pos = devfs_generate_path(vhdl, locbuf, buflen);
	if (pos < 0) {
		kfree(locbuf);
		return pos;
	}

	strcpy(buf, &locbuf[pos]);
	kfree(locbuf);
	return 0;
}

/*
** vertex_to_name converts a vertex into a canonical name by walking
** back through connect points until we hit the hwgraph root (or until
** we run out of buffer space).
**
** Usually returns a pointer to the original buffer, filled in as
** appropriate.  If the buffer is too small to hold the entire name,
** or if anything goes wrong while determining the name, vertex_to_name
** returns "UnknownDevice".
*/

#define DEVNAME_UNKNOWN "UnknownDevice"

char *
vertex_to_name(devfs_handle_t vhdl, char *buf, uint buflen)
{
	if (hwgraph_vertex_name_get(vhdl, buf, buflen) == GRAPH_SUCCESS)
		return(buf);
	else
		return(DEVNAME_UNKNOWN);
}

#ifdef IRIX
/*
** Return the compact node id of the node that ultimately "owns" the specified
** vertex.  In order to do this, we walk back through masters and connect points
** until we reach a vertex that represents a node.
*/
cnodeid_t
master_node_get(devfs_handle_t vhdl)
{
	cnodeid_t cnodeid;
	devfs_handle_t master;

	for (;;) {
		cnodeid = nodevertex_to_cnodeid(vhdl);
		if (cnodeid != CNODEID_NONE)
			return(cnodeid);

		master = device_master_get(vhdl);

		/* Check for exceptional cases */
		if (master == vhdl) {
			/* Since we got a reference to the "master" thru
			 * device_master_get() we should decrement
			 * its reference count by 1
			 */
			hwgraph_vertex_unref(master);
			return(CNODEID_NONE);
		}

		if (master == GRAPH_VERTEX_NONE) {
			master = hwgraph_connectpt_get(vhdl);
			if ((master == GRAPH_VERTEX_NONE) ||
			    (master == vhdl)) {
				if (master == vhdl)
					/* Since we got a reference to the
					 * "master" thru
					 * hwgraph_connectpt_get() we should
					 * decrement its reference count by 1
					 */
					hwgraph_vertex_unref(master);
				return(CNODEID_NONE);
			}
		}
		
		vhdl = master;
		/* Decrement the reference to "master" which was got
		 * either thru device_master_get() or hwgraph_connectpt_get()
		 * above.
		 */
		hwgraph_vertex_unref(master);
	}
}

/*
 * Using the canonical path name to get hold of the desired vertex handle will
 * not work on multi-hub sn0 nodes. Hence, we use the following (slightly
 * convoluted) algorithm.
 *
 * - Start at the vertex corresponding to the driver (provided as input parameter)
 * - Loop till you reach a vertex which has EDGE_LBL_MEMORY
 *    - If EDGE_LBL_CONN exists, follow that up.
 *      else if EDGE_LBL_MASTER exists, follow that up.
 *      else follow EDGE_LBL_DOTDOT up.
 *
 * * We should be at desired hub/heart vertex now *
 * - Follow EDGE_LBL_CONN to the widget vertex.
 *
 * - return vertex handle of this widget.
 */
devfs_handle_t
mem_vhdl_get(devfs_handle_t drv_vhdl)
{
devfs_handle_t cur_vhdl, cur_upper_vhdl;
devfs_handle_t tmp_mem_vhdl, mem_vhdl;
graph_error_t loop_rv;

  /* Initializations */
  cur_vhdl = drv_vhdl;
  loop_rv = ~GRAPH_SUCCESS;

  /* Loop till current vertex has EDGE_LBL_MEMORY */
  while (loop_rv != GRAPH_SUCCESS) {

    if ((hwgraph_edge_get(cur_vhdl, EDGE_LBL_CONN, &cur_upper_vhdl)) == GRAPH_SUCCESS) {

    } else if ((hwgraph_edge_get(cur_vhdl, EDGE_LBL_MASTER, &cur_upper_vhdl)) == GRAPH_SUCCESS) {
      } else { /* Follow HWGRAPH_EDGELBL_DOTDOT up */
           (void) hwgraph_edge_get(cur_vhdl, HWGRAPH_EDGELBL_DOTDOT, &cur_upper_vhdl);
        }

    cur_vhdl = cur_upper_vhdl;

#if DEBUG && HWG_DEBUG
    printf("Current vhdl %d \n", cur_vhdl);
#endif /* DEBUG */

    loop_rv = hwgraph_edge_get(cur_vhdl, EDGE_LBL_MEMORY, &tmp_mem_vhdl);
  }

  /* We should be at desired hub/heart vertex now */
  if ((hwgraph_edge_get(cur_vhdl, EDGE_LBL_CONN, &mem_vhdl)) != GRAPH_SUCCESS)
    return (GRAPH_VERTEX_NONE);

  return (mem_vhdl);
}
#endif /* IRIX */


/*
** Add a char device -- if the driver supports it -- at a specified vertex.
*/
graph_error_t
hwgraph_char_device_add(        devfs_handle_t from,
                                char *path,
                                char *prefix,
                                devfs_handle_t *devhdl)
{
	devfs_handle_t xx = NULL;

	printk("FIXME: hwgraph_char_device_add() called. Use hwgraph_register.\n");
	*devhdl = xx;	// Must set devhdl
	return(GRAPH_SUCCESS);
}

graph_error_t
hwgraph_edge_remove(devfs_handle_t from, char *name, devfs_handle_t *toptr)
{
	printk("FIXME: hwgraph_edge_remove\n");
	return(GRAPH_ILLEGAL_REQUEST);
}

graph_error_t
hwgraph_vertex_unref(devfs_handle_t vhdl)
{
	printk("FIXME: hwgraph_vertex_unref\n");
	return(GRAPH_ILLEGAL_REQUEST);
}


EXPORT_SYMBOL(hwgraph_mk_dir);
EXPORT_SYMBOL(hwgraph_path_add);
EXPORT_SYMBOL(hwgraph_char_device_add);
EXPORT_SYMBOL(hwgraph_register);
EXPORT_SYMBOL(hwgraph_vertex_destroy);

EXPORT_SYMBOL(hwgraph_fastinfo_get);
EXPORT_SYMBOL(hwgraph_edge_get);

EXPORT_SYMBOL(hwgraph_fastinfo_set);
EXPORT_SYMBOL(hwgraph_connectpt_set);
EXPORT_SYMBOL(hwgraph_connectpt_get);
EXPORT_SYMBOL(hwgraph_edge_get_next);
EXPORT_SYMBOL(hwgraph_info_add_LBL);
EXPORT_SYMBOL(hwgraph_info_remove_LBL);
EXPORT_SYMBOL(hwgraph_info_replace_LBL);
EXPORT_SYMBOL(hwgraph_info_get_LBL);
EXPORT_SYMBOL(hwgraph_info_get_exported_LBL);
EXPORT_SYMBOL(hwgraph_info_get_next_LBL);
EXPORT_SYMBOL(hwgraph_info_export_LBL);
EXPORT_SYMBOL(hwgraph_info_unexport_LBL);
EXPORT_SYMBOL(hwgraph_path_lookup);
EXPORT_SYMBOL(hwgraph_traverse);
EXPORT_SYMBOL(hwgraph_path_to_vertex);
EXPORT_SYMBOL(hwgraph_path_to_dev);
EXPORT_SYMBOL(hwgraph_block_device_get);
EXPORT_SYMBOL(hwgraph_char_device_get);
EXPORT_SYMBOL(hwgraph_cdevsw_get);
EXPORT_SYMBOL(hwgraph_bdevsw_get);
EXPORT_SYMBOL(hwgraph_vertex_name_get);
