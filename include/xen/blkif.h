/* 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __XEN_BLKIF_H__
#define __XEN_BLKIF_H__

#include <xen/interface/io/ring.h>
#include <xen/interface/io/blkif.h>
#include <xen/interface/io/protocols.h>

#define BLKIF_SEGS_PER_INDIRECT_FRAME \
	(PAGE_SIZE / sizeof(struct blkif_request_segment))
#define BLKIF_INDIRECT_PAGES(segs) \
	(((segs) + BLKIF_SEGS_PER_INDIRECT_FRAME - 1) \
	 / BLKIF_SEGS_PER_INDIRECT_FRAME)

/* Not a real protocol.  Used to generate ring structs which contain
 * the elements common to all protocols only.  This way we get a
 * compiler-checkable way to use common struct elements, so we can
 * avoid using switch(protocol) in a number of places.  */
struct blkif_common_request {
	char dummy;
};
struct blkif_common_response {
	char dummy;
};

union __attribute__((transparent_union)) blkif_union {
	struct blkif_request *generic;
	struct blkif_request_discard *discard;
	struct blkif_request_indirect *indirect;
};

/* i386 protocol version */
#pragma pack(push, 4)
struct blkif_x86_32_request {
	uint8_t        operation;    /* BLKIF_OP_???                         */
	uint8_t        nr_segments;  /* number of segments                   */
	blkif_vdev_t   handle;       /* only for read/write requests         */
	uint64_t       id;           /* private guest value, echoed in resp  */
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	struct blkif_request_segment seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
};
struct blkif_x86_32_discard {
	uint8_t        operation;    /* BLKIF_OP_DISCARD                     */
	uint8_t        flag;         /* BLKIF_DISCARD_*                      */
	blkif_vdev_t   handle;       /* same as for read/write requests      */
	uint64_t       id;           /* private guest value, echoed in resp  */
	blkif_sector_t sector_number;/* start sector idx on disk             */
	uint64_t       nr_sectors;   /* number of contiguous sectors         */
};
struct blkif_x86_32_indirect {
	uint8_t        operation;    /* BLKIF_OP_INDIRECT                    */
	uint8_t        indirect_op;  /* BLKIF_OP_{READ/WRITE}                */
	uint16_t       nr_segments;  /* number of segments                   */
	uint64_t       id;           /* private guest value, echoed in resp  */
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	blkif_vdev_t   handle;       /* same as for read/write requests      */
	grant_ref_t    indirect_grefs[BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST];
	uint64_t       pad;          /* make it 64 byte aligned */
};
union blkif_x86_32_union {
	struct blkif_x86_32_request generic;
	struct blkif_x86_32_discard discard;
	struct blkif_x86_32_indirect indirect;
};
struct blkif_x86_32_response {
	uint64_t        id;              /* copied from request */
	uint8_t         operation;       /* copied from request */
	int16_t         status;          /* BLKIF_RSP_???       */
};
#pragma pack(pop)

/* x86_64 protocol version */
struct blkif_x86_64_request {
	uint8_t        operation;    /* BLKIF_OP_???                         */
	uint8_t        nr_segments;  /* number of segments                   */
	blkif_vdev_t   handle;       /* only for read/write requests         */
	uint64_t       __attribute__((__aligned__(8))) id;
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	struct blkif_request_segment seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
};
struct blkif_x86_64_discard {
	uint8_t        operation;    /* BLKIF_OP_DISCARD                     */
	uint8_t        flag;         /* BLKIF_DISCARD_*                      */
	blkif_vdev_t   handle;       /* sane as for read/write requests      */
	uint64_t       __attribute__((__aligned__(8))) id;
	blkif_sector_t sector_number;/* start sector idx on disk             */
	uint64_t       nr_sectors;   /* number of contiguous sectors         */
};
struct blkif_x86_64_indirect {
	uint8_t        operation;    /* BLKIF_OP_INDIRECT                    */
	uint8_t        indirect_op;  /* BLKIF_OP_{READ/WRITE}                */
	uint16_t       nr_segments;  /* number of segments                   */
	uint64_t       __attribute__((__aligned__(8))) id;
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	blkif_vdev_t   handle;       /* same as for read/write requests      */
	grant_ref_t    indirect_grefs[BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST];
};
union blkif_x86_64_union {
	struct blkif_x86_64_request generic;
	struct blkif_x86_64_discard discard;
	struct blkif_x86_64_indirect indirect;
};
struct blkif_x86_64_response {
	uint64_t       __attribute__((__aligned__(8))) id;
	uint8_t         operation;       /* copied from request */
	int16_t         status;          /* BLKIF_RSP_???       */
};

