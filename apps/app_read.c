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
 * \brief Trivial application to read a variable
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_read.c $", "$Revision: 4723 $")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/app.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"

static char *tdesc = "Read Variable Application";

static void *read_app;
static char *read_name = "Read";
static char *read_synopsis = "Read a variable";
static char *read_syntax = "Read(variable[, filename[, maxdigits[, option[, attempts[, timeout]]]]])";
static char *read_descrip = 
"Reads a #-terminated string of digits a certain number of times from the\n"
"user in to the given variable.\n"
"  filename   -- file to play before reading digits.\n"
"  maxdigits  -- maximum acceptable number of digits. Stops reading after\n"
"                maxdigits have been entered (without requiring the user to\n"
"                press the '#' key).\n"
"                Defaults to 0 - no limit - wait for the user press the '#' key.\n"
"                Any value below 0 means the same. Max accepted value is 255.\n"
"  option     -- may be 'skip' to return immediately if the line is not up,\n"
"                or 'noanswer' to read digits even if the line is not up.\n"
"  attempts   -- if greater than 1, that many attempts will be made in the \n"
"                event no data is entered.\n"
"  timeout    -- if greater than 0, that value will override the default timeout.\n\n"
"Returns -1 on hangup or error and 0 otherwise.\n";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define cw_next_data(instr,ptr,delim) if((ptr=strchr(instr,delim))) { *(ptr) = '\0' ; ptr++;}

static int read_exec(struct cw_channel *chan, int argc, char **argv)
{
	int res = 0;
	struct localuser *u;
	char tmp[256];
	int option_skip = 0;
	int option_noanswer = 0;
	int maxdigits = 255;
	int tries = 1;
	int to = 0;

	if (argc < 1 || argc > 6) {
		cw_log(LOG_ERROR, "Syntax: %s\n", read_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);
	
	if (argc > 2 && argv[2][0]) {
		maxdigits = atoi(argv[2]);
		if (maxdigits < 1 || maxdigits > 255)
    			maxdigits = 255;
		else if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "Accepting a maximum of %d digits.\n", maxdigits);
	}

	if (argc > 3) {
		if (!strcasecmp(argv[3], "skip"))
			option_skip = 1;
		else if (!strcasecmp(argv[3], "noanswer"))
			option_noanswer = 1;
		else {
			if (strchr(argv[3], 's'))
				option_skip = 1;
			if (strchr(argv[3], 'n'))
				option_noanswer = 1;
		}
	}

	if (argc > 4) {
		tries = atoi(argv[4]);
		if (tries <= 0)
			tries = 1;
	}

	if (argc > 5) {
		to = atoi(argv[5]) * 1000;
		if (to <= 0)
			to = 0;
	}

	if (chan->_state != CW_STATE_UP) {
		if (option_skip) {
			/* At the user's option, skip if the line is not up */
			pbx_builtin_setvar_helper(chan, argv[0], "\0");
			LOCAL_USER_REMOVE(u);
			return 0;
		} else if (!option_noanswer) {
			/* Otherwise answer unless we're supposed to read while on-hook */
			res = cw_answer(chan);
		}
	}
	if (!res) {
		while(tries && !res) {
			cw_stopstream(chan);
			res = cw_app_getdata(chan, (argc > 1 && argv[1][0] ? argv[1] : NULL), tmp, maxdigits, to);
			if (res > -1) {
				pbx_builtin_setvar_helper(chan, argv[0], tmp);
				if (!cw_strlen_zero(tmp)) {
					if (option_verbose > 2)
						cw_verbose(VERBOSE_PREFIX_3 "User entered '%s'\n", tmp);
					tries = 0;
				} else {
					tries--;
					if (option_verbose > 2) {
						if (tries)
							cw_verbose(VERBOSE_PREFIX_3 "User entered nothing, %d chance%s left\n", tries, (tries != 1) ? "s" : "");
						else
							cw_verbose(VERBOSE_PREFIX_3 "User entered nothing.\n");
					}
				}
				res = 0;
			} else {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "User disconnected\n");
			}
		}
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(read_app);
	return res;
}

int load_module(void)
{
	read_app = cw_register_application(read_name, read_exec, read_synopsis, read_syntax, read_descrip);
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


