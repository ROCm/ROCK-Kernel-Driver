/**
 * \file drm_drv.h 
 * Generic driver template
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 *
 * To use this template, you must at least define the following (samples
 * given for the MGA driver):
 *
 * \code
 * #define DRIVER_AUTHOR	"VA Linux Systems, Inc."
 *
 * #define DRIVER_NAME		"mga"
 * #define DRIVER_DESC		"Matrox G200/G400"
 * #define DRIVER_DATE		"20001127"
 *
 * #define DRIVER_MAJOR		2
 * #define DRIVER_MINOR		0
 * #define DRIVER_PATCHLEVEL	2
 *
 * #define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( mga_ioctls )
 *
 * #define DRM(x)		mga_##x
 * \endcode
 */

/*
 * Created: Thu Nov 23 03:10:50 2000 by gareth@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef DRIVER_IOCTLS
#define DRIVER_IOCTLS
#endif

#ifndef MODULE
/** Use an additional macro to avoid preprocessor troubles */
#define DRM_OPTIONS_FUNC DRM(options)
/**
 * Called by the kernel to parse command-line options passed via the
 * boot-loader (e.g., LILO).  It calls the insmod option routine,
 * parse_options().
 */
static int __init DRM(options)( char *str )
{
	DRM(parse_options)( str );
	return 1;
}

__setup( DRIVER_NAME "=", DRM_OPTIONS_FUNC );
#undef DRM_OPTIONS_FUNC
#endif

#define MAX_DEVICES 4
static drm_device_t	DRM(device)[MAX_DEVICES];
static int		DRM(numdevs) = 0;

struct file_operations	DRM(fops) = {
	.owner   = THIS_MODULE,
	.open	 = DRM(open),
	.flush	 = DRM(flush),
	.release = DRM(release),
	.ioctl	 = DRM(ioctl),
	.mmap	 = DRM(mmap),
	.fasync  = DRM(fasync),
	.poll	 = DRM(poll),
	.read	 = DRM(read),
};

/** Ioctl table */
drm_ioctl_desc_t		  DRM(ioctls)[] = {
	[DRM_IOCTL_NR(DRM_IOCTL_VERSION)]       = { DRM(version),     0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_UNIQUE)]    = { DRM(getunique),   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAGIC)]     = { DRM(getmagic),    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_IRQ_BUSID)]     = { DRM(irq_by_busid), 0, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAP)]       = { DRM(getmap),      0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CLIENT)]    = { DRM(getclient),   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_STATS)]     = { DRM(getstats),    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SET_VERSION)]   = { DRM(setversion),  0, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_UNIQUE)]    = { DRM(setunique),   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_BLOCK)]         = { DRM(noop),        1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNBLOCK)]       = { DRM(noop),        1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AUTH_MAGIC)]    = { DRM(authmagic),   1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_MAP)]       = { DRM(addmap),      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_MAP)]        = { DRM(rmmap),       1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_SAREA_CTX)] = { DRM(setsareactx), 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_SAREA_CTX)] = { DRM(getsareactx), 1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_CTX)]       = { DRM(addctx),      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_CTX)]        = { DRM(rmctx),       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MOD_CTX)]       = { DRM(modctx),      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CTX)]       = { DRM(getctx),      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SWITCH_CTX)]    = { DRM(switchctx),   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_NEW_CTX)]       = { DRM(newctx),      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RES_CTX)]       = { DRM(resctx),      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_DRAW)]      = { DRM(adddraw),     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_DRAW)]       = { DRM(rmdraw),      1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_LOCK)]	        = { DRM(lock),        1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNLOCK)]        = { DRM(unlock),      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_FINISH)]        = { DRM(noop),      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_BUFS)]      = { DRM(addbufs),     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MARK_BUFS)]     = { DRM(markbufs),    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_INFO_BUFS)]     = { DRM(infobufs),    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MAP_BUFS)]      = { DRM(mapbufs),     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FREE_BUFS)]     = { DRM(freebufs),    1, 0 },
	/* The DRM_IOCTL_DMA ioctl should be defined by the driver. */

	[DRM_IOCTL_NR(DRM_IOCTL_CONTROL)]       = { DRM(control),     1, 1 },

