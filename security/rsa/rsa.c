
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>

#include "gnupg_mpi_mpi.h"

extern char rsa_key_n[];
extern char rsa_key_e[];
extern const int rsa_key_n_size;
extern const int rsa_key_e_size;

static MPI public_key[2];

int rsa_check_sig(char *sig, u8 *sha1)
{
	int nread;

	/* initialize our keys */
	nread = rsa_key_n_size;
	public_key[0] = mpi_read_from_buffer(&rsa_key_n[0], &nread, 0);
	nread = rsa_key_e_size;
	public_key[1] = mpi_read_from_buffer(&rsa_key_e[0], &nread, 0);


	return 0;
}
EXPORT_SYMBOL(rsa_check_sig);


