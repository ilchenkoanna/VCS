#include "config.h"
#include "branch.h"
#include <assert.h>

/*
**  vcs branch new    NAME BASIS ?OPTIONS?
**  argv0  argv1  argv2  argv3 argv4
*/
void branch_new(void){
  int rootid;            /* RID of the root check-in - what we branch off of */
  int brid;              /* RID of the branch check-in */
  int noSign;            /* True if the branch is unsigned */
  int i;                 /* Loop counter */
  char *zUuid;           /* Artifact ID of origin */
  Stmt q;                /* Generic query */
  const char *zBranch;   /* Name of the new branch */
  char *zDate;           /* Date that branch was created */
  char *zComment;        /* Check-in comment for the new branch */
  const char *zColor;    /* Color of the new branch */
  Blob branch;           /* manifest for the new branch */
  Manifest *pParent;     /* Parsed parent manifest */
  Blob mcksum;           /* Self-checksum on the manifest */
  const char *zDateOvrd; /* Override date string */
  const char *zUserOvrd; /* Override user name */
  int isPrivate = 0;     /* True if the branch should be private */
 
  noSign = find_option("nosign","",0)!=0;
  zColor = find_option("bgcolor","c",1);
  isPrivate = find_option("private",0,0)!=0;
  zDateOvrd = find_option("date-override",0,1);
  zUserOvrd = find_option("user-override",0,1);
  verify_all_options();
  if( g.argc<5 ){
    usage("new BRANCH-NAME BASIS ?OPTIONS?");
  }
  db_find_and_open_repository(0, 0);  
  noSign = db_get_int("omitsign", 0)|noSign;
  
  /* vcs branch new name */
  zBranch = g.argv[3];
  if( zBranch==0 || zBranch[0]==0 ){
    vcs_panic("branch name cannot be empty");
  }
  if( db_exists(
        "SELECT 1 FROM tagxref"
        " WHERE tagtype>0"
        "   AND tagid=(SELECT tagid FROM tag WHERE tagname='sym-%s')",
        zBranch)!=0 ){
    vcs_fatal("branch \"%s\" already exists", zBranch);
  }

  user_select();
  db_begin_transaction();
  rootid = name_to_typed_rid(g.argv[4], "ci");
  if( rootid==0 ){
    vcs_fatal("unable to locate check-in off of which to branch");
  }

  pParent = manifest_get(rootid, CFTYPE_MANIFEST);
  if( pParent==0 ){
    vcs_fatal("%s is not a valid check-in", g.argv[4]);
  }

  /* Create a manifest for the new branch */
  blob_zero(&branch);
  if( pParent->zBaseline ){
    blob_appendf(&branch, "B %s\n", pParent->zBaseline);
  }
  zComment = mprintf("Create new branch named \"%h\"", zBranch);
  blob_appendf(&branch, "C %F\n", zComment);
  zDate = date_in_standard_format(zDateOvrd ? zDateOvrd : "now");
  blob_appendf(&branch, "D %s\n", zDate);

  /* Copy all of the content from the parent into the branch */
  for(i=0; i<pParent->nFile; ++i){
    blob_appendf(&branch, "F %F", pParent->aFile[i].zName);
    if( pParent->aFile[i].zUuid ){
      blob_appendf(&branch, " %s", pParent->aFile[i].zUuid);
      if( pParent->aFile[i].zPerm && pParent->aFile[i].zPerm[0] ){
        blob_appendf(&branch, " %s", pParent->aFile[i].zPerm);
      }
    }
    blob_append(&branch, "\n", 1);
  }
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rootid);
  blob_appendf(&branch, "P %s\n", zUuid);
  if( pParent->zRepoCksum ){
    blob_appendf(&branch, "R %s\n", pParent->zRepoCksum);
  }
  manifest_destroy(pParent);

  /* Add the symbolic branch name and the "branch" tag to identify
  ** this as a new branch */
  if( content_is_private(rootid) ) isPrivate = 1;
  if( isPrivate && zColor==0 ) zColor = "#fec084";
  if( zColor!=0 ){
    blob_appendf(&branch, "T *bgcolor * %F\n", zColor);
  }
  blob_appendf(&branch, "T *branch * %F\n", zBranch);
  blob_appendf(&branch, "T *sym-%F *\n", zBranch);
  if( isPrivate ){
    blob_appendf(&branch, "T +private *\n");
    noSign = 1;
  }

  /* Cancel all other symbolic tags */
  db_prepare(&q,
      "SELECT tagname FROM tagxref, tag"
      " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
      "   AND tagtype>0 AND tagname GLOB 'sym-*'"
      " ORDER BY tagname",
      rootid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTag = db_column_text(&q, 0);
    blob_appendf(&branch, "T -%F *\n", zTag);
  }
  db_finalize(&q);
  
  blob_appendf(&branch, "U %F\n", zUserOvrd ? zUserOvrd : g.zLogin);
  md5sum_blob(&branch, &mcksum);
  blob_appendf(&branch, "Z %b\n", &mcksum);
  if( !noSign && clearsign(&branch, &branch) ){
    Blob ans;
    blob_zero(&ans);
    prompt_user("unable to sign manifest.  continue (y/N)? ", &ans);
    if( blob_str(&ans)[0]!='y' ){
      db_end_transaction(1);
      vcs_exit(1);
    }
  }

  brid = content_put_ex(&branch, 0, 0, 0, isPrivate);
  if( brid==0 ){
    vcs_panic("trouble committing manifest: %s", g.zErrMsg);
  }
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", brid);
  if( manifest_crosslink(brid, &branch)==0 ){
    vcs_panic("unable to install new manifest");
  }
  assert( blob_is_reset(&branch) );
  content_deltify(rootid, brid, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", brid);
  vcs_print("New branch: %s\n", zUuid);
  if( g.argc==3 ){
    vcs_print(
      "\n"
      "Note: the local check-out has not been updated to the new\n"
      "      branch.  To begin working on the new branch, do this:\n"
      "\n"
      "      %s update %s\n",
      vcs_nameofexe(), zBranch
    );
  }


  /* Commit */
  db_end_transaction(0);
  
  /* Do an autosync push, if requested */
  if( !isPrivate ) autosync(AUTOSYNC_PUSH);
}

