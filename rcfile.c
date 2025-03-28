


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "defs.h"
#include "rc.h"
#include "hw.h"
#include "path.h"


char *rcpath;

int rc_sourcefile(char *filename)
{
	FILE *f;
	char *name;
	char line[256], *p;

	name = path_search(filename, "r", rcpath);
#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
	fopen_s(&f, name, "r");
#else
	f = fopen(name, "r");
#endif

	if (!f) return -1;

	for (;;)
	{
		if (feof(f)) break;
		fgets(line, sizeof line, f);
		if ((p = strpbrk(line, "#\r\n")))
			*p = 0;
		rc_command(line);
	}
	fclose(f);
	return 0;
}


rcvar_t rcfile_exports[] =
{
	RCV_STRING("rcpath", &rcpath, "path to look for gnuboy.rc"),
	RCV_END
};


