/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com> and others.
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
 * \brief Custom SQLite3 CDR records.
 *
 * \author Adapted by Alejandro Rios <alejandro.rios@avatar.com.co> and
 *  Russell Bryant <russell@digium.com> from 
 *  cdr_mysql_custom by Edward Eastman <ed@dm3.co.uk>,
 *	and cdr_sqlite by Holger Schurig <hs4233@mail.mn-solutions.de>
 *	
 *
 * \arg See also \ref cwCDR
 *
 *
 * \ingroup cdr_drivers
 */

/*** MODULEINFO
	<depend>sqlite3</depend>
 ***/

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/cdr/cdr_sqlite3_custom.c $", "$Revision: 4723 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sqlite3.h>

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/cli.h"
#include "callweaver/options.h"

CW_MUTEX_DEFINE_STATIC(lock);

static const char config_file[] = "cdr_sqlite3_custom.conf";

static char *desc = "Customizable SQLite3 CDR Backend";
static char *name = "cdr_sqlite3_custom";
static sqlite3 *db = NULL;

static char table[80];
static char columns[1024];
static char values[1024];

static int load_config(int reload)
{
	struct cw_config *cfg;
	struct cw_variable *mappingvar;
	const char *tmp;

	if (!(cfg = cw_config_load(config_file))) {
		if (reload)
			cw_log(LOG_WARNING, "%s: Failed to reload configuration file.\n", name);
		else {
			cw_log(LOG_WARNING,
					"%s: Failed to load configuration file. Module not activated.\n",
					name);
		}
		return -1;
	}

	if (!reload)
		cw_mutex_lock(&lock);

	if (!(mappingvar = cw_variable_browse(cfg, "master"))) {
		/* nothing configured */
		cw_config_destroy(cfg);
		return 0;
	}
	
	/* Mapping must have a table name */
	tmp = cw_variable_retrieve(cfg, "master", "table");
	if (!cw_strlen_zero(tmp))
		cw_copy_string(table, tmp, sizeof(table));
	else {
		cw_log(LOG_WARNING, "%s: Table name not specified.  Assuming cdr.\n", name);
		strcpy(table, "cdr");
	}

	tmp = cw_variable_retrieve(cfg, "master", "columns");
	if (!cw_strlen_zero(tmp))
		cw_copy_string(columns, tmp, sizeof(columns));
	else {
		cw_log(LOG_WARNING, "%s: Column names not specified. Module not loaded.\n",
				name);
		cw_config_destroy(cfg);
		return -1;
	}

	tmp = cw_variable_retrieve(cfg, "master", "values");
	if (!cw_strlen_zero(tmp))
		cw_copy_string(values, tmp, sizeof(values));
	else {
		cw_log(LOG_WARNING, "%s: Values not specified. Module not loaded.\n", name);
		cw_config_destroy(cfg);
		return -1;
	}

	if (!reload)
		cw_mutex_unlock(&lock);

	cw_config_destroy(cfg);

	return 0;
}

/* assumues 'to' buffer is at least strlen(from) * 2 + 1 bytes */
static int do_escape(char *to, const char *from)
{
        char *out = to;

        for (; *from; from++) {
                if (*from == '\"' || *from == '\\')
                        *out++ = *from;
                *out++ = *from;
        }
        *out = '\0';

        return 0;
}

static int sqlite3_log(struct cw_cdr *cdr)
{
	int res = 0;
	char *zErr = 0;
	char *sql_cmd;
	struct cw_channel dummy;
	int count;

	{ /* Make it obvious that only sql_cmd should be used outside of this block */
		char *sql_tmp_cmd;
		char sql_insert_cmd[2048] = "";
		sql_tmp_cmd = sqlite3_mprintf("INSERT INTO %q (%q) VALUES (%s)", table, columns, values);
		dummy.cdr = cdr;
		pbx_substitute_variables_helper(&dummy, sql_tmp_cmd, sql_insert_cmd, sizeof(sql_insert_cmd));
		sqlite3_free(sql_tmp_cmd);
		sql_cmd = alloca(strlen(sql_insert_cmd) * 2 + 1);
		do_escape(sql_cmd, sql_insert_cmd);
	}

	cw_mutex_lock(&lock);

	for (count = 0; count < 5; count++) {
		res = sqlite3_exec(db, sql_cmd, NULL, NULL, &zErr);
		if (res != SQLITE_BUSY && res != SQLITE_LOCKED)
			break;
		usleep(200);
	}

	if (zErr) {
		cw_log(LOG_ERROR, "%s: %s. sentence: %s.\n", name, zErr, sql_cmd);
		sqlite3_free(zErr);
	}

	cw_mutex_unlock(&lock);

	return res;
}

int unload_module(void)
{
	if (db)
		sqlite3_close(db);

	cw_cdr_unregister(name);

	return 0;
}

int load_module(void)
{
	char *zErr;
	char fn[PATH_MAX];
	int res;
	char *sql_cmd;

	if (!load_config(0)) {
		res = cw_cdr_register(name, desc, sqlite3_log);
		if (res) {
			cw_log(LOG_ERROR, "%s: Unable to register custom SQLite3 CDR handling\n", name);
			return -1;
		}
	}

	/* is the database there? */
	snprintf(fn, sizeof(fn), "%s/master.db", cw_config_CW_LOG_DIR);
	res = sqlite3_open(fn, &db);
	if (!db) {
		cw_log(LOG_ERROR, "%s: Could not open database %s.\n", name, fn);
		sqlite3_free(zErr);
		return -1;
	}

	/* is the table there? */
	sql_cmd = sqlite3_mprintf("SELECT COUNT(AcctId) FROM %q;", table);
	res = sqlite3_exec(db, sql_cmd, NULL, NULL, NULL);
	sqlite3_free(sql_cmd);
	if (res) {
		sql_cmd = sqlite3_mprintf("CREATE TABLE %q (AcctId INTEGER PRIMARY KEY,%q)", table, columns);
		res = sqlite3_exec(db, sql_cmd, NULL, NULL, &zErr);
		sqlite3_free(sql_cmd);
		if (zErr) {
			cw_log(LOG_WARNING, "%s: %s.\n", name, zErr);
			sqlite3_free(zErr);
			return 0;
		}

		if (res) {
			cw_log(LOG_ERROR, "%s: Unable to create table '%s': %s.\n", name, table, zErr);
			sqlite3_free(zErr);
			if (db)
				sqlite3_close(db);
			return -1;
		}
	}

	return 0;
}

int reload(void)
{
	int res;

	cw_mutex_lock(&lock);
	res = load_config(1);
	cw_mutex_unlock(&lock);

	return res;
}

char *description(void)
{
	return desc;
}

int usecount(void)
{
	/* To be able to unload the module */
	if ( cw_mutex_trylock(&lock) ) {
		return 1;
	} else {
		cw_mutex_unlock(&lock);
		return 0;
	}
}
