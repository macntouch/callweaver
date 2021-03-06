/*
 * Copyright (C) 2006 Voop as
 * Thorsten Lockert <tholo@voop.as>
 *
 * Ported to CallWeaver by Roy Sigurd Karlsbakk <roy@karlsbakk.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/res/snmp/agent.c $", "$Revision: 4723 $")

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/indications.h"
#include "callweaver/pbx.h"

/* Colission between Net-SNMP and CallWeaver */
#define unload_module cw_unload_module
#include "callweaver/module.h"
#undef unload_module

#include "agent.h"

/* Helper functions in Net-SNMP, header file not installed by default */
int header_generic(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **);
int header_simple_table(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **, int);
int register_sysORTable(oid *, size_t, const char *);
int unregister_sysORTable(oid *, size_t);

/* Not defined in header files */
extern char cw_config_CW_SOCKET[];

/* Forward declaration */
static void init_callweaver_mib(void);

/*
 * Anchor for all the CallWeaver MIB values
 */
static oid callweaver_oid[] = { 1, 3, 6, 1, 4, 1, 22736, 1 };

/*
 * MIB values -- these correspond to values in the CallWeaver MIB,
 * and MUST be kept in sync with the MIB for things to work as
 * expected.
 */
#define CWVERSION 1
#define CWVERSTRING 1
#define CWVERTAG 2

#define CWCONFIGURATION 2
#define CWCONFUPTIME 1
#define CWCONFRELOADTIME 2
#define CWCONFPID 3
#define CWCONFSOCKET 4

#define CWMODULES 3
#define CWMODCOUNT 1

#define CWINDICATIONS 4
#define CWINDCOUNT 1
#define CWINDCURRENT 2

#define CWINDTABLE 3
#define CWINDINDEX 1
#define CWINDCOUNTRY 2
#define CWINDALIAS 3
#define CWINDDESCRIPTION 4

#define CWCHANNELS 5
#define CWCHANCOUNT 1

#define CWCHANTABLE 2
#define CWCHANINDEX 1
#define CWCHANNAME 2
#define CWCHANLANGUAGE 3
#define CWCHANTYPE 4
#define CWCHANMUSICCLASS 5
#define CWCHANBRIDGE 6
#define CWCHANMASQ 7
#define CWCHANMASQR 8
#define CWCHANWHENHANGUP 9
#define CWCHANAPP 10
#define CWCHANDATA 11
#define CWCHANCONTEXT 12
#define CWCHANPROCCONTEXT 13
#define CWCHANPROCEXTEN 14
#define CWCHANPROCPRI 15
#define CWCHANEXTEN 16
#define CWCHANPRI 17
#define CWCHANACCOUNTCODE 18
#define CWCHANFORWARDTO 19
#define CWCHANUNIQUEID 20
#define CWCHANCALLGROUP 21
#define CWCHANPICKUPGROUP 22
#define CWCHANSTATE 23
#define CWCHANMUTED 24
#define CWCHANRINGS 25
#define CWCHANCIDDNID 26
#define CWCHANCIDNUM 27
#define CWCHANCIDNAME 28
#define CWCHANCIDANI 29
#define CWCHANCIDRDNIS 30
#define CWCHANCIDPRES 31
#define CWCHANCIDANI2 32
#define CWCHANCIDTON 33
#define CWCHANCIDTNS 34
#define CWCHANAMAFLAGS 35
#define CWCHANADSI 36
#define CWCHANTONEZONE 37
#define CWCHANHANGUPCAUSE 38
#define CWCHANVARIABLES 39
#define CWCHANFLAGS 40
#define CWCHANTRANSFERCAP 41

#define CWCHANTYPECOUNT 3

#define CWCHANTYPETABLE 4
#define CWCHANTYPEINDEX 1
#define CWCHANTYPENAME 2
#define CWCHANTYPEDESC 3
#define CWCHANTYPEDEVSTATE 4
#define CWCHANTYPEINDICATIONS 5
#define CWCHANTYPETRANSFER 6
#define CWCHANTYPECHANNELS 7

