#ifndef FOO_H
#define FOO_H

/* The size of a `unsigned int', as computed by sizeof. */
#define SIZEOF_UNSIGNED_INT 4

/* The size of a `unsigned long', as computed by sizeof. */
#define SIZEOF_UNSIGNED_LONG 4

/* The size of a `unsigned long long', as computed by sizeof. */
#define SIZEOF_UNSIGNED_LONG_LONG 8

/* The size of a `unsigned short', as computed by sizeof. */
#define SIZEOF_UNSIGNED_SHORT 2

#ifndef UMUL_TIME
  #define UMUL_TIME 1
#endif
#ifndef UDIV_TIME
  #define UDIV_TIME UMUL_TIME
#endif

/* If udiv_qrnnd was not defined for this processor, use __udiv_qrnnd_c.  */
#if !defined (udiv_qrnnd)
#define UDIV_NEEDS_NORMALIZATION 1
#define udiv_qrnnd __udiv_qrnnd_c
#endif


#define BYTES_PER_MPI_LIMB  (SIZEOF_UNSIGNED_LONG)

#ifndef BITS_PER_MPI_LIMB
#if BYTES_PER_MPI_LIMB == SIZEOF_UNSIGNED_INT
  typedef unsigned int mpi_limb_t;
  typedef   signed int mpi_limb_signed_t;
#elif BYTES_PER_MPI_LIMB == SIZEOF_UNSIGNED_LONG
  typedef unsigned long int mpi_limb_t;
  typedef   signed long int mpi_limb_signed_t;
#elif BYTES_PER_MPI_LIMB == SIZEOF_UNSIGNED_LONG_LONG
  typedef unsigned long long int mpi_limb_t;
  typedef   signed long long int mpi_limb_signed_t;
#elif BYTES_PER_MPI_LIMB == SIZEOF_UNSIGNED_SHORT
  typedef unsigned short int mpi_limb_t;
  typedef   signed short int mpi_limb_signed_t;
#else
  #error BYTES_PER_MPI_LIMB does not match any C type
#endif
#define BITS_PER_MPI_LIMB    (8*BYTES_PER_MPI_LIMB)
#endif /*BITS_PER_MPI_LIMB*/



#define W_TYPE_SIZE BITS_PER_MPI_LIMB

//typedef unsigned long mpi_limb_t;
//typedef   signed long mpi_libb_signed_t;
typedef mpi_limb_t *mpi_ptr_t; /* pointer to a limb */

typedef mpi_limb_t UWtype;
typedef unsigned int UHWtype;

typedef size_t mpi_size_t;

struct gcry_mpi {
    int alloced;    /* array size (# of allocated limbs) */
    int nlimbs;     /* number of valid limbs */
    int sign;       /* indicates a negative number and is used for opaque
                     * MPIs to store the length */
    unsigned flags; /* bit 0: array must be allocated in secure memory space */
                    /* bit 2: the limb is a pointer to some m_alloced data */
    mpi_limb_t *d;  /* array with the limbs */
};

struct karatsuba_ctx {
    struct karatsuba_ctx *next;
    mpi_ptr_t tspace;
    mpi_size_t tspace_size;
    mpi_ptr_t tp;
    mpi_size_t tp_size;
};

typedef struct gcry_mpi *GcryMPI;
typedef struct gcry_mpi *GCRY_MPI;

#define mpi_get_nlimbs(a)	((a)->nlimbs)
#define mpi_is_neg(a)		((a)->sign)
#define mpi_is_secure(a)	((a) && ((a)->flags&1))



extern struct gcry_mpi *gcry_mpi_new( unsigned int nbits );
extern struct gcry_mpi *mpi_alloc_secure( unsigned nlimbs );
extern void mpi_free(struct gcry_mpi *a );

extern mpi_ptr_t mpi_alloc_limb_space( unsigned nlimbs, int sec );

extern void mpi_sub_ui(struct gcry_mpi *w, struct gcry_mpi *u, unsigned long v);
extern void mpi_add(struct gcry_mpi *w, struct gcry_mpi *u, struct gcry_mpi *v);
extern void mpi_fdiv_r(struct gcry_mpi *rem, struct gcry_mpi *dividend, struct gcry_mpi *divisor);
extern void mpi_powm(struct gcry_mpi *res, struct gcry_mpi *base, struct gcry_mpi *exp, struct gcry_mpi *mod);
extern void mpi_sub(struct gcry_mpi *w, struct gcry_mpi *u, struct gcry_mpi *v);
extern void mpi_mul(struct gcry_mpi *w, struct gcry_mpi *u,struct gcry_mpi *v);
extern void mpi_mulm(struct gcry_mpi *w, struct gcry_mpi *u,struct gcry_mpi *v, struct gcry_mpi *m);

