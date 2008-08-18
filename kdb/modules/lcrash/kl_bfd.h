/*
 * $Id: kl_bfd.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libklib.
 * A library which provides access to Linux system kernel dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_BFD_H
#define __KL_BFD_H

/* cpw:  " " form: */
#include "klib.h"

int kl_check_bfd_error(bfd_error_type);
int kl_open_elf(char*, bfd**, bfd**);
int kl_read_bfd_syminfo(maplist_t*);

#endif /*  __KL_BFD_H */
