/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2002, Christos Ricudis
 *
 * Christos Ricudis <ricudis@itc.auth.gr>
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
 * \brief PostgreSQL application
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://svn.callweaver.org/callweaver/branches/rel/1.2/apps/app_sql_postgres.c $", "$Revision: 4723 $")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/linkedlists.h"
#include "callweaver/chanvars.h"
//#include "callweaver/lock.h"

#include "libpq-fe.h"

#define EXTRA_LOG 0


static char *tdesc = "Simple PostgreSQL Interface";

static void *app;
static char *name = "PGSQL";
static char *synopsis = "Do several SQLy things";
static char *syntax = "PGSQL()";
static char *descrip = 
"Do several SQLy things\n"
"Syntax:\n"
"  PGSQL(Connect var option-string)\n"
"    Connects to a database.  Option string contains standard PostgreSQL\n"
"    parameters like host=, dbname=, user=.  Connection identifer returned\n"
"    in ${var}\n"
"  PGSQL(Query var ${connection_identifier} query-string)\n"
"    Executes standard SQL query contained in query-string using established\n"
"    connection identified by ${connection_identifier}. Reseult of query is\n"
"    is stored in ${var}.\n"
"  PGSQL(Fetch statusvar ${result_identifier} var1 var2 ... varn)\n"
"    Fetches a single row from a result set contained in ${result_identifier}.\n"
"    Assigns returned fields to ${var1} ... ${varn}.  ${statusvar} is set TRUE\n"
"    if additional rows exist in reseult set.\n"
"  PGSQL(Clear ${result_identifier})\n"
"    Frees memory and datastructures associated with result set.\n" 
"  PGSQL(Disconnect ${connection_identifier})\n"
"    Disconnects from named connection to PostgreSQL.\n" ;

/*

Syntax of SQL commands : 

	Connect var option-string
	
	Connects to a database using the option-string and stores the 
	connection identifier in ${var}
	
	
	Query var ${connection_identifier} query-string
	
	Submits query-string to database backend and stores the result
	identifier in ${var}
	
	
	Fetch statusvar ${result_identifier} var1 var2 var3 ... varn
	
	Fetches a row from the query and stores end-of-table status in 
	${statusvar} and columns in ${var1}..${varn}
	
	
	Clear ${result_identifier}

	Clears data structures associated with ${result_identifier}
	
	
	Disconnect ${connection_identifier}
	
	Disconnects from named connection
	
	
EXAMPLES OF USE : 

exten => s,2,PGSQL(Connect connid host=localhost user=callweaver dbname=credit)
exten => s,3,PGSQL(Query resultid ${connid} SELECT username,credit FROM credit WHERE callerid=${CALLERIDNUM})
exten => s,4,PGSQL(Fetch fetchid ${resultid} datavar1 datavar2)
exten => s,5,GotoIf(${fetchid}?6:8)
exten => s,6,Festival("User ${datavar1} currently has credit balance of ${datavar2} dollars.")	
exten => s,7,Goto(s,4)
exten => s,8,PGSQL(Clear ${resultid})
exten => s,9,PGSQL(Disconnect ${connid})

*/

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define CW_PGSQL_ID_DUMMY 0
#define CW_PGSQL_ID_CONNID 1
#define CW_PGSQL_ID_RESID 2
#define CW_PGSQL_ID_FETCHID 3

struct cw_PGSQL_id {
	int identifier_type; /* 0=dummy, 1=connid, 2=resultid */
	int identifier;
	void *data;
	CW_LIST_ENTRY(cw_PGSQL_id) entries;
} *cw_PGSQL_id;

CW_LIST_HEAD(PGSQLidshead,cw_PGSQL_id) PGSQLidshead;

static void *find_identifier(int identifier,int identifier_type) {
	struct PGSQLidshead *headp;
	struct cw_PGSQL_id *i;
	void *res=NULL;
	int found=0;
	
	headp=&PGSQLidshead;
	
	if (CW_LIST_LOCK(headp)) {
		cw_log(LOG_WARNING,"Unable to lock identifiers list\n");
	} else {
		CW_LIST_TRAVERSE(headp,i,entries) {
			if ((i->identifier==identifier) && (i->identifier_type==identifier_type)) {
				found=1;
				res=i->data;
				break;
			}
		}
		if (!found) {
			cw_log(LOG_WARNING,"Identifier %d, identifier_type %d not found in identifier list\n",identifier,identifier_type);
		}
		CW_LIST_UNLOCK(headp);
	}
	
	return(res);
}

