//#include <stdlib.h>
//#include <string.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "foo.h"

mpi_limb_t _gcry_mpih_mul( mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t usize, mpi_ptr_t vp, mpi_size_t vsize);

const unsigned char __clz_tab[] = {
  0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
};

//#define gcry_xmalloc(X)	malloc(X)
//#define gcry_xrealloc(X, Y)	realloc(X, Y)
//#define gcry_xcalloc(X, Y)	calloc(X, Y)
//#define gcry_free(X)		free(X)

static __inline__ void * kcalloc(size_t nmemb, size_t size)
{
	void *foo;
	foo = kmalloc(nmemb*size, GFP_KERNEL);
	if (!foo)
		return NULL;
	memset(foo, 0x00, nmemb*size);
	return foo;
}

// yeah, it's a hack, sue me...
static __inline__ void * krealloc(void *old, size_t size)
{
	void *new;

	new = kmalloc(size, GFP_KERNEL);
	if (old == NULL)
		return new;
	if (!new)
		return NULL;
	memcpy(new, old, size);
	return new;
}


#define gcry_xmalloc(X)		kmalloc(X, GFP_KERNEL)
#define gcry_xrealloc(X, Y)	krealloc(X, Y)
#define gcry_xcalloc(X, Y)	kcalloc(X, Y)
#define gcry_free(X)		kfree(X)


#define gcry_is_secure(X)	(0)
#define mpi_alloc(X)		mpi_alloc_secure(X)

#define mpi_free_limb_space(X)	gcry_free(X)

/* Divide the two-limb number in (NH,,NL) by D, with DI being the largest
 * limb not larger than (2**(2*BITS_PER_MP_LIMB))/D - (2**BITS_PER_MP_LIMB).
 * If this would yield overflow, DI should be the largest possible number
 * (i.e., only ones).  For correct operation, the most significant bit of D
 * has to be set.  Put the quotient in Q and the remainder in R.
 */
#define UDIV_QRNND_PREINV(q, r, nh, nl, d, di) \
    do {                                                            \
        mpi_limb_t _q, _ql, _r;                                     \
        mpi_limb_t _xh, _xl;                                        \
        umul_ppmm (_q, _ql, (nh), (di));                            \
        _q += (nh);     /* DI is 2**BITS_PER_MPI_LIMB too small */  \
        umul_ppmm (_xh, _xl, _q, (d));                              \
        sub_ddmmss (_xh, _r, (nh), (nl), _xh, _xl);                 \
        if( _xh ) {                                                 \
            sub_ddmmss (_xh, _r, _xh, _r, 0, (d));                  \
            _q++;                                                   \
            if( _xh) {                                              \
                sub_ddmmss (_xh, _r, _xh, _r, 0, (d));              \
                _q++;                                               \
            }                                                       \
        }                                                           \
        if( _r >= (d) ) {                                           \
            _r -= (d);                                              \
            _q++;                                                   \
        }                                                           \
        (r) = _r;                                                   \
        (q) = _q;                                                   \
    } while (0)


/* Define this unconditionally, so it can be used for debugging.  */
#define __udiv_qrnnd_c(q, r, n1, n0, d) \
  do {                                                                  \
    UWtype __d1, __d0, __q1, __q0, __r1, __r0, __m;                     \
    __d1 = __ll_highpart (d);                                           \
    __d0 = __ll_lowpart (d);                                            \
                                                                        \
    __r1 = (n1) % __d1;                                                 \
    __q1 = (n1) / __d1;                                                 \
    __m = (UWtype) __q1 * __d0;                                         \
    __r1 = __r1 * __ll_B | __ll_highpart (n0);                          \
    if (__r1 < __m)                                                     \
      {                                                                 \
        __q1--, __r1 += (d);                                            \
        if (__r1 >= (d)) /* i.e. we didn't get carry when adding to __r1 */\
          if (__r1 < __m)                                               \
            __q1--, __r1 += (d);                                        \
      }                                                                 \
    __r1 -= __m;                                                        \
                                                                        \
    __r0 = __r1 % __d1;                                                 \
    __q0 = __r1 / __d1;                                                 \
    __m = (UWtype) __q0 * __d0;                                         \
    __r0 = __r0 * __ll_B | __ll_lowpart (n0);                           \
    if (__r0 < __m)                                                     \
      {                                                                 \
        __q0--, __r0 += (d);                                            \
        if (__r0 >= (d))                                                \
          if (__r0 < __m)                                               \
            __q0--, __r0 += (d);                                        \
      }                                                                 \
    __r0 -= __m;                                                        \
                                                                        \
    (q) = (UWtype) __q1 * __ll_B | __q0;                                \
    (r) = __r0;                                                         \
  } while (0)


struct gcry_mpi *mpi_alloc_secure( unsigned nlimbs )
{
    struct gcry_mpi *a;

    a = gcry_xmalloc( sizeof *a );
    a->d = nlimbs? mpi_alloc_limb_space( nlimbs, 1 ) : NULL;
    a->alloced = nlimbs;
    a->flags = 1;
    a->nlimbs = 0;
    a->sign = 0;
    return a;
}


mpi_ptr_t mpi_alloc_limb_space( unsigned nlimbs, int secure )
{
    size_t len = nlimbs * sizeof(mpi_limb_t);
    mpi_ptr_t p;

    p = gcry_xmalloc( len );

    return p;
}

void mpi_assign_limb_space( struct gcry_mpi *a, mpi_ptr_t ap, unsigned nlimbs )
{
    mpi_free_limb_space(a->d);
    a->d = ap;
    a->alloced = nlimbs;
}



struct gcry_mpi * gcry_mpi_set_opaque( struct gcry_mpi * a, void *p, unsigned int nbits )
{
    if( !a ) {
        a = mpi_alloc(0);
    }

    if( a->flags & 4 )
        gcry_free( a->d );
    else {
        //mpi_free_limb_space(a->d);
        gcry_free(a->d);
    }

    a->d = p;
    a->alloced = 0;
    a->nlimbs = 0;
    a->sign  = nbits;
    a->flags = 4;
    return a;
}


/****************
 * Note: This copy function should not interpret the MPI
 *       but copy it transparently.
 */
struct gcry_mpi *mpi_copy( struct gcry_mpi * a )
{
    int i; 
    struct gcry_mpi *b;

    if( a && (a->flags & 4) ) {
        void *p = gcry_xmalloc( (a->sign+7)/8 );
        memcpy( p, a->d, (a->sign+7)/8 );
        b = gcry_mpi_set_opaque( NULL, p, a->sign );
    }
    else if( a ) {
        b = mpi_is_secure(a)? mpi_alloc_secure( a->nlimbs )
                            : mpi_alloc( a->nlimbs );
        b->nlimbs = a->nlimbs;
        b->sign = a->sign;
        b->flags  = a->flags;
        for(i=0; i < b->nlimbs; i++ )
            b->d[i] = a->d[i];
    }
    else
        b = NULL;
    return b;
}




mpi_limb_t _gcry_mpih_sub_1(mpi_ptr_t res_ptr,  mpi_ptr_t s1_ptr, size_t s1_size, mpi_limb_t s2_limb )
{
    mpi_limb_t x;

    x = *s1_ptr++;
    s2_limb = x - s2_limb;
    *res_ptr++ = s2_limb;
    if( s2_limb > x ) {
        while( --s1_size ) {
            x = *s1_ptr++;
            *res_ptr++ = x - 1;
            if( x )
                goto leave;
        }
        return 1;
    }

  leave:
    if( res_ptr != s1_ptr ) {
        size_t i;
        for( i=0; i < s1_size-1; i++ )
            res_ptr[i] = s1_ptr[i];
    }
    return 0;
}


/****************
 * Resize the array of A to NLIMBS. the additional space is cleared
 * (set to 0) [done by gcry_realloc()]
 */
static void mpi_resize(struct gcry_mpi *a, unsigned nlimbs )
{
    if( nlimbs <= a->alloced )
        return; /* no need to do it */
    /* Note: a->secure is not used - instead the realloc functions
     * take care of it. Maybe we should drop a->secure completely
     * and rely on a mpi_is_secure function, which would be
     * a wrapper around gcry_is_secure
     */
    if( a->d )
        a->d = gcry_xrealloc(a->d, nlimbs * sizeof(mpi_limb_t) );
    else  /* FIXME: It may not be allocted in secure memory */
        a->d = gcry_xcalloc( nlimbs , sizeof(mpi_limb_t) );
    a->alloced = nlimbs;
}


