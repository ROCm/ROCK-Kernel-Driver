/*
 * Flash memory access on Alchemy Pb1xxx boards
 * 
 * (C) 2001 Pete Popov <ppopov@mvista.com>
 * 
 * $Id: pb1xxx-flash.c,v 1.9 2003/06/23 11:48:18 dwmw2 Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/au1000.h>

#ifdef 	DEBUG_RW
#define	DBG(x...)	printk(x)
#else
#define	DBG(x...)	
#endif

#ifdef CONFIG_MIPS_PB1000
#define WINDOW_ADDR 0x1F800000
#define WINDOW_SIZE 0x800000
#endif


static struct map_info pb1xxx_map = {
	.name =	"Pb1xxx flash",
};


#ifdef CONFIG_MIPS_PB1000

static unsigned long flash_size = 0x00800000;
static unsigned char flash_buswidth = 4;
static struct mtd_partition pb1xxx_partitions[] = {
        {
                .name = "yamon env",
                .size = 0x00020000,
                .offset = 0,
                .mask_flags = MTD_WRITEABLE
        },{
                .name = "User FS",
                .size = 0x003e0000,
                .offset = 0x20000,
        },{
                .name = "boot code",
                .size = 0x100000,
                .offset = 0x400000,
                .mask_flags = MTD_WRITEABLE
        },{
                .name = "raw/kernel",
                .size = 0x300000,
                .offset = 0x500000
        }
};

#elif defined(CONFIG_MIPS_PB1500) || defined(CONFIG_MIPS_PB1100)

static unsigned char flash_buswidth = 4;
#if defined(CONFIG_MTD_PB1500_BOOT) && defined(CONFIG_MTD_PB1500_USER)
/* both 32MiB banks will be used. Combine the first 32MiB bank and the
 * first 28MiB of the second bank together into a single jffs/jffs2
 * partition.
 */
static unsigned long flash_size = 0x04000000;
#define WINDOW_ADDR 0x1C000000
#define WINDOW_SIZE 0x4000000
static struct mtd_partition pb1xxx_partitions[] = {
        {
                .name = "User FS",
                .size =   0x3c00000,
                .offset = 0x0000000
        },{
                .name = "yamon",
                .size = 0x0100000,
                .offset = 0x3c00000,
                .mask_flags = MTD_WRITEABLE
        },{
                .name = "raw kernel",
                .size = 0x02c0000,
                .offset = 0x3d00000
        }
};
#elif defined(CONFIG_MTD_PB1500_BOOT) && !defined(CONFIG_MTD_PB1500_USER)
static unsigned long flash_size = 0x02000000;
#define WINDOW_ADDR 0x1E000000
#define WINDOW_SIZE 0x2000000
static struct mtd_partition pb1xxx_partitions[] = {
        {
                .name = "User FS",
                .size =   0x1c00000,
                .offset = 0x0000000
        },{
                .name = "yamon",
                .size = 0x0100000,
                .offset = 0x1c00000,
                .mask_flags = MTD_WRITEABLE
        },{
                .name = "raw kernel",
                .size = 0x02c0000,
                .offset = 0x1d00000
        }
};
#elif !defined(CONFIG_MTD_PB1500_BOOT) && defined(CONFIG_MTD_PB1500_USER)
static unsigned long flash_size = 0x02000000;
#define WINDOW_ADDR 0x1C000000
#define WINDOW_SIZE 0x2000000
static struct mtd_partition pb1xxx_partitions[] = {
        {
                .name = "User FS",
                .size =   0x1e00000,
                .offset = 0x0000000
        },{
                .name = "raw kernel",
                .size = 0x0200000,
                .offset = 0x1e00000,
        }
};
#else
#error MTD_PB1500 define combo error /* should never happen */
#endif
#else
#error Unsupported board
#endif

static struct mtd_partition *parsed_parts;
static struct mtd_info *mymtd;

int __init pb1xxx_mtd_init(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	char *part_type;
	
	/* Default flash buswidth */
	pb1xxx_map.buswidth = flash_buswidth;

	/*
	 * Static partition definition selection
	 */
	part_type = "static";
	parts = pb1xxx_partitions;
	nb_parts = ARRAY_SIZE(pb1xxx_partitions);
	pb1xxx_map.size = flash_size;

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "Pb1xxx flash: probing %d-bit flash bus\n", 
			pb1xxx_map.buswidth*8);
	pb1xxx_map.phys = WINDOW_ADDR;
	pb1xxx_map.virt = (unsigned long)ioremap(WINDOW_ADDR, WINDOW_SIZE);

	simple_map_init(&pb1xxx_map);

	mymtd = do_map_probe("cfi_probe", &pb1xxx_map);
	if (!mymtd) {
		iounmap(pb1xxx_map.virt);
		return -ENXIO;
	}
	mymtd->owner = THIS_MODULE;

	add_mtd_partitions(mymtd, parts, nb_parts);
	return 0;
}

static void __exit pb1xxx_mtd_cleanup(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
		if (parsed_parts)
			kfree(parsed_parts);
	}
	if (pb1xxx_map.virt)
		iounmap(pb1xxx_map.virt);
}

module_init(pb1xxx_mtd_init);
module_exit(pb1xxx_mtd_cleanup);

MODULE_AUTHOR("Pete Popov");
MODULE_DESCRIPTION("Pb1xxx CFI map driver");
MODULE_LICENSE("GPL");
