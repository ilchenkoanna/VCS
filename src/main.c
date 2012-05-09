/*
** This module codes the main() procedure that runs first when the
** program is invoked.
*/
#include "config.h"
#include "main.h"
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h> /* atexit() */

#if INTERFACE
#ifdef vcs_ENABLE_JSON
#  include "cson_amalgamation.h" /* JSON API. Needed inside the INTERFACE block! */
#  include "json_detail.h"
#endif
#ifdef vcs_ENABLE_TCL
#include "tcl.h"
#endif

/*
** Number of elements in an array
*/
#define count(X)  (sizeof(X)/sizeof(X[0]))

/*
** Size of a UUID in characters
*/
#define UUID_SIZE 40

/*
** Maximum number of auxiliary parameters on reports
*/
#define MX_AUX  5

/*
** Holds flags for vcs user permissions.
*/
#ifdef vcs_ENABLE_TCL
/*
** All Tcl related context information is in this structure.  This structure
** definition has been copied from and should be kept in sync with the one in
** "th_tcl.c".
*/
struct TclContext {
  int argc;
  char **argv;
  Tcl_Interp *interp;
};
#endif

/*
** All global variables are in this structure.
*/
struct Global {
  int argc; char **argv;  /* Command-line arguments to the program */
  int isConst;            /* True if the output is unchanging */
  sqlite3 *db;            /* The connection to the databases */
  sqlite3 *dbConfig;      /* Separate connection for global_config table */
  int useAttach;          /* True if global_config is attached to repository */
  int configOpen;         /* True if the config database is open */
  sqlite3_int64 now;      /* Seconds since 1970 */
  int repositoryOpen;     /* True if the main repository database is open */
  char *zRepositoryName;  /* Name of the repository database */
  const char *zMainDbType;/* "configdb", "localdb", or "repository" */
  const char *zHome;      /* Name of user home directory */
  int localOpen;          /* True if the local database is open */
  char *zLocalRoot;       /* The directory holding the  local database */
  int minPrefix;          /* Number of digits needed for a distinct UUID */
  int fSqlTrace;          /* True if --sqltrace flag is present */
  int fSqlStats;          /* True if --sqltrace or --sqlstats are present */
  int fSqlPrint;          /* True if -sqlprint flag is present */
  int fQuiet;             /* True if -quiet flag is present */
  int fHttpTrace;         /* Trace outbound HTTP requests */
  int fSystemTrace;       /* Trace calls to vcs_system(), --systemtrace */
  int fNoSync;            /* Do not do an autosync even.  --nosync */
  char *zPath;            /* Name of webpage being served */
  char *zExtra;           /* Extra path information past the webpage name */
  char *zBaseURL;         /* Full text of the URL being served */
  char *zTop;             /* Parent directory of zPath */
  const char *zContentType;  /* The content type of the input HTTP request */
  int iErrPriority;       /* Priority of current error message */
  char *zErrMsg;          /* Text of an error message */
  int sslNotAvailable;    /* SSL is not available.  Do not redirect to https: */
  Blob cgiIn;             /* Input to an xfer www method */
  int cgiOutput;          /* Write error and status messages to CGI */
  int xferPanic;          /* Write error messages in XFER protocol */
  int fullHttpReply;      /* True for full HTTP reply.  False for CGI reply */
  Th_Interp *interp;      /* The TH1 interpreter */
  FILE *httpIn;           /* Accept HTTP input from here */
  FILE *httpOut;          /* Send HTTP output here */
  int xlinkClusterOnly;   /* Set when cloning.  Only process clusters */
  int fTimeFormat;        /* 1 for UTC.  2 for localtime.  0 not yet selected */
  int *aCommitFile;       /* Array of files to be committed */
  int markPrivate;        /* All new artifacts are private if true */
  int clockSkewSeen;      /* True if clocks on client and server out of sync */
  char isHTTP;            /* True if erver/CGI modes, else assume CLI. */
  char javascriptHyperlink; /* If true, set href= using script, not HTML */

