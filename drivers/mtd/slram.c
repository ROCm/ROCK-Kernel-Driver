/*======================================================================

  $Id: slram.c,v 1.10 2000/07/03 10:01:38 dwmw2 Exp $

======================================================================*/


#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <stdarg.h>

#include <linux/mtd/mtd.h>

struct mypriv {
	u_char *start;
	u_char *end;
};

int physmem_erase (struct mtd_info *mtd, struct erase_info *instr);
int physmem_point (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char **mtdbuf);
void physmem_unpoint (struct mtd_info *mtd, u_char *addr);
int physmem_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
int physmem_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf);
	

int physmem_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	struct mypriv *priv = mtd->priv;

	if (instr->addr + instr->len > mtd->size)
		return -EINVAL;
	
	memset(priv->start + instr->addr, 0xff, instr->len);

	/* This'll catch a few races. Free the thing before returning :) 
	 * I don't feel at all ashamed. This kind of thing is possible anyway
	 * with flash, but unlikely.
	 */

	instr->state = MTD_ERASE_DONE;

	if (instr->callback)
		(*(instr->callback))(instr);
	else
		kfree(instr);

	return 0;
}


int physmem_point (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char **mtdbuf)
{
	struct mypriv *priv = (struct mypriv *)mtd->priv;

	*mtdbuf = priv->start + from;
	*retlen = len;
	return 0;
}

void physmem_unpoint (struct mtd_info *mtd, u_char *addr)
{
}

int physmem_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct mypriv *priv = (struct mypriv *)mtd->priv;
	
	memcpy (buf, priv->start + from, len);

	*retlen=len;
	return 0;
}

int physmem_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	struct mypriv *priv = (struct mypriv *)mtd->priv;

	memcpy (priv->start + to, buf, len);

	*retlen=len;
	return 0;
}




/*====================================================================*/

/* Place your defaults here */

static u_long start = 100663296;
static u_long length = 33554432;
static u_long end = 0;

#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define init_slram init_module
#define cleanup_slram cleanup_module
#endif
#define __exit
#endif

#ifdef MODULE
MODULE_PARM(start,"l");
MODULE_PARM(length,"l");
MODULE_PARM(end,"l");
#endif

struct mtd_info *mymtd;

void __init mtd_slram_setup(char *str, int *ints)
{
	if (ints[0] > 0)
		start=ints[1];
	if (ints[0] > 1)
		length=ints[2];
}

int init_slram(void)
{
	if (!start)
	{
		printk(KERN_NOTICE "physmem: No start address for memory device.\n");
		return -EINVAL;
	}
	
	if (!length && !end)
	{
		printk(KERN_NOTICE "physmem: No length or endpointer given.\n");
		return -EINVAL;
	}
	
	if (!end)
		end = start + length;
	
	if (!length)
	  length = end - start;

	if (start + length != end)
	  {
	    printk(KERN_NOTICE "physmem: start(%lx) + length(%lx) != end(%lx) !\n",
		   start, length, end);
	    return -EINVAL;
	  }

	mymtd = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
	
	memset(mymtd, 0, sizeof(*mymtd));
	
	if (mymtd)
	{
		memset((char *)mymtd, 0, sizeof(struct mtd_info));
		mymtd->priv = (void *) kmalloc (sizeof(struct mypriv), GFP_KERNEL);
		if (!mymtd->priv)
		{
			kfree(mymtd);
			mymtd = NULL;
		}
		memset(mymtd->priv, 0, sizeof(struct mypriv));
	}

	if (!mymtd)
	{
		printk(KERN_NOTICE "physmem: Cannot allocate new MTD device.\n");
		return -ENOMEM;
	}
	
	
	((struct mypriv *)mymtd->priv)->start = ioremap(start, length);
	((struct mypriv *)mymtd->priv)->end = ((struct mypriv *)mymtd->priv)->start + length;


	mymtd->name = "Raw memory";

	mymtd->size = length;
	mymtd->flags = MTD_CLEAR_BITS | MTD_SET_BITS | MTD_WRITEB_WRITEABLE | MTD_VOLATILE;
        mymtd->erase = physmem_erase;
	mymtd->point = physmem_point;
	mymtd->unpoint = physmem_unpoint;
	mymtd->read = physmem_read;
	mymtd->write = physmem_write;
	mymtd->module = THIS_MODULE;
	mymtd->type = MTD_RAM;
	mymtd->erasesize = 0x10000;

	if (add_mtd_device(mymtd))
	{
		printk("Failed to register new device\n");
		kfree(mymtd->priv);
		kfree(mymtd);
		return -EAGAIN;
	}
	printk("Registered physmem device from %dKb to %dKb\n",
	       (int)(start / 1024), (int)(end / 1024));
	printk("Mapped from 0x%p to 0x%p\n",((struct mypriv *)mymtd->priv)->start,
((struct mypriv *)mymtd->priv)->end);

	return 0;
}

static void __exit cleanup_slram(void)
{
	iounmap(((struct mypriv *)mymtd->priv)->start);
	kfree (mymtd->priv);
	del_mtd_device(mymtd);
	kfree(mymtd);
}

#if LINUX_VERSION_CODE > 0x20300
module_init(init_slram);
module_exit(cleanup_slram);
#endif
