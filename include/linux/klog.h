/*
 * KLOG		Generic Logging facility built upon the relayfs infrastructure
 *
 * Authors:	Hubertus Frankeh  (frankeh@us.ibm.com)
 *		Tom Zanussi  (zanussi@us.ibm.com)
 *
 *		Please direct all questions/comments to zanussi@us.ibm.com
 *
 *		Copyright (C) 2003, IBM Corp
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_KLOG_H
#define _LINUX_KLOG_H

extern int klog(const char *fmt, ...);
extern int klog_raw(const char *buf,int len); 

#endif	/* _LINUX_KLOG_H */