static int add_identifier(int identifier_type,void *data) {
	struct cw_PGSQL_id *i,*j;
	struct PGSQLidshead *headp;
	int maxidentifier=0;
	
	headp=&PGSQLidshead;
	i=NULL;
	j=NULL;
	
	if (CW_LIST_LOCK(headp)) {
		cw_log(LOG_WARNING,"Unable to lock identifiers list\n");
		return(-1);
	} else {
 		i=malloc(sizeof(struct cw_PGSQL_id));
		CW_LIST_TRAVERSE(headp,j,entries) {
			if (j->identifier>maxidentifier) {
				maxidentifier=j->identifier;
			}
		}
		
		i->identifier=maxidentifier+1;
		i->identifier_type=identifier_type;
		i->data=data;
		CW_LIST_INSERT_HEAD(headp,i,entries);
		CW_LIST_UNLOCK(headp);
	}
	return(i->identifier);
}

static int del_identifier(int identifier,int identifier_type) {
	struct cw_PGSQL_id *i;
	struct PGSQLidshead *headp;
	int found=0;
	
        headp=&PGSQLidshead;
        
        if (CW_LIST_LOCK(headp)) {
		cw_log(LOG_WARNING,"Unable to lock identifiers list\n");
	} else {
		CW_LIST_TRAVERSE(headp,i,entries) {
			if ((i->identifier==identifier) && 
			    (i->identifier_type==identifier_type)) {
				CW_LIST_REMOVE(headp,i,entries);
				free(i);
				found=1;
				break;
			}
		}
		CW_LIST_UNLOCK(headp);
	}
	                
	if (found==0) {
		cw_log(LOG_WARNING,"Could not find identifier %d, identifier_type %d in list to delete\n",identifier,identifier_type);
		return(-1);
	} else {
		return(0);
	}
}

