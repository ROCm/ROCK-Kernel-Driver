/*
 * Workbit NinjaSCSI-32Bi/UDE PCI/Cardbus SCSI Host Bus Adapter driver
 * I/O routine
 *
 * This software may be used and distributed according to the terms of
 * the GNU General Public License.
 */

#ifndef _NSP32_IO_H
#define _NSP32_IO_H

static inline void nsp32_write1(unsigned int  base,
				unsigned int  index,
				unsigned char val)
{
	outb(val, (base + index));
}

static inline unsigned char nsp32_read1(unsigned int base,
					unsigned int index)
{
	return inb(base + index);
}

static inline void nsp32_write2(unsigned int  base,
				unsigned int  index,
				unsigned short val)
{
	outw(cpu_to_le16(val), (base + index));
}

static inline unsigned short nsp32_read2(unsigned int base,
					 unsigned int index)
{
	return le16_to_cpu(inw(base + index));
}

static inline void nsp32_write4(unsigned int  base,
				unsigned int  index,
				unsigned long val)
{
	outl(cpu_to_le32(val), (base + index));
}

static inline unsigned long nsp32_read4(unsigned int base,
					unsigned int index)
{
	return le32_to_cpu(inl(base + index));
}

/*==============================================*/

static inline void nsp32_mmio_write1(unsigned long base,
				     unsigned int  index,
				     unsigned char val)
{
	volatile unsigned char *ptr;

	ptr = (unsigned char *)(base + NSP32_MMIO_OFFSET + index);

	*ptr = val;
}

static inline unsigned char nsp32_mmio_read1(unsigned long base,
					     unsigned int  index)
{
	volatile unsigned char *ptr;

	ptr = (unsigned char *)(base + NSP32_MMIO_OFFSET + index);

	return *ptr;
}

static inline void nsp32_mmio_write2(unsigned long base,
				     unsigned int  index,
				     unsigned short val)
{
	volatile unsigned short *ptr;

	ptr = (unsigned short *)(base + NSP32_MMIO_OFFSET + index);

	*ptr = cpu_to_le16(val);
}

static inline unsigned short nsp32_mmio_read2(unsigned long base,
					      unsigned int  index)
{
	volatile unsigned short *ptr;

	//printk(__FUNCTION__ "\n");

	ptr = (unsigned short *)(base + NSP32_MMIO_OFFSET + index);

	return le16_to_cpu(*ptr);
}

static inline void nsp32_mmio_write4(unsigned long base,
				     unsigned int  index,
				     unsigned long val)
{
	volatile unsigned long *ptr;

	ptr = (unsigned long *)(base + NSP32_MMIO_OFFSET + index);

	*ptr = cpu_to_le32(val);
}

static inline unsigned long nsp32_mmio_read4(unsigned long base,
					     unsigned int  index)
{
	volatile unsigned long *ptr;

	//printk(__FUNCTION__ "\n");

	ptr = (unsigned long *)(base + NSP32_MMIO_OFFSET + index);

	return le32_to_cpu(*ptr);
}


/*=============================================*/


static inline unsigned char nsp32_index_read1(unsigned int base,
					      unsigned int reg)
{
	outb(reg, base + INDEX_REG);
	return inb(base + DATA_REG_LOW);
}

static inline void nsp32_index_write1(unsigned int  base,
				      unsigned int  reg,
				      unsigned char val)
{
	outb(reg, base + INDEX_REG);
	outb(val, base + DATA_REG_LOW);
}

static inline unsigned short nsp32_index_read2(unsigned int base,
					       unsigned int reg)
{
	outb(reg, base + INDEX_REG);
	return le16_to_cpu(inw(base + DATA_REG_LOW));
}

static inline void nsp32_index_write2(unsigned int  base,
				      unsigned int  reg,
				      unsigned short val)
{
	outb(reg, base + INDEX_REG);
	outw(cpu_to_le16(val), base + DATA_REG_LOW);
}

static inline unsigned long nsp32_index_read4(unsigned int base,
					      unsigned int reg)
{
	unsigned long h,l;

	outb(reg, base + INDEX_REG);
	l = le16_to_cpu(inw(base + DATA_REG_LOW));
	h = le16_to_cpu(inw(base + DATA_REG_HI ));

	return ((h << 16) | l);
}

static inline void nsp32_index_write4(unsigned int  base,
				      unsigned int  reg,
				      unsigned long val)
{
	unsigned long h,l;

	h = (val & 0xffff0000) >> 16;
	l = (val & 0x0000ffff) >> 0;

	outb(reg, base + INDEX_REG);
	outw(cpu_to_le16(l), base + DATA_REG_LOW);
	outw(cpu_to_le16(h), base + DATA_REG_HI);
}


/* ===================================*/

static inline unsigned char nsp32_mmio_index_read1(unsigned int base,
						   unsigned int reg)
{
	volatile unsigned short *index_ptr, *data_ptr;

	index_ptr = (unsigned short *)(base + NSP32_MMIO_OFFSET + INDEX_REG);
	data_ptr  = (unsigned short *)(base + NSP32_MMIO_OFFSET + DATA_REG_LOW);

	*index_ptr = reg;

	return ((*data_ptr) & 0xff);
}

static inline void nsp32_mmio_index_write1(unsigned int base,
					   unsigned int reg,
					   unsigned char val)
{
	volatile unsigned short *index_ptr, *data_ptr;

	index_ptr = (unsigned short *)(base + NSP32_MMIO_OFFSET + INDEX_REG);
	data_ptr  = (unsigned short *)(base + NSP32_MMIO_OFFSET + DATA_REG_LOW);

	*index_ptr = reg;
	*data_ptr  = (unsigned short)val;
}

static inline unsigned short nsp32_mmio_index_read2(unsigned int base,
						    unsigned int reg)
{
	volatile unsigned short *index_ptr, *data_ptr;

	index_ptr = (unsigned short *)(base + NSP32_MMIO_OFFSET + INDEX_REG);
	data_ptr  = (unsigned short *)(base + NSP32_MMIO_OFFSET + DATA_REG_LOW);

	*index_ptr = reg;

	return le16_to_cpu(*data_ptr);
}

static inline void nsp32_mmio_index_write2(unsigned int base,
					   unsigned int reg,
					   unsigned short val)
{
	volatile unsigned short *index_ptr, *data_ptr;

	index_ptr = (unsigned short *)(base + NSP32_MMIO_OFFSET + INDEX_REG);
	data_ptr  = (unsigned short *)(base + NSP32_MMIO_OFFSET + DATA_REG_LOW);

	*index_ptr = reg;
	*data_ptr  = val;
}

/*-------------------------------------------------------------------*/

static inline void nsp32_multi_read4(unsigned int   BaseAddr,
				     unsigned int   Register,
				     void          *buf,
				     unsigned long  count)
{
	insl(BaseAddr + Register, buf, count);
}

static inline void nsp32_fifo_read(unsigned int   base,
				   void          *buf,
				   unsigned long  count)
{
	//DEBUG(0, __FUNCTION__ "() buf=0x%p, count=0x%lx*4\n", buf, count);
	nsp32_multi_read4(base, FIFO_DATA_LOW, buf, count);
}

static inline void nsp32_multi_write4(unsigned int   BaseAddr,
				      unsigned int   Register,
				      void          *buf,
				      unsigned long  count)
{
	outsl(BaseAddr + Register, buf, count);
}

static inline void nsp32_fifo_write(unsigned int   base,
				    void          *buf,
				    unsigned long  count)
{
	nsp32_multi_write4(base, FIFO_DATA_LOW, buf, count);
}

#endif	/* _NSP32_IO_H */
/* end */
