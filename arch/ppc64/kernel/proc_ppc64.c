/*
 * arch/ppc64/kernel/proc_ppc64.c
 *
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen IBM Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


/*
 * Change Activity:
 * 2001       : mikec    : Created
 * 2001/06/05 : engebret : Software event count support.
 * 2003/02/13 : bergner  : Move PMC code to pmc.c
 * End Change Activity 
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>

#include <asm/proc_fs.h>
#include <asm/naca.h>
#include <asm/paca.h>
#include <asm/systemcfg.h>
#include <asm/rtas.h>
#include <asm/uaccess.h>

struct proc_ppc64_t proc_ppc64;

void proc_ppc64_create_paca(int num);

static loff_t  page_map_seek( struct file *file, loff_t off, int whence);
static ssize_t page_map_read( struct file *file, char *buf, size_t nbytes, loff_t *ppos);
static int     page_map_mmap( struct file *file, struct vm_area_struct *vma );

static struct file_operations page_map_fops = {
	.llseek	= page_map_seek,
	.read	= page_map_read,
	.mmap	= page_map_mmap
};


static int __init proc_ppc64_init(void)
{

	printk(KERN_INFO "proc_ppc64: Creating /proc/ppc64/\n");

	proc_ppc64.root = proc_mkdir("ppc64", 0);
	if (!proc_ppc64.root)
		return 0;

	proc_ppc64.naca = create_proc_entry("naca", S_IRUSR, proc_ppc64.root);
	if ( proc_ppc64.naca ) {
		proc_ppc64.naca->nlink = 1;
		proc_ppc64.naca->data = naca;
		proc_ppc64.naca->size = 4096;
		proc_ppc64.naca->proc_fops = &page_map_fops;
	}
	
	proc_ppc64.systemcfg = create_proc_entry("systemcfg", S_IFREG|S_IRUGO, proc_ppc64.root);
	if ( proc_ppc64.systemcfg ) {
		proc_ppc64.systemcfg->nlink = 1;
		proc_ppc64.systemcfg->data = systemcfg;
		proc_ppc64.systemcfg->size = 4096;
		proc_ppc64.systemcfg->proc_fops = &page_map_fops;
	}

	/* /proc/ppc64/paca/XX -- raw paca contents.  Only readable to root */
	proc_ppc64.paca = proc_mkdir("paca", proc_ppc64.root);
	if (proc_ppc64.paca) {
		unsigned long i;

		for (i = 0; i < NR_CPUS; i++) {
			if (!cpu_online(i))
				continue;
			proc_ppc64_create_paca(i);
		}
	}

	/* Placeholder for rtas interfaces. */
	proc_ppc64.rtas = proc_mkdir("rtas", proc_ppc64.root);

	return 0;
}


/*
 * NOTE: since paca data is always in flux the values will never be a consistant set.
 * In theory it could be made consistent if we made the corresponding cpu
 * copy the page for us (via an IPI).  Probably not worth it.
 *
 */
void proc_ppc64_create_paca(int num)
{
	struct proc_dir_entry *ent;
	struct paca_struct *lpaca = paca + num;
	char buf[16];

	sprintf(buf, "%02x", num);
	ent = create_proc_entry(buf, S_IRUSR, proc_ppc64.paca);
	if ( ent ) {
		ent->nlink = 1;
		ent->data = lpaca;
		ent->size = 4096;
		ent->proc_fops = &page_map_fops;
	}
}


static loff_t page_map_seek( struct file *file, loff_t off, int whence)
{
	loff_t new;
	struct proc_dir_entry *dp = PDE(file->f_dentry->d_inode);

	switch(whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	case 2:
		new = dp->size + off;
		break;
	default:
		return -EINVAL;
	}
	if ( new < 0 || new > dp->size )
		return -EINVAL;
	return (file->f_pos = new);
}

static ssize_t page_map_read( struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	unsigned pos = *ppos;
	struct proc_dir_entry *dp = PDE(file->f_dentry->d_inode);

	if ( pos >= dp->size )
		return 0;
	if ( nbytes >= dp->size )
		nbytes = dp->size;
	if ( pos + nbytes > dp->size )
		nbytes = dp->size - pos;

	copy_to_user( buf, (char *)dp->data + pos, nbytes );
	*ppos = pos + nbytes;
	return nbytes;
}

static int page_map_mmap( struct file *file, struct vm_area_struct *vma )
{
	struct proc_dir_entry *dp = PDE(file->f_dentry->d_inode);

	vma->vm_flags |= VM_SHM | VM_LOCKED;

	if ((vma->vm_end - vma->vm_start) > dp->size)
		return -EINVAL;

	remap_page_range( vma, vma->vm_start, __pa(dp->data), dp->size, vma->vm_page_prot );
	return 0;
}

fs_initcall(proc_ppc64_init);