  int urlIsFile;          /* True if a "file:" url */
  int urlIsHttps;         /* True if a "https:" url */
  int urlIsSsh;           /* True if an "ssh:" url */
  char *urlName;          /* Hostname for http: or filename for file: */
  char *urlHostname;      /* The HOST: parameter on http headers */
  char *urlProtocol;      /* "http" or "https" */
  int urlPort;            /* TCP port number for http: or https: */
  int urlDfltPort;        /* The default port for the given protocol */
  char *urlPath;          /* Pathname for http: */
  char *urlUser;          /* User id for http: */
  char *urlPasswd;        /* Password for http: */
  char *urlCanonical;     /* Canonical representation of the URL */
  char *urlProxyAuth;     /* Proxy-Authorizer: string */
  char *urlvcs;        /* The path of the ?vcs=path suffix on ssh: */
  int dontKeepUrl;        /* Do not persist the URL */
  
  const char *zLogin;     /* Login name.  "" if not logged in. */
  const char *zSSLIdentity;  /* Value of --ssl-identity option, filename of SSL client identity */
  int useLocalauth;       /* No login required if from 127.0.0.1 */
  int noPswd;             /* Logged in without password (on 127.0.0.1) */
  int userUid;            /* Integer user id */

  /* Information used to populate the RCVFROM table */
  int rcvid;              /* The rcvid.  0 if not yet defined. */
  char *zIpAddr;          /* The remote IP address */
  char *zNonce;           /* The nonce used for login */
  
  /* permissions used by the server */
  struct vcsUserPerms perm;

#ifdef vcs_ENABLE_TCL
  /* all Tcl related context necessary for integration */
  struct TclContext tcl;
#endif

  /* For defense against Cross-site Request Forgery attacks */
  char zCsrfToken[12];    /* Value of the anti-CSRF token */
  int okCsrf;             /* Anti-CSRF token is present and valid */

  int parseCnt[10];       /* Counts of artifacts parsed */
  FILE *fDebug;           /* Write debug information here, if the file exists */
  int thTrace;            /* True to enable TH1 debugging output */
  Blob thLog;             /* Text of the TH1 debugging output */

  int isHome;             /* True if rendering the "home" page */

  /* Storage for the aux() and/or option() SQL function arguments */
  int nAux;                    /* Number of distinct aux() or option() values */
  const char *azAuxName[MX_AUX]; /* Name of each aux() or option() value */
  char *azAuxParam[MX_AUX];      /* Param of each aux() or option() value */
  const char *azAuxVal[MX_AUX];  /* Value of each aux() or option() value */
  const char **azAuxOpt[MX_AUX]; /* Options of each option() value */
  int anAuxCols[MX_AUX];         /* Number of columns for option() values */
  
  int allowSymlinks;             /* Cached "allow-symlinks" option */

#ifdef vcs_ENABLE_JSON
  struct vcsJsonBits {
    int isJsonMode;            /* True if running in JSON mode, else
                                  false. This changes how errors are
                                  reported. In JSON mode we try to
                                  always output JSON-form error
                                  responses and always exit() with
                                  code 0 to avoid an HTTP 500 error.
                               */
    int resultCode;            /* used for passing back specific codes from /json callbacks. */
    int errorDetailParanoia;   /* 0=full error codes, 1=%10, 2=%100, 3=%1000 */
    cson_output_opt outOpt;    /* formatting options for JSON mode. */
    cson_value * authToken;    /* authentication token */
    char const * jsonp;        /* Name of JSONP function wrapper. */
    unsigned char dispatchDepth /* Tells JSON command dispatching
                                   which argument we are currently
                                   working on. For this purpose, arg#0
                                   is the "json" path/CLI arg.
                                */;
    struct {                   /* "garbage collector" */
      cson_value * v;
      cson_array * a;
    } gc;
    struct {                   /* JSON POST data. */
      cson_value * v;
      cson_array * a;
      int offset;              /* Tells us which PATH_INFO/CLI args
                                  part holds the "json" command, so
                                  that we can account for sub-repos
                                  and path prefixes.  This is handled
                                  differently for CLI and CGI modes.
                               */
      char const * commandStr  /*"command" request param.*/;
    } cmd;
    struct {                   /* JSON POST data. */
      cson_value * v;
      cson_object * o;
    } post;
    struct {                   /* GET/COOKIE params in JSON mode.
                                  FIXME (stephan): verify that this is
                                  still used and remove if it is not.
                               */
      cson_value * v;
      cson_object * o;
    } param;
    struct {
      cson_value * v;
      cson_object * o;
    } reqPayload;              /* request payload object (if any) */
    struct {                   /* response warnings */
      cson_value * v;
      cson_array * a;
    } warnings;
  } json;
#endif /* vcs_ENABLE_JSON */
};

