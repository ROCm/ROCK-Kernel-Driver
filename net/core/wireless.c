/*
 * This file implement the Wireless Extensions APIs.
 *
 * Authors :	Jean Tourrilhes - HPL - <jt@hpl.hp.com>
 * Copyright (c) 1997-2001 Jean Tourrilhes, All Rights Reserved.
 *
 * (As all part of the Linux kernel, this file is GPL)
 */

/************************** DOCUMENTATION **************************/
/*
 * API definition :
 * --------------
 * See <linux/wireless.h> for details of the APIs and the rest.
 *
 * History :
 * -------
 *
 * v1 - 5.12.01 - Jean II
 *	o Created this file.
 *
 * v2 - 13.12.01 - Jean II
 *	o Move /proc/net/wireless stuff from net/core/dev.c to here
 *	o Make Wireless Extension IOCTLs go through here
 *	o Added iw_handler handling ;-)
 *	o Added standard ioctl description
 *	o Initial dumb commit strategy based on orinoco.c
 */

/***************************** INCLUDES *****************************/

#include <asm/uaccess.h>		/* copy_to_user() */
#include <linux/config.h>		/* Not needed ??? */
#include <linux/types.h>		/* off_t */
#include <linux/netdevice.h>		/* struct ifreq, dev_get_by_name() */

#include <linux/wireless.h>		/* Pretty obvious */
#include <net/iw_handler.h>		/* New driver API */

/**************************** CONSTANTS ****************************/

/* This will be turned on later on... */
#undef WE_STRICT_WRITE		/* Check write buffer size */

/* Debuging stuff */
#undef WE_IOCTL_DEBUG		/* Debug IOCTL API */

/************************* GLOBAL VARIABLES *************************/
/*
 * You should not use global variables, because or re-entrancy.
 * On our case, it's only const, so it's OK...
 */
static const struct iw_ioctl_description	standard_ioctl[] = {
	/* SIOCSIWCOMMIT (internal) */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCGIWNAME */
	{ IW_HEADER_TYPE_CHAR, 0, 0, 0, 0, IW_DESCR_FLAG_DUMP},
	/* SIOCSIWNWID */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, IW_DESCR_FLAG_EVENT},
	/* SIOCGIWNWID */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, IW_DESCR_FLAG_DUMP},
	/* SIOCSIWFREQ */
	{ IW_HEADER_TYPE_FREQ, 0, 0, 0, 0, IW_DESCR_FLAG_EVENT},
	/* SIOCGIWFREQ */
	{ IW_HEADER_TYPE_FREQ, 0, 0, 0, 0, IW_DESCR_FLAG_DUMP},
	/* SIOCSIWMODE */
	{ IW_HEADER_TYPE_UINT, 0, 0, 0, 0, IW_DESCR_FLAG_EVENT},
	/* SIOCGIWMODE */
	{ IW_HEADER_TYPE_UINT, 0, 0, 0, 0, IW_DESCR_FLAG_DUMP},
	/* SIOCSIWSENS */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCGIWSENS */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCSIWRANGE */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCGIWRANGE */
	{ IW_HEADER_TYPE_POINT, 0, 1, 0, sizeof(struct iw_range), IW_DESCR_FLAG_DUMP},
	/* SIOCSIWPRIV */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCGIWPRIV (handled directly by us) */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCSIWSTATS */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCGIWSTATS (handled directly by us) */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, IW_DESCR_FLAG_DUMP},
	/* SIOCSIWSPY */
	{ IW_HEADER_TYPE_POINT, 0, sizeof(struct sockaddr), 0, IW_MAX_SPY, 0},
	/* SIOCGIWSPY */
	{ IW_HEADER_TYPE_POINT, 0, (sizeof(struct sockaddr) + sizeof(struct iw_quality)), 0, IW_MAX_SPY, 0},
	/* -- hole -- */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* -- hole -- */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCSIWAP */
	{ IW_HEADER_TYPE_ADDR, 0, 0, 0, 0, 0},
	/* SIOCGIWAP */
	{ IW_HEADER_TYPE_ADDR, 0, 0, 0, 0, IW_DESCR_FLAG_DUMP},
	/* -- hole -- */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCGIWAPLIST */
	{ IW_HEADER_TYPE_POINT, 0, (sizeof(struct sockaddr) + sizeof(struct iw_quality)), 0, IW_MAX_AP, 0},
	/* -- hole -- */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* -- hole -- */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCSIWESSID */
	{ IW_HEADER_TYPE_POINT, 0, 1, 0, IW_ESSID_MAX_SIZE, IW_DESCR_FLAG_EVENT},
	/* SIOCGIWESSID */
	{ IW_HEADER_TYPE_POINT, 0, 1, 0, IW_ESSID_MAX_SIZE, IW_DESCR_FLAG_DUMP},
	/* SIOCSIWNICKN */
	{ IW_HEADER_TYPE_POINT, 0, 1, 0, IW_ESSID_MAX_SIZE, 0},
	/* SIOCGIWNICKN */
	{ IW_HEADER_TYPE_POINT, 0, 1, 0, IW_ESSID_MAX_SIZE, 0},
	/* -- hole -- */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* -- hole -- */
	{ IW_HEADER_TYPE_NULL, 0, 0, 0, 0, 0},
	/* SIOCSIWRATE */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCGIWRATE */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCSIWRTS */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCGIWRTS */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCSIWFRAG */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCGIWFRAG */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCSIWTXPOW */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCGIWTXPOW */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCSIWRETRY */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCGIWRETRY */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCSIWENCODE */
	{ IW_HEADER_TYPE_POINT, 4, 1, 0, IW_ENCODING_TOKEN_MAX, IW_DESCR_FLAG_EVENT | IW_DESCR_FLAG_RESTRICT},
	/* SIOCGIWENCODE */
	{ IW_HEADER_TYPE_POINT, 0, 1, 0, IW_ENCODING_TOKEN_MAX, IW_DESCR_FLAG_DUMP | IW_DESCR_FLAG_RESTRICT},
	/* SIOCSIWPOWER */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
	/* SIOCGIWPOWER */
	{ IW_HEADER_TYPE_PARAM, 0, 0, 0, 0, 0},
};

