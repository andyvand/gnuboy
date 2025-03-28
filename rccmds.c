#include <string.h>
#include <stdlib.h>

#include "defs.h"
#include "rc.h"
#include "rckeys.h"
#include "hw.h"
#include "emu.h"
#include "loader.h"
#include "split.h"
#include "menu.h"
#include "sys.h"

#ifdef _MSC_VER
#define strncpy(a,b,c) strncpy_s(a,c,b,c)
#endif

/*
 * define the command functions for the controller pad.
 */

CMD_PAD_MACRO(up, UP);
CMD_PAD_MACRO(down, DOWN);
CMD_PAD_MACRO(left, LEFT);
CMD_PAD_MACRO(right, RIGHT);
CMD_PAD_MACRO(a, A);
CMD_PAD_MACRO(b, B);
CMD_PAD_MACRO(start, START);
CMD_PAD_MACRO(select, SELECT);


/*
 * the set command is used to set rc-exported variables.
 */

static int cmd_set(int argc, char **argv)
{
	if (argc < 3)
		return -1;
	return rc_setvar(argv[1], argc-2, argv+2);
}

/*
 * the toggle command is used to switch rc-exported bool variables.
 */

static int cmd_toggle(int argc, char **argv)
{
	static const char* bools[] = {"0", "1", 0};
	if (argc < 2)
		return -1;
	return rc_setvar(argv[1], 1, rc_getint(argv[1])?(void*)bools:(void*)(bools+1));
}


/*
 * the following commands allow keys to be bound to perform rc commands.
 */

static int cmd_bind(int argc, char **argv)
{
	if (argc < 3)
		return -1;
	return rc_bindkey(argv[1], argv[2]);
}

static int cmd_unbind(int argc, char **argv)
{
	if (argc < 2)
		return -1;
	return rc_unbindkey(argv[1]);
}

static int cmd_unbindall(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	rc_unbindall();
	return 0;
}

static int cmd_source(int argc, char **argv)
{
	if (argc < 2)
		return -1;
	return rc_sourcefile(argv[1]);
}

static int cmd_quit(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	exit(0);
	/* NOT REACHED */
}

static int cmd_reset(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	emu_reset();
	return 0;
}

static int cmd_savestate(int argc, char **argv)
{
	state_save(argc > 1 ? atoi(argv[1]) : -1);
	return 0;
}

static int cmd_loadstate(int argc, char **argv)
{
	state_load(argc > 1 ? atoi(argv[1]) : -1);
	return 0;
}

static int cmd_menu(int argc, char **argv)
{
	/* some of the actions we perform from the menu require us
	   to break out of the emu_run main loop, so we just signal
	   it to return and then let main() enter the menu instead. */
	emu_pause(1);
	return 0;
}


/*
 * table of command names and the corresponding functions to be called
 */

rccmd_t rccmds[] =
{
	RCC("set", cmd_set),
	RCC("toggle", cmd_toggle),
	RCC("bind", cmd_bind),
	RCC("unbind", cmd_unbind),
	RCC("unbindall", cmd_unbindall),
	RCC("source", cmd_source),
	RCC("reset", cmd_reset),
	RCC("quit", cmd_quit),
	RCC("menu", cmd_menu),
	RCC("savestate", cmd_savestate),
	RCC("loadstate", cmd_loadstate),
	
	RCC("+up", cmd_up),
	RCC("-up", cmd_up),
	RCC("+down", cmd_down),
	RCC("-down", cmd_down),
	RCC("+left", cmd_left),
	RCC("-left", cmd_left),
	RCC("+right", cmd_right),
	RCC("-right", cmd_right),
	RCC("+a", cmd_a),
	RCC("-a", cmd_a),
	RCC("+b", cmd_b),
	RCC("-b", cmd_b),
	RCC("+start", cmd_start),
	RCC("-start", cmd_start),
	RCC("+select", cmd_select),
	RCC("-select", cmd_select),
	
	RCC_END
};





int rc_command(char *line)
{
	int i, argc, ret;
	char *argv[128], *linecopy;

	linecopy = malloc(strlen(line)+1);
	strncpy(linecopy, line, strlen(line)+1);
	
	argc = splitline(argv, (sizeof argv)/(sizeof argv[0]), linecopy);
	if (!argc)
	{
		free(linecopy);
		return -1;
	}
	
	for (i = 0; rccmds[i].name; i++)
	{
		if (!strcmp(argv[0], rccmds[i].name))
		{
			ret = rccmds[i].func(argc, argv);
			free(linecopy);
			return ret;
		}
	}

	/* printf("unknown command: %s\n", argv[0]); */
	free(linecopy);
	
	return -1;
}























