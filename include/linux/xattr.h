/*
  File: linux/xattr.h

  Extended attributes handling.

  Copyright (C) 2001 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (c) 2001-2002 Silicon Graphics, Inc.  All Rights Reserved.
*/
#ifndef _LINUX_XATTR_H
#define _LINUX_XATTR_H

#define XATTR_CREATE		0x1	/* fail if attr already exists */
#define XATTR_REPLACE		0x2	/* fail if attr does not exist */
#define XATTR_KERNEL_CONTEXT	0x4	/* called from kernel context */

#endif	/* _LINUX_XATTR_H */
