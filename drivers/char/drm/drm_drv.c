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
 * #define drm_x		mga_##x
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

#include "drmP.h"
#include "drm_core.h"

struct file_operations	drm_fops = {
	.owner   = THIS_MODULE,
	.open	 = drm_open,
	.flush	 = drm_flush,
	.release = drm_release,
	.ioctl	 = drm_ioctl,
	.mmap	 = drm_mmap,
	.fasync  = drm_fasync,
	.poll	 = drm_poll,
	.read	 = drm_read,
};

/** Ioctl table */
drm_ioctl_desc_t		  drm_ioctls[] = {
	[DRM_IOCTL_NR(DRM_IOCTL_VERSION)]       = { drm_version,     0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_UNIQUE)]    = { drm_getunique,   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAGIC)]     = { drm_getmagic,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_IRQ_BUSID)]     = { drm_irq_by_busid, 0, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAP)]       = { drm_getmap,      0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CLIENT)]    = { drm_getclient,   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_STATS)]     = { drm_getstats,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SET_VERSION)]   = { drm_setversion,  0, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_UNIQUE)]    = { drm_setunique,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_BLOCK)]         = { drm_noop,        1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNBLOCK)]       = { drm_noop,        1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AUTH_MAGIC)]    = { drm_authmagic,   1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_MAP)]       = { drm_addmap,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_MAP)]        = { drm_rmmap,       1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_SAREA_CTX)] = { drm_setsareactx, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_SAREA_CTX)] = { drm_getsareactx, 1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_CTX)]       = { drm_addctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_CTX)]        = { drm_rmctx,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MOD_CTX)]       = { drm_modctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CTX)]       = { drm_getctx,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SWITCH_CTX)]    = { drm_switchctx,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_NEW_CTX)]       = { drm_newctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RES_CTX)]       = { drm_resctx,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_DRAW)]      = { drm_adddraw,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_DRAW)]       = { drm_rmdraw,      1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_LOCK)]	        = { drm_lock,        1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNLOCK)]        = { drm_unlock,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_FINISH)]        = { drm_noop,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_BUFS)]      = { drm_addbufs,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MARK_BUFS)]     = { drm_markbufs,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_INFO_BUFS)]     = { drm_infobufs,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MAP_BUFS)]      = { drm_mapbufs,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FREE_BUFS)]     = { drm_freebufs,    1, 0 },
	/* The DRM_IOCTL_DMA ioctl should be defined by the driver. */

	[DRM_IOCTL_NR(DRM_IOCTL_CONTROL)]       = { drm_control,     1, 1 },

#if __OS_HAS_AGP
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ACQUIRE)]   = { drm_agp_acquire, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_RELEASE)]   = { drm_agp_release, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ENABLE)]    = { drm_agp_enable,  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_INFO)]      = { drm_agp_info,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ALLOC)]     = { drm_agp_alloc,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_FREE)]      = { drm_agp_free,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_BIND)]      = { drm_agp_bind,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_UNBIND)]    = { drm_agp_unbind,  1, 1 },
#endif

	[DRM_IOCTL_NR(DRM_IOCTL_SG_ALLOC)]      = { drm_sg_alloc,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_SG_FREE)]       = { drm_sg_free,     1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_WAIT_VBLANK)]   = { drm_wait_vblank, 0, 0 },
};

#define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( drm_ioctls )

