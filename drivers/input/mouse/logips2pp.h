/*
 * Logitech PS/2++ mouse driver header
 *
 * Copyright (c) 2003 Vojtech Pavlik <vojtech@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _LOGIPS2PP_H
#define _LOGIPS2PP_H

#define PS2PP_4BTN	0x01
#define PS2PP_WHEEL	0x02
#define PS2PP_MX	0x04
#define PS2PP_MX310	0x08

struct psmouse;
void ps2pp_process_packet(struct psmouse *psmouse);
void ps2pp_set_800dpi(struct psmouse *psmouse);
int ps2pp_detect(struct psmouse *psmouse);
#endif