/*
** Macro for debugging:
*/
#define CGIDEBUG(X)  if( g.fDebug ) cgi_debug X

#endif

Global g;

/*
** The table of web pages supported by this application is generated 
** automatically by the "mkindex" program and written into a file
** named "page_index.h".  We include that file here to get access
** to the table.
*/
#include "page_index.h"

/*
** Search for a function whose name matches zName.  Write a pointer to
** that function into *pxFunc and return 0.  If no match is found,
** return 1.  If the command is ambiguous return 2;
**
** The NameMap structure and the tables we are searching against are
** defined in the page_index.h header file which is automatically
** generated by mkindex.c program.
*/
static int name_search(
  const char *zName,       /* The name we are looking for */
  const NameMap *aMap,     /* Search in this array */
  int nMap,                /* Number of slots in aMap[] */
  int *pIndex              /* OUT: The index in aMap[] of the match */
){
  int upr, lwr, cnt, m, i;
  int n = strlen(zName);
  lwr = 0;
  upr = nMap-1;
  while( lwr<=upr ){
    int mid, c;
    mid = (upr+lwr)/2;
    c = vcs_strcmp(zName, aMap[mid].zName);
    if( c==0 ){
      *pIndex = mid;
      return 0;
    }else if( c<0 ){
      upr = mid - 1;
    }else{
      lwr = mid + 1;
    }
  }
  for(m=cnt=0, i=upr-2; cnt<2 && i<=upr+3 && i<nMap; i++){
    if( i<0 ) continue;
    if( strncmp(zName, aMap[i].zName, n)==0 ){
      m = i;
      cnt++;
    }
  }
  if( cnt==1 ){
    *pIndex = m;
    return 0;
  }
  return 1+(cnt>1);
}

/*
** atexit() handler which frees up "some" of the resources
** used by vcs.
*/
void vcs_atexit(void) {
#ifdef vcs_ENABLE_JSON
  cson_value_free(g.json.gc.v);
  memset(&g.json, 0, sizeof(g.json));
#endif
  free(g.zErrMsg);
  if(g.db){
    db_close(0);
  }
}

/*
** Search g.argv for arguments "--args FILENAME".  If found, then
** (1) remove the two arguments from g.argv
** (2) Read the file FILENAME
** (3) Use the contents of FILE to replace the two removed arguments:
**     (a) Ignore blank lines in the file
**     (b) Each non-empty line of the file is an argument, except
**     (c) If the line begins with "-" and contains a space, it is broken
**         into two arguments at the space.
*/
static void expand_args_option(void){
  Blob file = empty_blob;   /* Content of the file */
  Blob line = empty_blob;   /* One line of the file */
  unsigned int nLine;       /* Number of lines in the file*/
  unsigned int i, j, k;     /* Loop counters */
  int n;                    /* Number of bytes in one line */
  char *z;            /* General use string pointer */
  char **newArgv;     /* New expanded g.argv under construction */
  char const * zFileName;   /* input file name */
  FILE * zInFile;           /* input FILE */
  for(i=1; i<g.argc-1; i++){
    z = g.argv[i];
    if( z[0]!='-' ) continue;
    z++;
    if( z[0]=='-' ) z++;
    if( z[0]==0 ) return;   /* Stop searching at "--" */
    if( vcs_strcmp(z, "args")==0 ) break;
  }
  if( i>=g.argc-1 ) return;

  zFileName = g.argv[i+1];
  zInFile = (0==strcmp("-",zFileName))
    ? stdin
    : fopen(zFileName,"rb");
  if(!zInFile){
    vcs_panic("Cannot open -args file [%s]", zFileName);
  }else{
    blob_read_from_channel(&file, zInFile, -1);
    if(stdin != zInFile){
      fclose(zInFile);
    }
    zInFile = NULL;
  }
  z = blob_str(&file);
  for(k=0, nLine=1; z[k]; k++) if( z[k]=='\n' ) nLine++;
  newArgv = vcs_malloc( sizeof(char*)*(g.argc + nLine*2) );
  for(j=0; j<i; j++) newArgv[j] = g.argv[j];
  
  blob_rewind(&file);
  while( (n = blob_line(&file, &line))>0 ){
    if( n<=1 ) continue;
    z = blob_buffer(&line);
    z[n-1] = 0;
    if((n>1) && ('\r'==z[n-2])){
      if(n==2) continue /*empty line*/;
      z[n-2] = 0;
    }
    newArgv[j++] = z;
    if( z[0]=='-' ){
      for(k=1; z[k] && !vcs_isspace(z[k]); k++){}
      if( z[k] ){
        z[k] = 0;
        k++;
        if( z[k] ) newArgv[j++] = &z[k];
      }
    }
  }
  i += 2;
  while( i<g.argc ) newArgv[j++] = g.argv[i++];
  newArgv[j] = 0;
  g.argc = j;
  g.argv = newArgv;
}

