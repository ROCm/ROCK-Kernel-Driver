
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "foo.h"


mpi_limb_t
_gcry_mpih_sub_n( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
                                  mpi_ptr_t s2_ptr, mpi_size_t size)
{
    mpi_limb_t x, y, cy;
    mpi_size_t j;

    /* The loop counter and index J goes from -SIZE to -1.  This way
       the loop becomes faster.  */
    j = -size;

    /* Offset the base pointers to compensate for the negative indices.  */
    s1_ptr -= j;
    s2_ptr -= j;
    res_ptr -= j;

    cy = 0;
    do {
        y = s2_ptr[j];
        x = s1_ptr[j];
        y += cy;                  /* add previous carry to subtrahend */
        cy = y < cy;              /* get out carry from that addition */
        y = x - y;                /* main subtract */
        cy += y > x;              /* get out carry from the subtract, combine */
        res_ptr[j] = y;
    } while( ++j );

    return cy;
}


mpi_limb_t
_gcry_mpih_add_n( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
               mpi_ptr_t s2_ptr, mpi_size_t size)
{
    mpi_limb_t x, y, cy;
    mpi_size_t j;

    /* The loop counter and index J goes from -SIZE to -1.  This way
       the loop becomes faster.  */
    j = -size;

    /* Offset the base pointers to compensate for the negative indices. */
    s1_ptr -= j;
    s2_ptr -= j;
    res_ptr -= j;

    cy = 0;
    do {
        y = s2_ptr[j];
        x = s1_ptr[j];
        y += cy;                  /* add previous carry to one addend */
        cy = y < cy;              /* get out carry from that addition */
        y += x;                   /* add other addend */
        cy += y < x;              /* get out carry from that add, combine */
        res_ptr[j] = y;
    } while( ++j );

    return cy;
}

/* Shift U (pointed to by UP and USIZE digits long) CNT bits to the left
 * and store the USIZE least significant digits of the result at WP.
 * Return the bits shifted out from the most significant digit.
 *
 * Argument constraints:
 * 1. 0 < CNT < BITS_PER_MP_LIMB
 * 2. If the result is to be written over the input, WP must be >= UP.
 */

mpi_limb_t
_gcry_mpih_lshift( mpi_ptr_t wp, mpi_ptr_t up, mpi_size_t usize,
                                            unsigned int cnt)
{
    mpi_limb_t high_limb, low_limb;
    unsigned sh_1, sh_2;
    mpi_size_t i;
    mpi_limb_t retval;

    sh_1 = cnt;
    wp += 1;
    sh_2 = BITS_PER_MPI_LIMB - sh_1;
    i = usize - 1;
    low_limb = up[i];
    retval = low_limb >> sh_2;
    high_limb = low_limb;
    while( --i >= 0 ) {
        low_limb = up[i];
        wp[i] = (high_limb << sh_1) | (low_limb >> sh_2);
        high_limb = low_limb;
    }
    wp[i] = high_limb << sh_1;

    return retval;
}


/* Shift U (pointed to by UP and USIZE limbs long) CNT bits to the right
 * and store the USIZE least significant limbs of the result at WP.
 * The bits shifted out to the right are returned.
 *
 * Argument constraints:
 * 1. 0 < CNT < BITS_PER_MP_LIMB
 * 2. If the result is to be written over the input, WP must be <= UP.
 */
mpi_limb_t
_gcry_mpih_rshift( mpi_ptr_t wp, mpi_ptr_t up, mpi_size_t usize, unsigned cnt)
{
    mpi_limb_t high_limb, low_limb;
    unsigned sh_1, sh_2;
    mpi_size_t i;
    mpi_limb_t retval;

    sh_1 = cnt;
    wp -= 1;
    sh_2 = BITS_PER_MPI_LIMB - sh_1;
    high_limb = up[0];
    retval = high_limb << sh_2;
    low_limb = high_limb;
    for( i=1; i < usize; i++) {
        high_limb = up[i];
        wp[i] = (low_limb >> sh_1) | (high_limb << sh_2);
        low_limb = high_limb;
    }
    wp[i] = low_limb >> sh_1;

    return retval;
}

mpi_limb_t
_gcry_mpih_submul_1( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
                  mpi_size_t s1_size, mpi_limb_t s2_limb)
{
    mpi_limb_t cy_limb;
    mpi_size_t j;
    mpi_limb_t prod_high, prod_low;
    mpi_limb_t x;

    /* The loop counter and index J goes from -SIZE to -1.  This way
     * the loop becomes faster.  */
    j = -s1_size;
    res_ptr -= j;
    s1_ptr -= j;

    cy_limb = 0;
    do {
        umul_ppmm( prod_high, prod_low, s1_ptr[j], s2_limb);

        prod_low += cy_limb;
        cy_limb = (prod_low < cy_limb?1:0) + prod_high;

        x = res_ptr[j];
        prod_low = x - prod_low;
        cy_limb += prod_low > x?1:0;
        res_ptr[j] = prod_low;
    } while( ++j );

    return cy_limb;
}


mpi_limb_t
_gcry_mpih_mul_1( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_size_t s1_size,
                                                    mpi_limb_t s2_limb)
{
    mpi_limb_t cy_limb;
    mpi_size_t j;
    mpi_limb_t prod_high, prod_low;

    /* The loop counter and index J goes from -S1_SIZE to -1.  This way
     * the loop becomes faster.  */
    j = -s1_size;

    /* Offset the base pointers to compensate for the negative indices.  */
    s1_ptr -= j;
    res_ptr -= j;

    cy_limb = 0;
    do {
        umul_ppmm( prod_high, prod_low, s1_ptr[j], s2_limb );
        prod_low += cy_limb;
        cy_limb = (prod_low < cy_limb?1:0) + prod_high;
        res_ptr[j] = prod_low;
    } while( ++j );

    return cy_limb;
}


mpi_limb_t
_gcry_mpih_addmul_1( mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
                  mpi_size_t s1_size, mpi_limb_t s2_limb)
{
    mpi_limb_t cy_limb;
    mpi_size_t j;
    mpi_limb_t prod_high, prod_low;
    mpi_limb_t x;

    /* The loop counter and index J goes from -SIZE to -1.  This way
     * the loop becomes faster.  */
    j = -s1_size;
    res_ptr -= j;
    s1_ptr -= j;

    cy_limb = 0;
    do {
        umul_ppmm( prod_high, prod_low, s1_ptr[j], s2_limb );

        prod_low += cy_limb;
        cy_limb = (prod_low < cy_limb?1:0) + prod_high;

        x = res_ptr[j];
        prod_low = x + prod_low;
        cy_limb += prod_low < x?1:0;
        res_ptr[j] = prod_low;
    } while ( ++j );
    return cy_limb;
}


