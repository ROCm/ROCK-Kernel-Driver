/*
 * pnp_proc.c: /proc/bus/pnp interface for Plug and Play devices
 *
 * Written by David Hinds, dahinds@users.sourceforge.net
 */

//#include <pcmcia/config.h>
#define __NO_VERSION__
//#include <pcmcia/k_compat.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/pnpbios.h>

static struct proc_dir_entry *proc_pnp = NULL;
static struct proc_dir_entry *proc_pnp_boot = NULL;
static struct pnp_dev_node_info node_info;

static int proc_read_devices(char *buf, char **start, off_t pos,
                             int count, int *eof, void *data)
{
	struct pnp_bios_node *node;
	int i;
	u8 nodenum;
	char *p = buf;

	if (pos != 0) {
	    *eof = 1;
	    return 0;
	}
	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node) return -ENOMEM;
	for (i=0,nodenum=0;i<0xff && nodenum!=0xff; i++) {
		if ( pnp_bios_get_dev_node(&nodenum, 1, node) )
			break;
		p += sprintf(p, "%02x\t%08x\t%02x:%02x:%02x\t%04x\n",
			     node->handle, node->eisa_id,
			     node->type_code[0], node->type_code[1],
			     node->type_code[2], node->flags);
	}
	kfree(node);
	return (p-buf);
}

static int proc_read_node(char *buf, char **start, off_t pos,
                          int count, int *eof, void *data)
{
	struct pnp_bios_node *node;
	int boot = (long)data >> 8;
	u8 nodenum = (long)data;
	int len;

	if (pos != 0) {
	    *eof = 1;
	    return 0;
	}
	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node) return -ENOMEM;
	if ( pnp_bios_get_dev_node(&nodenum, boot, node) )
		return -EIO;
	len = node->size - sizeof(struct pnp_bios_node);
	memcpy(buf, node->data, len);
	kfree(node);
	return len;
}

static int proc_write_node(struct file *file, const char *buf,
                           unsigned long count, void *data)
{
	struct pnp_bios_node *node;
	int boot = (long)data >> 8;
	u8 nodenum = (long)data;

	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node) return -ENOMEM;
	if ( pnp_bios_get_dev_node(&nodenum, boot, node) )
		return -EIO;
	if (count != node->size - sizeof(struct pnp_bios_node))
		return -EINVAL;
	memcpy(node->data, buf, count);
	if (pnp_bios_set_dev_node(node->handle, boot, node) != 0)
	    return -EINVAL;
	kfree(node);
	return count;
}

/*
 * When this is called, pnpbios functions are assumed to
 * work and the pnpbios_dont_use_current_config flag
 * should already have been set to the appropriate value
 */
void pnpbios_proc_init( void )
{
	struct pnp_bios_node *node;
	struct proc_dir_entry *ent;
	char name[3];
	int i;
	u8 nodenum;

	if (pnp_bios_dev_node_info(&node_info) != 0) return;
	
	proc_pnp = proc_mkdir("pnp", proc_bus);
	if (!proc_pnp) return;
	proc_pnp_boot = proc_mkdir("boot", proc_pnp);
	if (!proc_pnp_boot) return;
	create_proc_read_entry("devices", 0, proc_pnp, proc_read_devices, NULL);
	
	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node) return;
	for (i=0,nodenum = 0; i<0xff && nodenum != 0xff; i++) {
		if (pnp_bios_get_dev_node(&nodenum, 1, node) != 0)
			break;
		sprintf(name, "%02x", node->handle);
		if ( !pnpbios_dont_use_current_config ) {
			ent = create_proc_entry(name, 0, proc_pnp);
			if (ent) {
				ent->read_proc = proc_read_node;
				ent->write_proc = proc_write_node;
				ent->data = (void *)(long)(node->handle);
			}
		}
		ent = create_proc_entry(name, 0, proc_pnp_boot);
		if (ent) {
			ent->read_proc = proc_read_node;
			ent->write_proc = proc_write_node;
			ent->data = (void *)(long)(node->handle+0x100);
		}
	}
	kfree(node);
}

void pnpbios_proc_done(void)
{
	int i;
	char name[3];
	
	if (!proc_pnp) return;

	for (i=0; i<0xff; i++) {
		sprintf(name, "%02x", i);
		if ( !pnpbios_dont_use_current_config )
			remove_proc_entry(name, proc_pnp);
		remove_proc_entry(name, proc_pnp_boot);
	}
	remove_proc_entry("boot", proc_pnp);
	remove_proc_entry("devices", proc_pnp);
	remove_proc_entry("pnp", proc_bus);
}
