/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP4xxx device driver for Linux 2.6.x
 * Copyright (C) 2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/
/*
 * File Name: qlud.h
 *
 * Revision History:
 *
 */

#ifndef	_QLUD_H
#define	_QLUD_H

/*
 * NOTE: the following version defines must be updated each time the
 *	 changes made may affect the backward compatibility of the
 *	 input/output relations
 */
#define	UD_VERSION             1
#define	UD_VERSION_STR         "1.0"

/*
 * ***********************************************************************
 * Data type definitions
 * ***********************************************************************
 */
#ifdef _MSC_VER

#define	UD_BOOL	BOOLEAN
#define	UD_UI1	UCHAR
#define	UD_UI2	USHORT
#define	UD_UI4	ULONG
#define	UD_UI8	ULONGLONG
#define	UD_I1	CHAR
#define	UD_I2	SHORT
#define	UD_I4	LONG
#define	UD_I8	LONGLONG
#define UD_V	VOID
#define	UD_PV	PVOID
#define	PUD_UI1	PUCHAR
#define	PUD_UI2	PUSHORT
#define	PUD_UI4	PULONG
#define	PUD_I1	PCHAR
#define	PUD_I2	PSHORT
#define	PUD_I4	PLONG
#define UD_H	PVOID

#define PUD_H	UD_H*

#elif defined(linux)                 /* Linux */

#ifdef APILIB
#include <stdint.h>
#endif

#define	UD_BOOL	uint8_t
#define	UD_UI1	uint8_t
#define	UD_UI2	uint16_t
#define	UD_UI4	uint32_t
#define	UD_UI8	uint64_t
#define	UD_I1	int8_t
#define	UD_I2	int16_t
#define	UD_I4	int32_t
#define	UD_I8	int64_t
#define UD_V	void
#define	UD_PV	void *
#define	PUD_UI1	uint8_t *
#define	PUD_UI2	uint16_t *
#define	PUD_UI4	uint32_t *
#define	PUD_I1	int8_t *
#define	PUD_I2	int16_t *
#define	PUD_I4	int32_t *
#define UD_H	int
#define PUD_H	int *

#elif defined(sun) || defined(__sun) /* Solaris */

#endif

#endif /* _QLUD_H */
