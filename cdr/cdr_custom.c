/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Includes code and algorithms from the Zapata library.
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
 * \brief Custom Comma Separated Value CDR records.
 * 
 * \arg See also \ref cwCDR
 *
 * Logs in LOG_DIR/cdr_custom
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/cdr/cdr_custom.c $", "$Revision: 4723 $")

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"

#define CUSTOM_LOG_DIR "/cdr_custom"

#define DATE_FORMAT "%Y-%m-%d %T"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

CW_MUTEX_DEFINE_STATIC(lock);

static char *desc = "Customizable Comma Separated Values CDR Backend";

static char *name = "cdr-custom";

static char master[CW_CONFIG_MAX_PATH];
static char format[1024]="";

static int load_config(int reload) 
{
	struct cw_config *cfg;
	struct cw_variable *var;
	int res = -1;

	strcpy(format, "");
	strcpy(master, "");
	if((cfg = cw_config_load("cdr_custom.conf"))) {
		var = cw_variable_browse(cfg, "mappings");
		while(var) {
			cw_mutex_lock(&lock);
			if (!cw_strlen_zero(var->name) && !cw_strlen_zero(var->value)) {
				if (strlen(var->value) > (sizeof(format) - 2))
					cw_log(LOG_WARNING, "Format string too long, will be truncated, at line %d\n", var->lineno);
				strncpy(format, var->value, sizeof(format) - 2);
				strcat(format,"\n");
				snprintf(master, sizeof(master),"%s/%s/%s", cw_config_CW_LOG_DIR, name, var->name);
				cw_mutex_unlock(&lock);
			} else
				cw_log(LOG_NOTICE, "Mapping must have both filename and format at line %d\n", var->lineno);
			if (var->next)
				cw_log(LOG_NOTICE, "Sorry, only one mapping is supported at this time, mapping '%s' will be ignored at line %d.\n", var->next->name, var->next->lineno); 
			var = var->next;
		}
		cw_config_destroy(cfg);
		res = 0;
	} else {
		if (reload)
			cw_log(LOG_WARNING, "Failed to reload configuration file.\n");
		else
			cw_log(LOG_WARNING, "Failed to load configuration file. Module not activated.\n");
	}
	
	return res;
}



static int custom_log(struct cw_cdr *cdr)
{
	/* Make sure we have a big enough buf */
	char buf[2048];
	struct cw_channel dummy;
	FILE *mf;

	/* Abort if no master file is specified */
	if (cw_strlen_zero(master))
		return 0;

	/* Quite possibly the first use of a static struct cw_channel, we need it so the var funcs will work */
	memset(&dummy, 0, sizeof(dummy));
	dummy.cdr = cdr;
	pbx_substitute_variables_helper(&dummy, format, buf, sizeof(buf));

	/* because of the absolutely unconditional need for the
	   highest reliability possible in writing billing records,
	   we open write and close the log file each time */
	if ((mf = fopen(master, "a"))) {
		fputs(buf, mf);
		fclose(mf);
	} else
		cw_log(LOG_ERROR, "Unable to re-open master file %s : %s\n", master, strerror(errno));

	return 0;
}

char *description(void)
{
	return desc;
}

int unload_module(void)
{
	cw_cdr_unregister(name);
	return 0;
}

int load_module(void)
{
	int res = 0;

	if (!load_config(0)) {
		res = cw_cdr_register(name, desc, custom_log);
		if (res)
			cw_log(LOG_ERROR, "Unable to register custom CDR handling\n");
	}
	return res;
}

int reload(void)
{
	return load_config(1);
}

int usecount(void)
{
	return 0;
}