/*
** Prepare a query that will list branches.
**
** If (which<0) then the query pulls only closed branches. If
** (which>0) then the query pulls all (closed and opened)
** branches. Else the query pulls currently-opened branches.
*/
void branch_prepare_list_query(Stmt *pQuery, int which ){
  if( which < 0 ){
    db_prepare(pQuery,
      "SELECT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL "
      "EXCEPT "
      "SELECT value FROM tagxref"
      " WHERE tagid=%d"
      "   AND rid IN leaf"
      "   AND NOT %z"
      " ORDER BY value COLLATE nocase /*sort*/",
      TAG_BRANCH, TAG_BRANCH, leaf_is_closed_sql("tagxref.rid")
    );
  }else if( which>0 ){
    db_prepare(pQuery,
      "SELECT DISTINCT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      "   AND rid IN leaf"
      " ORDER BY value COLLATE nocase /*sort*/",
      TAG_BRANCH
    );
  }else{
    db_prepare(pQuery,
      "SELECT DISTINCT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      "   AND rid IN leaf"
      "   AND NOT %z"
      " ORDER BY value COLLATE nocase /*sort*/",
      TAG_BRANCH, leaf_is_closed_sql("tagxref.rid")
    );
  }
}


/*
** COMMAND: branch
**
** Usage: %vcs branch SUBCOMMAND ... ?OPTIONS?
**
*/
void branch_cmd(void){
  int n;
  const char *zCmd = "list";
  db_find_and_open_repository(0, 0);
  if( g.argc<2 ){
    usage("new|list|ls ...");
  }
  if( g.argc>=3 ) zCmd = g.argv[2];
  n = strlen(zCmd);
  if( strncmp(zCmd,"new",n)==0 ){
    branch_new();
  }else if( (strncmp(zCmd,"list",n)==0)||(strncmp(zCmd, "ls", n)==0) ){
    Stmt q;
    int vid;
    char *zCurrent = 0;
    int showAll = find_option("all",0,0)!=0;
    int showClosed = find_option("closed",0,0)!=0;

    if( g.localOpen ){
      vid = db_lget_int("checkout", 0);
      zCurrent = db_text(0, "SELECT value FROM tagxref"
                            " WHERE rid=%d AND tagid=%d", vid, TAG_BRANCH);
    }
    branch_prepare_list_query(&q, showAll?1:(showClosed?-1:0));
    while( db_step(&q)==SQLITE_ROW ){
      const char *zBr = db_column_text(&q, 0);
      int isCur = zCurrent!=0 && vcs_strcmp(zCurrent,zBr)==0;
      vcs_print("%s%s\n", (isCur ? "* " : "  "), zBr);
    }
    db_finalize(&q);
  }else{
    vcs_panic("branch subcommand should be one of: "
                 "new list ls");
  }
}
