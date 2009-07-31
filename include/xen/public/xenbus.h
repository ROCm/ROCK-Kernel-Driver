/******************************************************************************
 * xenbus.h
 * 
 * Interface to /proc/xen/xenbus.
 * 
 * Copyright (c) 2008, Diego Ongaro <diego.ongaro@citrix.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __LINUX_PUBLIC_XENBUS_H__
#define __LINUX_PUBLIC_XENBUS_H__

#include <linux/types.h>

#ifndef __user
#define __user
#endif

typedef struct xenbus_alloc {
	domid_t dom;
	__u32 port;
	__u32 grant_ref;
} xenbus_alloc_t;

/*
 * @cmd: IOCTL_XENBUS_ALLOC
 * @arg: &xenbus_alloc_t
 * Return: 0, or -1 for error
 */
#define IOCTL_XENBUS_ALLOC					\
	_IOC(_IOC_NONE, 'X', 0, sizeof(xenbus_alloc_t))

#endif /* __LINUX_PUBLIC_XENBUS_H__ */
