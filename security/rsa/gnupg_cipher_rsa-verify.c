/* rsa.c  -  RSA function
 *	Copyright (C) 1997, 1998, 1999 by Werner Koch (dd9jn)
 *	Copyright (C) 2000, 2001 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

/* This code uses an algorithm protected by U.S. Patent #4,405,829
   which expires on September 20, 2000.  The patent holder placed that
   patent into the public domain on Sep 6th, 2000.
*/
 
#include "gnupg_mpi_mpi.h"
#include "gnupg_cipher_rsa-verify.h"

#define G10ERR_PUBKEY_ALGO   4      /* Unknown pubkey algorithm */
#define G10ERR_BAD_SIGN      8      /* Bad signature */
#define PUBKEY_USAGE_SIG     1	    /* key is good for signatures */
#define PUBKEY_USAGE_ENC     2	    /* key is good for encryption */


typedef struct {
    MPI n;	    /* modulus */
    MPI e;	    /* exponent */
} RSA_public_key;


typedef struct {
    MPI n;	    /* public modulus */
    MPI e;	    /* public exponent */
    MPI d;	    /* exponent */
    MPI p;	    /* prime  p. */
    MPI q;	    /* prime  q. */
    MPI u;	    /* inverse of p mod q. */
} RSA_secret_key;


static void public(MPI output, MPI input, RSA_public_key *skey );


/****************
 * Public key operation. Encrypt INPUT with PKEY and put result into OUTPUT.
 *
 *	c = m^e mod n
 *
 * Where c is OUTPUT, m is INPUT and e,n are elements of PKEY.
 */
static void
public(MPI output, MPI input, RSA_public_key *pkey )
{
    if( output == input ) { /* powm doesn't like output and input the same */
	MPI x = mpi_alloc( mpi_get_nlimbs(input)*2 );
	mpi_powm( x, input, pkey->e, pkey->n );
	mpi_set(output, x);
	mpi_free(x);
    }
    else
	mpi_powm( output, input, pkey->e, pkey->n );
}


/*********************************************
 **************  interface  ******************
 *********************************************/

int
rsa_verify( MPI hash, MPI *data, MPI *pkey)
{
  RSA_public_key pk;
  MPI result;
  int rc;

  pk.n = pkey[0];
  pk.e = pkey[1];
  result = mpi_alloc( (160+(BITS_PER_MPI_LIMB-1))/BITS_PER_MPI_LIMB);
  public( result, data[0], &pk );
  rc = mpi_cmp( result, hash )? G10ERR_BAD_SIGN:0;
  mpi_free(result);

  return rc;
}


int rsa_encrypt(MPI *result, MPI data, MPI *pkey)
{
	RSA_public_key pk;

	pk.n = pkey[0];
	pk.e = pkey[1];
	result[0] = mpi_alloc(mpi_get_nlimbs(pk.n));
	public(result[0], data, &pk);
	return 0;
}