/* Size (in bytes) of the various private data types */
char priv_type_size[] = { 0, 1, 1, 0, 4, 4, 0, 0 };

/************************ COMMON SUBROUTINES ************************/
/*
 * Stuff that may be used in various place or doesn't fit in one
 * of the section below.
 */

/* ---------------------------------------------------------------- */
/*
 * Return the driver handler associated with a specific Wireless Extension.
 * Called from various place, so make sure it remains efficient.
 */
static inline iw_handler get_handler(struct net_device *dev,
				     unsigned int cmd)
{
	unsigned int	index;		/* MUST be unsigned */

	/* Check if we have some wireless handlers defined */
	if(dev->wireless_handlers == NULL)
		return NULL;

	/* Try as a standard command */
	index = cmd - SIOCIWFIRST;
	if(index < dev->wireless_handlers->num_standard)
		return dev->wireless_handlers->standard[index];

	/* Try as a private command */
	index = cmd - SIOCIWFIRSTPRIV;
	if(index < dev->wireless_handlers->num_private)
		return dev->wireless_handlers->private[index];

	/* Not found */
	return NULL;
}

/* ---------------------------------------------------------------- */
/*
 * Get statistics out of the driver
 */
static inline struct iw_statistics *get_wireless_stats(struct net_device *dev)
{
	return (dev->get_wireless_stats ?
		dev->get_wireless_stats(dev) :
		(struct iw_statistics *) NULL);
	/* In the future, get_wireless_stats may move from 'struct net_device'
	 * to 'struct iw_handler_def', to de-bloat struct net_device.
	 * Definitely worse a thought... */
}

/* ---------------------------------------------------------------- */
/*
 * Call the commit handler in the driver
 * (if exist and if conditions are right)
 *
 * Note : our current commit strategy is currently pretty dumb,
 * but we will be able to improve on that...
 * The goal is to try to agreagate as many changes as possible
 * before doing the commit. Drivers that will define a commit handler
 * are usually those that need a reset after changing parameters, so
 * we want to minimise the number of reset.
 * A cool idea is to use a timer : at each "set" command, we re-set the
 * timer, when the timer eventually fires, we call the driver.
 * Hopefully, more on that later.
 *
 * Also, I'm waiting to see how many people will complain about the
 * netif_running(dev) test. I'm open on that one...
 * Hopefully, the driver will remember to do a commit in "open()" ;-)
 */
