/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 * Introduced single menu mode (show all sub-menus in one large tree).
 * 2002-11-06 Petr Baudis <pasky@ucw.cz>
 */

#include <sys/ioctl.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

static char menu_backtitle[128];
static const char menu_instructions[] =
	"Arrow keys navigate the menu.  "
	"<Enter> selects submenus --->.  "
	"Highlighted letters are hotkeys.  "
	"Pressing <Y> includes, <N> excludes, <M> modularizes features.  "
	"Press <Esc><Esc> to exit, <?> for Help.  "
	"Legend: [*] built-in  [ ] excluded  <M> module  < > module capable",
radiolist_instructions[] =
	"Use the arrow keys to navigate this window or "
	"press the hotkey of the item you wish to select "
	"followed by the <SPACE BAR>. "
	"Press <?> for additional information about this option.",
inputbox_instructions_int[] =
	"Please enter a decimal value. "
	"Fractions will not be accepted.  "
	"Use the <TAB> key to move from the input field to the buttons below it.",
inputbox_instructions_hex[] =
	"Please enter a hexadecimal value. "
	"Use the <TAB> key to move from the input field to the buttons below it.",
inputbox_instructions_string[] =
	"Please enter a string value. "
	"Use the <TAB> key to move from the input field to the buttons below it.",
setmod_text[] =
	"This feature depends on another which has been configured as a module.\n"
	"As a result, this feature will be built as a module.",
nohelp_text[] =
	"There is no help available for this kernel option.\n",
load_config_text[] =
	"Enter the name of the configuration file you wish to load.  "
	"Accept the name shown to restore the configuration you "
	"last retrieved.  Leave blank to abort.",
load_config_help[] =
	"\n"
	"For various reasons, one may wish to keep several different kernel\n"
	"configurations available on a single machine.\n"
	"\n"
	"If you have saved a previous configuration in a file other than the\n"
	"kernel's default, entering the name of the file here will allow you\n"
	"to modify that configuration.\n"
	"\n"
	"If you are uncertain, then you have probably never used alternate\n"
	"configuration files.  You should therefor leave this blank to abort.\n",
save_config_text[] =
	"Enter a filename to which this configuration should be saved "
	"as an alternate.  Leave blank to abort.",
save_config_help[] =
	"\n"
	"For various reasons, one may wish to keep different kernel\n"
	"configurations available on a single machine.\n"
	"\n"
	"Entering a file name here will allow you to later retrieve, modify\n"
	"and use the current configuration as an alternate to whatever\n"
	"configuration options you have selected at that time.\n"
	"\n"
	"If you are uncertain what all this means then you should probably\n"
	"leave this blank.\n"
;

static char buf[4096], *bufptr = buf;
static char input_buf[4096];
static char filename[PATH_MAX+1] = ".config";
static char *args[1024], **argptr = args;
static int indent;
static struct termios ios_org;
static int rows, cols;
static struct menu *current_menu;
static int child_count;
static int do_resize;
static int single_menu_mode;

static void conf(struct menu *menu);
static void conf_choice(struct menu *menu);
static void conf_string(struct menu *menu);
static void conf_load(void);
static void conf_save(void);
static void show_textbox(const char *title, const char *text, int r, int c);
static void show_helptext(const char *title, const char *text);
static void show_help(struct menu *menu);
static void show_readme(void);

static void cprint_init(void);
static int cprint1(const char *fmt, ...);
static void cprint_done(void);
static int cprint(const char *fmt, ...);

static void init_wsize(void)
{
	struct winsize ws;
	char *env;

	if (ioctl(1, TIOCGWINSZ, &ws) == -1) {
		rows = 24;
		cols = 80;
	} else {
		rows = ws.ws_row;
		cols = ws.ws_col;
		if (!rows) {
			env = getenv("LINES");
			if (env)
				rows = atoi(env);
			if (!rows)
				rows = 24;
		}
		if (!cols) {
			env = getenv("COLUMNS");
			if (env)
				cols = atoi(env);
			if (!cols)
				cols = 80;
		}
	}

	if (rows < 19 || cols < 80) {
		fprintf(stderr, "Your display is too small to run Menuconfig!\n");
		fprintf(stderr, "It must be at least 19 lines by 80 columns.\n");
		exit(1);
	}

	rows -= 4;
	cols -= 5;
}

