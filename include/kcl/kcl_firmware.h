#ifndef AMDKCL_FIRMWARE_H
#define AMDKCL_FIRMWARE_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
#include <linux/firmware.h>

#define request_firmware_direct   request_firmware

#endif
#endif /* AMDKCL_FIRMWARE_H */

