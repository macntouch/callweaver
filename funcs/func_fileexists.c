/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Reisig Consulting
 *
 * Boris Reisig <boris@boris.ca>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * Check's if a file exists function.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/funcs/func_fileexists.c $", "$Revision: 4723 $")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/cdr.h"

static void *fileexists_function;
static const char *fileexists_func_name = "FILEEXISTS";
static const char *fileexists_func_synopsis = "Checks if a file exists";
static const char *fileexists_func_syntax = "FILEEXISTS(filename)";
static const char *fileexists_func_desc= "Returns the file status. Results are 'EXISTS' if the file exists and 'NONEXISTENT' if the file does not exist.\n";


static char *builtin_function_fileexists(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len) 
{
	if (argc != 1 || !argv[0][0]) {
		cw_log(LOG_ERROR, "Syntax: %s\n", fileexists_func_syntax);
		return NULL;
	}

	strncpy(buf, (access(argv[0], F_OK) ? "EXISTS" : "NONEXISTENT"), len);
	return buf;
}


static char *tdesc = "file existence dialplan function";

int unload_module(void)
{
        return cw_unregister_function(fileexists_function);
}

int load_module(void)
{
        fileexists_function = cw_register_function(fileexists_func_name, builtin_function_fileexists, NULL, fileexists_func_synopsis, fileexists_func_syntax, fileexists_func_desc);
	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 0;
}


/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