/****************
 * Subtract the unsigned integer V from the mpi-integer U and store the
 * result in W.
 */
void mpi_sub_ui(struct gcry_mpi *w, struct gcry_mpi *u, unsigned long v )
{
    mpi_ptr_t wp, up;
    size_t usize, wsize;
    int usign, wsign;

    usize = u->nlimbs;
    usign = u->sign;
    wsign = 0;

    /* If not space for W (and possible carry), increase space.  */
    wsize = usize + 1;
    if( w->alloced < wsize )
        mpi_resize(w, wsize);

    /* These must be after realloc (U may be the same as W).  */
    up = u->d;
    wp = w->d;

    if( !usize ) {  /* simple */
        wp[0] = v;
        wsize = v? 1:0;
        wsign = 1;
    }
    else if( usign ) {  /* mpi and v are negative */
        mpi_limb_t cy;
        cy = _gcry_mpih_add_1(wp, up, usize, v);
        wp[usize] = cy;
        wsize = usize + cy;
    }
    else {  /* The signs are different.  Need exact comparison to determine
             * which operand to subtract from which.  */
        if( usize == 1 && up[0] < v ) {
            wp[0] = v - up[0];
            wsize = 1;
            wsign = 1;
        }
        else {
            _gcry_mpih_sub_1(wp, up, usize, v);
            /* Size can decrease with at most one limb. */
            wsize = usize - (wp[usize-1]==0);
        }
    }

    w->nlimbs = wsize;
    w->sign   = wsign;
}

void mpi_free(struct gcry_mpi *a )
{
    if( !a )
        return;
    if( a->flags & 4 )
        gcry_free( a->d );
    else {
	//mpi_free_limb_space(a->d);
        gcry_free(a->d);
    }
//    if( a->flags & ~7 )
//        log_bug("invalid flag value in mpi\n");
    gcry_free(a);
}

void mpi_add(struct gcry_mpi *w, struct gcry_mpi *u, struct gcry_mpi *v)
{
    mpi_ptr_t wp, up, vp;
    size_t usize, vsize, wsize;
    int usign, vsign, wsign;

    if( u->nlimbs < v->nlimbs ) { /* Swap U and V. */
        usize = v->nlimbs;
        usign = v->sign;
        vsize = u->nlimbs;
        vsign = u->sign;
        wsize = usize + 1;
        RESIZE_IF_NEEDED(w, wsize);
        /* These must be after realloc (u or v may be the same as w).  */
        up    = v->d;
        vp    = u->d;
    }
    else {
        usize = u->nlimbs;
        usign = u->sign;
        vsize = v->nlimbs;
        vsign = v->sign;
        wsize = usize + 1;
        RESIZE_IF_NEEDED(w, wsize);
        /* These must be after realloc (u or v may be the same as w).  */
        up    = u->d;
        vp    = v->d;
    }
    wp = w->d;
    wsign = 0;

    if( !vsize ) {  /* simple */
        MPN_COPY(wp, up, usize );
        wsize = usize;
        wsign = usign;
    }
    else if( usign != vsign ) { /* different sign */
        /* This test is right since USIZE >= VSIZE */
        if( usize != vsize ) {
            _gcry_mpih_sub(wp, up, usize, vp, vsize);
            wsize = usize;
            MPN_NORMALIZE(wp, wsize);
            wsign = usign;
        }
        else if( _gcry_mpih_cmp(up, vp, usize) < 0 ) {
            _gcry_mpih_sub_n(wp, vp, up, usize);
            wsize = usize;
            MPN_NORMALIZE(wp, wsize);
            if( !usign )
                wsign = 1;
        }
        else {
            _gcry_mpih_sub_n(wp, up, vp, usize);
            wsize = usize;
            MPN_NORMALIZE(wp, wsize);
            if( usign )
                wsign = 1;
        }
    }
    else { /* U and V have same sign. Add them. */
        mpi_limb_t cy = _gcry_mpih_add(wp, up, usize, vp, vsize);
        wp[usize] = cy;
        wsize = usize + cy;
        if( usign )
            wsign = 1;
    }

    w->nlimbs = wsize;
    w->sign = wsign;
}


/****************
 * Divide (DIVIDEND_PTR,,DIVIDEND_SIZE) by DIVISOR_LIMB.
 * Write DIVIDEND_SIZE limbs of quotient at QUOT_PTR.
 * Return the single-limb remainder. 
 * There are no constraints on the value of the divisor.
 *              
 * QUOT_PTR and DIVIDEND_PTR might point to the same limb.
 */                      
                              
mpi_limb_t      
_gcry_mpih_divmod_1( mpi_ptr_t quot_ptr,
                        mpi_ptr_t dividend_ptr, mpi_size_t dividend_size,
                        mpi_limb_t divisor_limb)
{                              
    mpi_size_t i;
    mpi_limb_t n1, n0, r;
    int dummy;
            
    if( !dividend_size )
        return 0;

    /* If multiplication is much faster than division, and the
     * dividend is large, pre-invert the divisor, and use
     * only multiplications in the inner loop.
     *      
     * This test should be read:
     * Does it ever help to use udiv_qrnnd_preinv?
     * && Does what we save compensate for the inversion overhead?
     */                    
    if( UDIV_TIME > (2 * UMUL_TIME + 6)
        && (UDIV_TIME - (2 * UMUL_TIME + 6)) * dividend_size > UDIV_TIME ) {
        int normalization_steps;

        count_leading_zeros( normalization_steps, divisor_limb );
        if( normalization_steps ) {
            mpi_limb_t divisor_limb_inverted;
                
            divisor_limb <<= normalization_steps;
            
            /* Compute (2**2N - 2**N * DIVISOR_LIMB) / DIVISOR_LIMB.  The
             * result is a (N+1)-bit approximation to 1/DIVISOR_LIMB, with the
             * most significant bit (with weight 2**N) implicit.
             */
            /* Special case for DIVISOR_LIMB == 100...000.  */
            if( !(divisor_limb << 1) )
                divisor_limb_inverted = ~(mpi_limb_t)0;
            else
                udiv_qrnnd(divisor_limb_inverted, dummy,
                           -divisor_limb, 0, divisor_limb);

            n1 = dividend_ptr[dividend_size - 1];
            r = n1 >> (BITS_PER_MPI_LIMB - normalization_steps);

            /* Possible optimization:
             * if (r == 0
             * && divisor_limb > ((n1 << normalization_steps)
             *                 | (dividend_ptr[dividend_size - 2] >> ...)))
             * ...one division less...
             */
            for( i = dividend_size - 2; i >= 0; i--) {
                n0 = dividend_ptr[i];
                UDIV_QRNND_PREINV( quot_ptr[i + 1], r, r,
                                   ((n1 << normalization_steps)
                         | (n0 >> (BITS_PER_MPI_LIMB - normalization_steps))),
                              divisor_limb, divisor_limb_inverted);
                n1 = n0;
            }
            UDIV_QRNND_PREINV( quot_ptr[0], r, r,
                               n1 << normalization_steps,
                               divisor_limb, divisor_limb_inverted);
            return r >> normalization_steps;
        }
        else {
            mpi_limb_t divisor_limb_inverted;

            /* Compute (2**2N - 2**N * DIVISOR_LIMB) / DIVISOR_LIMB.  The
             * result is a (N+1)-bit approximation to 1/DIVISOR_LIMB, with the
             * most significant bit (with weight 2**N) implicit.
             */
            /* Special case for DIVISOR_LIMB == 100...000.  */
            if( !(divisor_limb << 1) )
                divisor_limb_inverted = ~(mpi_limb_t) 0;
            else
                udiv_qrnnd(divisor_limb_inverted, dummy,
                           -divisor_limb, 0, divisor_limb);

            i = dividend_size - 1;
            r = dividend_ptr[i];

            if( r >= divisor_limb )
                r = 0;
            else
                quot_ptr[i--] = 0;

            for( ; i >= 0; i-- ) {
                n0 = dividend_ptr[i];
                UDIV_QRNND_PREINV( quot_ptr[i], r, r,
                                   n0, divisor_limb, divisor_limb_inverted);
            }
            return r;
        }
    }
    else {
        if(UDIV_NEEDS_NORMALIZATION) {
            int normalization_steps;

            count_leading_zeros (normalization_steps, divisor_limb);
            if( normalization_steps ) {
                divisor_limb <<= normalization_steps;
 
                n1 = dividend_ptr[dividend_size - 1];
                r = n1 >> (BITS_PER_MPI_LIMB - normalization_steps);
 
                /* Possible optimization:
                 * if (r == 0
                 * && divisor_limb > ((n1 << normalization_steps)
                 *                 | (dividend_ptr[dividend_size - 2] >> ...)))
                 * ...one division less...
                 */
                for( i = dividend_size - 2; i >= 0; i--) {
                    n0 = dividend_ptr[i];
                    udiv_qrnnd (quot_ptr[i + 1], r, r,
                             ((n1 << normalization_steps)
                         | (n0 >> (BITS_PER_MPI_LIMB - normalization_steps))),
                                divisor_limb);
                    n1 = n0;
                }
                udiv_qrnnd (quot_ptr[0], r, r,
                            n1 << normalization_steps,
                            divisor_limb);
                return r >> normalization_steps;
            }
        }
        /* No normalization needed, either because udiv_qrnnd doesn't require
         * it, or because DIVISOR_LIMB is already normalized.  */
        i = dividend_size - 1;
        r = dividend_ptr[i];
 
        if(r >= divisor_limb)
            r = 0;
        else
            quot_ptr[i--] = 0;
 
        for(; i >= 0; i--) {
            n0 = dividend_ptr[i];
            udiv_qrnnd( quot_ptr[i], r, r, n0, divisor_limb );
        }
        return r;
    }
}


