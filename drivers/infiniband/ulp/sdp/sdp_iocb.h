/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: sdp_iocb.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_SDP_IOCB_H
#define _TS_SDP_IOCB_H
/*
 * topspin specific includes.
 */
#include "sdp_types.h"
#include "sdp_queue.h"
/*
 * definitions
 */
#define TS_SDP_IOCB_KEY_INVALID 0xffffffff /* invalid IOCB key */

#define TS_SDP_IOCB_READ  0x01
#define TS_SDP_IOCB_WRITE 0x02
/*
 * IOCB flags.
 */
#define TS_SDP_IOCB_F_BUFF   0x00000001 /* IOCB must be sent buffered */
#define TS_SDP_IOCB_F_CANCEL 0x00000002 /* IOCB has a pending cancel. */
#define TS_SDP_IOCB_F_ACTIVE 0x00000004 /* IOCB has an active operation */
#define TS_SDP_IOCB_F_QUEUED 0x00000008 /* IOCB is queued for transmission */
#define TS_SDP_IOCB_F_RDMA_R 0x00000010 /* IOCB is in RDMA read processing */
#define TS_SDP_IOCB_F_RDMA_W 0x00000020 /* IOCB is in RDMA write processing */
#define TS_SDP_IOCB_F_ALL    0xFFFFFFFF /* IOCB all mask */
/*
 * zcopy constants.
 */
#define TS_SDP_IOCB_SIZE_MAX  (128*1024) /* matches AIO max kvec size. */
#define TS_SDP_IOCB_PAGES_MAX (TS_SDP_IOCB_SIZE_MAX/PAGE_SIZE)
/*
 * make size a macro.
 */
#define tsSdpConnIocbTableSize(table) ((table)->size)

/* ----------------------------------------------------------------------- */
/* INET read/write IOCBs                                                   */
/* ----------------------------------------------------------------------- */
/*
 * save a kvec read/write for processing once data shows up.
 */
struct tSDP_IOCB_STRUCT {
  tSDP_IOCB                  next;      /* next structure in table */
  tSDP_IOCB                  prev;      /* previous structure in table */
  tUINT32                    type;      /* element type. (for generic queue) */
  tSDP_IOCB_TABLE            table;     /* table to which this iocb belongs */
  tSDP_GENERIC_DESTRUCT_FUNC release; /* release the object */
  /*
   * iocb sepcific
   */
  tUINT32          flags;     /* usage flags */
  /*
   * iocb information
   */
  tINT32           len;  /* space left in the user buffer */
  tINT32           size; /* total size of the user buffer */
  tINT32           post; /* amount of data requested so far. */
  tUINT32          wrid; /* work request completing this IOCB */
  tUINT32          key;  /* matches kiocb key for lookups */
  /*
   * IB specific information for zcopy.
   */
  tTS_IB_FMR_HANDLE mem;     /* memory region handle */
  tTS_IB_LKEY       l_key;   /* local access key */
  tTS_IB_RKEY       r_key;   /* remote access key */
  tUINT64           io_addr; /* virtual IO address */
  /*
   * page list.
   */
  tUINT64 *page_array;  /* list of physical pages. */
  tINT32   page_count;  /* number of physical pages. */
  tINT32   page_offset; /* offset into first page. */
  /*
   * AIO extension specific
   */
#ifdef _TS_SDP_AIO_SUPPORT
  kvec_cb_t        cb;
  union {
    struct kvec_dst  dst;
    struct kvec_dst  src;
  } kvec;
  struct kiocb    *req;
#endif
}; /* tSDP_IOCB_STRUCT */
/*
 * table for IOCBs
 */
struct tSDP_IOCB_TABLE_STRUCT {
  tSDP_IOCB head;  /* double linked list of IOCBs */
  tINT32    size;  /* current number of IOCBs in table */
}; /* tSDP_IOCB_TABLE_STRUCT */

/* ----------------------------------------------------------------------- */
/*                                                                         */
/* Address translations                                                    */
/*                                                                         */
/* ----------------------------------------------------------------------- */
/*
 * Get the phisical address of a 'struct page'
 */
#if defined(__i386__) && \
    (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,9)) && \
    defined(CONFIG_HIGHMEM64G_HIGHPTE)
/*
 * Work around RH AS 2.1 bug (see bugzilla.redhat.com bug 107336)
 */
# define TS_SDP_IOCB_PAGE_TO_PHYSICAL(page) \
         ((tUINT64) ((page) - mem_map) << PAGE_SHIFT)
