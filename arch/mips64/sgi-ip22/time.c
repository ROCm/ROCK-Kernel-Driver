/* $Id: time.c,v 1.1 1999/08/20 21:13:34 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * time.c: Generic SGI time_init() code, this will dispatch to the
 *         appropriate per-architecture time/counter init code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>

extern void indy_timer_init(void);

void __init time_init(void)
{
	/* XXX assume INDY for now XXX */
	indy_timer_init();
}