mpi_limb_t
_gcry_mpih_mod_1(mpi_ptr_t dividend_ptr, mpi_size_t dividend_size,
                                      mpi_limb_t divisor_limb)
{                                     
    mpi_size_t i;
    mpi_limb_t n1, n0, r;
    int dummy; 
    
    /* Botch: Should this be handled at all?  Rely on callers?  */
    if( !dividend_size )
        return 0;
        
    /* If multiplication is much faster than division, and the
     * dividend is large, pre-invert the divisor, and use
     * only multiplications in the inner loop.
     *
     * This test should be read:
     *   Does it ever help to use udiv_qrnnd_preinv?
     *     && Does what we save compensate for the inversion overhead?
     */
    if( UDIV_TIME > (2 * UMUL_TIME + 6)
        && (UDIV_TIME - (2 * UMUL_TIME + 6)) * dividend_size > UDIV_TIME ) {
        int normalization_steps;
        
        count_leading_zeros( normalization_steps, divisor_limb );
        if( normalization_steps ) {
            mpi_limb_t divisor_limb_inverted;
            
            divisor_limb <<= normalization_steps;
            
            /* Compute (2**2N - 2**N * DIVISOR_LIMB) / DIVISOR_LIMB.  The
             * result is a (N+1)-bit approximation to 1/DIVISOR_LIMB, with the
             * most significant bit (with weight 2**N) implicit.
             *
             * Special case for DIVISOR_LIMB == 100...000.
             */
            if( !(divisor_limb << 1) )
                divisor_limb_inverted = ~(mpi_limb_t)0;
            else
                udiv_qrnnd(divisor_limb_inverted, dummy,
                           -divisor_limb, 0, divisor_limb);

            n1 = dividend_ptr[dividend_size - 1];
            r = n1 >> (BITS_PER_MPI_LIMB - normalization_steps);

            /* Possible optimization:
             * if (r == 0
             * && divisor_limb > ((n1 << normalization_steps)
             *                 | (dividend_ptr[dividend_size - 2] >> ...)))
             * ...one division less...
             */
            for( i = dividend_size - 2; i >= 0; i--) {
                n0 = dividend_ptr[i];
                UDIV_QRNND_PREINV(dummy, r, r,
                                   ((n1 << normalization_steps)
                          | (n0 >> (BITS_PER_MPI_LIMB - normalization_steps))),
                          divisor_limb, divisor_limb_inverted);
                n1 = n0;
            }
            UDIV_QRNND_PREINV(dummy, r, r,
                              n1 << normalization_steps,
                              divisor_limb, divisor_limb_inverted);
            return r >> normalization_steps;
        }
        else {
            mpi_limb_t divisor_limb_inverted;

            /* Compute (2**2N - 2**N * DIVISOR_LIMB) / DIVISOR_LIMB.  The
             * result is a (N+1)-bit approximation to 1/DIVISOR_LIMB, with the
             * most significant bit (with weight 2**N) implicit.
             *
             * Special case for DIVISOR_LIMB == 100...000.
             */
            if( !(divisor_limb << 1) )
                divisor_limb_inverted = ~(mpi_limb_t)0;
            else
                udiv_qrnnd(divisor_limb_inverted, dummy,
                            -divisor_limb, 0, divisor_limb);

            i = dividend_size - 1;
            r = dividend_ptr[i];

            if( r >= divisor_limb )
                r = 0;
            else
                i--;

            for( ; i >= 0; i--) {
                n0 = dividend_ptr[i];
                UDIV_QRNND_PREINV(dummy, r, r,
                                  n0, divisor_limb, divisor_limb_inverted);
            }
            return r;
        }
    }
    else {
        if( UDIV_NEEDS_NORMALIZATION ) {
            int normalization_steps;

            count_leading_zeros(normalization_steps, divisor_limb);
            if( normalization_steps ) {
                divisor_limb <<= normalization_steps;

                n1 = dividend_ptr[dividend_size - 1];
                r = n1 >> (BITS_PER_MPI_LIMB - normalization_steps);

                /* Possible optimization:
                 * if (r == 0
                 * && divisor_limb > ((n1 << normalization_steps)
                 *                 | (dividend_ptr[dividend_size - 2] >> ...)))
                 * ...one division less...
                 */
                for(i = dividend_size - 2; i >= 0; i--) {
                    n0 = dividend_ptr[i];
                    udiv_qrnnd (dummy, r, r,
                                ((n1 << normalization_steps)
                         | (n0 >> (BITS_PER_MPI_LIMB - normalization_steps))),
                         divisor_limb);
                    n1 = n0;
                }
                udiv_qrnnd (dummy, r, r,
                            n1 << normalization_steps,
                            divisor_limb);
                return r >> normalization_steps;
            }
        }
        /* No normalization needed, either because udiv_qrnnd doesn't require
         * it, or because DIVISOR_LIMB is already normalized.  */
        i = dividend_size - 1;
        r = dividend_ptr[i];

        if(r >= divisor_limb)
            r = 0;
        else
            i--;

        for(; i >= 0; i--) {
            n0 = dividend_ptr[i];
            udiv_qrnnd (dummy, r, r, n0, divisor_limb);
        }
        return r;
    }
}


/* Divide num (NP/NSIZE) by den (DP/DSIZE) and write
 * the NSIZE-DSIZE least significant quotient limbs at QP
 * and the DSIZE long remainder at NP.  If QEXTRA_LIMBS is
 * non-zero, generate that many fraction bits and append them after the
 * other quotient limbs.
 * Return the most significant limb of the quotient, this is always 0 or 1.
 *
 * Preconditions:
 * 0. NSIZE >= DSIZE.
 * 1. The most significant bit of the divisor must be set.
 * 2. QP must either not overlap with the input operands at all, or
 *    QP + DSIZE >= NP must hold true.  (This means that it's
 *    possible to put the quotient in the high part of NUM, right after the
 *    remainder in NUM.
 * 3. NSIZE >= DSIZE, even if QEXTRA_LIMBS is non-zero.
 */

