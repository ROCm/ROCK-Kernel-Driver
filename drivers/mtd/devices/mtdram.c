/*
 * mtdram - a test mtd device
 * $Id: mtdram.c,v 1.25 2001/10/02 15:05:13 dwmw2 Exp $
 * Author: Alexander Larsson <alex@cendio.se>
 *
 * Copyright (c) 1999 Alexander Larsson <alex@cendio.se>
 *
 * This code is GPL
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/mtd/compatmac.h>
#include <linux/mtd/mtd.h>

#ifndef CONFIG_MTDRAM_ABS_POS
  #define CONFIG_MTDRAM_ABS_POS 0
#endif

#if CONFIG_MTDRAM_ABS_POS > 0
  #include <asm/io.h>
#endif

#ifdef MODULE
static unsigned long total_size = CONFIG_MTDRAM_TOTAL_SIZE;
static unsigned long erase_size = CONFIG_MTDRAM_ERASE_SIZE;
MODULE_PARM(total_size,"l");
MODULE_PARM(erase_size,"l");
#define MTDRAM_TOTAL_SIZE (total_size * 1024)
#define MTDRAM_ERASE_SIZE (erase_size * 1024)
#else
#define MTDRAM_TOTAL_SIZE (CONFIG_MTDRAM_TOTAL_SIZE * 1024)
#define MTDRAM_ERASE_SIZE (CONFIG_MTDRAM_ERASE_SIZE * 1024)
#endif


// We could store these in the mtd structure, but we only support 1 device..
static struct mtd_info *mtd_info;


static int
ram_erase(struct mtd_info *mtd, struct erase_info *instr)
{
  DEBUG(MTD_DEBUG_LEVEL2, "ram_erase(pos:%ld, len:%ld)\n", (long)instr->addr, (long)instr->len);
  if (instr->addr + instr->len > mtd->size) {
    DEBUG(MTD_DEBUG_LEVEL1, "ram_erase() out of bounds (%ld > %ld)\n", (long)(instr->addr + instr->len), (long)mtd->size);
    return -EINVAL;
  }
	
  memset((char *)mtd->priv + instr->addr, 0xff, instr->len);
	
  instr->state = MTD_ERASE_DONE;

  if (instr->callback)
    (*(instr->callback))(instr);
  return 0;
}

static int ram_point (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char **mtdbuf)
{
  if (from + len > mtd->size)
    return -EINVAL;
	
  *mtdbuf = mtd->priv + from;
  *retlen = len;
  return 0;
}

static void ram_unpoint (struct mtd_info *mtd, u_char *addr)
{
  DEBUG(MTD_DEBUG_LEVEL2, "ram_unpoint\n");
}

static int ram_read(struct mtd_info *mtd, loff_t from, size_t len,
	     size_t *retlen, u_char *buf)
{
  DEBUG(MTD_DEBUG_LEVEL2, "ram_read(pos:%ld, len:%ld)\n", (long)from, (long)len);
  if (from + len > mtd->size) {
    DEBUG(MTD_DEBUG_LEVEL1, "ram_read() out of bounds (%ld > %ld)\n", (long)(from + len), (long)mtd->size);
    return -EINVAL;
  }

  memcpy(buf, mtd->priv + from, len);

  *retlen=len;
  return 0;
}

static int ram_write(struct mtd_info *mtd, loff_t to, size_t len,
	      size_t *retlen, const u_char *buf)
{
  DEBUG(MTD_DEBUG_LEVEL2, "ram_write(pos:%ld, len:%ld)\n", (long)to, (long)len);
  if (to + len > mtd->size) {
    DEBUG(MTD_DEBUG_LEVEL1, "ram_write() out of bounds (%ld > %ld)\n", (long)(to + len), (long)mtd->size);
    return -EINVAL;
  }

  memcpy ((char *)mtd->priv + to, buf, len);

  *retlen=len;
  return 0;
}

static void __exit cleanup_mtdram(void)
{
  if (mtd_info) {
    del_mtd_device(mtd_info);
    if (mtd_info->priv)
#if CONFIG_MTDRAM_ABS_POS > 0
      iounmap(mtd_info->priv);
#else
      vfree(mtd_info->priv);
#endif	
    kfree(mtd_info);
  }
}

int __init init_mtdram(void)
{
   // Allocate some memory
   mtd_info = (struct mtd_info *)kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
   if (!mtd_info)
      return 0;
   
   memset(mtd_info, 0, sizeof(*mtd_info));

   // Setup the MTD structure
   mtd_info->name = "mtdram test device";
   mtd_info->type = MTD_RAM;
   mtd_info->flags = MTD_CAP_RAM;
   mtd_info->size = MTDRAM_TOTAL_SIZE;
   mtd_info->erasesize = MTDRAM_ERASE_SIZE;
#if CONFIG_MTDRAM_ABS_POS > 0
   mtd_info->priv = ioremap(CONFIG_MTDRAM_ABS_POS, MTDRAM_TOTAL_SIZE);
#else
   mtd_info->priv = vmalloc(MTDRAM_TOTAL_SIZE);
#endif

   if (!mtd_info->priv) {
     DEBUG(MTD_DEBUG_LEVEL1, "Failed to vmalloc(/ioremap) memory region of size %ld (ABS_POS:%ld)\n", (long)MTDRAM_TOTAL_SIZE, (long)CONFIG_MTDRAM_ABS_POS);
     kfree(mtd_info);
     mtd_info = NULL;
     return -ENOMEM;
   }
   memset(mtd_info->priv, 0xff, MTDRAM_TOTAL_SIZE);

   mtd_info->module = THIS_MODULE;			
   mtd_info->erase = ram_erase;
   mtd_info->point = ram_point;
   mtd_info->unpoint = ram_unpoint;
   mtd_info->read = ram_read;
   mtd_info->write = ram_write;

   if (add_mtd_device(mtd_info)) {
#if CONFIG_MTDRAM_ABS_POS > 0
     iounmap(mtd_info->priv);
#else
     vfree(mtd_info->priv);
#endif	
     kfree(mtd_info);
     mtd_info = NULL;
     return -EIO;
   }
   
   return 0;
}

module_init(init_mtdram);
module_exit(cleanup_mtdram);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Larsson <alexl@redhat.com>");
MODULE_DESCRIPTION("Simulated MTD driver for testing");

