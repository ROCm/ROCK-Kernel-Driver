#include <linux/parser.h>
#include <linux/ctype.h>

enum rule_token_t {
	TOKEN_PATH,
	TOKEN_CMD,
	TOKEN_ARGS,
	TOKEN_RUID_EQ,
	TOKEN_RUID_LT,
	TOKEN_RUID_GT,
	TOKEN_RUID_NOT,
	TOKEN_RGID_EQ,
	TOKEN_RGID_LT,
	TOKEN_RGID_GT,
	TOKEN_RGID_NOT,
	TOKEN_EUID_EQ,
	TOKEN_EUID_LT,
	TOKEN_EUID_GT,
	TOKEN_EUID_NOT,
	TOKEN_EGID_EQ,
	TOKEN_EGID_LT,
	TOKEN_EGID_GT,
	TOKEN_EGID_NOT,
	TOKEN_TAG,
	TOKEN_IPV4,
	TOKEN_IPV6,
	TOKEN_DEP,
	TOKEN_DEP_ADD,
	TOKEN_DEP_DEL,
	TOKEN_ORDER,
	TOKEN_CLASS,
	TOKEN_STATE,
	TOKEN_INVALID
};

int token_to_ruleop[TOKEN_INVALID+1] = {
	[ TOKEN_PATH ]		= RBCE_RULE_CMD_PATH,
	[ TOKEN_CMD ]		= RBCE_RULE_CMD,
	[ TOKEN_ARGS ]		= RBCE_RULE_ARGS,
	[ TOKEN_RUID_EQ ]	= RBCE_RULE_REAL_UID,
	[ TOKEN_RUID_LT ]	= RBCE_RULE_REAL_UID,
	[ TOKEN_RUID_GT ]	= RBCE_RULE_REAL_UID,
	[ TOKEN_RUID_NOT ]	= RBCE_RULE_REAL_UID,
	[ TOKEN_RGID_EQ ]	= RBCE_RULE_REAL_GID,
	[ TOKEN_RGID_LT ]	= RBCE_RULE_REAL_GID,
	[ TOKEN_RGID_GT ]	= RBCE_RULE_REAL_GID,
	[ TOKEN_RGID_NOT ]	= RBCE_RULE_REAL_GID,
	[ TOKEN_EUID_EQ ]	= RBCE_RULE_EFFECTIVE_UID,
	[ TOKEN_EUID_LT ]	= RBCE_RULE_EFFECTIVE_UID,
	[ TOKEN_EUID_GT ]	= RBCE_RULE_EFFECTIVE_UID,
	[ TOKEN_EUID_NOT ]	= RBCE_RULE_EFFECTIVE_UID,
	[ TOKEN_EGID_EQ ]	= RBCE_RULE_EFFECTIVE_GID,
	[ TOKEN_EGID_LT ]	= RBCE_RULE_EFFECTIVE_GID,
	[ TOKEN_EGID_GT ]	= RBCE_RULE_EFFECTIVE_GID,
	[ TOKEN_EGID_NOT ]	= RBCE_RULE_EFFECTIVE_GID,
	[ TOKEN_TAG ]		= RBCE_RULE_APP_TAG,
	[ TOKEN_IPV4 ]		= RBCE_RULE_IPV4,
	[ TOKEN_IPV6 ]		= RBCE_RULE_IPV6,
	[ TOKEN_DEP ]		= RBCE_RULE_DEP_RULE,
	[ TOKEN_DEP_ADD ]	= RBCE_RULE_DEP_RULE,
	[ TOKEN_DEP_DEL ]	= RBCE_RULE_DEP_RULE,
	[ TOKEN_ORDER ]		= RBCE_RULE_INVALID,
	[ TOKEN_CLASS ]		= RBCE_RULE_INVALID,
	[ TOKEN_STATE ]		= RBCE_RULE_INVALID,
};

