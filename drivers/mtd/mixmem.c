/* 
 * mixmem - a block device driver for flash rom found on the 
 *          piggyback board of the multi-purpose mixcom card
 *
 * Author: Gergely Madarasz <gorgo@itc.hu> 
 *
 * Copyright (c) 1999 ITConsult-Pro Co. <info@itc.hu>
 *
 * This code is GPL
 *
 */

#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/mtd/mapped.h>

#define MIXCOM_ID_OFFSET 0xc10
#define MIXCOM_PAGE_OFFSET 0xc11
#define MIXCOM_ID_1 0x11
#define MIXCOM_ID_2 0x13
#define MIXMEM_PAGESIZE 4096
#define FIRST_BLOCK_OFFSET 0x1000

#if LINUX_VERSION_CODE < 0x20300
#define __exit
#endif

static unsigned int mixmem_addrs[] = { 0xc8000, 0xd8000, 0 };
static unsigned int mixcom_ports[] = { 0x180, 0x280, 0x380, 0 };

// We could store these in the mtd structure, but we only support 1 device..
static unsigned long base_io = 0;
static unsigned long base_addr = 0;
static struct mapped_mtd_info *SSD;

static unsigned long mixmem_page(struct mapped_mtd_info *map,
				 unsigned long page)
{
	outb((char)(page & 0xff), base_io+MIXCOM_PAGE_OFFSET);
	outb((char)((page >> 8) & 0x7), base_io+MIXCOM_PAGE_OFFSET+1);
   	return base_addr;
}

static int flash_probe(int base)
{
	int prev,curr;
	unsigned long flags;

	writeb(0xf0, base);
	save_flags(flags); cli();
	
	prev=readw(base);

	writeb(0xaa, base+0x555);
	writeb(0x55, base+0x2AA);
	writeb(0x90, base+0x555);
	
	curr=readw(base);
	
	restore_flags(flags);
	writeb(0xf0, base);
	return(prev==curr?0:curr);
}

static int mixmem_probe(void) 
{
	int i;
	int id;
	int chip;
	
        /* This should really check to see if the io ports are in use before
           writing to them */
	for(i=0;mixcom_ports[i]!=0;i++) {
		id=inb(mixcom_ports[i]+MIXCOM_ID_OFFSET);
		if(id==MIXCOM_ID_1 || id==MIXCOM_ID_2) {
			printk("mixmem: mixcom board found at 0x%3x\n",mixcom_ports[i]);
			break;
		}
	}
	
	if(mixcom_ports[i]==0) {
		printk("mixmem: no mixcom board found\n");
		return -ENODEV;
	}
	
	if (check_region(mixcom_ports[i]+MIXCOM_PAGE_OFFSET, 2)) return -EAGAIN;

	
	
	// What is the deal with first_block_offset?
	for(i=0;mixmem_addrs[i]!=0;i++) {
		chip=flash_probe(mixmem_addrs[i]+FIRST_BLOCK_OFFSET);
		if(chip)break;
	}
	
	if(mixmem_addrs[i]==0) {
		printk("mixmem: no flash available\n");
		return -ENODEV;
	}
        base_io = mixcom_ports[i];
        base_addr = mixmem_addrs[i];
	request_region(mixcom_ports[i]+MIXCOM_PAGE_OFFSET, 2, "mixmem");
        return 0;
}


static void __exit cleanup_mixmem()
{
	mtd_mapped_remove(SSD);
	kfree(SSD);
	SSD = 0;
	release_region(base_io+MIXCOM_PAGE_OFFSET, 2);
}

//static int __init init_mixmem(void)
int __init init_mixmem(void)
{   
   if (mixmem_probe() != 0)
      return -EAGAIN;

   // Print out our little header..
   printk("mixcom MTD IO:0x%lx MEM:0x%lx-0x%lx\n",base_io,base_addr,
	  base_addr+MIXMEM_PAGESIZE);
   
   // Allocate some memory
   SSD = (struct mapped_mtd_info *)kmalloc(sizeof(*SSD),GFP_KERNEL);
   if (SSD == 0)
      return 0;   
   memset(SSD,0,sizeof(*SSD));

   // Setup the MTD structure
   SSD->page = mixmem_page;
   SSD->pagesize = MIXMEM_PAGESIZE;
   SSD->maxsize = 0x7FF;
   SSD->mtd.name = "mixcom piggyback";

   // Setup the MTD, this will sense the flash parameters and so on..
   if (mtd_mapped_setup(SSD) != 0)
   {
      printk("Failed to register new device\n");
      cleanup_module();
      return -EAGAIN;
   }

   return 0;
}
module_init(init_mixmem);
module_exit(cleanup_mixmem);