mpi_limb_t
_gcry_mpih_divrem( mpi_ptr_t qp, mpi_size_t qextra_limbs,
                      mpi_ptr_t np, mpi_size_t nsize,
                      mpi_ptr_t dp, mpi_size_t dsize)
{                     
    mpi_limb_t most_significant_q_limb = 0;
    
    switch(dsize) {
      case 0:
        /* We are asked to divide by zero, so go ahead and do it!  (To make
           the compiler not remove this statement, return the value.)  */
        return 1 / dsize;

      case 1:
        {
            mpi_size_t i;
            mpi_limb_t n1;
            mpi_limb_t d;

            d = dp[0];
            n1 = np[nsize - 1];

            if( n1 >= d ) {
                n1 -= d;
                most_significant_q_limb = 1;
            }

            qp += qextra_limbs;
            for( i = nsize - 2; i >= 0; i--)
                udiv_qrnnd( qp[i], n1, n1, np[i], d );
            qp -= qextra_limbs;

            for( i = qextra_limbs - 1; i >= 0; i-- )
                udiv_qrnnd (qp[i], n1, n1, 0, d);

            np[0] = n1;
        }
        break;

      case 2:
        {
            mpi_size_t i;
            mpi_limb_t n1, n0, n2;
            mpi_limb_t d1, d0;

            np += nsize - 2;
            d1 = dp[1];
            d0 = dp[0];
            n1 = np[1];
            n0 = np[0];

            if( n1 >= d1 && (n1 > d1 || n0 >= d0) ) {
                sub_ddmmss (n1, n0, n1, n0, d1, d0);
                most_significant_q_limb = 1;
            }

            for( i = qextra_limbs + nsize - 2 - 1; i >= 0; i-- ) {
                mpi_limb_t q;
                mpi_limb_t r;

                if( i >= qextra_limbs )
                    np--;
                else
                    np[0] = 0;

                if( n1 == d1 ) {
                    /* Q should be either 111..111 or 111..110.  Need special
                     * treatment of this rare case as normal division would
                     * give overflow.  */
                    q = ~(mpi_limb_t)0;

                    r = n0 + d1;
                    if( r < d1 ) {   /* Carry in the addition? */
                        add_ssaaaa( n1, n0, r - d0, np[0], 0, d0 );
                        qp[i] = q;
                        continue;
                    }
                    n1 = d0 - (d0 != 0?1:0);
                    n0 = -d0;
                }
                else {
                    udiv_qrnnd (q, r, n1, n0, d1);
                    umul_ppmm (n1, n0, d0, q);
                }

                n2 = np[0];
              q_test:
                if( n1 > r || (n1 == r && n0 > n2) ) {
                    /* The estimated Q was too large.  */
                    q--;
                    sub_ddmmss (n1, n0, n1, n0, 0, d0);
                    r += d1;
                    if( r >= d1 )    /* If not carry, test Q again.  */
                        goto q_test;
                }

                qp[i] = q;
                sub_ddmmss (n1, n0, r, n2, n1, n0);
            }
            np[1] = n1;
            np[0] = n0;
        }
        break;

      default:
        {
            mpi_size_t i;
            mpi_limb_t dX, d1, n0;

            np += nsize - dsize;
            dX = dp[dsize - 1];
            d1 = dp[dsize - 2];
            n0 = np[dsize - 1];

            if( n0 >= dX ) {
                if(n0 > dX || _gcry_mpih_cmp(np, dp, dsize - 1) >= 0 ) {
                    _gcry_mpih_sub_n(np, np, dp, dsize);
                    n0 = np[dsize - 1];
                    most_significant_q_limb = 1;
                }
            }

            for( i = qextra_limbs + nsize - dsize - 1; i >= 0; i--) {
                mpi_limb_t q;
                mpi_limb_t n1, n2;
                mpi_limb_t cy_limb;

                if( i >= qextra_limbs ) {
                    np--;
                    n2 = np[dsize];
                }
                else {
                    n2 = np[dsize - 1];
                    MPN_COPY_DECR (np + 1, np, dsize - 1);
                    np[0] = 0;
                }

                if( n0 == dX ) {
                    /* This might over-estimate q, but it's probably not worth
                     * the extra code here to find out.  */
                    q = ~(mpi_limb_t)0;
                }
                else {
                    mpi_limb_t r;

                    udiv_qrnnd(q, r, n0, np[dsize - 1], dX);
                    umul_ppmm(n1, n0, d1, q);

                    while( n1 > r || (n1 == r && n0 > np[dsize - 2])) {
                        q--;
                        r += dX;
                        if( r < dX ) /* I.e. "carry in previous addition?" */
                            break;
                        n1 -= n0 < d1;
                        n0 -= d1;
                    }
                }

                /* Possible optimization: We already have (q * n0) and (1 * n1)
                 * after the calculation of q.  Taking advantage of that, we
                 * could make this loop make two iterations less.  */
                cy_limb = _gcry_mpih_submul_1(np, dp, dsize, q);

                if( n2 != cy_limb ) {
                    _gcry_mpih_add_n(np, np, dp, dsize);
                    q--;
                }

                qp[i] = q;
                n0 = np[dsize - 1];
            }
        }
    }

    return most_significant_q_limb;
}




static void _gcry_mpi_tdiv_qr(struct gcry_mpi *quot, struct gcry_mpi *rem, struct gcry_mpi *num, struct gcry_mpi *den)
{
    mpi_ptr_t np, dp;
    mpi_ptr_t qp, rp;
    mpi_size_t nsize = num->nlimbs;
    mpi_size_t dsize = den->nlimbs;
    mpi_size_t qsize, rsize;
    mpi_size_t sign_remainder = num->sign;
    mpi_size_t sign_quotient = num->sign ^ den->sign;
    unsigned normalization_steps;
    mpi_limb_t q_limb;
    mpi_ptr_t marker[5];
    int markidx=0;

    /* Ensure space is enough for quotient and remainder.
     * We need space for an extra limb in the remainder, because it's
     * up-shifted (normalized) below.  */
    rsize = nsize + 1;
    mpi_resize( rem, rsize);

    qsize = rsize - dsize;        /* qsize cannot be bigger than this.  */
    if( qsize <= 0 ) {
        if( num != rem ) {
            rem->nlimbs = num->nlimbs;
            rem->sign = num->sign;
            MPN_COPY(rem->d, num->d, nsize);
        }
        if( quot ) {
            /* This needs to follow the assignment to rem, in case the
             * numerator and quotient are the same.  */
            quot->nlimbs = 0;
            quot->sign = 0;
        }
        return;
    }

    if( quot )
        mpi_resize( quot, qsize);

    /* Read pointers here, when reallocation is finished.  */
    np = num->d;
    dp = den->d;
    rp = rem->d;

    /* Optimize division by a single-limb divisor.  */
    if( dsize == 1 ) {
        mpi_limb_t rlimb;
        if( quot ) {
            qp = quot->d;
            rlimb = _gcry_mpih_divmod_1( qp, np, nsize, dp[0] );
            qsize -= qp[qsize - 1] == 0;
            quot->nlimbs = qsize;
            quot->sign = sign_quotient;
        }
        else
            rlimb = _gcry_mpih_mod_1( np, nsize, dp[0] );
        rp[0] = rlimb;
        rsize = rlimb != 0?1:0;
        rem->nlimbs = rsize;
        rem->sign = sign_remainder;
        return;
    }


    if( quot ) {
        qp = quot->d;
        /* Make sure QP and NP point to different objects.  Otherwise the
         * numerator would be gradually overwritten by the quotient limbs.  */
        if(qp == np) { /* Copy NP object to temporary space.  */
            np = marker[markidx++] = mpi_alloc_limb_space(nsize,
                                                          mpi_is_secure(quot));
            MPN_COPY(np, qp, nsize);
        }
    }
    else /* Put quotient at top of remainder. */
        qp = rp + dsize;

    count_leading_zeros( normalization_steps, dp[dsize - 1] );

    /* Normalize the denominator, i.e. make its most significant bit set by
     * shifting it NORMALIZATION_STEPS bits to the left.  Also shift the
     * numerator the same number of steps (to keep the quotient the same!).
     */
    if( normalization_steps ) {
        mpi_ptr_t tp;
        mpi_limb_t nlimb;

        /* Shift up the denominator setting the most significant bit of
         * the most significant word.  Use temporary storage not to clobber
         * the original contents of the denominator.  */
        tp = marker[markidx++] = mpi_alloc_limb_space(dsize,mpi_is_secure(den));
        _gcry_mpih_lshift( tp, dp, dsize, normalization_steps );
        dp = tp;

        /* Shift up the numerator, possibly introducing a new most
         * significant word.  Move the shifted numerator in the remainder
         * meanwhile.  */
        nlimb = _gcry_mpih_lshift(rp, np, nsize, normalization_steps);
        if( nlimb ) {
            rp[nsize] = nlimb;
            rsize = nsize + 1;
        }
        else
            rsize = nsize;
    }
    else {
        /* The denominator is already normalized, as required.  Copy it to
         * temporary space if it overlaps with the quotient or remainder.  */
        if( dp == rp || (quot && (dp == qp))) {
            mpi_ptr_t tp;

            tp = marker[markidx++] = mpi_alloc_limb_space(dsize, mpi_is_secure(den));
            MPN_COPY( tp, dp, dsize );
            dp = tp;
        }

        /* Move the numerator to the remainder.  */
        if( rp != np )
            MPN_COPY(rp, np, nsize);

        rsize = nsize;
    }

    q_limb = _gcry_mpih_divrem( qp, 0, rp, rsize, dp, dsize );

    if( quot ) {
        qsize = rsize - dsize;
        if(q_limb) {
            qp[qsize] = q_limb;
            qsize += 1;
        }

        quot->nlimbs = qsize;
        quot->sign = sign_quotient;
    }

    rsize = dsize;
    MPN_NORMALIZE (rp, rsize);

    if( normalization_steps && rsize ) {
        _gcry_mpih_rshift(rp, rp, rsize, normalization_steps);
        rsize -= rp[rsize - 1] == 0?1:0;
    }

    rem->nlimbs = rsize;
    rem->sign   = sign_remainder;
    while( markidx )
//        mpi_free_limb_space(marker[--markidx]);
        gcry_free(marker[--markidx]);
}