enum op_token {
	TOKEN_OP_EQUAL = RBCE_EQUAL,
	TOKEN_OP_NOT = RBCE_NOT,
	TOKEN_OP_LESS_THAN = RBCE_LESS_THAN,
	TOKEN_OP_GREATER_THAN = RBCE_GREATER_THAN,
	TOKEN_OP_DEP,
	TOKEN_OP_DEP_ADD,
	TOKEN_OP_DEP_DEL,
	TOKEN_OP_ORDER,
	TOKEN_OP_CLASS,
	TOKEN_OP_STATE,
};

enum op_token token_to_operator[TOKEN_INVALID+1] = {
	[ TOKEN_PATH ]		= TOKEN_OP_EQUAL,
	[ TOKEN_CMD ]		= TOKEN_OP_EQUAL,
	[ TOKEN_ARGS ]		= TOKEN_OP_EQUAL,
	[ TOKEN_RUID_EQ ]	= TOKEN_OP_EQUAL,
	[ TOKEN_RUID_LT ]	= TOKEN_OP_LESS_THAN,
	[ TOKEN_RUID_GT ]	= TOKEN_OP_GREATER_THAN,
	[ TOKEN_RUID_NOT ]	= TOKEN_OP_NOT,
	[ TOKEN_RGID_EQ ]	= TOKEN_OP_EQUAL,
	[ TOKEN_RGID_LT ]	= TOKEN_OP_LESS_THAN,
	[ TOKEN_RGID_GT ]	= TOKEN_OP_GREATER_THAN,
	[ TOKEN_RGID_NOT ]	= TOKEN_OP_NOT,
	[ TOKEN_EUID_EQ ]	= TOKEN_OP_EQUAL,
	[ TOKEN_EUID_LT ]	= TOKEN_OP_LESS_THAN,
	[ TOKEN_EUID_GT ]	= TOKEN_OP_GREATER_THAN,
	[ TOKEN_EUID_NOT ]	= TOKEN_OP_NOT,
	[ TOKEN_EGID_EQ ]	= TOKEN_OP_EQUAL,
	[ TOKEN_EGID_LT ]	= TOKEN_OP_LESS_THAN,
	[ TOKEN_EGID_GT ]	= TOKEN_OP_GREATER_THAN,
	[ TOKEN_EGID_NOT ]	= TOKEN_OP_NOT,
	[ TOKEN_TAG ]		= TOKEN_OP_EQUAL,
	[ TOKEN_IPV4 ]		= TOKEN_OP_EQUAL,
	[ TOKEN_IPV6 ]		= TOKEN_OP_EQUAL,
	[ TOKEN_DEP ]		= TOKEN_OP_DEP,
	[ TOKEN_DEP_ADD ]	= TOKEN_OP_DEP_ADD,
	[ TOKEN_DEP_DEL ]	= TOKEN_OP_DEP_DEL,
	[ TOKEN_ORDER ]		= TOKEN_OP_ORDER,
	[ TOKEN_CLASS ]		= TOKEN_OP_CLASS,
	[ TOKEN_STATE ]		= TOKEN_OP_STATE
};

static match_table_t tokens = {
	{TOKEN_PATH,	"path=%s"},
	{TOKEN_CMD,		"cmd=%s"},
	{TOKEN_ARGS,	"args=%s"},
	{TOKEN_RUID_EQ,	"uid=%d"},
	{TOKEN_RUID_LT,	"uid<%d"},
	{TOKEN_RUID_GT,	"uid>%d"},
	{TOKEN_RUID_NOT,"uid!%d"},
	{TOKEN_RGID_EQ,	"gid=%d"},
	{TOKEN_RGID_LT,	"gid<%d"},
	{TOKEN_RGID_GT,	"gid>%d"},
	{TOKEN_RGID_NOT,"gid!d"},
	{TOKEN_EUID_EQ,	"euid=%d"},
	{TOKEN_EUID_LT,	"euid<%d"},
	{TOKEN_EUID_GT,	"euid>%d"},
	{TOKEN_EUID_NOT,"euid!%d"},
	{TOKEN_EGID_EQ,	"egid=%d"},
	{TOKEN_EGID_LT,	"egid<%d"},
	{TOKEN_EGID_GT,	"egid>%d"},
	{TOKEN_EGID_NOT,"egid!%d"},
	{TOKEN_TAG,		"tag=%s"},
	{TOKEN_IPV4,	"ipv4=%s"},
	{TOKEN_IPV6,	"ipv6=%s"},
	{TOKEN_DEP,		"depend=%s"},
	{TOKEN_DEP_ADD,	"+depend=%s"},
	{TOKEN_DEP_DEL,	"-depend=%s"},
	{TOKEN_ORDER,	"order=%d"},
	{TOKEN_CLASS,	"class=%s"},
	{TOKEN_STATE,	"state=%d"},
	{TOKEN_INVALID,	NULL}
};