#if __OS_HAS_AGP
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ACQUIRE)]   = { DRM(agp_acquire), 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_RELEASE)]   = { DRM(agp_release), 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ENABLE)]    = { DRM(agp_enable),  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_INFO)]      = { DRM(agp_info),    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ALLOC)]     = { DRM(agp_alloc),   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_FREE)]      = { DRM(agp_free),    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_BIND)]      = { DRM(agp_bind),    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_UNBIND)]    = { DRM(agp_unbind),  1, 1 },
#endif

	[DRM_IOCTL_NR(DRM_IOCTL_SG_ALLOC)]      = { DRM(sg_alloc),    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_SG_FREE)]       = { DRM(sg_free),     1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_WAIT_VBLANK)]   = { DRM(wait_vblank), 0, 0 },

	DRIVER_IOCTLS
};

#define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( DRM(ioctls) )

#ifdef MODULE
static char *drm_opts = NULL;
#endif

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_PARM( drm_opts, "s" );
MODULE_LICENSE("GPL and additional rights");

static int DRM(setup)( drm_device_t *dev )
{
	int i;
	int ret;

	if (dev->fn_tbl.presetup)
	{
		ret=dev->fn_tbl.presetup(dev);
		if (ret!=0) 
			return ret;
	}

	atomic_set( &dev->ioctl_count, 0 );
	atomic_set( &dev->vma_count, 0 );
	dev->buf_use = 0;
	atomic_set( &dev->buf_alloc, 0 );

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
	{
		i = DRM(dma_setup)( dev );
		if ( i < 0 )
			return i;
	}

	for ( i = 0 ; i < DRM_ARRAY_SIZE(dev->counts) ; i++ )
		atomic_set( &dev->counts[i], 0 );

	for ( i = 0 ; i < DRM_HASH_SIZE ; i++ ) {
		dev->magiclist[i].head = NULL;
		dev->magiclist[i].tail = NULL;
	}

	dev->maplist = DRM(alloc)(sizeof(*dev->maplist),
				  DRM_MEM_MAPS);
	if(dev->maplist == NULL) return -ENOMEM;
	memset(dev->maplist, 0, sizeof(*dev->maplist));
	INIT_LIST_HEAD(&dev->maplist->head);

	dev->ctxlist = DRM(alloc)(sizeof(*dev->ctxlist),
				  DRM_MEM_CTXLIST);
	if(dev->ctxlist == NULL) return -ENOMEM;
	memset(dev->ctxlist, 0, sizeof(*dev->ctxlist));
	INIT_LIST_HEAD(&dev->ctxlist->head);

	dev->vmalist = NULL;
	dev->sigdata.lock = dev->lock.hw_lock = NULL;
	init_waitqueue_head( &dev->lock.lock_queue );
	dev->queue_count = 0;
	dev->queue_reserved = 0;
	dev->queue_slots = 0;
	dev->queuelist = NULL;
	dev->irq_enabled = 0;
	dev->context_flag = 0;
	dev->interrupt_flag = 0;
	dev->dma_flag = 0;
	dev->last_context = 0;
	dev->last_switch = 0;
	dev->last_checked = 0;
	init_waitqueue_head( &dev->context_wait );
	dev->if_version = 0;

	dev->ctx_start = 0;
	dev->lck_start = 0;

	dev->buf_rp = dev->buf;
	dev->buf_wp = dev->buf;
	dev->buf_end = dev->buf + DRM_BSZ;
	dev->buf_async = NULL;
	init_waitqueue_head( &dev->buf_readers );
	init_waitqueue_head( &dev->buf_writers );

	DRM_DEBUG( "\n" );

	/*
	 * The kernel's context could be created here, but is now created
	 * in drm_dma_enqueue.	This is more resource-efficient for
	 * hardware that does not do DMA, but may mean that
	 * drm_select_queue fails between the time the interrupt is
	 * initialized and the time the queues are initialized.
	 */
	if (dev->fn_tbl.postsetup)
		dev->fn_tbl.postsetup(dev);

	return 0;
}


/**
 * Take down the DRM device.
 *
 * \param dev DRM device structure.
 *
 * Frees every resource in \p dev.
 *
 * \sa drm_device and setup().
 */
