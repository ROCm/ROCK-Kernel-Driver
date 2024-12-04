/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_FIRMWARE_H
#define _KCL_FIRMWARE_H

#include <linux/firmware.h>

#ifndef HAVE_FIRMWARE_REQUEST_NOWARN
#define firmware_request_nowarn request_firmware
#endif

#endif
