/*
 * Flash memory access on SA11x0 based devices
 * 
 * (C) 2000 Nicolas Pitre <nico@cam.org>
 * 
 * $Id: sa1100-flash.c,v 1.47 2004/11/01 13:44:36 rmk Exp $
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/concat.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include <asm/sizes.h>
#include <asm/mach/flash.h>

#include <asm/arch/h3600.h>

#ifndef CONFIG_ARCH_SA1100
#error This is for SA1100 architecture only
#endif

#if 0
/*
 * This is here for documentation purposes only - until these people
 * submit their machine types.  It will be gone January 2005.
 */
static struct mtd_partition consus_partitions[] = {
	{
		.name		= "Consus boot firmware",
		.offset		= 0,
		.size		= 0x00040000,
		.mask_flags	= MTD_WRITABLE, /* force read-only */
	}, {
		.name		= "Consus kernel",
		.offset		= 0x00040000,
		.size		= 0x00100000,
		.mask_flags	= 0,
	}, {
		.name		= "Consus disk",
		.offset		= 0x00140000,
		/* The rest (up to 16M) for jffs.  We could put 0 and
		   make it find the size automatically, but right now
		   i have 32 megs.  jffs will use all 32 megs if given
		   the chance, and this leads to horrible problems
		   when you try to re-flash the image because blob
		   won't erase the whole partition. */
		.size		= 0x01000000 - 0x00140000,
		.mask_flags	= 0,
	}, {
		/* this disk is a secondary disk, which can be used as
		   needed, for simplicity, make it the size of the other
		   consus partition, although realistically it could be
		   the remainder of the disk (depending on the file
		   system used) */
		 .name		= "Consus disk2",
		 .offset	= 0x01000000,
		 .size		= 0x01000000 - 0x00140000,
		 .mask_flags	= 0,
	}
};

/* Frodo has 2 x 16M 28F128J3A flash chips in bank 0: */
static struct mtd_partition frodo_partitions[] =
{
	{
		.name		= "bootloader",
		.size		= 0x00040000,
		.offset		= 0x00000000,
		.mask_flags	= MTD_WRITEABLE
	}, {
		.name		= "bootloader params",
		.size		= 0x00040000,
		.offset		= MTDPART_OFS_APPEND,
		.mask_flags	= MTD_WRITEABLE
	}, {
		.name		= "kernel",
		.size		= 0x00100000,
		.offset		= MTDPART_OFS_APPEND,
		.mask_flags	= MTD_WRITEABLE
	}, {
		.name		= "ramdisk",
		.size		= 0x00400000,
		.offset		= MTDPART_OFS_APPEND,
		.mask_flags	= MTD_WRITEABLE
	}, {
		.name		= "file system",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND
	}
};

static struct mtd_partition jornada56x_partitions[] = {
	{
		.name		= "bootldr",
		.size		= 0x00040000,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "rootfs",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND,
	}
};

static void jornada56x_set_vpp(int vpp)
{
	if (vpp)
		GPSR = GPIO_GPIO26;
	else
		GPCR = GPIO_GPIO26;
	GPDR |= GPIO_GPIO26;
}

/*
 * Machine        Phys          Size    set_vpp
 * Consus    : SA1100_CS0_PHYS SZ_32M
 * Frodo     : SA1100_CS0_PHYS SZ_32M
 * Jornada56x: SA1100_CS0_PHYS SZ_32M jornada56x_set_vpp
 */
#endif

struct sa_subdev_info {
	unsigned long base;
	unsigned long size;
	char name[16];
	struct map_info map;
	struct mtd_info *mtd;
	struct flash_platform_data *data;
};

#define NR_SUBMTD 4

struct sa_info {
	struct mtd_partition	*parts;
	struct mtd_info		*mtd;
	int			num_subdev;
	struct sa_subdev_info	subdev[NR_SUBMTD];
};

static void sa1100_set_vpp(struct map_info *map, int on)
{
	struct sa_subdev_info *subdev = container_of(map, struct sa_subdev_info, map);
	subdev->data->set_vpp(on);
}

static void sa1100_destroy_subdev(struct sa_subdev_info *subdev)
{
	if (subdev->mtd)
		map_destroy(subdev->mtd);
	if (subdev->map.virt)
		iounmap(subdev->map.virt);
	release_mem_region(subdev->base, subdev->size);
}

