/*
** This file contains code used to merge the changes in the current
** checkout into a different version and switch to that version.
*/
#include "config.h"
#include "update.h"
#include <assert.h>

/*
** Return true if artifact rid is a version
*/
int is_a_version(int rid){
  return db_exists("SELECT 1 FROM event WHERE objid=%d AND type='ci'", rid);
}

/* This variable is set if we are doing an internal update.  It is clear
** when running the "update" command.
*/
static int internalUpdate = 0;
static int internalConflictCnt = 0;

/*
** Do an update to version vid.  
**
** Start an undo session but do not terminate it.  Do not autosync.
*/
int update_to(int vid){
  int savedArgc;
  char **savedArgv;
  char *newArgv[3];
  newArgv[0] = g.argv[0];
  newArgv[1] = "update";
  newArgv[2] = 0;
  savedArgv = g.argv;
  savedArgc = g.argc;
  g.argc = 2;
  g.argv = newArgv;
  internalUpdate = vid;
  internalConflictCnt = 0;
  update_cmd();
  g.argc = savedArgc;
  g.argv = savedArgv;
  return internalConflictCnt;
}

/*
** COMMAND: update
**
** Usage: %vcs update ?OPTIONS? ?VERSION? ?FILES...?
**
** Change the version of the current checkout to VERSION.  Any uncommitted
** changes are retained and applied to the new checkout.
**
*/
void update_cmd(void){
  int vid;              /* Current version */
  int tid=0;            /* Target version - version we are changing to */
  Stmt q;
  int latestFlag;       /* --latest.  Pick the latest version if true */
  int nochangeFlag;     /* -n or --nochange.  Do a dry run */
  int verboseFlag;      /* -v or --verbose.  Output extra information */
  int debugFlag;        /* --debug option */
  int nChng;            /* Number of file renames */
  int *aChng;           /* Array of file renames */
  int i;                /* Loop counter */
  int nConflict = 0;    /* Number of merge conflicts */
  int nOverwrite = 0;   /* Number of unmanaged files overwritten */
  Stmt mtimeXfer;       /* Statment to transfer mtimes */

  if( !internalUpdate ){
    undo_capture_command_line();
    url_proxy_options();
  }
  latestFlag = find_option("latest",0, 0)!=0;
  nochangeFlag = find_option("nochange","n",0)!=0;
  verboseFlag = find_option("verbose","v",0)!=0;
  debugFlag = find_option("debug",0,0)!=0;
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    vcs_fatal("cannot find current version");
  }
  if( !nochangeFlag && !internalUpdate ) autosync(AUTOSYNC_PULL);
  
  /* Create any empty directories now, as well as after the update,
  ** so changes in settings are reflected now */
  if( !nochangeFlag ) ensure_empty_dirs_created();

  if( internalUpdate ){
    tid = internalUpdate;
  }else if( g.argc>=3 ){
    if( vcs_strcmp(g.argv[2], "current")==0 ){
      /* If VERSION is "current", then use the same algorithm to find the
      ** target as if VERSION were omitted. */
    }else if( vcs_strcmp(g.argv[2], "latest")==0 ){
      /* If VERSION is "latest", then use the same algorithm to find the
      ** target as if VERSION were omitted and the --latest flag is present.
      */
      latestFlag = 1;
    }else{
      tid = name_to_typed_rid(g.argv[2],"ci");
      if( tid==0 ){
        vcs_fatal("no such version: %s", g.argv[2]);
      }else if( !is_a_version(tid) ){
        vcs_fatal("no such version: %s", g.argv[2]);
      }
    }
  }
  
  /* If no VERSION is specified on the command-line, then look for a
  ** descendent of the current version.  If there are multiple descendents,
  ** look for one from the same branch as the current version.  If there
  ** are still multiple descendents, show them all and refuse to update
  ** until the user selects one.
  */
  if( tid==0 ){
    int closeCode = 1;
    compute_leaves(vid, closeCode);
    if( !db_exists("SELECT 1 FROM leaves") ){
      closeCode = 0;
      compute_leaves(vid, closeCode);
    }
    if( !latestFlag && db_int(0, "SELECT count(*) FROM leaves")>1 ){
      db_multi_exec(
        "DELETE FROM leaves WHERE rid NOT IN"
        "   (SELECT leaves.rid FROM leaves, tagxref"
        "     WHERE leaves.rid=tagxref.rid AND tagxref.tagid=%d"
        "       AND tagxref.value==(SELECT value FROM tagxref"
                                   " WHERE tagid=%d AND rid=%d))",
        TAG_BRANCH, TAG_BRANCH, vid
      );
      if( db_int(0, "SELECT count(*) FROM leaves")>1 ){
        compute_leaves(vid, closeCode);
        db_prepare(&q, 
          "%s "
          "   AND event.objid IN leaves"
          " ORDER BY event.mtime DESC",
          timeline_query_for_tty()
        );
        print_timeline(&q, 100, 0);
        db_finalize(&q);
        vcs_fatal("Multiple descendants");
      }
    }
    tid = db_int(0, "SELECT rid FROM leaves, event"
                    " WHERE event.objid=leaves.rid"
                    " ORDER BY event.mtime DESC"); 
    if( tid==0 ) tid = vid;
  }

  if( tid==0 ){
    vcs_panic("Internal Error: unable to find a version to update to.");
  }

  db_begin_transaction();
  vfile_check_signature(vid, 1, 0);
  if( !nochangeFlag && !internalUpdate ) undo_begin();
  load_vfile_from_rid(tid);

  /*
  ** The record.fn field is used to match files against each other.  The
  ** FV table contains one row for each each unique filename in
  ** in the current checkout, the pivot, and the version being merged.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS fv;"
    "CREATE TEMP TABLE fv("
    "  fn TEXT PRIMARY KEY,"      /* The filename relative to root */
    "  idv INTEGER,"              /* VFILE entry for current version */
    "  idt INTEGER,"              /* VFILE entry for target version */
    "  chnged BOOLEAN,"           /* True if current version has been edited */
    "  islinkv BOOLEAN,"          /* True if current file is a link */
    "  islinkt BOOLEAN,"          /* True if target file is a link */
    "  ridv INTEGER,"             /* Record ID for current version */
    "  ridt INTEGER,"             /* Record ID for target */
    "  isexe BOOLEAN,"            /* Does target have execute permission? */
    "  fnt TEXT"                  /* Filename of same file on target version */
    ");"
  );

  /* Add files found in the current version
  */
  db_multi_exec(
    "INSERT OR IGNORE INTO fv(fn,fnt,idv,idt,ridv,ridt,isexe,chnged)"
    " SELECT pathname, pathname, id, 0, rid, 0, isexe, chnged"
    "   FROM vfile WHERE vid=%d",
    vid
  );

  /* Compute file name changes on V->T.  Record name changes in files that
  ** have changed locally.
  */
  find_filename_changes(vid, tid, 1, &nChng, &aChng, debugFlag ? "V->T": 0);
  if( nChng ){
    for(i=0; i<nChng; i++){
      db_multi_exec(
        "UPDATE fv"
        "   SET fnt=(SELECT name FROM filename WHERE fnid=%d)"
        " WHERE fn=(SELECT name FROM filename WHERE fnid=%d) AND chnged",
        aChng[i*2+1], aChng[i*2]
      );
    }
    vcs_free(aChng);
  }

  /* Add files found in the target version T but missing from the current
  ** version V.
  */
  db_multi_exec(
    "INSERT OR IGNORE INTO fv(fn,fnt,idv,idt,ridv,ridt,isexe,chnged)"
    " SELECT pathname, pathname, 0, 0, 0, 0, isexe, 0 FROM vfile"
    "  WHERE vid=%d"
    "    AND pathname NOT IN (SELECT fnt FROM fv)",
    tid
  );

  /*
  ** Compute the file version ids for T
  */
  db_multi_exec(
    "UPDATE fv SET"
    " idt=coalesce((SELECT id FROM vfile WHERE vid=%d AND pathname=fnt),0),"
    " ridt=coalesce((SELECT rid FROM vfile WHERE vid=%d AND pathname=fnt),0)",
    tid, tid
  );

  /*
  ** Add islink information
  */
  db_multi_exec(
    "UPDATE fv SET"
    " islinkv=coalesce((SELECT islink FROM vfile"
                       " WHERE vid=%d AND pathname=fnt),0),"
    " islinkt=coalesce((SELECT islink FROM vfile"
                       " WHERE vid=%d AND pathname=fnt),0)",
    vid, tid
  );


  if( debugFlag ){
    db_prepare(&q,
       "SELECT rowid, fn, fnt, chnged, ridv, ridt, isexe,"
       "       islinkv, islinkt FROM fv"
    );
    while( db_step(&q)==SQLITE_ROW ){
       vcs_print("%3d: ridv=%-4d ridt=%-4d chnged=%d isexe=%d"
                    " islinkv=%d  islinkt=%d\n",
          db_column_int(&q, 0),
          db_column_int(&q, 4),
          db_column_int(&q, 5),
          db_column_int(&q, 3),
          db_column_int(&q, 6),
          db_column_int(&q, 7),
          db_column_int(&q, 8));
       vcs_print("     fnv = [%s]\n", db_column_text(&q, 1));
       vcs_print("     fnt = [%s]\n", db_column_text(&q, 2));
    }
    db_finalize(&q);
  }

  /* If FILES appear on the command-line, remove from the "fv" table
  ** every entry that is not named on the command-line or which is not
  ** in a directory named on the command-line.
  */
  if( g.argc>=4 ){
    Blob sql;              /* SQL statement to purge unwanted entries */
    Blob treename;         /* Normalized filename */
    int i;                 /* Loop counter */
    const char *zSep;      /* Term separator */

    blob_zero(&sql);
    blob_append(&sql, "DELETE FROM fv WHERE ", -1);
    zSep = "";
    for(i=3; i<g.argc; i++){
      file_tree_name(g.argv[i], &treename, 1);
      if( file_wd_isdir(g.argv[i])==1 ){
        if( blob_size(&treename) != 1 || blob_str(&treename)[0] != '.' ){
          blob_appendf(&sql, "%sfn NOT GLOB '%b/*' ", zSep, &treename);
        }else{
          blob_reset(&sql);
          break;
        }
      }else{
        blob_appendf(&sql, "%sfn<>%B ", zSep, &treename);
      }
      zSep = "AND ";
      blob_reset(&treename);
    }
    db_multi_exec(blob_str(&sql));
    blob_reset(&sql);
  }

  /*
  ** Alter the content of the checkout so that it conforms with the
  ** target
  */
  db_prepare(&q, 
    "SELECT fn, idv, ridv, idt, ridt, chnged, fnt,"
    "       isexe, islinkv, islinkt FROM fv ORDER BY 1"
  );
  db_prepare(&mtimeXfer,
    "UPDATE vfile SET mtime=(SELECT mtime FROM vfile WHERE id=:idv)"
    " WHERE id=:idt"
  );
  assert( g.zLocalRoot!=0 );
  assert( strlen(g.zLocalRoot)>1 );
  assert( g.zLocalRoot[strlen(g.zLocalRoot)-1]=='/' );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);  /* The filename from root */
    int idv = db_column_int(&q, 1);             /* VFILE entry for current */
    int ridv = db_column_int(&q, 2);            /* RecordID for current */
    int idt = db_column_int(&q, 3);             /* VFILE entry for target */
    int ridt = db_column_int(&q, 4);            /* RecordID for target */
    int chnged = db_column_int(&q, 5);          /* Current is edited */
    const char *zNewName = db_column_text(&q,6);/* New filename */
    int isexe = db_column_int(&q, 7);           /* EXE perm for new file */
    int islinkv = db_column_int(&q, 8);         /* Is current file is a link */
    int islinkt = db_column_int(&q, 9);         /* Is target file is a link */
    char *zFullPath;                            /* Full pathname of the file */
    char *zFullNewPath;                         /* Full pathname of dest */
    char nameChng;                              /* True if the name changed */

    zFullPath = mprintf("%s%s", g.zLocalRoot, zName);
    zFullNewPath = mprintf("%s%s", g.zLocalRoot, zNewName);
    nameChng = vcs_strcmp(zName, zNewName);
    if( idv>0 && ridv==0 && idt>0 && ridt>0 ){
      /* Conflict.  This file has been added to the current checkout
      ** but also exists in the target checkout.  Use the current version.
      */
      vcs_print("CONFLICT %s\n", zName);
      nConflict++;
    }else if( idt>0 && idv==0 ){
      /* File added in the target. */
      if( file_wd_isfile_or_link(zFullPath) ){
        vcs_print("ADD %s (overwrites an unmanaged file)\n", zName);
        nOverwrite++;
      }else{
        vcs_print("ADD %s\n", zName);
      }
      undo_save(zName);
      if( !nochangeFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt>0 && idv>0 && ridt!=ridv && chnged==0 ){
      /* The file is unedited.  Change it to the target version */
      undo_save(zName);
      vcs_print("UPDATE %s\n", zName);
      if( !nochangeFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt>0 && idv>0 && file_wd_size(zFullPath)<0 ){
      /* The file missing from the local check-out. Restore it to the
      ** version that appears in the target. */
      vcs_print("UPDATE %s\n", zName);
      undo_save(zName);
      if( !nochangeFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt==0 && idv>0 ){
      if( ridv==0 ){
        /* Added in current checkout.  Continue to hold the file as
        ** as an addition */
        db_multi_exec("UPDATE vfile SET vid=%d WHERE id=%d", tid, idv);
      }else if( chnged ){
        /* Edited locally but deleted from the target.  Do not track the
        ** file but keep the edited version around. */
        vcs_print("CONFLICT %s - edited locally but deleted by update\n",
                     zName);
        nConflict++;
      }else{
        vcs_print("REMOVE %s\n", zName);
        undo_save(zName);
        if( !nochangeFlag ) file_delete(zFullPath);
      }
    }else if( idt>0 && idv>0 && ridt!=ridv && chnged ){
      /* Merge the changes in the current tree into the target version */
      Blob r, t, v;
      int rc;
      if( nameChng ){
        vcs_print("MERGE %s -> %s\n", zName, zNewName);
      }else{
        vcs_print("MERGE %s\n", zName);
      }
      if( islinkv || islinkt /* || file_wd_islink(zFullPath) */ ){
        vcs_print("***** Cannot merge symlink %s\n", zNewName);
        nConflict++;        
      }else{
        undo_save(zName);
        content_get(ridt, &t);
        content_get(ridv, &v);
        rc = merge_3way(&v, zFullPath, &t, &r);
        if( rc>=0 ){
          if( !nochangeFlag ){
            blob_write_to_file(&r, zFullNewPath);
            file_wd_setexe(zFullNewPath, isexe);
          }
          if( rc>0 ){
            vcs_print("***** %d merge conflicts in %s\n", rc, zNewName);
            nConflict++;
          }
        }else{
          if( !nochangeFlag ){
            blob_write_to_file(&t, zFullNewPath);
            file_wd_setexe(zFullNewPath, isexe);
          }
          vcs_print("***** Cannot merge binary file %s\n", zNewName);
          nConflict++;
        }
      }
      if( nameChng && !nochangeFlag ) file_delete(zFullPath);
      blob_reset(&v);
      blob_reset(&t);
      blob_reset(&r);
    }else{
      if( chnged ){
        if( verboseFlag ) vcs_print("EDITED %s\n", zName);
      }else{
        db_bind_int(&mtimeXfer, ":idv", idv);
        db_bind_int(&mtimeXfer, ":idt", idt);
        db_step(&mtimeXfer);
        db_reset(&mtimeXfer);
        if( verboseFlag ) vcs_print("UNCHANGED %s\n", zName);
      }
    }
    free(zFullPath);
    free(zFullNewPath);
  }
  db_finalize(&q);
  db_finalize(&mtimeXfer);
  vcs_print("--------------\n");
  show_common_info(tid, "updated-to:", 1, 0);

  /* Report on conflicts
  */
  if( !nochangeFlag ){
    Stmt q;
    int nMerge = 0;
    db_prepare(&q, "SELECT uuid, id FROM vmerge JOIN blob ON merge=rid"
                   " WHERE id<=0");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zLabel = "merge";
      switch( db_column_int(&q, 1) ){
        case -1:  zLabel = "cherrypick merge"; break;
        case -2:  zLabel = "backout merge";    break;
      }
      vcs_warning("uncommitted %s against %S.",
                     zLabel, db_column_text(&q, 0));
      nMerge++;
    }
    db_finalize(&q);
    
    if( nConflict ){
      if( internalUpdate ){
        internalConflictCnt = nConflict;
        nConflict = 0;
      }else{
        vcs_warning("WARNING: %d merge conflicts", nConflict);
      }
    }
    if( nOverwrite ){
      vcs_warning("WARNING: %d unmanaged files were overwritten",
                     nOverwrite);
    }
    if( nMerge ){
      vcs_warning("WARNING: %d uncommitted prior merges", nMerge);
    }
  }
  
  /*
  ** Clean up the mid and pid VFILE entries.  Then commit the changes.
  */
  if( nochangeFlag ){
    db_end_transaction(1);  /* With --nochange, rollback changes */
  }else{
    ensure_empty_dirs_created();
    if( g.argc<=3 ){
      /* All files updated.  Shift the current checkout to the target. */
      db_multi_exec("DELETE FROM vfile WHERE vid!=%d", tid);
      checkout_set_all_exe(tid);
      manifest_to_disk(tid);
      db_lset_int("checkout", tid);
    }else{
      /* A subset of files have been checked out.  Keep the current
      ** checkout unchanged. */
      db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
    }
    if( !internalUpdate ) undo_finish();
    db_end_transaction(0);
  }
}

