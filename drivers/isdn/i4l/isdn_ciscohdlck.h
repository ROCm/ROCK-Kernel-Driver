/*
 * Linux ISDN subsystem, CISCO HDLC network interfaces
 *
 * Copyright 1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *           2001       by Bjoern A. Zeeb <i4l@zabbadoz.net>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef ISDN_CISCOHDLCK_H
#define ISDN_CISCOHDLCK_H

int  isdn_ciscohdlck_setup(isdn_net_dev *p);
void isdn_ciscohdlck_connected(isdn_net_local *lp);
void isdn_ciscohdlck_disconnected(isdn_net_local *lp);

#endif
