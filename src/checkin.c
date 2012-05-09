/*
** This file contains code used to check-in versions of the project
** from the local repository.
*/
#include "config.h"
#include "checkin.h"
#include <assert.h>

/*
** Generate text describing all changes.  Prepend zPrefix to each line
** of output.
**
** We assume that vfile_check_signature has been run.
**
** If missingIsFatal is true, then any files that are missing or which
** are not true files results in a fatal error.
*/
static void status_report(
  Blob *report,          /* Append the status report here */
  const char *zPrefix,   /* Prefix on each line of the report */
  int missingIsFatal,    /* MISSING and NOT_A_FILE are fatal errors */
  int cwdRelative        /* Report relative to the current working dir */ 
){
  Stmt q;
  int nPrefix = strlen(zPrefix);
  int nErr = 0;
  Blob rewrittenPathname;
  db_prepare(&q, 
    "SELECT pathname, deleted, chnged, rid, coalesce(origname!=pathname,0)"
    "  FROM vfile "
    " WHERE file_is_selected(id)"
    "   AND (chnged OR deleted OR rid=0 OR pathname!=origname) ORDER BY 1"
  );
  blob_zero(&rewrittenPathname);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    const char *zDisplayName = zPathname;
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3)==0;
    int isRenamed = db_column_int(&q,4);
    char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    if( cwdRelative ){
      file_relative_name(zFullName, &rewrittenPathname, 0);
      zDisplayName = blob_str(&rewrittenPathname);
      if( zDisplayName[0]=='.' && zDisplayName[1]=='/' ){
        zDisplayName += 2;  /* no unnecessary ./ prefix */
      }
    }
    blob_append(report, zPrefix, nPrefix);
    if( isDeleted ){
      blob_appendf(report, "DELETED    %s\n", zDisplayName);
    }else if( !file_wd_isfile_or_link(zFullName) ){
      if( file_access(zFullName, 0)==0 ){
        blob_appendf(report, "NOT_A_FILE %s\n", zDisplayName);
        if( missingIsFatal ){
          vcs_warning("not a file: %s", zDisplayName);
          nErr++;
        }
      }else{
        blob_appendf(report, "MISSING    %s\n", zDisplayName);
        if( missingIsFatal ){
          vcs_warning("missing file: %s", zDisplayName);
          nErr++;
        }
      }
    }else if( isNew ){
      blob_appendf(report, "ADDED      %s\n", zDisplayName);
    }else if( isDeleted ){
      blob_appendf(report, "DELETED    %s\n", zDisplayName);
    }else if( isChnged==2 ){
      blob_appendf(report, "UPDATED_BY_MERGE %s\n", zDisplayName);
    }else if( isChnged==3 ){
      blob_appendf(report, "ADDED_BY_MERGE %s\n", zDisplayName);
    }else if( isChnged==1 ){
      blob_appendf(report, "EDITED     %s\n", zDisplayName);
    }else if( isRenamed ){
      blob_appendf(report, "RENAMED    %s\n", zDisplayName);
    }
    free(zFullName);
  }
  blob_reset(&rewrittenPathname);
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid, id FROM vmerge JOIN blob ON merge=rid"
                 " WHERE id<=0");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zLabel = "MERGED_WITH";
    switch( db_column_int(&q, 1) ){
      case -1:  zLabel = "CHERRYPICK ";  break;
      case -2:  zLabel = "BACKOUT    ";  break;
    }
    blob_append(report, zPrefix, nPrefix);
    blob_appendf(report, "%s %s\n", zLabel, db_column_text(&q, 0));
  }
  db_finalize(&q);
  if( nErr ){
    vcs_fatal("aborting due to prior errors");
  }
}

static int determine_cwd_relative_option()
{
  int relativePaths = db_get_boolean("relative-paths", 1);
  int absPathOption = find_option("abs-paths", 0, 0)!=0;
  int relPathOption = find_option("rel-paths", 0, 0)!=0;
  if( absPathOption ){ relativePaths = 0; }
  if( relPathOption ){ relativePaths = 1; }
  return relativePaths;
}

/*
** COMMAND: changes
**
** Usage: %vcs changes ?OPTIONS?
**
** Report on the edit status of all files in the current checkout.
*/
void changes_cmd(void){
  Blob report;
  int vid;
  int useSha1sum = find_option("sha1sum", 0, 0)!=0;
  int showHdr = find_option("header",0,0)!=0;
  int verbose = find_option("verbose","v",0)!=0;
  int cwdRelative = 0;
  db_must_be_within_tree();
  cwdRelative = determine_cwd_relative_option();
  blob_zero(&report);
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid, 0, useSha1sum);
  status_report(&report, "", 0, cwdRelative);
  if( verbose && blob_size(&report)==0 ){
    blob_append(&report, "  (none)\n", -1);
  }
  if( showHdr && blob_size(&report)>0 ){
    vcs_print("Changes for %s at %s:\n", db_get("project-name","???"),
                 g.zLocalRoot);
  }
  blob_write_to_file(&report, "-");
}

