/*
** This file contains code used to push, pull, and sync a repository
*/
#include "config.h"
#include "sync.h"
#include <assert.h>

#if INTERFACE
/*
** Flags used to determine which direction(s) an autosync goes in.
*/
#define AUTOSYNC_PUSH  1
#define AUTOSYNC_PULL  2

#endif /* INTERFACE */

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
static void process_sync_args(int *pConfigSync, int *pPrivate){
  const char *zUrl = 0;
  const char *zPw = 0;
  int configSync = 0;
  int urlOptional = find_option("autourl",0,0)!=0;
  g.dontKeepUrl = find_option("once",0,0)!=0;
  *pPrivate = find_option("private",0,0)!=0;
  url_proxy_options();
  db_find_and_open_repository(0, 0);
  db_open_config(0);
  if( g.argc==2 ){
    zUrl = db_get("last-sync-url", 0);
    zPw = unobscure(db_get("last-sync-pw", 0));
    if( db_get_boolean("auto-shun",1) ) configSync = CONFIGSET_SHUN;
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
  }
  if( zUrl==0 ){
    if( urlOptional ) vcs_exit(0);
    usage("URL");
  }
  url_parse(zUrl);
  if( g.urlUser!=0 && g.urlPasswd==0 ){
    if( zPw==0 ){
      url_prompt_for_password();
    }else{
      g.urlPasswd = mprintf("%s", zPw);
    }
  }
  if( !g.dontKeepUrl ){
    db_set("last-sync-url", g.urlCanonical, 0);
    if( g.urlPasswd ) db_set("last-sync-pw", obscure(g.urlPasswd), 0);
  }
  user_select();
  if( g.argc==2 ){
    vcs_print("Server:    %s\n", g.urlCanonical);
  }
  url_enable_proxy("via proxy: ");
  *pConfigSync = configSync;
}

/*
** COMMAND: pull
**
** Usage: %vcs pull ?URL? ?options?
**
*/
void pull_cmd(void){
  int syncFlags;
  int bPrivate;
  process_sync_args(&syncFlags, &bPrivate);
  client_sync(0,1,0,bPrivate,syncFlags,0);
}

/*
** COMMAND: push
**
** Usage: %vcs push ?URL? ?options?
*/
void push_cmd(void){
  int syncFlags;
  int bPrivate;
  process_sync_args(&syncFlags, &bPrivate);
  if( db_get_boolean("dont-push",0) ){
    vcs_fatal("pushing is prohibited: the 'dont-push' option is set");
  }
  client_sync(1,0,0,bPrivate,0,0);
}


/*
** COMMAND: sync
**
** Usage: %vcs sync ?URL? ?options?
*/
void sync_cmd(void){
  int syncFlags;
  int bPrivate;
  int pushFlag = 1;
  process_sync_args(&syncFlags, &bPrivate);
  if( db_get_boolean("dont-push",0) ) pushFlag = 0;
  client_sync(pushFlag,1,0,bPrivate,syncFlags,0);
  if( pushFlag==0 ){
    vcs_warning("pull only: the 'dont-push' option is set");
  }
}
