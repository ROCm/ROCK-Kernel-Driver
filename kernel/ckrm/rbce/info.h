static char *info =
"1. Magic files\n"
"\t|--rbce_info - read only file detailing how to setup and use RBCE.\n\n"

"\t|--rbce_reclassify - contains nothing. Writing a pid to it reclassifies\n"
"\tthe given task according to the current set of rules.\n"
"\tWriting 0 to it reclassifies all tasks in the system according to the \n"
"\tsurrent set of rules. This is typically done by the user/sysadmin \n"
"\tafter changing/creating rules. \n\n"

"\t|--rbce_state - determines whether RBCE is currently active or inactive.\n"
"\tWriting 1 (0) activates (deactivates) the CE. Reading the file\n"
"\treturns the current state.\n\n"

"\t|--rbce_tag - set tag of the given pid, syntax - \"pid tag\"\n\n"

"2. Rules subdirectory: Each rule of the RBCE is represented by a file in\n"
"/rcfs/ce/rules.\n\n"

"Following are the different attr/value pairs that can be specified.\n\n"

"Note: attr/value pairs must be separated by commas(,) with no space"
"between them\n\n"

"\t<*id> <OP> number      where   <OP>={>,<,=,!}\n"
"\t<*id>={uid,euid,gid,egid}\n\n"

"\tcmd=\"string\" // basename of the command\n\n"

"\tpath=\"/path/to/string\" // full pathname of the command\n\n"

"\targs=\"string\" // argv[1] - argv[argc] of command\n\n"

"\ttag=\"string\" // application tag of the task\n\n"

"\t[+,-]depend=rule_filename\n"
"\t\t\t// used to chain a rule's terms with existing rules\n"
"\t\t\t// to avoid respecifying the latter's rule terms.\n"
"\t\t\t// A rule's dependent rules are evaluated before \n"
"\t\t\t// its rule terms get evaluated.\n"
"\t\t\t//\n"
"\t\t\t// An optional + or - can precede the depend keyword.\n"
"\t\t\t// +depend adds a dependent rule to the tail of the\n"
"\t\t\t// current chain, -depend removes an existing \n"
"\t\t\t// dependent rule\n\n"

"\torder=number // order in which this rule is executed relative to\n"
"\t\t\t// other independent rules.\n"
"\t\t\t// rule with order 1 is checked first and so on.\n"
"\t\t\t// As soon as a rule matches, the class of that rule\n"
"\t\t\t// is returned to Core. So, order really matters.\n"
"\t\t\t// If no order is specified by the user, the next\n"
"\t\t\t// highest available order number is assigned to\n"
"\t\t\t// the rule.\n\n"


"\tclass=\"/rcfs/.../classname\" // target class of this rule.\n"
"\t\t\t// /rcfs all by itself indicates the\n"
"\t\t\t// systemwide default class\n\n"

"\tstate=number // 1 or 0, provides the ability to deactivate a\n"
"\t\t\t// specific rule, if needed.\n\n"

"\tipv4=\"string\"  // ipv4 address in dotted decimal and port\n"
"\t\t\t// e.g. \"127.0.0.1\\80\"\n"
"\t\t\t// e.g. \"*\\80\" for CE to match any address\n"
"\t\t\t// used in socket accept queue classes\n\n"

"\tipv6=\"string\" // ipv6 address in hex and port\n"
"\t\t\t// e.g. \"fe80::4567\\80\"\n"
"\t\t\t// e.g. \"*\\80\" for CE to match any address \n"
"\t\t\t// used in socket accept queue classes\n\n"

"\texample:\n"
"\techo \"uid=100,euid<200,class=/rcfs\" > /rcfs/ce/rules/rule1\n";
