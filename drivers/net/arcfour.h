/* arcfour.h */

#ifndef _ARCFOUR_H
#define _ARCFOUR_H

typedef struct {
    unsigned i;
    unsigned j;
    unsigned char S[256];
} arcfour_context;

extern void arcfour_setkey(arcfour_context *, const unsigned char *, unsigned);
extern void arcfour_encrypt(arcfour_context *, const unsigned char *, unsigned,
			    unsigned char *);
#define arcfour_decrypt arcfour_encrypt

#endif /* _ARCFOUR_H */
