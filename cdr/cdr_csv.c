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
 * \brief Comma Separated Value CDR records.
 * 
 * \arg See also \ref cwCDR
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/cdr/cdr_csv.c $", "$Revision: 4723 $")

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"

#define CSV_LOG_DIR "/cdr-csv"
#define CSV_MASTER  "/Master.csv"

#define DATE_FORMAT "%Y-%m-%d %T"

/* #define CSV_LOGUNIQUEID 1 */
/* #define CSV_LOGUSERFIELD 1 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/*----------------------------------------------------
  The values are as follows:


  "accountcode", 	accountcode is the account name of detail records, Master.csv contains all records *
  			Detail records are configured on a channel basis, IAX and SIP are determined by user *
			Zap is determined by channel in zaptel.conf 
  "source",
  "destination",
  "destination context", 
  "callerid",
  "channel",
  "destination channel",	(if applicable)
  "last application",	Last application run on the channel 
  "last app argument",	argument to the last channel 
  "start time", 
  "answer time", 
  "end time", 
  duration,   		Duration is the whole length that the entire call lasted. ie. call rx'd to hangup  
  			"end time" minus "start time" 
  billable seconds, 	the duration that a call was up after other end answered which will be <= to duration  
  			"end time" minus "answer time" 
  "disposition",    	ANSWERED, NO ANSWER, BUSY 
  "amaflags",       	DOCUMENTATION, BILL, IGNORE etc, specified on a per channel basis like accountcode. 
  "uniqueid",           unique call identifier 
  "userfield"		user field set via SetCDRUserField 
----------------------------------------------------------*/

static char *desc = "Comma Separated Values CDR Backend";

static char *name = "csv";


static int append_string(char *buf, char *s, size_t bufsize)
{
	int pos = strlen(buf);
	int spos = 0;
	int error = 0;
	if (pos >= bufsize - 4)
		return -1;
	buf[pos++] = '\"';
	error = -1;
	while(pos < bufsize - 3) {
		if (!s[spos]) {
			error = 0;
			break;
		}
		if (s[spos] == '\"')
			buf[pos++] = '\"';
		buf[pos++] = s[spos];
		spos++;
	}
	buf[pos++] = '\"';
	buf[pos++] = ',';
	buf[pos++] = '\0';
	return error;
}

static int append_int(char *buf, int s, size_t bufsize)
{
	char tmp[32];
	int pos = strlen(buf);
	snprintf(tmp, sizeof(tmp), "%d", s);
	if (pos + strlen(tmp) > bufsize - 3)
		return -1;
	strncat(buf, tmp, bufsize - strlen(buf) - 1);
	pos = strlen(buf);
	buf[pos++] = ',';
	buf[pos++] = '\0';
	return 0;
}

static int append_date(char *buf, struct timeval tv, size_t bufsize)
{
	char tmp[80] = "";
	struct tm tm;
	time_t t;
	t = tv.tv_sec;
	if (strlen(buf) > bufsize - 3)
		return -1;
	if (cw_tvzero(tv)) {
		strncat(buf, ",", bufsize - strlen(buf) - 1);
		return 0;
	}
	localtime_r(&t,&tm);
	strftime(tmp, sizeof(tmp), DATE_FORMAT, &tm);
	return append_string(buf, tmp, bufsize);
}

