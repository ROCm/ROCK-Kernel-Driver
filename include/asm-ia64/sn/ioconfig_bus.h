/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Silicon Graphics, Inc.  All Rights Reserved.
 */

#define IOCONFIG_PCIBUS "/boot/efi/ioconfig_pcibus"
#define POUND_CHAR                   '#'
#define MAX_LINE_LEN	128
#define MAXPATHLEN	128

struct ioconfig_parm {
	unsigned long ioconfig_activated;
        unsigned long number;
        void *buffer;
};

struct  ascii_moduleid{
                unsigned char   io_moduleid[8]; /* pci path name */
};
