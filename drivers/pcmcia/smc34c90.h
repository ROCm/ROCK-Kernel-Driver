/*
 * smc34c90.h 1.7 1999/10/25 20:03:34
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dhinds@pcmcia.sourceforge.org>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_SMC34C90_H
#define _LINUX_SMC34C90_H

#ifndef PCI_VENDOR_ID_SMC
#define PCI_VENDOR_ID_SMC		0x10b3
#endif

#ifndef PCI_DEVICE_ID_SMC_34C90
#define PCI_DEVICE_ID_SMC_34C90		0xb106
#endif

/* Register definitions for SMC 34C90 PCI-to-CardBus bridge */

/* EEPROM Information Register */
#define SMC34C90_EEINFO			0x0088
#define SMC34C90_EEINFO_ONE_SOCKET	0x0001
#define SMC34C90_EEINFO_5V_ONLY		0x0002
#define SMC34C90_EEINFO_ISA_IRQ		0x0004
#define SMC34C90_EEINFO_ZV_PORT		0x0008
#define SMC34C90_EEINFO_RING		0x0010
#define SMC34C90_EEINFO_LED		0x0020

#endif /* _LINUX_SMC34C90_H */
