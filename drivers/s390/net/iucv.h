/*
 *  drivers/s390/net/iucv.h
 *    Network driver for VM using iucv
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Stefan Hegewald <hegewald@de.ibm.com>
 *               Hartmut Penner <hpenner@de.ibm.com> 
 */

#ifndef _IUCV_H
#define _IUCV_H


#define UCHAR  unsigned char
#define USHORT unsigned short
#define ULONG  unsigned long

#define DEFAULT_BUFFERSIZE  2048
#define DEFAULT_FN_LENGTH   27
#define TRANSFERLENGTH      10



/* function ID's */
#define RETRIEVE_BUFFER 2
#define REPLY           3
#define SEND            4
#define RECEIVE         5
#define ACCEPT          10
#define CONNECT         11
#define DECLARE_BUFFER  12
#define SEVER           15
#define SETMASK         16
#define SETCMASK        17
#define PURGE           9999

/* structures */
typedef struct {
  USHORT res0;
  UCHAR  ipflags1;
  UCHAR  iprcode;
  ULONG  res1;
  ULONG  res2;
  ULONG  ipbfadr1;
  ULONG  res[6];
} DCLBFR_T;

typedef struct {
  USHORT ippathid;
  UCHAR  ipflags1;
  UCHAR  iprcode;
  USHORT ipmsglim;
  USHORT res1;
  UCHAR  ipvmid[8];
  UCHAR  ipuser[16];
  UCHAR  iptarget[8];
} CONNECT_T;

typedef struct {
  USHORT ippathid;
  UCHAR  ipflags1;
  UCHAR  iprcode;
  USHORT ipmsglim;
  USHORT res1;
  UCHAR  res2[8];
  UCHAR  ipuser[16];
  UCHAR  res3[8];
} ACCEPT_T;

typedef struct {
  USHORT ippathid;
  UCHAR  ipflags1;
  UCHAR  iprcode;
  ULONG  ipmsgid;
  ULONG  iptrgcls;
  ULONG  ipbfadr1;
  ULONG  ipbfln1f;
  ULONG  ipsrccls;
  ULONG  ipmsgtag;
  ULONG  ipbfadr2;
  ULONG  ipbfln2f;
  ULONG  res;
} SEND_T;

typedef struct {
  USHORT ippathid;
  UCHAR  ipflags1;
  UCHAR  iprcode;
  ULONG  ipmsgid;
  ULONG  iptrgcls;
  ULONG  iprmmsg1;
  ULONG  iprmmsg2;
  ULONG  res1[2];
  ULONG  ipbfadr2;
  ULONG  ipbfln2f;
  ULONG  res2;
} REPLY_T;

typedef struct {
  USHORT ippathid;
  UCHAR  ipflags1;
  UCHAR  iprcode;
  ULONG  ipmsgid;
  ULONG  iptrgcls;
  ULONG  ipbfadr1;
  ULONG  ipbfln1f;
  ULONG  res1[3];
  ULONG  ipbfln2f;
  ULONG  res2;
} RECEIVE_T;

typedef struct {
  USHORT ippathid;
  UCHAR  ipflags1;
  UCHAR  iprcode;
  ULONG  res1[3];
  UCHAR  ipuser[16];
  ULONG  res2[2];
} SEVER_T;

typedef struct {
  UCHAR  ipmask;
  UCHAR  res1[2];
  UCHAR  iprcode;
  ULONG  res2[9];
} MASK_T;

typedef struct {
  USHORT ippathid;
  UCHAR  ipflags1;
  UCHAR  iptype;
  ULONG  ipmsgid;
  ULONG  ipaudit;
  ULONG  iprmmsg1;
  ULONG  iprmmsg2;
  ULONG  ipsrccls;
  ULONG  ipmsgtag;
  ULONG  ipbfadr2;
  ULONG  ipbfln2f;
  UCHAR  ippollfg;
  UCHAR  res2[3];
} INTERRUPT_T;


#endif