static void cprint_init(void)
{
	bufptr = buf;
	argptr = args;
	memset(args, 0, sizeof(args));
	indent = 0;
	child_count = 0;
	cprint("./scripts/lxdialog/lxdialog");
	cprint("--backtitle");
	cprint(menu_backtitle);
}

static int cprint1(const char *fmt, ...)
{
	va_list ap;
	int res;

	if (!*argptr)
		*argptr = bufptr;
	va_start(ap, fmt);
	res = vsprintf(bufptr, fmt, ap);
	va_end(ap);
	bufptr += res;

	return res;
}

static void cprint_done(void)
{
	*bufptr++ = 0;
	argptr++;
}

static int cprint(const char *fmt, ...)
{
	va_list ap;
	int res;

	*argptr++ = bufptr;
	va_start(ap, fmt);
	res = vsprintf(bufptr, fmt, ap);
	va_end(ap);
	bufptr += res;
	*bufptr++ = 0;

	return res;
}

pid_t pid;

static void winch_handler(int sig)
{
	if (!do_resize) {
		kill(pid, SIGINT);
		do_resize = 1;
	}
}

static int exec_conf(void)
{
	int pipefd[2], stat, size;
	struct sigaction sa;
	sigset_t sset, osset;

	sigemptyset(&sset);
	sigaddset(&sset, SIGINT);
	sigprocmask(SIG_BLOCK, &sset, &osset);

	signal(SIGINT, SIG_DFL);

	sa.sa_handler = winch_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGWINCH, &sa, NULL);

	*argptr++ = NULL;

	pipe(pipefd);
	pid = fork();
	if (pid == 0) {
		sigprocmask(SIG_SETMASK, &osset, NULL);
		dup2(pipefd[1], 2);
		close(pipefd[0]);
		close(pipefd[1]);
		execv(args[0], args);
		_exit(EXIT_FAILURE);
	}

	close(pipefd[1]);
	bufptr = input_buf;
	while (1) {
		size = input_buf + sizeof(input_buf) - bufptr;
		size = read(pipefd[0], bufptr, size);
		if (size <= 0) {
			if (size < 0) {
				if (errno == EINTR || errno == EAGAIN)
					continue;
				perror("read");
			}
			break;
		}
		bufptr += size;
	}
	*bufptr++ = 0;
	close(pipefd[0]);
	waitpid(pid, &stat, 0);

	if (do_resize) {
		init_wsize();
		do_resize = 0;
		sigprocmask(SIG_SETMASK, &osset, NULL);
		return -1;
	}
	if (WIFSIGNALED(stat)) {
		printf("\finterrupted(%d)\n", WTERMSIG(stat));
		exit(1);
	}
#if 0
	printf("\fexit state: %d\nexit data: '%s'\n", WEXITSTATUS(stat), input_buf);
	sleep(1);
#endif
	sigpending(&sset);
	if (sigismember(&sset, SIGINT)) {
		printf("\finterrupted\n");
		exit(1);
	}
	sigprocmask(SIG_SETMASK, &osset, NULL);

	return WEXITSTATUS(stat);
}

