/*
 * linux/fs/nls_euc-jp.c
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/nls.h>
#include <linux/errno.h>

static struct nls_table *p_nls;


#define SS2		(0x8E)		/* Single Shift 2 */
#define SS3		(0x8F)		/* Single Shift 3 */

static int uni2char(const wchar_t uni,
			unsigned char *out, int boundlen)
{
	int n;

	if ( !p_nls )
 		return -EINVAL;
	if ( (n = p_nls->uni2char(uni, out, boundlen)) < 0)
		return n;
	
	/* translate SJIS into EUC-JP */
	if (n == 1) {
		/* JIS X 201 KANA */
		if (0xA1 <= out[0] && out[0] <= 0xDF) {
			if (boundlen <= 1)
				return -ENAMETOOLONG;
			out[1] = out[0];
			out[0] = SS2;
			n = 2;    
		}
	} else if (n == 2) { 
		/* JIS X 208 */

		/* SJIS codes 0xF0xx to 0xFFxx are machine-dependent codes and user-defining characters */
		if (out[0] >= 0xF0) {
			out[0] = 0x81;  /* 'GETA' with SJIS coding */
			out[1] = 0xAC;
		}

		out[0] = (out[0]^0xA0)*2 + 0x5F;
		if (out[1] > 0x9E)
			out[0]++; 
		
		if (out[1] < 0x7F)
			out[1] = out[1] + 0x61;
		else if (out[1] < 0x9F)
			out[1] = out[1] + 0x60;
		else
			out[1] = out[1] + 0x02;
	}
	else
		return -EINVAL;

	return n;
}

static int char2uni(const unsigned char *rawstring, int boundlen,
				wchar_t *uni)
{
	unsigned char sjis_temp[2];
	int euc_offset, n;
	
	if ( !p_nls )
		return -EINVAL;
	if (boundlen <= 0)
		return -ENAMETOOLONG;

	if (boundlen == 1) {
		*uni = rawstring[0];
		return 1;
	}

	/* translate EUC-JP into SJIS */
	if (rawstring[0] > 0x7F) {
		if (rawstring[0] == SS2) {
			/* JIS X 201 KANA */
			sjis_temp[0] = rawstring[1];
			sjis_temp[1] = 0x00;
			euc_offset = 2;
		} else if (rawstring[0] == SS3) {
			/* JIS X 212 */
			sjis_temp[0] = 0x81; /* 'GETA' with SJIS coding */
			sjis_temp[1] = 0xAC;
			euc_offset = 3;
		} else { 
			/* JIS X 208 */
			sjis_temp[0] = ((rawstring[0]-0x5f)/2) ^ 0xA0;
			if (!(rawstring[0]&1))
				sjis_temp[1] = rawstring[1] - 0x02;
			else if (rawstring[1] < 0xE0)
				sjis_temp[1] = rawstring[1] - 0x61;
			else
				sjis_temp[1] = rawstring[1] - 0x60;
			euc_offset = 2;
		}
	} else { 
		/* JIS X 201 ROMAJI */
		sjis_temp[0] = rawstring[0];
		sjis_temp[1] = rawstring[1];
		euc_offset = 1;
	}

	if ( (n = p_nls->char2uni(sjis_temp, boundlen, uni)) < 0)
		return n;

	return euc_offset;
}

static struct nls_table table = {
	"euc-jp",
	uni2char,
	char2uni,
	NULL,
	NULL,
	THIS_MODULE,
};

static int __init init_nls_euc_jp(void)
{
	p_nls = load_nls("cp932");

	if (p_nls) {
		table.charset2upper = p_nls->charset2upper;
		table.charset2lower = p_nls->charset2lower;
		return register_nls(&table);
	}

	return -EINVAL;
}

static void __exit exit_nls_euc_jp(void)
{
	unregister_nls(&table);
	unload_nls(p_nls);
}

module_init(init_nls_euc_jp)
module_exit(exit_nls_euc_jp)

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 *
---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
