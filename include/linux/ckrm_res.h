/* ckrm_res.h - Dummy resource controller for CKRM
 *
 * Copyright (C) Chandra Seetharaman, IBM Corp. 2003
 * 
 * 
 * Provides a dummy resource controller for CKRM
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 *
 * 06 Nov 2003
 *        Created.
 */

#ifndef _LINUX_CKRM_RES_H
#define _LINUX_CKRM_RES_H

#ifdef CONFIG_CKRM_RES_DUMMY

extern int init_ckrm_dummy_res(void);

#else

#define init_ckrm_dummy_res()

#endif

#endif // _LINUX_CKRM_RES_H