static void build_conf(struct menu *menu)
{
	struct symbol *sym;
	struct property *prop;
	struct menu *child;
	int type, tmp, doint = 2;
	tristate val;
	char ch;

	if (!menu_is_visible(menu))
		return;

	sym = menu->sym;
	prop = menu->prompt;
	if (!sym) {
		if (prop && menu != current_menu) {
			const char *prompt = menu_get_prompt(menu);
			switch (prop->type) {
			case P_MENU:
				child_count++;
				cprint("m%p", menu);

				if (single_menu_mode) {
					cprint1("%s%*c%s",
						menu->data ? "-->" : "++>",
						indent + 1, ' ', prompt);
				} else
					cprint1("   %*c%s  --->", indent + 1, ' ', prompt);

				cprint_done();
				if (single_menu_mode && menu->data)
					goto conf_childs;
				return;
			default:
				if (prompt) {
					child_count++;
					cprint(":%p", menu);
					cprint("---%*c%s", indent + 1, ' ', prompt);
				}
			}
		} else
			doint = 0;
		goto conf_childs;
	}

	type = sym_get_type(sym);
	if (sym_is_choice(sym)) {
		struct symbol *def_sym = sym_get_choice_value(sym);
		struct menu *def_menu = NULL;

		child_count++;
		for (child = menu->list; child; child = child->next) {
			if (menu_is_visible(child) && child->sym == def_sym)
				def_menu = child;
		}

		val = sym_get_tristate_value(sym);
		if (sym_is_changable(sym)) {
			cprint("t%p", menu);
			switch (type) {
			case S_BOOLEAN:
				cprint1("[%c]", val == no ? ' ' : '*');
				break;
			case S_TRISTATE:
				switch (val) {
				case yes: ch = '*'; break;
				case mod: ch = 'M'; break;
				default:  ch = ' '; break;
				}
				cprint1("<%c>", ch);
				break;
			}
		} else {
			cprint("%c%p", def_menu ? 't' : ':', menu);
			cprint1("   ");
		}

		cprint1("%*c%s", indent + 1, ' ', menu_get_prompt(menu));
		if (val == yes) {
			if (def_menu) {
				cprint1(" (%s)", menu_get_prompt(def_menu));
				cprint1("  --->");
				cprint_done();
				if (def_menu->list) {
					indent += 2;
					build_conf(def_menu);
					indent -= 2;
				}
			} else
				cprint_done();
			return;
		}
		cprint_done();
	} else {
		if (menu == current_menu) {
			cprint(":%p", menu);
			cprint("---%*c%s", indent + 1, ' ', menu_get_prompt(menu));
			goto conf_childs;
		}
		child_count++;
		val = sym_get_tristate_value(sym);
		if (sym_is_choice_value(sym) && val == yes) {
			cprint(":%p", menu);
			cprint1("   ");
		} else {
			switch (type) {
			case S_BOOLEAN:
				cprint("t%p", menu);
				if (sym_is_changable(sym))
					cprint1("[%c]", val == no ? ' ' : '*');
				else
					cprint1("---");
				break;
			case S_TRISTATE:
				cprint("t%p", menu);
				switch (val) {
				case yes: ch = '*'; break;
				case mod: ch = 'M'; break;
				default:  ch = ' '; break;
				}
				if (sym_is_changable(sym))
					cprint1("<%c>", ch);
				else
					cprint1("---");
				break;
			default:
				cprint("s%p", menu);
				tmp = cprint1("(%s)", sym_get_string_value(sym));
				tmp = indent - tmp + 4;
				if (tmp < 0)
					tmp = 0;
				cprint1("%*c%s%s", tmp, ' ', menu_get_prompt(menu),
					(sym_has_value(sym) || !sym_is_changable(sym)) ?
					"" : " (NEW)");
				cprint_done();
				goto conf_childs;
			}
		}
		cprint1("%*c%s%s", indent + 1, ' ', menu_get_prompt(menu),
			(sym_has_value(sym) || !sym_is_changable(sym)) ?
			"" : " (NEW)");
		if (menu->prompt->type == P_MENU) {
			cprint1("  --->");
			cprint_done();
			return;
		}
		cprint_done();
	}

conf_childs:
	indent += doint;
	for (child = menu->list; child; child = child->next)
		build_conf(child);
	indent -= doint;
}