/*
** COMMAND: status
**
** Usage: %vcs status ?OPTIONS?
**
** Report on the status of the current checkout.
*/
void status_cmd(void){
  int vid;
  db_must_be_within_tree();
       /* 012345678901234 */
  vcs_print("repository:   %s\n", db_repository_filename());
  vcs_print("local-root:   %s\n", g.zLocalRoot);
  vid = db_lget_int("checkout", 0);
  if( vid ){
    show_common_info(vid, "checkout:", 1, 1);
  }
  db_record_repository_filename(0);
  changes_cmd();
}

/*
** COMMAND: ls
**
** Usage: %vcs ls ?OPTIONS?
**
** Show the names of all files in the current checkout.  The -l provides
** extra information about each file.
*/
void ls_cmd(void){
  int vid;
  Stmt q;
  int isBrief;

  isBrief = find_option("l","l", 0)==0;
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid, 0, 0);
  db_prepare(&q,
     "SELECT pathname, deleted, rid, chnged, coalesce(origname!=pathname,0)"
     "  FROM vfile"
     " ORDER BY 1"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isNew = db_column_int(&q,2)==0;
    int chnged = db_column_int(&q,3);
    int renamed = db_column_int(&q,4);
    char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    if( isBrief ){
      vcs_print("%s\n", zPathname);
    }else if( isNew ){
      vcs_print("ADDED      %s\n", zPathname);
    }else if( isDeleted ){
      vcs_print("DELETED    %s\n", zPathname);
    }else if( !file_wd_isfile_or_link(zFullName) ){
      if( file_access(zFullName, 0)==0 ){
        vcs_print("NOT_A_FILE %s\n", zPathname);
      }else{
        vcs_print("MISSING    %s\n", zPathname);
      }
    }else if( chnged ){
      vcs_print("EDITED     %s\n", zPathname);
    }else if( renamed ){
      vcs_print("RENAMED    %s\n", zPathname);
    }else{
      vcs_print("UNCHANGED  %s\n", zPathname);
    }
    free(zFullName);
  }
  db_finalize(&q);
}

