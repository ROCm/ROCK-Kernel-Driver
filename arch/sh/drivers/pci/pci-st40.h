/* 
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Defintions for the ST40 PCI hardware.
 */

#ifndef __PCI_ST40_H__
#define __PCI_ST40_H__

#define ST40PCI_VCR_STATUS    0x00

#define ST40PCI_VCR_VERSION   0x08

#define ST40PCI_CR            0x10

#define CR_SOFT_RESET (1<<12)
#define CR_PFCS       (1<<11)
#define CR_PFE        (1<<9)
#define CR_BMAM       (1<<6)
#define CR_HOST       (1<<5)
#define CR_CLKEN      (1<<4)
#define CR_SOCS       (1<<3)
#define CR_IOCS       (1<<2)
#define CR_RSTCTL     (1<<1)
#define CR_CFINT      (1<<0)
#define CR_LOCK_MASK  0x5a000000


#define ST40PCI_LSR0          0X14
#define ST40PCI_LAR0          0x1c

#define ST40PCI_INT           0x24
#define INT_MADIM             (1<<2)


#define ST40PCI_INTM          0x28
#define ST40PCI_AIR           0x2c
#define ST40PCI_CIR           0x30
#define ST40PCI_AINT          0x40
#define ST40PCI_AINTM         0x44
#define ST40PCI_BMIR          0x48
#define ST40PCI_PAR           0x4c
#define ST40PCI_MBR           0x50
#define ST40PCI_IOBR          0x54
#define ST40PCI_PINT          0x58
#define ST40PCI_PINTM         0x5c
#define ST40PCI_MBMR          0x70
#define ST40PCI_IOBMR         0x74
#define ST40PCI_PDR           0x78

/* These are configs space registers */
#define ST40PCI_CSR_VID               0x10000
#define ST40PCI_CSR_DID               0x10002
#define ST40PCI_CSR_CMD               0x10004
#define ST40PCI_CSR_STATUS            0x10006
#define ST40PCI_CSR_MBAR0             0x10010
#define ST40PCI_CSR_TRDY              0x10040
#define ST40PCI_CSR_RETRY             0x10041
#define ST40PCI_CSR_MIT               0x1000d

#define ST40_IO_ADDR 0xb6000000       

#endif /* __PCI_ST40_H__ */
