/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_DRIVERLINK_API_H
#define EFX_DRIVERLINK_API_H

#include <linux/list.h>

/* Forward declarations */
struct pci_dev;
struct net_device;
struct sk_buff;
struct efx_dl_device;
struct efx_dl_device_info;

/* An extra safeguard in addition to symbol versioning */
#define EFX_DRIVERLINK_API_VERSION 2

/**
 * struct efx_dl_driver - An Efx driverlink device driver
 *
 * A driverlink client defines and initializes as many instances of
 * efx_dl_driver as required, registering each one with
 * efx_dl_register_driver().
 *
 * @name: Name of the driver
 * @probe: Called when device added
 *	The client should use the @def_info linked list and @silicon_rev
 *	to determine if they wish to attach to this device.
 *	Context: process, driverlink semaphore held
 * @remove: Called when device removed
 *	The client must ensure the finish all operations with this
 *	device before returning from this method.
 *	Context: process, driverlink semaphore held
 * @reset_suspend: Called before device is reset
 *	Called immediately before a hardware reset. The client must stop all
 *	hardware processing before returning from this method. Callbacks will
 *	be inactive when this method is called.
 *	Context: process, driverlink semaphore held. rtnl_lock may be held
 * @reset_resume: Called after device is reset
 *	Called after a hardware reset. If @ok is true, the client should
 *	state and resume normal operations. If @ok is false, the client should
 *	abandon use of the hardware resources. remove() will still be called.
 *	Context: process, driverlink semaphore held. rtnl_lock may be held
 */
struct efx_dl_driver {
	const char *name;

	int (*probe) (struct efx_dl_device *efx_dl_dev,
		      const struct net_device *net_dev,
		      const struct efx_dl_device_info *dev_info,
		      const char *silicon_rev);
	void (*remove) (struct efx_dl_device *efx_dev);
	void (*reset_suspend) (struct efx_dl_device *efx_dev);
	void (*reset_resume) (struct efx_dl_device *efx_dev, int ok);

/* private: */
	struct list_head node;
	struct list_head device_list;
};

/**
 * enum efx_dl_device_info_type - Device information identifier.
 *
 * Used to identify each item in the &struct efx_dl_device_info linked list
 * provided to each driverlink client in the probe() @dev_info member.
 *
 * @EFX_DL_FALCON_RESOURCES: Information type is &struct efx_dl_falcon_resources
 */
enum efx_dl_device_info_type {
	/** Falcon resources available for export */
	EFX_DL_FALCON_RESOURCES = 0,
};

/**
 * struct efx_dl_device_info - device information structure
 *
 * @next: Link to next structure, if any
 * @type: Type code for this structure
 */
struct efx_dl_device_info {
	struct efx_dl_device_info *next;
	enum efx_dl_device_info_type type;
};

/**
 * enum efx_dl_falcon_resource_flags - Falcon resource information flags.
 *
 * Flags that describe hardware variations for the current Falcon device.
 *
 * @EFX_DL_FALCON_DUAL_FUNC: Port is dual-function.
 *	Certain silicon revisions have two pci functions, and require
 *	certain hardware resources to be accessed via the secondary
 *	function
 * @EFX_DL_FALCON_USE_MSI: Port is initialised to use MSI/MSI-X interrupts.
 *	Falcon supports traditional legacy interrupts and MSI/MSI-X
 *	interrupts. The choice is made at run time by the sfc driver, and
 *	notified to the clients by this enumeration
 */
enum efx_dl_falcon_resource_flags {
	EFX_DL_FALCON_DUAL_FUNC = 0x1,
	EFX_DL_FALCON_USE_MSI = 0x2,
};

