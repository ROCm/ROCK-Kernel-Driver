/*
 * $Id: prep_pci.c,v 1.40 1999/09/17 17:23:05 cort Exp $
 * PReP pci functions.
 * Originally by Gary Thomas
 * rewritten and updated by Cort Dougan (cort@cs.nmt.edu)
 *
 * The motherboard routes/maps will disappear shortly. -- Cort
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/openpic.h>

#include <asm/init.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/machdep.h>

#include "pci.h"

#define MAX_DEVNR 22

/* Which PCI interrupt line does a given device [slot] use? */
/* Note: This really should be two dimensional based in slot/pin used */
unsigned char *Motherboard_map;
unsigned char *Motherboard_map_name;

/* How is the 82378 PIRQ mapping setup? */
unsigned char *Motherboard_routes;

/* Used for Motorola to store system config register */
static unsigned long	*ProcInfo;

extern int chrp_get_irq(struct pt_regs *);
extern void chrp_post_irq(struct pt_regs* regs, int);

/* Tables for known hardware */   

/* Motorola PowerStackII - Utah */
static char Utah_pci_IRQ_map[23] __prepdata =
{
        0,   /* Slot 0  - unused */
        0,   /* Slot 1  - unused */
        5,   /* Slot 2  - SCSI - NCR825A  */
        0,   /* Slot 3  - unused */
        1,   /* Slot 4  - Ethernet - DEC2114x */
        0,   /* Slot 5  - unused */
        3,   /* Slot 6  - PCI Card slot #1 */
        4,   /* Slot 7  - PCI Card slot #2 */
        5,   /* Slot 8  - PCI Card slot #3 */
        5,   /* Slot 9  - PCI Bridge */
             /* added here in case we ever support PCI bridges */
             /* Secondary PCI bus cards are at slot-9,6 & slot-9,7 */
        0,   /* Slot 10 - unused */
        0,   /* Slot 11 - unused */
        5,   /* Slot 12 - SCSI - NCR825A */
        0,   /* Slot 13 - unused */
        3,   /* Slot 14 - enet */
        0,   /* Slot 15 - unused */
        2,   /* Slot 16 - unused */
        3,   /* Slot 17 - unused */
        5,   /* Slot 18 - unused */
        0,   /* Slot 19 - unused */
        0,   /* Slot 20 - unused */
        0,   /* Slot 21 - unused */
        0,   /* Slot 22 - unused */
};

static char Utah_pci_IRQ_routes[] __prepdata =
{
        0,   /* Line 0 - Unused */
        9,   /* Line 1 */
	10,  /* Line 2 */
        11,  /* Line 3 */
        14,  /* Line 4 */
        15,  /* Line 5 */
};

/* Motorola PowerStackII - Omaha */
/* no integrated SCSI or ethernet */
static char Omaha_pci_IRQ_map[23] __prepdata =
{
        0,   /* Slot 0  - unused */
        0,   /* Slot 1  - unused */
        3,   /* Slot 2  - Winbond EIDE */
        0,   /* Slot 3  - unused */
        0,   /* Slot 4  - unused */
        0,   /* Slot 5  - unused */
        1,   /* Slot 6  - PCI slot 1 */
        2,   /* Slot 7  - PCI slot 2  */
        3,   /* Slot 8  - PCI slot 3 */
        4,   /* Slot 9  - PCI slot 4 */ /* needs indirect access */
        0,   /* Slot 10 - unused */
        0,   /* Slot 11 - unused */
        0,   /* Slot 12 - unused */
        0,   /* Slot 13 - unused */
        0,   /* Slot 14 - unused */
        0,   /* Slot 15 - unused */
        1,   /* Slot 16  - PCI slot 1 */
        2,   /* Slot 17  - PCI slot 2  */
        3,   /* Slot 18  - PCI slot 3 */
        4,   /* Slot 19  - PCI slot 4 */ /* needs indirect access */
        0,
        0,
        0,
};

