#ifndef _S390_BYTEORDER_H
#define _S390_BYTEORDER_H

/*
 *  include/asm-s390/byteorder.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <asm/types.h>

#ifdef __GNUC__

static __inline__ __const__ __u64 ___arch__swab64(__u64 x)
{
  __u64 result;

  __asm__ __volatile__ (
          "   lrvg %0,%1"
          : "=&d" (result) : "m" (x) );
  return result;
}

static __inline__ __const__ __u64 ___arch__swab64p(__u64 *x)
{
  __u64 result;

  __asm__ __volatile__ (
          "   lrvg %0,%1"
          : "=d" (result) : "m" (*x) );
  return result;
}

static __inline__ void ___arch__swab64s(__u64 *x)
{
  __asm__ __volatile__ (
          "   lrvg %0,%1\n"
	  "   stg  %0,%1"
          : : "m" (*x) : "memory");
}

static __inline__ __const__ __u32 ___arch__swab32(__u32 x)
{
  __u32 result;

  __asm__ __volatile__ (
          "   lrv  %0,%1"
          : "=&d" (result) : "m" (x) );
  return result;
}

static __inline__ __const__ __u32 ___arch__swab32p(__u32 *x)
{
  __u32 result;

  __asm__ __volatile__ (
          "   lrv  %0,%1"
          : "=d" (result) : "m" (*x) );
  return result;
}

static __inline__ void ___arch__swab32s(__u32 *x)
{
  __asm__ __volatile__ (
          "   lrv  %0,%1\n"
	  "   st   %0,%1"
          : : "m" (*x) : "memory");
}

static __inline__ __const__ __u16 ___arch__swab16(__u16 x)
{
  __u16 result;

  __asm__ __volatile__ (
          "   lrvh %0,%1"
          : "=d" (result) : "m" (x) );
  return result;
}

static __inline__ __const__ __u16 ___arch__swab16p(__u16 *x)
{
  __u16 result;

  __asm__ __volatile__ (
          "   lrvh %0,%1"
          : "=d" (result) : "m" (*x) );
  return result;
}

static __inline__ void ___arch__swab16s(__u16 *x)
{
  __asm__ __volatile__ (
          "   lrvh %0,%1\n"
	  "   sth  %0,%1"
          : : "m" (*x) : "memory");
}

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)
#define __arch__swab32p(x) ___arch__swab32p(x)
#define __arch__swab16p(x) ___arch__swab16p(x)
#define __arch__swab32s(x) ___arch__swab32s(x)
#define __arch__swab16s(x) ___arch__swab16s(x)

#define __BYTEORDER_HAS_U64__

#endif /* __GNUC__ */

#include <linux/byteorder/big_endian.h>

#endif /* _S390_BYTEORDER_H */
