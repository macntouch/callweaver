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
 * \brief Playback the special information tone to get rid of telemarketers
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_zapateller.c $", "$Revision: 4723 $")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"

static char *tdesc = "Block Telemarketers with Special Information Tone";

static void *zapateller_app;
static const char *zapateller_name = "Zapateller";
static const char *zapateller_synopsis = "Block telemarketers with SIT";
static const char *zapateller_syntax = "Zapateller(options)";
static const char *zapateller_descrip = 
"Generates special information tone to block\n"
"telemarketers from calling you.  Returns 0 normally or -1 on hangup.\n"
"Options is a pipe-delimited list of options.  The following options\n"
"are available: 'answer' causes the line to be answered before playing\n"
"the tone, 'nocallerid' causes Zapateller to only play the tone if there\n"
"is no callerid information available.  Options should be separated by |\n"
"characters\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int zapateller_exec(struct cw_channel *chan, int argc, char **argv)
{
	int res = 0;
	struct localuser *u;
	int answer = 0, nocallerid = 0;

	LOCAL_USER_ADD(u);

	for(; argc; argv++, argc--) {
		if (!strcasecmp(argv[0], "answer"))
			answer = 1;
		else if (!strcasecmp(argv[0], "nocallerid"))
			nocallerid = 1;
        }

	cw_stopstream(chan);
	if (chan->_state != CW_STATE_UP) {
		if (answer) 
			res = cw_answer(chan);
		if (!res) {
			res = cw_safe_sleep(chan, 500);
		}
	}
	if (chan->cid.cid_num && nocallerid) {
		LOCAL_USER_REMOVE(u);
		return res;
	} 
	if (!res) 
		res = cw_tonepair(chan, 950, 0, 330, 0);
	if (!res) 
		res = cw_tonepair(chan, 1400, 0, 330, 0);
	if (!res) 
		res = cw_tonepair(chan, 1800, 0, 330, 0);
	if (!res) 
		res = cw_tonepair(chan, 0, 0, 1000, 0);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(zapateller_app);
	return res;
}

int load_module(void)
{
	zapateller_app = cw_register_application(zapateller_name, zapateller_exec, zapateller_synopsis, zapateller_syntax, zapateller_descrip);
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