static char Omaha_pci_IRQ_routes[] __prepdata =
{
        0,   /* Line 0 - Unused */
        9,   /* Line 1 */
        11,  /* Line 2 */
        14,  /* Line 3 */
        15   /* Line 4 */
};

/* Motorola PowerStack */
static char Blackhawk_pci_IRQ_map[19] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
 	1,	/* Slot P7 */
 	2,	/* Slot P6 */
 	3,	/* Slot P5 */
};

static char Blackhawk_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	9,	/* Line 1 */
   	11,	/* Line 2 */
   	15,	/* Line 3 */
   	15	/* Line 4 */
};
   
/* Motorola Mesquite */
static char Mesquite_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - unxued */
	0,	/* Slot 11 - unused */
	0,	/* Slot 12 - unused */
	0,	/* Slot 13 - unused */
	2,	/* Slot 14 - Ethernet */
	0,	/* Slot 15 - unused */
	3,	/* Slot 16 - PMC */
	0,	/* Slot 17 - unused */
	0,	/* Slot 18 - unused */
	0,	/* Slot 19 - unused */
	0,	/* Slot 20 - unused */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola Sitka */
static char Sitka_pci_IRQ_map[21] __prepdata =
{
	0,      /* Slot 0  - unused */
	0,      /* Slot 1  - unused */
	0,      /* Slot 2  - unused */
	0,      /* Slot 3  - unused */
	0,      /* Slot 4  - unused */
	0,      /* Slot 5  - unused */
	0,      /* Slot 6  - unused */
	0,      /* Slot 7  - unused */
	0,      /* Slot 8  - unused */
	0,      /* Slot 9  - unused */
	0,      /* Slot 10 - unxued */
	0,      /* Slot 11 - unused */
	0,      /* Slot 12 - unused */
	0,      /* Slot 13 - unused */
	2,      /* Slot 14 - Ethernet */
	0,      /* Slot 15 - unused */
	9,      /* Slot 16 - PMC 1  */
	12,     /* Slot 17 - PMC 2  */
	0,      /* Slot 18 - unused */
	0,      /* Slot 19 - unused */
	4,      /* Slot 20 - NT P2P bridge */
};

/* Motorola MTX */
static char MTX_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - unused */
	3,	/* Slot 12 - SCSI */
	0,	/* Slot 13 - unused */
	2,	/* Slot 14 - Ethernet */
	0,	/* Slot 15 - unused */
	9,      /* Slot 16 - PCI/PMC slot 1 */
	10,     /* Slot 17 - PCI/PMC slot 2 */
	11,     /* Slot 18 - PCI slot 3 */
	0,	/* Slot 19 - unused */
	0,	/* Slot 20 - unused */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola MTX Plus */
/* Secondary bus interrupt routing is not supported yet */
static char MTXplus_pci_IRQ_map[23] __prepdata =
{
        0,      /* Slot 0  - unused */
        0,      /* Slot 1  - unused */
        0,      /* Slot 2  - unused */
        0,      /* Slot 3  - unused */
        0,      /* Slot 4  - unused */
        0,      /* Slot 5  - unused */
        0,      /* Slot 6  - unused */
        0,      /* Slot 7  - unused */
        0,      /* Slot 8  - unused */
        0,      /* Slot 9  - unused */
        0,      /* Slot 10 - unused */
        0,      /* Slot 11 - unused */
        3,      /* Slot 12 - SCSI */
        0,      /* Slot 13 - unused */
        2,      /* Slot 14 - Ethernet 1 */
        0,      /* Slot 15 - unused */
        9,      /* Slot 16 - PCI slot 1P */
        10,     /* Slot 17 - PCI slot 2P */
        11,     /* Slot 18 - PCI slot 3P */
        10,     /* Slot 19 - Ethernet 2 */
        0,      /* Slot 20 - P2P Bridge */
        0,      /* Slot 21 - unused */
        0,      /* Slot 22 - unused */
};

static char Raven_pci_IRQ_routes[] __prepdata =
{
   	0,	/* This is a dummy structure */
};
   
