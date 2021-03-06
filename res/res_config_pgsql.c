/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 CallWeaver.org.
 *
 * Marc Olivier Chouinard <mochouinard@callweaver.org>
 *
 * res_config_pgsql.c <PostgreSQL plugin for portable configuration engine >
 * Copyright (C) 2005 Business Technology Group (http://www.btg.co.nz)
 *   Danel Swarbrick <daniel@btg.co.nz>
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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/res/res_config_pgsql.c $", "$Revision: 4723 $")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/config.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"

#include <libpq-fe.h>

static char *tdesc = "PostgreSQL Configuration";

CW_MUTEX_DEFINE_STATIC(pgsql_lock);
#define RES_CONFIG_PGSQL_CONF "res_pgsql.conf"
static char conninfo[512];
static PGconn *conn = NULL;

static int parse_config(void);
static int pgsql_reconnect(const char *database);

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int parse_config(void)
{
	struct cw_config *config;
	char *s;

	config = cw_config_load(RES_CONFIG_PGSQL_CONF);

	if (config) {

		/* get the database host */
		s = cw_variable_retrieve(config, "general", "dsn");
		if (s == NULL) {
			cw_log(LOG_WARNING, "PgSQL RealTime: No DSN found, using 'dbname=callweaver user=callweaver'.\n");
			strncpy(conninfo, "dbname=callweaver user=callweaver", sizeof(conninfo));
		} else {
			strncpy(conninfo, s, sizeof(conninfo));
		}

	} else {
		cw_log(LOG_WARNING, "PgSQL RealTime config file (%s) not found.\n", RES_CONFIG_PGSQL_CONF);
	}
	cw_config_destroy(config);

	return 1;
}

static int pgsql_reconnect(const char *database)
{
	if (conn != NULL) {
		/* we may already be connected */
		if (PQstatus(conn) == CONNECTION_OK) {
			return 1;
		} else {
			cw_log(LOG_NOTICE, "PgSQL RealTime: Existing database connection broken. Trying to reset.\n");

			/* try to reset the connection */
			PQreset(conn);

			/* check the connection status again */
			if (PQstatus(conn) == CONNECTION_OK) {
				cw_log(LOG_NOTICE, "PgSQL RealTime: Existing database connection reset ok.\n");
				return 1;
			} else {
				/* still no luck, tear down the connection and we'll make a new connection */
				cw_log(LOG_NOTICE, "PgSQL RealTime: Unable to reset existing database connection.\n");
				PQfinish(conn);
			}
		}
	}

	conn = PQconnectdb(conninfo);

	if (PQstatus(conn) == CONNECTION_OK) {
		cw_log(LOG_NOTICE, "PgSQL RealTime: Successfully connected to PostgreSQL database.\n");
		return 1;
	} else {
		cw_log(LOG_WARNING, "PgSQL RealTime: Couldn't establish DB connection. Check debug.\n");
		cw_log(LOG_ERROR, "PgSQL RealTime: reason %s\n", PQerrorMessage(conn));
	}		

	return -1;
}

static struct cw_variable *realtime_pgsql(const char *database, const char *table, va_list ap)
{
	PGresult *res;
	long int row, rowcount = 0;
	int col, colcount = 0;
	char sql[1024];
	char *stringp;
	char *chunk;
	char *op;	
	const char *newparam, *newval;
	struct cw_variable *var=NULL, *prev=NULL;

	if (!table) {
		cw_log(LOG_WARNING, "PgSQL RealTime: No table specified.\n");
		return NULL;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval)  {
		cw_log(LOG_WARNING, "PgSQL RealTime: Realtime retrieval requires at least 1 parameter and 1 value to search on.\n");
		return NULL;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	if (!strchr(newparam, ' ')) op = " ="; else op = "";

	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s%s '%s'", table, newparam, op, newval);
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		if (!strchr(newparam, ' ')) op = " ="; else op = "";
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND %s%s '%s'", newparam, op, newval);
	}
	va_end(ap);

	cw_log(LOG_DEBUG, "PgSQL RealTime: Retrieve SQL: %s\n", sql);

	/* SQL statement is ready, check database connection is still good */
	if (!pgsql_reconnect(database)) {
		return NULL;
	}

	cw_mutex_lock(&pgsql_lock);
	res = PQexec(conn, sql);

	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		cw_log(LOG_WARNING, "PgSQL RealTime: Failed to query database. Check debug for more info.\n");
		cw_log(LOG_DEBUG, "PgSQL RealTime: Query: %s\n", sql);
		cw_log(LOG_DEBUG, "PgSQL RealTime: Query failed because: %s\n", PQresultErrorMessage(res));
		PQclear(res);
		cw_mutex_unlock(&pgsql_lock);
		return NULL;
	}

	rowcount = PQntuples(res);
	if (rowcount > 0) {
		colcount = PQnfields(res);

		for (row = 0; row < rowcount; row++) {
			for (col = 0; col < colcount; col++) {
				stringp = PQgetvalue(res, row, col);
				
				while (stringp) {
					chunk = strsep(&stringp, ";");
					if (chunk && !cw_strlen_zero(cw_strip(chunk))) {
						if (prev) {
							prev->next = cw_variable_new(PQfname(res, col), chunk);
							if (prev->next) {
								prev = prev->next;
							}
						} else {
							prev = var = cw_variable_new(PQfname(res, col), chunk);
						}
					}
				}
			}
		}
	} else {
		cw_log(LOG_WARNING, "PgSQL RealTime: Could not find any rows in table %s.\n", table);
	}

	PQclear(res);
	cw_mutex_unlock(&pgsql_lock);

	return var;
}

