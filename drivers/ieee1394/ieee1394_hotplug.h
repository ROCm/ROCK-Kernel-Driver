#ifndef _IEEE1394_HOTPLUG_H
#define _IEEE1394_HOTPLUG_H

#include "ieee1394_core.h"

#define IEEE1394_DEVICE_ID_MATCH_VENDOR_ID		0x0001
#define IEEE1394_DEVICE_ID_MATCH_MODEL_ID		0x0002
#define IEEE1394_DEVICE_ID_MATCH_SW_SPECIFIER_ID	0x0004
#define IEEE1394_DEVICE_ID_MATCH_SW_SPECIFIER_VERSION	0x0008

struct ieee1394_device_id {
	u32 match_flags;
	u32 vendor_id;
	u32 model_id;
	u32 sw_specifier_id;
	u32 sw_specifier_version;
};

#define IEEE1394_PROTOCOL(id, version) {				       \
	match_flags:		IEEE1394_DEVICE_ID_MATCH_SW_SPECIFIER_ID |     \
				IEEE1394_DEVICE_ID_MATCH_SW_SPECIFIER_VERSION, \
	sw_specifier_id:	id,					       \
	sw_specifier_version:	version					       \
}

#define IEEE1394_DEVICE(vendor_id, model_id) {			\
	match_flags: 	IEEE1394_DEVICE_ID_MATCH_VENDOR_ID |	\
			IEEE1394_DEVICE_ID_MATCH_MODEL_ID,	\
	vendor_id:	vendor_id,				\
	model_id:	vendor_id,				\
}

#endif /* _IEEE1394_HOTPLUG_H */
