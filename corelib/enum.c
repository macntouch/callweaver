/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Funding provided by nic.at
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
 * \brief ENUM Support for CallWeaver
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#if defined(__APPLE_CC__) && __APPLE_CC__ >= 1495
#include <arpa/nameser_compat.h>
#endif
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <unistd.h>
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/corelib/enum.c $", "$Revision: 4723 $")

#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/enum.h"
#include "callweaver/dns.h"
#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/utils.h"

#ifdef __APPLE__
#undef T_NAPTR
#define T_NAPTR 35
#endif

#ifdef __APPLE__
#undef T_TXT
#define T_TXT 16
#endif

/* The IETF Enum standard root, managed by the ITU */
#define TOPLEV "e164.arpa."

/* Linked list from config file */
static struct enum_search {
	char toplev[512];
	struct enum_search *next;
} *toplevs;

static int enumver = 0;

CW_MUTEX_DEFINE_STATIC(enumlock);

struct naptr {
	unsigned short order;
	unsigned short pref;
} __attribute__ ((__packed__));

/*--- parse_ie: Parse NAPTR record information elements */
static int parse_ie(char *data, int maxdatalen, char *src, int srclen)
{
	int len, olen;

	len = olen = (int)src[0];
	src++;
	srclen--;
	if (len > srclen) {
		cw_log(LOG_WARNING, "Want %d, got %d\n", len, srclen);
		return -1;
	}
	if (len > maxdatalen)
		len = maxdatalen;
	memcpy(data, src, len);
	return olen + 1;
}