static inline int call_commit_handler(struct net_device *	dev)
{
	if((netif_running(dev)) &&
	   (dev->wireless_handlers->standard[0] != NULL)) {
		/* Call the commit handler on the driver */
		return dev->wireless_handlers->standard[0](dev, NULL,
							   NULL, NULL);
	} else
		return 0;		/* Command completed successfully */
}

/* ---------------------------------------------------------------- */
/*
 * Number of private arguments
 */
static inline int get_priv_size(__u16	args)
{
	int	num = args & IW_PRIV_SIZE_MASK;
	int	type = (args & IW_PRIV_TYPE_MASK) >> 12;

	return num * priv_type_size[type];
}


/******************** /proc/net/wireless SUPPORT ********************/
/*
 * The /proc/net/wireless file is a human readable user-space interface
 * exporting various wireless specific statistics from the wireless devices.
 * This is the most popular part of the Wireless Extensions ;-)
 *
 * This interface is a pure clone of /proc/net/dev (in net/core/dev.c).
 * The content of the file is basically the content of "struct iw_statistics".
 */

#ifdef CONFIG_PROC_FS

/* ---------------------------------------------------------------- */
/*
 * Print one entry (line) of /proc/net/wireless
 */
static inline int sprintf_wireless_stats(char *buffer, struct net_device *dev)
{
	/* Get stats from the driver */
	struct iw_statistics *stats;
	int size;

	stats = get_wireless_stats(dev);
	if (stats != (struct iw_statistics *) NULL) {
		size = sprintf(buffer,
			       "%6s: %04x  %3d%c  %3d%c  %3d%c  %6d %6d %6d %6d %6d   %6d\n",
			       dev->name,
			       stats->status,
			       stats->qual.qual,
			       stats->qual.updated & 1 ? '.' : ' ',
			       stats->qual.level,
			       stats->qual.updated & 2 ? '.' : ' ',
			       stats->qual.noise,
			       stats->qual.updated & 4 ? '.' : ' ',
			       stats->discard.nwid,
			       stats->discard.code,
			       stats->discard.fragment,
			       stats->discard.retries,
			       stats->discard.misc,
			       stats->miss.beacon);
		stats->qual.updated = 0;
	}
	else
		size = 0;

	return size;
}

/* ---------------------------------------------------------------- */
/*
 * Print info for /proc/net/wireless (print all entries)
 */
int dev_get_wireless_info(char * buffer, char **start, off_t offset,
			  int length)
{
	int		len = 0;
	off_t		begin = 0;
	off_t		pos = 0;
	int		size;
	
	struct net_device *	dev;

	size = sprintf(buffer,
		       "Inter-| sta-|   Quality        |   Discarded packets               | Missed\n"
		       " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon\n"
			);
	
	pos += size;
	len += size;

	read_lock(&dev_base_lock);
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		size = sprintf_wireless_stats(buffer + len, dev);
		len += size;
		pos = begin + len;

		if (pos < offset) {
			len = 0;
			begin = pos;
		}
		if (pos > offset + length)
			break;
	}
	read_unlock(&dev_base_lock);

	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);		/* Start slop */
	if (len > length)
		len = length;			/* Ending slop */
	if (len < 0)
		len = 0;

	return len;
}
#endif	/* CONFIG_PROC_FS */

/************************** IOCTL SUPPORT **************************/
/*
 * The original user space API to configure all those Wireless Extensions
 * is through IOCTLs.
 * In there, we check if we need to call the new driver API (iw_handler)
 * or just call the driver ioctl handler.
 */

/* ---------------------------------------------------------------- */
/*
 *	Allow programatic access to /proc/net/wireless even if /proc
 *	doesn't exist... Also more efficient...
 */
static inline int dev_iwstats(struct net_device *dev, struct ifreq *ifr)
{
	/* Get stats from the driver */
	struct iw_statistics *stats;

	stats = get_wireless_stats(dev);
	if (stats != (struct iw_statistics *) NULL) {
		struct iwreq *	wrq = (struct iwreq *)ifr;

		/* Copy statistics to the user buffer */
		if(copy_to_user(wrq->u.data.pointer, stats,
				sizeof(struct iw_statistics)))
			return -EFAULT;

		/* Check if we need to clear the update flag */
		if(wrq->u.data.flags != 0)
			stats->qual.updated = 0;
		return 0;
	} else
		return -EOPNOTSUPP;
}