static int build_csv_record(char *buf, size_t bufsize, struct cw_cdr *cdr)
{

	buf[0] = '\0';
	/* Account code */
	append_string(buf, cdr->accountcode, bufsize);
	/* Source */
	append_string(buf, cdr->src, bufsize);
	/* Destination */
	append_string(buf, cdr->dst, bufsize);
	/* Destination context */
	append_string(buf, cdr->dcontext, bufsize);
	/* Caller*ID */
	append_string(buf, cdr->clid, bufsize);
	/* Channel */
	append_string(buf, cdr->channel, bufsize);
	/* Destination Channel */
	append_string(buf, cdr->dstchannel, bufsize);
	/* Last Application */
	append_string(buf, cdr->lastapp, bufsize);
	/* Last Data */
	append_string(buf, cdr->lastdata, bufsize);
	/* Start Time */
	append_date(buf, cdr->start, bufsize);
	/* Answer Time */
	append_date(buf, cdr->answer, bufsize);
	/* End Time */
	append_date(buf, cdr->end, bufsize);
	/* Duration */
	append_int(buf, cdr->duration, bufsize);
	/* Billable seconds */
	append_int(buf, cdr->billsec, bufsize);
	/* Disposition */
	append_string(buf, cw_cdr_disp2str(cdr->disposition), bufsize);
	/* AMA Flags */
	append_string(buf, cw_cdr_flags2str(cdr->amaflags), bufsize);

#ifdef CSV_LOGUNIQUEID
	/* Unique ID */
	append_string(buf, cdr->uniqueid, bufsize);
#endif
#ifdef CSV_LOGUSERFIELD
	/* append the user field */
	append_string(buf, cdr->userfield,bufsize);	
#endif
	/* If we hit the end of our buffer, log an error */
	if (strlen(buf) < bufsize - 5) {
		/* Trim off trailing comma */
		buf[strlen(buf) - 1] = '\0';
		strncat(buf, "\n", bufsize - strlen(buf) - 1);
		return 0;
	}
	return -1;
}

static int writefile(char *s, char *acc)
{
	char tmp[CW_CONFIG_MAX_PATH];
	FILE *f;
	if (strchr(acc, '/') || (acc[0] == '.')) {
		cw_log(LOG_WARNING, "Account code '%s' insecure for writing file\n", acc);
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "%s/%s/%s.csv", (char *)cw_config_CW_LOG_DIR,CSV_LOG_DIR, acc);
	f = fopen(tmp, "a");
	if (!f)
		return -1;
	fputs(s, f);
	fflush(f);
	fclose(f);
	return 0;
}


static int csv_log(struct cw_cdr *cdr)
{
	/* Make sure we have a big enough buf */
	char buf[1024];
	char csvmaster[CW_CONFIG_MAX_PATH];
	FILE *mf = NULL;

	snprintf(csvmaster, sizeof(csvmaster),"%s/%s/%s", cw_config_CW_LOG_DIR, CSV_LOG_DIR, CSV_MASTER);
#if 0
	printf("[CDR] %s ('%s' -> '%s') Dur: %ds Bill: %ds Disp: %s Flags: %s Account: [%s]\n", cdr->channel, cdr->src, cdr->dst, cdr->duration, cdr->billsec, cw_cdr_disp2str(cdr->disposition), cw_cdr_flags2str(cdr->amaflags), cdr->accountcode);
#endif
	if (build_csv_record(buf, sizeof(buf), cdr)) {
		cw_log(LOG_WARNING, "Unable to create CSV record in %d bytes.  CDR not recorded!\n", (int)sizeof(buf));
	} else {
		/* because of the absolutely unconditional need for the
		   highest reliability possible in writing billing records,
		   we open write and close the log file each time */
		if ((mf = fopen(csvmaster, "a"))) {
			fputs(buf, mf);
			fclose(mf);
		} else
			cw_log(LOG_ERROR, "Unable to re-open master file %s : %s\n", csvmaster, strerror(errno));

		if (!cw_strlen_zero(cdr->accountcode)) {
			if (writefile(buf, cdr->accountcode))
				cw_log(LOG_WARNING, "Unable to write CSV record to account file '%s' : %s\n", cdr->accountcode, strerror(errno));
		}
	}
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
	int res;

	res = cw_cdr_register(name, desc, csv_log);
	if (res)
		cw_log(LOG_ERROR, "Unable to register CSV CDR handling\n");
	return res;
}

int reload(void)
{
	return 0;
}

int usecount(void)
{
	return 0;
}