/*--- parse_naptr: Parse DNS NAPTR record used in ENUM ---*/
static int parse_naptr(char *dst, int dstsize, char *tech, int techsize, char *answer, int len, char *naptrinput)
{
	char tech_return[80];
	char *oanswer = answer;
	char flags[512] = "";
	char services[512] = "";
	char *p;
	char regexp[512] = "";
	char repl[512] = "";
	char temp[512] = "";
	char delim;
	char *delim2;
	char *pattern, *subst, *d;
	int res;
	int regexp_len, size, backref;
	int d_len = sizeof(temp) - 1;
	regex_t preg;
	regmatch_t pmatch[9];

	tech_return[0] = '\0';

	dst[0] = '\0';

	if (len < sizeof(struct naptr)) {
		cw_log(LOG_WARNING, "NAPTR record length too short\n");
		return -1;
	}
	answer += sizeof(struct naptr);
	len -= sizeof(struct naptr);
	if ((res = parse_ie(flags, sizeof(flags) - 1, answer, len)) < 0) {
		cw_log(LOG_WARNING, "Failed to get flags from NAPTR record\n");
		return -1;
	} else {
		answer += res;
		len -= res;
	}
	if ((res = parse_ie(services, sizeof(services) - 1, answer, len)) < 0) {
		cw_log(LOG_WARNING, "Failed to get services from NAPTR record\n");
		return -1;
	} else {
		answer += res;
		len -= res;
	}
	if ((res = parse_ie(regexp, sizeof(regexp) - 1, answer, len)) < 0) {
		cw_log(LOG_WARNING, "Failed to get regexp from NAPTR record\n");
		return -1;
	} else {
		answer += res;
		len -= res;
	}

	if ((res = dn_expand((unsigned char *)oanswer, (unsigned char *)answer + len, (unsigned char *)answer, repl, sizeof(repl) - 1)) < 0) {
		cw_log(LOG_WARNING, "Failed to expand hostname\n");
		return -1;
	}

	if (option_debug > 2)	/* Advanced NAPTR debugging */
		cw_log(LOG_DEBUG, "NAPTR input='%s', flags='%s', services='%s', regexp='%s', repl='%s'\n",
			naptrinput, flags, services, regexp, repl);

	if (tolower(flags[0]) != 'u') {
		cw_log(LOG_WARNING, "NAPTR Flag must be 'U' or 'u'.\n");
		return -1;
	}

	p = strstr(services, "e2u+");
	if (p == NULL)
		p = strstr(services, "E2U+");

	if (p) {
		p = p + 4;
		if(strchr(p, ':')) {
			p = strchr(p, ':') + 1;
		}
		cw_copy_string(tech_return, p, sizeof(tech_return));
	} else {
		p = strstr(services, "+e2u");
		if(p == NULL)
			p = strstr(services, "+E2U");
		if(p) {
			*p = 0;
			p = strchr(services, ':');
			if(p)
				*p = 0;
			cw_copy_string(tech_return, services, sizeof(tech_return));
		}
	}

	/* DEDBUGGING STUB
	cw_copy_string(regexp, "!^\\+43(.*)$!\\1@bla.fasel!", sizeof(regexp) - 1);
	*/

	regexp_len = strlen(regexp);
	if (regexp_len < 7) {
		cw_log(LOG_WARNING, "Regex too short to be meaningful.\n");
		return -1;
	}


	delim = regexp[0];
	delim2 = strchr(regexp + 1, delim);
	if ((delim2 == NULL) || (regexp[regexp_len-1] != delim)) {
		cw_log(LOG_WARNING, "Regex delimiter error (on \"%s\").\n",regexp);
		return -1;
	}

	pattern = regexp + 1;
	*delim2 = 0;
	subst   = delim2 + 1;
	regexp[regexp_len-1] = 0;

/*
 * now do the regex wizardry.
 */

	if (regcomp(&preg, pattern, REG_EXTENDED | REG_NEWLINE)) {
		cw_log(LOG_WARNING, "NAPTR Regex compilation error (regex = \"%s\").\n",regexp);
		return -1;
	}

	if (preg.re_nsub > 9) {
		cw_log(LOG_WARNING, "NAPTR Regex compilation error: too many subs.\n");
		regfree(&preg);
		return -1;
	}

	if (regexec(&preg, naptrinput, 9, pmatch, 0)) {
		cw_log(LOG_WARNING, "NAPTR Regex match failed.\n");
		regfree(&preg);
		return -1;
	}
	regfree(&preg);

	d = temp;
	d_len--;
	while( *subst && (d_len > 0) ) {
		if ((subst[0] == '\\') && isdigit(subst[1]) && (pmatch[subst[1]-'0'].rm_so != -1)) {
			backref = subst[1]-'0';
			size = pmatch[backref].rm_eo - pmatch[backref].rm_so;
			if (size > d_len) {
				cw_log(LOG_WARNING, "Not enough space during NAPTR regex substitution.\n");
				return -1;
				}
			memcpy(d, naptrinput + pmatch[backref].rm_so, size);
			d += size;
			d_len -= size;
			subst += 2;
		} else if (isprint(*subst)) {
			*d++ = *subst++;
			d_len--;
		} else {
			cw_log(LOG_WARNING, "Error during regex substitution.\n");
			return -1;
		}
	}
	*d = 0;
	cw_copy_string(dst, temp, dstsize);
	dst[dstsize - 1] = '\0';

	if(*tech != '\0'){ /* check if it is requested NAPTR */
		if(!strncasecmp(tech, "ALL", techsize)){
			return 1; /* return or count any RR */
		}
		if(!strncasecmp(tech_return, tech, sizeof(tech_return)<techsize?sizeof(tech_return):techsize)){
			cw_copy_string(tech, tech_return, techsize);
			return 1; /* we got out RR */
		} else { /* go to the next RR in the DNS answer */
			return 0;
		}
	}

	/* tech was not specified, return first parsed RR */
	cw_copy_string(tech, tech_return, techsize);

	return 1;
}

/* do not return requested value, just count RRs and return thei number in dst */
#define ENUMLOOKUP_OPTIONS_COUNT	1
/* use options field as a variable name pattern to set (e.g. name%d) */
#define ENUMLOOKUP_OPTIONS_ARRAY       2

struct enum_naptr_rr {
	struct naptr naptr; /* order and preference of RR */
	char *result; /* result of naptr parsing,e.g.: tel:+5553 */
	char *tech; /* Technology (from URL scheme) */
	int sort_pos; /* sort position */
};

