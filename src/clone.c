/*
** This file contains code used to clone a repository
*/
#include "config.h"
#include "clone.h"
#include <assert.h>

/*
** If there are public BLOBs that deltas from private BLOBs, then
** undeltify the public BLOBs so that the private BLOBs may be safely
** deleted.
*/
void fix_private_blob_dependencies(int showWarning){
  Bag toUndelta;
  Stmt q;
  int rid;

  db_prepare(&q,
    "SELECT "
    "   rid, (SELECT uuid FROM blob WHERE rid=delta.rid),"
    "   srcid, (SELECT uuid FROM blob WHERE rid=delta.srcid)"
    "  FROM delta"
    " WHERE srcid in private AND rid NOT IN private"
  );
  bag_init(&toUndelta);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    const char *zId = db_column_text(&q, 1);
    int srcid = db_column_int(&q, 2);
    const char *zSrc = db_column_text(&q, 3);
    if( showWarning ){
      vcs_warning(
        "public artifact %S (%d) is a delta from private artifact %S (%d)",
        zId, rid, zSrc, srcid
      );
    }
    bag_insert(&toUndelta, rid);
  }
  db_finalize(&q);
  while( (rid = bag_first(&toUndelta))>0 ){
    content_undelta(rid);
    bag_remove(&toUndelta, rid);
  }
  bag_clear(&toUndelta);
}

/*
** Delete all private content from a repository.
*/
void delete_private_content(void){
  fix_private_blob_dependencies(1);
  db_multi_exec(
    "DELETE FROM blob WHERE rid IN private;"
    "DELETE FROM delta wHERE rid IN private;"
    "DELETE FROM private;"
  );
}


/*
** COMMAND: clone
**
** Usage: %vcs clone ?OPTIONS? URL FILENAME
**
** Make a clone of a repository specified by URL in the local
** file named FILENAME.  
**
*/
void clone_cmd(void){
  bPrivate = find_option("private",0,0)!=0;
  url_proxy_options();
  if( g.argc < 4 ){
    usage("?OPTIONS? FILE-OR-URL NEW-REPOSITORY");
  }
  db_open_config(0);
  if( file_size(g.argv[3])>0 ){
    vcs_panic("file already exists: %s", g.argv[3]);
  }

  zDefaultUser = find_option("admin-user","A",1);

  url_parse(g.argv[2]);
  if( g.urlIsFile ){
    file_copy(g.urlName, g.argv[3]);
    db_close(1);
    db_open_repository(g.argv[3]);
    db_record_repository_filename(g.argv[3]);
    db_multi_exec(
      "REPLACE INTO config(name,value,mtime)"
      " VALUES('server-code', lower(hex(randomblob(20))),now());"
      "REPLACE INTO config(name,value,mtime)"
      " VALUES('last-sync-url', '%q',now());",
      g.urlCanonical
    );
    if( !bPrivate ) delete_private_content();
    shun_artifacts();
    db_create_default_users(1, zDefaultUser);
    if( zDefaultUser ){
      g.zLogin = zDefaultUser;
    }else{
      g.zLogin = db_text(0, "SELECT login FROM user WHERE cap LIKE '%%s%%'");
    }
    vcs_print("Repository cloned into %s\n", g.argv[3]);
  }else{
    db_create_repository(g.argv[3]);
    db_open_repository(g.argv[3]);
    db_begin_transaction();
    db_record_repository_filename(g.argv[3]);
    db_initial_setup(0, zDefaultUser, 0);
    user_select();
    db_set("content-schema", CONTENT_SCHEMA, 0);
    db_set("aux-schema", AUX_SCHEMA, 0);
    db_set("last-sync-url", g.argv[2], 0);
    if( g.zSSLIdentity!=0 ){
      /* If the --ssl-identity option was specified, store it as a setting */
      Blob fn;
      blob_zero(&fn);
      file_canonical_name(g.zSSLIdentity, &fn, 0);
      db_set("ssl-identity", blob_str(&fn), 0);
      blob_reset(&fn);
    }
    db_multi_exec(
      "REPLACE INTO config(name,value,mtime)"
      " VALUES('server-code', lower(hex(randomblob(20))), now());"
    );
    url_enable_proxy(0);
    url_get_password_if_needed();
    g.xlinkClusterOnly = 1;
    nErr = client_sync(0,0,1,bPrivate,CONFIGSET_ALL,0);
    g.xlinkClusterOnly = 0;
    verify_cancel();
    db_end_transaction(0);
    db_close(1);
    if( nErr ){
      file_delete(g.argv[3]);
      vcs_fatal("server returned an error - clone aborted");
    }
    db_open_repository(g.argv[3]);
  }
  db_begin_transaction();
  vcs_print("Rebuilding repository meta-data...\n");
  rebuild_db(0, 1, 0);
  vcs_print("project-id: %s\n", db_get("project-code", 0));
  vcs_print("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  vcs_print("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
  zPw = g.urlPasswd;
  if( !g.dontKeepUrl && zPw) db_set("last-sync-pw", obscure(zPw), 0);
  db_end_transaction(0);
}