/* If den == quot, den needs temporary storage.
 * If den == rem, den needs temporary storage.
 * If num == quot, num needs temporary storage.
 * If den has temporary storage, it can be normalized while being copied,
 *   i.e no extra storage should be allocated.
 */

static void _gcry_mpi_tdiv_r(struct gcry_mpi *rem, struct gcry_mpi *num, struct gcry_mpi *den)
{
    _gcry_mpi_tdiv_qr(NULL, rem, num, den );
}



void mpi_fdiv_r(struct gcry_mpi *rem, struct gcry_mpi *dividend, struct gcry_mpi *divisor )
{
    int divisor_sign = divisor->sign;
    struct gcry_mpi *temp_divisor = NULL;

    /* We need the original value of the divisor after the remainder has been
     * preliminary calculated.  We have to copy it to temporary space if it's
     * the same variable as REM.  */
    if( rem == divisor ) {
        temp_divisor = mpi_copy( divisor );
        divisor = temp_divisor;
    }

    _gcry_mpi_tdiv_r( rem, dividend, divisor );

    if( ((divisor_sign?1:0) ^ (dividend->sign?1:0)) && rem->nlimbs )
        mpi_add( rem, rem, divisor);

    if( temp_divisor )
        mpi_free(temp_divisor);
}



void
_gcry_mpih_sqr_n_basecase( mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t size )
{
    mpi_size_t i;
    mpi_limb_t cy_limb;
    mpi_limb_t v_limb;
    
    /* Multiply by the first limb in V separately, as the result can be
     * stored (not added) to PROD.  We also avoid a loop for zeroing.  */
    v_limb = up[0];
    if( v_limb <= 1 ) {
        if( v_limb == 1 )
            MPN_COPY( prodp, up, size );
        else
            MPN_ZERO(prodp, size);
        cy_limb = 0;
    }   
    else
        cy_limb = _gcry_mpih_mul_1( prodp, up, size, v_limb );
        
    prodp[size] = cy_limb;
    prodp++;

    /* For each iteration in the outer loop, multiply one limb from
     * U with one limb from V, and add it to PROD.  */
    for( i=1; i < size; i++) {
        v_limb = up[i];
        if( v_limb <= 1 ) {
            cy_limb = 0;
            if( v_limb == 1 )
                cy_limb = _gcry_mpih_add_n(prodp, prodp, up, size);
        }
        else
            cy_limb = _gcry_mpih_addmul_1(prodp, up, size, v_limb);

        prodp[size] = cy_limb;
        prodp++;
    }
}


/* Multiply the natural numbers u (pointed to by UP) and v (pointed to by VP),
 * both with SIZE limbs, and store the result at PRODP.  2 * SIZE limbs are
 * always stored.  Return the most significant limb.
 *
 * Argument constraints:
 * 1. PRODP != UP and PRODP != VP, i.e. the destination
 *    must be distinct from the multiplier and the multiplicand.
 *
 *
 * Handle simple cases with traditional multiplication.
 *
 * This is the most critical code of multiplication.  All multiplies rely
 * on this, both small and huge.  Small ones arrive here immediately.  Huge
 * ones arrive here as this is the base case for Karatsuba's recursive
 * algorithm below.
 */

static mpi_limb_t
mul_n_basecase( mpi_ptr_t prodp, mpi_ptr_t up,
                                 mpi_ptr_t vp, mpi_size_t size)
{
    mpi_size_t i;
    mpi_limb_t cy;
    mpi_limb_t v_limb;

    /* Multiply by the first limb in V separately, as the result can be
     * stored (not added) to PROD.  We also avoid a loop for zeroing.  */
    v_limb = vp[0];
    if( v_limb <= 1 ) {
        if( v_limb == 1 )
            MPN_COPY( prodp, up, size );
        else
            MPN_ZERO( prodp, size );
        cy = 0;
    }
    else
        cy = _gcry_mpih_mul_1( prodp, up, size, v_limb );

    prodp[size] = cy;
    prodp++;

    /* For each iteration in the outer loop, multiply one limb from
     * U with one limb from V, and add it to PROD.  */
    for( i = 1; i < size; i++ ) {
        v_limb = vp[i];
        if( v_limb <= 1 ) {
            cy = 0;
            if( v_limb == 1 )
               cy = _gcry_mpih_add_n(prodp, prodp, up, size);
        }
        else
            cy = _gcry_mpih_addmul_1(prodp, up, size, v_limb);

        prodp[size] = cy;
        prodp++;
    }

    return cy;
}



void
_gcry_mpih_sqr_n( mpi_ptr_t prodp,
                  mpi_ptr_t up, mpi_size_t size, mpi_ptr_t tspace)
{
    if( size & 1 ) {
        /* The size is odd, and the code below doesn't handle that.
         * Multiply the least significant (size - 1) limbs with a recursive
         * call, and handle the most significant limb of S1 and S2
         * separately.
         * A slightly faster way to do this would be to make the Karatsuba
         * code below behave as if the size were even, and let it check for
         * odd size in the end.  I.e., in essence move this code to the end.
         * Doing so would save us a recursive call, and potentially make the
         * stack grow a lot less.
         */
        mpi_size_t esize = size - 1;       /* even size */
        mpi_limb_t cy_limb;

        MPN_SQR_N_RECURSE( prodp, up, esize, tspace );
        cy_limb = _gcry_mpih_addmul_1( prodp + esize, up, esize, up[esize] );
        prodp[esize + esize] = cy_limb;
        cy_limb = _gcry_mpih_addmul_1( prodp + esize, up, size, up[esize] );

        prodp[esize + size] = cy_limb;
    }
    else {
        mpi_size_t hsize = size >> 1;
        mpi_limb_t cy;

        /* Product H.      ________________  ________________
         *                |_____U1 x U1____||____U0 x U0_____|
         * Put result in upper part of PROD and pass low part of TSPACE
         * as new TSPACE.
         */
        MPN_SQR_N_RECURSE(prodp + size, up + hsize, hsize, tspace);

        /* Product M.      ________________
         *                |_(U1-U0)(U0-U1)_|
         */
        if( _gcry_mpih_cmp( up + hsize, up, hsize) >= 0 )
            _gcry_mpih_sub_n( prodp, up + hsize, up, hsize);
        else
            _gcry_mpih_sub_n (prodp, up, up + hsize, hsize);

        /* Read temporary operands from low part of PROD.
         * Put result in low part of TSPACE using upper part of TSPACE
         * as new TSPACE.  */
        MPN_SQR_N_RECURSE(tspace, prodp, hsize, tspace + size);

        /* Add/copy product H  */
        MPN_COPY(prodp + hsize, prodp + size, hsize);
        cy = _gcry_mpih_add_n(prodp + size, prodp + size,
                           prodp + size + hsize, hsize);

        /* Add product M (if NEGFLG M is a negative number).  */
        cy -= _gcry_mpih_sub_n (prodp + hsize, prodp + hsize, tspace, size);

        /* Product L.      ________________  ________________
         *                |________________||____U0 x U0_____|
         * Read temporary operands from low part of PROD.
         * Put result in low part of TSPACE using upper part of TSPACE
         * as new TSPACE.  */
        MPN_SQR_N_RECURSE (tspace, up, hsize, tspace + size);

        /* Add/copy Product L (twice).  */
        cy += _gcry_mpih_add_n (prodp + hsize, prodp + hsize, tspace, size);
        if( cy )
            _gcry_mpih_add_1(prodp + hsize + size, prodp + hsize + size,
                                                            hsize, cy);

        MPN_COPY(prodp, tspace, hsize);
        cy = _gcry_mpih_add_n (prodp + hsize, prodp + hsize, tspace + hsize, hsize);
        if( cy )
            _gcry_mpih_add_1 (prodp + size, prodp + size, size, 1);
    }
}


