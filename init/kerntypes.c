/*
 * kerntypes.c
 *
 * Copyright (C) 2000 Tom Morano (tjm@sgi.com) and
 *                    Matt D. Robinson (yakker@alacritech.com)
 *
 * Dummy module that includes headers for all kernel types of interest. 
 * The kernel type information is used by the lcrash utility when 
 * analyzing system crash dumps or the live system. Using the type 
 * information for the running system, rather than kernel header files,
 * makes for a more flexible and robust analysis tool.
 *
 * This source code is released under version 2 of the GNU GPL.
 */

#include <linux/compile.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/utsname.h>
#include <linux/dump.h>

#ifdef LINUX_COMPILE_VERSION_ID_TYPE
/* Define version type for version validation of dump and kerntypes */
LINUX_COMPILE_VERSION_ID_TYPE;
#endif

void
kerntypes_dummy(void)
{
}
