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
 * \brief Save to raw, headerless h263 data.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/formats/format_h263.c $", "$Revision: 4723 $")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

/* Some Ideas for this code came from makeh263e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct cw_filestream
{
    void *reserved[CW_RESERVED_POINTERS];
    /* Believe it or not, we must decode/recode to account for the
       weird MS format */
    /* This is what a filestream means to us */
    FILE *f; /* Descriptor */
    unsigned int lastts;
    struct cw_frame fr;                /* Frame information */
    char waste[CW_FRIENDLY_OFFSET];    /* Buffer for sending frames, etc */
    char empty;                            /* Empty character */
    unsigned char h263[16384];                /* Four Real h263 Frames */
};


CW_MUTEX_DEFINE_STATIC(h263_lock);
static int glistcnt = 0;

static char *name = "h263";
static char *desc = "Raw h263 data";
static char *exts = "h263";

static struct cw_filestream *h263_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct cw_filestream *tmp;
    unsigned int ts;
    int res;

    if ((res = fread(&ts, 1, sizeof(ts), f)) < sizeof(ts))
    {
        cw_log(LOG_WARNING, "Empty file!\n");
        return NULL;
    }
        
    if ((tmp = malloc(sizeof(struct cw_filestream))))
    {
        memset(tmp, 0, sizeof(struct cw_filestream));
        if (cw_mutex_lock(&h263_lock))
        {
            cw_log(LOG_WARNING, "Unable to lock h263 list\n");
            free(tmp);
            return NULL;
        }
        tmp->f = f;
        cw_fr_init_ex(&tmp->fr, CW_FRAME_VIDEO, CW_FORMAT_H263, name);
        tmp->fr.data = tmp->h263;
        /* datalen will vary for each frame */
        glistcnt++;
        cw_mutex_unlock(&h263_lock);
        cw_update_use_count();
    }
    return tmp;
}

static struct cw_filestream *h263_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct cw_filestream *tmp;
    
    if ((tmp = malloc(sizeof(struct cw_filestream))))
    {
        memset(tmp, 0, sizeof(struct cw_filestream));
        if (cw_mutex_lock(&h263_lock))
        {
            cw_log(LOG_WARNING, "Unable to lock h263 list\n");
            free(tmp);
            return NULL;
        }
        tmp->f = f;
        glistcnt++;
        cw_mutex_unlock(&h263_lock);
        cw_update_use_count();
    }
    else
    {
        cw_log(LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static void h263_close(struct cw_filestream *s)
{
    if (cw_mutex_lock(&h263_lock))
    {
        cw_log(LOG_WARNING, "Unable to lock h263 list\n");
        return;
    }
    glistcnt--;
    cw_mutex_unlock(&h263_lock);
    cw_update_use_count();
    fclose(s->f);
    free(s);
    s = NULL;
}

static struct cw_frame *h263_read(struct cw_filestream *s, int *whennext)
{
    int res;
    int mark=0;
    unsigned short len;
    unsigned int ts;

    /* Send a frame from the file to the appropriate channel */
    cw_fr_init_ex(&s->fr, CW_FRAME_VIDEO, CW_FORMAT_H263, NULL);
    s->fr.offset = CW_FRIENDLY_OFFSET;
    s->fr.data = s->h263;
    if ((res = fread(&len, 1, sizeof(len), s->f)) < 1)
        return NULL;
    len = ntohs(len);
    if (len & 0x8000)
    {
        mark = 1;
    }
    len &= 0x7fff;
    if (len > sizeof(s->h263))
    {
        cw_log(LOG_WARNING, "Length %d is too long\n", len);
        return NULL;
    }
    if ((res = fread(s->h263, 1, len, s->f)) != len)
    {
        if (res)
            cw_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    s->fr.samples = s->lastts;
    s->fr.datalen = len;
    s->fr.subclass |= mark;
    s->fr.delivery.tv_sec = 0;
    s->fr.delivery.tv_usec = 0;
    if ((res = fread(&ts, 1, sizeof(ts), s->f)) == sizeof(ts))
    {
        s->lastts = ntohl(ts);
        *whennext = s->lastts * 4/45;
    }
    else
    {
        *whennext = 0;
    }
    return &s->fr;
}

static int h263_write(struct cw_filestream *fs, struct cw_frame *f)
{
    int res;
    unsigned int ts;
    unsigned short len;
    int subclass;
    int mark = 0;
    
    if (f->frametype != CW_FRAME_VIDEO)
    {
        cw_log(LOG_WARNING, "Asked to write non-video frame!\n");
        return -1;
    }
    subclass = f->subclass;
    if (subclass & 0x1)
        mark=0x8000;
    subclass &= ~0x1;
    if (subclass != CW_FORMAT_H263)
    {
        cw_log(LOG_WARNING, "Asked to write non-h263 frame (%d)!\n", f->subclass);
        return -1;
    }
    ts = htonl(f->samples);
    if ((res = fwrite(&ts, 1, sizeof(ts), fs->f)) != sizeof(ts))
    {
        cw_log(LOG_WARNING, "Bad write (%d/4): %s\n", res, strerror(errno));
        return -1;
    }
    len = htons(f->datalen | mark);
    if ((res = fwrite(&len, 1, sizeof(len), fs->f)) != sizeof(len))
    {
        cw_log(LOG_WARNING, "Bad write (%d/2): %s\n", res, strerror(errno));
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen)
    {
        cw_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
        return -1;
    }
    return 0;
}

static char *h263_getcomment(struct cw_filestream *s)
{
    return NULL;
}

static int h263_seek(struct cw_filestream *fs, long sample_offset, int whence)
{
    /* No way Jose */
    return -1;
}

static int h263_trunc(struct cw_filestream *fs)
{
    /* Truncate file to current length */
    if (ftruncate(fileno(fs->f), ftell(fs->f)) < 0)
        return -1;
    return 0;
}

static long h263_tell(struct cw_filestream *fs)
{
    /* XXX This is totally bogus XXX */
    off_t offset;
    offset = ftell(fs->f);
    return (offset/20)*160;
}

int load_module(void)
{
    return cw_format_register(name,
                                exts,
                                CW_FORMAT_H263,
                                h263_open,
                                h263_rewrite,
                                h263_write,
                                h263_seek,
                                h263_trunc,
                                h263_tell,
                                h263_read,
                                h263_close,
                                h263_getcomment);
                                
                                
}

int unload_module()
{
    return cw_format_unregister(name);
}    

int usecount()
{
    return glistcnt;
}

char *description()
{
    return desc;
}
