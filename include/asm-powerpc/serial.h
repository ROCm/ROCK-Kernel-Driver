/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_SERIAL_H
#define _ASM_POWERPC_SERIAL_H

/*
 * Serial ports are not listed here, because they are discovered
 * through the device tree.
 */

/* Default baud base if not found in device-tree */
#define BASE_BAUD ( 1843200 / 16 )

extern void find_legacy_serial_ports(void);

#if defined(SUPPORT_SYSRQ) && defined(CONFIG_PPC_PSERIES)
#undef arch_8250_sysrq_via_ctrl_o
extern int power4_sysrq_via_ctrl_o;
#define arch_8250_sysrq_via_ctrl_o(ch, port) ((ch) == '\x0f' && power4_sysrq_via_ctrl_o && uart_handle_break((port)))
#endif

#endif /* _PPC64_SERIAL_H */