static int drm_setup( drm_device_t *dev )
{
	int i;
	int ret;

	if (dev->fn_tbl->presetup)
	{
		ret=dev->fn_tbl->presetup(dev);
		if (ret!=0) 
			return ret;
	}

	atomic_set( &dev->ioctl_count, 0 );
	atomic_set( &dev->vma_count, 0 );
	dev->buf_use = 0;
	atomic_set( &dev->buf_alloc, 0 );

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
	{
		i = drm_dma_setup( dev );
		if ( i < 0 )
			return i;
	}

	for ( i = 0 ; i < DRM_ARRAY_SIZE(dev->counts) ; i++ )
		atomic_set( &dev->counts[i], 0 );

	for ( i = 0 ; i < DRM_HASH_SIZE ; i++ ) {
		dev->magiclist[i].head = NULL;
		dev->magiclist[i].tail = NULL;
	}

	dev->maplist = drm_alloc(sizeof(*dev->maplist),
				  DRM_MEM_MAPS);
	if(dev->maplist == NULL) return -ENOMEM;
	memset(dev->maplist, 0, sizeof(*dev->maplist));
	INIT_LIST_HEAD(&dev->maplist->head);

	dev->ctxlist = drm_alloc(sizeof(*dev->ctxlist),
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
	if (dev->fn_tbl->postsetup)
		dev->fn_tbl->postsetup(dev);

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
static int drm_takedown( drm_device_t *dev )
{
	drm_magic_entry_t *pt, *next;
	drm_map_t *map;
	drm_map_list_t *r_list;
	struct list_head *list, *list_next;
	drm_vma_entry_t *vma, *vma_next;
	int i;

	DRM_DEBUG( "\n" );

	if (dev->fn_tbl->pretakedown)
	  dev->fn_tbl->pretakedown(dev);

	if ( dev->irq_enabled ) drm_irq_uninstall( dev );

	down( &dev->struct_sem );
	del_timer( &dev->timer );

	if ( dev->devname ) {
		drm_free( dev->devname, strlen( dev->devname ) + 1,
			   DRM_MEM_DRIVER );
		dev->devname = NULL;
	}

	if ( dev->unique ) {
		drm_free( dev->unique, strlen( dev->unique ) + 1,
			   DRM_MEM_DRIVER );
		dev->unique = NULL;
		dev->unique_len = 0;
	}
				/* Clear pid list */
	for ( i = 0 ; i < DRM_HASH_SIZE ; i++ ) {
		for ( pt = dev->magiclist[i].head ; pt ; pt = next ) {
			next = pt->next;
			drm_free( pt, sizeof(*pt), DRM_MEM_MAGIC );
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
			if ( entry->bound ) drm_unbind_agp( entry->memory );
			drm_free_agp( entry->memory, entry->pages );
			drm_free( entry, sizeof(*entry), DRM_MEM_AGPLISTS );
		}
		dev->agp->memory = NULL;

		if ( dev->agp->acquired ) drm_agp_do_release();

		dev->agp->acquired = 0;
		dev->agp->enabled  = 0;
	}

				/* Clear vma list (only built for debugging) */
	if ( dev->vmalist ) {
		for ( vma = dev->vmalist ; vma ; vma = vma_next ) {
			vma_next = vma->next;
			drm_free( vma, sizeof(*vma), DRM_MEM_VMAS );
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
					drm_ioremapfree( map->handle, map->size, dev );
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
						drm_sg_cleanup(dev->sg);
						dev->sg = NULL;
					}
					break;
				}
				drm_free(map, sizeof(*map), DRM_MEM_MAPS);
			}
			list_del( list );
			drm_free(r_list, sizeof(*r_list), DRM_MEM_MAPS);
 		}
		drm_free(dev->maplist, sizeof(*dev->maplist), DRM_MEM_MAPS);
		dev->maplist = NULL;
 	}

	if (drm_core_check_feature(dev, DRIVER_DMA_QUEUE) && dev->queuelist ) {
		for ( i = 0 ; i < dev->queue_count ; i++ ) {
			if ( dev->queuelist[i] ) {
				drm_free( dev->queuelist[i],
					  sizeof(*dev->queuelist[0]),
					  DRM_MEM_QUEUES );
				dev->queuelist[i] = NULL;
			}
		}
		drm_free( dev->queuelist,
			  dev->queue_slots * sizeof(*dev->queuelist),
			  DRM_MEM_QUEUES );
		dev->queuelist = NULL;
	}
	dev->queue_count = 0;

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
		drm_dma_takedown( dev );

	if ( dev->lock.hw_lock ) {
		dev->sigdata.lock = dev->lock.hw_lock = NULL; /* SHM removed */
		dev->lock.filp = NULL;
		wake_up_interruptible( &dev->lock.lock_queue );
	}
	up( &dev->struct_sem );

	return 0;
}

