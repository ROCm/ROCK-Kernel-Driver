/*
 *  drivers/s390/net/netiucv.h
 *    IUCV base support.
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s): Xenia Tkatschow (xenia@us.ibm.com)
 *
 *
 * Functionality:
 * To explore any of the IUCV functions, one must first register
 * their program using iucv_register(). Once your program has
 * successfully completed a register, it can use the other functions.
 * For furthur reference on all IUCV functionality, refer to the
 * CP Programming Services book, also available on the web
 * thru www.ibm.com/s390/vm.
 */
#ifndef _IUCV_H
#define _IUCV_H
#define uchar  unsigned char
#define ushort unsigned short
#define ulong  unsigned long
#define iucv_handle_t void *
/***********************FLAGS*************************************/
#define  source_class   0x01
#define  target_class   0x01
#define  local_conn     0x01
#define  specify_pathid 0x02
#define  specify_msgid  0x04
#define  reply_array    0x08
#define  one_way_msg    0x10
#define  prior_msg      0x20
#define  array          0x40
#define  quiesce_msg    0x40
#define  parm_data      0x80
#define  IPRMDATA	 0x80
#define  IPBUFLST	 0x40
#define  IPPRTY	 0x20
#define  IPNORPY	 0x10
#define  IPANSLST	 0x08
#define  IPFGMID	 0x04
#define  IPFGPID	 0x02
#define  IPFGMCL	 0x01

/*---------------------------------------------------------*/
/* Mapping of external interrupt buffers                   */
/* Names: iucv_ConnectionPending    ->  connection pending */
/*        iucv_ConnectionComplete   ->  connection complete */
/*        iucv_ConnectionSevered    ->  connection severed */
/*        iucv_ConnectionQuiesced   ->  connection quiesced */
/*        iucv_ConnectionResumed    ->  connection resumed */
/*        iucv_MessagePending       ->  message pending    */
/*        iucv_MessageComplete      ->  message complete   */
/*---------------------------------------------------------*/
typedef struct {
	ushort ippathid;
	uchar ipflags1;
	uchar iptype;
	ushort ipmsglim;
	ushort res1;
	uchar ipvmid[8];
	uchar ipuser[16];
	ulong res3;
	uchar ippollfg;
	uchar res4[3];
} iucv_ConnectionPending;

typedef struct {
	ushort ippathid;
	uchar ipflags1;
	uchar iptype;
	ushort ipmsglim;
	ushort res1;
	uchar res2[8];
	uchar ipuser[16];
	ulong res3;
	uchar ippollfg;
	uchar res4[3];
} iucv_ConnectionComplete;

typedef struct {
	ushort ippathid;
	uchar res1;
	uchar iptype;
	ulong res2;
	uchar res3[8];
	uchar ipuser[16];
	ulong res4;
	uchar ippollfg;
	uchar res5[3];
} iucv_ConnectionSevered;

typedef struct {
	ushort ippathid;
	uchar res1;
	uchar iptype;
	ulong res2;
	uchar res3[8];
	uchar ipuser[16];
	ulong res4;
	uchar ippollfg;
	uchar res5[3];
} iucv_ConnectionQuiesced;

typedef struct {
	ushort ippathid;
	uchar res1;
	uchar iptype;
	ulong res2;
	uchar res3[8];
	uchar ipuser[16];
	ulong res4;
	uchar ippollfg;
	uchar res5[3];
} iucv_ConnectionResumed;

typedef struct {
	ushort ippathid;
	uchar ipflags1;
	uchar iptype;
	ulong ipmsgid;
	ulong iptrgcls;
	ulong iprmmsg1;
	union u1 {
		ulong ipbfln1f;
		ulong iprmmsg2;
	} ln1msg2;
	ulong res1[3];
	ulong ipbfln2f;
	uchar ippollfg;
	uchar res2[3];
} iucv_MessagePending;

typedef struct {
	ushort ippathid;
	uchar ipflags1;
	uchar iptype;
	ulong ipmsgid;
	ulong ipaudit;
	ulong iprmmsg1;
	ulong iprmmsg2;
	ulong ipsrccls;
	ulong ipmsgtag;
	ulong res;
	ulong ipbfln2f;
	uchar ippollfg;
	uchar res2[3];
} iucv_MessageComplete;

/************************structures*************************/
/*---------------------------------------------------------*/
/*iucv_interrupt_ops_t: List of functions for interrupt    */
/* handling.                                               */
/*---------------------------------------------------------*/

typedef struct {
	void (*ConnectionPending) (iucv_ConnectionPending * eib,
				   ulong pgm_data);
	void (*ConnectionComplete) (iucv_ConnectionComplete * eib,
				    ulong pgm_data);
	void (*ConnectionSevered) (iucv_ConnectionSevered * eib,
				   ulong pgm_data);
	void (*ConnectionQuiesced) (iucv_ConnectionQuiesced * eib,
				    ulong pgm_data);
	void (*ConnectionResumed) (iucv_ConnectionResumed * eib,
				   ulong pgm_data);
	void (*MessagePending) (iucv_MessagePending * eib,
				ulong pgm_data);
	void (*MessageComplete) (iucv_MessageComplete * eib,
				 ulong pgm_data);
} iucv_interrupt_ops_t;

