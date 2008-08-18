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

#ifndef __ASMIA64_KL_TYPES_H
#define __ASMIA64_KL_TYPES_H

/* cpw */
/* was #include <kl_dump_ia64.h> */
#include "kl_dump_ia64.h"

#define HOST_ARCH_IA64
/* cpw: add this, as otherwise comes from makefile */
#define DUMP_ARCH_IA64

/* Format string that allows a single fprintf() call to work for both
 * 32-bit and 64-bit pointer values (architecture specific).
 */
#ifdef CONFIG_X86_32
#define FMT64  "ll"
#else
#define FMT64  "l"
#endif
#define FMTPTR  "l"

/* for usage in common code where host architecture
 * specific type/macro is needed
 */
typedef kl_dump_header_ia64_t kl_dump_header_asm_t;
#define KL_DUMP_ASM_MAGIC_NUMBER KL_DUMP_MAGIC_NUMBER_IA64

#endif /* __ASMIA64_KL_TYPES_H */
