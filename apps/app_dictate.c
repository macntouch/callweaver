/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * Donated by Sangoma Technologies <http://www.samgoma.com>
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
 * \brief Virtual Dictation Machine Application For CallWeaver
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>	/* for mkdir */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_dictate.c $", "$Revision: 4723 $")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/say.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"

static char *tdesc = "Virtual Dictation Machine";

static void *dictate_app;
static char *dictate_name = "Dictate";
static char *dictate_synopsis = "Virtual Dictation Machine";
static char *dictate_syntax = "Dictate([base_dir])";
static char *dictate_descrip = "Start dictation machine using optional base dir for files.\n";


STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

typedef enum {
	DFLAG_RECORD = (1 << 0),
	DFLAG_PLAY = (1 << 1),
	DFLAG_TRUNC = (1 << 2),
	DFLAG_PAUSE = (1 << 3),
} dflags;

typedef enum {
	DMODE_INIT,
	DMODE_RECORD,
	DMODE_PLAY
} dmodes;

#define cw_toggle_flag(it,flag) if(cw_test_flag(it, flag)) cw_clear_flag(it, flag); else cw_set_flag(it, flag)

static int play_and_wait(struct cw_channel *chan, char *file, char *digits) 
{
	int res = -1;
	if (!cw_streamfile(chan, file, chan->language)) {
		res = cw_waitstream(chan, digits);
	}
	return res;
}

