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
 * \brief Terminal Routines 
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/corelib/term.c $", "$Revision: 4723 $")

#include "callweaver/term.h"
#include "callweaver/options.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"

static int vt100compat = 0;

static char prepdata[80] = "";
static char enddata[80] = "";
static char quitdata[80] = "";

static const char *termpath[] = {
	"/usr/share/terminfo",
	"/usr/local/share/misc/terminfo",
	"/usr/lib/terminfo",
	NULL
	};

/* Ripped off from Ross Ridge, but it's public domain code (libmytinfo) */
static short convshort(char *s)
{
	register int a,b;

	a = (int) s[0] & 0377;
	b = (int) s[1] & 0377;

	if (a == 0377 && b == 0377)
		return -1;
	if (a == 0376 && b == 0377)
		return -2;

	return a + b * 256;
}


int cw_term_init(void)
{
	char *term = getenv("TERM");
	char termfile[256] = "";
	char buffer[512] = "";
	int termfd = -1, parseokay = 0, i;

	if (!term)
		return 0;

	if ( option_nocolor )
		return 0;

	for (i=0 ;; i++) {
		if (termpath[i] == NULL) {
			break;
		}
		snprintf(termfile, sizeof(termfile), "%s/%c/%s", termpath[i], *term, term);
		termfd = open(termfile, O_RDONLY);
		if (termfd > -1) {
			break;
		}
	}
	if (termfd > -1) {
		int actsize = read(termfd, buffer, sizeof(buffer) - 1);
		short sz_names = convshort(buffer + 2);
		short sz_bools = convshort(buffer + 4);
		short n_nums   = convshort(buffer + 6);


		if (sz_names + sz_bools + n_nums < actsize) {
			short max_colors = convshort(buffer + 12 + sz_names + sz_bools + 13 * 2);
			if (max_colors > 0) {
				vt100compat = 1;
			}
			parseokay = 1;
		}
		close(termfd);
	}

	if (!parseokay) {
		if (!strcmp(term, "linux")) {
			vt100compat = 1;
		} else if (!strcmp(term, "xterm")) {
			vt100compat = 1;
		} else if (!strcmp(term, "xterm-color")) {
			vt100compat = 1;
		} else if (!strncmp(term, "Eterm", 5)) {
			vt100compat = 1;
		} else if (!strcmp(term, "vt100")) {
			vt100compat = 1;
		} else if (!strncmp(term, "crt", 3)) {
			vt100compat = 1;
		}
	}

	if (vt100compat) {
		snprintf(prepdata, sizeof(prepdata), "%c[%d;%d;%dm", ESC, ATTR_BRIGHT, COLOR_BROWN, COLOR_BLACK + 10);
		snprintf(enddata, sizeof(enddata), "%c[%d;%d;%dm", ESC, ATTR_RESET, COLOR_WHITE, COLOR_BLACK + 10);
		snprintf(quitdata, sizeof(quitdata), "%c[0m", ESC);
	}
	return 0;
}


char *cw_term_color(char *outbuf, const char *inbuf, int fgcolor, int bgcolor, int maxout)
{
	int attr=0;
	char tmp[40];
	if (!vt100compat) {
		cw_copy_string(outbuf, inbuf, maxout);
		return outbuf;
	}
	if (!fgcolor && !bgcolor) {
		cw_copy_string(outbuf, inbuf, maxout);
		return outbuf;
	}
	if ((fgcolor & 128) && (bgcolor & 128)) {
		/* Can't both be highlighted */
		cw_copy_string(outbuf, inbuf, maxout);
		return outbuf;
	}
	if (!bgcolor)
		bgcolor = COLOR_BLACK;

	if (bgcolor) {
		bgcolor &= ~128;
		bgcolor += 10;
	}
	if (fgcolor & 128) {
		attr = ATTR_BRIGHT;
		fgcolor &= ~128;
	}
	if (fgcolor && bgcolor) {
		snprintf(tmp, sizeof(tmp), "%d;%d", fgcolor, bgcolor);
	} else if (bgcolor) {
		snprintf(tmp, sizeof(tmp), "%d", bgcolor);
	} else if (fgcolor) {
		snprintf(tmp, sizeof(tmp), "%d", fgcolor);
	}
	if (attr) {
		snprintf(outbuf, maxout, "%c[%d;%sm%s%c[0;%d;%dm", ESC, attr, tmp, inbuf, ESC, COLOR_WHITE, COLOR_BLACK + 10);
	} else {
		snprintf(outbuf, maxout, "%c[%sm%s%c[0;%d;%dm", ESC, tmp, inbuf, ESC, COLOR_WHITE, COLOR_BLACK + 10);
	}
	return outbuf;
}

char *cw_term_color_code(char *outbuf, int fgcolor, int bgcolor, int maxout)
{
	int attr=0;
	char tmp[40];
	if ((!vt100compat) || (!fgcolor && !bgcolor)) {
		*outbuf = '\0';
		return outbuf;
	}
	if ((fgcolor & 128) && (bgcolor & 128)) {
		/* Can't both be highlighted */
		*outbuf = '\0';
		return outbuf;
	}
	if (!bgcolor)
		bgcolor = COLOR_BLACK;

	if (bgcolor) {
		bgcolor &= ~128;
		bgcolor += 10;
	}
	if (fgcolor & 128) {
		attr = ATTR_BRIGHT;
		fgcolor &= ~128;
	}
	if (fgcolor && bgcolor) {
		snprintf(tmp, sizeof(tmp), "%d;%d", fgcolor, bgcolor);
	} else if (bgcolor) {
		snprintf(tmp, sizeof(tmp), "%d", bgcolor);
	} else if (fgcolor) {
		snprintf(tmp, sizeof(tmp), "%d", fgcolor);
	}
	if (attr) {
		snprintf(outbuf, maxout, "%c[%d;%sm", ESC, attr, tmp);
	} else {
		snprintf(outbuf, maxout, "%c[%sm", ESC, tmp);
	}
	return outbuf;
}

char *cw_term_strip(char *outbuf, char *inbuf, int maxout)
{
	char *outbuf_ptr = outbuf, *inbuf_ptr = inbuf;

	while (outbuf_ptr < outbuf + maxout) {
		switch (*inbuf_ptr) {
			case ESC:
				while (*inbuf_ptr && (*inbuf_ptr != 'm'))
					inbuf_ptr++;
				break;
			default:
				*outbuf_ptr = *inbuf_ptr;
				outbuf_ptr++;
		}
		if (! *inbuf_ptr)
			break;
		inbuf_ptr++;
	}
	return outbuf;
}

char *cw_term_prompt(char *outbuf, const char *inbuf, int maxout)
{
	if (!vt100compat) {
		cw_copy_string(outbuf, inbuf, maxout);
		return outbuf;
	}
	snprintf(outbuf, maxout, "%c[%d;%d;%dm%c%c[%d;%d;%dm%s",
		ESC, ATTR_BRIGHT, COLOR_BLUE, COLOR_BLACK + 10,
		inbuf[0],
		ESC, 0, COLOR_WHITE, COLOR_BLACK + 10,
		inbuf + 1);
	return outbuf;
}

char *cw_term_prep(void)
{
	return prepdata;
}

char *cw_term_end(void)
{
	return enddata;
}


char *cw_term_quit(void)
{
	return quitdata;
}