static void
mul_n( mpi_ptr_t prodp, mpi_ptr_t up, mpi_ptr_t vp,
                        mpi_size_t size, mpi_ptr_t tspace )
{
    if( size & 1 ) {
      /* The size is odd, and the code below doesn't handle that.
       * Multiply the least significant (size - 1) limbs with a recursive
       * call, and handle the most significant limb of S1 and S2
       * separately.
       * A slightly faster way to do this would be to make the Karatsuba
       * code below behave as if the size were even, and let it check for
       * odd size in the end.  I.e., in essence move this code to the end.
       * Doing so would save us a recursive call, and potentially make the
       * stack grow a lot less.
       */
      mpi_size_t esize = size - 1;       /* even size */
      mpi_limb_t cy_limb;

      MPN_MUL_N_RECURSE( prodp, up, vp, esize, tspace );
      cy_limb = _gcry_mpih_addmul_1( prodp + esize, up, esize, vp[esize] );
      prodp[esize + esize] = cy_limb;
      cy_limb = _gcry_mpih_addmul_1( prodp + esize, vp, size, up[esize] );
      prodp[esize + size] = cy_limb;
    }
    else {
        /* Anatolij Alekseevich Karatsuba's divide-and-conquer algorithm.
         *
         * Split U in two pieces, U1 and U0, such that
         * U = U0 + U1*(B**n),
         * and V in V1 and V0, such that
         * V = V0 + V1*(B**n).
         *
         * UV is then computed recursively using the identity
         *
         *        2n   n          n                     n
         * UV = (B  + B )U V  +  B (U -U )(V -V )  +  (B + 1)U V
         *                1 1        1  0   0  1              0 0
         *
         * Where B = 2**BITS_PER_MP_LIMB.
         */
        mpi_size_t hsize = size >> 1;
        mpi_limb_t cy;
        int negflg;

        /* Product H.      ________________  ________________
         *                |_____U1 x V1____||____U0 x V0_____|
         * Put result in upper part of PROD and pass low part of TSPACE
         * as new TSPACE.
         */
        MPN_MUL_N_RECURSE(prodp + size, up + hsize, vp + hsize, hsize, tspace);

        /* Product M.      ________________
         *                |_(U1-U0)(V0-V1)_|
         */
        if( _gcry_mpih_cmp(up + hsize, up, hsize) >= 0 ) {
            _gcry_mpih_sub_n(prodp, up + hsize, up, hsize);
            negflg = 0;
        }
        else {
            _gcry_mpih_sub_n(prodp, up, up + hsize, hsize);
            negflg = 1;
        }
        if( _gcry_mpih_cmp(vp + hsize, vp, hsize) >= 0 ) {
            _gcry_mpih_sub_n(prodp + hsize, vp + hsize, vp, hsize);
            negflg ^= 1;
        }
        else {
            _gcry_mpih_sub_n(prodp + hsize, vp, vp + hsize, hsize);
            /* No change of NEGFLG.  */
        }
        /* Read temporary operands from low part of PROD.
         * Put result in low part of TSPACE using upper part of TSPACE
         * as new TSPACE.
         */
        MPN_MUL_N_RECURSE(tspace, prodp, prodp + hsize, hsize, tspace + size);

        /* Add/copy product H. */
        MPN_COPY (prodp + hsize, prodp + size, hsize);
        cy = _gcry_mpih_add_n( prodp + size, prodp + size,
                            prodp + size + hsize, hsize);

        /* Add product M (if NEGFLG M is a negative number) */
        if(negflg)
            cy -= _gcry_mpih_sub_n(prodp + hsize, prodp + hsize, tspace, size);
        else
            cy += _gcry_mpih_add_n(prodp + hsize, prodp + hsize, tspace, size);

        /* Product L.      ________________  ________________
         *                |________________||____U0 x V0_____|
         * Read temporary operands from low part of PROD.
         * Put result in low part of TSPACE using upper part of TSPACE
         * as new TSPACE.
         */
        MPN_MUL_N_RECURSE(tspace, up, vp, hsize, tspace + size);

        /* Add/copy Product L (twice) */

        cy += _gcry_mpih_add_n(prodp + hsize, prodp + hsize, tspace, size);
        if( cy )
          _gcry_mpih_add_1(prodp + hsize + size, prodp + hsize + size, hsize, cy);

        MPN_COPY(prodp, tspace, hsize);
        cy = _gcry_mpih_add_n(prodp + hsize, prodp + hsize, tspace + hsize, hsize);
        if( cy )
            _gcry_mpih_add_1(prodp + size, prodp + size, size, 1);
    }
}


void
_gcry_mpih_mul_karatsuba_case( mpi_ptr_t prodp,
                                  mpi_ptr_t up, mpi_size_t usize,
                                  mpi_ptr_t vp, mpi_size_t vsize,
                                  struct karatsuba_ctx *ctx )
{
    mpi_limb_t cy;

    if( !ctx->tspace || ctx->tspace_size < vsize ) {
        if( ctx->tspace )
            mpi_free_limb_space( ctx->tspace );
        ctx->tspace = mpi_alloc_limb_space( 2 * vsize,
                                       gcry_is_secure( up ) || gcry_is_secure( vp ) );
        ctx->tspace_size = vsize;
    }

    MPN_MUL_N_RECURSE( prodp, up, vp, vsize, ctx->tspace );

    prodp += vsize;
    up += vsize;
    usize -= vsize;
    if( usize >= vsize ) {
        if( !ctx->tp || ctx->tp_size < vsize ) {
            if( ctx->tp )
                mpi_free_limb_space( ctx->tp );
            ctx->tp = mpi_alloc_limb_space( 2 * vsize, gcry_is_secure( up )
                                                      || gcry_is_secure( vp ) );
            ctx->tp_size = vsize;
        }

        do {
            MPN_MUL_N_RECURSE( ctx->tp, up, vp, vsize, ctx->tspace );
            cy = _gcry_mpih_add_n( prodp, prodp, ctx->tp, vsize );
            _gcry_mpih_add_1( prodp + vsize, ctx->tp + vsize, vsize, cy );
            prodp += vsize;
            up += vsize;
            usize -= vsize;
        } while( usize >= vsize );
    }

    if( usize ) {
        if( usize < KARATSUBA_THRESHOLD ) {
            _gcry_mpih_mul( ctx->tspace, vp, vsize, up, usize );
        }
        else {
            if( !ctx->next ) {
                ctx->next = gcry_xcalloc( 1, sizeof *ctx );
            }
            _gcry_mpih_mul_karatsuba_case( ctx->tspace,
                                        vp, vsize,
                                        up, usize,
                                        ctx->next );
        }

        cy = _gcry_mpih_add_n( prodp, prodp, ctx->tspace, vsize);
        _gcry_mpih_add_1( prodp + vsize, ctx->tspace + vsize, usize, cy );
    }
}


void
_gcry_mpih_release_karatsuba_ctx( struct karatsuba_ctx *ctx )
{
    struct karatsuba_ctx *ctx2;

    if( ctx->tp )
        mpi_free_limb_space( ctx->tp );
    if( ctx->tspace )
        mpi_free_limb_space( ctx->tspace );
    for( ctx=ctx->next; ctx; ctx = ctx2 ) {
        ctx2 = ctx->next;
        if( ctx->tp )
            mpi_free_limb_space( ctx->tp );
        if( ctx->tspace )
            mpi_free_limb_space( ctx->tspace );
        gcry_free( ctx );
    }
}