struct enum_context {
	char *dst;	/* Destination part of URL from ENUM */
	int dstlen;	/* Length */
	char *tech;	/* Technology (from URL scheme) */
	int techlen;	/* Length */
	char *txt;	/* TXT record in TXT lookup */
	int txtlen;	/* Length */
	char *naptrinput;	/* The number to lookup */
	int position; /* used as counter for RRs or specifies position of required RR */
	int options; /* options , see ENUMLOOKUP_OPTIONS_* defined above */
	struct enum_naptr_rr *naptr_rrs; /* array of parsed NAPTR RRs */
	int naptr_rrs_count; /* Size of array naptr_rrs */
};

/*--- txt_callback: Callback for TXT record lookup */
static int txt_callback(void *context, char *answer, int len, char *fullanswer)
{
	struct enum_context *c = (struct enum_context *)context;

	if (answer == NULL) {
		c->txt = NULL;
		c->txtlen = 0;
		return 0;
	}

	/* skip over first byte, as for some reason it's a vertical tab character */
	answer += 1;
	len -= 1;

	/* answer is not null-terminated, but should be */
	/* this is safe to do, as answer has extra bytes on the end we can
	    safely overwrite with a null */
	answer[len] = '\0';
	/* now increment len so that len includes the null, so that we can
	   compare apples to apples */
	len +=1;

	/* finally, copy the answer into c->txt */
	cw_copy_string(c->txt, answer, len < c->txtlen ? len : (c->txtlen));

	/* just to be safe, let's make sure c->txt is null terminated */
	c->txt[(c->txtlen)-1] = '\0';

	return 1;
}

/*--- enum_callback: Callback from ENUM lookup function */
static int enum_callback(void *context, char *answer, int len, char *fullanswer)
{
	struct enum_context *c = (struct enum_context *)context;
	void *p = NULL;
	int res;

	res = parse_naptr(c->dst, c->dstlen, c->tech, c->techlen, answer, len, c->naptrinput);

	if(res < 0){
		cw_log(LOG_WARNING, "Failed to parse naptr :(\n");
		return -1;
	} else if(res > 0 && !cw_strlen_zero(c->dst)){ /* ok, we got needed NAPTR */
		if ((p = realloc(c->naptr_rrs, sizeof(*c->naptr_rrs) * (c->naptr_rrs_count + 1)))) {
			c->naptr_rrs = p;
			memcpy(&c->naptr_rrs[c->naptr_rrs_count].naptr, answer, sizeof(c->naptr_rrs->naptr));
			c->naptr_rrs[c->naptr_rrs_count].result = strdup(c->dst);
			c->naptr_rrs[c->naptr_rrs_count].tech = strdup(c->tech);
			c->naptr_rrs[c->naptr_rrs_count].sort_pos = c->naptr_rrs_count;
			c->naptr_rrs_count++;
		 }
		 return 0;
	}

	return 0;
}