static int DRM(takedown)( drm_device_t *dev )
{
	drm_magic_entry_t *pt, *next;
	drm_map_t *map;
	drm_map_list_t *r_list;
	struct list_head *list, *list_next;
	drm_vma_entry_t *vma, *vma_next;
	int i;

	DRM_DEBUG( "\n" );

	if (dev->fn_tbl.pretakedown)
	  dev->fn_tbl.pretakedown(dev);

	if ( dev->irq_enabled ) DRM(irq_uninstall)( dev );

	down( &dev->struct_sem );
	del_timer( &dev->timer );

	if ( dev->devname ) {
		DRM(free)( dev->devname, strlen( dev->devname ) + 1,
			   DRM_MEM_DRIVER );
		dev->devname = NULL;
	}

	if ( dev->unique ) {
		DRM(free)( dev->unique, strlen( dev->unique ) + 1,
			   DRM_MEM_DRIVER );
		dev->unique = NULL;
		dev->unique_len = 0;
	}
				/* Clear pid list */
	for ( i = 0 ; i < DRM_HASH_SIZE ; i++ ) {
		for ( pt = dev->magiclist[i].head ; pt ; pt = next ) {
			next = pt->next;
			DRM(free)( pt, sizeof(*pt), DRM_MEM_MAGIC );
		}
		dev->magiclist[i].head = dev->magiclist[i].tail = NULL;
	}

				/* Clear AGP information */
	if (drm_core_has_AGP(dev) && dev->agp) {
		drm_agp_mem_t *entry;
		drm_agp_mem_t *nexte;

				/* Remove AGP resources, but leave dev->agp
                                   intact until drv_cleanup is called. */
		for ( entry = dev->agp->memory ; entry ; entry = nexte ) {
			nexte = entry->next;
			if ( entry->bound ) DRM(unbind_agp)( entry->memory );
			DRM(free_agp)( entry->memory, entry->pages );
			DRM(free)( entry, sizeof(*entry), DRM_MEM_AGPLISTS );
		}
		dev->agp->memory = NULL;

		if ( dev->agp->acquired ) DRM(agp_do_release)();

		dev->agp->acquired = 0;
		dev->agp->enabled  = 0;
	}

				/* Clear vma list (only built for debugging) */
	if ( dev->vmalist ) {
		for ( vma = dev->vmalist ; vma ; vma = vma_next ) {
			vma_next = vma->next;
			DRM(free)( vma, sizeof(*vma), DRM_MEM_VMAS );
		}
		dev->vmalist = NULL;
	}

	if( dev->maplist ) {
		list_for_each_safe( list, list_next, &dev->maplist->head ) {
			r_list = (drm_map_list_t *)list;

			if ( ( map = r_list->map ) ) {
				switch ( map->type ) {
				case _DRM_REGISTERS:
				case _DRM_FRAME_BUFFER:
					if (drm_core_has_MTRR(dev)) {
						if ( map->mtrr >= 0 ) {
							int retcode;
							retcode = mtrr_del( map->mtrr,
									    map->offset,
									    map->size );
							DRM_DEBUG( "mtrr_del=%d\n", retcode );
						}
					}
					DRM(ioremapfree)( map->handle, map->size, dev );
					break;
				case _DRM_SHM:
					vfree(map->handle);
					break;

				case _DRM_AGP:
					/* Do nothing here, because this is all
					 * handled in the AGP/GART driver.
					 */
					break;
				case _DRM_SCATTER_GATHER:
					/* Handle it */
					if (drm_core_check_feature(dev, DRIVER_SG) && dev->sg) {
						DRM(sg_cleanup)(dev->sg);
						dev->sg = NULL;
					}
					break;
				}
				DRM(free)(map, sizeof(*map), DRM_MEM_MAPS);
			}
			list_del( list );
			DRM(free)(r_list, sizeof(*r_list), DRM_MEM_MAPS);
 		}
		DRM(free)(dev->maplist, sizeof(*dev->maplist), DRM_MEM_MAPS);
		dev->maplist = NULL;
 	}

	if (drm_core_check_feature(dev, DRIVER_DMA_QUEUE) && dev->queuelist ) {
		for ( i = 0 ; i < dev->queue_count ; i++ ) {
			if ( dev->queuelist[i] ) {
				DRM(free)( dev->queuelist[i],
					  sizeof(*dev->queuelist[0]),
					  DRM_MEM_QUEUES );
				dev->queuelist[i] = NULL;
			}
		}
		DRM(free)( dev->queuelist,
			  dev->queue_slots * sizeof(*dev->queuelist),
			  DRM_MEM_QUEUES );
		dev->queuelist = NULL;
	}
	dev->queue_count = 0;

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
		DRM(dma_takedown)( dev );

	if ( dev->lock.hw_lock ) {
		dev->sigdata.lock = dev->lock.hw_lock = NULL; /* SHM removed */
		dev->lock.filp = NULL;
		wake_up_interruptible( &dev->lock.lock_queue );
	}
	up( &dev->struct_sem );

	return 0;
}