/*
** Make sure empty directories are created
*/
void ensure_empty_dirs_created(void){
  /* Make empty directories? */
  char *zEmptyDirs = db_get("empty-dirs", 0);
  if( zEmptyDirs!=0 ){
    char *bc;
    Blob dirName;
    Blob dirsList;

    blob_zero(&dirsList);
    blob_init(&dirsList, zEmptyDirs, strlen(zEmptyDirs));
    /* Replace commas by spaces */
    bc = blob_str(&dirsList);
    while( (*bc)!='\0' ){
      if( (*bc)==',' ) { *bc = ' '; }
      ++bc;
    }
    /* Make directories */
    blob_zero(&dirName);
    while( blob_token(&dirsList, &dirName) ){
      const char *zDir = blob_str(&dirName);
      /* Make full pathname of the directory */
      Blob path;
      const char *zPath;

      blob_zero(&path);
      blob_appendf(&path, "%s/%s", g.zLocalRoot, zDir);
      zPath = blob_str(&path);      
      /* Handle various cases of existence of the directory */
      switch( file_wd_isdir(zPath) ){
        case 0: { /* doesn't exist */
          if( file_mkdir(zPath, 0)!=0 ) {
            vcs_warning("couldn't create directory %s as "
                           "required by empty-dirs setting", zDir);
          }          
          break;
        }
        case 1: { /* exists, and is a directory */
          /* do nothing - required directory exists already */
          break;
        }
        case 2: { /* exists, but isn't a directory */
          vcs_warning("file %s found, but a directory is required "
                         "by empty-dirs setting", zDir);          
        }
      }
      blob_reset(&path);
    }
  }
}