/*
** This procedure runs first.
*/
int main(int argc, char **argv){
  const char *zCmdName = "unknown";
  int idx;
  int rc;
  int i;

#ifdef vcs_ENABLE_TCL
  g.tcl.argc = argc;
  g.tcl.argv = argv;
  g.tcl.interp = 0;
#endif

  sqlite3_config(SQLITE_CONFIG_LOG, vcs_sqlite_log, 0);
  memset(&g, 0, sizeof(g));
  g.now = time(0);
  g.argc = argc;
  g.argv = argv;
#ifdef vcs_ENABLE_JSON
#if defined(NDEBUG)
  g.json.errorDetailParanoia = 2 /* FIXME: make configurable
                                    One problem we have here is that this
                                    code is needed before the db is opened,
                                    so we can't sql for it.*/;
#else
  g.json.errorDetailParanoia = 0;
#endif
  g.json.outOpt = cson_output_opt_empty;
  g.json.outOpt.addNewline = 1;
  g.json.outOpt.indentation = 1 /* in CGI/server mode this can be configured */;
#endif /* vcs_ENABLE_JSON */
  expand_args_option();
  argc = g.argc;
  argv = g.argv;
  for(i=0; i<argc; i++) g.argv[i] = vcs_mbcs_to_utf8(argv[i]);
  if( vcs_getenv("GATEWAY_INTERFACE")!=0 && !find_option("nocgi", 0, 0)){
    zCmdName = "cgi";
    g.isHTTP = 1;
  }else if( argc<2 ){
    vcs_print(
       "Usage: %s COMMAND ...\n"
       "   or: %s help           -- for a list of common commands\n"
       "   or: %s help COMMMAND  -- for help with the named command\n",
       argv[0], argv[0], argv[0]);
    vcs_exit(1);
  }else{
    const char *zChdir = find_option("chdir",0,1);
    g.isHTTP = 0;
    g.fQuiet = find_option("quiet", 0, 0)!=0;
    g.fSqlTrace = find_option("sqltrace", 0, 0)!=0;
    g.fSqlStats = find_option("sqlstats", 0, 0)!=0;
    g.fSystemTrace = find_option("systemtrace", 0, 0)!=0;
    if( g.fSqlTrace ) g.fSqlStats = 1;
    g.fSqlPrint = find_option("sqlprint", 0, 0)!=0;
    g.fHttpTrace = find_option("httptrace", 0, 0)!=0;
    g.zLogin = find_option("user", "U", 1);
    g.zSSLIdentity = find_option("ssl-identity", 0, 1);
    if( zChdir && chdir(zChdir) ){
      vcs_fatal("unable to change directories to %s", zChdir);
    }
    if( find_option("help",0,0)!=0 ){
      /* --help anywhere on the command line is translated into
      ** "vcs help argv[1] argv[2]..." */
      int i;
      char **zNewArgv = vcs_malloc( sizeof(char*)*(g.argc+2) );
      for(i=1; i<g.argc; i++) zNewArgv[i+1] = argv[i];
      zNewArgv[i+1] = 0;
      zNewArgv[0] = argv[0];
      zNewArgv[1] = "help";
      g.argc++;
      g.argv = zNewArgv;
    }
    zCmdName = g.argv[1];
  }
  rc = name_search(zCmdName, aCommand, count(aCommand), &idx);
  if( rc==1 ){
    vcs_fatal("%s: unknown command: %s\n"
                 "%s: use \"help\" for more information\n",
                   argv[0], zCmdName, argv[0]);
  }else if( rc==2 ){
    int i, n;
    Blob couldbe;
    blob_zero(&couldbe);
    n = strlen(zCmdName);
    for(i=0; i<count(aCommand); i++){
      if( memcmp(zCmdName, aCommand[i].zName, n)==0 ){
        blob_appendf(&couldbe, " %s", aCommand[i].zName);
      }
    }
    vcs_print("%s: ambiguous command prefix: %s\n"
                 "%s: could be any of:%s\n"
                 "%s: use \"help\" for more information\n",
                 argv[0], zCmdName, argv[0], blob_str(&couldbe), argv[0]);
    vcs_exit(1);
  }
  atexit( vcs_atexit );
  aCommand[idx].xFunc();
  vcs_exit(0);
  /*NOT_REACHED*/
  return 0;
}

