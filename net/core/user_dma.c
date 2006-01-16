/*
  Copyright(c) 2004 - 2005 Intel Corporation
  Portions based on net/core/datagram.c and copyrighted by their authors.

  This code allows the net stack to make use of a DMA engine for
  skb to iovec copies.
*/

#include <linux/dmaengine.h>
#include <linux/socket.h>
#include <linux/rtnetlink.h> /* for BUG_TRAP */
#include <net/tcp.h>


#ifdef CONFIG_NET_DMA

#define NET_DMA_DEFAULT_COPYBREAK 1024

int sysctl_tcp_dma_copybreak = NET_DMA_DEFAULT_COPYBREAK;

/**
 *	dma_skb_copy_datagram_iovec - Copy a datagram to an iovec.
 *	@skb - buffer to copy
 *	@offset - offset in the buffer to start copying from
 *	@iovec - io vector to copy to
 *	@len - amount of data to copy from buffer to iovec
 *	@locked_list - locked iovec buffer data
 *
 *	Note: the iovec is modified during the copy.
 */
int dma_skb_copy_datagram_iovec(struct dma_chan *chan,
			struct sk_buff *skb, int offset, struct iovec *to,
			size_t len, struct dma_locked_list *locked_list)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	dma_cookie_t cookie = 0;

	/* Copy header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		if ((cookie = dma_memcpy_toiovec(chan, to, locked_list,
		     skb->data + offset, copy)) < 0)
			goto fault;
		if ((len -= copy) == 0)
			goto end;
		offset += copy;
	}

	/* Copy paged appendix. Hmm... why does this look so complicated? */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		BUG_TRAP(start <= offset + len);

		end = start + skb_shinfo(skb)->frags[i].size;
		if ((copy = end - offset) > 0) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			struct page *page = frag->page;

			if (copy > len)
				copy = len;

			cookie = dma_memcpy_pg_toiovec(chan, to, locked_list, page,
					frag->page_offset + offset - start, copy);
			if (cookie < 0)
				goto fault;
			if (!(len -= copy))
				goto end;
			offset += copy;
		}
		start = end;
	}

	if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *list = skb_shinfo(skb)->frag_list;

		for (; list; list = list->next) {
			int end;

			BUG_TRAP(start <= offset + len);

			end = start + list->len;
			if ((copy = end - offset) > 0) {
				if (copy > len)
					copy = len;
				if ((cookie = dma_skb_copy_datagram_iovec(chan, list,
					        offset - start, to, copy, locked_list)) < 0)
					goto fault;
				if ((len -= copy) == 0)
					goto end;
				offset += copy;
			}
			start = end;
		}
	}

end:
	if (!len) {
		skb->dma_cookie = cookie;
		return cookie;
	}

fault:
 	return -EFAULT;
}

#else

int dma_skb_copy_datagram_iovec(struct dma_chan *chan,
			const struct sk_buff *skb, int offset, struct iovec *to,
			size_t len, struct dma_locked_list *locked_list)
{
	return skb_copy_datagram_iovec(skb, offset, to, len);
}

#endif
