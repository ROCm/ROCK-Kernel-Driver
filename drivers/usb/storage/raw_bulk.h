#ifndef _USB_STORAGE_RAW_BULK_H_
#define _USB_STORAGE_RAW_BULK_H_

/* usb bulk */
extern int usb_storage_send_control(
	struct us_data *us, unsigned int pipe,
	unsigned char request, unsigned char requesttype,
	unsigned int value, unsigned int index,
	unsigned char *xfer_data, unsigned int xfer_len);

extern int usb_storage_raw_bulk(
	struct us_data *us, int direction,
	unsigned char *data, unsigned int len, unsigned int *act_len);

extern int usb_storage_bulk_transport(
	struct us_data *us, int direction,
	unsigned char *data, unsigned int len, int use_sg);

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
