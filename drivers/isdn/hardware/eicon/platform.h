/* $Id: platform.h,v 1.1.2.6 2001/05/01 15:48:05 armin Exp $
 *
 * platform.h
 * 
 *
 * Copyright 2000-2002  by Armin Schindler (mac@melware.de)
 * Copyright 2000  Eicon Networks 
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */


#ifndef	__PLATFORM_H__
#define	__PLATFORM_H__

#if !defined(DIVA_BUILD)
#define DIVA_BUILD "local"
#endif

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <asm/types.h>
#include <asm/io.h>

#include "cardtype.h"

#define DIVA_INIT_FUNCTION  __init
#define DIVA_EXIT_FUNCTION  __exit

#define DIVA_USER_MODE_CARD_CONFIG 1
#define XDI_USE_XLOG 1
#define	USE_EXTENDED_DEBUGS 1

#define MAX_ADAPTER     32

#define DIVA_ISTREAM 1

#define MEMORY_SPACE_TYPE  0
#define PORT_SPACE_TYPE    1

#include "debuglib.h"

#define dtrc(p) DBG_PRV0(p)
#define dbug(a,p) DBG_PRV1(p)


#include <linux/string.h>

#ifndef	byte
#define	byte   u8
#endif

#ifndef	word
#define	word   u16
#endif

#ifndef	dword
#define	dword  u32
#endif

#ifndef	qword
#define	qword  u64
#endif

#ifndef	TRUE
#define	TRUE	1
#endif

#ifndef	FALSE
#define	FALSE	0
#endif

#ifndef	NULL
#define	NULL	((void *) 0)
#endif

#ifndef	MIN
#define MIN(a,b)	((a)>(b) ? (b) : (a))
#endif

#ifndef	MAX
#define MAX(a,b)	((a)>(b) ? (a) : (b))
#endif

#ifndef	far
#define far
#endif

#ifndef	_pascal
#define _pascal
#endif

#ifndef	_loadds
#define _loadds
#endif

#ifndef	_cdecl
#define _cdecl
#endif

#if !defined(DIM)
#define DIM(array)  (sizeof (array)/sizeof ((array)[0]))
#endif

#define DIVA_INVALID_FILE_HANDLE  ((dword)(-1))

#define DIVAS_CONTAINING_RECORD(address, type, field) \
        ((type *)((char*)(address) - (char*)(&((type *)0)->field)))

extern int sprintf(char *, const char*, ...);

typedef void* LIST_ENTRY;

typedef char    DEVICE_NAME[64];
typedef struct _ISDN_ADAPTER   ISDN_ADAPTER;
typedef struct _ISDN_ADAPTER* PISDN_ADAPTER;

typedef void (* DIVA_DI_PRINTF) (unsigned char *, ...);
extern DIVA_DI_PRINTF dprintf;

typedef struct e_info_s E_INFO ;

typedef char diva_os_dependent_devica_name_t[64];
typedef void* PDEVICE_OBJECT;

struct _diva_os_soft_isr;
struct _diva_os_timer;
struct _ISDN_ADAPTER;

void diva_log_info(unsigned char *, ...);

/*
**  XDI DIDD Interface
*/
void diva_xdi_didd_register_adapter (int card);
void diva_xdi_didd_remove_adapter (int card);

/*
** memory allocation
*/
void* diva_os_malloc (unsigned long flags, unsigned long size);
void  diva_os_free   (unsigned long flags, void* ptr);

/*
** use skbuffs for message buffer
*/
typedef struct sk_buff diva_os_message_buffer_s;
diva_os_message_buffer_s *diva_os_alloc_message_buffer(unsigned long size, void **data_buf);
void diva_os_free_message_buffer(diva_os_message_buffer_s *dmb);
#define DIVA_MESSAGE_BUFFER_LEN(x) x->len
#define DIVA_MESSAGE_BUFFER_DATA(x) x->data

/*
** mSeconds waiting
*/
void diva_os_sleep(dword mSec);
void diva_os_wait(dword mSec);

/*
**  PCI Configuration space access
*/
void PCIwrite (byte bus, byte func, int offset, void* data, int length, void* pci_dev_handle);
void PCIread (byte bus, byte func, int offset, void* data, int length, void* pci_dev_handle);

/*
**  I/O Port utilities
*/
int diva_os_register_io_port (int register, unsigned long port, unsigned long length, const char* name);

/*
**  I/O port access abstraction
*/
byte inpp (void*);
word inppw (void*);
void inppw_buffer (void*, void*, int);
void outppw (void*, word);
void outppw_buffer (void* , void*, int);
void outpp (void*, word);

/*
**  IRQ 
*/
typedef struct _diva_os_adapter_irq_info {
        byte irq_nr;
        int  registered;
        char irq_name[24];
} diva_os_adapter_irq_info_t;
int diva_os_register_irq (void* context, byte irq, const char* name);
void diva_os_remove_irq (void* context, byte irq);

