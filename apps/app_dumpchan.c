/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005, Anthony Minessale II.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * disclaimed to Digium
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
 * \brief Application to dump channel variables
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

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_dumpchan.c $", "$Revision: 4723 $")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"

static char *tdesc = "Dump Info About The Calling Channel";

static void *dumpchan_app;
static char *dumpchan_name = "DumpChan";
static char *dumpchan_synopsis = "Dump Info About The Calling Channel";
static char *dumpchan_syntax = "DumpChan([min_verbose_level])";
static char *dumpchan_descrip = 
"Displays information on channel and listing of all channel\n"
"variables. If min_verbose_level is specified, output is only\n"
"displayed when the verbose level is currently set to that number\n"
"or greater. Always returns 0.\n\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int cw_serialize_showchan(struct cw_channel *c, char *buf, size_t size)
{
	struct timeval now;
	long elapsed_seconds=0;
	int hour=0, min=0, sec=0;
	char cgrp[256];
	char pgrp[256];
	
	now = cw_tvnow();
	memset(buf,0,size);
	if (!c)
		return 0;

	if (c->cdr) {
		elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
		hour = elapsed_seconds / 3600;
		min = (elapsed_seconds % 3600) / 60;
		sec = elapsed_seconds % 60;
	}

	snprintf(buf,size, 
			 "Name=               %s\n"
			 "Type=               %s\n"
			 "UniqueID=           %s\n"
			 "CallerID=           %s\n"
			 "CallerIDName=       %s\n"
			 "DNIDDigits=         %s\n"
			 "State=              %s (%d)\n"
			 "Rings=              %d\n"
			 "NativeFormat=       %d\n"
			 "WriteFormat=        %d\n"
			 "ReadFormat=         %d\n"
			 "1stFileDescriptor=  %d\n"
			 "Framesin=           %d %s\n"
			 "Framesout=          %d %s\n"
			 "TimetoHangup=       %ld\n"
			 "ElapsedTime=        %dh%dm%ds\n"
			 "Context=            %s\n"
			 "Extension=          %s\n"
			 "Priority=           %d\n"
			 "CallGroup=          %s\n"
			 "PickupGroup=        %s\n"
			 "Application=        %s\n"
			 "Blocking_in=        %s\n",
			 c->name,
			 c->type,
			 c->uniqueid,
			 (c->cid.cid_num ? c->cid.cid_num : "(N/A)"),
			 (c->cid.cid_name ? c->cid.cid_name : "(N/A)"),
			 (c->cid.cid_dnid ? c->cid.cid_dnid : "(N/A)" ),
			 cw_state2str(c->_state),
			 c->_state,
			 c->rings,
			 c->nativeformats,
			 c->writeformat,
			 c->readformat,
			 c->fds[0], c->fin & 0x7fffffff, (c->fin & 0x80000000) ? " (DEBUGGED)" : "",
			 c->fout & 0x7fffffff, (c->fout & 0x80000000) ? " (DEBUGGED)" : "", (long)c->whentohangup,
			 hour,
			 min,
			 sec,
			 c->context,
			 c->exten,
			 c->priority,
			 cw_print_group(cgrp, sizeof(cgrp), c->callgroup),
			 cw_print_group(pgrp, sizeof(pgrp), c->pickupgroup),
			 ( c->appl ? c->appl : "(N/A)" ),
			 (cw_test_flag(c, CW_FLAG_BLOCKING) ? c->blockproc : "(Not Blocking)"));

	return 0;
}

static int dumpchan_exec(struct cw_channel *chan, int argc, char **argv)
{
	static char *line = "================================================================================";
	char vars[1024];
	char info[1024];
	struct localuser *u;
	int level;
	
	LOCAL_USER_ADD(u);

	level = (argc > 0 ? atoi(argv[0]) : 0);

	if (option_verbose >= level) {
		cw_serialize_showchan(chan, info, sizeof(info));
		pbx_builtin_serialize_variables(chan, vars, sizeof(vars));
		cw_verbose("\nDumping Info For Channel: %s:\n%s\nInfo:\n%s\nVariables:\n%s%s\n", chan->name, line, info, vars, line);
	}

	LOCAL_USER_REMOVE(u);
	return 0;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(dumpchan_app);
	return res;
}

int load_module(void)
{
	dumpchan_app = cw_register_application(dumpchan_name, dumpchan_exec, dumpchan_synopsis, dumpchan_syntax, dumpchan_descrip);
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