static int sa1100_probe_subdev(struct sa_subdev_info *subdev)
{
	unsigned long phys;
	unsigned int size;
	int ret;

	phys = subdev->base;
	size = subdev->size;

	/*
	 * Retrieve the bankwidth from the MSC registers.
	 * We currently only implement CS0 and CS1 here.
	 */
	switch (phys) {
	default:
		printk(KERN_WARNING "SA1100 flash: unknown base address "
		       "0x%08lx, assuming CS0\n", phys);

	case SA1100_CS0_PHYS:
		subdev->map.bankwidth = (MSC0 & MSC_RBW) ? 2 : 4;
		break;

	case SA1100_CS1_PHYS:
		subdev->map.bankwidth = ((MSC0 >> 16) & MSC_RBW) ? 2 : 4;
		break;
	}

	if (!request_mem_region(phys, size, subdev->name)) {
		ret = -EBUSY;
		goto out;
	}

	if (subdev->data->set_vpp)
		subdev->map.set_vpp = sa1100_set_vpp;

	subdev->map.phys = phys;
	subdev->map.size = size;
	subdev->map.virt = ioremap(phys, size);
	if (!subdev->map.virt) {
		ret = -ENOMEM;
		goto err;
	}

	simple_map_init(&subdev->map);

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	subdev->mtd = do_map_probe(subdev->data->map_name, &subdev->map);
	if (subdev->mtd == NULL) {
		ret = -ENXIO;
		goto err;
	}
	subdev->mtd->owner = THIS_MODULE;

	printk(KERN_INFO "SA1100 flash: CFI device at 0x%08lx, %dMiB, "
		"%d-bit\n", phys, subdev->mtd->size >> 20,
		subdev->map.bankwidth * 8);

	return 0;

 err:
	sa1100_destroy_subdev(subdev);
 out:
	return ret;
}

static void sa1100_destroy(struct sa_info *info)
{
	int i;

	if (info->mtd) {
		del_mtd_partitions(info->mtd);

#ifdef CONFIG_MTD_CONCAT
		if (info->mtd != info->subdev[0].mtd)
			mtd_concat_destroy(info->mtd);
#endif
	}

	if (info->parts)
		kfree(info->parts);

	for (i = info->num_subdev - 1; i >= 0; i--)
		sa1100_destroy_subdev(&info->subdev[i]);
}

static int __init
sa1100_setup_mtd(struct sa_info *info, int nr, struct flash_platform_data *flash)
{
	struct mtd_info *cdev[nr];
	int i, ret = 0;

	/*
	 * Claim and then map the memory regions.
	 */
	for (i = 0; i < nr; i++) {
		struct sa_subdev_info *subdev = &info->subdev[i];
		if (subdev->base == (unsigned long)-1)
			break;

		subdev->map.name = subdev->name;
		sprintf(subdev->name, "sa1100-%d", i);
		subdev->data = flash;

		ret = sa1100_probe_subdev(subdev);
		if (ret)
			break;

		cdev[i] = subdev->mtd;
	}

	info->num_subdev = i;

	/*
	 * ENXIO is special.  It means we didn't find a chip when we probed.
	 */
	if (ret != 0 && !(ret == -ENXIO && info->num_subdev > 0))
		goto err;

	/*
	 * If we found one device, don't bother with concat support.  If
	 * we found multiple devices, use concat if we have it available,
	 * otherwise fail.  Either way, it'll be called "sa1100".
	 */
	if (info->num_subdev == 1) {
		strcpy(info->subdev[0].name, "sa1100");
		info->mtd = info->subdev[0].mtd;
		ret = 0;
	} else if (info->num_subdev > 1) {
		/*
		 * We detected multiple devices.  Concatenate them together.
		 */
#ifdef CONFIG_MTD_CONCAT
		info->mtd = mtd_concat_create(cdev, info->num_subdev,
					      "sa1100");
		if (info->mtd == NULL)
			ret = -ENXIO;
#else
		printk(KERN_ERR "SA1100 flash: multiple devices "
		       "found but MTD concat support disabled.\n");
		ret = -ENXIO;
#endif
	}

	if (ret == 0)
		return 0;

 err:
	sa1100_destroy(info);
	return ret;
}

