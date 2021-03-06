/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) <Year>, <Your Name Here>
 *
 * <Your Name Here> <<You Email Here>>
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

/*! \file
 *
 * \brief Skeleton application
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_skel.c $", "$Revision: 4723 $")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"

static char *tdesc = "Trivial skeleton Application";

static void *skel_app = NULL;
static const char *skel_name = "Skel";
static const char *skel_synopsis = "Skeleton application.";
static const char *skel_syntax = "Skel()";
static const char *skel_descrip = "This application is a template to build other applications from.\n"
 " It shows you the basic structure to create your own CallWeaver applications.\n";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int skel_exec(struct cw_channel *chan, int argc, char **argv)
{
	int res = 0;
	struct localuser *u;

	/* Check the argument count is within range and any
	 * required arguments are none blank.
	 */
	if (argc < 1 || argc > 2 || !argv[0][0]) {
		cw_log(LOG_ERROR, "Syntax: %s\n", skel_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);
	
	/* Do our thing here.
	 * The argv array is private and modifiable as are the strings
	 * pointed to by the argv[] elements (but don't assume they are
	 * contiguous and can be glued together by overwriting the
	 * terminating nulls!)
	 * If you pass argv data to something outside your control
	 * you should assume it has been trashed and is unusable
	 * on return. If you want to preserve it either malloc
	 * and copy or use cw_strdupa()
	 */

	LOCAL_USER_REMOVE(u);
	
	return res;
}


/* \brief unload this module (CallWeaver continues running)
 * 
 * This is _only_ called if the module is explicitly unloaded.
 * It is _not_ called if CallWeaver exits completely. If you need
 * to perform clean up on exit you should register functions
 * using cw_register_atexit - and remember to remove them
 * with cw_unregister_atexit in your unload_module and
 * call them yourself if necessary.
 */
int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;

	/* Unregister _everything_ that you registered in your
	 * load_module routine. Return zero if unregistering was
	 * successful and you are happy for the module to be
	 * removed. Otherwise return non-zero.
	 * If you allow the module to be removed while things
	 * are still registered you _will_ crash CallWeaver!
	 */
	if (skel_app)
		res |= cw_unregister_application(skel_app);
	return res;
}

int load_module(void)
{
	skel_app = cw_register_application(skel_name, skel_exec, skel_synopsis, skel_syntax, skel_descrip);
	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}