/**
 * struct efx_dl_falcon_resources - Falcon resource information.
 *
 * This structure describes Falcon hardware resources available for
 * use by a driverlink driver.
 *
 * @hdr: Resource linked list header
 * @biu_lock: Register access lock.
 *	Some Falcon revisions require register access for configuration
 *	registers to be serialised between ports and PCI functions.
 *	The sfc driver will provide the appropriate lock semantics for
 *	the underlying hardware.
 * @buffer_table_min: First available buffer table entry
 * @buffer_table_lim: Last available buffer table entry + 1
 * @evq_timer_min: First available event queue with timer
 * @evq_timer_lim: Last available event queue with timer + 1
 * @evq_int_min: First available event queue with interrupt
 * @evq_int_lim: Last available event queue with interrupt + 1
 * @rxq_min: First available RX queue
 * @rxq_lim: Last available RX queue + 1
 * @txq_min: First available TX queue
 * @txq_lim: Last available TX queue + 1
 * @flags: Hardware variation flags
 */
struct efx_dl_falcon_resources {
	struct efx_dl_device_info hdr;
	spinlock_t *biu_lock;
	unsigned buffer_table_min;
	unsigned buffer_table_lim;
	unsigned evq_timer_min;
	unsigned evq_timer_lim;
	unsigned evq_int_min;
	unsigned evq_int_lim;
	unsigned rxq_min;
	unsigned rxq_lim;
	unsigned txq_min;
	unsigned txq_lim;
	enum efx_dl_falcon_resource_flags flags;
};

/**
 * struct efx_dl_device - An Efx driverlink device.
 *
 * @pci_dev: PCI device used by the sfc driver.
 * @priv: Driver private data
 *	Driverlink clients can use this to store a pointer to their
 *	internal per-device data structure. Each (driver, device)
 *	tuple has a separate &struct efx_dl_device, so clients can use
 *	this @priv field independently.
 * @driver: Efx driverlink driver for this device
 */
struct efx_dl_device {
	struct pci_dev *pci_dev;
	void *priv;
	struct efx_dl_driver *driver;
};

/**
 * enum efx_veto - Packet veto request flag.
 *
 * This is the return type for the rx_packet() and tx_packet() methods
 * in &struct efx_dl_callbacks.
 *
 * @EFX_ALLOW_PACKET: Packet may be transmitted/received
 * @EFX_VETO_PACKET: Packet must not be transmitted/received
 */
enum efx_veto {
	EFX_ALLOW_PACKET = 0,
	EFX_VETO_PACKET = 1,
};

/**
 * struct efx_dl_callbacks - Efx callbacks
 *
 * This is a tighly controlled set of simple callbacks, that are attached
 * to the sfc driver via efx_dl_register_callbacks().  They export just enough
 * state to allow clients to make use of the available hardware resources.
 *
 * For efficiency, only one client can hook each callback. Since these
 * callbacks are called on packet transmit and reception paths, and the
 * sfc driver may have multiple tx and rx queues per port, clients should
 * avoid acquiring locks or allocating memory.
 *
 * @tx_packet: Called when packet is about to be transmitted
 *	Called for every packet about to be transmitted, providing means
 *	for the client to snoop traffic, and veto transmission by returning
 *	%EFX_VETO_PACKET (the sfc driver will subsequently free the skb).
 *	Context: tasklet, netif_tx_lock held
 * @rx_packet: Called when packet is received
 *	Called for every received packet (after LRO), allowing the client
 *	to snoop every received packet (on every rx queue), and veto
 *	reception by returning %EFX_VETO_PACKET.
 *	Context: tasklet
 * @request_mtu: Called to request MTU change.
 *	Called whenever the user requests the net_dev mtu to be changed.
 *	If the client returns an error, the mtu change is aborted. The sfc
 *	driver guarantees that no other callbacks are running.
 *	Context: process, rtnl_lock held.
 * @mtu_changed: Called when MTU has been changed.
 *	Called after the mtu has been successfully changed, always after
 *	a previous call to request_mtu(). The sfc driver guarantees that no
 *	other callbacks are running.
 *	Context: process, rtnl_lock held.
 * @event: Called when a hardware NIC event is not understood by the sfc driver.
 *	Context: tasklet.
 */
