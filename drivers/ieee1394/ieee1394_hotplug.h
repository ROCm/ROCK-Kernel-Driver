#ifndef _IEEE1394_HOTPLUG_H
#define _IEEE1394_HOTPLUG_H

#include <linux/device.h>

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
	 * The update function is called when the node has just
	 * survived a bus reset, i.e. it is still present on the bus.
	 * However, it may be necessary to reestablish the connection
	 * or login into the node again, depending on the protocol.
	 */
	void (*update)(struct unit_directory *ud);


	/* Our LDM structure */
	struct device_driver driver;
};

int hpsb_register_protocol(struct hpsb_protocol_driver *driver);
void hpsb_unregister_protocol(struct hpsb_protocol_driver *driver);

#endif /* _IEEE1394_HOTPLUG_H */
