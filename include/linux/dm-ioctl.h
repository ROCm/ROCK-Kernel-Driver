/*
 * Copyright (C) 2003 Sistina Software (UK) Limited.
 *
 * This file is released under the LGPL.
 */

#ifndef _LINUX_DM_IOCTL_H
#define _LINUX_DM_IOCTL_H

#include <linux/config.h>

#ifdef CONFIG_DM_IOCTL_V4
#include "dm-ioctl-v4.h"
#else
#include "dm-ioctl-v1.h"
#endif

#endif
