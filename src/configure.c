/*
** This file contains code used to manage repository configurations.
**
** By "responsitory configure" we mean the local state of a repository
** distinct from the versioned files.
*/
#include "config.h"
#include "configure.h"
#include <assert.h>

/*
** Return true if z[] is not a "safe" SQL token.  A safe token is one of:
**
**   *   A string literal
**   *   A blob literal
**   *   An integer literal  (no floating point)
**   *   NULL
*/
static int safeSql(const char *z){
  int i;
  if( z==0 || z[0]==0 ) return 0;
  if( (z[0]=='x' || z[0]=='X') && z[1]=='\'' ) z++;
  if( z[0]=='\'' ){
    for(i=1; z[i]; i++){
      if( z[i]=='\'' ){
        i++;
        if( z[i]=='\'' ){ continue; }
        return z[i]==0;
      }
    }
    return 0;
  }else{
    char c;
    for(i=0; (c = z[i])!=0; i++){
      if( !vcs_isalnum(c) ) return 0;
    }
  }
  return 1;
}

/*
** Return true if z[] consists of nothing but digits
*/
static int safeInt(const char *z){
  int i;
  if( z==0 || z[0]==0 ) return 0;
  for(i=0; vcs_isdigit(z[i]); i++){}
  return z[i]==0;
}

/*
** Process a single "config" card received from the other side of a
** sync session.
**
** Process a file full of "config" cards.
*/
void configure_receive_all(Blob *pIn, int groupMask){
  Blob line;
  int nToken;
  int size;
  Blob aToken[4];

  configHasBeenReset = 0;
  while( blob_line(pIn, &line) ){
    if( blob_buffer(&line)[0]=='#' ) continue;
    nToken = blob_tokenize(&line, aToken, count(aToken));
    if( blob_eq(&aToken[0],"config")
     && nToken==3
     && blob_is_int(&aToken[2], &size)
    ){
      const char *zName = blob_str(&aToken[1]);
      Blob content;
      blob_zero(&content);
      blob_extract(pIn, size, &content);
      g.perm.Admin = g.perm.RdAddr = 1;
      configure_receive(zName, &content, groupMask);
      blob_reset(&content);
      blob_seek(pIn, 1, BLOB_SEEK_CUR);
    }
  }
}
    

