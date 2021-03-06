/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Martin Pycko <martinp@digium.com>
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
 * \brief Applications connected with CDR engine
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_cdr.c $", "$Revision: 4723 $")

#include "callweaver/channel.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"


static char *tdesc = "Make sure callweaver doesn't save CDR for a certain call";

static void *nocdr_app;
static char *nocdr_name = "NoCDR";
static char *nocdr_synopsis = "Make sure callweaver doesn't save CDR for a certain call";
static char *nocdr_syntax = "NoCDR()";
static char *nocdr_descrip = "Makes sure there won't be any CDR written for a certain call";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int nocdr_exec(struct cw_channel *chan, int argc, char **argv)
{
	struct localuser *u;
	
	LOCAL_USER_ADD(u);

	if (chan->cdr) {
		cw_cdr_free(chan->cdr);
		chan->cdr = NULL;
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(nocdr_app);
	return res;
}

int load_module(void)
{
	nocdr_app = cw_register_application(nocdr_name, nocdr_exec, nocdr_synopsis, nocdr_syntax, nocdr_descrip);
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