void *agent_thread(void *arg)
{
    cw_verbose(VERBOSE_PREFIX_2 "Starting %sAgent\n", res_snmp_agentx_subagent ? "Sub" : "");

    snmp_enable_stderrlog();

    if (res_snmp_agentx_subagent)
        netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID,
                               NETSNMP_DS_AGENT_ROLE,
                               1);

    init_agent("CallWeaver");

    init_callweaver_mib();

    init_snmp("CallWeaver");

    if (!res_snmp_agentx_subagent)
        init_master_agent();

    while (res_snmp_dont_stop)
        agent_check_and_process(1);

    snmp_shutdown("CallWeaver");

    cw_verbose(VERBOSE_PREFIX_2 "Terminating %sAgent\n",
                 res_snmp_agentx_subagent ? "Sub" : "");

    return NULL;
}

static u_char *
cw_var_channels(struct variable *vp, oid *name, size_t *length,
                  int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case CWCHANCOUNT:
        long_ret = cw_active_channels();
        return (u_char *)&long_ret;
    default:
        break;
    }
    return NULL;
}

static u_char *cw_var_channels_table(struct variable *vp, oid *name, size_t *length,
                                       int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    static u_char bits_ret[2];
    static char string_ret[256];
    struct cw_channel *chan, *bridge;
    struct timeval tval;
    u_char *ret;
    int i, bit;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, cw_active_channels()))
        return NULL;

    i = name[*length - 1] - 1;
    for (chan = cw_channel_walk_locked(NULL);
         chan  &&  i;
         chan = cw_channel_walk_locked(chan), i--)
    {
        cw_channel_unlock(chan);
    }
    if (chan == NULL)
        return NULL;
    *var_len = sizeof(long_ret);

    switch (vp->magic)
    {
    case CWCHANINDEX:
        long_ret = name[*length - 1];
        ret = (u_char *)&long_ret;
        break;
    case CWCHANNAME:
        if (!cw_strlen_zero(chan->name))
        {
            strncpy(string_ret, chan->name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case CWCHANLANGUAGE:
        if (!cw_strlen_zero(chan->language))
        {
            strncpy(string_ret, chan->language, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case CWCHANTYPE:
        strncpy(string_ret, chan->tech->type, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *)string_ret;
        break;
    case CWCHANMUSICCLASS:
        if (!cw_strlen_zero(chan->musicclass))
        {
            strncpy(string_ret, chan->musicclass, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case CWCHANBRIDGE:
        if ((bridge = cw_bridged_channel(chan)) != NULL)
        {
            strncpy(string_ret, bridge->name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case CWCHANMASQ:
        if (chan->masq  &&  !cw_strlen_zero(chan->masq->name))
        {
            strncpy(string_ret, chan->masq->name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case CWCHANMASQR:
        if (chan->masqr  &&  !cw_strlen_zero(chan->masqr->name))
        {
            strncpy(string_ret, chan->masqr->name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case CWCHANWHENHANGUP:
        if (chan->whentohangup)
        {
            gettimeofday(&tval, NULL);
            long_ret = difftime(chan->whentohangup, tval.tv_sec) * 100 - tval.tv_usec / 10000;
            ret = (u_char *) &long_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANAPP:
        if (chan->appl)
        {
            strncpy(string_ret, chan->appl, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *) string_ret;
        }
        else
            ret = NULL;
        break;
    case CWCHANDATA:
        cw_log(LOG_WARNING, "CWCHANDATA doesn't exist anymore\n");
        ret = NULL;
        break;
    case CWCHANCONTEXT:
        strncpy(string_ret, chan->context, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case CWCHANPROCCONTEXT:
        strncpy(string_ret, chan->proc_context, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case CWCHANPROCEXTEN:
        strncpy(string_ret, chan->proc_exten, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case CWCHANPROCPRI:
        long_ret = chan->proc_priority;
        ret = (u_char *) &long_ret;
        break;
    case CWCHANEXTEN:
        strncpy(string_ret, chan->exten, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case CWCHANPRI:
        long_ret = chan->priority;
        ret = (u_char *) &long_ret;
        break;
    case CWCHANACCOUNTCODE:
        if (!cw_strlen_zero(chan->accountcode))
        {
            strncpy(string_ret, chan->accountcode, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *) string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANFORWARDTO:
        if (!cw_strlen_zero(chan->call_forward))
        {
            strncpy(string_ret, chan->call_forward, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *) string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANUNIQUEID:
        strncpy(string_ret, chan->uniqueid, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case CWCHANCALLGROUP:
        long_ret = chan->callgroup;
        ret = (u_char *) &long_ret;
        break;
    case CWCHANPICKUPGROUP:
        long_ret = chan->pickupgroup;
        ret = (u_char *) &long_ret;
        break;
    case CWCHANSTATE:
        long_ret = chan->_state & 0xffff;
        ret = (u_char *) &long_ret;
        break;
    case CWCHANMUTED:
        long_ret = chan->_state & CW_STATE_MUTE  ?  1  :  2;
        ret = (u_char *) &long_ret;
        break;
    case CWCHANRINGS:
        long_ret = chan->rings;
        ret = (u_char *) &long_ret;
        break;
    case CWCHANCIDDNID:
        if (chan->cid.cid_dnid)
        {
            strncpy(string_ret, chan->cid.cid_dnid, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *) string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANCIDNUM:
        if (chan->cid.cid_num)
        {
            strncpy(string_ret, chan->cid.cid_num, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANCIDNAME:
        if (chan->cid.cid_name)
        {
            strncpy(string_ret, chan->cid.cid_name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANCIDANI:
        if (chan->cid.cid_ani)
        {
            strncpy(string_ret, chan->cid.cid_ani, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANCIDRDNIS:
        if (chan->cid.cid_rdnis)
        {
            strncpy(string_ret, chan->cid.cid_rdnis, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANCIDPRES:
        long_ret = chan->cid.cid_pres;
        ret = (u_char *)&long_ret;
        break;
    case CWCHANCIDANI2:
        long_ret = chan->cid.cid_ani2;
        ret = (u_char *)&long_ret;
        break;
    case CWCHANCIDTON:
        long_ret = chan->cid.cid_ton;
        ret = (u_char *)&long_ret;
        break;
    case CWCHANCIDTNS:
        long_ret = chan->cid.cid_tns;
        ret = (u_char *)&long_ret;
        break;
    case CWCHANAMAFLAGS:
        long_ret = chan->amaflags;
        ret = (u_char *)&long_ret;
        break;
    case CWCHANADSI:
        long_ret = chan->adsicpe;
        ret = (u_char *)&long_ret;
        break;
    case CWCHANTONEZONE:
        if (chan->zone)
        {
            strncpy(string_ret, chan->zone->country, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANHANGUPCAUSE:
        long_ret = chan->hangupcause;
        ret = (u_char *)&long_ret;
        break;
    case CWCHANVARIABLES:
        if (pbx_builtin_serialize_variables(chan, string_ret, sizeof(string_ret)))
        {
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case CWCHANFLAGS:
        bits_ret[0] = 0;
        for (bit = 0;  bit < 8;  bit++)
            bits_ret[0] |= ((chan->flags & (1 << bit)) >> bit) << (7 - bit);
        bits_ret[1] = 0;
        for (bit = 0;  bit < 8;  bit++)
            bits_ret[1] |= (((chan->flags >> 8) & (1 << bit)) >> bit) << (7 - bit);
        *var_len = 2;
        ret = bits_ret;
        break;
    case CWCHANTRANSFERCAP:
        long_ret = chan->transfercapability;
        ret = (u_char *)&long_ret;
        break;
    default:
        ret = NULL;
        break;
    }
    cw_channel_unlock(chan);
    return ret;
}

static u_char *cw_var_channel_types(struct variable *vp, oid *name, size_t *length,
                                      int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct cw_variable *channel_types, *next;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case CWCHANTYPECOUNT:
        long_ret = 0;
        for (channel_types = next = cw_channeltype_list();  next;  next = next->next)
            long_ret++;
        cw_variables_destroy(channel_types);
        return (u_char *)&long_ret;
    default:
        break;
    }
    return NULL;
}

static u_char *cw_var_channel_types_table(struct variable *vp, oid *name, size_t *length,
        int exact, size_t *var_len, WriteMethod **write_method)
{
    const struct cw_channel_tech *tech = NULL;
    struct cw_variable *channel_types, *next;
    static unsigned long long_ret;
    struct cw_channel *chan;
    u_long i;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
        return NULL;

    channel_types = cw_channeltype_list();
    for (i = 1, next = channel_types;  next  &&  i != name[*length - 1];  next = next->next, i++)
        ;
    if (next != NULL)
        tech = cw_get_channel_tech(next->name);
    cw_variables_destroy(channel_types);
    if (next == NULL  ||  tech == NULL)
        return NULL;

    switch (vp->magic)
    {
    case CWCHANTYPEINDEX:
        long_ret = name[*length - 1];
        return (u_char *) &long_ret;
    case CWCHANTYPENAME:
        *var_len = strlen(tech->type);
        return (u_char *) tech->type;
    case CWCHANTYPEDESC:
        *var_len = strlen(tech->description);
        return (u_char *) tech->description;
    case CWCHANTYPEDEVSTATE:
        long_ret = tech->devicestate  ?  1  :  2;
        return (u_char *) &long_ret;
    case CWCHANTYPEINDICATIONS:
        long_ret = tech->indicate  ?  1  :  2;
        return (u_char *) &long_ret;
    case CWCHANTYPETRANSFER:
        long_ret = tech->transfer  ?  1  :  2;
        return (u_char *) &long_ret;
    case CWCHANTYPECHANNELS:
        long_ret = 0;
        for (chan = cw_channel_walk_locked(NULL);
             chan;
             chan = cw_channel_walk_locked(chan))
        {
            cw_channel_unlock(chan);
            if (chan->tech == tech)
                long_ret++;
        }
        return (u_char *)&long_ret;
    default:
        break;
    }
    return NULL;
}

static u_char *cw_var_Config(struct variable *vp, oid *name, size_t *length,
                               int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct timeval tval;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case CWCONFUPTIME:
        gettimeofday(&tval, NULL);
        long_ret = difftime(tval.tv_sec, cw_startuptime) * 100 + tval.tv_usec / 10000;
        return (u_char *) &long_ret;
    case CWCONFRELOADTIME:
        gettimeofday(&tval, NULL);
        if (cw_lastreloadtime)
            long_ret = difftime(tval.tv_sec, cw_lastreloadtime) * 100 + tval.tv_usec / 10000;
        else
            long_ret = difftime(tval.tv_sec, cw_startuptime) * 100 + tval.tv_usec / 10000;
        return (u_char *) &long_ret;
    case CWCONFPID:
        long_ret = getpid();
        return (u_char *) &long_ret;
    case CWCONFSOCKET:
        *var_len = strlen(cw_config_CW_SOCKET);
        return (u_char *) cw_config_CW_SOCKET;
    default:
        break;
    }
    return NULL;
}

static u_char *cw_var_indications(struct variable *vp,
                                    oid *name,
                                    size_t *length,
                                    int exact,
                                    size_t *var_len,
                                    WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct tone_zone *tz = NULL;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case CWINDCOUNT:
        long_ret = 0;
        while ((tz = cw_walk_indications(tz)))
            long_ret++;
        return (u_char *) &long_ret;
    case CWINDCURRENT:
        tz = cw_get_indication_zone(NULL);
        if (tz)
        {
            *var_len = strlen(tz->country);
            return (u_char *) tz->country;
        }
        *var_len = 0;
        return NULL;
    default:
        break;
    }
    return NULL;
}

static u_char *cw_var_indications_table(struct variable *vp, oid *name, size_t *length,
        int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct tone_zone *tz = NULL;
    int i;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
        return NULL;

    i = name[*length - 1] - 1;
    while ((tz = cw_walk_indications(tz))  &&  i)
        i--;
    if (tz == NULL)
        return NULL;

    switch (vp->magic)
    {
    case CWINDINDEX:
        long_ret = name[*length - 1];
        return (u_char *)&long_ret;
    case CWINDCOUNTRY:
        *var_len = strlen(tz->country);
        return (u_char *)tz->country;
    case CWINDALIAS:
        if (tz->alias)
        {
            *var_len = strlen(tz->alias);
            return (u_char *)tz->alias;
        }
        return NULL;
    case CWINDDESCRIPTION:
        *var_len = strlen(tz->description);
        return (u_char *)tz->description;
    default:
        break;
    }
    return NULL;
}

static int countmodule(const char *mod, const char *desc, int use, const char *like)
{
    return 1;
}

static u_char *cw_var_Modules(struct variable *vp,
                                oid *name, size_t *length,
                                int exact,
                                size_t *var_len,
                                WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case CWMODCOUNT:
        long_ret = cw_update_module_list(countmodule, NULL);
        return (u_char *) &long_ret;
    default:
        break;
    }
    return NULL;
}

static u_char *cw_var_Version(struct variable *vp, oid *name, size_t *length,
                                int exact, size_t *var_len, WriteMethod **write_method)
{
    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

#if 0
    FIXME
    Someone please help me out here. I dunno where to find these

    switch (vp->magic)
    {
    case CWVERSTRING:
        *var_len = strlen(CW_VERSION);
        return (u_char *)CW_VERSION;
    case CWVERTAG:
        long_ret = CALLWEAVER_VERSION_NUM;
        return (u_char *)&long_ret;
    default:
        break;
    }
#endif
    return NULL;
}

static int term_callweaver_mib(int majorID, int minorID, void *serverarg, void *clientarg)
{
    unregister_sysORTable(callweaver_oid, OID_LENGTH(callweaver_oid));
    return 0;
}

static void init_callweaver_mib(void)
{
    static struct variable4 callweaver_vars[] =
    {
        {CWVERSTRING,          ASN_OCTET_STR, RONLY, cw_var_Version,             2, {CWVERSION, CWVERSTRING}},
        {CWVERTAG,             ASN_UNSIGNED,  RONLY, cw_var_Version,             2, {CWVERSION, CWVERTAG}},
        {CWCONFUPTIME,         ASN_TIMETICKS, RONLY, cw_var_Config,              2, {CWCONFIGURATION, CWCONFUPTIME}},
        {CWCONFRELOADTIME,     ASN_TIMETICKS, RONLY, cw_var_Config,              2, {CWCONFIGURATION, CWCONFRELOADTIME}},
        {CWCONFPID,            ASN_INTEGER,   RONLY, cw_var_Config,              2, {CWCONFIGURATION, CWCONFPID}},
        {CWCONFSOCKET,         ASN_OCTET_STR, RONLY, cw_var_Config,              2, {CWCONFIGURATION, CWCONFSOCKET}},
        {CWMODCOUNT,           ASN_INTEGER,   RONLY, cw_var_Modules ,            2, {CWMODULES, CWMODCOUNT}},
        {CWINDCOUNT,           ASN_INTEGER,   RONLY, cw_var_indications,         2, {CWINDICATIONS, CWINDCOUNT}},
        {CWINDCURRENT,         ASN_OCTET_STR, RONLY, cw_var_indications,         2, {CWINDICATIONS, CWINDCURRENT}},
        {CWINDINDEX,           ASN_INTEGER,   RONLY, cw_var_indications_table,   4, {CWINDICATIONS, CWINDTABLE, 1, CWINDINDEX}},
        {CWINDCOUNTRY,         ASN_OCTET_STR, RONLY, cw_var_indications_table,   4, {CWINDICATIONS, CWINDTABLE, 1, CWINDCOUNTRY}},
        {CWINDALIAS,           ASN_OCTET_STR, RONLY, cw_var_indications_table,   4, {CWINDICATIONS, CWINDTABLE, 1, CWINDALIAS}},
        {CWINDDESCRIPTION,     ASN_OCTET_STR, RONLY, cw_var_indications_table,   4, {CWINDICATIONS, CWINDTABLE, 1, CWINDDESCRIPTION}},
        {CWCHANCOUNT,          ASN_INTEGER,   RONLY, cw_var_channels,            2, {CWCHANNELS, CWCHANCOUNT}},
        {CWCHANINDEX,          ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANINDEX}},
        {CWCHANNAME,           ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANNAME}},
        {CWCHANLANGUAGE,       ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANLANGUAGE}},
        {CWCHANTYPE,           ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANTYPE}},
        {CWCHANMUSICCLASS,     ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANMUSICCLASS}},
        {CWCHANBRIDGE,         ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANBRIDGE}},
        {CWCHANMASQ,           ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANMASQ}},
        {CWCHANMASQR,          ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANMASQR}},
        {CWCHANWHENHANGUP,     ASN_TIMETICKS, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANWHENHANGUP}},
        {CWCHANAPP,            ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANAPP}},
        {CWCHANDATA,           ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANDATA}},
        {CWCHANCONTEXT,        ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCONTEXT}},
        {CWCHANPROCCONTEXT,    ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANPROCCONTEXT}},
        {CWCHANPROCEXTEN,      ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANPROCEXTEN}},
        {CWCHANPROCPRI,        ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANPROCPRI}},
        {CWCHANEXTEN,          ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANEXTEN}},
        {CWCHANPRI,            ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANPRI}},
        {CWCHANACCOUNTCODE,    ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANACCOUNTCODE}},
        {CWCHANFORWARDTO,      ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANFORWARDTO}},
        {CWCHANUNIQUEID,       ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANUNIQUEID}},
        {CWCHANCALLGROUP,      ASN_UNSIGNED,  RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCALLGROUP}},
        {CWCHANPICKUPGROUP,    ASN_UNSIGNED,  RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANPICKUPGROUP}},
        {CWCHANSTATE,          ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANSTATE}},
        {CWCHANMUTED,          ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANMUTED}},
        {CWCHANRINGS,          ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANRINGS}},
        {CWCHANCIDDNID,        ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDDNID}},
        {CWCHANCIDNUM,         ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDNUM}},
        {CWCHANCIDNAME,        ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDNAME}},
        {CWCHANCIDANI,         ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDANI}},
        {CWCHANCIDRDNIS,       ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDRDNIS}},
        {CWCHANCIDPRES,        ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDPRES}},
        {CWCHANCIDANI2,        ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDANI2}},
        {CWCHANCIDTON,         ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDTON}},
        {CWCHANCIDTNS,         ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANCIDTNS}},
        {CWCHANAMAFLAGS,       ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANAMAFLAGS}},
        {CWCHANADSI,           ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANADSI}},
        {CWCHANTONEZONE,       ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANTONEZONE}},
        {CWCHANHANGUPCAUSE,    ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANHANGUPCAUSE}},
        {CWCHANVARIABLES,      ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANVARIABLES}},
        {CWCHANFLAGS,          ASN_OCTET_STR, RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANFLAGS}},
        {CWCHANTRANSFERCAP,    ASN_INTEGER,   RONLY, cw_var_channels_table,      4, {CWCHANNELS, CWCHANTABLE, 1, CWCHANTRANSFERCAP}},
        {CWCHANTYPECOUNT,      ASN_INTEGER,   RONLY, cw_var_channel_types,       2, {CWCHANNELS, CWCHANTYPECOUNT}},
        {CWCHANTYPEINDEX,      ASN_INTEGER,   RONLY, cw_var_channel_types_table, 4, {CWCHANNELS, CWCHANTYPETABLE, 1, CWCHANTYPEINDEX}},
        {CWCHANTYPENAME,       ASN_OCTET_STR, RONLY, cw_var_channel_types_table, 4, {CWCHANNELS, CWCHANTYPETABLE, 1, CWCHANTYPENAME}},
        {CWCHANTYPEDESC,       ASN_OCTET_STR, RONLY, cw_var_channel_types_table, 4, {CWCHANNELS, CWCHANTYPETABLE, 1, CWCHANTYPEDESC}},
        {CWCHANTYPEDEVSTATE,   ASN_INTEGER,   RONLY, cw_var_channel_types_table, 4, {CWCHANNELS, CWCHANTYPETABLE, 1, CWCHANTYPEDEVSTATE}},
        {CWCHANTYPEINDICATIONS,ASN_INTEGER,   RONLY, cw_var_channel_types_table, 4, {CWCHANNELS, CWCHANTYPETABLE, 1, CWCHANTYPEINDICATIONS}},
        {CWCHANTYPETRANSFER,   ASN_INTEGER,   RONLY, cw_var_channel_types_table, 4, {CWCHANNELS, CWCHANTYPETABLE, 1, CWCHANTYPETRANSFER}},
        {CWCHANTYPECHANNELS,   ASN_GAUGE,     RONLY, cw_var_channel_types_table, 4, {CWCHANNELS, CWCHANTYPETABLE, 1, CWCHANTYPECHANNELS}},
    };

    register_sysORTable(callweaver_oid, OID_LENGTH(callweaver_oid),
                        "CALLWEAVER-MIB implementation for CallWeaver.");

    REGISTER_MIB("res_snmp", callweaver_vars, variable4, callweaver_oid);

    snmp_register_callback(SNMP_CALLBACK_LIBRARY,
                           SNMP_CALLBACK_SHUTDOWN,
                           term_callweaver_mib, NULL);
}
