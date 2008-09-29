/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains public EFX VI API to Solarflare resource manager.
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

#ifndef __CI_DRIVER_RESOURCE_EFX_VI_H__
#define __CI_DRIVER_RESOURCE_EFX_VI_H__

/* Default size of event queue in the efx_vi resource.  Copied from
 * CI_CFG_NETIF_EVENTQ_SIZE */
#define EFX_VI_EVENTQ_SIZE_DEFAULT 1024

extern int efx_vi_eventq_size;

/**************************************************************************
 * efx_vi_state types, allocation and free
 **************************************************************************/

/*! Handle for refering to a efx_vi */
struct efx_vi_state;

/*!
 * Allocate an efx_vi, including event queue and pt_endpoint
 *
 * \param vih_out Pointer to a handle that is set on success
 * \param ifindex Index of the network interface desired
 * \return Zero on success (and vih_out set), non-zero on failure.
 */
extern int
efx_vi_alloc(struct efx_vi_state **vih_out, int ifindex);

/*!
 * Free a previously allocated efx_vi
 *
 * \param vih The handle of the efx_vi to free
 */
extern void
efx_vi_free(struct efx_vi_state *vih);

/*!
 * Reset a previously allocated efx_vi
 *
 * \param vih The handle of the efx_vi to reset
 */
extern void
efx_vi_reset(struct efx_vi_state *vih);

/**************************************************************************
 * efx_vi_eventq types and functions
 **************************************************************************/

/*!
 * Register a function to receive callbacks when event queue timeouts
 * or wakeups occur.  Only one function per efx_vi can be registered
 * at once.
 *
 * \param vih The handle to identify the efx_vi
 * \param callback The function to callback
 * \param context An argument to pass to the callback function
 * \return Zero on success, non-zero on failure.
 */
extern int
efx_vi_eventq_register_callback(struct efx_vi_state *vih,
				void (*callback)(void *context, int is_timeout),
				void *context);

/*!
 * Remove the current eventq timeout or wakeup callback function
 *
 * \param vih The handle to identify the efx_vi
 * \return Zero on success, non-zero on failure
 */
extern int
efx_vi_eventq_kill_callback(struct efx_vi_state *vih);

/**************************************************************************
 * efx_vi_dma_map types and functions
 **************************************************************************/

/*!
 * Handle for refering to a efx_vi
 */
struct efx_vi_dma_map_state;

/*!
 * Map a list of buffer pages so they are registered with the hardware
 *
 * \param vih The handle to identify the efx_vi
 * \param addrs An array of page pointers to map
 * \param n_addrs Length of the page pointer array.  Must be a power of two.
 * \param dmh_out Set on success to a handle used to refer to this mapping
 * \return Zero on success, non-zero on failure.
 */
extern int
efx_vi_dma_map_pages(struct efx_vi_state *vih, struct page **pages,
			 int n_pages, struct efx_vi_dma_map_state **dmh_out);
extern int
efx_vi_dma_map_addrs(struct efx_vi_state *vih,
		     unsigned long long *dev_bus_addrs, int n_pages,
		     struct efx_vi_dma_map_state **dmh_out);

/*!
 * Unmap a previously mapped set of pages so they are no longer registered
 * with the hardware.
 *
 * \param vih The handle to identify the efx_vi
 * \param dmh The handle to identify the dma mapping
 */
extern void
efx_vi_dma_unmap_pages(struct efx_vi_state *vih,
		       struct efx_vi_dma_map_state *dmh);
extern void
efx_vi_dma_unmap_addrs(struct efx_vi_state *vih,
		       struct efx_vi_dma_map_state *dmh);

/*!
 * Retrieve the buffer address of the mapping
 *
 * \param vih The handle to identify the efx_vi
 * \param dmh The handle to identify the buffer mapping
 * \return The buffer address on success, or zero on failure
 */
extern unsigned
efx_vi_dma_get_map_addr(struct efx_vi_state *vih,
			struct efx_vi_dma_map_state *dmh);

/**************************************************************************
 * efx_vi filter functions
 **************************************************************************/

#define EFX_VI_STATIC_FILTERS 32

/*! Handle to refer to a filter instance */
struct filter_resource_t;

