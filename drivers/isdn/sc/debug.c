/*
 *  $Id: debug.c,v 1.5 2000/11/12 15:29:37 kai Exp $
 *  Copyright (C) 1996  SpellCaster Telecommunications Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  For more information, please contact gpl-info@spellcast.com or write:
 *
 *     SpellCaster Telecommunications Inc.
 *     5621 Finch Avenue East, Unit #3
 *     Scarborough, Ontario  Canada
 *     M1B 2T9
 *     +1 (416) 297-8565
 *     +1 (416) 297-6433 Facsimile
 */
#include <linux/kernel.h>

inline char *strcpy(char *, const char *);

int dbg_level = 0;
static char dbg_funcname[255];

void dbg_endfunc(void)
{
	if (dbg_level) {
		printk("<-- Leaving function %s\n", dbg_funcname);
		strcpy(dbg_funcname, "");
	}
}

void dbg_func(char *func)
{
	strcpy(dbg_funcname, func);
	if(dbg_level)
		printk("--> Entering function %s\n", dbg_funcname);
}

inline char *strcpy(char *dest, const char *src)
{
	char *i = dest;
	char *j = (char *) src;

	while(*j) {
		*i = *j;
		i++; j++;
	}
	*(++i) = 0;
	return dest;
}

inline void pullphone(char *dn, char *str)
{
	int i = 0;

	while(dn[i] != ',')
		str[i] = dn[i], i++;
	str[i] = 0x0;
}
