#ifndef _RAID0_H
#define _RAID0_H

#include <linux/raid/md.h>

struct strip_zone
{
	sector_t zone_offset;	/* Zone offset in md_dev */
	sector_t dev_offset;	/* Zone offset in real dev */
	sector_t size;		/* Zone size */
	int nb_dev;			/* # of devices attached to the zone */
	mdk_rdev_t *dev[MD_SB_DISKS]; /* Devices attached to the zone */
};

struct raid0_private_data
{
	struct strip_zone **hash_table; /* Table of indexes into strip_zone */
	struct strip_zone *strip_zone;
	int nr_strip_zones;

	sector_t hash_spacing;
	int preshift;			/* shift this before divide by hash_spacing */
};

typedef struct raid0_private_data raid0_conf_t;

#define mddev_to_conf(mddev) ((raid0_conf_t *) mddev->private)

#endif