/*---------------------------------------------------------*/
/*iucv_array_t : Defines buffer array                      */
/*---------------------------------------------------------*/

typedef struct {
	void *address;
	int length;
} iucv_array_t __attribute__ ((aligned (8)));

/*************************-prototypes-******************************/

iucv_handle_t iucv_register_program (uchar pgmname[16],
				     uchar userid[8],
				     uchar pgmmask[24],
				     iucv_interrupt_ops_t * ops,
				     ulong pgm_data);

int iucv_unregister (iucv_handle_t handle);

int iucv_purge (ulong msgid,
		ushort pathid,
		ulong srccls,
		uchar audit[4]);

void iucv_query (ulong * bufsize,
		 ulong * conmax);

int iucv_quiesce (ushort pathid,
		  uchar user_data[16]);

int iucv_resume (ushort pathid,
		 uchar user_data[16]);

int iucv_reject (ushort pathid,
		 ulong msgid,
		 ulong trgcls);

int iucv_setmask (uchar non_priority_interrupts,
		  uchar priority_interrupts,
		  uchar non_priority_completion_interrupts,
		  uchar priority_completion_interrupts);

int iucv_connect (ushort * pathid,
		  ushort msglim,
		  uchar user_data[16],
		  uchar userid[8],
		  uchar system_name[8],
		  uchar priority_requested,
		  uchar prmdata,
		  uchar quiesce,
		  uchar control,
		  uchar local,
		  uchar * priority_permitted,
		  iucv_handle_t handle,
		  ulong pgm_data);

int iucv_accept (ushort pathid,
		 ushort msglim,
		 uchar user_data[16],
		 uchar priority_requested,
		 uchar prmdata,
		 uchar quiesce,
		 uchar control,
		 uchar * priority_permitted,
		 iucv_handle_t handle,
		 ulong pgm_data);

int iucv_sever (ushort pathid,
		uchar user_data[16]);

int iucv_receive (ushort pathid,
		  ulong * msgid,
		  ulong * trgcls,
		  void *buffer, ulong buflen,
		  uchar * reply_required,
		  uchar * priority_msg,
		  ulong * adds_curr_buffer,
		  ulong * adds_curr_length);

int iucv_receive_simple (ushort pathid,
			 ulong msgid,
			 ulong trgcls,
			 void *buffer, ulong buflen);

int iucv_receive_array (ushort pathid,
			ulong * msgid,
			ulong * trgcls,
			iucv_array_t * buffer,
			ulong * buflen,
			uchar * reply_required,
			uchar * priority_msg,
			ulong * adds_curr_buffer,
			ulong * adds_curr_length);

int iucv_send (ushort pathid,
	       ulong * msgid,
	       ulong trgcls,
	       ulong srccls,
	       ulong msgtag,
	       uchar priority_msg,
	       void *buffer,
	       ulong buflen);

int iucv_send_array (ushort pathid,
		     ulong * msgid,
		     ulong trgcls,
		     ulong srccls,
		     ulong msgtag,
		     uchar priority_msg,
		     iucv_array_t * buffer,
		     ulong buflen);

int iucv_send_prmmsg (ushort pathid,
		      ulong * msgid,
		      ulong trgcls,
		      ulong srccls,
		      ulong msgtag,
		      uchar priority_msg,
		      uchar prmmsg[8]);

int iucv_send2way (ushort pathid,
		   ulong * msgid,
		   ulong trgcls,
		   ulong srccls,
		   ulong msgtag,
		   uchar priority_msg,
		   void *buffer,
		   ulong buflen,
		   void *ansbuf,
		   ulong anslen);

int iucv_send2way_array (ushort pathid,
			 ulong * msgid,
			 ulong trgcls,
			 ulong srccls,
			 ulong msgtag,
			 uchar priority_msg,
			 iucv_array_t * buffer,
			 ulong buflen,
			 iucv_array_t * ansbuf,
			 ulong anslen);

int iucv_send2way_prmmsg (ushort pathid,
			  ulong * msgid,
			  ulong trgcls,
			  ulong srccls,
			  ulong msgtag,
			  uchar priority_msg,
			  uchar prmmsg[8],
			  void *ansbuf,
			  ulong anslen);

int iucv_send2way_prmmsg_array (ushort pathid,
				ulong * msgid,
				ulong trgcls, ulong srccls,
				ulong msgtag,
				uchar priority_msg,
				uchar prmmsg[8],
				iucv_array_t * ansbuf,
				ulong anslen);
int iucv_reply (ushort pathid,
		ulong msgid,
		ulong trgcls,
		uchar priority_msg,
		void *buf,
		ulong buflen);

int iucv_reply_array (ushort pathid,
		      ulong msgid,
		      ulong trgcls,
		      uchar priority_msg,
		      iucv_array_t * buffer,
		      ulong buflen);

int iucv_reply_prmmsg (ushort pathid,
		       ulong msgid,
		       ulong trgcls,
		       uchar priority_msg,
		       uchar prmmsg[8]);

#endif
