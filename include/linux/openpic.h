/*
 *  linux/openpic.h -- OpenPIC definitions
 *
 *  Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is based on the following documentation:
 *
 *	The Open Programmable Interrupt Controller (PIC)
 *	Register Interface Specification Revision 1.2
 *
 *	Issue Date: October 1995
 *
 *	Issued jointly by Advanced Micro Devices and Cyrix Corporation
 *
 *	AMD is a registered trademark of Advanced Micro Devices, Inc.
 *	Copyright (C) 1995, Advanced Micro Devices, Inc. and Cyrix, Inc.
 *	All Rights Reserved.
 *
 *  To receive a copy of this documentation, send an email to openpic@amd.com.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _LINUX_OPENPIC_H
#define _LINUX_OPENPIC_H

#if !defined(__powerpc__) && !defined(__i386__)
#error Unsupported OpenPIC platform
#endif


#ifdef __KERNEL__

    /*
     *  OpenPIC supports up to 2048 interrupt sources and up to 32 processors
     */

#define OPENPIC_MAX_SOURCES	2048
#define OPENPIC_MAX_PROCESSORS	32

#define OPENPIC_NUM_TIMERS	4
#define OPENPIC_NUM_IPI		4
#define OPENPIC_NUM_PRI		16
#define OPENPIC_NUM_VECTORS	256


    /*
     *  Vector numbers
     */

#define OPENPIC_VEC_SOURCE      16    /* and up */
#define OPENPIC_VEC_TIMER       64    /* and up */
#define OPENPIC_VEC_IPI         72    /* and up */
#define OPENPIC_VEC_SPURIOUS    127


    /*
     *  OpenPIC Registers are 32 bits and aligned on 128 bit boundaries
     */

typedef struct _OpenPIC_Reg {
    u_int Reg;					/* Little endian! */
    char Pad[0xc];
} OpenPIC_Reg;


    /*
     *  Per Processor Registers
     */

typedef struct _OpenPIC_Processor {
    /*
     *  Private Shadow Registers (for SLiC backwards compatibility)
     */
    u_int IPI0_Dispatch_Shadow;			/* Write Only */
    char Pad1[0x4];
    u_int IPI0_Vector_Priority_Shadow;		/* Read/Write */
    char Pad2[0x34];
    /*
     *  Interprocessor Interrupt Command Ports
     */
    OpenPIC_Reg _IPI_Dispatch[OPENPIC_NUM_IPI];	/* Write Only */
    /*
     *  Current Task Priority Register
     */
    OpenPIC_Reg _Current_Task_Priority;		/* Read/Write */
#ifndef __powerpc__
    /*
     *  Who Am I Register
     */
    OpenPIC_Reg _Who_Am_I;			/* Read Only */
#else
    char Pad3[0x10];
#endif
#ifndef __i386__
    /*
     *  Interrupt Acknowledge Register
     */
    OpenPIC_Reg _Interrupt_Acknowledge;		/* Read Only */
#else
    char Pad4[0x10];
#endif
    /*
     *  End of Interrupt (EOI) Register
     */
    OpenPIC_Reg _EOI;				/* Read/Write */
    char Pad5[0xf40];
} OpenPIC_Processor;


    /*
     *  Timer Registers
     */

typedef struct _OpenPIC_Timer {
    OpenPIC_Reg _Current_Count;			/* Read Only */
    OpenPIC_Reg _Base_Count;			/* Read/Write */
    OpenPIC_Reg _Vector_Priority;		/* Read/Write */
    OpenPIC_Reg _Destination;			/* Read/Write */
} OpenPIC_Timer;


    /*
     *  Global Registers
     */

