/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005, Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_eval__v001@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 * \brief Eval application
 *
 * \author Tilghman Lesher <app_eval__v001 at the-tilghman.com>
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_eval.c $", "$Revision: 4723 $")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"

/* Maximum length of any variable */
#define MAXRESULT	1024

static char *tdesc = "Reevaluates strings";

static void *eval_app;
static char *eval_name = "Eval";
static char *eval_synopsis = "Evaluates a string";
static char *eval_syntax = "Eval(newvar=somestring)";
static char *eval_descrip =
"Normally CallWeaver evaluates variables inline.  But what if you want to\n"
"store variable offsets in a database, to be evaluated later?  Eval is\n"
"the answer, by allowing a string to be evaluated twice in the dialplan,\n"
"the first time as part of the normal dialplan, and the second using Eval.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int eval_exec(struct cw_channel *chan, int argc, char **argv)
{
	static int dep_warning = 0;
	char tmp[MAXRESULT];
	struct localuser *u;
	char *newvar = NULL;
	int res = 0;

	if (!dep_warning) {
		cw_log(LOG_WARNING, "This application has been deprecated in favor of the dialplan function, EVAL\n");
		dep_warning = 1;
	}

	LOCAL_USER_ADD(u);
	
	/* Check and parse arguments */
	if (argv[0]) {
		newvar = strsep(&argv[0], "=");
		if (newvar && (newvar[0] != '\0')) {
			pbx_substitute_variables_helper(chan, argv[0], tmp, sizeof(tmp));
			pbx_builtin_setvar_helper(chan, newvar, tmp);
		}
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(eval_app);
	return res;
}

int load_module(void)
{
	eval_app = cw_register_application(eval_name, eval_exec, eval_synopsis, eval_syntax, eval_descrip);
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