#else
/*
 * normal page_to_phys() is fine
 */
# define TS_SDP_IOCB_PAGE_TO_PHYSICAL(page) \
         page_to_phys(page)
#endif

#ifdef _TS_SDP_AIO_SUPPORT
/*
 * Everything else or RedHat Advanced Server 2.1/Enterprise Linux 3
 */
# ifdef pte_offset
#  define TS_SDP_GET_FIXMAP_PTE(vaddr) \
        pte_offset(pmd_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr))
# else
#  define TS_SDP_GET_FIXMAP_PTE(vaddr) \
        pte_offset_map(pmd_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr))
# endif

# ifdef _TS_SDP_AIO_SUPPORT
#  define TS_SDP_KM_PAGE KM_IRQ0
# else
#  define TS_SDP_KM_PAGE KM_BH_IRQ
# endif
/*
 * Temporary hacks for the RedHat Enterprise Linux 3 release, and
 * the 2.4.19 AIO kernel, to workaround some differences until a
 * better solution is developed.
 */
# define ts_aio_put_req(x) aio_put_req(x)

# if defined(TS_host_i386_2_4_19_aio)
#  define _TS_AIO_UNUSED_CANCEL_PARAM
#  define _TS_FILE_OP_TABLE_ADDR 0
# else    /* TS_host_i386_2_4_19_aio */
#  define _TS_AIO_UNUSED_CANCEL_PARAM ,struct io_event *ev
/*
 * total hack for a single specific kernel, until redhat
 * exports the symbol. Beware!
 * _TS_FILE_OP_TABLE_ADDR is address of socket_file_ops symbol
 * To get the correct address, grep socket_file_ops /boot/System.map-<KERNEL>.
 */
#  if defined(TS_host_i386_smp_2_4_21_1_b1_rhel)
typedef void (* _ts_aio_put_req_func)(struct kiocb *iocb);
#   undef  ts_aio_put_req
#   define ts_aio_put_req ((_ts_aio_put_req_func)0x02187240)
#   define _TS_FILE_OP_TABLE_ADDR 0
#  elif defined(TS_host_i386_smp_2_4_21_4_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xc03e53c0)
#  elif defined(TS_host_i386_hugemem_2_4_21_4_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0x023e03c0)
#  elif defined(TS_host_i386_smp_2_4_21_4_0_1_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xc03e53c0)
#  elif defined(TS_host_i386_hugemem_2_4_21_4_0_1_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0x023e03c0)
#  elif defined(TS_host_i386_smp_2_4_21_9_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xc03e5620)
#  elif defined(TS_host_i386_hugemem_2_4_21_9_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0x023e0620)
#  elif defined(TS_host_i386_smp_2_4_21_9_0_1_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xc03e5620)
#  elif defined(TS_host_i386_hugemem_2_4_21_9_0_1_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0x023e0620)
#  elif defined(TS_host_amd64_smp_2_4_21_4_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xffffffff804ad020)
#  elif defined(TS_host_amd64_smp_2_4_21_9_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xffffffff804b2a60)
#  elif defined(TS_host_amd64_smp_2_4_21_9_0_1_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xffffffff804b2aa0)
#  elif defined(TS_host_ia64_smp_2_4_21_4_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xe000000004b5d2b8)
#  elif defined(TS_host_ia64_smp_2_4_21_9_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xe000000004b61758)
#  elif defined(TS_host_ia64_smp_2_4_21_9_0_1_EL_rhel)
#   define _TS_FILE_OP_TABLE_ADDR (0xe000000004b61758)
#  elif !defined(IN_TREE_BUILD)
#   error "AIO function aio_put_req not defined"
#  endif
# endif /* TS_host_i386_2_4_19_aio */

# ifdef CONFIG_HIGHMEM
extern pte_t   *__sdp_pte;
extern pgprot_t __sdp_pgprot;
/*
 * initialize region we're going to use.
 */
/* =========================================================== */
/*..__tsSdpKmapInit -- init map in kernel space */
static __inline__ void __tsSdpKmapInit
(
 void
)
{
#if defined(__i386__)
  unsigned long __sdp_vstart;

  __sdp_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
  __sdp_pte    = TS_SDP_GET_FIXMAP_PTE(__sdp_vstart);
  __sdp_pgprot = PAGE_KERNEL;
#endif
} /*__ tsSdpKmapInit */
/*
 * our own kmap functions which check for IRQ/process context.
 */
