/*
 * fbsysfs.c - framebuffer device class and attributes
 *
 * Copyright (c) 2004 James Simmons <jsimmons@infradead.org>
 * 
 *	This program is free software you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/fb.h>

#define to_fb_info(class) container_of(class, struct fb_info, class_dev)

static void release_fb_info(struct class_device *class_dev)
{
	struct fb_info *info = to_fb_info(class_dev);

	/* This doesn't harm */
	fb_dealloc_cmap(&info->cmap);

	kfree(info);
}

struct class fb_class = {
	.name 		= "graphics",
	.release 	= &release_fb_info,
};

static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	struct fb_info *info = to_fb_info(class_dev);

	return sprintf(buf, "%u:%u\n", FB_MAJOR, info->node);
}

static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);

int fb_add_class_device(struct fb_info *info)
{
	int retval;

	info->class_dev.class = &fb_class;
	snprintf(info->class_dev.class_id, BUS_ID_SIZE, "fb%d",
		 info->node);
	retval = class_device_register(&info->class_dev);
	if (retval)
		return retval;
	return class_device_create_file(&info->class_dev,
					&class_device_attr_dev);
}

/**
 * framebuffer_alloc - creates a new frame buffer info structure
 *
 * @size: size of driver private data, can be zero
 * @dev: pointer to the device for this fb, this can be NULL
 *
 * Creates a new frame buffer info structure. Also reserves @size bytes
 * for driver private data (info->par). info->par (if any) will be
 * aligned to sizeof(long).
 *
 * Returns the new structure, or NULL if an error occured.
 *
 */
struct fb_info *framebuffer_alloc(size_t size, struct device *dev)
{
#define BYTES_PER_LONG (BITS_PER_LONG/8)
#define PADDING (BYTES_PER_LONG - (sizeof(struct fb_info) % BYTES_PER_LONG))
	int fb_info_size = sizeof(struct fb_info);
	struct fb_info *info;
	char *p;

	if (size)
		fb_info_size += PADDING;

	p = kmalloc(fb_info_size + size, GFP_KERNEL);
	if (!p)
		return NULL;
	memset(p, 0, fb_info_size + size);
	info = (struct fb_info *) p;
	info->class_dev.dev = dev;

	if (size)
		info->par = p + fb_info_size;

	return info;
#undef PADDING
#undef BYTES_PER_LONG
}

/**
 * framebuffer_release - marks the structure available for freeing
 *
 * @info: frame buffer info structure
 *
 * Drop the reference count of the class_device embedded in the
 * framebuffer info structure.
 *
 */
void framebuffer_release(struct fb_info *info)
{
	class_device_put(&info->class_dev);
}

EXPORT_SYMBOL(framebuffer_release);
EXPORT_SYMBOL(framebuffer_alloc);
