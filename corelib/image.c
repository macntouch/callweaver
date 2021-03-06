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
 * \brief Image Management
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/corelib/image.c $", "$Revision: 4723 $")

#include "callweaver/sched.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/file.h"
#include "callweaver/image.h"
#include "callweaver/translate.h"
#include "callweaver/cli.h"
#include "callweaver/lock.h"

static struct cw_imager *list;
CW_MUTEX_DEFINE_STATIC(listlock);

int cw_image_register(struct cw_imager *img)
{
	if (option_verbose > 1)
		cw_verbose(VERBOSE_PREFIX_2 "Registered format '%s' (%s)\n", img->name, img->desc);
	cw_mutex_lock(&listlock);
	img->next = list;
	list = img;
	cw_mutex_unlock(&listlock);
	return 0;
}

void cw_image_unregister(struct cw_imager *img)
{
	struct cw_imager *i, *prev = NULL;
	cw_mutex_lock(&listlock);
	i = list;
	while(i) {
		if (i == img) {
			if (prev) 
				prev->next = i->next;
			else
				list = i->next;
			break;
		}
		prev = i;
		i = i->next;
	}
	cw_mutex_unlock(&listlock);
	if (i && (option_verbose > 1))
		cw_verbose(VERBOSE_PREFIX_2 "Unregistered format '%s' (%s)\n", img->name, img->desc);
}

int cw_supports_images(struct cw_channel *chan)
{
	if (!chan || !chan->tech)
		return 0;
	if (!chan->tech->send_image)
		return 0;
	return 1;
}

static int file_exists(char *filename)
{
	int res;
	struct stat st;
	res = stat(filename, &st);
	if (!res)
		return st.st_size;
	return 0;
}

static void make_filename(char *buf, int len, char *filename, char *preflang, char *ext)
{
	if (filename[0] == '/') {
		if (preflang && strlen(preflang))
			snprintf(buf, len, "%s-%s.%s", filename, preflang, ext);
		else
			snprintf(buf, len, "%s.%s", filename, ext);
	} else {
		if (preflang && strlen(preflang))
			snprintf(buf, len, "%s/%s/%s-%s.%s", cw_config_CW_VAR_DIR, "images", filename, preflang, ext);
		else
			snprintf(buf, len, "%s/%s/%s.%s", cw_config_CW_VAR_DIR, "images", filename, ext);
	}
}

struct cw_frame *cw_read_image(char *filename, char *preflang, int format)
{
	struct cw_imager *i;
	char buf[256];
	char tmp[80];
	char *e;
	struct cw_imager *found = NULL;
	int fd;
	int len=0;
	struct cw_frame *f = NULL;
#if 0 /* We need to have some sort of read-only lock */
	cw_mutex_lock(&listlock);
#endif	
	i = list;
	while(!found && i) {
		if (i->format & format) {
			char *stringp=NULL;
			strncpy(tmp, i->exts, sizeof(tmp)-1);
			stringp=tmp;
			e = strsep(&stringp, "|,");
			while(e) {
				make_filename(buf, sizeof(buf), filename, preflang, e);
				if ((len = file_exists(buf))) {
					found = i;
					break;
				}
				make_filename(buf, sizeof(buf), filename, NULL, e);
				if ((len = file_exists(buf))) {
					found = i;
					break;
				}
				e = strsep(&stringp, "|,");
			}
		}
		i = i->next;
	}
	if (found) {
		fd = open(buf, O_RDONLY);
		if (fd > -1) {
			if (!found->identify || found->identify(fd)) {
				/* Reset file pointer */
				lseek(fd, 0, SEEK_SET);
				f = found->read_image(fd,len); 
			} else
				cw_log(LOG_WARNING, "%s does not appear to be a %s file\n", buf, i->name);
			close(fd);
		} else
			cw_log(LOG_WARNING, "Unable to open '%s': %s\n", buf, strerror(errno));
	} else
		cw_log(LOG_WARNING, "Image file '%s' not found\n", filename);
#if 0
	cw_mutex_unlock(&listlock);
#endif	
	return f;
}


int cw_send_image(struct cw_channel *chan, char *filename)
{
	struct cw_frame *f;
	int res = -1;

	if (chan->tech->send_image) {
		f = cw_read_image(filename, chan->language, -1);
		if (f) {
			res = chan->tech->send_image(chan, f);
			cw_fr_free(f);
		}
	}
	return res;
}

static int show_image_formats(int fd, int argc, char *argv[])
{
#define FORMAT "%10s %10s %50s %10s\n"
#define FORMAT2 "%10s %10s %50s %10s\n"
	struct cw_imager *i;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	cw_cli(fd, FORMAT, "Name", "Extensions", "Description", "Format");
	i = list;
	while(i) {
		cw_cli(fd, FORMAT2, i->name, i->exts, i->desc, cw_getformatname(i->format));
		i = i->next;
	};
	return RESULT_SUCCESS;
}

struct cw_cli_entry show_images =
{
	{ "show", "image", "formats" },
	show_image_formats,
	"Displays image formats",
"Usage: show image formats\n"
"       displays currently registered image formats (if any)\n"
};


int cw_image_init(void)
{
	cw_cli_register(&show_images);
	return 0;
}