/* ---------------------------------------------------------------- */
/*
 * Export the driver private handler definition
 * They will be picked up by tools like iwpriv...
 */
static inline int ioctl_export_private(struct net_device *	dev,
				       struct ifreq *		ifr)
{
	struct iwreq *				iwr = (struct iwreq *) ifr;

	/* Check if the driver has something to export */
	if((dev->wireless_handlers->num_private_args == 0) ||
	   (dev->wireless_handlers->private_args == NULL))
		return -EOPNOTSUPP;

	/* Check NULL pointer */
	if(iwr->u.data.pointer == NULL)
		return -EFAULT;
#ifdef WE_STRICT_WRITE
	/* Check if there is enough buffer up there */
	if(iwr->u.data.length < (SIOCIWLASTPRIV - SIOCIWFIRSTPRIV + 1))
		return -E2BIG;
#endif	/* WE_STRICT_WRITE */

	/* Set the number of available ioctls. */
	iwr->u.data.length = dev->wireless_handlers->num_private_args;

	/* Copy structure to the user buffer. */
	if (copy_to_user(iwr->u.data.pointer,
			 dev->wireless_handlers->private_args,
			 sizeof(struct iw_priv_args) * iwr->u.data.length))
		return -EFAULT;

	return 0;
}

/* ---------------------------------------------------------------- */
/*
 * Wrapper to call a standard Wireless Extension handler.
 * We do various checks and also take care of moving data between
 * user space and kernel space.
 */
static inline int ioctl_standard_call(struct net_device *	dev,
				      struct ifreq *		ifr,
				      unsigned int		cmd,
				      iw_handler		handler)
{
	struct iwreq *				iwr = (struct iwreq *) ifr;
	const struct iw_ioctl_description *	descr;
	struct iw_request_info			info;
	int					ret = -EINVAL;

	/* Get the description of the IOCTL */
	descr = &(standard_ioctl[cmd - SIOCIWFIRST]);

#ifdef WE_IOCTL_DEBUG
	printk(KERN_DEBUG "%s : Found standard handler for 0x%04X\n",
	       ifr->ifr_name, cmd);
	printk(KERN_DEBUG "Header type : %d, token type : %d, token_size : %d, max_token : %d\n", descr->header_type, descr->token_type, descr->token_size, descr->max_tokens);
#endif	/* WE_IOCTL_DEBUG */

	/* Prepare the call */
	info.cmd = cmd;
	info.flags = 0;

	/* Check if we have a pointer to user space data or not */
	if(descr->header_type != IW_HEADER_TYPE_POINT) {
		/* No extra arguments. Trivial to handle */
		ret = handler(dev, &info, &(iwr->u), NULL);
	} else {
		char *	extra;
		int	err;

		/* Check what user space is giving us */
		if(IW_IS_SET(cmd)) {
			/* Check NULL pointer */
			if((iwr->u.data.pointer == NULL) &&
			   (iwr->u.data.length != 0))
				return -EFAULT;
			/* Check if number of token fits within bounds */
			if(iwr->u.data.length > descr->max_tokens)
				return -E2BIG;
			if(iwr->u.data.length < descr->min_tokens)
				return -EINVAL;
		} else {
			/* Check NULL pointer */
			if(iwr->u.data.pointer == NULL)
				return -EFAULT;
#ifdef WE_STRICT_WRITE
			/* Check if there is enough buffer up there */
			if(iwr->u.data.length < descr->max_tokens)
				return -E2BIG;
#endif	/* WE_STRICT_WRITE */
		}

#ifdef WE_IOCTL_DEBUG
		printk(KERN_DEBUG "Malloc %d bytes\n",
		       descr->max_tokens * descr->token_size);
#endif	/* WE_IOCTL_DEBUG */

		/* Always allocate for max space. Easier, and won't last
		 * long... */
		extra = kmalloc(descr->max_tokens * descr->token_size,
				GFP_KERNEL);
		if (extra == NULL) {
			return -ENOMEM;
		}

		/* If it is a SET, get all the extra data in here */
		if(IW_IS_SET(cmd) && (iwr->u.data.length != 0)) {
			err = copy_from_user(extra, iwr->u.data.pointer,
					     iwr->u.data.length *
					     descr->token_size);
			if (err) {
				kfree(extra);
				return -EFAULT;
			}
#ifdef WE_IOCTL_DEBUG
			printk(KERN_DEBUG "Got %d bytes\n",
			       iwr->u.data.length * descr->token_size);
#endif	/* WE_IOCTL_DEBUG */
		}

		/* Call the handler */
		ret = handler(dev, &info, &(iwr->u), extra);

		/* If we have something to return to the user */
		if (!ret && IW_IS_GET(cmd)) {
			err = copy_to_user(iwr->u.data.pointer, extra,
					   iwr->u.data.length *
					   descr->token_size);
			if (err)
				ret =  -EFAULT;				   
#ifdef WE_IOCTL_DEBUG
			printk(KERN_DEBUG "Wrote %d bytes\n",
			       iwr->u.data.length * descr->token_size);
#endif	/* WE_IOCTL_DEBUG */
		}

		/* Cleanup - I told you it wasn't that long ;-) */
		kfree(extra);
	}

	/* Call commit handler if needed and defined */
	if(ret == -EIWCOMMIT)
		ret = call_commit_handler(dev);

	/* Here, we will generate the appropriate event if needed */

	return ret;
}