/*
 * return -EINVAL in case of failures
 * returns number of terms in terms on success.
 * never returns 0.
 */

static int
rules_parse(char *rule_defn, struct rbce_rule_term **rterms,
		int *term_mask)
{
	char *p, *rp = rule_defn;
	int option, i = 0, nterms;
	struct rbce_rule_term *terms;

	*rterms = NULL;
	*term_mask = 0;
	if (!rule_defn)
		return -EINVAL;
	
	nterms = 0;
	while (*rp++) {
		if (*rp == '>' || *rp == '<' || *rp == '=') {
			nterms++;
		}
	}

	if (!nterms) {
		return -EINVAL;
	}

	terms = kmalloc(nterms * sizeof(struct rbce_rule_term), GFP_KERNEL);
	if (!terms) {
		return -ENOMEM;
	}

	while ((p = strsep(&rule_defn, ",")) != NULL) {
		
		substring_t args[MAX_OPT_ARGS];
		int token;

		while (*p && isspace(*p)) p++;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);

		terms[i].op = token_to_ruleop[token];
		terms[i].operator = token_to_operator[token];
		switch (token) {
			
		case TOKEN_PATH:
		case TOKEN_CMD:
		case TOKEN_ARGS:
		case TOKEN_TAG:
		case TOKEN_IPV4:
		case TOKEN_IPV6:
			// all these tokens can be specified only once
			if (*term_mask & (1 << terms[i].op)) {
				nterms = -EINVAL;
				goto out;
			}
			/*FALLTHRU*/
		case TOKEN_CLASS:
		case TOKEN_DEP:
		case TOKEN_DEP_ADD:
		case TOKEN_DEP_DEL:
			terms[i].u.string = args->from;
			break;
			
		case TOKEN_RUID_EQ:
		case TOKEN_RUID_LT:
		case TOKEN_RUID_GT:
		case TOKEN_RUID_NOT:
		case TOKEN_RGID_EQ:
		case TOKEN_RGID_LT:
		case TOKEN_RGID_GT:
		case TOKEN_RGID_NOT:
		case TOKEN_EUID_EQ:
		case TOKEN_EUID_LT:
		case TOKEN_EUID_GT:
		case TOKEN_EUID_NOT:
		case TOKEN_EGID_EQ:
		case TOKEN_EGID_LT:
		case TOKEN_EGID_GT:
		case TOKEN_EGID_NOT:
			// all these tokens can be specified only once
			if (*term_mask & (1 << terms[i].op)) {
				nterms = -EINVAL;
				goto out;
			}
			/*FALLTHRU*/
		case TOKEN_ORDER:
		case TOKEN_STATE:
			if (match_int(args, &option)) {
				nterms = -EINVAL;
				goto out;
			}
			terms[i].u.id = option;
			break;
		default:
			nterms = -EINVAL;
			goto out;
		}
		*term_mask |= (1 << terms[i].op);
		i++;
	}
	*rterms = terms;

out:
	if (nterms < 0) {
		kfree(terms);
		*term_mask = 0;
	}/* else {
		for (i = 0; i < nterms; i++) {
			printk("token: i %d; op %d, operator %d, str %ld\n",
					i, terms[i].op, terms[i].operator, terms[i].u.id);
		}
	} */
	return nterms;
}	
