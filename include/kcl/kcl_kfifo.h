/* SPDX-License-Identifier: GPL-2.0 */

#ifndef KCL_LINUX_KFIFO_H
#define KCL_LINUX_KFIFO_H

#ifndef HAVE_KFIFO_OUT_LINEAR

#include <linux/kfifo.h>

/**
 * commit 4edd7e96a1f159f43bd1cb82616f81eaddd54262
 * Author: Jiri Slaby (SUSE) <jirislaby@kernel.org>
 * Date:   Fri Apr 5 08:08:14 2024 +0200
 *
 *   kfifo: add kfifo_out_linear{,_ptr}()
 */

/**
 * kfifo_out_linear - gets a tail of/offset to available data
 * @fifo: address of the fifo to be used
 * @tail: pointer to an unsigned int to store the value of tail
 * @n: max. number of elements to point at
 *
 * This macro obtains the offset (tail) to the available data in the fifo
 * buffer and returns the
 * numbers of elements available. It returns the available count till the end
 * of data or till the end of the buffer. So that it can be used for linear
 * data processing (like memcpy() of (@fifo->data + @tail) with count
 * returned).
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these macro.
 */
#define kfifo_out_linear(fifo, tail, n) \
__kfifo_uint_must_check_helper( \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	unsigned int *__tail = (tail); \
	unsigned long __n = (n); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->kfifo; \
	(__recsize) ? \
	__kfifo_out_linear_r(__kfifo, __tail, __n, __recsize) : \
	__kfifo_out_linear(__kfifo, __tail, __n); \
}) \
)

/**
 * kfifo_out_linear_ptr - gets a pointer to the available data
 * @fifo: address of the fifo to be used
 * @ptr: pointer to data to store the pointer to tail
 * @n: max. number of elements to point at
 *
 * Similarly to kfifo_out_linear(), this macro obtains the pointer to the
 * available data in the fifo buffer and returns the numbers of elements
 * available. It returns the available count till the end of available data or
 * till the end of the buffer. So that it can be used for linear data
 * processing (like memcpy() of @ptr with count returned).
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these macro.
 */
#define kfifo_out_linear_ptr(fifo, ptr, n) \
__kfifo_uint_must_check_helper( \
({ \
	typeof((fifo) + 1) ___tmp = (fifo); \
	unsigned int ___tail; \
	unsigned int ___n = kfifo_out_linear(___tmp, &___tail, (n)); \
	*(ptr) = ___tmp->kfifo.data + ___tail * kfifo_esize(___tmp); \
	___n; \
}) \
)

extern unsigned int __kfifo_out_linear(struct __kfifo *fifo,
	unsigned int *tail, unsigned int n);

extern unsigned int __kfifo_out_linear_r(struct __kfifo *fifo,
	unsigned int *tail, unsigned int n, size_t recsize);

/**
 * kfifo_skip_count - skip output data
 * @fifo: address of the fifo to be used
 * @count: count of data to skip
 */
#define	kfifo_skip_count(fifo, count) do { \
	typeof((fifo) + 1) __tmp = (fifo); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->kfifo; \
	if (__recsize) \
		__kfifo_skip_r(__kfifo, __recsize); \
	else \
		__kfifo->out += (count); \
} while (0)

#endif
#endif