/* Motorola MVME16xx */
static char Genesis_pci_IRQ_map[16] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
};

static char Genesis_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};
   
static char Genesis2_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - Ethernet */
	0,	/* Slot 11 - Universe PCI - VME Bridge */
	3,	/* Slot 12 - unused */
	0,	/* Slot 13 - unused */
	2,	/* Slot 14 - SCSI */
	0,	/* Slot 15 - graphics on 3600 */
	9,	/* Slot 16 - PMC */
	12,	/* Slot 17 - pci */
	11,	/* Slot 18 - pci */
	10,	/* Slot 19 - pci */
	0,	/* Slot 20 - pci */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola Series-E */
static char Comet_pci_IRQ_map[23] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
	1,	/* Slot 16 - PCI slot 1 */
	2,	/* Slot 17 - PCI slot 2 */
	3,	/* Slot 18 - PCI slot 3 */
	4,	/* Slot 19 - PCI bridge */
	0,
	0,
	0,
};

static char Comet_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};

/* Motorola Series-EX */
static char Comet2_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	3,	/* Slot 2  - SCSI - NCR825A */
	0,	/* Slot 3  - unused */
	1,	/* Slot 4  - Ethernet - DEC2104X */
	0,	/* Slot 5  - unused */
	1,	/* Slot 6  - PCI slot 1 */
	2,	/* Slot 7  - PCI slot 2 */
	3,	/* Slot 8  - PCI slot 3 */
	4,	/* Slot 9  - PCI bridge  */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - unused */
	3,	/* Slot 12 - SCSI - NCR825A */
	0,	/* Slot 13 - unused */
	1,	/* Slot 14 - Ethernet - DEC2104X */
	0,	/* Slot 15 - unused */
	1,	/* Slot 16 - PCI slot 1 */
	2,	/* Slot 17 - PCI slot 2 */
	3,	/* Slot 18 - PCI slot 3 */
	4,	/* Slot 19 - PCI bridge */
	0,
	0,
	0,
};

static char Comet2_pci_IRQ_routes[] __prepdata =
{
	0,	/* Line 0 - Unused */
	10,	/* Line 1 */
	11,	/* Line 2 */
	14,	/* Line 3 */
	15,	/* Line 4 */
};

/*
 * ibm 830 (and 850?).
 * This is actually based on the Carolina motherboard
 * -- Cort
 */
static char ibm8xx_pci_IRQ_map[23] __prepdata = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - FireCoral */
        4, /* Slot 12 - Ethernet  PCIINTD# */
        2, /* Slot 13 - PCI Slot #2 */
        2, /* Slot 14 - S3 Video PCIINTD# */
        0, /* Slot 15 - onboard SCSI (INDI) [1] */
        3, /* Slot 16 - NCR58C810 RS6000 Only PCIINTC# */
        0, /* Slot 17 - unused */
        2, /* Slot 18 - PCI Slot 2 PCIINTx# (See below) */
        0, /* Slot 19 - unused */
        0, /* Slot 20 - unused */
        0, /* Slot 21 - unused */
        2, /* Slot 22 - PCI slot 1 PCIINTx# (See below) */
};

static char ibm8xx_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        13,     /* Line 1 */
        10,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};

/*
 * a 6015 ibm board
 * -- Cort
 */
static char ibm6015_pci_IRQ_map[23] __prepdata = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 -  */
        1, /* Slot 12 - SCSI */
        2, /* Slot 13 -  */
        2, /* Slot 14 -  */
        1, /* Slot 15 -  */
        1, /* Slot 16 -  */
        0, /* Slot 17 -  */
        2, /* Slot 18 -  */
        0, /* Slot 19 -  */
        0, /* Slot 20 -  */
        0, /* Slot 21 -  */
        2, /* Slot 22 -  */
};
static char ibm6015_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        13,     /* Line 1 */
        10,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};


/* IBM Nobis and 850 */
static char Nobis_pci_IRQ_map[23] __prepdata ={
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - unused */
        3, /* Slot 12 - SCSI */
        0, /* Slot 13 - unused */
        0, /* Slot 14 - unused */
        0, /* Slot 15 - unused */
};