#define assert(x)	

#define RESIZE_IF_NEEDED(a,b) \
    do {                           \
        if( (a)->alloced < (b) )   \
            mpi_resize((a), (b));  \
    } while(0)


/* Copy N limbs from S to D.  */
#define MPN_COPY( d, s, n) \
    do {                                \
        size_t _i;                  \
        for( _i = 0; _i < (n); _i++ )   \
            (d)[_i] = (s)[_i];          \
    } while(0)

/* Zero N limbs at D */
#define MPN_ZERO(d, n) \
    do {                                  \
        int  _i;                          \
        for( _i = 0; _i < (n); _i++ )  \
            (d)[_i] = 0;                    \
    } while (0)


#define MPN_NORMALIZE(d, n)  \
    do {                       \
        while( (n) > 0 ) {     \
            if( (d)[(n)-1] ) \
                break;         \
            (n)--;             \
        }                      \
    } while(0)

#define MPN_COPY_DECR( d, s, n ) \
    do {                                \
        mpi_size_t _i;                  \
        for( _i = (n)-1; _i >= 0; _i--) \
           (d)[_i] = (s)[_i];           \
    } while(0)



#define MPN_MUL_N_RECURSE(prodp, up, vp, size, tspace) \
    do {                                                \
        if( (size) < KARATSUBA_THRESHOLD )              \
            mul_n_basecase (prodp, up, vp, size);       \
        else                                            \
            mul_n (prodp, up, vp, size, tspace);        \
    } while (0);


#define MPN_SQR_N_RECURSE(prodp, up, size, tspace) \
    do {                                            \
        if ((size) < KARATSUBA_THRESHOLD)           \
            _gcry_mpih_sqr_n_basecase (prodp, up, size);         \
        else                                        \
            _gcry_mpih_sqr_n (prodp, up, size, tspace);  \
    } while (0);


#ifndef KARATSUBA_THRESHOLD
    #define KARATSUBA_THRESHOLD 16
#endif


#if !defined (count_leading_zeros)
extern const unsigned char __clz_tab[];
#define MPI_INTERNAL_NEED_CLZ_TAB 1
#define __BITS4 (W_TYPE_SIZE / 4)
#define __ll_B ((UWtype) 1 << (W_TYPE_SIZE / 2))
#define __ll_lowpart(t) ((UWtype) (t) & (__ll_B - 1))
#define __ll_highpart(t) ((UWtype) (t) >> (W_TYPE_SIZE / 2))


#if !defined (add_ssaaaa)
#define add_ssaaaa(sh, sl, ah, al, bh, bl) \
  do {                                                                  \
    UWtype __x;                                                         \
    __x = (al) + (bl);                                                  \
    (sh) = (ah) + (bh) + (__x < (al));                                  \
    (sl) = __x;                                                         \
  } while (0)
#endif


#if !defined (sub_ddmmss)
#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
  do {                                                                  \
    UWtype __x;                                                         \
    __x = (al) - (bl);                                                  \
    (sh) = (ah) - (bh) - (__x > (al));                                  \
    (sl) = __x;                                                         \
  } while (0)
#endif



#if !defined (umul_ppmm)
#define umul_ppmm(w1, w0, u, v)                                         \
  do {                                                                  \
    UWtype __x0, __x1, __x2, __x3;                                      \
    UHWtype __ul, __vl, __uh, __vh;                                     \
    UWtype __u = (u), __v = (v);                                        \
                                                                        \
    __ul = __ll_lowpart (__u);                                          \
    __uh = __ll_highpart (__u);                                         \
    __vl = __ll_lowpart (__v);                                          \
    __vh = __ll_highpart (__v);                                         \
                                                                        \
    __x0 = (UWtype) __ul * __vl;                                        \
    __x1 = (UWtype) __ul * __vh;                                        \
    __x2 = (UWtype) __uh * __vl;                                        \
    __x3 = (UWtype) __uh * __vh;                                        \
                                                                        \
    __x1 += __ll_highpart (__x0);/* this can't give carry */            \
    __x1 += __x2;               /* but this indeed can */               \
    if (__x1 < __x2)            /* did we get it? */                    \
      __x3 += __ll_B;           /* yes, add it in the proper pos. */    \
                                                                        \
    (w1) = __x3 + __ll_highpart (__x1);                                 \
    (w0) = (__ll_lowpart (__x1) << W_TYPE_SIZE/2) + __ll_lowpart (__x0);\
  } while (0)
#endif