/* =========================================================== */
/*..__tsSdpKmap -- map a page into kernel space */
static __inline__ tPTR __tsSdpKmap
(
 struct page *page
)
{
  tPTR   vaddr;
#if defined(__i386__)
  tINT32 index;

  if (page < highmem_start_page) {

    vaddr = page_address(page);
  } /* if */
  else {

    if (in_interrupt()) {
      /*
       * There are reserved pages at the end of kernel virtual memory
       * for, amoung other things, mapping pages into the kernel. Since
       * this is in an interrupt there should not be contention for
       * the page we want. (TS_SDP_KM_PAGE)
       */
      index = (KM_TYPE_NR * smp_processor_id()) + TS_SDP_KM_PAGE;
      vaddr = (tPTR)__fix_to_virt(FIX_KMAP_BEGIN + index);
      /*
       * update the page table entry (pte) for the kmap region, with
       * the new mapping, flush if needed
       */
      if (pte_val(*(__sdp_pte-index)) !=
	  pte_val(mk_pte(page, __sdp_pgprot))) {

	set_pte((__sdp_pte - index), mk_pte(page, __sdp_pgprot));
	__flush_tlb_one(vaddr);
      }
    } /* if */
    else {

#ifdef kmap /* RedHat Enterprise Linux 3 */
      vaddr = kmap_high(page, 0);
#else /* 2.4.19 AIO */
      vaddr = kmap_high(page);
#endif
    } /* else */
  } /* else */

#else   /* __i386 */
  vaddr = page_address(page);

#endif  /* __i386__ */

  return vaddr;
} /* __tsSdpKmap */

/* =========================================================== */
/*..__tsSdpKunmap -- unmap a page into kernel space */
static __inline__ void __tsSdpKunmap
(
 struct page *page
)
{
#if defined(__i386__)
  if (!(page < highmem_start_page)) {

    if (in_interrupt()) {
#if 1
      tINT32 index;
      /*
       * This isn't necessary, but it will catch bug if someone tries
       * to use an area which has been unmapped.
       */
      index = (KM_TYPE_NR * smp_processor_id()) + TS_SDP_KM_PAGE;

      pte_clear((__sdp_pte - index));
      __flush_tlb_one(page);
#endif
    } /* if */
    else {

      kunmap_high(page);
    } /* else */
  } /* if */

#endif  /* __i386__* */

  return;
} /* __tsSdpKunmap */

/*
 * When HIGHMEM configuration is turned on k(un)map_atomic uses
 * symbols which are not exported. Since our map/unmap are not
 * happening in IRQ context, these replacment macros are OK.
 */
#define TS_SDP_KVEC_DST_MAP(Xdst)                                       \
        do {                                                            \
                struct kvec_dst *_dst = (Xdst);                         \
                struct kveclet *_let = _dst->let;                       \
                _dst->dst = _dst->addr = __tsSdpKmap(_let->page);       \
                _dst->dst += _let->offset + _dst->offset;               \
                _dst->space = _let->length - _dst->offset;              \
                _dst->offset = 0;                                       \
        } while(0)

#define TS_SDP_KVEC_DST_UNMAP(Xdst)                                     \
        do {                                                            \
                struct kvec_dst *_dst = (Xdst);                         \
                __tsSdpKunmap(_dst->let->page);                         \
                _dst->offset = _dst->dst - _dst->addr;                  \
                _dst->offset -= _dst->let->offset;                      \
                _dst->addr = NULL;                                      \
        } while(0)
# else
# define __tsSdpKmapInit()        do {} while (0)
# define __tsSdpKmap              page_address
# define __tsSdpKunmap(x)         do {} while (0)
# define TS_SDP_KVEC_DST_MAP(x)   do {} while (0)
# define TS_SDP_KVEC_DST_UNMAP(x) do {} while (0)
# endif /* CONFIG_HIGHMEM */
#else /* !_TS_SDP_AIO_SUPPORT */
# define __tsSdpKmapInit()        do {} while (0)
# define __tsSdpKmap(x)           do {} while (0)
# define __tsSdpKunmap(x)         do {} while (0)
# define TS_SDP_KVEC_DST_MAP(x)   do {} while (0)
# define TS_SDP_KVEC_DST_UNMAP(x) do {} while (0)
#endif /* _TS_SDP_AIO_SUPPORT */

#endif /* _TS_SDP_IOCB_H */
