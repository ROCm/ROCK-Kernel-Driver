// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kfifo.h>

#ifndef HAVE_KFIFO_OUT_LINEAR

#define	__KFIFO_PEEK(data, out, mask) \
	((data)[(out) & (mask)])
/*
 * __kfifo_peek_n internal helper function for determinate the length of
 * the next record in the fifo
 */
static unsigned int __kfifo_peek_n(struct __kfifo *fifo, size_t recsize)
{
	unsigned int l;
	unsigned int mask = fifo->mask;
	unsigned char *data = fifo->data;

	l = __KFIFO_PEEK(data, fifo->out, mask);

	if (--recsize)
		l |= __KFIFO_PEEK(data, fifo->out + 1, mask) << 8;

	return l;
}

unsigned int __kfifo_out_linear(struct __kfifo *fifo,
		unsigned int *tail, unsigned int n)
{
	unsigned int size = fifo->mask + 1;
	unsigned int off = fifo->out & fifo->mask;

	if (tail)
		*tail = off;

	return min3(n, fifo->in - fifo->out, size - off);
}
EXPORT_SYMBOL(__kfifo_out_linear);

unsigned int __kfifo_out_linear_r(struct __kfifo *fifo,
		unsigned int *tail, unsigned int n, size_t recsize)
{
	if (fifo->in == fifo->out)
		return 0;

	if (tail)
		*tail = fifo->out + recsize;

	return min(n, __kfifo_peek_n(fifo, recsize));
}
EXPORT_SYMBOL(__kfifo_out_linear_r);

#endif