/*
** Get the contents of a file within the checking "revision".  If
** revision==NULL then get the file content for the current checkout.
*/
int historical_version_of_file(
  const char *revision,    /* The checkin containing the file */
  const char *file,        /* Full treename of the file */
  Blob *content,           /* Put the content here */
  int *pIsLink,             /* Set to true if file is link. */
  int *pIsExe,             /* Set to true if file is executable */
  int errCode              /* Error code if file not found.  Panic if 0. */
){
  Manifest *pManifest;
  ManifestFile *pFile;
  int rid=0;
  
  if( revision ){
    rid = name_to_typed_rid(revision,"ci");
  }else{
    rid = db_lget_int("checkout", 0);
  }
  if( !is_a_version(rid) ){
    if( errCode>0 ) return errCode;
    vcs_fatal("no such checkin: %s", revision);
  }
  pManifest = manifest_get(rid, CFTYPE_MANIFEST);
  
  if( pManifest ){
    pFile = manifest_file_find(pManifest, file);
    if( pFile ){
      rid = uuid_to_rid(pFile->zUuid, 0);
      if( pIsExe ) *pIsExe = ( manifest_file_mperm(pFile)==PERM_EXE );
      if( pIsLink ) *pIsLink = ( manifest_file_mperm(pFile)==PERM_LNK );
      manifest_destroy(pManifest);
      return content_get(rid, content);
    }
    manifest_destroy(pManifest);
    if( errCode<=0 ){
      vcs_fatal("file %s does not exist in checkin: %s", file, revision);
    }
  }else if( errCode<=0 ){
    if( revision==0 ){
      revision = db_text("current", "SELECT uuid FROM blob WHERE rid=%d", rid);
    }
    vcs_panic("could not parse manifest for checkin: %s", revision);
  }
  return errCode;
}


