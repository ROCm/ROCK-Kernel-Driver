/* i915_drv.c -- i830,i845,i855,i865,i915 driver -*- linux-c -*-
 */

/**************************************************************************
 * 
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * 
 **************************************************************************/

#include "i915.h"
#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#include "drm_agpsupport.h"
#include "drm_auth.h"		/* is this needed? */
#include "drm_bufs.h"
#include "drm_context.h"	/* is this needed? */
#include "drm_drawable.h"	/* is this needed? */
#include "drm_drv.h"
#include "drm_fops.h"
#include "drm_init.h"
#include "drm_irq.h"
#include "drm_ioctl.h"
#include "drm_lock.h"
#include "drm_memory.h"		/*  */
#include "drm_proc.h"
#include "drm_vm.h"
#include "drm_stub.h"
#include "drm_scatter.h"
