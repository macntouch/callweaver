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
 * \brief Playback a file with audio detect
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_backgrounddetect.c $", "$Revision: 4723 $")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/utils.h"
#include "callweaver/dsp.h"

static char *tdesc = "Playback with Talk and Fax Detection";

static void *background_detect_app;
static const char *background_detect_name = "BackgroundDetect";
static const char *background_detect_synopsis = "Background a file with Talk and Fax detect";
static const char *background_detect_syntax = "BackgroundDetect(filename[, options[, sildur[, mindur|, maxdur]]]])";
static const char *background_detect_descrip = 
"Parameters:\n"
"      filename: File to play in the background.\n"
"      options:\n"
"        'n':     Attempt on-hook if unanswered (default=no)\n"
"        'x':     DTMF digits terminate without extension (default=no)\n"
"        'd':     Ignore DTMF digit detection (default=no)\n"
"        'D':     DTMF digit detection and (default=no)\n"
"                 jump do the dialled extension after pressing #\n"
"                 Disables single digit jumps#\n"
"        'f':     Ignore fax detection (default=no)\n"
"        't':     Ignore talk detection (default=no)\n"
"        'j':     To be used in OGI scripts. Does not jump to any extension.\n"
"        sildur:  Silence ms after mindur/maxdur before aborting (default=1000)\n"
"        mindur:  Minimum non-silence ms needed (default=100)\n"
"        maxdur:  Maximum non-silence ms allowed (default=0/forever)\n"
"\n"
"Plays back a given filename, waiting for interruption from a given digit \n"
"(the digit must start the beginning of a valid extension, or it will be ignored).\n"
"If D option is specified (overrides d), the application will wait for\n"
"the user to enter digits terminated by a # and jump to the corresponding\n"
"extension, if it exists.\n"
"During the playback of the file, audio is monitored in the receive\n"
"direction, and if a period of non-silence which is greater than 'min' ms\n"
"yet less than 'max' ms is followed by silence for at least 'sil' ms then\n"
"the audio playback is aborted and processing jumps to the 'talk' extension\n"
"if available.  If unspecified, sil, min, and max default to 1000, 100, and\n"
"infinity respectively.\n"
"If all undetected, control will continue at the next priority.\n"
"The application, upon exit, will set the folloging channel variables: \n"
"   - DTMF_DETECTED : set to 1 when a DTMF digit would have caused a jump.\n"
"   - TALK_DETECTED : set to 1 when talk has been detected.\n"
"   - FAX_DETECTED  : set to 1 when fax tones detected.\n"
"   - FAXEXTEN      : original dialplan extension of this application\n"
"   - DTMF_DID      : digits dialled beforeexit (#excluded)\n"
"Returns -1 on hangup, and 0 on successful completion with no exit conditions.\n"
"\n";

