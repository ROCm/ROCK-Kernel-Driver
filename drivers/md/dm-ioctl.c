/*
 * Copyright (C) 2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include <linux/dm-ioctl.h>

#ifdef CONFIG_DM_IOCTL_V4
#include "dm-ioctl-v4.c"
#else
#include "dm-ioctl-v1.c"
#endif
