/*
 * Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <asm/io.h>

/* MODULE API */

static volatile u8 *buffer1;
static volatile u8 *buffer2;

struct dma_client *test_dma_client;
struct dma_chan *test_dma_chan;
static dma_cookie_t cookie;

void
test_added_chan(void)
{
	int i;

	printk("buffer1 = %p\n", buffer1);
	printk("buffer2 = %p\n", buffer2);
	for (i = 0; i < 20; i+=4)
		printk("%u %u %u %u\n", buffer2[i], buffer2[i+1], buffer2[i+2], buffer2[i+3]);

//	for (i = 0; i < 10; i++) {
	cookie = dma_async_memcpy_buf_to_buf(test_dma_chan,
		(void *)buffer2,
		(void *)buffer1,
		2000);
	dma_async_memcpy_issue_pending(test_dma_chan);
//	}
//	printk("dma cookie = %i\n", cookie);
	if (dma_async_memcpy_complete(test_dma_chan, cookie, NULL, NULL) == DMA_IN_PROGRESS)
		printk("DMA cookie == IN PROGRESS\n");
	else
		printk("DMA cookie == SUCCESS\n");
#if 0
	for (i = 0; i < 1000; i++) {
		if (buffer2[1] != 0)
			break;
		mdelay(1);
	}
	printk("i = %d\n", i);
	for (i = 0; i < 20; i+=4)
		printk("%u %u %u %u\n", buffer2[i], buffer2[i+1], buffer2[i+2], buffer2[i+3]);
	for (i = 0; i < 20; i+=4)
		printk("%u %u %u %u\n", buffer1[i], buffer1[i+1], buffer1[i+2], buffer1[i+3]);
#endif
}

void test_dma_event(struct dma_client *client, struct dma_chan *chan, enum dma_event event)
{
	switch (event) {
	case DMA_RESOURCE_ADDED:
		test_dma_chan = chan;
		test_added_chan();
		break;
	case DMA_RESOURCE_REMOVED:
		test_dma_chan = NULL;
		break;
	default:
		break;
	}
}

static int __init
testclient_init_module(void)
{
	int i;

	buffer1 = kmalloc(sizeof(u8) * 2000, SLAB_KERNEL);
	buffer2 = kmalloc(sizeof(u8) * 2000, SLAB_KERNEL);

	memset((void *)buffer2, 0, 2000);
	for (i = 0; i < 2000; i++)
		buffer1[i] = i;

	test_dma_client = dma_async_client_register(test_dma_event);
	if (!test_dma_client) {
		printk(KERN_ERR "Could not register dma client!\n");
		return 0;
	}

	dma_async_client_chan_request(test_dma_client, 1);

	return 0;
}

module_init(testclient_init_module);

static void __exit
testclient_exit_module(void)
{
	int i;
	for (i = 0; i < 20; i+=4)
		printk("%u %u %u %u\n", buffer2[i], buffer2[i+1], buffer2[i+2], buffer2[i+3]);

	if (test_dma_chan) {
		if (dma_async_memcpy_complete(test_dma_chan, cookie, NULL, NULL) == DMA_SUCCESS)
			printk("DMA cookie == SUCCESS\n");
		else
			printk("DMA cookie == IN PROGRESS\n");
	}

	dma_async_client_unregister(test_dma_client);

	kfree ((void*)buffer1);
	kfree ((void*)buffer2);
}

module_exit(testclient_exit_module);