#define count_leading_zeros(count, x) \
  do {                                                                  \
    UWtype __xr = (x);                                                  \
    UWtype __a;                                                         \
                                                                        \
    if (W_TYPE_SIZE <= 32)                                              \
      {                                                                 \
        __a = __xr < ((UWtype) 1 << 2*__BITS4)                          \
          ? (__xr < ((UWtype) 1 << __BITS4) ? 0 : __BITS4)              \
          : (__xr < ((UWtype) 1 << 3*__BITS4) ?  2*__BITS4 : 3*__BITS4);\
      }                                                                 \
    else                                                                \
      {                                                                 \
        for (__a = W_TYPE_SIZE - 8; __a > 0; __a -= 8)                  \
          if (((__xr >> __a) & 0xff) != 0)                              \
            break;                                                      \
      }                                                                 \
                                                                        \
    (count) = W_TYPE_SIZE - (__clz_tab[__xr >> __a] + __a);             \
  } while (0)
#endif




extern mpi_limb_t _gcry_mpih_sub_1( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, size_t s1_size, mpi_limb_t s2_limb );
extern mpi_limb_t _gcry_mpih_sub_n( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_ptr_t s2_ptr, size_t size);
extern mpi_limb_t _gcry_mpih_add_n( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_ptr_t s2_ptr, mpi_size_t size);
extern mpi_limb_t _gcry_mpih_lshift( mpi_ptr_t wp, mpi_ptr_t up, mpi_size_t usize, unsigned int cnt);
extern mpi_limb_t _gcry_mpih_rshift( mpi_ptr_t wp, mpi_ptr_t up, mpi_size_t usize, unsigned cnt);
extern mpi_limb_t _gcry_mpih_submul_1( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_size_t s1_size, mpi_limb_t s2_limb);
extern mpi_limb_t _gcry_mpih_mul_1( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_size_t s1_size, mpi_limb_t s2_limb);
extern mpi_limb_t _gcry_mpih_addmul_1( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_size_t s1_size, mpi_limb_t s2_limb);

static __inline__  mpi_limb_t
_gcry_mpih_sub( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, size_t s1_size,
                                mpi_ptr_t s2_ptr, size_t s2_size)
{
    mpi_limb_t cy = 0;

    if( s2_size )
        cy = _gcry_mpih_sub_n(res_ptr, s1_ptr, s2_ptr, s2_size);

    if( s1_size - s2_size )
        cy = _gcry_mpih_sub_1(res_ptr + s2_size, s1_ptr + s2_size,
                                      s1_size - s2_size, cy);
    return cy;
}



static __inline__  mpi_limb_t
_gcry_mpih_add_1( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
               mpi_size_t s1_size, mpi_limb_t s2_limb)
{
    mpi_limb_t x;

    x = *s1_ptr++;
    s2_limb += x;
    *res_ptr++ = s2_limb;
    if( s2_limb < x ) { /* sum is less than the left operand: handle carry */
        while( --s1_size ) {
            x = *s1_ptr++ + 1;  /* add carry */
            *res_ptr++ = x;     /* and store */
            if( x )             /* not 0 (no overflow): we can stop */
                goto leave;
        }
        return 1; /* return carry (size of s1 to small) */
    }

  leave:
    if( res_ptr != s1_ptr ) { /* not the same variable */
        mpi_size_t i;          /* copy the rest */
        for( i=0; i < s1_size-1; i++ )
            res_ptr[i] = s1_ptr[i];
    }
    return 0; /* no carry */
}

static __inline__ mpi_limb_t
_gcry_mpih_add(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_size_t s1_size,
                               mpi_ptr_t s2_ptr, mpi_size_t s2_size)
{
    mpi_limb_t cy = 0;

    if( s2_size )
        cy = _gcry_mpih_add_n( res_ptr, s1_ptr, s2_ptr, s2_size );

    if( s1_size - s2_size )
        cy = _gcry_mpih_add_1( res_ptr + s2_size, s1_ptr + s2_size,
                            s1_size - s2_size, cy);
    return cy;
}




/****************
 * Compare OP1_PTR/OP1_SIZE with OP2_PTR/OP2_SIZE.
 * There are no restrictions on the relative sizes of
 * the two arguments.
 * Return 1 if OP1 > OP2, 0 if they are equal, and -1 if OP1 < OP2.
 */
static __inline__ int
_gcry_mpih_cmp( mpi_ptr_t op1_ptr, mpi_ptr_t op2_ptr, mpi_size_t size )
{
    mpi_size_t i;
    mpi_limb_t op1_word, op2_word;

    for( i = size - 1; i >= 0 ; i--) {
        op1_word = op1_ptr[i];
        op2_word = op2_ptr[i];
        if( op1_word != op2_word )
            goto diff;
    }
    return 0;

  diff:
    /* This can *not* be simplified to
     *   op2_word - op2_word
     * since that expression might give signed overflow.  */
    return (op1_word > op2_word) ? 1 : -1;
}


#endif