/* ---------------------------------------------------------------- */
/*
 * Wrapper to call a private Wireless Extension handler.
 * We do various checks and also take care of moving data between
 * user space and kernel space.
 * It's not as nice and slimline as the standard wrapper. The cause
 * is struct iw_priv_args, which was not really designed for the
 * job we are going here.
 *
 * IMPORTANT : This function prevent to set and get data on the same
 * IOCTL and enforce the SET/GET convention. Not doing it would be
 * far too hairy...
 * If you need to set and get data at the same time, please don't use
 * a iw_handler but process it in your ioctl handler (i.e. use the
 * old driver API).
 */
static inline int ioctl_private_call(struct net_device *	dev,
				     struct ifreq *		ifr,
				     unsigned int		cmd,
				     iw_handler		handler)
{
	struct iwreq *			iwr = (struct iwreq *) ifr;
	struct iw_priv_args *		descr = NULL;
	struct iw_request_info		info;
	int				extra_size = 0;
	int				i;
	int				ret = -EINVAL;

	/* Get the description of the IOCTL */
	for(i = 0; i < dev->wireless_handlers->num_private_args; i++)
		if(cmd == dev->wireless_handlers->private_args[i].cmd) {
			descr = &(dev->wireless_handlers->private_args[i]);
			break;
		}

#ifdef WE_IOCTL_DEBUG
	printk(KERN_DEBUG "%s : Found private handler for 0x%04X\n",
	       ifr->ifr_name, cmd);
	if(descr) {
		printk(KERN_DEBUG "Name %s, set %X, get %X\n",
		       descr->name, descr->set_args, descr->get_args);
	}
#endif	/* WE_IOCTL_DEBUG */

	/* Compute the size of the set/get arguments */
	if(descr != NULL) {
		if(IW_IS_SET(cmd)) {
			/* Size of set arguments */
			extra_size = get_priv_size(descr->set_args);

			/* Does it fits in iwr ? */
			if((descr->set_args & IW_PRIV_SIZE_FIXED) &&
			   (extra_size < IFNAMSIZ))
				extra_size = 0;
		} else {
			/* Size of set arguments */
			extra_size = get_priv_size(descr->get_args);

			/* Does it fits in iwr ? */
			if((descr->get_args & IW_PRIV_SIZE_FIXED) &&
			   (extra_size < IFNAMSIZ))
				extra_size = 0;
		}
	}

	/* Prepare the call */
	info.cmd = cmd;
	info.flags = 0;

	/* Check if we have a pointer to user space data or not. */
	if(extra_size == 0) {
		/* No extra arguments. Trivial to handle */
		ret = handler(dev, &info, &(iwr->u), (char *) &(iwr->u));
	} else {
		char *	extra;
		int	err;

		/* Check what user space is giving us */
		if(IW_IS_SET(cmd)) {
			/* Check NULL pointer */
			if((iwr->u.data.pointer == NULL) &&
			   (iwr->u.data.length != 0))
				return -EFAULT;

			/* Does it fits within bounds ? */
			if(iwr->u.data.length > (descr->set_args &
						 IW_PRIV_SIZE_MASK))
				return -E2BIG;
		} else {
			/* Check NULL pointer */
			if(iwr->u.data.pointer == NULL)
				return -EFAULT;
		}

#ifdef WE_IOCTL_DEBUG
		printk(KERN_DEBUG "Malloc %d bytes\n", extra_size);
#endif	/* WE_IOCTL_DEBUG */

		/* Always allocate for max space. Easier, and won't last
		 * long... */
		extra = kmalloc(extra_size, GFP_KERNEL);
		if (extra == NULL) {
			return -ENOMEM;
		}

		/* If it is a SET, get all the extra data in here */
		if(IW_IS_SET(cmd) && (iwr->u.data.length != 0)) {
			err = copy_from_user(extra, iwr->u.data.pointer,
					     extra_size);
			if (err) {
				kfree(extra);
				return -EFAULT;
			}
#ifdef WE_IOCTL_DEBUG
			printk(KERN_DEBUG "Got %d elem\n", iwr->u.data.length);
#endif	/* WE_IOCTL_DEBUG */
		}

		/* Call the handler */
		ret = handler(dev, &info, &(iwr->u), extra);

		/* If we have something to return to the user */
		if (!ret && IW_IS_GET(cmd)) {
			err = copy_to_user(iwr->u.data.pointer, extra,
					   extra_size);
			if (err)
				ret =  -EFAULT;				   
#ifdef WE_IOCTL_DEBUG
			printk(KERN_DEBUG "Wrote %d elem\n",
			       iwr->u.data.length);
#endif	/* WE_IOCTL_DEBUG */
		}

		/* Cleanup - I told you it wasn't that long ;-) */
		kfree(extra);
	}


	/* Call commit handler if needed and defined */
	if(ret == -EIWCOMMIT)
		ret = call_commit_handler(dev);

	return ret;
}