static void DRM(init_fn_table)(struct drm_device *dev)
{
	dev->fn_tbl.reclaim_buffers = DRM(core_reclaim_buffers);
	dev->fn_tbl.get_map_ofs = DRM(core_get_map_ofs);
	dev->fn_tbl.get_reg_ofs = DRM(core_get_reg_ofs);
}

#include "drm_pciids.h"

static struct pci_device_id DRM(pciidlist)[] = {
	DRM(PCI_IDS)
};

static int DRM(probe)(struct pci_dev *pdev)
{
	drm_device_t *dev;
	int retcode;
	int i;
	int is_compat = 0;

	DRM_DEBUG( "\n" );

	for (i = 0; DRM(pciidlist)[i].vendor != 0; i++) {
		if ((DRM(pciidlist)[i].vendor == pdev->vendor) &&
		    (DRM(pciidlist)[i].device == pdev->device)) {
			is_compat = 1;
		}
	}
	if (is_compat == 0)
		return -ENODEV;

	if (DRM(numdevs) >= MAX_DEVICES)
		return -ENODEV;

	if ((retcode=pci_enable_device(pdev)))
		return retcode;

	dev = &(DRM(device)[DRM(numdevs)]);

	memset( (void *)dev, 0, sizeof(*dev) );
	dev->count_lock = SPIN_LOCK_UNLOCKED;
	init_timer( &dev->timer );
	sema_init( &dev->struct_sem, 1 );
	sema_init( &dev->ctxlist_sem, 1 );

	if ((dev->minor = DRM(stub_register)(DRIVER_NAME, &DRM(fops),dev)) < 0)
		return -EPERM;
	dev->device = MKDEV(DRM_MAJOR, dev->minor );
	dev->name   = DRIVER_NAME;

	dev->pdev   = pdev;
#ifdef __alpha__
	dev->hose   = pdev->sysdata;
	dev->pci_domain = dev->hose->bus->number;
#else
	dev->pci_domain = 0;
#endif
	dev->pci_bus = pdev->bus->number;
	dev->pci_slot = PCI_SLOT(pdev->devfn);
	dev->pci_func = PCI_FUNC(pdev->devfn);
	dev->irq = pdev->irq;

	/* dev_priv_size can be changed by a driver in driver_register_fns */
	dev->dev_priv_size = sizeof(u32);

	/* the DRM has 6 basic counters - drivers add theirs in register_fns */
	dev->counters = 6;
	dev->types[0]  = _DRM_STAT_LOCK;
	dev->types[1]  = _DRM_STAT_OPENS;
	dev->types[2]  = _DRM_STAT_CLOSES;
	dev->types[3]  = _DRM_STAT_IOCTLS;
	dev->types[4]  = _DRM_STAT_LOCKS;
	dev->types[5]  = _DRM_STAT_UNLOCKS;

	DRM(init_fn_table)(dev);

	DRM(driver_register_fns)(dev);

	if (dev->fn_tbl.preinit)
	  dev->fn_tbl.preinit(dev);

	if (drm_core_has_AGP(dev))
	{
		dev->agp = DRM(agp_init)();
		if (drm_core_check_feature(dev, DRIVER_REQUIRE_AGP) && (dev->agp == NULL)) {
			DRM_ERROR( "Cannot initialize the agpgart module.\n" );
			DRM(stub_unregister)(dev->minor);
			DRM(takedown)( dev );
			return -EINVAL;
		}
		if (drm_core_has_MTRR(dev)) {
			if (dev->agp)
				dev->agp->agp_mtrr = mtrr_add( dev->agp->agp_info.aper_base,
							       dev->agp->agp_info.aper_size*1024*1024,
							       MTRR_TYPE_WRCOMB,
							       1 );
		}
	}

	retcode = DRM(ctxbitmap_init)( dev );
	if( retcode ) {
		DRM_ERROR( "Cannot allocate memory for context bitmap.\n" );
		DRM(stub_unregister)(dev->minor);
		DRM(takedown)( dev );
		return retcode;
	}

	DRM(numdevs)++; /* no errors, mark it reserved */
	
	DRM_INFO( "Initialized %s %d.%d.%d %s on minor %d: %s\n",
		DRIVER_NAME,
		DRIVER_MAJOR,
		DRIVER_MINOR,
		DRIVER_PATCHLEVEL,
		DRIVER_DATE,
		dev->minor,
		pci_pretty_name(pdev));

	if (dev->fn_tbl.postinit)
	  dev->fn_tbl.postinit(dev);

	return 0;
}

