/* ------------------------------------------------------------
 * vioconfig.c
 * (C) Copyright IBM Corporation 1994, 2004
 * Author: Dave Boutcher (sleddog@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 * ------------------------------------------------------------
 * proc file to display configuration information retrieved froma vio
 * host
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/vio.h>

static struct proc_dir_entry *vio_config;

extern int ibmvscsi_get_host_config(struct vio_dev *vdev,
				    unsigned char *buffer, 
				    int length);

static int proc_read(char *buf, char **start, off_t offset,
		     int blen, int *eof, void *data)
{
	struct device_node *node_vroot, *of_node;
	struct vio_dev * vdev;
	
	node_vroot = find_devices("vdevice");
	if ((node_vroot == NULL) || (node_vroot->child == NULL)) {
		/* this machine doesn't do virtual IO, and that's ok */
		return 0;
	}

	/*
	 * loop through all vdevices
	 */
	for (of_node = node_vroot->child;
			of_node != NULL;
			of_node = of_node->sibling) {
		/* see if this is a vscsi device */
		if ((of_node->type != NULL) &&
		    (strncmp(of_node->type, "vscsi", 5) == 0)) {
			/* see if we get a vdevice for that */
			vdev = vio_find_node(of_node);
			
			/* Finally see if we get host config data */
			if (ibmvscsi_get_host_config(vdev, buf, blen) == 0) {
				break;
			}
		}
	}

	*eof = 1;
	return strlen(buf);
}

int __init vioconfig_module_init(void)
{
	vio_config = create_proc_read_entry("vioconfig",
					    S_IFREG | S_IRUSR,
					    NULL,
					    proc_read,
					    NULL);
	if (!vio_config)
		return -1;
	return 0;
}

void __exit vioconfig_module_exit(void)
{
	remove_proc_entry("vioconfig", vio_config->parent);
}	

module_init(vioconfig_module_init);
module_exit(vioconfig_module_exit);
