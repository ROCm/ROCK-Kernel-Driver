#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/unistd.h>

#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/gss_asn1.h>
#include <linux/sunrpc/gss_krb5.h>

/* sec_triples: */
EXPORT_SYMBOL(gss_register_triple);
EXPORT_SYMBOL(gss_unregister_triple);
EXPORT_SYMBOL(gss_cmp_triples);
EXPORT_SYMBOL(gss_pseudoflavor_to_mechOID);
EXPORT_SYMBOL(gss_pseudoflavor_supported);
EXPORT_SYMBOL(gss_pseudoflavor_to_service);
EXPORT_SYMBOL(svcauth_gss_register_pseudoflavor);

/* registering gss mechanisms to the mech switching code: */
EXPORT_SYMBOL(gss_mech_register);
EXPORT_SYMBOL(gss_mech_get);
EXPORT_SYMBOL(gss_mech_get_by_OID);
EXPORT_SYMBOL(gss_mech_put);

/* generic functionality in gss code: */
EXPORT_SYMBOL(g_make_token_header);
EXPORT_SYMBOL(g_verify_token_header);
EXPORT_SYMBOL(g_token_size);
EXPORT_SYMBOL(make_checksum);
EXPORT_SYMBOL(krb5_encrypt);
EXPORT_SYMBOL(krb5_decrypt);

/* debug */
EXPORT_SYMBOL(print_hexl);
