/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1999 by Ralf Baechle
 *
 * Declare variables for rebooting.
 */
#ifndef _ASM_REBOOT_H
#define _ASM_REBOOT_H

void (*_machine_restart)(char *command);
void (*_machine_halt)(void);
void (*_machine_power_off)(void);

#endif /* _ASM_REBOOT_H */