/*
** The following variable becomes true while processing a fatal error
** or a panic.  If additional "recursive-fatal" errors occur while
** shutting down, the recursive errors are silently ignored.
*/
static int mainInFatalError = 0;

/*
** Return the name of the current executable.
*/
const char *vcs_nameofexe(void){
#ifdef _WIN32
  return _pgmptr;
#else
  return g.argv[0];
#endif
}

/*
** Exit.  Take care to close the database first.
*/
NORETURN void vcs_exit(int rc){
  db_close(1);
  exit(rc);
}

/*
** Print an error message, rollback all databases, and quit.  These
** routines never return.
*/
NORETURN void vcs_panic(const char *zFormat, ...){
  char *z;
  va_list ap;
  int rc = 1;
  static int once = 1;
  mainInFatalError = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
#ifdef vcs_ENABLE_JSON
  if( g.json.isJsonMode ){
    json_err( 0, z, 1 );
    if( g.isHTTP ){
      rc = 0 /* avoid HTTP 500 */;
    }
  }
  else
#endif
  {
    if( g.cgiOutput && once ){
      once = 0;
      cgi_printf("<p class=\"generalError\">%h</p>", z);
      cgi_reply();
    }else if( !g.fQuiet ){
      char *zOut = mprintf("%s: %s\n", vcs_nameofexe(), z);
      vcs_puts(zOut, 1);
    }
  }
  free(z);
  db_force_rollback();
  vcs_exit(rc);
}

NORETURN void vcs_fatal(const char *zFormat, ...){
  char *z;
  int rc = 1;
  va_list ap;
  mainInFatalError = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
#ifdef vcs_ENABLE_JSON
  if( g.json.isJsonMode ){
    json_err( g.json.resultCode, z, 1 );
    if( g.isHTTP ){
      rc = 0 /* avoid HTTP 500 */;
    }
  }
  else
#endif
  {
    if( g.cgiOutput ){
      g.cgiOutput = 0;
      cgi_printf("<p class=\"generalError\">%h</p>", z);
      cgi_reply();
    }else if( !g.fQuiet ){
      char *zOut = mprintf("\r%s: %s\n", vcs_nameofexe(), z);
      vcs_puts(zOut, 1);
    }
  }
  free(z);
  db_force_rollback();
  vcs_exit(rc);
}

/* This routine works like vcs_fatal() except that if called
** recursively, the recursive call is a no-op.
*/
void vcs_fatal_recursive(const char *zFormat, ...){
  char *z;
  va_list ap;
  int rc = 1;
  if( mainInFatalError ) return;
  mainInFatalError = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
#ifdef vcs_ENABLE_JSON
  if( g.json.isJsonMode ){
    json_err( g.json.resultCode, z, 1 );
    if( g.isHTTP ){
      rc = 0 /* avoid HTTP 500 */;
    }
  } else
#endif
  {
    if( g.cgiOutput ){
      g.cgiOutput = 0;
      cgi_printf("<p class=\"generalError\">%h</p>", z);
      cgi_reply();
    }else{
      char *zOut = mprintf("\r%s: %s\n", vcs_nameofexe(), z);
      vcs_puts(zOut, 1);
      free(zOut);
    }
  }
  db_force_rollback();
  vcs_exit(rc);
}