/*--- cw_get_enum: ENUM lookup */
int cw_get_enum(struct cw_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen, char* suffix, char* options)
{
	struct enum_context context;
	char tmp[259 + 512];
	char naptrinput[512];
	int pos = strlen(number) - 1;
	int newpos = 0;
	int ret = -1;
	struct enum_search *s = NULL;
	int version = -1;
	/* for ISN rewrite */
	char *p1 = NULL;
	char *p2 = NULL;
	int k = 0;
	int i = 0;
	int z = 0;

	if(number[0] == 'n'){
		 strncpy(naptrinput, number+1, sizeof(naptrinput));
	} else {
		 strncpy(naptrinput, number, sizeof(naptrinput));
	}

	context.naptrinput = naptrinput;	/* The number */
	context.dst = dst;			/* Return string */
	context.dstlen = dstlen;
	context.tech = tech;
	context.techlen = techlen;
	context.options = 0;
	context.position = 1;
	context.naptr_rrs = NULL;
	context.naptr_rrs_count = 0;

	if(options != NULL){
		p1 = strchr(options, '%');
		if (p1 && p1[1] == 'd' && !strchr(p1+1, '%')) {
			context.options = ENUMLOOKUP_OPTIONS_ARRAY|ENUMLOOKUP_OPTIONS_COUNT;
			context.position = 0;
		} else if(*options == 'c'){
			  context.options = ENUMLOOKUP_OPTIONS_COUNT;
			  context.position = 0;
		 } else {
			  context.position = atoi(options);
			  if(context.position < 1)
				   context.position = 1;
		 }
	}

	if (pos > 128)
		pos = 128;

	/* ISN rewrite */
	p1 = strchr(number, '*');

	if(number[0] == 'n'){ /* do not perform ISN rewrite ('n' is testing flag) */
		 p1 = NULL;
		 k = 1; /* strip 'n' from number */
	}

	if(p1 != NULL){
		 p2 = p1+1;
		 while(p1 > number){
			  p1--;
			  tmp[newpos++] = *p1;
			  tmp[newpos++] = '.';
		 }
		 if(*p2){
			  while(*p2 && newpos < 128){
				   tmp[newpos++] = *p2;
				   p2++;
			  }
			  tmp[newpos++] = '.';
		 }

	} else {
		 while(pos >= k) {
			  if(isdigit(number[pos])){
				   tmp[newpos++] = number[pos];
				   tmp[newpos++] = '.';
			  }
			  pos--;
		 }
	}

	if (chan && cw_autoservice_start(chan) < 0)
		return -1;

	for(;;) {
		cw_mutex_lock(&enumlock);
		if (version != enumver) {
			/* Ooh, a reload... */
			s = toplevs;
			version = enumver;
		} else {
			s = s->next;
		}
		 if(suffix != NULL){
			  strncpy(tmp + newpos, suffix, sizeof(tmp) - newpos - 1);
		 } else if (s) {
			strncpy(tmp + newpos, s->toplev, sizeof(tmp) - newpos - 1);
		}
		cw_mutex_unlock(&enumlock);
		if (!s)
			break;
		ret = cw_search_dns(&context, tmp, C_IN, T_NAPTR, enum_callback);
		if (ret > 0)
			break;
		 if(suffix != NULL)
			  break;
	}
	if (ret < 0) {
		cw_log(LOG_DEBUG, "No such number found: %s (%s)\n", tmp, strerror(errno));
		ret = 0;
	}

	if (context.naptr_rrs_count >= context.position || (context.options & ENUMLOOKUP_OPTIONS_ARRAY)) {
		 /* sort array by NAPTR order/preference */
		 for(k=0; k<context.naptr_rrs_count; k++){
			  for(i=0; i<context.naptr_rrs_count; i++){
				   /* use order first and then preference to compare */
				   if((ntohs(context.naptr_rrs[k].naptr.order) < ntohs(context.naptr_rrs[i].naptr.order)
						     && context.naptr_rrs[k].sort_pos > context.naptr_rrs[i].sort_pos)
					    || (ntohs(context.naptr_rrs[k].naptr.order) > ntohs(context.naptr_rrs[i].naptr.order)
						     && context.naptr_rrs[k].sort_pos < context.naptr_rrs[i].sort_pos)){
					    z = context.naptr_rrs[k].sort_pos;
					    context.naptr_rrs[k].sort_pos = context.naptr_rrs[i].sort_pos;
					    context.naptr_rrs[i].sort_pos = z;
					    continue;
				   }
				   if(ntohs(context.naptr_rrs[k].naptr.order) == ntohs(context.naptr_rrs[i].naptr.order)){
					    if((ntohs(context.naptr_rrs[k].naptr.pref) < ntohs(context.naptr_rrs[i].naptr.pref)
							      && context.naptr_rrs[k].sort_pos > context.naptr_rrs[i].sort_pos)
						     || (ntohs(context.naptr_rrs[k].naptr.pref) > ntohs(context.naptr_rrs[i].naptr.pref)
							      && context.naptr_rrs[k].sort_pos < context.naptr_rrs[i].sort_pos)){
						     z = context.naptr_rrs[k].sort_pos;
						     context.naptr_rrs[k].sort_pos = context.naptr_rrs[i].sort_pos;
						     context.naptr_rrs[i].sort_pos = z;
					    }
				   }
			  }
		 }
		if ((context.options & ENUMLOOKUP_OPTIONS_ARRAY)) {
			for (k = 0; k < context.naptr_rrs_count; k++) {
				if (snprintf(dst, dstlen, options, context.naptr_rrs[k].sort_pos+1) >= dstlen) {
					cw_log(LOG_WARNING, "ENUM buffer too small setting result vars!");
					break;
				} else {
					pbx_builtin_setvar_helper(chan, dst, context.naptr_rrs[k].result);	
				}
			}
		} else {
			for (k = 0; k < context.naptr_rrs_count; k++) {
				if (context.naptr_rrs[k].sort_pos == context.position-1) {
					cw_copy_string(dst, context.naptr_rrs[k].result, dstlen);
					cw_copy_string(tech, context.naptr_rrs[k].tech, techlen);
					break;
				}
			}
		}
	}
	if ((context.options & ENUMLOOKUP_OPTIONS_COUNT)) {
		snprintf(dst, dstlen, "%d", context.naptr_rrs_count);
	}

	if (chan)
		ret |= cw_autoservice_stop(chan);

	for(k=0; k<context.naptr_rrs_count; k++){
		 free(context.naptr_rrs[k].result);
		 free(context.naptr_rrs[k].tech);
	}

	free(context.naptr_rrs);

	return ret;
}