/*!
 * Allocate and add a filter
 *
 * \param vih The handle to identify the efx_vi
 * \param protocol The protocol of the new filter: UDP or TCP
 * \param ip_addr_be32 The local ip address of the filter
 * \param port_le16 The local port of the filter
 * \param fh_out Set on success to be a handle to refer to this filter
 * \return Zero on success, non-zero on failure.
 */
extern int
efx_vi_filter(struct efx_vi_state *vih, int protocol, unsigned ip_addr_be32,
	      int port_le16, struct filter_resource_t **fh_out);

/*!
 * Remove a filter and free resources associated with it
 *
 * \param vih The handle to identify the efx_vi
 * \param fh The handle to identify the filter
 * \return Zero on success, non-zero on failure
 */
extern int
efx_vi_filter_stop(struct efx_vi_state *vih, struct filter_resource_t *fh);

/**************************************************************************
 * efx_vi hw resources types and functions
 **************************************************************************/

/*! Constants for the type field in efx_vi_hw_resource */
#define EFX_VI_HW_RESOURCE_TXDMAQ    0x0	/* PFN of TX DMA Q */
#define EFX_VI_HW_RESOURCE_RXDMAQ    0x1	/* PFN of RX DMA Q */
#define EFX_VI_HW_RESOURCE_EVQTIMER  0x4	/* Address of event q timer */

/* Address of event q pointer (EF1) */
#define EFX_VI_HW_RESOURCE_EVQPTR    0x5
/* Address of register pointer (Falcon A) */
#define EFX_VI_HW_RESOURCE_EVQRPTR   0x6
/* Offset of register pointer (Falcon B) */
#define EFX_VI_HW_RESOURCE_EVQRPTR_OFFSET 0x7
/* Address of mem KVA */
#define EFX_VI_HW_RESOURCE_EVQMEMKVA 0x8
/* PFN of doorbell page (Falcon) */
#define EFX_VI_HW_RESOURCE_BELLPAGE  0x9

/*! How large an array to allocate for the get_() functions - smaller
  than the total number of constants as some are mutually exclusive */
#define EFX_VI_HW_RESOURCE_MAXSIZE   0x7

/*! Constants for the mem_type field in efx_vi_hw_resource */
#define EFX_VI_HW_RESOURCE_IOBUFFER   0	/* Host memory */
#define EFX_VI_HW_RESOURCE_PERIPHERAL 1	/* Card memory/registers */

/*!
 * Data structure providing information on a hardware resource mapping
 */
struct efx_vi_hw_resource {
	u8 type;		/*!< What this resource represents */
	u8 mem_type;		/*!< What type of memory is it in, eg,
				 * host or iomem */
	u8 more_to_follow;	/*!< Is this part of a multi-region resource */
	u32 length;		/*!< Length of the resource in bytes */
	unsigned long address;	/*!< Address of this resource */
};

/*!
 * Metadata concerning the list of hardware resource mappings
 */
struct efx_vi_hw_resource_metadata {
	int evq_order;
	int evq_offs;
	int evq_capacity;
	int instance;
	unsigned rx_capacity;
	unsigned tx_capacity;
	int nic_arch;
	int nic_revision;
	char nic_variant;
};

/*!
 * Obtain a list of hardware resource mappings, using virtual addresses
 *
 * \param vih The handle to identify the efx_vi
 * \param mdata Pointer to a structure to receive the metadata
 * \param hw_res_array An array to receive the list of hardware resources
 * \param length The length of hw_res_array.  Updated on success to contain
 * the number of entries in the supplied array that were used.
 * \return Zero on success, non-zero on failure
 */
extern int
efx_vi_hw_resource_get_virt(struct efx_vi_state *vih,
			    struct efx_vi_hw_resource_metadata *mdata,
			    struct efx_vi_hw_resource *hw_res_array,
			    int *length);

/*!
 * Obtain a list of hardware resource mappings, using physical addresses
 *
 * \param vih The handle to identify the efx_vi
 * \param mdata Pointer to a structure to receive the metadata
 * \param hw_res_array An array to receive the list of hardware resources
 * \param length The length of hw_res_array.  Updated on success to contain
 * the number of entries in the supplied array that were used.
 * \return Zero on success, non-zero on failure
 */
extern int
efx_vi_hw_resource_get_phys(struct efx_vi_state *vih,
			    struct efx_vi_hw_resource_metadata *mdata,
			    struct efx_vi_hw_resource *hw_res_array,
			    int *length);

#endif /* __CI_DRIVER_RESOURCE_EFX_VI_H__ */