static void conf(struct menu *menu)
{
	struct menu *submenu;
	const char *prompt = menu_get_prompt(menu);
	struct symbol *sym;
	char active_entry[40];
	int stat, type, i;

	unlink("lxdialog.scrltmp");
	active_entry[0] = 0;
	while (1) {
		cprint_init();
		cprint("--title");
		cprint("%s", prompt ? prompt : "Main Menu");
		cprint("--menu");
		cprint(menu_instructions);
		cprint("%d", rows);
		cprint("%d", cols);
		cprint("%d", rows - 10);
		cprint("%s", active_entry);
		current_menu = menu;
		build_conf(menu);
		if (!child_count)
			break;
		if (menu == &rootmenu) {
			cprint(":");
			cprint("--- ");
			cprint("L");
			cprint("    Load an Alternate Configuration File");
			cprint("S");
			cprint("    Save Configuration to an Alternate File");
		}
		stat = exec_conf();
		if (stat < 0)
			continue;

		if (stat == 1 || stat == 255)
			break;

		type = input_buf[0];
		if (!type)
			continue;

		for (i = 0; input_buf[i] && !isspace(input_buf[i]); i++)
			;
		if (i >= sizeof(active_entry))
			i = sizeof(active_entry) - 1;
		input_buf[i] = 0;
		strcpy(active_entry, input_buf);

		sym = NULL;
		submenu = NULL;
		if (sscanf(input_buf + 1, "%p", &submenu) == 1)
			sym = submenu->sym;

		switch (stat) {
		case 0:
			switch (type) {
			case 'm':
				if (single_menu_mode)
					submenu->data = (void *) (long) !submenu->data;
				else
					conf(submenu);
				break;
			case 't':
				if (sym_is_choice(sym) && sym_get_tristate_value(sym) == yes)
					conf_choice(submenu);
				else if (submenu->prompt->type == P_MENU)
					conf(submenu);
				break;
			case 's':
				conf_string(submenu);
				break;
			case 'L':
				conf_load();
				break;
			case 'S':
				conf_save();
				break;
			}
			break;
		case 2:
			if (sym)
				show_help(submenu);
			else
				show_readme();
			break;
		case 3:
			if (type == 't') {
				if (sym_set_tristate_value(sym, yes))
					break;
				if (sym_set_tristate_value(sym, mod))
					show_textbox(NULL, setmod_text, 6, 74);
			}
			break;
		case 4:
			if (type == 't')
				sym_set_tristate_value(sym, no);
			break;
		case 5:
			if (type == 't')
				sym_set_tristate_value(sym, mod);
			break;
		case 6:
			if (type == 't')
				sym_toggle_tristate_value(sym);
			else if (type == 'm')
				conf(submenu);
			break;
		}
	}
}

static void show_textbox(const char *title, const char *text, int r, int c)
{
	int fd;

	fd = creat(".help.tmp", 0777);
	write(fd, text, strlen(text));
	close(fd);
	do {
		cprint_init();
		if (title) {
			cprint("--title");
			cprint("%s", title);
		}
		cprint("--textbox");
		cprint(".help.tmp");
		cprint("%d", r);
		cprint("%d", c);
	} while (exec_conf() < 0);
	unlink(".help.tmp");
}

static void show_helptext(const char *title, const char *text)
{
	show_textbox(title, text, rows, cols);
}

static void show_help(struct menu *menu)
{
	const char *help;
	char *helptext;
	struct symbol *sym = menu->sym;

	help = sym->help;
	if (!help)
		help = nohelp_text;
	if (sym->name) {
		helptext = malloc(strlen(sym->name) + strlen(help) + 16);
		sprintf(helptext, "CONFIG_%s:\n\n%s", sym->name, help);
		show_helptext(menu_get_prompt(menu), helptext);
		free(helptext);
	} else
		show_helptext(menu_get_prompt(menu), help);
}

static void show_readme(void)
{
	do {
		cprint_init();
		cprint("--textbox");
		cprint("scripts/README.Menuconfig");
		cprint("%d", rows);
		cprint("%d", cols);
	} while (exec_conf() == -1);
}

static void conf_choice(struct menu *menu)
{
	const char *prompt = menu_get_prompt(menu);
	struct menu *child;
	struct symbol *active;
	int stat;

	while (1) {
		cprint_init();
		cprint("--title");
		cprint("%s", prompt ? prompt : "Main Menu");
		cprint("--radiolist");
		cprint(radiolist_instructions);
		cprint("15");
		cprint("70");
		cprint("6");

		current_menu = menu;
		active = sym_get_choice_value(menu->sym);
		for (child = menu->list; child; child = child->next) {
			if (!menu_is_visible(child))
				continue;
			cprint("%p", child);
			cprint("%s", menu_get_prompt(child));
			cprint(child->sym == active ? "ON" : "OFF");
		}

		stat = exec_conf();
		switch (stat) {
		case 0:
			if (sscanf(input_buf, "%p", &menu) != 1)
				break;
			sym_set_tristate_value(menu->sym, yes);
			return;
		case 1:
			show_help(menu);
			break;
		case 255:
			return;
		}
	}
}