/* Print a warning message */
void vcs_warning(const char *zFormat, ...){
  char *z;
  va_list ap;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
#ifdef vcs_ENABLE_JSON
  if(g.json.isJsonMode){
    json_warn( FSL_JSON_W_UNKNOWN, z );
  }else
#endif
  {
    if( g.cgiOutput ){
      cgi_printf("<p class=\"generalError\">%h</p>", z);
    }else{
      char *zOut = mprintf("\r%s: %s\n", vcs_nameofexe(), z);
      vcs_puts(zOut, 1);
      free(zOut);
    }
  }
  free(z);
}

/*
** Malloc and free routines that cannot fail
*/
void *vcs_malloc(size_t n){
  void *p = malloc(n==0 ? 1 : n);
  if( p==0 ) vcs_panic("out of memory");
  return p;
}
void vcs_free(void *p){
  free(p);
}
void *vcs_realloc(void *p, size_t n){
  p = realloc(p, n);
  if( p==0 ) vcs_panic("out of memory");
  return p;
}

/*
** This function implements a cross-platform "system()" interface.
*/
int vcs_system(const char *zOrigCmd){
  int rc;
#if defined(_WIN32)
  /* On windows, we have to put double-quotes around the entire command.
  ** Who knows why - this is just the way windows works.
  */
  char *zNewCmd = mprintf("\"%s\"", zOrigCmd);
  char *zMbcs = vcs_utf8_to_mbcs(zNewCmd);
  if( g.fSystemTrace ) fprintf(stderr, "SYSTEM: %s\n", zMbcs);
  rc = system(zMbcs);
  vcs_mbcs_free(zMbcs);
  free(zNewCmd);
#else
  /* On unix, evaluate the command directly.
  */
  if( g.fSystemTrace ) fprintf(stderr, "SYSTEM: %s\n", zOrigCmd);
  rc = system(zOrigCmd);
#endif 
  return rc; 
}

/*
** Turn off any NL to CRNL translation on the stream given as an
** argument.  This is a no-op on unix but is necessary on windows.
*/
void vcs_binary_mode(FILE *p){
#if defined(_WIN32)
  _setmode(_fileno(p), _O_BINARY);
#endif
#ifdef __EMX__     /* OS/2 */
  setmode(fileno(p), O_BINARY);
#endif
}



/*
** Return a name for an SQLite error code
*/
static const char *sqlite_error_code_name(int iCode){
  static char zCode[30];
  switch( iCode & 0xff ){
    case SQLITE_OK:         return "SQLITE_OK";
    case SQLITE_ERROR:      return "SQLITE_ERROR";
    case SQLITE_PERM:       return "SQLITE_PERM";
    case SQLITE_ABORT:      return "SQLITE_ABORT";
    case SQLITE_BUSY:       return "SQLITE_BUSY";
    case SQLITE_NOMEM:      return "SQLITE_NOMEM";
    case SQLITE_READONLY:   return "SQLITE_READONLY";
    case SQLITE_INTERRUPT:  return "SQLITE_INTERRUPT";
    case SQLITE_IOERR:      return "SQLITE_IOERR";
    case SQLITE_CORRUPT:    return "SQLITE_CORRUPT";
    case SQLITE_FULL:       return "SQLITE_FULL";
    case SQLITE_CANTOPEN:   return "SQLITE_CANTOPEN";
    case SQLITE_PROTOCOL:   return "SQLITE_PROTOCOL";
    case SQLITE_EMPTY:      return "SQLITE_EMPTY";
    case SQLITE_SCHEMA:     return "SQLITE_SCHEMA";
    case SQLITE_CONSTRAINT: return "SQLITE_CONSTRAINT";
    case SQLITE_MISMATCH:   return "SQLITE_MISMATCH";
    case SQLITE_MISUSE:     return "SQLITE_MISUSE";
    case SQLITE_NOLFS:      return "SQLITE_NOLFS";
    case SQLITE_FORMAT:     return "SQLITE_FORMAT";
    case SQLITE_RANGE:      return "SQLITE_RANGE";
    case SQLITE_NOTADB:     return "SQLITE_NOTADB";
    default: {
      sqlite3_snprintf(sizeof(zCode),zCode,"error code %d",iCode);
    }
  }
  return zCode;
}