int drm_fill_in_dev(drm_device_t *dev, struct pci_dev *pdev, const struct pci_device_id *ent, struct drm_driver_fn *driver_fn)
{
	int retcode;

	dev->count_lock = SPIN_LOCK_UNLOCKED;
	init_timer( &dev->timer );
	sema_init( &dev->struct_sem, 1 );
	sema_init( &dev->ctxlist_sem, 1 );

	dev->fops   = &drm_fops;
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

	/* the DRM has 6 basic counters */
	dev->counters = 6;
	dev->types[0]  = _DRM_STAT_LOCK;
	dev->types[1]  = _DRM_STAT_OPENS;
	dev->types[2]  = _DRM_STAT_CLOSES;
	dev->types[3]  = _DRM_STAT_IOCTLS;
	dev->types[4]  = _DRM_STAT_LOCKS;
	dev->types[5]  = _DRM_STAT_UNLOCKS;

	dev->fn_tbl = driver_fn;
	
	if (dev->fn_tbl->preinit)
		if ((retcode = dev->fn_tbl->preinit(dev)))
			goto error_out_unreg;

	if (drm_core_has_AGP(dev)) {
		dev->agp = drm_agp_init();
		if (drm_core_check_feature(dev, DRIVER_REQUIRE_AGP) && (dev->agp == NULL)) {
			DRM_ERROR( "Cannot initialize the agpgart module.\n" );
			retcode = -EINVAL;
			goto error_out_unreg;
		}
		if (drm_core_has_MTRR(dev)) {
			if (dev->agp)
				dev->agp->agp_mtrr = mtrr_add( dev->agp->agp_info.aper_base,
							       dev->agp->agp_info.aper_size*1024*1024,
							       MTRR_TYPE_WRCOMB,
							       1 );
		}
	}

	retcode = drm_ctxbitmap_init( dev );
	if( retcode ) {
		DRM_ERROR( "Cannot allocate memory for context bitmap.\n" );
		goto error_out_unreg;
	}

	dev->device = MKDEV(DRM_MAJOR, dev->minor );

	/* postinit is a required function to display the signon banner */
	if ((retcode = dev->fn_tbl->postinit(dev, ent->driver_data)))
		goto error_out_unreg;

	return 0;
	
error_out_unreg:
	drm_takedown(dev);
	return retcode;
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
int drm_init( struct drm_driver_fn *driver_fn )
{
	struct pci_dev *pdev = NULL;
	struct pci_device_id *pid;
	int i;

	DRM_DEBUG( "\n" );

	drm_mem_init();

	for (i=0; driver_fn->pci_driver.id_table[i].vendor != 0; i++) {
		pid = (struct pci_device_id *)&driver_fn->pci_driver.id_table[i];
		
		pdev=NULL;
		/* pass back in pdev to account for multiple identical cards */		
		while ((pdev = pci_get_subsys(pid->vendor, pid->device, pid->subvendor, pid->subdevice, pdev)) != NULL) {
			/* stealth mode requires a manual probe */
			pci_dev_get(pdev);
			drm_probe(pdev, pid, driver_fn);
		}
	}
	return 0;
}
EXPORT_SYMBOL(drm_init);

/**
 * Called via cleanup_module() at module unload time.
 *
 * Cleans up all DRM device, calling takedown().
 * 
 * \sa drm_init().
 */
static void drm_cleanup( drm_device_t *dev )
{
	DRM_DEBUG( "\n" );

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}

	drm_takedown( dev );	

	drm_ctxbitmap_cleanup( dev );
	
	if (drm_core_has_MTRR(dev) && drm_core_has_AGP(dev) &&
	    dev->agp && dev->agp->agp_mtrr >= 0) {
		int retval;
		retval = mtrr_del( dev->agp->agp_mtrr,
				   dev->agp->agp_info.aper_base,
				   dev->agp->agp_info.aper_size*1024*1024 );
		DRM_DEBUG( "mtrr_del=%d\n", retval );
	}
	
	if (drm_core_has_AGP(dev) && dev->agp ) {
		drm_agp_uninit();
		drm_free( dev->agp, sizeof(*dev->agp), DRM_MEM_AGPLISTS );
		dev->agp = NULL;
	}

	if (dev->fn_tbl->postcleanup)
		dev->fn_tbl->postcleanup(dev);
	
	if ( drm_put_minor(dev) )
		DRM_ERROR( "Cannot unload module\n" );
}

void drm_exit (struct drm_driver_fn *driver_fn)
{
	int i;
	drm_device_t *dev = NULL;
	drm_minor_t *minor;
	
	DRM_DEBUG( "\n" );

	for (i = 0; i < drm_cards_limit; i++) {
		minor = &drm_minors[i];
		if (!minor->dev)
			continue;
		if (minor->dev->fn_tbl!=driver_fn)
			continue;

		dev = minor->dev;
		
	}
	if (dev) {
		/* release the pci driver */
		if (dev->pdev)
			pci_dev_put(dev->pdev);
		drm_cleanup(dev);
	}
	
	DRM_INFO( "Module unloaded\n" );
}
EXPORT_SYMBOL(drm_exit);

