/* H8MAX IDE I/F Config */

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