/* Error logs from SQLite */
void vcs_sqlite_log(void *notUsed, int iCode, const char *zErrmsg){
  vcs_warning("%s: %s", sqlite_error_code_name(iCode), zErrmsg);
}

/*
** Print a usage comment and quit
*/
void usage(const char *zFormat){
  vcs_fatal("Usage: %s %s %s\n", vcs_nameofexe(), g.argv[1], zFormat);
}

/*
** Remove n elements from g.argv beginning with the i-th element.
*/
void remove_from_argv(int i, int n){
  int j;
  for(j=i+n; j<g.argc; i++, j++){
    g.argv[i] = g.argv[j];
  }
  g.argc = i;
}


/*
** Look for a command-line option.  If present, return a pointer.
** Return NULL if missing.
**
** hasArg==0 means the option is a flag.  It is either present or not.
** hasArg==1 means the option has an argument.  Return a pointer to the
** argument.
*/
const char *find_option(const char *zLong, const char *zShort, int hasArg){
  int i;
  int nLong;
  const char *zReturn = 0;
  assert( hasArg==0 || hasArg==1 );
  nLong = strlen(zLong);
  for(i=1; i<g.argc; i++){
    char *z;
    if (i+hasArg >= g.argc) break;
    z = g.argv[i];
    if( z[0]!='-' ) continue;
    z++;
    if( z[0]=='-' ){
      if( z[1]==0 ){
        remove_from_argv(i, 1);
        break;
      }
      z++;
    }
    if( strncmp(z,zLong,nLong)==0 ){
      if( hasArg && z[nLong]=='=' ){
        zReturn = &z[nLong+1];
        remove_from_argv(i, 1);
        break;
      }else if( z[nLong]==0 ){
        zReturn = g.argv[i+hasArg];
        remove_from_argv(i, 1+hasArg);
        break;
      }
    }else if( vcs_strcmp(z,zShort)==0 ){
      zReturn = g.argv[i+hasArg];
      remove_from_argv(i, 1+hasArg);
      break;
    }
  }
  return zReturn;
}

/*
** Verify that there are no unprocessed command-line options.  If
** Any remaining command-line argument begins with "-" print
** an error message and quit.
*/
void verify_all_options(void){
  int i;
  for(i=1; i<g.argc; i++){
    if( g.argv[i][0]=='-' ){
      vcs_fatal("unrecognized command-line option, or missing argument: %s", g.argv[i]);
    }
  }
}

/*
** Print a list of words in multiple columns.
*/
static void multi_column_list(const char **azWord, int nWord){
  int i, j, len;
  int mxLen = 0;
  int nCol;
  int nRow;
  for(i=0; i<nWord; i++){
    len = strlen(azWord[i]);
    if( len>mxLen ) mxLen = len;
  }
  nCol = 80/(mxLen+2);
  if( nCol==0 ) nCol = 1;
  nRow = (nWord + nCol - 1)/nCol;
  for(i=0; i<nRow; i++){
    const char *zSpacer = "";
    for(j=i; j<nWord; j+=nRow){
      vcs_print("%s%-*s", zSpacer, mxLen, azWord[j]);
      zSpacer = "  ";
    }
    vcs_print("\n");
  }
}

/*
** List of commands starting with zPrefix, or all commands if zPrefix is NULL.
*/
static void command_list(const char *zPrefix, int cmdMask){
  int i, nCmd;
  int nPrefix = zPrefix ? strlen(zPrefix) : 0;
  const char *aCmd[count(aCommand)];
  for(i=nCmd=0; i<count(aCommand); i++){
    const char *z = aCommand[i].zName;
    if( (aCommand[i].cmdFlags & cmdMask)==0 ) continue;
    if( zPrefix && memcmp(zPrefix, z, nPrefix)!=0 ) continue;
    aCmd[nCmd++] = aCommand[i].zName;
  }
  multi_column_list(aCmd, nCmd);
}

