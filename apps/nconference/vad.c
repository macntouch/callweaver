/*
 * app_nconference
 *
 * NConference
 * A channel independent conference application for CallWeaver
 *
 * Copyright (C) 2002, 2003 Navynet SRL
 * http://www.navynet.it
 *
 * Massimo "CtRiX" Cetra - ctrix (at) navynet.it
 *
 * This program may be modified and distributed under the 
 * terms of the GNU Public License V2.
 *
 */

#ifdef HAVE_CONFIG_H 
#include "confdefs.h" 
#endif 

#include <stdio.h>
#include "common.h"
#include "vad.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/nconference/vad.c $", "$Revision: 4723 $");

#define THRESH	200

static int detect_silence(char *buf, int len, int threshold)
{
    int i, totover = 0;
    int16_t value=0;

    for (i=0; i< len; i++){
	value =  abs( ((int16_t *)buf)[i] );
	if ( value > threshold ) { 
	    totover++;
	}
    }
    //cw_log(LOG_WARNING,"THR: %d %d\n", (max-min), threshold );
    if( totover > len % 5)
	return 0;

    return 1;
}


/*
 *  Silence (Voice Activity Detection )
 * Take data (samples) and length as input and check if it's a silence packet
 * return 1 if silence, 0 if no silence, -1 if error
 */

int vad_is_talk(char *buf, int len, int *silence_nr, int silence_threshold)
{
    int retval;

    retval = detect_silence(buf, len, THRESH);

    if (retval == 1)
	(*silence_nr)++;
    else
	*silence_nr = 0;

    if (*silence_nr > silence_threshold)
	return 0; /* really silent */
    return 1;
}

