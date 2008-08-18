/*
 * $Id: kl_types.h 1122 2004-12-21 23:26:23Z tjm $
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

#ifndef __KL_TYPES_H
#define __KL_TYPES_H

/* The following typedef should be used for variables or return values
 * that contain kernel virtual or physical addresses. It should be sized
 * such that it can hold both pointers of 64 bit architectures as well as
 * pointers from 32 bit architectures.
 */
typedef unsigned long kaddr_t;

/* The following typedef should be used when converting a pointer value
 * (either kernel or application) to an unsigned value for pointer
 * calculations.
 */
typedef unsigned long  uaddr_t;

/* KLIB error type
 */
typedef uint64_t 	k_error_t;

/* Typedef that allows a single fprintf() call to work for both
 * 32-bit and 64-bit pointer values.
 */
#define UADDR(X) ((kaddr_t)X)
#define UADDR64(X) ((kaddr_t)X))
/* #define UADDR(X) ((uaddr_t)X) */
/* #define UADDR64(X) ((uint64_t)((uaddr_t)X)) */


/* cpw */
/* was: #include <asm/kl_types.h> */
#include "asm/kl_types.h"

#endif /* __KL_TYPES_H */