struct efx_dl_callbacks {
	enum efx_veto (*tx_packet) (struct efx_dl_device *efx_dev,
				    struct sk_buff *skb);
	enum efx_veto (*rx_packet) (struct efx_dl_device *efx_dev,
				    const char *pkt_hdr, int pkt_len);
	int (*request_mtu) (struct efx_dl_device *efx_dev, int new_mtu);
	void (*mtu_changed) (struct efx_dl_device *efx_dev, int mtu);
	void (*event) (struct efx_dl_device *efx_dev, void *p_event);
};

/* Include API version number in symbol used for efx_dl_register_driver */
#define efx_dl_stringify_1(x, y) x ## y
#define efx_dl_stringify_2(x, y) efx_dl_stringify_1(x, y)
#define efx_dl_register_driver					\
	efx_dl_stringify_2(efx_dl_register_driver_api_ver_,	\
			   EFX_DRIVERLINK_API_VERSION)

/* Exported driverlink api used to register and unregister the client driver
 * and any callbacks [only one per port allowed], and to allow a client driver
 * to request reset to recover from an error condition.
 *
 * All of these functions acquire the driverlink semaphore, so must not be
 * called from an efx_dl_driver or efx_dl_callbacks member, and must be called
 * from process context.
 */
extern int efx_dl_register_driver(struct efx_dl_driver *driver);

extern void efx_dl_unregister_driver(struct efx_dl_driver *driver);

extern int efx_dl_register_callbacks(struct efx_dl_device *efx_dev,
				     struct efx_dl_callbacks *callbacks);

extern void efx_dl_unregister_callbacks(struct efx_dl_device *efx_dev,
					struct efx_dl_callbacks *callbacks);

/* Schedule a reset without grabbing any locks */
extern void efx_dl_schedule_reset(struct efx_dl_device *efx_dev);

/**
 * efx_dl_for_each_device_info_matching - iterate an efx_dl_device_info list
 * @_dev_info: Pointer to first &struct efx_dl_device_info
 * @_type: Type code to look for
 * @_info_type: Structure type corresponding to type code
 * @_field: Name of &struct efx_dl_device_info field in the type
 * @_p: Iterator variable
 *
 * Example:
 *	struct efx_dl_falcon_resources *res;
 *	efx_dl_for_each_device_info_matching(dev_info, EFX_DL_FALCON_RESOURCES,
 *		 			     struct efx_dl_falcon_resources,
 *					     hdr, res) {
 *		if (res->flags & EFX_DL_FALCON_DUAL_FUNC)
 *			....
 *	}
 */
#define efx_dl_for_each_device_info_matching(_dev_info, _type,		\
					     _info_type, _field, _p)	\
	for ((_p) = container_of((_dev_info), _info_type, _field);	\
	     (_p) != NULL;						\
	     (_p) = container_of((_p)->_field.next, _info_type, _field))\
		if ((_p)->_field.type != _type)				\
			continue;					\
		else

/**
 * efx_dl_search_device_info - search an efx_dl_device_info list
 * @_dev_info: Pointer to first &struct efx_dl_device_info
 * @_type: Type code to look for
 * @_info_type: Structure type corresponding to type code
 * @_field: Name of &struct efx_dl_device_info member in this type
 * @_p: Result variable
 *
 * Example:
 *	struct efx_dl_falcon_resources *res;
 *	efx_dl_search_device_info(dev_info, EFX_DL_FALCON_RESOURCES,
 *				  struct efx_dl_falcon_resources, hdr, res);
 *	if (res)
 *		....
 */
#define efx_dl_search_device_info(_dev_info, _type, _info_type,		\
				  _field, _p)				\
	efx_dl_for_each_device_info_matching((_dev_info), (_type),	\
					     _info_type, _field, (_p))	\
		break;

#endif /* EFX_DRIVERLINK_API_H */