/**
 * Module initialization. Called via init_module at module load time, or via
 * linux/init/main.c (this is not currently supported).
 *
 * \return zero on success or a negative number on failure.
 *
 * Initializes an array of drm_device structures, and attempts to
 * initialize all available devices, using consecutive minors, registering the
 * stubs and initializing the AGP device.
 * 
 * Expands the \c DRIVER_PREINIT and \c DRIVER_POST_INIT macros before and
 * after the initialization for driver customization.
 */
static int __init drm_init( void )
{
	struct pci_dev *pdev = NULL;

	DRM_DEBUG( "\n" );

#ifdef MODULE
	DRM(parse_options)( drm_opts );
#endif

	DRM(mem_init)();

	for_each_pci_dev(pdev) 
		DRM(probe)(pdev);
	return 0;
}

/**
 * Called via cleanup_module() at module unload time.
 *
 * Cleans up all DRM device, calling takedown().
 * 
 * \sa drm_init().
 */
static void __exit drm_cleanup( void )
{
	drm_device_t *dev;
	int i;

	DRM_DEBUG( "\n" );

	for (i = DRM(numdevs) - 1; i >= 0; i--) {
		dev = &(DRM(device)[i]);
		if ( DRM(stub_unregister)(dev->minor) ) {
			DRM_ERROR( "Cannot unload module\n" );
		} else {
			DRM_DEBUG("minor %d unregistered\n", dev->minor);
			if (i == 0) {
				DRM_INFO( "Module unloaded\n" );
			}
		}

		DRM(ctxbitmap_cleanup)( dev );

		if (drm_core_has_MTRR(dev) && drm_core_has_AGP(dev) &&
		    dev->agp && dev->agp->agp_mtrr >= 0) {
			int retval;
			retval = mtrr_del( dev->agp->agp_mtrr,
				   dev->agp->agp_info.aper_base,
				   dev->agp->agp_info.aper_size*1024*1024 );
			DRM_DEBUG( "mtrr_del=%d\n", retval );
		}

		DRM(takedown)( dev );

		if (drm_core_has_AGP(dev) && dev->agp ) {
			DRM(agp_uninit)();
			DRM(free)( dev->agp, sizeof(*dev->agp), DRM_MEM_AGPLISTS );
			dev->agp = NULL;
		}

		if (dev->fn_tbl.postcleanup)
		  dev->fn_tbl.postcleanup(dev);

	}
	DRM(numdevs) = 0;
}

module_init( drm_init );
module_exit( drm_cleanup );


/**
 * Get version information
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_version structure.
 * \return zero on success or negative number on failure.
 *
 * Fills in the version information in \p arg.
 */
