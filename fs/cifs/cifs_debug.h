/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2002
 *   Modified by Steve French (sfrench@us.ibm.com)
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
*/
#define CIFS_DEBUG		/* BB temporary */

#ifndef _H_CIFS_DEBUG
#define _H_CIFS_DEBUG

void cifs_dump_mem(char *label, void *data, int length);
extern int traceSMB;		/* flag which enables the function below */
void dump_smb(struct smb_hdr *, int);

/*
 *	debug ON
 *	--------
 */
#ifdef CIFS_DEBUG

/* information message: e.g., configuration, major event */
extern int cifsFYI;
#define cFYI(button,prspec)\
{ if (button && cifsFYI) printk prspec; }

/* debug event message: */
#define cEVENT(button,prspec)\
{ if (button) printk prspec; }

/* error event message: e.g., i/o error */
extern int cifsERROR;
#define cERROR(button, prspec)\
{ if (button && cifsERROR) { printk prspec; if (button > 1) BUG(); } }

/*
 *	debug OFF
 *	---------
 */
#else				/* _CIFS_DEBUG */
#define cERROR(button,prspec)
#define cEVENT(button,prspec)
#define cFYI(button, prspec)
#endif				/* _CIFS_DEBUG */

/*
 *	statistics
 *	----------
 */
#ifdef	_CIFS_STATISTICS
#define	INCREMENT(x)	((x)++)
#define	DECREMENT(x)	((x)--)
#define	HIGHWATERMARK(x,y)	x = MAX((x), (y))
#else
#define	INCREMENT(x)
#define	DECREMENT(x)
#define	HIGHWATERMARK(x,y)
#endif				/* _CIFS_STATISTICS */

#endif				/* _H_CIFS_DEBUG */
