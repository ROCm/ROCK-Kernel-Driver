#ifndef _AV7110_IPACK_H_
#define _AV7110_IPACK_H_

extern void av7110_ipack_init(ipack *p, int size,
			      void (*func)(u8 *buf,  int size, void *priv));

extern void av7110_ipack_reset(ipack *p);
extern int  av7110_ipack_instant_repack(const u8 *buf, int count, ipack *p);
extern void av7110_ipack_free(ipack * p);
extern void av7110_ipack_flush(ipack *p);

#endif