/*--- cw_get_txt: Get TXT record from DNS.
	Really has nothing to do with enum, but anyway...
 */
int cw_get_txt(struct cw_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen, char *txt, int txtlen)
{
	struct enum_context context;
	char tmp[259 + 512];
	char naptrinput[512] = "+";
	int pos = strlen(number) - 1;
	int newpos = 0;
	int ret = -1;
	struct enum_search *s = NULL;
	int version = -1;

	strncat(naptrinput, number, sizeof(naptrinput) - 2);

	context.naptrinput = naptrinput;
	context.dst = dst;
	context.dstlen = dstlen;
	context.tech = tech;
	context.techlen = techlen;
	context.txt = txt;
	context.txtlen = txtlen;

	if (pos > 128)
		pos = 128;
	while(pos >= 0) {
		tmp[newpos++] = number[pos--];
		tmp[newpos++] = '.';
	}

	if (chan && cw_autoservice_start(chan) < 0)
		return -1;

	for(;;) {
		cw_mutex_lock(&enumlock);
		if (version != enumver) {
			/* Ooh, a reload... */
			s = toplevs;
			version = enumver;
		} else {
			s = s->next;
		}
		if (s) {
			strncpy(tmp + newpos, s->toplev, sizeof(tmp) - newpos - 1);
		}
		cw_mutex_unlock(&enumlock);
		if (!s)
			break;

		ret = cw_search_dns(&context, tmp, C_IN, T_TXT, txt_callback);
		if (ret > 0)
			break;
	}
	if (ret < 0) {
		cw_log(LOG_DEBUG, "No such number found: %s (%s)\n", tmp, strerror(errno));
		ret = 0;
	}
	if (chan)
		ret |= cw_autoservice_stop(chan);
	return ret;
}

/*--- enum_newtoplev: Add enum tree to linked list ---*/
static struct enum_search *enum_newtoplev(char *s)
{
	struct enum_search *tmp;

	tmp = malloc(sizeof(struct enum_search));
	if (tmp) {
		memset(tmp, 0, sizeof(struct enum_search));
		cw_copy_string(tmp->toplev, s, sizeof(tmp->toplev));
	}
	return tmp;
}

/*--- cw_enum_init: Initialize the ENUM support subsystem */
int cw_enum_init(void)
{
	struct cw_config *cfg;
	struct enum_search *s, *sl;
	struct cw_variable *v;

	/* Destroy existing list */
	cw_mutex_lock(&enumlock);
	s = toplevs;
	while(s) {
		sl = s;
		s = s->next;
		free(sl);
	}
	toplevs = NULL;
	cfg = cw_config_load("enum.conf");
	if (cfg) {
		sl = NULL;
		v = cw_variable_browse(cfg, "general");
		while(v) {
			if (!strcasecmp(v->name, "search")) {
				s = enum_newtoplev(v->value);
				if (s) {
					if (sl)
						sl->next = s;
					else
						toplevs = s;
					sl = s;
				}
			}
			v = v->next;
		}
		cw_config_destroy(cfg);
	} else {
		toplevs = enum_newtoplev(TOPLEV);
	}
	enumver++;
	cw_mutex_unlock(&enumlock);
	return 0;
}

int cw_enum_reload(void)
{
	return cw_enum_init();
}
