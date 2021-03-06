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
 * \brief JPEG File format support.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
 
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/formats/format_jpeg.c $", "$Revision: 4723 $")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"
#include "callweaver/image.h"
#include "callweaver/lock.h"

static char *desc = "JPEG (Joint Picture Experts Group) Image Format";

static struct cw_frame *jpeg_read_image(int fd, int len)
{
    struct cw_frame fr;
    int res;
    char buf[65536];
    
    if (len > sizeof(buf)  ||  len < 0)
    {
        cw_log(LOG_WARNING, "JPEG image too large to read\n");
        return NULL;
    }
    if ((res = read(fd, buf, len)) < len)
    {
        cw_log(LOG_WARNING, "Only read %d of %d bytes: %s\n", res, len, strerror(errno));
    }
    cw_fr_init_ex(&fr, CW_FRAME_IMAGE, CW_FORMAT_JPEG, "JPEG Read");
    fr.data = buf;
    fr.datalen = len;
    return cw_frisolate(&fr);
}

static int jpeg_identify(int fd)
{
    char buf[10];
    int res;

    res = read(fd, buf, sizeof(buf));
    if (res < sizeof(buf))
        return 0;
    if (memcmp(buf + 6, "JFIF", 4))
        return 0;
    return 1;
}

static int jpeg_write_image(int fd, struct cw_frame *fr)
{
    int res = 0;

    if (fr->frametype != CW_FRAME_IMAGE)
    {
        cw_log(LOG_WARNING, "Not an image\n");
        return -1;
    }
    if (fr->subclass != CW_FORMAT_JPEG)
    {
        cw_log(LOG_WARNING, "Not a jpeg image\n");
        return -1;
    }
    if (fr->datalen)
    {
        res = write(fd, fr->data, fr->datalen);
        if (res != fr->datalen)
        {
            cw_log(LOG_WARNING, "Only wrote %d of %d bytes: %s\n", res, fr->datalen, strerror(errno));
            return -1;
        }
    }
    return res;
}

static struct cw_imager jpeg_format =
{
    "jpg",
    "JPEG (Joint Picture Experts Group)",
    "jpg|jpeg",
    CW_FORMAT_JPEG,
    jpeg_read_image,
    jpeg_identify,
    jpeg_write_image,
};

int load_module(void)
{
    return cw_image_register(&jpeg_format);
}

int unload_module(void)
{
    cw_image_unregister(&jpeg_format);
    return 0;
}    

int usecount(void)
{
    /* We never really have any users */
    return 0;
}

char *description(void)
{
    return desc;
}