int DRM(version)( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_version_t __user *argp = (void __user *)arg;
	drm_version_t version;
	int len;

	if ( copy_from_user( &version, argp, sizeof(version) ) )
		return -EFAULT;

#define DRM_COPY( name, value )						\
	len = strlen( value );						\
	if ( len > name##_len ) len = name##_len;			\
	name##_len = strlen( value );					\
	if ( len && name ) {						\
		if ( copy_to_user( name, value, len ) )			\
			return -EFAULT;					\
	}

	version.version_major = DRIVER_MAJOR;
	version.version_minor = DRIVER_MINOR;
	version.version_patchlevel = DRIVER_PATCHLEVEL;

	DRM_COPY( version.name, DRIVER_NAME );
	DRM_COPY( version.date, DRIVER_DATE );
	DRM_COPY( version.desc, DRIVER_DESC );

	if ( copy_to_user( argp, &version, sizeof(version) ) )
		return -EFAULT;
	return 0;
}

/**
 * Open file.
 * 
 * \param inode device inode
 * \param filp file pointer.
 * \return zero on success or a negative number on failure.
 *
 * Searches the DRM device with the same minor number, calls open_helper(), and
 * increments the device open count. If the open count was previous at zero,
 * i.e., it's the first that the device is open, then calls setup().
 */
int DRM(open)( struct inode *inode, struct file *filp )
{
	drm_device_t *dev = NULL;
	int retcode = 0;
	int i;

	for (i = 0; i < DRM(numdevs); i++) {
		if (iminor(inode) == DRM(device)[i].minor) {
			dev = &(DRM(device)[i]);
			break;
		}
	}
	if (!dev) {
		return -ENODEV;
	}

	retcode = DRM(open_helper)( inode, filp, dev );
	if ( !retcode ) {
		atomic_inc( &dev->counts[_DRM_STAT_OPENS] );
		spin_lock( &dev->count_lock );
		if ( !dev->open_count++ ) {
			spin_unlock( &dev->count_lock );
			return DRM(setup)( dev );
		}
		spin_unlock( &dev->count_lock );
	}

	return retcode;
}

/**
 * Release file.
 *
 * \param inode device inode
 * \param filp file pointer.
 * \return zero on success or a negative number on failure.
 *
 * If the hardware lock is held then free it, and take it again for the kernel
 * context since it's necessary to reclaim buffers. Unlink the file private
 * data from its list and free it. Decreases the open count and if it reaches
 * zero calls takedown().
 */
int DRM(release)( struct inode *inode, struct file *filp )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev;
	int retcode = 0;

	lock_kernel();
	dev = priv->dev;

	DRM_DEBUG( "open_count = %d\n", dev->open_count );

	if (dev->fn_tbl.prerelease)
		dev->fn_tbl.prerelease(dev, filp);

	/* ========================================================
	 * Begin inline drm_release
	 */

	DRM_DEBUG( "pid = %d, device = 0x%lx, open_count = %d\n",
		   current->pid, (long)old_encode_dev(dev->device), dev->open_count );

	if ( priv->lock_count && dev->lock.hw_lock &&
	     _DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) &&
	     dev->lock.filp == filp ) {
		DRM_DEBUG( "File %p released, freeing lock for context %d\n",
			filp,
			_DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );
		
		if (dev->fn_tbl.release)
			dev->fn_tbl.release(dev, filp);

		DRM(lock_free)( dev, &dev->lock.hw_lock->lock,
				_DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );

				/* FIXME: may require heavy-handed reset of
                                   hardware at this point, possibly
                                   processed via a callback to the X
                                   server. */
	}
	else if ( dev->fn_tbl.release && priv->lock_count && dev->lock.hw_lock ) {
		/* The lock is required to reclaim buffers */
		DECLARE_WAITQUEUE( entry, current );

		add_wait_queue( &dev->lock.lock_queue, &entry );
		for (;;) {
			__set_current_state(TASK_INTERRUPTIBLE);
			if ( !dev->lock.hw_lock ) {
				/* Device has been unregistered */
				retcode = -EINTR;
				break;
			}
			if ( DRM(lock_take)( &dev->lock.hw_lock->lock,
					     DRM_KERNEL_CONTEXT ) ) {
				dev->lock.filp	    = filp;
				dev->lock.lock_time = jiffies;
                                atomic_inc( &dev->counts[_DRM_STAT_LOCKS] );
				break;	/* Got lock */
			}
				/* Contention */
			schedule();
			if ( signal_pending( current ) ) {
				retcode = -ERESTARTSYS;
				break;
			}
		}
		__set_current_state(TASK_RUNNING);
		remove_wait_queue( &dev->lock.lock_queue, &entry );
		if( !retcode ) {
			if (dev->fn_tbl.release)
				dev->fn_tbl.release(dev, filp);
			DRM(lock_free)( dev, &dev->lock.hw_lock->lock,
					DRM_KERNEL_CONTEXT );
		}
	}
	
	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
	{
		dev->fn_tbl.reclaim_buffers(filp);
	}

	DRM(fasync)( -1, filp, 0 );

	down( &dev->ctxlist_sem );
	if ( !list_empty( &dev->ctxlist->head ) ) {
		drm_ctx_list_t *pos, *n;

		list_for_each_entry_safe( pos, n, &dev->ctxlist->head, head ) {
			if ( pos->tag == priv &&
			     pos->handle != DRM_KERNEL_CONTEXT ) {
				if (dev->fn_tbl.context_dtor)
					dev->fn_tbl.context_dtor(dev, pos->handle);

				DRM(ctxbitmap_free)( dev, pos->handle );

				list_del( &pos->head );
				DRM(free)( pos, sizeof(*pos), DRM_MEM_CTXLIST );
				--dev->ctx_count;
			}
		}
	}
	up( &dev->ctxlist_sem );

	down( &dev->struct_sem );
	if ( priv->remove_auth_on_close == 1 ) {
		drm_file_t *temp = dev->file_first;
		while ( temp ) {
			temp->authenticated = 0;
			temp = temp->next;
		}
	}
	if ( priv->prev ) {
		priv->prev->next = priv->next;
	} else {
		dev->file_first	 = priv->next;
	}
	if ( priv->next ) {
		priv->next->prev = priv->prev;
	} else {
		dev->file_last	 = priv->prev;
	}
	up( &dev->struct_sem );
	
	if (dev->fn_tbl.free_filp_priv)
		dev->fn_tbl.free_filp_priv(dev, priv);

	DRM(free)( priv, sizeof(*priv), DRM_MEM_FILES );

	/* ========================================================
	 * End inline drm_release
	 */

	atomic_inc( &dev->counts[_DRM_STAT_CLOSES] );
	spin_lock( &dev->count_lock );
	if ( !--dev->open_count ) {
		if ( atomic_read( &dev->ioctl_count ) || dev->blocked ) {
			DRM_ERROR( "Device busy: %d %d\n",
				   atomic_read( &dev->ioctl_count ),
				   dev->blocked );
			spin_unlock( &dev->count_lock );
			unlock_kernel();
			return -EBUSY;
		}
		spin_unlock( &dev->count_lock );
		unlock_kernel();
		return DRM(takedown)( dev );
	}
	spin_unlock( &dev->count_lock );

	unlock_kernel();

	return retcode;
}

