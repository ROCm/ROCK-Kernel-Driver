/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
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

#ifndef NETBACK_ACCEL_UTIL_H
#define NETBACK_ACCEL_UTIL_H

#ifdef DPRINTK
#undef DPRINTK
#endif

#define FILE_LEAF strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__

#if 1
#define VPRINTK(_f, _a...) 
#else
#define VPRINTK(_f, _a...)			\
	printk("(file=%s, line=%d) " _f,	\
	       FILE_LEAF , __LINE__ , ## _a )
#endif

#if 1
#define DPRINTK(_f, _a...) 
#else
#define DPRINTK(_f, _a...)			\
	printk("(file=%s, line=%d) " _f,	\
	       FILE_LEAF , __LINE__ , ## _a )
#endif

#define EPRINTK(_f, _a...)			\
	printk("(file=%s, line=%d) " _f,	\
	       FILE_LEAF , __LINE__ , ## _a )

#define EPRINTK_ON(exp)							\
	do {								\
		if (exp)						\
			EPRINTK("%s at %s:%d\n", #exp, __FILE__, __LINE__); \
	} while(0)

#define DPRINTK_ON(exp)							\
	do {								\
		if (exp)						\
			DPRINTK("%s at %s:%d\n", #exp, __FILE__, __LINE__); \
	} while(0)

#include <xen/xenbus.h>

/*! Map a set of pages from another domain
 * \param dev The xenbus device context
 * \param priv The private data returned by the mapping function 
 */
extern 
void *net_accel_map_grants_contig(struct xenbus_device *dev, 
				  unsigned *grants, int npages, 
				  void **priv);

/*! Unmap a set of pages mapped using net_accel_map_grants_contig.
 * \param dev The xenbus device context
 * \param priv The private data returned by the mapping function 
 */
extern 
void net_accel_unmap_grants_contig(struct xenbus_device *dev, void *priv);

/*! Read the MAC address of a device from xenstore */
extern
int net_accel_xen_net_read_mac(struct xenbus_device *dev, u8 mac[]);

/*! Update the accelstate field for a device in xenstore */
extern
void net_accel_update_state(struct xenbus_device *dev, int state);

/* These four map/unmap functions are based on
 * xenbus_backend_client.c:xenbus_map_ring().  However, they are not
 * used for ring buffers, instead just to map pages between domains,
 * or to map a page so that it is accessible by a device
 */
extern
int net_accel_map_device_page(struct xenbus_device *dev,  
			      int gnt_ref, grant_handle_t *handle,
			      u64 *dev_bus_addr);
extern
int net_accel_unmap_device_page(struct xenbus_device *dev,
				grant_handle_t handle, u64 dev_bus_addr);
extern
void *net_accel_map_iomem_page(struct xenbus_device *dev, int gnt_ref,
			     void **priv);
extern
void net_accel_unmap_iomem_page(struct xenbus_device *dev, void *priv);

/*! Grrant a page to remote domain */
extern
int net_accel_grant_page(struct xenbus_device *dev, unsigned long mfn, 
			 int is_iomem);
/*! Undo a net_accel_grant_page */
extern
int net_accel_ungrant_page(grant_ref_t gntref);


/*! Shutdown remote domain that is misbehaving */
extern
int net_accel_shutdown_remote(int domain);


#endif