/* Multiply the natural numbers u (pointed to by UP, with USIZE limbs)
 * and v (pointed to by VP, with VSIZE limbs), and store the result at
 * PRODP.  USIZE + VSIZE limbs are always stored, but if the input
 * operands are normalized.  Return the most significant limb of the
 * result.
 *
 * NOTE: The space pointed to by PRODP is overwritten before finished
 * with U and V, so overlap is an error.
 *
 * Argument constraints:
 * 1. USIZE >= VSIZE.
 * 2. PRODP != UP and PRODP != VP, i.e. the destination
 *    must be distinct from the multiplier and the multiplicand.
 */

mpi_limb_t
_gcry_mpih_mul( mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t usize,
                   mpi_ptr_t vp, mpi_size_t vsize)
{
    mpi_ptr_t prod_endp = prodp + usize + vsize - 1;
    mpi_limb_t cy;
    struct karatsuba_ctx ctx;

    if( vsize < KARATSUBA_THRESHOLD ) {
        mpi_size_t i;
        mpi_limb_t v_limb;

        if( !vsize )
            return 0;

        /* Multiply by the first limb in V separately, as the result can be
         * stored (not added) to PROD.  We also avoid a loop for zeroing.  */
        v_limb = vp[0];
        if( v_limb <= 1 ) {
            if( v_limb == 1 )
                MPN_COPY( prodp, up, usize );
            else
                MPN_ZERO( prodp, usize );
            cy = 0;
        }
        else
            cy = _gcry_mpih_mul_1( prodp, up, usize, v_limb );

        prodp[usize] = cy;
        prodp++;

        /* For each iteration in the outer loop, multiply one limb from
         * U with one limb from V, and add it to PROD.  */
        for( i = 1; i < vsize; i++ ) {
            v_limb = vp[i];
            if( v_limb <= 1 ) {
                cy = 0;
                if( v_limb == 1 )
                   cy = _gcry_mpih_add_n(prodp, prodp, up, usize);
            }
            else
                cy = _gcry_mpih_addmul_1(prodp, up, usize, v_limb);

            prodp[usize] = cy;
            prodp++;
        }

        return cy;
    }

    memset( &ctx, 0, sizeof ctx );
    _gcry_mpih_mul_karatsuba_case( prodp, up, usize, vp, vsize, &ctx );
    _gcry_mpih_release_karatsuba_ctx( &ctx );
    return *prod_endp;
}








/****************
 * RES = BASE ^ EXP mod MOD
 */
void mpi_powm(struct gcry_mpi *res, struct gcry_mpi *base, struct gcry_mpi *exp, struct gcry_mpi *mod)
{
    mpi_ptr_t  rp, ep, mp, bp;
    mpi_size_t esize, msize, bsize, rsize;
    int        esign, msign, bsign, rsign;
    int        esec,  msec,  bsec,  rsec;
    mpi_size_t size;
    int mod_shift_cnt;
    int negative_result;
    mpi_ptr_t mp_marker=NULL, bp_marker=NULL, ep_marker=NULL;
    mpi_ptr_t xp_marker=NULL;
    int assign_rp=0;
    mpi_ptr_t tspace = NULL;
    mpi_size_t tsize=0;   /* to avoid compiler warning */
			  /* fixme: we should check that the warning is void*/

    esize = exp->nlimbs;
    msize = mod->nlimbs;
    size = 2 * msize;
    esign = exp->sign;
    msign = mod->sign;

    esec = mpi_is_secure(exp);
    msec = mpi_is_secure(mod);
    bsec = mpi_is_secure(base);
    rsec = mpi_is_secure(res);

    rp = res->d;
    ep = exp->d;

    if( !msize )
	msize = 1 / msize;	    /* provoke a signal */

    if( !esize ) {
	/* Exponent is zero, result is 1 mod MOD, i.e., 1 or 0
	 * depending on if MOD equals 1.  */
	rp[0] = 1;
	res->nlimbs = (msize == 1 && mod->d[0] == 1) ? 0 : 1;
	res->sign = 0;
	goto leave;
    }

    /* Normalize MOD (i.e. make its most significant bit set) as required by
     * mpn_divrem.  This will make the intermediate values in the calculation
     * slightly larger, but the correct result is obtained after a final
     * reduction using the original MOD value.	*/
    mp = mp_marker = mpi_alloc_limb_space(msize, msec);
    count_leading_zeros( mod_shift_cnt, mod->d[msize-1] );
    if( mod_shift_cnt )
	_gcry_mpih_lshift( mp, mod->d, msize, mod_shift_cnt );
    else
	MPN_COPY( mp, mod->d, msize );

    bsize = base->nlimbs;
    bsign = base->sign;
    if( bsize > msize ) { /* The base is larger than the module. Reduce it. */
	/* Allocate (BSIZE + 1) with space for remainder and quotient.
	 * (The quotient is (bsize - msize + 1) limbs.)  */
	bp = bp_marker = mpi_alloc_limb_space( bsize + 1, bsec );
	MPN_COPY( bp, base->d, bsize );
	/* We don't care about the quotient, store it above the remainder,
	 * at BP + MSIZE.  */
	_gcry_mpih_divrem( bp + msize, 0, bp, bsize, mp, msize );
	bsize = msize;
	/* Canonicalize the base, since we are going to multiply with it
	 * quite a few times.  */
	MPN_NORMALIZE( bp, bsize );
    }
    else
	bp = base->d;

    if( !bsize ) {
	res->nlimbs = 0;
	res->sign = 0;
	goto leave;
    }

    if( res->alloced < size ) {
	/* We have to allocate more space for RES.  If any of the input
	 * parameters are identical to RES, defer deallocation of the old
	 * space.  */
	if( rp == ep || rp == mp || rp == bp ) {
	    rp = mpi_alloc_limb_space( size, rsec );
	    assign_rp = 1;
	}
	else {
	    mpi_resize( res, size );
	    rp = res->d;
	}
    }
    else { /* Make BASE, EXP and MOD not overlap with RES.  */
	if( rp == bp ) {
	    /* RES and BASE are identical.  Allocate temp. space for BASE.  */
	    assert( !bp_marker );
	    bp = bp_marker = mpi_alloc_limb_space( bsize, bsec );
	    MPN_COPY(bp, rp, bsize);
	}
	if( rp == ep ) {
	    /* RES and EXP are identical.  Allocate temp. space for EXP.  */
	    ep = ep_marker = mpi_alloc_limb_space( esize, esec );
	    MPN_COPY(ep, rp, esize);
	}
	if( rp == mp ) {
	    /* RES and MOD are identical.  Allocate temporary space for MOD.*/
	    assert( !mp_marker );
	    mp = mp_marker = mpi_alloc_limb_space( msize, msec );
	    MPN_COPY(mp, rp, msize);
	}
    }

    MPN_COPY( rp, bp, bsize );
    rsize = bsize;
    rsign = bsign;

    {
	mpi_size_t i;
	mpi_ptr_t xp = xp_marker = mpi_alloc_limb_space( 2 * (msize + 1), msec );
	int c;
	mpi_limb_t e;
	mpi_limb_t carry_limb;
	struct karatsuba_ctx karactx;

	memset( &karactx, 0, sizeof karactx );
	negative_result = (ep[0] & 1) && base->sign;

	i = esize - 1;
	e = ep[i];
	count_leading_zeros (c, e);
	e = (e << c) << 1;     /* shift the exp bits to the left, lose msb */
	c = BITS_PER_MPI_LIMB - 1 - c;

	/* Main loop.
	 *
	 * Make the result be pointed to alternately by XP and RP.  This
	 * helps us avoid block copying, which would otherwise be necessary
	 * with the overlap restrictions of _gcry_mpih_divmod. With 50% probability
	 * the result after this loop will be in the area originally pointed
	 * by RP (==RES->d), and with 50% probability in the area originally
	 * pointed to by XP.
	 */

	for(;;) {
	    while( c ) {
		mpi_ptr_t tp;
		mpi_size_t xsize;

		/*mpih_mul_n(xp, rp, rp, rsize);*/
		if( rsize < KARATSUBA_THRESHOLD )
		    _gcry_mpih_sqr_n_basecase( xp, rp, rsize );
		else {
		    if( !tspace ) {
			tsize = 2 * rsize;
			tspace = mpi_alloc_limb_space( tsize, 0 );
		    }
		    else if( tsize < (2*rsize) ) {
			mpi_free_limb_space( tspace );
			tsize = 2 * rsize;
			tspace = mpi_alloc_limb_space( tsize, 0 );
		    }
		    _gcry_mpih_sqr_n( xp, rp, rsize, tspace );
		}

		xsize = 2 * rsize;
		if( xsize > msize ) {
		    _gcry_mpih_divrem(xp + msize, 0, xp, xsize, mp, msize);
		    xsize = msize;
		}

		tp = rp; rp = xp; xp = tp;
		rsize = xsize;

		if( (mpi_limb_signed_t)e < 0 ) {
		    /*mpih_mul( xp, rp, rsize, bp, bsize );*/
		    if( bsize < KARATSUBA_THRESHOLD ) {
			_gcry_mpih_mul( xp, rp, rsize, bp, bsize );
		    }
		    else {
			_gcry_mpih_mul_karatsuba_case(
				     xp, rp, rsize, bp, bsize, &karactx );
		    }

		    xsize = rsize + bsize;
		    if( xsize > msize ) {
			_gcry_mpih_divrem(xp + msize, 0, xp, xsize, mp, msize);
			xsize = msize;
		    }

		    tp = rp; rp = xp; xp = tp;
		    rsize = xsize;
		}
		e <<= 1;
		c--;
	    }

	    i--;
	    if( i < 0 )
		break;
	    e = ep[i];
	    c = BITS_PER_MPI_LIMB;
	}

	/* We shifted MOD, the modulo reduction argument, left MOD_SHIFT_CNT
	 * steps.  Adjust the result by reducing it with the original MOD.
	 *
	 * Also make sure the result is put in RES->d (where it already
	 * might be, see above).
	 */
	if( mod_shift_cnt ) {
	    carry_limb = _gcry_mpih_lshift( res->d, rp, rsize, mod_shift_cnt);
	    rp = res->d;
	    if( carry_limb ) {
		rp[rsize] = carry_limb;
		rsize++;
	    }
	}
	else {
	    MPN_COPY( res->d, rp, rsize);
	    rp = res->d;
	}

	if( rsize >= msize ) {
	    _gcry_mpih_divrem(rp + msize, 0, rp, rsize, mp, msize);
	    rsize = msize;
	}

	/* Remove any leading zero words from the result.  */
	if( mod_shift_cnt )
	    _gcry_mpih_rshift( rp, rp, rsize, mod_shift_cnt);
	MPN_NORMALIZE (rp, rsize);

	_gcry_mpih_release_karatsuba_ctx( &karactx );
    }

    if( negative_result && rsize ) {
	if( mod_shift_cnt )
	    _gcry_mpih_rshift( mp, mp, msize, mod_shift_cnt);
	_gcry_mpih_sub( rp, mp, msize, rp, rsize);
	rsize = msize;
	rsign = msign;
	MPN_NORMALIZE(rp, rsize);
    }
    res->nlimbs = rsize;
    res->sign = rsign;

  leave:
    if( assign_rp ) mpi_assign_limb_space( res, rp, size );
    if( mp_marker ) mpi_free_limb_space( mp_marker );
    if( bp_marker ) mpi_free_limb_space( bp_marker );
    if( ep_marker ) mpi_free_limb_space( ep_marker );
    if( xp_marker ) mpi_free_limb_space( xp_marker );
    if( tspace )    mpi_free_limb_space( tspace );
}


