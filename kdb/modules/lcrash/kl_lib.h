/*
 * $Id: kl_lib.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libutil.
 * A library which provides auxiliary functions.
 * libutil is part of lkcdutils -- utilities for Linux kernel crash dumps.
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

#ifndef __KL_LIB_H
#define __KL_LIB_H

/* Include system header files
 */

#if 0
/* cpw: we don't need this userland stuff: */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <string.h>
#include <setjmp.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#endif

/* Include lkcd library header files
 */
/* cpw: change these from the < > form to the " " form: */
#include "kl_types.h"
#include "kl_alloc.h"
#include "kl_libutil.h"
#include "kl_btnode.h"
#include "kl_htnode.h"
#include "kl_queue.h"
#include "kl_stringtab.h"

#endif /* __KL_LIB_H */
