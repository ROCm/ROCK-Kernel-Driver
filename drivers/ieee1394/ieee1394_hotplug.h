#ifndef _IEEE1394_HOTPLUG_H
#define _IEEE1394_HOTPLUG_H

#include "ieee1394_core.h"
#include "nodemgr.h"

#define IEEE1394_MATCH_VENDOR_ID	0x0001
#define IEEE1394_MATCH_MODEL_ID		0x0002
#define IEEE1394_MATCH_SPECIFIER_ID	0x0004
#define IEEE1394_MATCH_VERSION		0x0008

struct ieee1394_device_id {
	u32 match_flags;
	u32 vendor_id;
	u32 model_id;
	u32 specifier_id;
	u32 version;
	void *driver_data;
};

struct hpsb_protocol_driver {
	/* The name of the driver, e.g. SBP2 or IP1394 */
	const char *name;

	/* 
	 * The device id table describing the protocols and/or devices
	 * supported by this driver.  This is used by the nodemgr to
	 * decide if a driver could support a given node, but the
	 * probe function below can implement further protocol
	 * dependent or vendor dependent checking.
	 */
	struct ieee1394_device_id *id_table;

	/* 
	 * The probe function is called when a device is added to the
	 * bus and the nodemgr finds a matching entry in the drivers
	 * device id table or when registering this driver and a
	 * previously unhandled device can be handled.  The driver may
	 * decline to handle the device based on further investigation
	 * of the device (or whatever reason) in which case a negative
	 * error code should be returned, otherwise 0 should be
	 * returned. The driver may use the driver_data field in the
	 * unit directory to store per device driver specific data.
	 */
	int (*probe)(struct unit_directory *ud);

	/* 
	 * The disconnect function is called when a device is removed
	 * from the bus or if it wasn't possible to read the guid
	 * after the last bus reset.
	 */
	void (*disconnect)(struct unit_directory *ud);

	/* 
	 * The update function is called when the node has just
	 * survived a bus reset, i.e. it is still present on the bus.
	 * However, it may be necessary to reestablish the connection
	 * or login into the node again, depending on the protocol.
	 */
	void (*update)(struct unit_directory *ud);

	/* Driver in list of all registered drivers */
	struct list_head list;

	/* The list of unit directories managed by this driver */
	struct list_head unit_directories;
};

int hpsb_register_protocol(struct hpsb_protocol_driver *driver);
void hpsb_unregister_protocol(struct hpsb_protocol_driver *driver);

int hpsb_claim_unit_directory(struct unit_directory *ud,
			      struct hpsb_protocol_driver *driver);
void hpsb_release_unit_directory(struct unit_directory *ud);

#endif /* _IEEE1394_HOTPLUG_H */