/** 
 * Called whenever a process performs an ioctl on /dev/drm.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument.
 * \return zero on success or negative number on failure.
 *
 * Looks up the ioctl function in the ::ioctls table, checking for root
 * previleges if so required, and dispatches to the respective function.
 */
int DRM(ioctl)( struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ioctl_desc_t *ioctl;
	drm_ioctl_t *func;
	int nr = DRM_IOCTL_NR(cmd);
	int retcode = 0;

	atomic_inc( &dev->ioctl_count );
	atomic_inc( &dev->counts[_DRM_STAT_IOCTLS] );
	++priv->ioctl_count;

	DRM_DEBUG( "pid=%d, cmd=0x%02x, nr=0x%02x, dev 0x%lx, auth=%d\n",
		   current->pid, cmd, nr, (long)old_encode_dev(dev->device), 
		   priv->authenticated );

	if ( nr >= DRIVER_IOCTL_COUNT ) {
		retcode = -EINVAL;
	} else {
		ioctl = &DRM(ioctls)[nr];
		func = ioctl->func;

		if ( !func ) {
			DRM_DEBUG( "no function\n" );
			retcode = -EINVAL;
		} else if ( ( ioctl->root_only && !capable( CAP_SYS_ADMIN ) )||
			    ( ioctl->auth_needed && !priv->authenticated ) ) {
			retcode = -EACCES;
		} else {
			retcode = func( inode, filp, cmd, arg );
		}
	}

	atomic_dec( &dev->ioctl_count );
	return retcode;
}

