#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/crypto.h>
#include <asm/scatterlist.h>

#define SHA1_DIGEST_SIZE        20

extern int rsa_check_sig(char *sig, u8 *sha1);

static u8 sha1_result[SHA1_DIGEST_SIZE];
int module_check_sig(Elf_Ehdr *hdr, Elf_Shdr *sechdrs, const char *secstrings)
{
	int i;
	unsigned int sig_index = 0;
	static struct crypto_tfm *sha1_tfm;
	char *sig;

	/* Can't use find_sec as it only checks for Alloc bit */
	for (i = 1; i < hdr->e_shnum; i++)
		if (strcmp(secstrings+sechdrs[i].sh_name, "module_sig") == 0) {
			sig_index = i;
			break;
		}
	if (sig_index <= 0)
		return 0;
	printk(KERN_DEBUG "GREG: sig_index = %d sh_size = %d\n", sig_index, sechdrs[sig_index].sh_size);

	sig = (char *)sechdrs[sig_index].sh_addr;
	printk(KERN_DEBUG "GREG: ");
	for (i = 0; i < sechdrs[sig_index].sh_size; ++i)
		printk("%.2x ", (unsigned char)sig[i]);
	printk("\n");

	sha1_tfm = crypto_alloc_tfm("sha1", 0);
	if (sha1_tfm == NULL) {
		printk(KERN_DEBUG "GREG: failed to load transform for sha1\n");
		return 0;
	}

	crypto_digest_init(sha1_tfm);

	for (i = 1; i < hdr->e_shnum; i++) {
		struct scatterlist sg;
		void *temp;
		int size;
		const char *name = secstrings+sechdrs[i].sh_name;

		/* We only care about sections with "text" or "data" in their names */
		if ((strstr(name, "text") == NULL) &&
		    (strstr(name, "data") == NULL))
			continue;
		/* avoid the ".rel.*" sections too. */
		if (strstr(name, ".rel.") != NULL)
			continue;

		temp = (void *)sechdrs[i].sh_addr;
		size = sechdrs[i].sh_size;
		printk(KERN_DEBUG "GREG: sha1ing the %s section, size %d\n", name, size);
		do {
			memset(&sg, 0x00, sizeof(struct scatterlist));
			sg.page = virt_to_page(temp);
			sg.offset = offset_in_page(temp);
			sg.length = min((unsigned long)size, (PAGE_SIZE - (unsigned long)sg.offset));
			size -= sg.length;
			temp += sg.length;
			printk(KERN_DEBUG "GREG: page = %p, .offset = %d, .length = %d, temp = %p, size = %d\n",
				sg.page, sg.offset, sg.length, temp, size);
			crypto_digest_update(sha1_tfm, &sg, 1);
		} while (size > 0);
	}
	crypto_digest_final(sha1_tfm, sha1_result);
	crypto_free_tfm(sha1_tfm);
	printk(KERN_DEBUG "GREG: sha1 is: ");
	for (i = 0; i < sizeof(sha1_result); ++i)
		printk("%.2x ", sha1_result[i]);
	printk("\n");

	/* see if this sha1 is really encrypted in the section */
	return rsa_check_sig(sig, &sha1_result[0]);
}