typedef struct _OpenPIC_Global {
    /*
     *  Feature Reporting Registers
     */
    OpenPIC_Reg _Feature_Reporting0;		/* Read Only */
    OpenPIC_Reg _Feature_Reporting1;		/* Future Expansion */
    /*
     *  Global Configuration Registers
     */
    OpenPIC_Reg _Global_Configuration0;		/* Read/Write */
    OpenPIC_Reg _Global_Configuration1;		/* Future Expansion */
    /*
     *  Vendor Specific Registers
     */
    OpenPIC_Reg _Vendor_Specific[4];
    /*
     *  Vendor Identification Register
     */
    OpenPIC_Reg _Vendor_Identification;		/* Read Only */
    /*
     *  Processor Initialization Register
     */
    OpenPIC_Reg _Processor_Initialization;	/* Read/Write */
    /*
     *  IPI Vector/Priority Registers
     */
    OpenPIC_Reg _IPI_Vector_Priority[OPENPIC_NUM_IPI];	/* Read/Write */
    /*
     *  Spurious Vector Register
     */
    OpenPIC_Reg _Spurious_Vector;		/* Read/Write */
    /*
     *  Global Timer Registers
     */
    OpenPIC_Reg _Timer_Frequency;		/* Read/Write */
    OpenPIC_Timer Timer[OPENPIC_NUM_TIMERS];
    char Pad1[0xee00];
} OpenPIC_Global;


    /*
     *  Interrupt Source Registers
     */

typedef struct _OpenPIC_Source {
    OpenPIC_Reg _Vector_Priority;		/* Read/Write */
    OpenPIC_Reg _Destination;			/* Read/Write */
} OpenPIC_Source;


    /*
     *  OpenPIC Register Map
     */

struct OpenPIC {
#ifndef __powerpc__
    /*
     *  Per Processor Registers --- Private Access
     */
    OpenPIC_Processor Private;
#else
    char Pad1[0x1000];
#endif
    /*
     *  Global Registers
     */
    OpenPIC_Global Global;
    /*
     *  Interrupt Source Configuration Registers
     */
    OpenPIC_Source Source[OPENPIC_MAX_SOURCES];
    /*
     *  Per Processor Registers
     */
    OpenPIC_Processor Processor[OPENPIC_MAX_PROCESSORS];
};

extern volatile struct OpenPIC *OpenPIC;
extern u_int OpenPIC_NumInitSenses;
extern u_char *OpenPIC_InitSenses;


    /*
     *  Current Task Priority Register
     */

#define OPENPIC_CURRENT_TASK_PRIORITY_MASK	0x0000000f

    /*
     *  Who Am I Register
     */

#define OPENPIC_WHO_AM_I_ID_MASK		0x0000001f

    /*
     *  Feature Reporting Register 0
     */

#define OPENPIC_FEATURE_LAST_SOURCE_MASK	0x07ff0000
#define OPENPIC_FEATURE_LAST_SOURCE_SHIFT	16
#define OPENPIC_FEATURE_LAST_PROCESSOR_MASK	0x00001f00
#define OPENPIC_FEATURE_LAST_PROCESSOR_SHIFT	8
#define OPENPIC_FEATURE_VERSION_MASK		0x000000ff

    /*
     *  Global Configuration Register 0
     */

#define OPENPIC_CONFIG_RESET			0x80000000
#define OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE	0x20000000
#define OPENPIC_CONFIG_BASE_MASK		0x000fffff

    /*
     *  Vendor Identification Register
     */

#define OPENPIC_VENDOR_ID_STEPPING_MASK		0x00ff0000
#define OPENPIC_VENDOR_ID_STEPPING_SHIFT	16
#define OPENPIC_VENDOR_ID_DEVICE_ID_MASK	0x0000ff00
#define OPENPIC_VENDOR_ID_DEVICE_ID_SHIFT	8
#define OPENPIC_VENDOR_ID_VENDOR_ID_MASK	0x000000ff

    /*
     *  Vector/Priority Registers
     */

#define OPENPIC_MASK				0x80000000
#define OPENPIC_ACTIVITY			0x40000000	/* Read Only */
#define OPENPIC_PRIORITY_MASK			0x000f0000
#define OPENPIC_PRIORITY_SHIFT			16
#define OPENPIC_VECTOR_MASK			0x000000ff


    /*
     *  Interrupt Source Registers
     */

