/*
 * linux/include/asm-m68k/raw_io.h 
 *
 * 10/20/00 RZ: - created from bits of io.h and ide.h to cleanup namespace
 *
 */

#ifndef _RAW_IO_H
#define _RAW_IO_H

#ifdef __KERNEL__


/* ++roman: The assignments to temp. vars avoid that gcc sometimes generates
 * two accesses to memory, which may be undesirable for some devices.
 */
#define in_8(addr) \
    ({ unsigned char __v = (*(volatile unsigned char *) (addr)); __v; })
#define in_be16(addr) \
    ({ unsigned short __v = (*(volatile unsigned short *) (addr)); __v; })
#define in_be32(addr) \
    ({ unsigned int __v = (*(volatile unsigned int *) (addr)); __v; })

#define out_8(addr,b) (void)((*(volatile unsigned char *) (addr)) = (b))
#define out_be16(addr,b) (void)((*(volatile unsigned short *) (addr)) = (b))
#define out_be32(addr,b) (void)((*(volatile unsigned int *) (addr)) = (b))

#define raw_inb in_8
#define raw_inw in_be16
#define raw_inl in_be32

#define raw_outb(val,port) out_8((port),(val))
#define raw_outw(val,port) out_be16((port),(val))
#define raw_outl(val,port) out_be32((port),(val))

#define raw_insb(port, buf, len) ({	   \
	volatile unsigned char *_port = (volatile unsigned char *) (port);   \
        unsigned char *_buf =(unsigned char *)(buf);	   \
        unsigned int  _i,_len=(unsigned int)(len);	   \
        for(_i=0; _i< _len; _i++)  \
           *_buf++=in_8(_port);      \
  })

#define raw_outsb(port, buf, len) ({	   \
	volatile unsigned char *_port = (volatile unsigned char *) (port);   \
        unsigned char *_buf =(unsigned char *)(buf);	   \
        unsigned int  _i,_len=(unsigned int)(len);	   \
        for( _i=0; _i< _len; _i++)  \
           out_8(_port,*_buf++);      \
  })
 

#define raw_insw(port, buf, nr) ({				\
	volatile unsigned char *_port = (volatile unsigned char *) (port);	\
	unsigned char *_buf = (unsigned char *)(buf);			\
	unsigned int _nr = (unsigned int)(nr);					\
	unsigned long _tmp;				\
							\
	if (_nr & 15) {					\
		_tmp = (_nr & 15) - 1;			\
		asm volatile (				\
			"1: movew %2@,%0@+; dbra %1,1b"	\
			: "=a" (_buf), "=d" (_tmp)	\
			: "a" (_port), "0" (_buf),	\
			  "1" (_tmp));			\
	}						\
	if (_nr >> 4) {					\
		_tmp = (_nr >> 4) - 1;			\
		asm volatile (				\
			"1: "				\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"movew %2@,%0@+; "		\
			"dbra %1,1b"			\
			: "=a" (_buf), "=d" (_tmp)	\
			: "a" (_port), "0" (_buf),	\
			  "1" (_tmp));			\
	}						\
})

#define raw_outsw(port, buf, nr) ({				\
	volatile unsigned char *_port = (volatile unsigned char *) (port);	\
	unsigned char *_buf = (unsigned char *)(buf);			\
	unsigned int _nr = (unsigned int)(nr);					\
	unsigned long _tmp;				\
							\
	if (_nr & 15) {					\
		_tmp = (_nr & 15) - 1;			\
		asm volatile (				\
			"1: movew %0@+,%2@; dbra %1,1b"	\
			: "=a" (_buf), "=d" (_tmp)	\
			: "a" (_port), "0" (_buf),	\
			  "1" (_tmp));			\
	}						\
	if (_nr >> 4) {					\
		_tmp = (_nr >> 4) - 1;			\
		asm volatile (				\
			"1: "				\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"movew %0@+,%2@; "		\
			"dbra %1,1b"	   		\
			: "=a" (_buf), "=d" (_tmp)	\
			: "a" (_port), "0" (_buf),	\
			  "1" (_tmp));			\
	}						\
})


#define raw_insw_swapw(port, buf, nr) \
({  if ((nr) % 8) \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 dbra %/d6,1b"  \
		:               \
		: "g" (port), "g" (buf), "g" (nr) \
		: "d0", "a0", "a1", "d6"); \
    else \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 lsrl  #3,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 dbra %/d6,1b"  \
                :               \
		: "g" (port), "g" (buf), "g" (nr) \
		: "d0", "a0", "a1", "d6"); \
})


#define raw_outsw_swapw(port, buf, nr) \
({  if ((nr) % 8) \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 dbra %/d6,1b"  \
                :               \
		: "g" (port), "g" (buf), "g" (nr) \
		: "d0", "a0", "a1", "d6"); \
    else \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 lsrl  #3,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 dbra %/d6,1b"  \
                :               \
		: "g" (port), "g" (buf), "g" (nr) \
		: "d0", "a0", "a1", "d6"); \
})


#endif /* __KERNEL__ */

#endif /* _RAW_IO_H */
