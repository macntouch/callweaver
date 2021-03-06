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
 * \brief Old-style G.723 frame/timestamp format.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/formats/format_g723_1.c $", "$Revision: 4723 $")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

#define G723_MAX_SIZE 1024

struct cw_filestream
{
    /* First entry MUST be reserved for the channel type */
    void *reserved[CW_RESERVED_POINTERS];
    /* This is what a filestream means to us */
    FILE *f; /* Descriptor */
    struct cw_filestream *next;
    struct cw_frame *fr;    /* Frame representation of buf */
    struct timeval orig;    /* Original frame time */
    char buf[G723_MAX_SIZE + CW_FRIENDLY_OFFSET];    /* Buffer for sending frames, etc */
};


CW_MUTEX_DEFINE_STATIC(g723_lock);
static int glistcnt = 0;

static char *name = "g723.1";
static char *desc = "G.723.1 Simple Timestamp File Format";
static char *exts = "g723.1|g723";

static struct cw_filestream *g723_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct cw_filestream *tmp;

    if ((tmp = malloc(sizeof(struct cw_filestream))))
    {
        memset(tmp, 0, sizeof(struct cw_filestream));
        if (cw_mutex_lock(&g723_lock))
        {
            cw_log(LOG_WARNING, "Unable to lock g723 list\n");
            free(tmp);
            return NULL;
        }
        tmp->f = f;
        tmp->fr = (struct cw_frame *) tmp->buf;
        cw_fr_init_ex(tmp->fr, CW_FRAME_VOICE, CW_FORMAT_G723_1, name);
        tmp->fr->data = tmp->buf + sizeof(struct cw_frame);
        /* datalen will vary for each frame */
        glistcnt++;
        cw_mutex_unlock(&g723_lock);
        cw_update_use_count();
    }
    return tmp;
}

static struct cw_filestream *g723_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct cw_filestream *tmp;

    if ((tmp = malloc(sizeof(struct cw_filestream))))
    {
        memset(tmp, 0, sizeof(struct cw_filestream));
        if (cw_mutex_lock(&g723_lock))
        {
            cw_log(LOG_WARNING, "Unable to lock g723 list\n");
            free(tmp);
            return NULL;
        }
        tmp->f = f;
        glistcnt++;
        cw_mutex_unlock(&g723_lock);
        cw_update_use_count();
    }
    else
        cw_log(LOG_WARNING, "Out of memory\n");
    return tmp;
}

static struct cw_frame *g723_read(struct cw_filestream *s, int *whennext)
{
    unsigned short size;
    int res;
    int delay;

    /* Read the delay for the next packet, and schedule again if necessary */
    if (fread(&delay, 1, 4, s->f) == 4) 
        delay = ntohl(delay);
    else
        delay = -1;
    if (fread(&size, 1, 2, s->f) != 2)
    {
        /* Out of data, or the file is no longer valid.  In any case
           go ahead and stop the stream */
        return NULL;
    }
    /* Looks like we have a frame to read from here */
    size = ntohs(size);
    if (size > G723_MAX_SIZE - sizeof(struct cw_frame))
    {
        cw_log(LOG_WARNING, "Size %d is invalid\n", size);
        /* The file is apparently no longer any good, as we
           shouldn't ever get frames even close to this 
           size.  */
        return NULL;
    }
    /* Read the data into the buffer */
    s->fr->offset = CW_FRIENDLY_OFFSET;
    s->fr->datalen = size;
    s->fr->data = s->buf + sizeof(struct cw_frame) + CW_FRIENDLY_OFFSET;
    if ((res = fread(s->fr->data, 1, size, s->f)) != size)
    {
        cw_log(LOG_WARNING, "Short read (%d of %d bytes) (%s)!\n", res, size, strerror(errno));
        return NULL;
    }
#if 0
        /* Average out frames <= 50 ms */
        if (delay < 50)
            s->fr->timelen = 30;
        else
            s->fr->timelen = delay;
#else
        s->fr->samples = 240;
#endif
    *whennext = s->fr->samples;
    return s->fr;
}

static void g723_close(struct cw_filestream *s)
{
    if (cw_mutex_lock(&g723_lock))
    {
        cw_log(LOG_WARNING, "Unable to lock g723 list\n");
        return;
    }
    glistcnt--;
    cw_mutex_unlock(&g723_lock);
    cw_update_use_count();
    fclose(s->f);
    free(s);
    s = NULL;
}


static int g723_write(struct cw_filestream *fs, struct cw_frame *f)
{
    u_int32_t delay;
    u_int16_t size;
    int res;

    if (fs->fr) {
        cw_log(LOG_WARNING, "Asked to write on a read stream??\n");
        return -1;
    }
    if (f->frametype != CW_FRAME_VOICE) {
        cw_log(LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != CW_FORMAT_G723_1) {
        cw_log(LOG_WARNING, "Asked to write non-g723 frame!\n");
        return -1;
    }
    delay = 0;
    if (f->datalen <= 0) {
        cw_log(LOG_WARNING, "Short frame ignored (%d bytes long?)\n", f->datalen);
        return 0;
    }
    if ((res = fwrite(&delay, 1, 4, fs->f)) != 4) {
        cw_log(LOG_WARNING, "Unable to write delay: res=%d (%s)\n", res, strerror(errno));
        return -1;
    }
    size = htons(f->datalen);
    if ((res = fwrite(&size, 1, 2, fs->f)) != 2) {
        cw_log(LOG_WARNING, "Unable to write size: res=%d (%s)\n", res, strerror(errno));
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen)
    {
        cw_log(LOG_WARNING, "Unable to write frame: res=%d (%s)\n", res, strerror(errno));
        return -1;
    }    
    return 0;
}

static int g723_seek(struct cw_filestream *fs, long sample_offset, int whence)
{
    return -1;
}

static int g723_trunc(struct cw_filestream *fs)
{
    /* Truncate file to current length */
    if (ftruncate(fileno(fs->f), ftell(fs->f)) < 0)
        return -1;
    return 0;
}

static long g723_tell(struct cw_filestream *fs)
{
    return -1;
}

static char *g723_getcomment(struct cw_filestream *s)
{
    return NULL;
}

int load_module(void)
{
    return cw_format_register(name,
                                exts,
                                CW_FORMAT_G723_1,
                                g723_open,
                                g723_rewrite,
                                g723_write,
                                g723_seek,
                                g723_trunc,
                                g723_tell,
                                g723_read,
                                g723_close,
                                g723_getcomment);
}

int unload_module(void)
{
    return cw_format_unregister(name);
}    

int usecount(void)
{
    return glistcnt;
}

char *description(void)
{
    return desc;
}