static int __init sa1100_locate_flash(struct sa_info *info)
{
	int i, nr = -ENODEV;

	if (machine_is_adsbitsy()) {
		info->subdev[0].base = SA1100_CS1_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_assabet()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		info->subdev[1].base = SA1100_CS1_PHYS; /* neponset */
		info->subdev[1].size = SZ_32M;
		nr = 2;
	}
	if (machine_is_badge4()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_64M;
		nr = 1;
	}
	if (machine_is_cerf()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_consus()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_flexanet()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_freebird()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_frodo()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_graphicsclient()) {
		info->subdev[0].base = SA1100_CS1_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_graphicsmaster()) {
		info->subdev[0].base = SA1100_CS1_PHYS;
		info->subdev[0].size = SZ_16M;
		nr = 1;
	}
	if (machine_is_h3xxx()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_huw_webpanel()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_16M;
		nr = 1;
	}
	if (machine_is_itsy()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_jornada56x()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_jornada720()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_nanoengine()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[1].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_pangolin()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_64M;
		nr = 1;
	}
	if (machine_is_pfs168()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_pleb()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_4M;
		info->subdev[1].base = SA1100_CS1_PHYS;
		info->subdev[1].size = SZ_4M;
		nr = 2;
	}
	if (machine_is_pt_system3()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_16M;
		nr = 1;
	}
	if (machine_is_shannon()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_4M;
		nr = 1;
	}
	if (machine_is_sherman()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_simpad()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_16M;
		info->subdev[1].base = SA1100_CS1_PHYS;
		info->subdev[1].size = SZ_16M;
		nr = 2;
	}
	if (machine_is_stork()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_32M;
		nr = 1;
	}
	if (machine_is_trizeps()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_16M;
		nr = 1;
	}
	if (machine_is_victor()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_2M;
		nr = 1;
	}
	if (machine_is_yopy()) {
		info->subdev[0].base = SA1100_CS0_PHYS;
		info->subdev[0].size = SZ_64M;
		info->subdev[1].base = SA1100_CS1_PHYS;
		info->subdev[1].size = SZ_64M;
		nr = 2;
	}

	return nr;
}

static const char *part_probes[] = { "cmdlinepart", "RedBoot", NULL };

static struct sa_info sa_info;

static int __init sa1100_mtd_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct flash_platform_data *flash = pdev->dev.platform_data;
	struct mtd_partition *parts;
	const char *part_type = NULL;
	struct sa_info *info = &sa_info;
	int err, nr_parts = 0;
	int nr;

	if (!flash)
		return -ENODEV;

	nr = sa1100_locate_flash(info);
	if (nr < 0)
		return nr;

	err = sa1100_setup_mtd(info, nr, flash);
	if (err != 0)
		goto out;

	/*
	 * Partition selection stuff.
	 */
#ifdef CONFIG_MTD_PARTITIONS
	nr_parts = parse_mtd_partitions(info->mtd, part_probes, &parts, 0);
	if (nr_parts > 0) {
		info->parts = parts;
		part_type = "dynamic";
	} else
#endif
	{
		parts = flash->parts;
		nr_parts = flash->nr_parts;
		part_type = "static";
	}

	if (nr_parts == 0) {
		printk(KERN_NOTICE "SA1100 flash: no partition info "
			"available, registering whole flash\n");
		add_mtd_device(info->mtd);
	} else {
		printk(KERN_NOTICE "SA1100 flash: using %s partition "
			"definition\n", part_type);
		add_mtd_partitions(info->mtd, parts, nr_parts);
	}

	dev_set_drvdata(dev, info);
	err = 0;

 out:
	return err;
}

static int __exit sa1100_mtd_remove(struct device *dev)
{
	struct sa_info *info = dev_get_drvdata(dev);
	dev_set_drvdata(dev, NULL);
	sa1100_destroy(info);
	return 0;
}

#ifdef CONFIG_PM
static int sa1100_mtd_suspend(struct device *dev, u32 state, u32 level)
{
	struct sa_info *info = dev_get_drvdata(dev);
	int ret = 0;

	if (info && level == SUSPEND_SAVE_STATE)
		ret = info->mtd->suspend(info->mtd);

	return ret;
}

static int sa1100_mtd_resume(struct device *dev, u32 level)
{
	struct sa_info *info = dev_get_drvdata(dev);
	if (info && level == RESUME_RESTORE_STATE)
		info->mtd->resume(info->mtd);
	return 0;
}
#else
#define sa1100_mtd_suspend NULL
#define sa1100_mtd_resume  NULL
#endif

static struct device_driver sa1100_mtd_driver = {
	.name		= "flash",
	.bus		= &platform_bus_type,
	.probe		= sa1100_mtd_probe,
	.remove		= __exit_p(sa1100_mtd_remove),
	.suspend	= sa1100_mtd_suspend,
	.resume		= sa1100_mtd_resume,
};

static int __init sa1100_mtd_init(void)
{
	return driver_register(&sa1100_mtd_driver);
}

static void __exit sa1100_mtd_exit(void)
{
	driver_unregister(&sa1100_mtd_driver);
}

module_init(sa1100_mtd_init);
module_exit(sa1100_mtd_exit);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("SA1100 CFI map driver");
MODULE_LICENSE("GPL");
