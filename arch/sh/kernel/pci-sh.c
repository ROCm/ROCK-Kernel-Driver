#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <asm/machvec.h>

void __init pcibios_init(void)
{
	if (sh_mv.mv_init_pci != NULL) {
		sh_mv.mv_init_pci();
	}
}

/* Haven't done anything here as yet */
char * __init pcibios_setup(char *str)
{
	return str;
}

/* We don't have anything here to fixup */
struct pci_fixup pcibios_fixups[] = {
	{0, 0, 0, NULL}
};