static void conf_string(struct menu *menu)
{
	const char *prompt = menu_get_prompt(menu);
	int stat;

	while (1) {
		cprint_init();
		cprint("--title");
		cprint("%s", prompt ? prompt : "Main Menu");
		cprint("--inputbox");
		switch (sym_get_type(menu->sym)) {
		case S_INT:
			cprint(inputbox_instructions_int);
			break;
		case S_HEX:
			cprint(inputbox_instructions_hex);
			break;
		case S_STRING:
			cprint(inputbox_instructions_string);
			break;
		default:
			/* panic? */;
		}
		cprint("10");
		cprint("75");
		cprint("%s", sym_get_string_value(menu->sym));
		stat = exec_conf();
		switch (stat) {
		case 0:
			if (sym_set_string_value(menu->sym, input_buf))
				return;
			show_textbox(NULL, "You have made an invalid entry.", 5, 43);
			break;
		case 1:
			show_help(menu);
			break;
		case 255:
			return;
		}
	}
}

static void conf_load(void)
{
	int stat;

	while (1) {
		cprint_init();
		cprint("--inputbox");
		cprint(load_config_text);
		cprint("11");
		cprint("55");
		cprint("%s", filename);
		stat = exec_conf();
		switch(stat) {
		case 0:
			if (!input_buf[0])
				return;
			if (!conf_read(input_buf))
				return;
			show_textbox(NULL, "File does not exist!", 5, 38);
			break;
		case 1:
			show_helptext("Load Alternate Configuration", load_config_help);
			break;
		case 255:
			return;
		}
	}
}

static void conf_save(void)
{
	int stat;

	while (1) {
		cprint_init();
		cprint("--inputbox");
		cprint(save_config_text);
		cprint("11");
		cprint("55");
		cprint("%s", filename);
		stat = exec_conf();
		switch(stat) {
		case 0:
			if (!input_buf[0])
				return;
			if (!conf_write(input_buf))
				return;
			show_textbox(NULL, "Can't create file!  Probably a nonexistent directory.", 5, 60);
			break;
		case 1:
			show_helptext("Save Alternate Configuration", save_config_help);
			break;
		case 255:
			return;
		}
	}
}

static void conf_cleanup(void)
{
	tcsetattr(1, TCSAFLUSH, &ios_org);
	unlink(".help.tmp");
	unlink("lxdialog.scrltmp");
}

int main(int ac, char **av)
{
	struct symbol *sym;
	char *mode;
	int stat;

	conf_parse(av[1]);
	conf_read(NULL);

	sym = sym_lookup("KERNELRELEASE", 0);
	sym_calc_value(sym);
	sprintf(menu_backtitle, "Linux Kernel v%s Configuration",
		sym_get_string_value(sym));

	mode = getenv("MENUCONFIG_MODE");
	if (mode) {
		if (!strcasecmp(mode, "single_menu"))
			single_menu_mode = 1;
	}

	tcgetattr(1, &ios_org);
	atexit(conf_cleanup);
	init_wsize();
	conf(&rootmenu);

	do {
		cprint_init();
		cprint("--yesno");
		cprint("Do you wish to save your new kernel configuration?");
		cprint("5");
		cprint("60");
		stat = exec_conf();
	} while (stat < 0);

	if (stat == 0) {
		conf_write(NULL);
		printf("\n\n"
			"*** End of Linux kernel configuration.\n"
			"*** Execute 'make' to build the kernel or try 'make help'."
			"\n\n");
	} else
		printf("\n\n"
			"Your kernel configuration changes were NOT saved."
			"\n\n");

	return 0;
}