static int aPGSQL_connect(struct cw_channel *chan, void *data) {
	
	char *s1;
	char s[100] = "";
	char *optionstring;
	char *var;
	int l;
	int res;
	PGconn *karoto;
	int id;
	char *stringp=NULL;
	 
	
	res=0;
	l=strlen(data)+2;
	s1=malloc(l);
	strncpy(s1, data, l -1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	var=strsep(&stringp," ");
	optionstring=strsep(&stringp,"\n");
		
      	karoto = PQconnectdb(optionstring);
        if (PQstatus(karoto) == CONNECTION_BAD) {
        	cw_log(LOG_WARNING,"Connection to database using '%s' failed. postgress reports : %s\n", optionstring,
                                                 PQerrorMessage(karoto));
        	res=-1;
        } else {
        	cw_log(LOG_WARNING,"adding identifier\n");
		id=add_identifier(CW_PGSQL_ID_CONNID,karoto);
		snprintf(s, sizeof(s), "%d", id);
		pbx_builtin_setvar_helper(chan,var,s);
	}
 	
	free(s1);
	return res;
}

static int aPGSQL_query(struct cw_channel *chan, void *data) {
	

	char *s1,*s2,*s3,*s4;
	char s[100] = "";
	char *querystring;
	char *var;
	int l;
	int res,nres;
	PGconn *karoto;
	PGresult *PGSQLres;
	int id,id1;
	char *stringp=NULL;
	 
	
	res=0;
	l=strlen(data)+2;
	s1=malloc(l);
	s2=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	s3=strsep(&stringp," ");
	while (1) {	/* ugly trick to make branches with break; */
		var=s3;
		s4=strsep(&stringp," ");
		id=atoi(s4);
		querystring=strsep(&stringp,"\n");
		if ((karoto=find_identifier(id,CW_PGSQL_ID_CONNID))==NULL) {
			cw_log(LOG_WARNING,"Invalid connection identifier %d passed in aPGSQL_query\n",id);
			res=-1;
			break;
		}
		PGSQLres=PQexec(karoto,querystring);
		if (PGSQLres==NULL) {
			cw_log(LOG_WARNING,"aPGSQL_query: Connection Error (connection identifier = %d, error message : %s)\n",id,PQerrorMessage(karoto));
			res=-1;
			break;
		}
		if (PQresultStatus(PGSQLres) == PGRES_BAD_RESPONSE ||
		    PQresultStatus(PGSQLres) == PGRES_NONFATAL_ERROR ||
		    PQresultStatus(PGSQLres) == PGRES_FATAL_ERROR) {
		    	cw_log(LOG_WARNING,"aPGSQL_query: Query Error (connection identifier : %d, error message : %s)\n",id,PQcmdStatus(PGSQLres));
		    	res=-1;
		    	break;
		}
		nres=PQnfields(PGSQLres); 
		id1=add_identifier(CW_PGSQL_ID_RESID,PGSQLres);
		snprintf(s, sizeof(s), "%d", id1);
		pbx_builtin_setvar_helper(chan,var,s);
	 	break;
	}
	
	free(s1);
	free(s2);

	return(res);
}


static int aPGSQL_fetch(struct cw_channel *chan, void *data) {
	
	char *s1,*s2,*fetchid_var,*s4,*s5,*s6,*s7;
	char s[100];
	char *var;
	int l;
	int res;
	PGresult *PGSQLres;
	int id,id1,i,j,fnd;
	int *lalares=NULL;
	int nres;
        struct cw_var_t *variables;
        struct varshead *headp;
	char *stringp=NULL;
        
        headp=&chan->varshead;
	
	res=0;
	l=strlen(data)+2;
	s7=NULL;
	s1=malloc(l);
	s2=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	fetchid_var=strsep(&stringp," ");
	while (1) {	/* ugly trick to make branches with break; */
	  var=fetchid_var; /* fetchid */
		fnd=0;
		
		CW_LIST_TRAVERSE(headp,variables,entries) {
	    if (strncasecmp(cw_var_name(variables),fetchid_var,strlen(fetchid_var))==0) {
	                        s7=cw_var_value(variables);
	                        fnd=1;
                                break;
			}
		}
		
		if (fnd==0) { 
			s7="0";
	    pbx_builtin_setvar_helper(chan,fetchid_var,s7);
		}

		s4=strsep(&stringp," ");
		id=atoi(s4); /* resultid */
		if ((PGSQLres=find_identifier(id,CW_PGSQL_ID_RESID))==NULL) {
			cw_log(LOG_WARNING,"Invalid result identifier %d passed in aPGSQL_fetch\n",id);
			res=-1;
			break;
		}
		id=atoi(s7); /*fetchid */
		if ((lalares=find_identifier(id,CW_PGSQL_ID_FETCHID))==NULL) {
	    i=0;       /* fetching the very first row */
		} else {
			i=*lalares;
			free(lalares);
	    del_identifier(id,CW_PGSQL_ID_FETCHID); /* will re-add it a bit later */
		}

	  if (i<PQntuples(PGSQLres)) {
		nres=PQnfields(PGSQLres); 
		cw_log(LOG_WARNING,"cw_PGSQL_fetch : nres = %d i = %d ;\n",nres,i);
		for (j=0;j<nres;j++) {
			s5=strsep(&stringp," ");
			if (s5==NULL) {
				cw_log(LOG_WARNING,"cw_PGSQL_fetch : More tuples (%d) than variables (%d)\n",nres,j);
				break;
			}
			s6=PQgetvalue(PGSQLres,i,j);
			if (s6==NULL) { 
				cw_log(LOG_WARNING,"PWgetvalue(res,%d,%d) returned NULL in cw_PGSQL_fetch\n",i,j);
				break;
			}
			cw_log(LOG_WARNING,"===setting variable '%s' to '%s'\n",s5,s6);
			pbx_builtin_setvar_helper(chan,s5,s6);
		}
			lalares=malloc(sizeof(int));
	    *lalares = ++i; /* advance to the next row */
	    id1 = add_identifier(CW_PGSQL_ID_FETCHID,lalares);
		} else {
	    cw_log(LOG_WARNING,"cw_PGSQL_fetch : EOF\n");
	    id1 = 0; /* no more rows */
		}
		snprintf(s, sizeof(s), "%d", id1);
	  cw_log(LOG_WARNING,"Setting var '%s' to value '%s'\n",fetchid_var,s);
	  pbx_builtin_setvar_helper(chan,fetchid_var,s);
	 	break;
	}
	
	free(s1);
	free(s2);
	return(res);
}

static int aPGSQL_reset(struct cw_channel *chan, void *data) {
	
	char *s1,*s3;
	int l;
	PGconn *karoto;
	int id;
	char *stringp=NULL;
	 
	
	l=strlen(data)+2;
	s1=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	s3=strsep(&stringp," ");
	id=atoi(s3);
	if ((karoto=find_identifier(id,CW_PGSQL_ID_CONNID))==NULL) {
		cw_log(LOG_WARNING,"Invalid connection identifier %d passed in aPGSQL_reset\n",id);
	} else {
		PQreset(karoto);
	} 
	free(s1);
	return(0);
	
}

static int aPGSQL_clear(struct cw_channel *chan, void *data) {
	
	char *s1,*s3;
	int l;
	PGresult *karoto;
	int id;
	char *stringp=NULL;
	 
	
	l=strlen(data)+2;
	s1=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	s3=strsep(&stringp," ");
	id=atoi(s3);
	if ((karoto=find_identifier(id,CW_PGSQL_ID_RESID))==NULL) {
		cw_log(LOG_WARNING,"Invalid result identifier %d passed in aPGSQL_clear\n",id);
	} else {
		PQclear(karoto);
		del_identifier(id,CW_PGSQL_ID_RESID);
	}
	free(s1);
	return(0);
	
}

	   
	   
	
static int aPGSQL_disconnect(struct cw_channel *chan, void *data) {
	
	char *s1,*s3;
	int l;
	PGconn *karoto;
	int id;
	char *stringp=NULL;
	 
	
	l=strlen(data)+2;
	s1=malloc(l);
	strncpy(s1, data, l - 1);
	stringp=s1;
	strsep(&stringp," "); /* eat the first token, we already know it :P  */
	s3=strsep(&stringp," ");
	id=atoi(s3);
	if ((karoto=find_identifier(id,CW_PGSQL_ID_CONNID))==NULL) {
		cw_log(LOG_WARNING,"Invalid connection identifier %d passed in aPGSQL_disconnect\n",id);
	} else {
		PQfinish(karoto);
		del_identifier(id,CW_PGSQL_ID_CONNID);
	} 
	free(s1);
	return(0);
	
}

static int aPGSQL_debug(struct cw_channel *chan, void *data) {
	cw_log(LOG_WARNING,"Debug : %s\n",(char *)data);
	return(0);
}
		
	

static int PGSQL_exec(struct cw_channel *chan, int argc, char **argv)
{
	struct localuser *u;
	int result;

	if (argc == 0 || !argv[0][0]) {
		cw_log(LOG_WARNING, "APP_PGSQL requires an argument (see manual)\n");
		return -1;
	}

#if EXTRA_LOG
	printf("PRSQL_exec: arg=%s\n", argv[0]);
#endif
	LOCAL_USER_ADD(u);
	result=0;
	
	if (strncasecmp("connect",argv[0],strlen("connect"))==0) {
		result=(aPGSQL_connect(chan,argv[0]));
	} else 	if (strncasecmp("query",argv[0],strlen("query"))==0) {
		result=(aPGSQL_query(chan,argv[0]));
	} else 	if (strncasecmp("fetch",argv[0],strlen("fetch"))==0) {
		result=(aPGSQL_fetch(chan,argv[0]));
	} else 	if (strncasecmp("reset",argv[0],strlen("reset"))==0) {
		result=(aPGSQL_reset(chan,argv[0]));
	} else 	if (strncasecmp("clear",argv[0],strlen("clear"))==0) {
		result=(aPGSQL_clear(chan,argv[0]));
	} else  if (strncasecmp("debug",argv[0],strlen("debug"))==0) {
		result=(aPGSQL_debug(chan,argv[0]));
	} else 	if (strncasecmp("disconnect",argv[0],strlen("disconnect"))==0) {
		result=(aPGSQL_disconnect(chan,argv[0]));
	} else {
		cw_log(LOG_WARNING, "Unknown APP_PGSQL argument : %s\n", argv[0]);
		result=-1;	
	}
		
	LOCAL_USER_REMOVE(u);                                                                                
	return result;

}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= cw_unregister_application(app);
	return res;
}

int load_module(void)
{
	struct PGSQLidshead *headp;
	
        headp=&PGSQLidshead;
        
	CW_LIST_HEAD_INIT(headp);
	app = cw_register_application(name, PGSQL_exec, synopsis, syntax, descrip);
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