/* ---------------------------------------------------------------- */
/*
 * Main IOCTl dispatcher. Called from the main networking code
 * (dev_ioctl() in net/core/dev.c).
 * Check the type of IOCTL and call the appropriate wrapper...
 */
int wireless_process_ioctl(struct ifreq *ifr, unsigned int cmd)
{
	struct net_device *dev;
	iw_handler	handler;

	/* Permissions are already checked in dev_ioctl() before calling us.
	 * The copy_to/from_user() of ifr is also dealt with in there */

	/* Make sure the device exist */
	if ((dev = __dev_get_by_name(ifr->ifr_name)) == NULL)
		return -ENODEV;

	/* A bunch of special cases, then the generic case...
	 * Note that 'cmd' is already filtered in dev_ioctl() with
	 * (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) */
	switch(cmd) 
	{
		case SIOCGIWSTATS:
			/* Get Wireless Stats */
			return dev_iwstats(dev, ifr);

		case SIOCGIWPRIV:
			/* Check if we have some wireless handlers defined */
			if(dev->wireless_handlers != NULL) {
				/* We export to user space the definition of
				 * the private handler ourselves */
				return ioctl_export_private(dev, ifr);
			}
			// ## Fall-through for old API ##
		default:
			/* Generic IOCTL */
			/* Basic check */
			if (!netif_device_present(dev))
				return -ENODEV;
			/* New driver API : try to find the handler */
			handler = get_handler(dev, cmd);
			if(handler != NULL) {
				/* Standard and private are not the same */
				if(cmd < SIOCIWFIRSTPRIV)
					return ioctl_standard_call(dev,
								   ifr,
								   cmd,
								   handler);
				else
					return ioctl_private_call(dev,
								  ifr,
								  cmd,
								  handler);
			}
			/* Old driver API : call driver ioctl handler */
			if (dev->do_ioctl) {
				return dev->do_ioctl(dev, ifr, cmd);
			}
			return -EOPNOTSUPP;
	}
	/* Not reached */
	return -EINVAL;
}