static int __init drm_core_init(void)
{
	int ret = -ENOMEM;
	
	drm_cards_limit = (drm_cards_limit < DRM_MAX_MINOR + 1 ? drm_cards_limit : DRM_MAX_MINOR + 1);
	drm_minors = drm_calloc(drm_cards_limit,
				sizeof(*drm_minors), DRM_MEM_STUB);
	if(!drm_minors) 
		goto err_p1;
	
	if (register_chrdev(DRM_MAJOR, "drm", &drm_stub_fops))
		goto err_p1;
	
	drm_class = class_simple_create(THIS_MODULE, "drm");
	if (IS_ERR(drm_class)) {
		printk (KERN_ERR "DRM: Error creating drm class.\n");
		ret = PTR_ERR(drm_class);
		goto err_p2;
	}

	drm_proc_root = create_proc_entry("dri", S_IFDIR, NULL);
	if (!drm_proc_root) {
		DRM_ERROR("Cannot create /proc/dri\n");
		ret = -1;
		goto err_p3;
	}
		
	DRM_INFO( "Initialized %s %d.%d.%d %s\n",
		DRIVER_NAME,
		DRIVER_MAJOR,
		DRIVER_MINOR,
		DRIVER_PATCHLEVEL,
		DRIVER_DATE
		);
	return 0;
err_p3:
	class_simple_destroy(drm_class);
err_p2:
	unregister_chrdev(DRM_MAJOR, "drm");
	drm_free(drm_minors, sizeof(*drm_minors) * drm_cards_limit, DRM_MEM_STUB);
err_p1:	
	return ret;
}

static void __exit drm_core_exit (void)
{
	remove_proc_entry("dri", NULL);
	class_simple_destroy(drm_class);

	unregister_chrdev(DRM_MAJOR, "drm");

	drm_free(drm_minors, sizeof(*drm_minors) *
				drm_cards_limit, DRM_MEM_STUB);
}