#define diva_os_in_irq() in_irq()

/*
** module locking
*/
#define DIVA_LOCK_MODULE MOD_INC_USE_COUNT
#define DIVA_UNLOCK_MODULE MOD_DEC_USE_COUNT

/*
**  Spin Lock framework
*/
typedef long diva_os_spin_lock_magic_t;
typedef spinlock_t diva_os_spin_lock_t;
static __inline__ int diva_os_initialize_spin_lock (spinlock_t *lock, void * unused) { \
  spin_lock_init (lock); return(0); }
static __inline__ void diva_os_enter_spin_lock (diva_os_spin_lock_t* a, \
                              diva_os_spin_lock_magic_t* old_irql, \
                              void* dbg) { spin_lock_bh(a); }
static __inline__ void diva_os_leave_spin_lock (diva_os_spin_lock_t* a, \
                              diva_os_spin_lock_magic_t* old_irql, \
                              void* dbg) { spin_unlock_bh(a); }

static __inline__ void diva_os_enter_spin_lock_hard (diva_os_spin_lock_t* a, \
                                   diva_os_spin_lock_magic_t* old_irql, \
                                   void* dbg) { \
  unsigned long flags; \
  spin_lock_irqsave (a, flags); \
  *old_irql = (diva_os_spin_lock_magic_t)flags; \
}
static __inline__ void diva_os_leave_spin_lock_hard (diva_os_spin_lock_t* a, \
                                   diva_os_spin_lock_magic_t* old_irql, \
                                   void* dbg) { \
  unsigned long flags = (unsigned long)*old_irql; \
	spin_unlock_irqrestore (a, flags); \
}

#define diva_os_destroy_spin_lock(a,b) do { } while(0)

/*
**  Deffered processing framework
*/
typedef int (*diva_os_isr_callback_t)(struct _ISDN_ADAPTER*);
typedef void (*diva_os_soft_isr_callback_t)(struct _diva_os_soft_isr* psoft_isr, void* context);

typedef struct _diva_os_soft_isr {
  void* object;
  diva_os_soft_isr_callback_t callback;
  void* callback_context;
  char dpc_thread_name[24];
} diva_os_soft_isr_t;

int diva_os_initialize_soft_isr (diva_os_soft_isr_t* psoft_isr, diva_os_soft_isr_callback_t callback, void*   callback_context);
int diva_os_schedule_soft_isr (diva_os_soft_isr_t* psoft_isr);
int diva_os_cancel_soft_isr (diva_os_soft_isr_t* psoft_isr);
void diva_os_remove_soft_isr (diva_os_soft_isr_t* psoft_isr);

/*
  Get time service
  */
void diva_os_get_time (dword* sec, dword* usec);

/*
**  atomic operation, fake because we use threads
*/
typedef int diva_os_atomic_t;
static diva_os_atomic_t __inline__
diva_os_atomic_increment(diva_os_atomic_t* pv)
{
  *pv += 1;
  return (*pv);
}
static diva_os_atomic_t __inline__
diva_os_atomic_decrement(diva_os_atomic_t* pv)
{
  *pv -= 1;
  return (*pv);
}

/* 
**  CAPI SECTION
*/
#define NO_CORNETN
#define IMPLEMENT_DTMF 1
#define IMPLEMENT_LINE_INTERCONNECT2 1
#define IMPLEMENT_ECHO_CANCELLER 1
#define IMPLEMENT_RTP 1
#define IMPLEMENT_T38 1
#define IMPLEMENT_FAX_SUB_SEP_PWD 1
#define IMPLEMENT_V18 1
#define IMPLEMENT_DTMF_TONE 1
#define IMPLEMENT_PIAFS 1
#define IMPLEMENT_FAX_PAPER_FORMATS 1
#define IMPLEMENT_VOWN 1
#define IMPLEMENT_CAPIDTMF 1
#define IMPLEMENT_FAX_NONSTANDARD 1
#define VSWITCH_SUPPORT 1

#define IMPLEMENT_LINE_INTERCONNECT  0
#define IMPLEMENT_MARKED_OK_AFTER_FC 1

#define DIVA_IDI_RX_DMA 1

#define READ_WORD(addr)   readw(addr)
#define READ_DWORD(addr)  readl(addr)

#define WRITE_WORD(addr,v)  writew(v,addr)
#define WRITE_DWORD(addr,v) writel(v,addr)

#ifdef BITS_PER_LONG
 #if BITS_PER_LONG > 32 
  #define PLATFORM_GT_32BIT
  #define ULongToPtr(x) (void *)(unsigned long)(x)
 #endif
#endif

#undef ID_MASK
#undef N_DATA
#undef ADDR

#endif	/* __PLATFORM_H__ */