void mpi_sub(struct gcry_mpi *w, struct gcry_mpi *u, struct gcry_mpi *v)
{
    if( w == v ) {
        struct gcry_mpi *vv = mpi_copy(v);
        vv->sign = !vv->sign;
        mpi_add( w, u, vv );
        mpi_free(vv);
    }
    else {
        /* fixme: this is not thread-save (we temp. modify v) */
        v->sign = !v->sign;
        mpi_add( w, u, v );
        v->sign = !v->sign;
    }
}

void _gcry_mpi_fdiv_r(struct gcry_mpi *rem, struct gcry_mpi *dividend, struct gcry_mpi *divisor )
{
    int divisor_sign = divisor->sign;
    struct gcry_mpi * temp_divisor = NULL;

    /* We need the original value of the divisor after the remainder has been
     * preliminary calculated.  We have to copy it to temporary space if it's
     * the same variable as REM.  */
    if( rem == divisor ) {
        temp_divisor = mpi_copy( divisor );
        divisor = temp_divisor;
    }

    _gcry_mpi_tdiv_r( rem, dividend, divisor );

    if( ((divisor_sign?1:0) ^ (dividend->sign?1:0)) && rem->nlimbs )
        mpi_add( rem, rem, divisor);

    if( temp_divisor )
        mpi_free(temp_divisor);
}




void mpi_mul(struct gcry_mpi *w, struct gcry_mpi *u,struct gcry_mpi *v)
{           
    mpi_size_t usize, vsize, wsize;
    mpi_ptr_t up, vp, wp;
    mpi_limb_t cy;
    int usign, vsign, usecure, vsecure, sign_product;
    int assign_wp=0;
    mpi_ptr_t tmp_limb=NULL;
    
        
    if( u->nlimbs < v->nlimbs ) { /* Swap U and V. */
        usize = v->nlimbs;
        usign = v->sign;
        usecure = mpi_is_secure(v);
        up    = v->d;
        vsize = u->nlimbs;
        vsign = u->sign;
        vsecure = mpi_is_secure(u);
        vp    = u->d;
    }
    else {
        usize = u->nlimbs;
        usign = u->sign;
        usecure = mpi_is_secure(u);
        up    = u->d;
        vsize = v->nlimbs;
        vsign = v->sign;
        vsecure = mpi_is_secure(v);
        vp    = v->d;
    }
    sign_product = usign ^ vsign;
    wp = w->d;

    /* Ensure W has space enough to store the result.  */
    wsize = usize + vsize;
    if ( !mpi_is_secure (w) && (mpi_is_secure (u) || mpi_is_secure (v)) ) {
        /* w is not allocated in secure space but u or v is.  To make sure
         * that no temporray results are stored in w, we temporary use 
         * a newly allocated limb space for w */
        wp = mpi_alloc_limb_space( wsize, 1 );
        assign_wp = 2; /* mark it as 2 so that we can later copy it back to
                        * mormal memory */
    }
    else if( w->alloced < wsize ) {
        if( wp == up || wp == vp ) {
            wp = mpi_alloc_limb_space( wsize, mpi_is_secure(w) );
            assign_wp = 1;
        }
        else {
            mpi_resize(w, wsize );
            wp = w->d;
        }
    }
    else { /* Make U and V not overlap with W.  */
        if( wp == up ) {
            /* W and U are identical.  Allocate temporary space for U.  */
            up = tmp_limb = mpi_alloc_limb_space( usize, usecure  );
            /* Is V identical too?  Keep it identical with U.  */
            if( wp == vp )
                vp = up;
            /* Copy to the temporary space.  */
            MPN_COPY( up, wp, usize );
        }
        else if( wp == vp ) {
            /* W and V are identical.  Allocate temporary space for V.  */
            vp = tmp_limb = mpi_alloc_limb_space( vsize, vsecure );
            /* Copy to the temporary space.  */
            MPN_COPY( vp, wp, vsize );
        }
    }

    if( !vsize )
        wsize = 0;
    else {
        cy = _gcry_mpih_mul( wp, up, usize, vp, vsize );
        wsize -= cy? 0:1;
    }

    if( assign_wp ) {
        if (assign_wp == 2) {
            /* copy the temp wp from secure memory back to normal memory */
            mpi_ptr_t tmp_wp = mpi_alloc_limb_space (wsize, 0);
            MPN_COPY (tmp_wp, wp, wsize);
            mpi_free_limb_space (wp);
            wp = tmp_wp;
        }
        mpi_assign_limb_space( w, wp, wsize );
    }
    w->nlimbs = wsize;
    w->sign = sign_product;
    if( tmp_limb )
        mpi_free_limb_space( tmp_limb );
}


void mpi_mulm(struct gcry_mpi *w, struct gcry_mpi *u,struct gcry_mpi *v, struct gcry_mpi *m)
{
    mpi_mul(w, u, v);
    _gcry_mpi_fdiv_r( w, w, m );
}

