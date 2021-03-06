/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief App to flash a zap trunk
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h> 
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_flash.c $", "$Revision: 4723 $")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"


static char *tdesc = "Send hook flash";

static void *flash_app;
static char *flash_name = "Flash";
static char *flash_synopsis = "Send a hook flashes";
static char *flash_syntax = "Flash()";
static char *flash_descrip =
"Sends a hook flash as if the handset cradle was momentarily depressed\n"
"or the \"flash\" button on the phone was pressed.\n"
"Always returns 0\n";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;


static int flash_exec(struct cw_channel *chan, int argc, char **argv)
{
	struct localuser *u;

	LOCAL_USER_ADD(u);
	if (chan)
		cw_indicate(chan, CW_CONTROL_FLASH);
	LOCAL_USER_REMOVE(u);
	return 0;
}


int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(flash_app);
	return res;
}

int load_module(void)
{
	flash_app = cw_register_application(flash_name, flash_exec, flash_synopsis, flash_syntax, flash_descrip);
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