/*
** COMMAND: revert
**
** Usage: %vcs revert ?-r REVISION? ?FILE ...?
**
** Revert to the current repository version of FILE, or to
** the version associated with baseline REVISION if the -r flag
** appears.
**
*/
void revert_cmd(void){
  const char *zFile;
  const char *zRevision;
  Blob record;
  int i;
  int errCode;
  Stmt q;

  undo_capture_command_line();  
  zRevision = find_option("revision", "r", 1);
  verify_all_options();
  
  if( g.argc<2 ){
    usage("?OPTIONS? [FILE] ...");
  }
  if( zRevision && g.argc<3 ){
    vcs_fatal("the --revision option does not work for the entire tree");
  }
  db_must_be_within_tree();
  db_begin_transaction();
  undo_begin();
  db_multi_exec("CREATE TEMP TABLE torevert(name UNIQUE);");

  if( g.argc>2 ){
    for(i=2; i<g.argc; i++){
      Blob fname;
      zFile = mprintf("%/", g.argv[i]);
      file_tree_name(zFile, &fname, 1);
      db_multi_exec("REPLACE INTO torevert VALUES(%B)", &fname);
      blob_reset(&fname);
    }
  }else{
    int vid;
    vid = db_lget_int("checkout", 0);
    vfile_check_signature(vid, 0, 0);
    db_multi_exec(
      "DELETE FROM vmerge;"
      "INSERT INTO torevert "
      "SELECT pathname"
      "  FROM vfile "
      " WHERE chnged OR deleted OR rid=0 OR pathname!=origname;"
    );
  }
  blob_zero(&record);
  db_prepare(&q, "SELECT name FROM torevert");
  if( zRevision==0 ){
    int vid = db_lget_int("checkout", 0);
    zRevision = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
  }
  while( db_step(&q)==SQLITE_ROW ){
    int isExe = 0;
    int isLink = 0;
    char *zFull;
    zFile = db_column_text(&q, 0);
    zFull = mprintf("%/%/", g.zLocalRoot, zFile);
    errCode = historical_version_of_file(zRevision, zFile, &record,
                                         &isLink, &isExe,2);
    if( errCode==2 ){
      if( db_int(0, "SELECT rid FROM vfile WHERE pathname=%Q", zFile)==0 ){
        vcs_print("UNMANAGE: %s\n", zFile);
      }else{
        undo_save(zFile);
        file_delete(zFull);
        vcs_print("DELETE: %s\n", zFile);
      }
      db_multi_exec("DELETE FROM vfile WHERE pathname=%Q", zFile);
    }else{
      sqlite3_int64 mtime;
      undo_save(zFile);
      if( file_wd_size(zFull)>=0 && (isLink || file_wd_islink(zFull)) ){
        file_delete(zFull);
      }
      if( isLink ){
        symlink_create(blob_str(&record), zFull);
      }else{
        blob_write_to_file(&record, zFull);
      }
      file_wd_setexe(zFull, isExe);
      vcs_print("REVERTED: %s\n", zFile);
      mtime = file_wd_mtime(zFull);
      db_multi_exec(
         "UPDATE vfile"
         "   SET mtime=%lld, chnged=0, deleted=0, isexe=%d, islink=%d,mrid=rid,"
         "       pathname=coalesce(origname,pathname), origname=NULL"     
         " WHERE pathname=%Q",
         mtime, isExe, isLink, zFile
      );
    }
    blob_reset(&record);
    free(zFull);
  }
  db_finalize(&q);
  undo_finish();
  db_end_transaction(0);
}
