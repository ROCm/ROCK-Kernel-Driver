/*
 * Common routines for a handful of drivers.
 * Unrelated to CF/SM - just scatter-gather stuff.
 */

#include "usb.h"
#include "raw_bulk.h"

/*
 * The routines below convert scatter-gather to single buffer.
 * Some drivers claim this is necessary.
 * Nothing is done when use_sg is zero.
 */

/*
 * Copy from scatter-gather buffer into a newly allocated single buffer,
 * starting at a given index and offset.
 * When done, update index and offset.
 * Return a pointer to the single buffer.
 */
unsigned char *
us_copy_from_sgbuf(unsigned char *content, int len,
		   int *index, int *offset, int use_sg) {
	struct scatterlist *sg;
	unsigned char *buffer;
	int transferred, i;

	if (!use_sg)
		return content;

	sg = (struct scatterlist *)content;
	buffer = kmalloc(len, GFP_NOIO);
	if (buffer == NULL)
		return NULL;

	transferred = 0;
	i = *index;
	while (i < use_sg && transferred < len) {
		unsigned char *ptr;
		unsigned int length, room;

		ptr = sg_address(sg[i]) + *offset;

		room = sg[i].length - *offset;
		length = len - transferred;
		if (length > room)
			length = room;

		memcpy(buffer+transferred, ptr, length);
		transferred += length;
		*offset += length;
		if (length == room) {
			i++;
			*offset = 0;
		}
	}
	*index = i;

	return buffer;
}

unsigned char *
us_copy_from_sgbuf_all(unsigned char *content, int len, int use_sg) {
	int index, offset;

	index = offset = 0;
	return us_copy_from_sgbuf(content, len, &index, &offset, use_sg);
}

/*
 * Copy from a single buffer into a scatter-gather buffer,
 * starting at a given index and offset.
 * When done, update index and offset.
 */
void
us_copy_to_sgbuf(unsigned char *buffer, int buflen,
		 void *content, int *index, int *offset, int use_sg) {
	struct scatterlist *sg;
	int i, transferred;

	if (!use_sg)
		return;

	transferred = 0;
	sg = content;
	i = *index;
	while (i < use_sg && transferred < buflen) {
		unsigned char *ptr;
		unsigned int length, room;

		ptr = sg_address(sg[i]) + *offset;

		room = sg[i].length - *offset;
		length = buflen - transferred;
		if (length > room)
			length = room;
		
		memcpy(ptr, buffer+transferred, length);
		transferred += sg[i].length;
		*offset += length;
		if (length == room) {
			i++;
			*offset = 0;
		}
	}
	*index = i;
}

void
us_copy_to_sgbuf_all(unsigned char *buffer, int buflen,
		     void *content, int use_sg) {
	int index, offset;

	index = offset = 0;
	us_copy_to_sgbuf(buffer, buflen, content, &index, &offset, use_sg);
}