#define OPENPIC_POLARITY_POSITIVE		0x00800000
#define OPENPIC_POLARITY_NEGATIVE		0x00000000
#define OPENPIC_POLARITY_MASK			0x00800000
#define OPENPIC_SENSE_LEVEL			0x00400000
#define OPENPIC_SENSE_EDGE			0x00000000
#define OPENPIC_SENSE_MASK			0x00400000


    /*
     *  Timer Registers
     */

#define OPENPIC_COUNT_MASK			0x7fffffff
#define OPENPIC_TIMER_TOGGLE			0x80000000
#define OPENPIC_TIMER_COUNT_INHIBIT		0x80000000


    /*
     *  Aliases to make life simpler
     */

/* Per Processor Registers */
#define IPI_Dispatch(i)			_IPI_Dispatch[i].Reg
#define Current_Task_Priority		_Current_Task_Priority.Reg
#ifndef __powerpc__
#define Who_Am_I			_Who_Am_I.Reg
#endif
#ifndef __i386__
#define Interrupt_Acknowledge		_Interrupt_Acknowledge.Reg
#endif
#define EOI				_EOI.Reg

/* Global Registers */
#define Feature_Reporting0		_Feature_Reporting0.Reg
#define Feature_Reporting1		_Feature_Reporting1.Reg
#define Global_Configuration0		_Global_Configuration0.Reg
#define Global_Configuration1		_Global_Configuration1.Reg
#define Vendor_Specific(i)		_Vendor_Specific[i].Reg
#define Vendor_Identification		_Vendor_Identification.Reg
#define Processor_Initialization	_Processor_Initialization.Reg
#define IPI_Vector_Priority(i)		_IPI_Vector_Priority[i].Reg
#define Spurious_Vector			_Spurious_Vector.Reg
#define Timer_Frequency			_Timer_Frequency.Reg

/* Timer Registers */
#define Current_Count			_Current_Count.Reg
#define Base_Count			_Base_Count.Reg
#define Vector_Priority			_Vector_Priority.Reg
#define Destination			_Destination.Reg

/* Interrupt Source Registers */
#define Vector_Priority			_Vector_Priority.Reg
#define Destination			_Destination.Reg

    /*
     *  OpenPIC Operations
     */

/* Global Operations */
extern void openpic_init(int);
extern void openpic_reset(void);
extern void openpic_enable_8259_pass_through(void);
extern void openpic_disable_8259_pass_through(void);
#ifndef __i386__
extern u_int openpic_irq(u_int cpu);
#endif
#ifndef __powerpc__
extern void openpic_eoi(void);
extern u_int openpic_get_priority(void);
extern void openpic_set_priority(u_int pri);
#else
extern void openpic_eoi(u_int cpu);
extern u_int openpic_get_priority(u_int cpu);
extern void openpic_set_priority(u_int cpu, u_int pri);
#endif
extern u_int openpic_get_spurious(void);
extern void openpic_set_spurious(u_int vector);
extern void openpic_init_processor(u_int cpumask);

/* Interprocessor Interrupts */
extern void openpic_initipi(u_int ipi, u_int pri, u_int vector);
#ifndef __powerpc__
extern void openpic_cause_IPI(u_int ipi, u_int cpumask);
#else
extern void openpic_cause_IPI(u_int cpu, u_int ipi, u_int cpumask);
#endif

/* Timer Interrupts */
extern void openpic_inittimer(u_int timer, u_int pri, u_int vector);
extern void openpic_maptimer(u_int timer, u_int cpumask);

/* Interrupt Sources */
extern void openpic_enable_irq(u_int irq);
extern void openpic_disable_irq(u_int irq);
extern void openpic_initirq(u_int irq, u_int pri, u_int vector, int polarity,
			    int is_level);
extern void openpic_mapirq(u_int irq, u_int cpumask);
extern void openpic_set_sense(u_int irq, int sense);

#endif /* __KERNEL__ */

#endif /* _LINUX_OPENPIC_H */
