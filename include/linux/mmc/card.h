/*
 *  linux/include/linux/mmc/card.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Card driver specific definitions.
 */
#ifndef LINUX_MMC_CARD_H
#define LINUX_MMC_CARD_H

#include <linux/mmc/mmc.h>

struct mmc_cid {
	unsigned int		manfid;
	unsigned int		serial;
	char			prod_name[8];
	unsigned char		hwrev;
	unsigned char		fwrev;
	unsigned char		month;
	unsigned char		year;
};

struct mmc_csd {
	unsigned char		mmc_prot;
	unsigned short		cmdclass;
	unsigned short		tacc_clks;
	unsigned int		tacc_ns;
	unsigned int		max_dtr;
	unsigned int		read_blkbits;
	unsigned int		capacity;
};

struct mmc_host;

/*
 * MMC device
 */
struct mmc_card {
	struct list_head	node;		/* node in hosts devices list */
	struct mmc_host		*host;		/* the host this device belongs to */
	struct device		dev;		/* the device */
	unsigned int		rca;		/* relative card address of device */
	unsigned int		state;		/* (our) card state */
#define MMC_STATE_PRESENT	(1<<0)
#define MMC_STATE_DEAD		(1<<1)
	struct mmc_cid		cid;		/* card identification */
	struct mmc_csd		csd;		/* card specific */
};

#define mmc_card_dead(c)	((c)->state & MMC_STATE_DEAD)
#define mmc_card_present(c)	((c)->state & MMC_STATE_PRESENT)

#define mmc_card_name(c)	((c)->cid.prod_name)
#define mmc_card_id(c)		((c)->dev.bus_id)

#define mmc_list_to_card(l)	container_of(l, struct mmc_card, node)
#define mmc_get_drvdata(c)	dev_get_drvdata(&(c)->dev)
#define mmc_set_drvdata(c,d)	dev_set_drvdata(&(c)->dev, d)

/*
 * MMC device driver (e.g., Flash card, I/O card...)
 */
struct mmc_driver {
	struct device_driver drv;
	int (*probe)(struct mmc_card *);
	void (*remove)(struct mmc_card *);
	int (*suspend)(struct mmc_card *, u32);
	int (*resume)(struct mmc_card *);
};

extern int mmc_register_driver(struct mmc_driver *);
extern void mmc_unregister_driver(struct mmc_driver *);

static inline int mmc_card_claim_host(struct mmc_card *card)
{
	return __mmc_claim_host(card->host, card);
}

#define mmc_card_release_host(c)	mmc_release_host((c)->host)

#endif
