/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2002-2003 Silicon Graphics, Inc.  All Rights Reserved.
 */

#ifndef _ASM_IA64_SN_IOC4_H
#define _ASM_IA64_SN_IOC4_H

/*
 * Bytebus device space
 */
#define IOC4_BYTEBUS_DEV0	0x80000L  /* Addressed using pci_bar0 */ 
#define IOC4_BYTEBUS_DEV1	0xA0000L  /* Addressed using pci_bar0 */
#define IOC4_BYTEBUS_DEV2	0xC0000L  /* Addressed using pci_bar0 */
#define IOC4_BYTEBUS_DEV3	0xE0000L  /* Addressed using pci_bar0 */

#endif	/* _ASM_IA64_SN_IOC4_H */
