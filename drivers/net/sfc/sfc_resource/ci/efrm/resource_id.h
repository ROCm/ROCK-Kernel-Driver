/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides public type and definitions resource handle, and the
 * definitions of resource types.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#ifndef __CI_DRIVER_EFRM_RESOURCE_ID_H__
#define __CI_DRIVER_EFRM_RESOURCE_ID_H__

/***********************************************************************
 * Resource handles
 *
 * Resource handles are intended for identifying resources at kernel
 * level, within the context of a particular NIC. particularly because
 * for some resource types, the low 16 bites correspond to hardware
 * IDs. They were historically also used at user level, with a nonce
 * stored in the bits 16 to 27 (inclusive), but that approach is
 * deprecated (but sill alive!).
 *
 * The handle value 0 is used to mean "no resource".
 * Identify resources within the context of a file descriptor at user
 * level.
 ***********************************************************************/

typedef struct {
	uint32_t handle;
} efrm_resource_handle_t;

/* You may think these following functions should all have
 * _HANDLE_ in their names, but really we are providing an abstract set
 * of methods on a (hypothetical) efrm_resource_t object, with
 * efrm_resource_handle_t being just the reference one holds to access
 * the object (aka "this" or "self").
 */

/* Below I use inline instead of macros where possible in order to get
 * more type checking help from the compiler; hopefully we'll never
 * have to rewrite these to use #define as we've found some horrible
 * compiler on which we cannot make static inline do the Right Thing (tm).
 *
 * For consistency and to avoid pointless change I spell these
 * routines as macro names (CAPTILIZE_UNDERSCORED), which also serves
 * to remind people they are compact and inlined.
 */

#define EFRM_RESOURCE_FMT  "[rs:%08x]"

static inline unsigned EFRM_RESOURCE_PRI_ARG(efrm_resource_handle_t h)
{
	return h.handle;
}

static inline unsigned EFRM_RESOURCE_INSTANCE(efrm_resource_handle_t h)
{
	return h.handle & 0x0000ffff;
}

static inline unsigned EFRM_RESOURCE_TYPE(efrm_resource_handle_t h)
{
	return (h.handle & 0xf0000000) >> 28;
}

/***********************************************************************
 * Resource type codes
 ***********************************************************************/

#define EFRM_RESOURCE_IOBUFSET          0x0
#define EFRM_RESOURCE_VI                0x1
#define EFRM_RESOURCE_FILTER            0x2
#define EFRM_RESOURCE_NUM               0x3	/* This isn't a resource! */

#define	EFRM_RESOURCE_NAME(type) \
	((type) == EFRM_RESOURCE_IOBUFSET?	"IOBUFSET"	: \
	 (type) == EFRM_RESOURCE_VI?		"VI"		: \
	 (type) == EFRM_RESOURCE_FILTER?	"FILTER"	: \
						"<invalid>")

#endif /* __CI_DRIVER_EFRM_RESOURCE_ID_H__ */