static char Nobis_pci_IRQ_routes[] __prepdata = {
        0, /* Line 0 - Unused */
        13, /* Line 1 */
        13, /* Line 2 */
        13, /* Line 3 */
        13      /* Line 4 */
};

/* We have to turn on LEVEL mode for changed IRQ's */
/* All PCI IRQ's need to be level mode, so this should be something
 * other than hard-coded as well... IRQ's are individually mappable
 * to either edge or level.
 */
#define CAROLINA_IRQ_EDGE_MASK_LO   0x00  /* IRQ's 0-7  */
#define CAROLINA_IRQ_EDGE_MASK_HI   0xA4  /* IRQ's 8-15 [10,13,15] */

/*
 * 8259 edge/level control definitions
 */
#define ISA8259_M_ELCR 0x4d0
#define ISA8259_S_ELCR 0x4d1

#define ELCRS_INT15_LVL         0x80
#define ELCRS_INT14_LVL         0x40
#define ELCRS_INT12_LVL         0x10
#define ELCRS_INT11_LVL         0x08
#define ELCRS_INT10_LVL         0x04
#define ELCRS_INT9_LVL          0x02
#define ELCRS_INT8_LVL          0x01
#define ELCRM_INT7_LVL          0x80
#define ELCRM_INT5_LVL          0x20

#define CFGPTR(dev) (0x80800000 | (1<<(dev>>3)) | ((dev&7)<<8) | offset)
#define DEVNO(dev)  (dev>>3)                                  