/*
** Prepare a commit comment.  Let the user modify it using the
** editor specified in the global_config table or either
** the VISUAL or EDITOR environment variable.
**
** Store the final commit comment in pComment.  pComment is assumed
** to be uninitialized - any prior content is overwritten.
**
** zInit is the text of the most recent failed attempt to check in
** this same change.  Use zInit to reinitialize the check-in comment
** so that the user does not have to retype.
**
** zBranch is the name of a new branch that this check-in is forced into.
** zBranch might be NULL or an empty string if no forcing occurs.
**
** parent_rid is the recordid of the parent check-in.
*/
static void prepare_commit_comment(
  Blob *pComment,
  char *zInit,
  const char *zBranch,
  int parent_rid,
  const char *zUserOvrd
){
  const char *zEditor;
  char *zCmd;
  char *zFile;
  Blob text, line;
  char *zComment;
  int i;
  blob_init(&text, zInit, -1);
  blob_append(&text,
    "\n"
    "# Enter comments on this check-in.  Lines beginning with # are ignored.\n"
    "# The check-in comment follows wiki formatting rules.\n"
    "#\n", -1
  );
  blob_appendf(&text, "# user: %s\n", zUserOvrd ? zUserOvrd : g.zLogin);
  if( zBranch && zBranch[0] ){
    blob_appendf(&text, "# tags: %s\n#\n", zBranch);
  }else{
    char *zTags = info_tags_of_checkin(parent_rid, 1);
    if( zTags )  blob_appendf(&text, "# tags: %z\n#\n", zTags);
  }
  if( g.markPrivate ){
    blob_append(&text,
      "# PRIVATE BRANCH: This check-in will be private and will not sync to\n"
      "# repositories.\n"
      "#\n", -1
    );
  }
  status_report(&text, "# ", 1, 0);
  zEditor = db_get("editor", 0);
  if( zEditor==0 ){
    zEditor = vcs_getenv("VISUAL");
  }
  if( zEditor==0 ){
    zEditor = vcs_getenv("EDITOR");
  }
  if( zEditor==0 ){
    blob_append(&text,
       "#\n"
       "# Since no default text editor is set using EDITOR or VISUAL\n"
       "# environment variables or the \"vcs set editor\" command,\n"
       "# and because no check-in comment was specified using the \"-m\"\n"
       "# or \"-M\" command-line options, you will need to enter the\n"
       "# check-in comment below.  Type \".\" on a line by itself when\n"
       "# you are done:\n", -1);
    zFile = mprintf("-");
  }else{
    zFile = db_text(0, "SELECT '%qci-comment-' || hex(randomblob(6)) || '.txt'",
                    g.zLocalRoot);
  }
#if defined(_WIN32)
  blob_add_cr(&text);
#endif
  blob_write_to_file(&text, zFile);
  if( zEditor ){
    zCmd = mprintf("%s \"%s\"", zEditor, zFile);
    vcs_print("%s\n", zCmd);
    if( vcs_system(zCmd) ){
      vcs_panic("editor aborted");
    }
    blob_reset(&text);
    blob_read_from_file(&text, zFile);
  }else{
    char zIn[300];
    blob_reset(&text);
    while( fgets(zIn, sizeof(zIn), stdin)!=0 ){
      char *zUtf8 = vcs_mbcs_to_utf8(zIn);
      if( zUtf8[0]=='.' && (zUtf8[1]==0 || zUtf8[1]=='\r' || zUtf8[1]=='\n') ){
        vcs_mbcs_free(zUtf8);
        break;
      }
      blob_append(&text, zIn, -1);
      vcs_mbcs_free(zUtf8);
    }
  }
  blob_remove_cr(&text);
  file_delete(zFile);
  free(zFile);
  blob_zero(pComment);
  while( blob_line(&text, &line) ){
    int i, n;
    char *z;
    n = blob_size(&line);
    z = blob_buffer(&line);
    for(i=0; i<n && vcs_isspace(z[i]);  i++){}
    if( i<n && z[i]=='#' ) continue;
    if( i<n || blob_size(pComment)>0 ){
      blob_appendf(pComment, "%b", &line);
    }
  }
  blob_reset(&text);
  zComment = blob_str(pComment);
  i = strlen(zComment);
  while( i>0 && vcs_isspace(zComment[i-1]) ){ i--; }
  blob_resize(pComment, i);
}

/*
** Populate the Global.aCommitFile[] based on the command line arguments
** to a [commit] command. Global.aCommitFile is an array of integers
** sized at (N+1), where N is the number of arguments passed to [commit].
** The contents are the [id] values from the vfile table corresponding
** to the filenames passed as arguments.
**
** The last element of aCommitFile[] is always 0 - indicating the end
** of the array.
**
** If there were no arguments passed to [commit], aCommitFile is not
** allocated and remains NULL. Other parts of the code interpret this
** to mean "all files".
*/
void select_commit_files(void){
  if( g.argc>2 ){
    int ii;
    Blob b;
    blob_zero(&b);
    g.aCommitFile = vcs_malloc(sizeof(int)*(g.argc-1));

    for(ii=2; ii<g.argc; ii++){
      int iId;
      file_tree_name(g.argv[ii], &b, 1);
      iId = db_int(-1, "SELECT id FROM vfile WHERE pathname=%Q", blob_str(&b));
      if( iId<0 ){
        vcs_fatal("vcs knows nothing about: %s", g.argv[ii]);
      }
      g.aCommitFile[ii-2] = iId;
      blob_reset(&b);
    }
    g.aCommitFile[ii-2] = 0;
  }
}

/*
** Make sure the current check-in with timestamp zDate is younger than its
** ancestor identified rid and zUuid.  Throw a fatal error if not.
*/
static void checkin_verify_younger(
  int rid,              /* The record ID of the ancestor */
  const char *zUuid,    /* The artifact ID of the ancestor */
  const char *zDate     /* Date & time of the current check-in */
){
#ifndef vcs_ALLOW_OUT_OF_ORDER_DATES
  int b;
  b = db_exists(
    "SELECT 1 FROM event"
    " WHERE datetime(mtime)>=%Q"
    "   AND type='ci' AND objid=%d",
    zDate, rid
  );
  if( b ){
    vcs_fatal("ancestor check-in [%.10s] (%s) is not older (clock skew?)"
                 " Use -f to override.", zUuid, zDate);
  }
#endif
}

