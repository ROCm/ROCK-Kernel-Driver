/* H8MAX board depend header */

/* TIMER rate define */
#ifdef H8300_TIMER_DEFINE
#define H8300_TIMER_COUNT_DATA 25000*10/8192
#define H8300_TIMER_FREQ 25000*1000/8192
#endif

/* H8MAX RTL8019AS Config */
#ifdef H8300_NE_DEFINE

#define NE2000_ADDR		0x800600
#define NE2000_IRQ              4
#define NE2000_IRQ_VECTOR	(12 + NE2000_IRQ)
#define	NE2000_BYTE		volatile unsigned short

#define IER                     0xfee015
#define ISR			0xfee016
#define IRQ_MASK		(1 << NE2000_IRQ)
/* sorry quick hack */
#if defined(outb)
# undef outb
#endif
#define outb(d,a)               h8max_outb((d),(a) - NE2000_ADDR)
#if defined(inb)
# undef inb
#endif
#define inb(a)                  h8max_inb((a) - NE2000_ADDR)
#if defined(outb_p)
# undef outb_p
#endif
#define outb_p(d,a)             h8max_outb((d),(a) - NE2000_ADDR)
#if defined(inb_p)
# undef inb_p
#endif
#define inb_p(a)                h8max_inb((a) - NE2000_ADDR)
#if defined(outsw)
# undef outsw
#endif
#define outsw(a,p,l)            h8max_outsw((a) - NE2000_ADDR,(unsigned short *)p,l)
#if defined(insw)
# undef insw
#endif
#define insw(a,p,l)             h8max_insw((a) - NE2000_ADDR,(unsigned short *)p,l)
#if defined(outsb)
# undef outsb
#endif
#define outsb(a,p,l)            h8max_outsb((a) - NE2000_ADDR,(unsigned char *)p,l)
#if defined(insb)
# undef insb
#endif
#define insb(a,p,l)             h8max_insb((a) - NE2000_ADDR,(unsigned char *)p,l)

#define H8300_INIT_NE()                  \
do {                                     \
	wordlength = 2;                  \
	h8max_outb(0x49, ioaddr + EN0_DCFG); \
	SA_prom[14] = SA_prom[15] = 0x57;\
} while(0)

static inline void h8max_outb(unsigned char d,unsigned char a)
{
	*(unsigned short *)(NE2000_ADDR + (a << 1)) = d;
}

static inline unsigned char h8max_inb(unsigned char a)
{
	return *(unsigned char *)(NE2000_ADDR + (a << 1) +1);
}

static inline void h8max_outsw(unsigned char a,unsigned short *p,unsigned long l)
{
	unsigned short d;
	for (; l != 0; --l, p++) {
		d = (((*p) >> 8) & 0xff) | ((*p) << 8);
		*(unsigned short *)(NE2000_ADDR + (a << 1)) = d;
	}
}

static inline void h8max_insw(unsigned char a,unsigned short *p,unsigned long l)
{
	unsigned short d;
	for (; l != 0; --l, p++) {
		d = *(unsigned short *)(NE2000_ADDR + (a << 1));
		*p = (d << 8)|((d >> 8) & 0xff);
	}
}

static inline void h8max_outsb(unsigned char a,unsigned char *p,unsigned long l)
{
	for (; l != 0; --l, p++) {
		*(unsigned short *)(NE2000_ADDR + (a << 1)) = *p;
	}
}

static inline void h8max_insb(unsigned char a,unsigned char *p,unsigned long l)
{
	for (; l != 0; --l, p++) {
		*p = *((unsigned char *)(NE2000_ADDR + (a << 1))+1);
	}
}

#endif

/* H8MAX IDE I/F Config */
#ifdef H8300_IDE_DEFINE

#define H8300_IDE_BASE 0x200000
#define H8300_IDE_CTRL 0x60000c
#define H8300_IDE_IRQ  5
#define H8300_IDE_REG_OFFSET 2

#undef outb
#undef inb
#undef outb_p
#undef inb_p
#undef outsw
#undef insw

#define outb(d,a) h8max_outb(d,(unsigned short *)a)
#define inb(a) h8max_inb((unsigned char *)a)
#define outb_p(d,a) h8max_outb(d,(unsigned short *)a)
#define inb_p(a) h8max_inb((unsigned char *)a)
#define outsw(addr,buf,len) h8max_outsw(addr,buf,len);
#define insw(addr,buf,len) h8max_insw(addr,buf,len);

static inline void h8max_outb(unsigned short d,unsigned short *a)
{
	*a = d;
}

static inline unsigned char h8max_inb(unsigned char *a)
{
	return *(a+1);
}

static inline void h8max_outsw(void *addr, void *buf, int len)
{
	unsigned volatile short *ap = (unsigned volatile short *)addr;
	unsigned short *bp = (unsigned short *)buf;
	unsigned short d;
	while(len--) {
		d = *bp++;
		*ap = (d >> 8) | (d << 8);
	}
}

static inline void h8max_insw(void *addr, void *buf, int len)
{
	unsigned volatile short *ap = (unsigned volatile short *)addr;
	unsigned short *bp = (unsigned short *)buf;
	unsigned short d;
	while(len--) {
		d = *ap;
		*bp++ = (d >> 8) | (d << 8);
	}
}

static inline void target_ide_fix_driveid(struct hd_driveid *id)
{
	int c;
	unsigned short *p = (unsigned short *)id;
	for (c = 0; c < SECTOR_WORDS; c++, p++)
		*p = (*p >> 8) | (*p << 8);
}

#endif
