/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1997, 1999 by Ralf Baechle
 */
#ifndef _ASM_BCACHE_H
#define _ASM_BCACHE_H

struct bcache_ops {
	void (*bc_enable)(void);
	void (*bc_disable)(void);
	void (*bc_wback_inv)(unsigned long page, unsigned long size);
	void (*bc_inv)(unsigned long page, unsigned long size);
};

extern void indy_sc_init(void);
extern void sni_pcimt_sc_init(void);

extern struct bcache_ops *bcops;

#endif /* _ASM_BCACHE_H */