static struct cw_config *realtime_multi_pgsql(const char *database, const char *table, va_list ap)
{
	PGresult *res;
	long int row, rowcount = 0;
	int col, colcount = 0;
	char sql[1024];
	const char *initfield = NULL;
	char *stringp;
	char *chunk;
	char *op;
	const char *newparam, *newval;
	struct cw_realloca ra;
	struct cw_variable *var=NULL;
	struct cw_config *cfg = NULL;
	struct cw_category *cat = NULL;

	if(!table) {
		cw_log(LOG_WARNING, "PgSQL RealTime: No table specified.\n");
		return NULL;
	}
	
	memset(&ra, 0, sizeof(ra));

	cfg = cw_config_new();
	if (!cfg) {
		/* If I can't alloc memory at this point, why bother doing anything else? */
		cw_log(LOG_WARNING, "Out of memory!\n");
		return NULL;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval)  {
		cw_log(LOG_WARNING, "PgSQL RealTime: Realtime retrieval requires at least 1 parameter and 1 value to search on.\n");
		return NULL;
	}

	initfield = cw_strdupa(newparam);
	if ((op = strchr(initfield, ' ')))
		*op = '\0';

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	if(!strchr(newparam, ' ')) op = " ="; else op = "";

	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s%s '%s'", table, newparam, op, newval);
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		if (!strchr(newparam, ' ')) op = " ="; else op = "";
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND %s%s '%s'", newparam, op, newval);
	}

	if (initfield) {
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY %s", initfield);
	}

	va_end(ap);

	cw_log(LOG_DEBUG, "PgSQL RealTime: Retrieve SQL: %s\n", sql);

	/* SQL statement is ready, check database connection is still good */
	if (!pgsql_reconnect(database)) {
		return NULL;
	}

	cw_mutex_lock(&pgsql_lock);
	res = PQexec(conn, sql);
	
	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		cw_log(LOG_WARNING, "PgSQL RealTime: Failed to query database. Check debug for more info.\n");
		cw_log(LOG_DEBUG, "PgSQL RealTime: Query: %s\n", sql);
		cw_log(LOG_DEBUG, "PgSQL RealTime: Query failed because: %s\n", PQresultErrorMessage(res));
		PQclear(res);
		cw_mutex_unlock(&pgsql_lock);
		return NULL;
	}

	rowcount = PQntuples(res);
	if (rowcount > 0) {
		colcount = PQnfields(res);

		for (row = 0; row < rowcount; row++) {

			var = NULL;
			cat = cw_category_new("");
			if (!cat) {
				cw_log(LOG_WARNING, "Out of memory!\n");
				continue;
			}

			for (col = 0; col < colcount; col++) {
				stringp = PQgetvalue(res, row, col);
				while (stringp) {
					chunk = strsep(&stringp, ";");
					if (chunk && !cw_strlen_zero(cw_strip(chunk))) {
						if (initfield && !strcmp(initfield, PQfname(res, col))) {
							cw_category_rename(cat, chunk);
						}
						var = cw_variable_new(PQfname(res, col), chunk);
						cw_variable_append(cat, var);
					}
				}
			}
			cw_category_append(cfg, cat);
		}
	} else {
		cw_log(LOG_WARNING, "PgSQL RealTime: Could not find any rows in table %s.\n", table);
	}

	PQclear(res);
	cw_mutex_unlock(&pgsql_lock);

	return cfg;
}

static int update_pgsql(const char *database, const char *table, const char *keyfield, const char *lookup, va_list ap)
{
	PGresult *res;
	long int rowcount = 0;
	char sql[1024];
	const char *newparam, *newval;

	if (!table) {
		cw_log(LOG_WARNING, "PgSQL RealTime: No table specified.\n");
		return -1;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval)  {
		cw_log(LOG_WARNING, "PgSQL RealTime: Realtime retrieval requires at least 1 parameter and 1 value to search on.\n");
		return -1;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	snprintf(sql, sizeof(sql), "UPDATE %s SET %s = '%s'", table, newparam, newval);
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), ", %s = '%s'", newparam, newval);
	}
	va_end(ap);
	snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " WHERE %s = '%s'", keyfield, lookup);

	cw_log(LOG_DEBUG, "PgSQL RealTime: Update SQL: %s\n", sql);

	/* SQL statement is ready, check database connection is still good */
	if (!pgsql_reconnect(database)) {
		return -1;
	}

	cw_mutex_lock(&pgsql_lock);
	res = PQexec(conn, sql);
	
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		cw_log(LOG_WARNING, "PgSQL RealTime: Failed to query database. Check debug for more info.\n");
		cw_log(LOG_DEBUG, "PgSQL RealTime: Query: %s\n", sql);
		cw_log(LOG_DEBUG, "PgSQL RealTime: Query failed because: %s\n", PQresultErrorMessage(res));
		PQclear(res);
		cw_mutex_unlock(&pgsql_lock);
		return -1;
	}

	rowcount = atol(PQcmdTuples(res));

	PQclear(res);
	cw_mutex_unlock(&pgsql_lock);

	cw_log(LOG_DEBUG, "PgSQL RealTime: Updated %lu rows on table: %s\n", rowcount, table);

	if (rowcount >= 0)
		return (int)rowcount;
	else
		return -1;
}