#define blkif_native_sring blkif_sring
DEFINE_RING_TYPES(blkif_common, struct blkif_common_request, struct blkif_common_response);
DEFINE_RING_TYPES(blkif_x86_32, union blkif_x86_32_union, struct blkif_x86_32_response);
DEFINE_RING_TYPES(blkif_x86_64, union blkif_x86_64_union, struct blkif_x86_64_response);

union blkif_back_rings {
	blkif_back_ring_t        native;
	blkif_common_back_ring_t common;
	blkif_x86_32_back_ring_t x86_32;
	blkif_x86_64_back_ring_t x86_64;
};
typedef union blkif_back_rings blkif_back_rings_t;

enum blkif_protocol {
	BLKIF_PROTOCOL_NATIVE = 1,
	BLKIF_PROTOCOL_X86_32 = 2,
	BLKIF_PROTOCOL_X86_64 = 3,
};

static void inline blkif_get_x86_32_req(union blkif_union dst,
					const union blkif_x86_32_union *src)
{
#ifdef __i386__
	memcpy(dst.generic, src, sizeof(*dst.generic));
#else
	unsigned int i, n;

	dst.generic->operation = src->generic.operation;
	dst.generic->nr_segments = src->generic.nr_segments;
	dst.generic->handle = src->generic.handle;
	dst.generic->id = src->generic.id;
	dst.generic->sector_number = src->generic.sector_number;
	barrier();
	switch (__builtin_expect(dst.generic->operation, BLKIF_OP_READ)) {
	default:
		n = min_t(unsigned int, dst.generic->nr_segments,
			  BLKIF_MAX_SEGMENTS_PER_REQUEST);
		for (i = 0; i < n; i++)
			dst.generic->seg[i] = src->generic.seg[i];
		break;
	case BLKIF_OP_DISCARD:
		/* All fields up to sector_number got copied above already. */
		dst.discard->nr_sectors = src->discard.nr_sectors;
		break;
	case BLKIF_OP_INDIRECT:
		/* All fields up to sector_number got copied above already. */
		dst.indirect->handle = src->indirect.handle;
		n = min_t(unsigned int, BLKIF_INDIRECT_PAGES(dst.indirect->nr_segments),
			  BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST);
		for (i = 0; i < n; ++i)
			dst.indirect->indirect_grefs[i] = src->indirect.indirect_grefs[i];
		break;
	}
#endif
}

static void inline blkif_get_x86_64_req(union blkif_union dst,
					const union blkif_x86_64_union *src)
{
#ifdef __x86_64__
	memcpy(dst.generic, src, sizeof(*dst.generic));
#else
	unsigned int i, n;

	dst.generic->operation = src->generic.operation;
	dst.generic->nr_segments = src->generic.nr_segments;
	dst.generic->handle = src->generic.handle;
	dst.generic->id = src->generic.id;
	dst.generic->sector_number = src->generic.sector_number;
	barrier();
	switch (__builtin_expect(dst.generic->operation, BLKIF_OP_READ)) {
	default:
		n = min_t(unsigned int, dst.generic->nr_segments,
			  BLKIF_MAX_SEGMENTS_PER_REQUEST);
		for (i = 0; i < n; i++)
			dst.generic->seg[i] = src->generic.seg[i];
		break;
	case BLKIF_OP_DISCARD:
		/* All fields up to sector_number got copied above already. */
		dst.discard->nr_sectors = src->discard.nr_sectors;
		break;
	case BLKIF_OP_INDIRECT:
		/* All fields up to sector_number got copied above already. */
		dst.indirect->handle = src->indirect.handle;
		n = min_t(unsigned int, BLKIF_INDIRECT_PAGES(dst.indirect->nr_segments),
			  BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST);
		for (i = 0; i < n; ++i)
			dst.indirect->indirect_grefs[i] = src->indirect.indirect_grefs[i];
		break;
	}
#endif
}

#endif /* __XEN_BLKIF_H__ */
