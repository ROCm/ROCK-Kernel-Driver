/***************************************************************************
 *
 *  drivers/s390/char/tape_idalbuf.h
 *    functions for idal buffer handling
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 *
 ****************************************************************************
 */

#include <linux/kernel.h>
#include <asm/hardirq.h> // in_interrupt

/*
 * Macros
 */

#ifdef CONFIG_ARCH_S390X
	// on ESAME each idal entry points to a 4K buffer
	#define IDALBUF_BLK_SIZE    4096
#else
	// on ESA each idal entry points to a 2K buffer
	#define IDALBUF_BLK_SIZE    2048
#endif

#define IDALBUF_MAX_ENTRIES 33 // an ida list can have up to 33 entries
#define IDALBUF_PAGE_ORDER  1  // each chunk has 2exp(1) pages

#define __IDALBUF_CHUNK_SIZE   ((1<<IDALBUF_PAGE_ORDER) * PAGE_SIZE)
#define __IDALBUF_ENTRIES_PER_CHUNK (__IDALBUF_CHUNK_SIZE/IDALBUF_BLK_SIZE)

// Macro which finds out, if we need idal addressing

#ifdef CONFIG_ARCH_S390X
#define __IDALBUF_DIRECT_ADDR(idal) \
	( (idal->size <= __IDALBUF_CHUNK_SIZE) \
	&& ( ( ((unsigned long)idal->data[0]) >> 31) == 0) )
#else
#define __IDALBUF_DIRECT_ADDR(idal) \
	(idal->size <= __IDALBUF_CHUNK_SIZE)
#endif

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

/*
 * The idalbuf data structure
 */

typedef struct _idalbuf_t{
	void*   data[IDALBUF_MAX_ENTRIES];
	int     size;
} idalbuf_t;

static inline unsigned int
__round_up_multiple (unsigned int no, unsigned int mult)
{
        int rem = no % mult;
        return (rem ? no - rem + mult : no);
}

/*
 * Setup a ccw in a way that the data buf is an idalbuf_mem_t
 */

static inline void
idalbuf_set_normalized_cda(ccw1_t *ccw, idalbuf_t* idal)
{
	if(__IDALBUF_DIRECT_ADDR(idal)){
		// we do not need idals - use direct addressing
		ccw->cda = (unsigned long) idal->data[0];
	} else {
		// setup idals
		ccw->flags |= CCW_FLAG_IDA;
		ccw->cda = (unsigned long) idal->data;
	}
	ccw->count = idal->size;
}

/*
 * Alloc size bytes of memory
 */

static inline idalbuf_t* 
idalbuf_alloc(size_t size)
{
	int i = 0;
	int count = __round_up_multiple(size,IDALBUF_BLK_SIZE) / IDALBUF_BLK_SIZE;
	idalbuf_t* rc;
	char* addr = NULL;
	int kmalloc_flags;
	if(in_interrupt())
		kmalloc_flags = GFP_ATOMIC;
	else
		kmalloc_flags = GFP_KERNEL;

	if(size/IDALBUF_BLK_SIZE > IDALBUF_MAX_ENTRIES)
		BUG();
	// the ida list must be below 2GB --> GFP_DMA
	rc = kmalloc(sizeof(idalbuf_t),kmalloc_flags | GFP_DMA);
	if(!rc)
		goto error;
	for(i=0; i< count;i++){
		if((i % __IDALBUF_ENTRIES_PER_CHUNK) == 0){
			// data does not need to be below 2GB
			rc->data[i] = (void*)__get_free_pages(kmalloc_flags ,IDALBUF_PAGE_ORDER);
			if(!rc->data[i])
				goto error;
			addr = (char*)(rc->data[i]);
		} else {
			addr+=IDALBUF_BLK_SIZE;
			rc->data[i] = addr;
		}
	}
	rc->size=size;
	return rc;
error:
	if(rc){
		int end = i;
		for(i=end-1;i>=0;i-=__IDALBUF_ENTRIES_PER_CHUNK)
			free_pages((unsigned long)rc->data[i],IDALBUF_PAGE_ORDER);
		kfree(rc);
	}
	return NULL;
}

/*
 * Free an idal buffer
 */

static inline void
idalbuf_free(idalbuf_t* idal)
{
	int count = __round_up_multiple(idal->size,__IDALBUF_CHUNK_SIZE)/__IDALBUF_CHUNK_SIZE;
	int i;
	for(i = 0; i < count; i++){
		free_pages((unsigned long)idal->data[i*__IDALBUF_ENTRIES_PER_CHUNK],IDALBUF_PAGE_ORDER);
	}
	kfree(idal);
}

/*
 * Copy count bytes from an idal buffer to contiguous user memory
 */

static inline int
idalbuf_copy_to_user(void* to, const idalbuf_t* from, size_t count)
{
	int i;
	int rc = 0;
	if(count > from->size)
		BUG();
	for(i = 0; i < count; i+=__IDALBUF_CHUNK_SIZE){
		rc = copy_to_user(((char*)to) + i,from->data[i/IDALBUF_BLK_SIZE],MIN(__IDALBUF_CHUNK_SIZE,(count-i)));
		if(rc)
			goto out;
	}
out:
	return rc;
}

/*
 * Copy count bytes from contiguous user memory to an idal buffer
 */

static inline int
idalbuf_copy_from_user(idalbuf_t* to, const void* from, size_t count)
{
	int i;
	int rc = 0;
	if(count > to->size)
		BUG();
	for(i = 0; i < count; i+=__IDALBUF_CHUNK_SIZE){
		rc = copy_from_user(to->data[i/IDALBUF_BLK_SIZE],((char*)from)+i,MIN(__IDALBUF_CHUNK_SIZE,(count-i)));
		if(rc)
			goto out;
	}
out:
	return rc;
}

/*
 * Copy count bytes from an idal buffer to a contiguous kernel buffer
 */

static inline void
idalbuf_copy_from_idal(void* to, const idalbuf_t* from, size_t count)
{
        int i;
	if(count > from->size)
		BUG();
        for(i = 0; i < count; i+=__IDALBUF_CHUNK_SIZE){
                memcpy((char*)to + i,(from->data[i/IDALBUF_BLK_SIZE]),MIN(__IDALBUF_CHUNK_SIZE,(count-i)) );
	}
}

/*
 * Copy count bytes from a contiguous kernel buffer to an idal buffer
 */
 
static inline void 
idalbuf_copy_to_idal(idalbuf_t* to, const void* from, size_t count)
{
        int i;
	if(count > to->size)
		BUG();
        for(i = 0; i < count; i+=__IDALBUF_CHUNK_SIZE){
                memcpy(to->data[i/IDALBUF_BLK_SIZE],(char*)from+i,MIN(__IDALBUF_CHUNK_SIZE,(count-i)) );
	}
}