#define CALLERID_FIELD cid.cid_num

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int background_detect_exec(struct cw_channel *chan, int argc, char **argv)
{
	char dtmf_did[256] = "\0";
	struct timeval start = { 0, 0};
	struct cw_dsp *dsp;
	struct localuser *u;
	struct cw_frame *fr = NULL, *fr2 = NULL;
	int res = 0;
	int notsilent = 0;
	int sil;
	int min;
	int max;
	int origrformat = 0;
	int skipanswer = 0;
	int ignoredtmf = 0;
	int ignorefax = 0;
	int ignoretalk = 0;
	int ignorejump = 0;
	int features = 0;
	int noextneeded = 0;
	int longdtmf = 1;

	pbx_builtin_setvar_helper(chan, "FAX_DETECTED", "0");
	pbx_builtin_setvar_helper(chan, "FAXEXTEN", "unknown");
	pbx_builtin_setvar_helper(chan, "DTMF_DETECTED", "0");
	pbx_builtin_setvar_helper(chan, "TALK_DETECTED", "0");
	pbx_builtin_setvar_helper(chan, "DTMF_DID", "");
	
	if (argc < 1 || argc > 5) {
		cw_log(LOG_ERROR, "Syntax: %s\n", background_detect_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);

	if (argc > 1) for (; argv[1][0]; argv[1]++) {
		switch (argv[1][0]) {
			case 'n': skipanswer = 1; break;
			case 'x': noextneeded = 1; break;
			case 'd': ignoredtmf = 1; break;
			case 'D': ignoredtmf = 0; longdtmf = 1; break;
			case 'f': ignorefax = 1; break;
			case 't': ignoretalk = 1; break;
			case 'j': ignorejump = 1; break;
		}
	}

	sil = (argc > 2 ? atoi(argv[2]) : 1000);
	min = (argc > 3 ? atoi(argv[3]) : 100);
	max = (argc > 4 ? atoi(argv[4]) : -1);

	cw_log(LOG_DEBUG, "Preparing detect of '%s', sil=%d,min=%d,max=%d\n", argv[0], sil, min, max);

	if (chan->_state != CW_STATE_UP && !skipanswer) {
		// Otherwise answer unless we're supposed to send this while on-hook 
		res = cw_answer(chan);
	}
	if (!res) {
		origrformat = chan->readformat;
		if ((res = cw_set_read_format(chan, CW_FORMAT_SLINEAR))) 
			cw_log(LOG_WARNING, "Unable to set read format to linear!\n");
	}
	if (!(dsp = cw_dsp_new())) {
		cw_log(LOG_WARNING, "Unable to allocate DSP!\n");
		res = -1;
	}

	if (dsp) {
		if (!ignoretalk)
			; // features |= DSP_FEATURE_SILENCE_SUPPRESS; 
		if (!ignorefax)
			features |= DSP_FEATURE_FAX_CNG_DETECT;
		//if (!ignoredtmf)
			features |= DSP_FEATURE_DTMF_DETECT;

		cw_dsp_set_threshold(dsp, 256);
		cw_dsp_set_features(dsp, features | DSP_DIGITMODE_RELAXDTMF);
		cw_dsp_digitmode(dsp, DSP_DIGITMODE_DTMF);
	}

	if (!res) {
		cw_stopstream(chan);
		res = cw_streamfile(chan, argv[0], chan->language);
		if (!res) {
			while(chan->stream) {
				res = cw_sched_wait(chan->sched);
				if (res < 0) {
					res = 0;
					break;
				}

				/* Check for a T38 switchover */
				if (chan->t38_status == T38_NEGOTIATED && !ignorefax) {
				    cw_log(LOG_DEBUG, "Fax detected on %s. T38 switchover completed.\n", chan->name);
				    pbx_builtin_setvar_helper(chan, "FAX_DETECTED", "1");
				    pbx_builtin_setvar_helper(chan,"FAXEXTEN",chan->exten);
				    if (!ignorejump) {
					if (strcmp(chan->exten, "fax")) {
					    cw_log(LOG_NOTICE, "Redirecting %s to fax extension [T38]\n", chan->name);
					    if (cw_exists_extension(chan, chan->context, "fax", 1, chan->CALLERID_FIELD)) {
						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
						strncpy(chan->exten, "fax", sizeof(chan->exten)-1);
						chan->priority = 0;									
					    } else
						cw_log(LOG_WARNING, "Fax detected, but no fax extension\n");
					} else
			    		    cw_log(LOG_WARNING, "Already in a fax extension, not redirecting\n");
				    }
				    res = 0;
				    cw_fr_free(fr);
				    break;
				}

				// NOW let's check for incoming RTP audio
				res = cw_waitfor(chan, res);

				if (res < 0) {
					cw_log(LOG_WARNING, "Waitfor failed on %s\n", chan->name);
					break;
				} else if (res > 0) {
					fr = cw_read(chan);
					if (!fr) {
						cw_log(LOG_DEBUG, "Got hangup\n");
						res = -1;
						break;
					}

					fr2 = cw_dsp_process(chan, dsp, fr);
					if (!fr2) {
						cw_log(LOG_WARNING, "Bad DSP received (what happened?)\n");
						fr2 = fr;
					}

					if (fr2->frametype == CW_FRAME_DTMF) {
					    if (fr2->subclass == 'f' && !ignorefax) {
						// Fax tone -- Handle and return NULL
						cw_log(LOG_DEBUG, "Fax detected on %s\n", chan->name);
						pbx_builtin_setvar_helper(chan, "FAX_DETECTED", "1");
						pbx_builtin_setvar_helper(chan,"FAXEXTEN",chan->exten);
						if (!ignorejump) {
						    if (strcmp(chan->exten, "fax")) {
							cw_log(LOG_NOTICE, "Redirecting %s to fax extension [DTMF]\n", chan->name);
							if (cw_exists_extension(chan, chan->context, "fax", 1, chan->CALLERID_FIELD)) {
							    // Save the DID/DNIS when we transfer the fax call to a "fax" extension 
							    strncpy(chan->exten, "fax", sizeof(chan->exten)-1);
							    chan->priority = 0;
							} else
							    cw_log(LOG_WARNING, "Fax detected, but no fax extension\n");
						    } else
							cw_log(LOG_WARNING, "Already in a fax extension, not redirecting\n");
						}
						res = 0;
						cw_fr_free(fr);
						break;
					    } else if (!ignoredtmf) {
						char t[2];
						t[0] = fr2->subclass;
						t[1] = '\0';
						cw_log(LOG_DEBUG, "DTMF detected on %s: %s\n", chan->name,t);
						if (
						    ( noextneeded || cw_canmatch_extension(chan, chan->context, t, 1, chan->CALLERID_FIELD) )
							&& !longdtmf
						    ) {
						    // They entered a valid extension, or might be anyhow 
						    if (noextneeded) {
							cw_log(LOG_NOTICE, "DTMF received (not matching to exten)\n");
							res = 0;
						    } else {
							cw_log(LOG_NOTICE, "DTMF received (matching to exten)\n");
							res = fr2->subclass;
						    }
						    pbx_builtin_setvar_helper(chan, "DTMF_DETECTED", "1");
						    cw_fr_free(fr);
						    break;
						} else {
						    if (strcmp(t,"#") || !longdtmf) {
							strncat(dtmf_did,t,sizeof(dtmf_did)-strlen(dtmf_did)-1 );
						    } else {
							pbx_builtin_setvar_helper(chan, "DTMF_DID", dtmf_did);
							pbx_builtin_setvar_helper(chan, "DTMF_DETECTED", "1");
							if (!ignorejump && cw_canmatch_extension(chan, chan->context, dtmf_did, 1, chan->CALLERID_FIELD) ) {
							    strncpy(chan->exten, dtmf_did, sizeof(chan->exten)-1);
							    chan->priority = 0;
							}
							res=0;
							cw_fr_free(fr);
							break;
						    }
						    cw_log(LOG_DEBUG, "Valid extension requested and DTMF did not match [%s]\n",t);
						}
					    }
					} else if ((fr->frametype == CW_FRAME_VOICE) && (fr->subclass == CW_FORMAT_SLINEAR)) {
						int totalsilence;
						int ms;
						res = cw_dsp_silence(dsp, fr, &totalsilence);
						if (res && (totalsilence > sil)) {
							/* We've been quiet a little while */
							if (notsilent) {
								/* We had heard some talking */
								ms = cw_tvdiff_ms(cw_tvnow(), start);
								ms -= sil;
								if (ms < 0)
									ms = 0;
								if ((ms > min) && ((max < 0) || (ms < max))) {
									char ms_str[10];
									cw_log(LOG_DEBUG, "Found qualified token of %d ms\n", ms);

									/* Save detected talk time (in milliseconds) */ 
									sprintf(ms_str, "%d", ms );	
									pbx_builtin_setvar_helper(chan, "TALK_DETECTED", ms_str);
									
									cw_goto_if_exists(chan, chan->context, "talk", 1);
									res = 0;
									cw_fr_free(fr);
									break;
								} else
									cw_log(LOG_DEBUG, "Found unqualified token of %d ms\n", ms);
								notsilent = 0;
							}
						} else {
							if (!notsilent) {
								/* Heard some audio, mark the begining of the token */
								start = cw_tvnow();
								cw_log(LOG_DEBUG, "Start of voice token!\n");
								notsilent = 1;
							}
						}
						
					}
					cw_fr_free(fr);
				}
				cw_sched_runq(chan->sched);
			}
			cw_stopstream(chan);
		} else {
			cw_log(LOG_WARNING, "cw_streamfile failed on %s for %s\n", chan->name, argv[0]);
			res = 0;
		}
	}
	if (res > -1) {
		if (origrformat && cw_set_read_format(chan, origrformat)) {
			cw_log(LOG_WARNING, "Failed to restore read format for %s to %s\n", 
				chan->name, cw_getformatname(origrformat));
		}
	}
	if (dsp)
		cw_dsp_free(dsp);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(background_detect_app);
	return res;
}

int load_module(void)
{
	background_detect_app = cw_register_application(background_detect_name, background_detect_exec, background_detect_synopsis, background_detect_syntax, background_detect_descrip);
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