/*
** COMMAND: version
**
** Usage: %vcs version
**
** Print the source code version number for the vcs executable.
*/
void version_cmd(void){
  vcs_print("This is vcs version " RELEASE_VERSION " "
                MANIFEST_VERSION " " MANIFEST_DATE " UTC\n");
}


/*
** COMMAND: help
**
** Usage: %vcs help COMMAND
**    or: %vcs COMMAND -help
**
** Display information on how to use COMMAND.  To display a list of
** available commands one of:
**
**    %vcs help              Show common commands
**    %vcs help --all        Show both command and auxiliary commands
**    %vcs help --test       Show test commands only
**    %vcs help --aux        Show auxiliary commands only
*/
void help_cmd(void){
  int rc, idx;
  const char *z;
  if( g.argc<3 ){
    z = vcs_nameofexe();
    vcs_print(
      "Usage: %s help COMMAND\n"
      "Common COMMANDs:  (use \"%s help --all\" for a complete list)\n",
      z, z);
    command_list(0, CMDFLAG_1ST_TIER);
    version_cmd();
    return;
  }
  if( find_option("all",0,0) ){
    command_list(0, CMDFLAG_1ST_TIER | CMDFLAG_2ND_TIER);
    return;
  }
  if( find_option("aux",0,0) ){
    command_list(0, CMDFLAG_2ND_TIER);
    return;
  }
  if( find_option("test",0,0) ){
    command_list(0, CMDFLAG_TEST);
    return;
  }
  rc = name_search(g.argv[2], aCommand, count(aCommand), &idx);
  if( rc==1 ){
    vcs_print("unknown command: %s\nAvailable commands:\n", g.argv[2]);
    command_list(0, 0xff);
    vcs_exit(1);
  }else if( rc==2 ){
    vcs_print("ambiguous command prefix: %s\nMatching commands:\n",
                 g.argv[2]);
    command_list(g.argv[2], 0xff);
    vcs_exit(1);
  }
  z = aCmdHelp[idx];
  if( z==0 ){
    vcs_fatal("no help available for the %s command",
       aCommand[idx].zName);
  }
  while( *z ){
    if( *z=='%' && strncmp(z, "%vcs", 7)==0 ){
      vcs_print("%s", vcs_nameofexe());
      z += 7;
    }else{
      putchar(*z);
      z++;
    }
  }
  putchar('\n');
}


/*
** If running as root, chroot to the directory containing the
** repository zRepo and then drop root privileges.  Return the
** new repository name.
**
** zRepo might be a directory itself.  In that case chroot into
** the directory zRepo.
**
** Assume the user-id and group-id of the repository, or if zRepo
** is a directory, of that directory.
*/
static char *enter_chroot_jail(char *zRepo){
#if !defined(_WIN32)
  if( getuid()==0 ){
    int i;
    struct stat sStat;
    Blob dir;
    char *zDir;

    file_canonical_name(zRepo, &dir, 0);
    zDir = blob_str(&dir);
    if( file_isdir(zDir)==1 ){
      if( chdir(zDir) || chroot(zDir) || chdir("/") ){
        vcs_fatal("unable to chroot into %s", zDir);
      }
      zRepo = "/";
    }else{
      for(i=strlen(zDir)-1; i>0 && zDir[i]!='/'; i--){}
      if( zDir[i]!='/' ) vcs_panic("bad repository name: %s", zRepo);
      zDir[i] = 0;
      if( chdir(zDir) || chroot(zDir) || chdir("/") ){
        vcs_fatal("unable to chroot into %s", zDir);
      }
      zDir[i] = '/';
      zRepo = &zDir[i];
    }
    if( stat(zRepo, &sStat)!=0 ){
      vcs_fatal("cannot stat() repository: %s", zRepo);
    }
    setgid(sStat.st_gid);
    setuid(sStat.st_uid);
    if( g.db!=0 ){
      db_close(1);
      db_open_repository(zRepo);
    }
  }
#endif
  return zRepo;
}