module_init( drm_core_init );
module_exit( drm_core_exit );


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
int drm_version( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_version_t __user *argp = (void __user *)arg;
	drm_version_t version;
	int ret;

	if ( copy_from_user( &version, argp, sizeof(version) ) )
		return -EFAULT;

	/* version is a required function to return the personality module version */
	if ((ret = dev->fn_tbl->version(&version)))
		return ret;
		
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
int drm_open( struct inode *inode, struct file *filp )
{
	drm_device_t *dev = NULL;
	int minor = iminor(inode);
	int retcode = 0;

	if (!((minor >= 0) && (minor < drm_cards_limit)))
		return -ENODEV;
		
	dev = drm_minors[minor].dev;
	if (!dev)
		return -ENODEV;
	
	retcode = drm_open_helper( inode, filp, dev );
	if ( !retcode ) {
		atomic_inc( &dev->counts[_DRM_STAT_OPENS] );
		spin_lock( &dev->count_lock );
		if ( !dev->open_count++ ) {
			spin_unlock( &dev->count_lock );
			return drm_setup( dev );
		}
		spin_unlock( &dev->count_lock );
	}

	return retcode;
}
EXPORT_SYMBOL(drm_open);

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
int drm_release( struct inode *inode, struct file *filp )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev;
	int retcode = 0;

	lock_kernel();
	dev = priv->dev;

	DRM_DEBUG( "open_count = %d\n", dev->open_count );

	if (dev->fn_tbl->prerelease)
		dev->fn_tbl->prerelease(dev, filp);

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
		
		if (dev->fn_tbl->release)
			dev->fn_tbl->release(dev, filp);

		drm_lock_free( dev, &dev->lock.hw_lock->lock,
				_DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );

				/* FIXME: may require heavy-handed reset of
                                   hardware at this point, possibly
                                   processed via a callback to the X
                                   server. */
	}
	else if ( dev->fn_tbl->release && priv->lock_count && dev->lock.hw_lock ) {
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
			if ( drm_lock_take( &dev->lock.hw_lock->lock,
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
			if (dev->fn_tbl->release)
				dev->fn_tbl->release(dev, filp);
			drm_lock_free( dev, &dev->lock.hw_lock->lock,
					DRM_KERNEL_CONTEXT );
		}
	}
	
	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
	{
		dev->fn_tbl->reclaim_buffers(filp);
	}

	drm_fasync( -1, filp, 0 );

	down( &dev->ctxlist_sem );
	if ( !list_empty( &dev->ctxlist->head ) ) {
		drm_ctx_list_t *pos, *n;

		list_for_each_entry_safe( pos, n, &dev->ctxlist->head, head ) {
			if ( pos->tag == priv &&
			     pos->handle != DRM_KERNEL_CONTEXT ) {
				if (dev->fn_tbl->context_dtor)
					dev->fn_tbl->context_dtor(dev, pos->handle);

				drm_ctxbitmap_free( dev, pos->handle );

				list_del( &pos->head );
				drm_free( pos, sizeof(*pos), DRM_MEM_CTXLIST );
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
	
	if (dev->fn_tbl->free_filp_priv)
		dev->fn_tbl->free_filp_priv(dev, priv);

	drm_free( priv, sizeof(*priv), DRM_MEM_FILES );

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
		return drm_takedown( dev );
	}
	spin_unlock( &dev->count_lock );

	unlock_kernel();

	return retcode;
}
EXPORT_SYMBOL(drm_release);

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
int drm_ioctl( struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ioctl_desc_t *ioctl;
	drm_ioctl_t *func;
	unsigned int nr = DRM_IOCTL_NR(cmd);
	int retcode = -EINVAL;

	atomic_inc( &dev->ioctl_count );
	atomic_inc( &dev->counts[_DRM_STAT_IOCTLS] );
	++priv->ioctl_count;

	DRM_DEBUG( "pid=%d, cmd=0x%02x, nr=0x%02x, dev 0x%lx, auth=%d\n",
		   current->pid, cmd, nr, (long)old_encode_dev(dev->device), 
		   priv->authenticated );
	
	if (nr < DRIVER_IOCTL_COUNT)
		ioctl = &drm_ioctls[nr];
	else if ((nr >= DRM_COMMAND_BASE) || (nr < DRM_COMMAND_BASE + dev->fn_tbl->num_ioctls))
		ioctl = &dev->fn_tbl->ioctls[nr - DRM_COMMAND_BASE];
	else
		goto err_i1;
	
	func = ioctl->func;
	/* is there a local override? */
	if ((nr == DRM_IOCTL_NR(DRM_IOCTL_DMA)) && dev->fn_tbl->dma_ioctl)
		func = dev->fn_tbl->dma_ioctl;
	
	if ( !func ) {
		DRM_DEBUG( "no function\n" );
		retcode = -EINVAL;
	} else if ( ( ioctl->root_only && !capable( CAP_SYS_ADMIN ) )||
		    ( ioctl->auth_needed && !priv->authenticated ) ) {
		retcode = -EACCES;
	} else {
		retcode = func( inode, filp, cmd, arg );
	}
	
err_i1:
	atomic_dec( &dev->ioctl_count );
	if (retcode) DRM_DEBUG( "ret = %x\n", retcode);
	return retcode;
}
EXPORT_SYMBOL(drm_ioctl);

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
int drm_lock( struct inode *inode, struct file *filp,
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
		if ( drm_lock_take( &dev->lock.hw_lock->lock,
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
	block_all_signals( drm_notifier,
			   &dev->sigdata, &dev->sigmask );
	
	if (dev->fn_tbl->dma_ready && (lock.flags & _DRM_LOCK_READY))
		dev->fn_tbl->dma_ready(dev);
	
	if ( dev->fn_tbl->dma_quiescent && (lock.flags & _DRM_LOCK_QUIESCENT ))
		return dev->fn_tbl->dma_quiescent(dev);
	
	/* dev->fn_tbl->kernel_context_switch isn't used by any of the x86 
	 *  drivers but is used by the Sparc driver.
	 */
	
	if (dev->fn_tbl->kernel_context_switch && 
	    dev->last_context != lock.context) {
	  dev->fn_tbl->kernel_context_switch(dev, dev->last_context, 
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
int drm_unlock( struct inode *inode, struct file *filp,
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
	if (dev->fn_tbl->kernel_context_switch_unlock)
		dev->fn_tbl->kernel_context_switch_unlock(dev, &lock);
	else {
		drm_lock_transfer( dev, &dev->lock.hw_lock->lock, 
				    DRM_KERNEL_CONTEXT );
		
		if ( drm_lock_free( dev, &dev->lock.hw_lock->lock,
				     DRM_KERNEL_CONTEXT ) ) {
			DRM_ERROR( "\n" );
		}
	}

	unblock_all_signals();
	return 0;
}