__prep
int
prep_pcibios_read_config_dword (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned int *val)
{
	unsigned long _val;                                          
	unsigned long *ptr;

	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{                   
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;    
	} else                                                                
	{
		ptr = (unsigned long *)CFGPTR(dev);
		_val = le32_to_cpu(*ptr);
	}
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

__prep
int
prep_pcibios_read_config_word (unsigned char bus,
			  unsigned char dev, unsigned char offset, unsigned short *val)
{
	unsigned short _val;                                          
	unsigned short *ptr;

	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{                   
		*val = 0xFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;    
	} else                                                                
	{
		ptr = (unsigned short *)CFGPTR(dev);
		_val = le16_to_cpu(*ptr);
	}
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

__prep
int
prep_pcibios_read_config_byte (unsigned char bus,
			  unsigned char dev, unsigned char offset, unsigned char *val)
{
	unsigned char _val;                                          
	unsigned char *ptr;

	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{                   
		*val = 0xFF;
		return PCIBIOS_DEVICE_NOT_FOUND;    
	} else                                                                
	{
		ptr = (unsigned char *)CFGPTR(dev);
		_val = *ptr;
	}
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

__prep
int
prep_pcibios_write_config_dword (unsigned char bus,
			    unsigned char dev, unsigned char offset, unsigned int val)
{
	unsigned long _val;
	unsigned long *ptr;

	_val = le32_to_cpu(val);
	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned long *)CFGPTR(dev);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

__prep
int
prep_pcibios_write_config_word (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned short val)
{
	unsigned short _val;
	unsigned short *ptr;

	_val = le16_to_cpu(val);
	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned short *)CFGPTR(dev);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

__prep
int
prep_pcibios_write_config_byte (unsigned char bus,
			   unsigned char dev, unsigned char offset, unsigned char val)
{
	unsigned char _val;
	unsigned char *ptr;

	_val = val;
	if ((bus != 0) || (DEVNO(dev) > MAX_DEVNR))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned char *)CFGPTR(dev);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

#define MOTOROLA_CPUTYPE_REG	0x800
#define MOTOROLA_BASETYPE_REG	0x803
#define MPIC_RAVEN_ID		0x48010000
#define	MPIC_HAWK_ID		0x48030000
#define	MOT_PROC2_BIT		0x800

static u_char mvme2600_openpic_initsenses[] __initdata = {
    1,	/* MVME2600_INT_SIO */
    0,	/* MVME2600_INT_FALCN_ECC_ERR */
    1,	/* MVME2600_INT_PCI_ETHERNET */
    1,	/* MVME2600_INT_PCI_SCSI */
    1,	/* MVME2600_INT_PCI_GRAPHICS */
    1,	/* MVME2600_INT_PCI_VME0 */
    1,	/* MVME2600_INT_PCI_VME1 */
    1,	/* MVME2600_INT_PCI_VME2 */
    1,	/* MVME2600_INT_PCI_VME3 */
    1,	/* MVME2600_INT_PCI_INTA */
    1,	/* MVME2600_INT_PCI_INTB */
    1,	/* MVME2600_INT_PCI_INTC */
    1,	/* MVME2600_INT_PCI_INTD */
    1,	/* MVME2600_INT_LM_SIG0 */
    1,	/* MVME2600_INT_LM_SIG1 */
};

#define MOT_RAVEN_PRESENT	0x1
#define MOT_HAWK_PRESENT	0x2

int prep_keybd_present = 1;
int MotMPIC = 0;

int __init raven_init(void)
{
	unsigned int	devid;
	unsigned int	pci_membase;
	unsigned char	base_mod;

	/* Check to see if the Raven chip exists. */
	if ( _prep_type != _PREP_Motorola) {
		OpenPIC = NULL;
		return 0;
	}

	/* Check to see if this board is a type that might have a Raven. */
	if ((inb(MOTOROLA_CPUTYPE_REG) & 0xF0) != 0xE0) {
		OpenPIC = NULL;
		return 0;
	}

	/* Check the first PCI device to see if it is a Raven. */
	pcibios_read_config_dword(0, 0, PCI_VENDOR_ID, &devid);

	switch (devid & 0xffff0000) {
	case MPIC_RAVEN_ID:
		MotMPIC = MOT_RAVEN_PRESENT;
		break;
	case MPIC_HAWK_ID:
		MotMPIC = MOT_HAWK_PRESENT;
		break;
	default:
		OpenPIC = NULL;
		return 0;
	}


	/* Read the memory base register. */
	pcibios_read_config_dword(0, 0, PCI_BASE_ADDRESS_1, &pci_membase);

	if (pci_membase == 0) {
		OpenPIC = NULL;
		return 0;
	}

	/* Map the Raven MPIC registers to virtual memory. */
	OpenPIC = (struct OpenPIC *)ioremap(pci_membase+0xC0000000, 0x22000);

	OpenPIC_InitSenses = mvme2600_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(mvme2600_openpic_initsenses);

	ppc_md.get_irq = chrp_get_irq;
	ppc_md.post_irq = chrp_post_irq;
	
	/* If raven is present on Motorola store the system config register
	 * for later use.
	 */
	ProcInfo = (unsigned long *)ioremap(0xfef80400, 4);

	/* This is a hack.  If this is a 2300 or 2400 mot board then there is
	 * no keyboard controller and we have to indicate that.
	 */
	base_mod = inb(MOTOROLA_BASETYPE_REG);
	if ((MotMPIC == MOT_HAWK_PRESENT) || (base_mod == 0xF9) ||
	    (base_mod == 0xFA) || (base_mod == 0xE1))
		prep_keybd_present = 0;

	return 1;
}

struct mot_info {
	int		cpu_type;	/* 0x100 mask assumes for Raven and Hawk boards that the level/edge are set */
					/* 0x200 if this board has a Hawk chip. */
	int		base_type;
	int		max_cpu;	/* ored with 0x80 if this board should be checked for multi CPU */
	const char	*name;
	unsigned char	*map;
	unsigned char	*routes;
} mot_info[] = {
	{0x300, 0x00, 0x00, "MVME 2400",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x010, 0x00, 0x00, "Genesis",				Genesis_pci_IRQ_map,	Genesis_pci_IRQ_routes},
	{0x020, 0x00, 0x00, "Powerstack (Series E)",		Comet_pci_IRQ_map,	Comet_pci_IRQ_routes},
	{0x040, 0x00, 0x00, "Blackhawk (Powerstack)",		Blackhawk_pci_IRQ_map,	Blackhawk_pci_IRQ_routes},
	{0x050, 0x00, 0x00, "Omaha (PowerStack II Pro3000)",	Omaha_pci_IRQ_map,	Omaha_pci_IRQ_routes},
	{0x060, 0x00, 0x00, "Utah (Powerstack II Pro4000)",	Utah_pci_IRQ_map,	Utah_pci_IRQ_routes},
	{0x0A0, 0x00, 0x00, "Powerstack (Series EX)",		Comet2_pci_IRQ_map,	Comet2_pci_IRQ_routes},
	{0x1E0, 0xE0, 0x00, "Mesquite cPCI (MCP750)",		Mesquite_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xE1, 0x00, "Sitka cPCI (MCPN750)",		Sitka_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xE2, 0x00, "Mesquite cPCI (MCP750) w/ HAC",	Mesquite_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xF6, 0x80, "MTX Plus",				MTXplus_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xF6, 0x81, "Dual MTX Plus",			MTXplus_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xF7, 0x80, "MTX wo/ Parallel Port",		MTX_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xF7, 0x81, "Dual MTX wo/ Parallel Port",	MTX_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xF8, 0x80, "MTX w/ Parallel Port",		MTX_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xF8, 0x81, "Dual MTX w/ Parallel Port",	MTX_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xF9, 0x00, "MVME 2300",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xFA, 0x00, "MVME 2300SC/2600",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xFB, 0x00, "MVME 2600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xFC, 0x00, "MVME 2600/2700 with MVME761",	Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xFD, 0x80, "MVME 3600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xFD, 0x81, "MVME 4600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xFE, 0x80, "MVME 3600 with MVME761",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xFE, 0x81, "MVME 4600 with MVME761",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x1E0, 0xFF, 0x00, "MVME 1600-001 or 1600-011",	Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes},
	{0x000, 0x00, 0x00, "",					NULL,			NULL}
};

unsigned long __init prep_route_pci_interrupts(void)
{
	unsigned char *ibc_pirq = (unsigned char *)0x80800860;
	unsigned char *ibc_pcicon = (unsigned char *)0x80800840;
	int i;
	
	if ( _prep_type == _PREP_Motorola)
	{
		unsigned short irq_mode;
		unsigned char  cpu_type;
		unsigned char  base_mod;
		int	       entry;
		int	       mot_entry = -1;

		cpu_type = inb(MOTOROLA_CPUTYPE_REG) & 0xF0;
		base_mod = inb(MOTOROLA_BASETYPE_REG);

		for (entry = 0; mot_info[entry].cpu_type != 0; entry++) {
			if (mot_info[entry].cpu_type & 0x200) {		 	/* Check for Hawk chip */
				if (!(MotMPIC & MOT_HAWK_PRESENT))
					continue;
			} else {						/* Check non hawk boards */
				if ((mot_info[entry].cpu_type & 0xff) != cpu_type)
					continue;

				if (mot_info[entry].base_type == 0) {
					mot_entry = entry;
					break;
				}

				if (mot_info[entry].base_type != base_mod)
					continue;
			}

			if (!(mot_info[entry].max_cpu & 0x80)) {
				mot_entry = entry;
				break;
			}

			/* processor 1 not present and max processor zero indicated */
			if ((*ProcInfo & MOT_PROC2_BIT) && !(mot_info[entry].max_cpu & 0x7f)) {
				mot_entry = entry;
				break;
			}

			/* processor 1 present and max processor zero indicated */
			if (!(*ProcInfo & MOT_PROC2_BIT) && (mot_info[entry].max_cpu & 0x7f)) {
				mot_entry = entry;
				break;
			}
		}

		if (mot_entry == -1) 	/* No particular cpu type found - assume Blackhawk */
			mot_entry = 3;

		Motherboard_map_name = (unsigned char *)mot_info[mot_entry].name;
		Motherboard_map = mot_info[mot_entry].map;
		Motherboard_routes = mot_info[mot_entry].routes;

		if (!(mot_info[entry].cpu_type & 0x100)) {
			/* AJF adjust level/edge control according to routes */
			irq_mode = 0;
			for (i = 1;  i <= 4;  i++)
			{
				irq_mode |= ( 1 << Motherboard_routes[i] );
			}
			outb( irq_mode & 0xff, 0x4d0 );
			outb( (irq_mode >> 8) & 0xff, 0x4d1 );
		}
	} else if ( _prep_type == _PREP_IBM )
	{
		unsigned char pl_id;
		/*
		 * my carolina is 0xf0
		 * 6015 has 0xfc
		 * -- Cort
		 */
		printk("IBM ID: %08x\n", inb(0x0852));
		switch(inb(0x0852))
		{
		case 0xff:
			Motherboard_map_name = "IBM 850/860 Portable\n";
			Motherboard_map = Nobis_pci_IRQ_map;
			Motherboard_routes = Nobis_pci_IRQ_routes;
			break;
		case 0xfc:
			Motherboard_map_name = "IBM 6015";
			Motherboard_map = ibm6015_pci_IRQ_map;
			Motherboard_routes = ibm6015_pci_IRQ_routes;
			break;			
		default:
			Motherboard_map_name = "IBM 8xx (Carolina)";
			Motherboard_map = ibm8xx_pci_IRQ_map;
			Motherboard_routes = ibm8xx_pci_IRQ_routes;
			break;
		}

		/*printk("Changing IRQ mode\n");*/
		pl_id=inb(0x04d0);
		/*printk("Low mask is %#0x\n", pl_id);*/
		outb(pl_id|CAROLINA_IRQ_EDGE_MASK_LO, 0x04d0);
		
		pl_id=inb(0x04d1);
		/*printk("Hi mask is  %#0x\n", pl_id);*/
		outb(pl_id|CAROLINA_IRQ_EDGE_MASK_HI, 0x04d1);
		pl_id=inb(0x04d1);
		/*printk("Hi mask now %#0x\n", pl_id);*/
	} else if ( _prep_type == _PREP_Radstone )
	{
		unsigned char ucElcrM, ucElcrS;

		/*
		 * Set up edge/level
		 */
		switch(ucSystemType)
		{
			case RS_SYS_TYPE_PPC1:
			{
				if(ucBoardRevMaj<5)
				{
					ucElcrS=ELCRS_INT15_LVL;
				}
				else
				{
					ucElcrS=ELCRS_INT9_LVL |
					        ELCRS_INT11_LVL |
					        ELCRS_INT14_LVL |
					        ELCRS_INT15_LVL;
				}
				ucElcrM=ELCRM_INT5_LVL | ELCRM_INT7_LVL;
				break;
			}

			case RS_SYS_TYPE_PPC1a:
			{
				ucElcrS=ELCRS_INT9_LVL |
				        ELCRS_INT11_LVL |
				        ELCRS_INT14_LVL |
				        ELCRS_INT15_LVL;
				ucElcrM=ELCRM_INT5_LVL;
				break;
			}

			case RS_SYS_TYPE_PPC2:
			case RS_SYS_TYPE_PPC2a:
			case RS_SYS_TYPE_PPC2ep:
			case RS_SYS_TYPE_PPC4:
			case RS_SYS_TYPE_PPC4a:
			default:
			{
				ucElcrS=ELCRS_INT9_LVL |
				        ELCRS_INT10_LVL |
				        ELCRS_INT11_LVL |
				        ELCRS_INT14_LVL |
				        ELCRS_INT15_LVL;
				ucElcrM=ELCRM_INT5_LVL |
				        ELCRM_INT7_LVL;
				break;
			}
		}

		/*
		 * Write edge/level selection
		 */
		outb(ucElcrS, ISA8259_S_ELCR);
		outb(ucElcrM, ISA8259_M_ELCR);

		/*
		 * Radstone boards have PCI interrupts all set up
		 * so leave well alone
		 */
		return 0;
	} else
	{
		printk("No known machine pci routing!\n");
		return -1;
	}
	
	/* Set up mapping from slots */
	for (i = 1;  i <= 4;  i++)
	{
		ibc_pirq[i-1] = Motherboard_routes[i];
	}
	/* Enable PCI interrupts */
	*ibc_pcicon |= 0x20;
	return 0;
}

void __init
prep_pcibios_fixup(void)
{
        struct pci_dev *dev;
        extern unsigned char *Motherboard_map;
        extern unsigned char *Motherboard_routes;
        unsigned char i;

        if ( _prep_type == _PREP_Radstone )
        {
                printk("Radstone boards require no PCI fixups\n");
		return;
        }

	prep_route_pci_interrupts();

	printk("Setting PCI interrupts for a \"%s\"\n", Motherboard_map_name);
	if (OpenPIC) {
		/* PCI interrupts are controlled by the OpenPIC */
		pci_for_each_dev(dev) {
			if (dev->bus->number == 0) {
                       		dev->irq = openpic_to_irq(Motherboard_map[PCI_SLOT(dev->devfn)]);
				pcibios_write_config_byte(dev->bus->number, dev->devfn, PCI_INTERRUPT_PIN, dev->irq);
			}
		}
		return;
	}

	pci_for_each_dev(dev) {
		/*
		 * Use our old hard-coded kludge to figure out what
		 * irq this device uses.  This is necessary on things
		 * without residual data. -- Cort
		 */
		unsigned char d = PCI_SLOT(dev->devfn);
		dev->irq = Motherboard_routes[Motherboard_map[d]];

		for ( i = 0 ; i <= 5 ; i++ )
		{
		        if ( dev->resource[i].start > 0x10000000 )
		        {
		                printk("Relocating PCI address %lx -> %lx\n",
		                       dev->resource[i].start,
		                       (dev->resource[i].start & 0x00FFFFFF)
		                       | 0x01000000);
		                dev->resource[i].start =
		                  (dev->resource[i].start & 0x00FFFFFF) | 0x01000000;
		                pci_write_config_dword(dev,
		                        PCI_BASE_ADDRESS_0+(i*0x4),
		                       dev->resource[i].start );
		        }
		}
#if 0
		/*
		 * If we have residual data and if it knows about this
		 * device ask it what the irq is.
		 *  -- Cort
		 */
		ppcd = residual_find_device_id( ~0L, dev->device,
		                                -1,-1,-1, 0);
#endif
	}
}

decl_config_access_method(indirect);

void __init
prep_setup_pci_ptrs(void)
{
	PPC_DEVICE *hostbridge;

        printk("PReP architecture\n");
        if ( _prep_type == _PREP_Radstone )
        {
		pci_config_address = (unsigned *)0x80000cf8;
		pci_config_data = (char *)0x80000cfc;
		set_config_access_method(indirect);		
        }
        else
        {
                hostbridge = residual_find_device(PROCESSORDEVICE, NULL,
		       BridgeController, PCIBridge, -1, 0);
                if (hostbridge &&
                    hostbridge->DeviceId.Interface == PCIBridgeIndirect) {
                        PnP_TAG_PACKET * pkt;
                        set_config_access_method(indirect);
                        pkt = PnP_find_large_vendor_packet(
				res->DevicePnPHeap+hostbridge->AllocatedOffset,
				3, 0);
                        if(pkt)
			{
#define p pkt->L4_Pack.L4_Data.L4_PPCPack
                                pci_config_address= (unsigned *)ld_le32((unsigned *) p.PPCData);
				pci_config_data= (unsigned char *)ld_le32((unsigned *) (p.PPCData+8));
                        }
			else
			{
                                pci_config_address= (unsigned *) 0x80000cf8;
                                pci_config_data= (unsigned char *) 0x80000cfc;
                        }
                }
		else
		{
                        set_config_access_method(prep);
                }

        }

	ppc_md.pcibios_fixup = prep_pcibios_fixup;
}

