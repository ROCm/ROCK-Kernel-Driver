#ifndef _USB_STORAGE_RAW_BULK_H_
#define _USB_STORAGE_RAW_BULK_H_

/* scatter-gather */
extern unsigned char *us_copy_from_sgbuf(
	unsigned char *content, int buflen,
	int *index, int *offset, int use_sg);

extern unsigned char *us_copy_from_sgbuf_all(
	unsigned char *content, int len, int use_sg);

extern void us_copy_to_sgbuf(
	unsigned char *buffer, int buflen,
	void *content, int *index, int *offset, int use_sg);

extern void us_copy_to_sgbuf_all(
	unsigned char *buffer, int buflen,
	void *content, int use_sg);

#endif