/** 
 * Lock ioctl.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Add the current task to the lock wait queue, and attempt to take to lock.
 */
int DRM(lock)( struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
        DECLARE_WAITQUEUE( entry, current );
        drm_lock_t lock;
        int ret = 0;

	++priv->lock_count;

        if ( copy_from_user( &lock, (drm_lock_t __user *)arg, sizeof(lock) ) )
		return -EFAULT;

        if ( lock.context == DRM_KERNEL_CONTEXT ) {
                DRM_ERROR( "Process %d using kernel context %d\n",
			   current->pid, lock.context );
                return -EINVAL;
        }

        DRM_DEBUG( "%d (pid %d) requests lock (0x%08x), flags = 0x%08x\n",
		   lock.context, current->pid,
		   dev->lock.hw_lock->lock, lock.flags );

	if (drm_core_check_feature(dev, DRIVER_DMA_QUEUE))
		if ( lock.context < 0 )
			return -EINVAL;

	add_wait_queue( &dev->lock.lock_queue, &entry );
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		if ( !dev->lock.hw_lock ) {
			/* Device has been unregistered */
			ret = -EINTR;
			break;
		}
		if ( DRM(lock_take)( &dev->lock.hw_lock->lock,
				     lock.context ) ) {
			dev->lock.filp      = filp;
			dev->lock.lock_time = jiffies;
			atomic_inc( &dev->counts[_DRM_STAT_LOCKS] );
			break;  /* Got lock */
		}
		
		/* Contention */
		schedule();
		if ( signal_pending( current ) ) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue( &dev->lock.lock_queue, &entry );

	sigemptyset( &dev->sigmask );
	sigaddset( &dev->sigmask, SIGSTOP );
	sigaddset( &dev->sigmask, SIGTSTP );
	sigaddset( &dev->sigmask, SIGTTIN );
	sigaddset( &dev->sigmask, SIGTTOU );
	dev->sigdata.context = lock.context;
	dev->sigdata.lock    = dev->lock.hw_lock;
	block_all_signals( DRM(notifier),
			   &dev->sigdata, &dev->sigmask );
	
	if (dev->fn_tbl.dma_ready && (lock.flags & _DRM_LOCK_READY))
		dev->fn_tbl.dma_ready(dev);
	
	if ( dev->fn_tbl.dma_quiescent && (lock.flags & _DRM_LOCK_QUIESCENT ))
		return dev->fn_tbl.dma_quiescent(dev);
	
	/* dev->fn_tbl.kernel_context_switch isn't used by any of the x86 
	 *  drivers but is used by the Sparc driver.
	 */
	
	if (dev->fn_tbl.kernel_context_switch && 
	    dev->last_context != lock.context) {
	  dev->fn_tbl.kernel_context_switch(dev, dev->last_context, 
					    lock.context);
	}
        DRM_DEBUG( "%d %s\n", lock.context, ret ? "interrupted" : "has lock" );

        return ret;
}

/** 
 * Unlock ioctl.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Transfer and free the lock.
 */
int DRM(unlock)( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_lock_t lock;

	if ( copy_from_user( &lock, (drm_lock_t __user *)arg, sizeof(lock) ) )
		return -EFAULT;

	if ( lock.context == DRM_KERNEL_CONTEXT ) {
		DRM_ERROR( "Process %d using kernel context %d\n",
			   current->pid, lock.context );
		return -EINVAL;
	}

	atomic_inc( &dev->counts[_DRM_STAT_UNLOCKS] );

	/* kernel_context_switch isn't used by any of the x86 drm
	 * modules but is required by the Sparc driver.
	 */
	if (dev->fn_tbl.kernel_context_switch_unlock)
		dev->fn_tbl.kernel_context_switch_unlock(dev, &lock);
	else {
		DRM(lock_transfer)( dev, &dev->lock.hw_lock->lock, 
				    DRM_KERNEL_CONTEXT );
		
		if ( DRM(lock_free)( dev, &dev->lock.hw_lock->lock,
				     DRM_KERNEL_CONTEXT ) ) {
			DRM_ERROR( "\n" );
		}
	}

	unblock_all_signals();
	return 0;
}