static struct cw_config *config_pgsql(const char *database, const char *table, const char *file, struct cw_config *cfg)
{
	PGresult *res;
	long int row, rowcount = 0;
	int colcount = 0;
	char sql[1024];
	char last[128] = "";
	struct cw_category *cur_cat = NULL;
	struct cw_variable *new_v;
	int last_cat_metric = 0;

	if (!file || !strcmp(file, RES_CONFIG_PGSQL_CONF)) {
		cw_log(LOG_WARNING, "PgSQL RealTime: Cannot configure myself.\n");
		return NULL;		
	}

	snprintf(sql, sizeof(sql), "SELECT category, var_name, var_val, cat_metric FROM %s WHERE filename='%s' and commented=0 ORDER BY filename, cat_metric DESC, var_metric ASC, category, var_name, var_val, id", table, file);

	cw_log(LOG_NOTICE, "PgSQL RealTime: Static SQL: %s\n", sql);

	/* SQL statement is ready, check database connection is still good */
	if (!pgsql_reconnect(database)) {
		return NULL;
	}

	cw_mutex_lock(&pgsql_lock);
	res = PQexec(conn, sql);
	
	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		cw_log(LOG_WARNING, "PgSQL RealTime: Failed to query database. Check debug for more info.\n");
		cw_log(LOG_DEBUG, "PgSQL RealTime: Query: %s\n", sql);
		cw_log(LOG_DEBUG, "PgSQL RealTime: Query failed because: %s\n", PQresultErrorMessage(res));
		PQclear(res);
		cw_mutex_unlock(&pgsql_lock);
		return NULL;
	}

	rowcount = PQntuples(res);
	if (rowcount > 0) {

		cw_log(LOG_NOTICE, "PgSQL RealTime: Found %lu rows.\n", rowcount);
		colcount = PQnfields(res);
		for (row = 0; row < rowcount; row++) {
			if (!strcmp(PQgetvalue(res, row, 1), "#include")) {
				if (!cw_config_internal_load(PQgetvalue(res, row, 2), cfg)) {
					PQclear(res);
					cw_mutex_unlock(&pgsql_lock);
					return NULL;
				}
				continue;
			}

			if (strcmp(last, PQgetvalue(res, row, 0)) || last_cat_metric != atoi(PQgetvalue(res, row, 3))) {
				cur_cat = cw_category_new(PQgetvalue(res, row, 0));
				if (!cur_cat) {
					cw_log(LOG_WARNING, "Out of memory!\n");
					break;
				}
				strcpy(last, PQgetvalue(res, row, 0));
				last_cat_metric = atoi(PQgetvalue(res, row, 3));
				cw_category_append(cfg, cur_cat);
			}
			new_v = cw_variable_new(PQgetvalue(res, row, 1), PQgetvalue(res, row, 2));
			cw_variable_append(cur_cat, new_v);

		}

	} else {
		cw_log(LOG_WARNING, "PgSQL RealTime: Could not find config '%s' in database.\n", file);
	}

	PQclear(res);
	cw_mutex_unlock(&pgsql_lock);

	return cfg;
}

static struct cw_config_engine pgsql_engine = {
	.name = "pgsql",
	.load_func = config_pgsql,
	.realtime_func = realtime_pgsql,
	.realtime_multi_func = realtime_multi_pgsql,
	.update_func = update_pgsql
};

int unload_module(void)
{
	/* acquire control before doing anything to the module itself */
	cw_mutex_lock(&pgsql_lock);

	PQfinish(conn);
	cw_config_engine_deregister(&pgsql_engine);
	if (option_verbose) {
		cw_verbose("PgSQL RealTime unloaded.\n");
	}

	STANDARD_HANGUP_LOCALUSERS;

	/* Unlock so something else can destroy the lock */
	cw_mutex_unlock(&pgsql_lock);

	return 0;
}

int load_module(void)
{
	parse_config();

	pgsql_reconnect(NULL);

	cw_config_engine_register(&pgsql_engine);
	if (option_verbose) {
		cw_verbose("PgSQL RealTime driver loaded.\n");
	}

	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	/* never unload a config module */
	return 1;
}