static int dictate_exec(struct cw_channel *chan, int argc, char **argv)
{
	char *path = NULL, filein[256];
	char dftbase[256];
	char *base;
	struct cw_flags flags = {0};
	struct cw_filestream *fs;
	struct cw_frame *f = NULL;
	struct localuser *u;
	int ffactor = 320 * 80,
		res = 0,
		done = 0,
		oldr = 0,
		lastop = 0,
		samples = 0,
		speed = 1,
		digit = 0,
		len = 0,
		maxlen = 0,
		mode = 0;
		
	LOCAL_USER_ADD(u);
	
	snprintf(dftbase, sizeof(dftbase), "%s/dictate", cw_config_CW_SPOOL_DIR);
	
	base = (argc > 0 && argv[0][0] ? argv[0] : dftbase);

	oldr = chan->readformat;
	if ((res = cw_set_read_format(chan, CW_FORMAT_SLINEAR)) < 0) {
		cw_log(LOG_WARNING, "Unable to set to linear mode.\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	cw_answer(chan);
	cw_safe_sleep(chan, 200);
	for(res = 0; !res;) {
		if (cw_app_getdata(chan, "dictate/enter_filename", filein, sizeof(filein), 0) || 
			cw_strlen_zero(filein)) {
			res = -1;
			break;
		}
		
		mkdir(base, 0755);
		len = strlen(base) + strlen(filein) + 2;
		if (!path || len > maxlen) {
			path = alloca(len);
			memset(path, 0, len);
			maxlen = len;
		} else {
			memset(path, 0, maxlen);
		}

		snprintf(path, len, "%s/%s", base, filein);
		fs = cw_writefile(path, "wav", NULL, O_CREAT|O_APPEND, 0, 0700);
		mode = DMODE_PLAY;
		memset(&flags, 0, sizeof(flags));
		cw_set_flag(&flags, DFLAG_PAUSE);
		digit = play_and_wait(chan, "dictate/forhelp", CW_DIGIT_ANY);
		done = 0;
		speed = 1;
		res = 0;
		lastop = 0;
		samples = 0;
		while (!done && ((res = cw_waitfor(chan, -1)) > -1) && fs && (f = cw_read(chan))) {
			if (digit) {
				struct cw_frame fr = {CW_FRAME_DTMF, digit};
				cw_queue_frame(chan, &fr);
				digit = 0;
			}
			if ((f->frametype == CW_FRAME_DTMF)) {
				int got = 1;
				switch(mode) {
				case DMODE_PLAY:
					switch(f->subclass) {
					case '1':
						cw_set_flag(&flags, DFLAG_PAUSE);
						mode = DMODE_RECORD;
						break;
					case '2':
						speed++;
						if (speed > 4) {
							speed = 1;
						}
						res = cw_say_number(chan, speed, CW_DIGIT_ANY, chan->language, (char *) NULL);
						break;
					case '7':
						samples -= ffactor;
						if(samples < 0) {
							samples = 0;
						}
						cw_seekstream(fs, samples, SEEK_SET);
						break;
					case '8':
						samples += ffactor;
						cw_seekstream(fs, samples, SEEK_SET);
						break;
						
					default:
						got = 0;
					}
					break;
				case DMODE_RECORD:
					switch(f->subclass) {
					case '1':
						cw_set_flag(&flags, DFLAG_PAUSE);
						mode = DMODE_PLAY;
						break;
					case '8':
						cw_toggle_flag(&flags, DFLAG_TRUNC);
						lastop = 0;
						break;
					default:
						got = 0;
					}
					break;
				default:
					got = 0;
				}
				if (!got) {
					switch(f->subclass) {
					case '#':
						done = 1;
						continue;
						break;
					case '*':
						cw_toggle_flag(&flags, DFLAG_PAUSE);
						if (cw_test_flag(&flags, DFLAG_PAUSE)) {
							digit = play_and_wait(chan, "dictate/pause", CW_DIGIT_ANY);
						} else {
							digit = play_and_wait(chan, mode == DMODE_PLAY ? "dictate/playback" : "dictate/record", CW_DIGIT_ANY);
						}
						break;
					case '0':
						cw_set_flag(&flags, DFLAG_PAUSE);
						digit = play_and_wait(chan, "dictate/paused", CW_DIGIT_ANY);
						switch(mode) {
						case DMODE_PLAY:
							digit = play_and_wait(chan, "dictate/play_help", CW_DIGIT_ANY);
							break;
						case DMODE_RECORD:
							digit = play_and_wait(chan, "dictate/record_help", CW_DIGIT_ANY);
							break;
						}
						if (digit == 0) {
							digit = play_and_wait(chan, "dictate/both_help", CW_DIGIT_ANY);
						} else if (digit < 0) {
							done = 1;
							break;
						}
						break;
					}
				}
				
			} else if (f->frametype == CW_FRAME_VOICE) {
				switch(mode) {
					struct cw_frame *fr;
					int x;
				case DMODE_PLAY:
					if (lastop != DMODE_PLAY) {
						if (cw_test_flag(&flags, DFLAG_PAUSE)) {
							digit = play_and_wait(chan, "dictate/playback_mode", CW_DIGIT_ANY);
							if (digit == 0) {
								digit = play_and_wait(chan, "dictate/paused", CW_DIGIT_ANY);
							} else if (digit < 0) {
								break;
							}
						}
						if (lastop != DFLAG_PLAY) {
							lastop = DFLAG_PLAY;
							cw_closestream(fs);
							fs = cw_openstream(chan, path, chan->language);
							cw_seekstream(fs, samples, SEEK_SET);
							chan->stream = NULL;
						}
						lastop = DMODE_PLAY;
					}

					if (!cw_test_flag(&flags, DFLAG_PAUSE)) {
						for (x = 0; x < speed; x++) {
							if ((fr = cw_readframe(fs))) {
								cw_write(chan, fr);
								samples += fr->samples;
								cw_fr_free(fr);
								fr = NULL;
							} else {
								samples = 0;
								cw_seekstream(fs, 0, SEEK_SET);
							}
						}
					}
					break;
				case DMODE_RECORD:
					if (lastop != DMODE_RECORD) {
						int oflags = O_CREAT | O_WRONLY;
						if (cw_test_flag(&flags, DFLAG_PAUSE)) {						
							digit = play_and_wait(chan, "dictate/record_mode", CW_DIGIT_ANY);
							if (digit == 0) {
								digit = play_and_wait(chan, "dictate/paused", CW_DIGIT_ANY);
							} else if (digit < 0) {
								break;
							}
						}
						lastop = DMODE_RECORD;
						cw_closestream(fs);
						if ( cw_test_flag(&flags, DFLAG_TRUNC)) {
							oflags |= O_TRUNC;
							digit = play_and_wait(chan, "dictate/truncating_audio", CW_DIGIT_ANY);
						} else {
							oflags |= O_APPEND;
						}
						fs = cw_writefile(path, "wav", NULL, oflags, 0, 0700);
						if (cw_test_flag(&flags, DFLAG_TRUNC)) {
							cw_seekstream(fs, 0, SEEK_SET);
							cw_clear_flag(&flags, DFLAG_TRUNC);
						} else {
							cw_seekstream(fs, 0, SEEK_END);
						}
					}
					if (!cw_test_flag(&flags, DFLAG_PAUSE)) {
						res = cw_writestream(fs, f);
					}
					break;
				}
			}
			cw_fr_free(f);
		}
	}
	if (oldr) {
		cw_set_read_format(chan, oldr);
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(dictate_app);
	return res;
}

int load_module(void)
{
	dictate_app = cw_register_application(dictate_name, dictate_exec, dictate_synopsis, dictate_syntax, dictate_descrip);
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



