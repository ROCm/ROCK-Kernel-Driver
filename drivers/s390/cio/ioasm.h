#ifndef S390_CIO_IOASM_H
#define S390_CIO_IOASM_H

/*
 * area for channel subsystem call
 */
struct chsc_area {
	struct {
		/* word 0 */
		__u16 command_code1;
		__u16 command_code2;
		union {
			struct {
				/* word 1 */
				__u32 reserved1;
				/* word 2 */
				__u32 reserved2;
			} __attribute__ ((packed,aligned(8))) sei_req;
			struct {
				/* word 1 */
				__u16 reserved1;
				__u16 f_sch;	 /* first subchannel */
				/* word 2 */
				__u16 reserved2;
				__u16 l_sch;	/* last subchannel */
			} __attribute__ ((packed,aligned(8))) ssd_req;
		} request_block_data;
		/* word 3 */
		__u32 reserved3;
	} __attribute__ ((packed,aligned(8))) request_block;
	struct {
		/* word 0 */
		__u16 length;
		__u16 response_code;
		/* word 1 */
		__u32 reserved1;
		union {
			struct {
				/* word 2 */
				__u8  flags;
				__u8  vf;	  /* validity flags */
				__u8  rs;	  /* reporting source */
				__u8  cc;	  /* content code */
				/* word 3 */
				__u16 fla;	  /* full link address */
				__u16 rsid;	  /* reporting source id */
				/* word 4 */
				__u32 reserved2;
				/* word 5 */
				__u32 reserved3;
				/* word 6 */
				__u32 ccdf;	  /* content-code dependent field */
				/* word 7 */
				__u32 reserved4;
				/* word 8 */
				__u32 reserved5;
				/* word 9 */
				__u32 reserved6;
			} __attribute__ ((packed,aligned(8))) sei_res;
			struct {
				/* word 2 */
				__u8 sch_valid : 1;
				__u8 dev_valid : 1;
				__u8 st	       : 3; /* subchannel type */
				__u8 zeroes    : 3;
				__u8  unit_addr;  /* unit address */
				__u16 devno;	  /* device number */
				/* word 3 */
				__u8 path_mask;
				__u8 fla_valid_mask;
				__u16 sch;	  /* subchannel */
				/* words 4-5 */
				__u8 chpid[8];	  /* chpids 0-7 */
				/* words 6-9 */
				__u16 fla[8];	  /* full link addresses 0-7 */
			} __attribute__ ((packed,aligned(8))) ssd_res;
		} response_block_data;
	} __attribute__ ((packed,aligned(8))) response_block;
} __attribute__ ((packed,aligned(PAGE_SIZE)));

/*
 * TPI info structure
 */
struct tpi_info {
	__u32 reserved1	 : 16;	 /* reserved 0x00000001 */
	__u32 irq	 : 16;	 /* aka. subchannel number */
	__u32 intparm;		 /* interruption parameter */
	__u32 adapter_IO : 1;
	__u32 reserved2	 : 1;
	__u32 isc	 : 3;
	__u32 reserved3	 : 12;
	__u32 int_type	 : 3;
	__u32 reserved4	 : 12;
} __attribute__ ((packed));


/*
 * Some S390 specific IO instructions as inline
 */

extern __inline__ int stsch(int irq, volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   stsch 0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000), "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int msch(int irq, volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   msch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L), "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int msch_err(int irq, volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"    lhi  %0,%3\n"
		"    lr	  1,%1\n"
		"    msch 0(%2)\n"
		"0:  ipm  %0\n"
		"    srl  %0,28\n"
		"1:\n"
#ifdef CONFIG_ARCH_S390X
		".section __ex_table,\"a\"\n"
		"   .align 8\n"
		"   .quad 0b,1b\n"
		".previous"
#else
		".section __ex_table,\"a\"\n"
		"   .align 4\n"
		"   .long 0b,1b\n"
		".previous"
#endif
		: "=&d" (ccode)
		: "d" (irq | 0x10000L), "a" (addr), "K" (-EIO)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int tsch(int irq, volatile struct irb *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   tsch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L), "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int tpi( volatile struct tpi_info *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   tpi	  0(%1)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int ssch(int irq, volatile struct orb *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   ssch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L), "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int rsch(int irq)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   rsch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int csch(int irq)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   csch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int hsch(int irq)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   hsch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int xsch(int irq)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   .insn rre,0xb2760000,%1,0\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int chsc(void *chsc_area)
{
	int cc;

	__asm__ __volatile__ (
		".insn	rre,0xb25f0000,%1,0	\n\t"
		"ipm	%0	\n\t"
		"srl	%0,28	\n\t"
		: "=d" (cc)
		: "d" (chsc_area)
		: "cc" );

	return cc;
}

extern __inline__ int iac( void)
{
	int ccode;

	__asm__ __volatile__(
		"   iac	  1\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode) : : "cc", "1" );
	return ccode;
}

extern __inline__ int rchp(int chpid)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   rchp\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (chpid)
		: "cc", "1" );
	return ccode;
}

#endif
