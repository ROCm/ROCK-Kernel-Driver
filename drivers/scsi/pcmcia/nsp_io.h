/*
  NinjaSCSI I/O funtions 
      By: YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>
 
  This software may be used and distributed according to the terms of
  the GNU General Public License.

  */

/* $Id: nsp_io.h,v 1.9 2001/09/07 04:32:42 elca Exp $ */

#ifndef __NSP_IO_H__
#define __NSP_IO_H__

static inline          void nsp_write(unsigned int base,
				      unsigned int index,
				      unsigned char val);
static inline unsigned char nsp_read(unsigned int base,
				     unsigned int index);
static inline          void nsp_index_write(unsigned int BaseAddr,
					    unsigned int Register,
					    unsigned char Value);
static inline unsigned char nsp_index_read(unsigned int BaseAddr,
					   unsigned int Register);

/*******************************************************************
 * Basic IO
 */

static inline void nsp_write(unsigned int  base,
			     unsigned int  index,
			     unsigned char val)
{
	outb(val, (base + index));
}

static inline unsigned char nsp_read(unsigned int base,
				     unsigned int index)
{
	return inb(base + index);
}


/**********************************************************************
 * Indexed IO
 */
static inline unsigned char nsp_index_read(unsigned int BaseAddr,
					   unsigned int Register)
{
	outb(Register, BaseAddr + INDEXREG);
	return inb(BaseAddr + DATAREG);
}

static inline void nsp_index_write(unsigned int  BaseAddr,
				   unsigned int  Register,
				   unsigned char Value)
{
	outb(Register, BaseAddr + INDEXREG);
	outb(Value, BaseAddr + DATAREG);
}

/*********************************************************************
 * fifo func
 */

/* read 8 bit FIFO */
static inline void nsp_multi_read_1(unsigned int   BaseAddr,
				    unsigned int   Register,
				    void          *buf,
				    unsigned long  count)
{
	insb(BaseAddr + Register, buf, count);
}

static inline void nsp_fifo8_read(unsigned int   base,
				  void          *buf,
				  unsigned long  count)
{
	//DEBUG(0, __FUNCTION__ "() buf=0x%p, count=0x%lx\n", buf, count);
	nsp_multi_read_1(base, FIFODATA, buf, count);
}

/*--------------------------------------------------------------*/

/* read 16 bit FIFO */
static inline void nsp_multi_read_2(unsigned int   BaseAddr,
				    unsigned int   Register,
				    void          *buf,
				    unsigned long  count)
{
	insw(BaseAddr + Register, buf, count);
}

static inline void nsp_fifo16_read(unsigned int   base,
				   void          *buf,
				   unsigned long  count)
{
	//DEBUG(0, __FUNCTION__ "() buf=0x%p, count=0x%lx*2\n", buf, count);
	nsp_multi_read_2(base, FIFODATA, buf, count);
}

/*--------------------------------------------------------------*/

/* read 32bit FIFO */
static inline void nsp_multi_read_4(unsigned int   BaseAddr,
				    unsigned int   Register,
				    void          *buf,
				    unsigned long  count)
{
	insl(BaseAddr + Register, buf, count);
}

static inline void nsp_fifo32_read(unsigned int   base,
				   void          *buf,
				   unsigned long  count)
{
	//DEBUG(0, __FUNCTION__ "() buf=0x%p, count=0x%lx*4\n", buf, count);
	nsp_multi_read_4(base, FIFODATA, buf, count);
}

/*----------------------------------------------------------*/

/* write 8bit FIFO */
static inline void nsp_multi_write_1(unsigned int   BaseAddr,
				     unsigned int   Register,
				     void          *buf,
				     unsigned long  count)
{
	outsb(BaseAddr + Register, buf, count);
}

static inline void nsp_fifo8_write(unsigned int   base,
				   void          *buf,
				   unsigned long  count)
{
	nsp_multi_write_1(base, FIFODATA, buf, count);
}

/*---------------------------------------------------------*/

/* write 16bit FIFO */
static inline void nsp_multi_write_2(unsigned int   BaseAddr,
				     unsigned int   Register,
				     void          *buf,
				     unsigned long  count)
{
	outsw(BaseAddr + Register, buf, count);
}

static inline void nsp_fifo16_write(unsigned int   base,
				    void          *buf,
				    unsigned long  count)
{
	nsp_multi_write_2(base, FIFODATA, buf, count);
}

/*---------------------------------------------------------*/

/* write 32bit FIFO */
static inline void nsp_multi_write_4(unsigned int   BaseAddr,
				     unsigned int   Register,
				     void          *buf,
				     unsigned long  count)
{
	outsl(BaseAddr + Register, buf, count);
}

static inline void nsp_fifo32_write(unsigned int   base,
				    void          *buf,
				    unsigned long  count)
{
	nsp_multi_write_4(base, FIFODATA, buf, count);
}

#endif
/* end */
