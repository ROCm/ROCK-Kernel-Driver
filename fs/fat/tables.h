struct unicode_value {
	unsigned char uni1;
	unsigned char uni2;
};

extern unsigned char fat_a2alias[];		/* Ascii to alias name conversion table */
extern struct unicode_value fat_a2uni[];	/* Ascii to Unicode conversion table */
extern unsigned char *fat_uni2asc_pg[];

/*
 * Since Linux can't deal with Unicode in filenames, these provide
 * a method to encode the Unicode names in a manner that the vfat
 * filesystem can them decode back to Unicode.  This conversion
 * only occurs when the filesystem was mounted with the 'uni_xlate' mount
 * option.
 */
extern unsigned char fat_uni2code[];
extern unsigned char fat_code2uni[];

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
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
