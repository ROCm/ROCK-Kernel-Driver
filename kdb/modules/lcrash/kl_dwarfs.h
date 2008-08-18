/*
 * $Id: kl_dwarfs.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by: Prashanth Tamraparni (prasht@in.ibm.com)
 * Contributions by SGI
 *
 * Copyright (C) 2004 International Business Machines Corp.
 * Copyright (C) 2004 Silicon Graphics, Inc. All rights reserved.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */
#ifndef __KL_DWARFS_H
#define __KL_DWARFS_H

/* Dwarf function declarations */

int dw_open_namelist(char*, int);
int dw_setup_typeinfo(void);

#endif /*  __KL_DWARFS_H */
