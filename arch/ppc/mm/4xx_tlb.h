/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: 4xx_tlb.h
 *
 *    Description:
 *      Routines for manipulating the TLB on PowerPC 400-class processors.
 *
 */

#ifndef __4XX_TLB_H__
#define __4XX_TLB_H__


#ifdef __cplusplus
extern "C" {
#endif


/* Function Prototypes */

extern void	 PPC4xx_tlb_pin(unsigned long va, unsigned long pa,
				int pagesz, int cache);
extern void	 PPC4xx_tlb_unpin(unsigned long va, unsigned long pa,
				  int size);
extern void	 PPC4xx_tlb_flush_all(void);
extern void	 PPC4xx_tlb_flush(unsigned long va, int pid);


#ifdef __cplusplus
}
#endif

#endif /* __4XX_TLB_H__ */