/*
** zDate should be a valid date string.  Convert this string into the
** format YYYY-MM-DDTHH:MM:SS.  If the string is not a valid date, 
** print a fatal error and quit.
*/
char *date_in_standard_format(const char *zInputDate){
  char *zDate;
  if( g.perm.Setup && vcs_strcmp(zInputDate,"now")==0 ){
    zInputDate = PD("date_override","now");
  }
  zDate = db_text(0, "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%f',%Q)",
                  zInputDate);
  if( zDate[0]==0 ){
    vcs_fatal(
      "unrecognized date format (%s): use \"YYYY-MM-DD HH:MM:SS.SSS\"",
      zInputDate
    );
  }
  return zDate;
}

/*
** qsort() comparison routine for an array of pointers to strings.
*/
static int tagCmp(const void *a, const void *b){
  char **pA = (char**)a;
  char **pB = (char**)b;
  return vcs_strcmp(pA[0], pB[0]);
}

/*
** COMMAND: ci*
** COMMAND: commit
**
** Usage: %vcs commit ?OPTIONS? ?FILE...?
*/
void commit_cmd(void){
  int hasChanges;        /* True if unsaved changes exist */
  int vid;               /* blob-id of parent version */
  int nrid;              /* blob-id of a modified file */
  int nvid;              /* Blob-id of the new check-in */
  Blob comment;          /* Check-in comment */
  const char *zComment;  /* Check-in comment */
  Stmt q;                /* Query to find files that have been modified */
  char *zUuid;           /* UUID of the new check-in */
  int noSign = 0;        /* True to omit signing the manifest using GPG */
  int isAMerge = 0;      /* True if checking in a merge */
  int forceFlag = 0;     /* Force a fork */
  int forceDelta = 0;    /* Force a delta-manifest */
  int forceBaseline = 0; /* Force a baseline-manifest */
  char *zManifestFile;   /* Name of the manifest file */
  int useCksum;          /* True if checksums should be computed and verified */
  int outputManifest;    /* True to output "manifest" and "manifest.uuid" */
  int testRun;           /* True for a test run.  Debugging only */
  const char *zBranch;   /* Create a new branch with this name */
  const char *zBrClr;    /* Set background color when branching */
  const char *zColor;    /* One-time check-in color */
  const char *zDateOvrd; /* Override date string */
  const char *zUserOvrd; /* Override user name */
  const char *zComFile;  /* Read commit message from this file */
  int nTag = 0;          /* Number of --tag arguments */
  const char *zTag;      /* A single --tag argument */
  const char **azTag = 0;/* Array of all --tag arguments */
  Blob manifest;         /* Manifest in baseline form */
  Blob muuid;            /* Manifest uuid */
  Blob cksum1, cksum2;   /* Before and after commit checksums */
  Blob cksum1b;          /* Checksum recorded in the manifest */
  int szD;               /* Size of the delta manifest */
  int szB;               /* Size of the baseline manifest */
 
  url_proxy_options();
  noSign = find_option("nosign",0,0)!=0;
  forceDelta = find_option("delta",0,0)!=0;
  forceBaseline = find_option("baseline",0,0)!=0;
  if( forceDelta && forceBaseline ){
    vcs_fatal("cannot use --delta and --baseline together");
  }
  testRun = find_option("test",0,0)!=0;
  zComment = find_option("comment","m",1);
  forceFlag = find_option("force", "f", 0)!=0;
  zBranch = find_option("branch","b",1);
  zColor = find_option("bgcolor",0,1);
  zBrClr = find_option("branchcolor",0,1);
  while( (zTag = find_option("tag",0,1))!=0 ){
    if( zTag[0]==0 ) continue;
    azTag = vcs_realloc(azTag, sizeof(char*)*(nTag+2));
    azTag[nTag++] = zTag;
    azTag[nTag] = 0;
  }
  zComFile = find_option("message-file", "M", 1);
  if( find_option("private",0,0) ){
    g.markPrivate = 1;
    if( zBranch==0 ) zBranch = "private";
    if( zBrClr==0 && zColor==0 ) zBrClr = "#fec084";  /* Orange */
  }
  zDateOvrd = find_option("date-override",0,1);
  zUserOvrd = find_option("user-override",0,1);
  db_must_be_within_tree();
  noSign = db_get_boolean("omitsign", 0)|noSign;
  if( db_get_boolean("clearsign", 0)==0 ){ noSign = 1; }
  useCksum = db_get_boolean("repo-cksum", 1);
  outputManifest = db_get_boolean("manifest", 0);
  verify_all_options();

  /* Escape special characters in tags and put all tags in sorted order */
  if( nTag ){
    int i;
    for(i=0; i<nTag; i++) azTag[i] = mprintf("%F", azTag[i]);
    qsort((void*)azTag, nTag, sizeof(azTag[0]), tagCmp);
  }