/*
** Send "config" cards using the new format for all elements of a group
** that have recently changed.
**
** Output goes into pOut.  The groupMask identifies the group(s) to be sent.
** Send only entries whose timestamp is later than or equal to iStart.
**
** Return the number of cards sent.
*/
int configure_send_group(
  Blob *pOut,              /* Write output here */
  int groupMask,           /* Mask of groups to be send */
  sqlite3_int64 iStart     /* Only write values changed since this time */
){
  Stmt q;
  Blob rec;
  int ii;
  int nCard = 0;

  blob_zero(&rec);
  if( groupMask & CONFIGSET_SHUN ){
    db_prepare(&q, "SELECT mtime, quote(uuid), quote(scom) FROM shun"
                   " WHERE mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s scom %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2)
      );
      blob_appendf(pOut, "config /shun %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  if( groupMask & CONFIGSET_USER ){
    db_prepare(&q, "SELECT mtime, quote(login), quote(pw), quote(cap),"
                   "       quote(info), quote(photo) FROM user"
                   " WHERE mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s pw %s cap %s info %s photo %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2),
        db_column_text(&q, 3),
        db_column_text(&q, 4),
        db_column_text(&q, 5)
      );
      blob_appendf(pOut, "config /user %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  if( groupMask & CONFIGSET_TKT ){
    db_prepare(&q, "SELECT mtime, quote(title), quote(owner), quote(cols),"
                   "       quote(sqlcode) FROM reportfmt"
                   " WHERE mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s owner %s cols %s sqlcode %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2),
        db_column_text(&q, 3),
        db_column_text(&q, 4)
      );
      blob_appendf(pOut, "config /reportfmt %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  if( groupMask & CONFIGSET_ADDR ){
    db_prepare(&q, "SELECT mtime, quote(hash), quote(content) FROM concealed"
                   " WHERE mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s content %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2)
      );
      blob_appendf(pOut, "config /concealed %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  db_prepare(&q, "SELECT mtime, quote(name), quote(value) FROM config"
                 " WHERE name=:name AND mtime>=%lld", iStart);
  for(ii=0; ii<count(aConfig); ii++){
    if( (aConfig[ii].groupMask & groupMask)!=0 && aConfig[ii].zName[0]!='@' ){
      db_bind_text(&q, ":name", aConfig[ii].zName);
      while( db_step(&q)==SQLITE_ROW ){
        blob_appendf(&rec,"%s %s value %s",
          db_column_text(&q, 0),
          db_column_text(&q, 1),
          db_column_text(&q, 2)
        );
        blob_appendf(pOut, "config /config %d\n%s\n",
                     blob_size(&rec), blob_str(&rec));
        nCard++;
        blob_reset(&rec);
      }
      db_reset(&q);
    }
  }
  db_finalize(&q);
  return nCard;
}

/*
** Identify a configuration group by name.  Return its mask.
** Throw an error if no match.
*/
int configure_name_to_mask(const char *z, int notFoundIsFatal){
  int i;
  int n = strlen(z);
  for(i=0; i<count(aGroupName); i++){
    if( strncmp(z, &aGroupName[i].zName[1], n)==0 ){
      return aGroupName[i].groupMask;
    }
  }
  if( notFoundIsFatal ){
    vcs_print("Available configuration areas:\n");
    for(i=0; i<count(aGroupName); i++){
      vcs_print("  %-10s %s\n", &aGroupName[i].zName[1], aGroupName[i].zHelp);
    }
    vcs_fatal("no such configuration area: \"%s\"", z);
  }
  return 0;
}

/*
** Write SQL text into file zFilename that will restore the configuration
** area identified by mask to its current state from any other state.
*/
static void export_config(
  int groupMask,            /* Mask indicating which configuration to export */
  const char *zMask,        /* Name of the configuration */
  sqlite3_int64 iStart,     /* Start date */
  const char *zFilename     /* Write into this file */
){
  Blob out;
  blob_zero(&out);
  blob_appendf(&out, 
    "# The \"%s\" configuration exported from\n"
    "# repository \"%s\"\n"
    "# on %s\n",
    zMask, g.zRepositoryName,
    db_text(0, "SELECT datetime('now')")
  );
  configure_send_group(&out, groupMask, iStart);
  blob_write_to_file(&out, zFilename);
  blob_reset(&out);
}


/*
** COMMAND: configuration*
**
** Usage: %vcs configuration METHOD ... ?OPTIONS?
**
** Where METHOD is one of: export import merge pull push reset.  All methods
** accept the -R or --repository option to specific a repository.
**
**    %vcs configuration export AREA FILENAME
**
**         Write to FILENAME exported configuraton information for AREA.
**         AREA can be one of:  all email project shun skin ticket user
**
**    %vcs configuration import FILENAME
**
**         Read a configuration from FILENAME, overwriting the current
**         configuration.
**
**    %vcs configuration merge FILENAME
**
**         Read a configuration from FILENAME and merge its values into
**         the current configuration.  Existing values take priority over
**         values read from FILENAME.
**
**    %vcs configuration pull AREA ?URL?
**
**         Pull and install the configuration from a different server
**         identified by URL.  If no URL is specified, then the default
**         server is used. Use the --legacy option for the older protocol
**         (when talking to servers compiled prior to 2011-04-27.)  Use
**         the --overwrite flag to completely replace local settings with
**         content received from URL.
**
**    %vcs configuration push AREA ?URL?
**
**         Push the local configuration into the remote server identified
**         by URL.  Admin privilege is required on the remote server for
**         this to work.  When the same record exists both locally and on
**         the remote end, the one that was most recently changed wins.
**         Use the --legacy flag when talking to holder servers.
**
**    %vcs configuration reset AREA
**
**         Restore the configuration to the default.  AREA as above.
**
**    %vcs configuration sync AREA ?URL?
**
**         Synchronize configuration changes in the local repository with
**         the remote repository at URL.  
**
** Options:
**    -R|--repository FILE       Extract info from repository FILE
**
** See also: settings, unset
*/
void configuration_cmd(void){
  int n;
  const char *zMethod;
  if( g.argc<3 ){
    usage("export|import|merge|pull|reset ...");
  }
  db_find_and_open_repository(0, 0);
  db_open_config(0);
  zMethod = g.argv[2];
  n = strlen(zMethod);
  if( strncmp(zMethod, "export", n)==0 ){
    int mask;
    const char *zSince = find_option("since",0,1);
    sqlite3_int64 iStart;
    if( g.argc!=5 ){
      usage("export AREA FILENAME");
    }
    mask = configure_name_to_mask(g.argv[3], 1);
    if( zSince ){
      iStart = db_multi_exec(
         "SELECT coalesce(strftime('%%s',%Q),strftime('%%s','now',%Q))+0",
         zSince, zSince
      );
    }else{
      iStart = 0;
    }
    export_config(mask, g.argv[3], iStart, g.argv[4]);
  }else
  if( strncmp(zMethod, "import", n)==0 
       || strncmp(zMethod, "merge", n)==0 ){
    Blob in;
    int groupMask;
    if( g.argc!=4 ) usage(mprintf("%s FILENAME",zMethod));
    blob_read_from_file(&in, g.argv[3]);
    db_begin_transaction();
    if( zMethod[0]=='i' ){
      groupMask = CONFIGSET_ALL | CONFIGSET_OVERWRITE;
    }else{
      groupMask = CONFIGSET_ALL;
    }
    configure_receive_all(&in, groupMask);
    db_end_transaction(0);
  }else
  if( strncmp(zMethod, "pull", n)==0
   || strncmp(zMethod, "push", n)==0
   || strncmp(zMethod, "sync", n)==0
  ){
    int mask;
    const char *zServer;
    const char *zPw;
    int legacyFlag = 0;
    int overwriteFlag = 0;
    if( zMethod[0]!='s' ) legacyFlag = find_option("legacy",0,0)!=0;
    if( strncmp(zMethod,"pull",n)==0 ){
      overwriteFlag = find_option("overwrite",0,0)!=0;
    }
    url_proxy_options();
    if( g.argc!=4 && g.argc!=5 ){
      usage("pull AREA ?URL?");
    }
    mask = configure_name_to_mask(g.argv[3], 1);
    if( g.argc==5 ){
      zServer = g.argv[4];
      zPw = 0;
      g.dontKeepUrl = 1;
    }else{
      zServer = db_get("last-sync-url", 0);
      if( zServer==0 ){
        vcs_fatal("no server specified");
      }
      zPw = unobscure(db_get("last-sync-pw", 0));
    }
    url_parse(zServer);
    if( g.urlPasswd==0 && zPw ) g.urlPasswd = mprintf("%s", zPw);
    user_select();
    url_enable_proxy("via proxy: ");
    if( legacyFlag ) mask |= CONFIGSET_OLDFORMAT;
    if( overwriteFlag ) mask |= CONFIGSET_OVERWRITE;
    if( strncmp(zMethod, "push", n)==0 ){
      client_sync(0,0,0,0,0,mask);
    }else if( strncmp(zMethod, "pull", n)==0 ){
      client_sync(0,0,0,0,mask,0);
    }else{
      client_sync(0,0,0,0,mask,mask);
    }
  }else
  if( strncmp(zMethod, "reset", n)==0 ){
    int mask, i;
    char *zBackup;
    if( g.argc!=4 ) usage("reset AREA");
    mask = configure_name_to_mask(g.argv[3], 1);
    zBackup = db_text(0, 
       "SELECT strftime('config-backup-%%Y%%m%%d%%H%%M%%f','now')");
    db_begin_transaction();
    export_config(mask, g.argv[3], 0, zBackup);
    for(i=0; i<count(aConfig); i++){
      const char *zName = aConfig[i].zName;
      if( (aConfig[i].groupMask & mask)==0 ) continue;
      if( zName[0]!='@' ){
        db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
      }else if( vcs_strcmp(zName,"@user")==0 ){
        db_multi_exec("DELETE FROM user");
        db_create_default_users(0, 0);
      }else if( vcs_strcmp(zName,"@concealed")==0 ){
        db_multi_exec("DELETE FROM concealed");
      }else if( vcs_strcmp(zName,"@shun")==0 ){
        db_multi_exec("DELETE FROM shun");
      }else if( vcs_strcmp(zName,"@reportfmt")==0 ){
        db_multi_exec("DELETE FROM reportfmt");
      }
    }
    db_end_transaction(0);
    vcs_print("Configuration reset to factory defaults.\n");
    vcs_print("To recover, use:  %s %s import %s\n", 
            vcs_nameofexe(), g.argv[1], zBackup);
  }else
  {
    vcs_fatal("METHOD should be one of:"
                 " export import merge pull push reset");
  }
}
