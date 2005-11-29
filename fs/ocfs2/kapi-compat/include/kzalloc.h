#ifndef KAPI_KZALLOC_H
#define KAPI_KZALLOC_H

#define kzalloc(_size, _gfp_flags)	kcalloc(1, _size, _gfp_flags)

#endif /* KAPI_KZALLOC_H */
