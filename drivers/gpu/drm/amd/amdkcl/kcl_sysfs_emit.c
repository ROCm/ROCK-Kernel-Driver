// SPDX-License-Identifier: GPL-2.0
/*
 * fs/sysfs/file.c - sysfs regular (text) file implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * Please see Documentation/filesystems/sysfs.rst for more information.
 */
#include <linux/mm.h>
#include <linux/sysfs.h>

/* Copied from fs/sysfs/file.c */
#ifndef HAVE_SYSFS_EMIT
int sysfs_emit(char *buf, const char *fmt, ...)
{
        va_list args;
        int len;

        if (WARN(!buf || offset_in_page(buf),
                 "invalid sysfs_emit: buf:%p\n", buf))
                return 0;

        va_start(args, fmt);
        len = vscnprintf(buf, PAGE_SIZE, fmt, args);
        va_end(args);

        return len;
}
EXPORT_SYMBOL_GPL(sysfs_emit);
#endif
