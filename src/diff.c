/*
** This file contains code used to compute a "diff" between two
** text files.
*/
#include "config.h"
#include "diff.h"
#include <assert.h>


#if INTERFACE
/*
** Allowed flag parameters to the text_diff() and html_sbsdiff() funtions:
*/
#define DIFF_CONTEXT_MASK  0x0000ffff  /* Lines of context.  Default if 0 */
#define DIFF_WIDTH_MASK    0x00ff0000  /* side-by-side column width */
#define DIFF_IGNORE_EOLWS  0x01000000  /* Ignore end-of-line whitespace */
#define DIFF_SIDEBYSIDE    0x02000000  /* Generate a side-by-side diff */
#define DIFF_NEWFILE       0x04000000  /* Missing files are as empty files */
#define DIFF_BRIEF         0x08000000  /* Show filenames only */
#define DIFF_INLINE        0x00000000  /* Inline (not side-by-side) diff */
#define DIFF_HTML          0x10000000  /* Render for HTML */
#define DIFF_LINENO        0x20000000  /* Show line numbers in context diff */
#define DIFF_NOOPT         0x40000000  /* Suppress optimizations for debug */
#define DIFF_INVERT        0x80000000  /* Invert the diff for debug */

#endif /* INTERFACE */

/*
** Maximum length of a line in a text file.  (8192)
*/
#define LENGTH_MASK_SZ  13
#define LENGTH_MASK     ((1<<LENGTH_MASK_SZ)-1)

/*
** Information about each line of a file being diffed.
**
** The lower LENGTH_MASK_SZ bits of the hash (DLine.h) are the length
** of the line.  If any line is longer than LENGTH_MASK characters,
** the file is considered binary.
*/
typedef struct DLine DLine;
struct DLine {
  const char *z;        /* The text of the line */
  unsigned int h;       /* Hash of the line */
  unsigned int iNext;   /* 1+(Index of next line with same the same hash) */

  /* an array of DLine elements services two purposes.  The fields
  ** above are one per line of input text.  But each entry is also
  ** a bucket in a hash table, as follows: */
  unsigned int iHash;   /* 1+(first entry in the hash chain) */
};

/*
** Length of a dline
*/
#define LENGTH(X)   ((X)->h & LENGTH_MASK)

/*
** A context for running a raw diff.
**
** The aEdit[] array describes the raw diff.  Each triple of integers in
** aEdit[] means:
**
**   (1) COPY:   Number of lines aFrom and aTo have in common
**   (2) DELETE: Number of lines found only in aFrom
**   (3) INSERT: Number of lines found only in aTo
**
** The triples repeat until all lines of both aFrom and aTo are accounted
** for.
*/
typedef struct DContext DContext;
struct DContext {
  int *aEdit;        /* Array of copy/delete/insert triples */
  int nEdit;         /* Number of integers (3x num of triples) in aEdit[] */
  int nEditAlloc;    /* Space allocated for aEdit[] */
  DLine *aFrom;      /* File on left side of the diff */
  int nFrom;         /* Number of lines in aFrom[] */
  DLine *aTo;        /* File on right side of the diff */
  int nTo;           /* Number of lines in aTo[] */
};

/*
** Return 0 if the file is binary or contains a line that is
** too long.
*/
static DLine *break_into_lines(const char *z, int n, int *pnLine, int ignoreWS){
  int nLine, i, j, k, x;
  unsigned int h, h2;
  DLine *a;

  /* Count the number of lines.  Allocate space to hold
  ** the returned array.
  */
  for(i=j=0, nLine=1; i<n; i++, j++){
    int c = z[i];
    if( c==0 ){
      return 0;
    }
    if( c=='\n' && z[i+1]!=0 ){
      nLine++;
      if( j>LENGTH_MASK ){
        return 0;
      }
      j = 0;
    }
  }
  if( j>LENGTH_MASK ){
    return 0;
  }
  a = vcs_malloc( nLine*sizeof(a[0]) );
  memset(a, 0, nLine*sizeof(a[0]) );
  if( n==0 ){
    *pnLine = 0;
    return a;
  }

  /* Fill in the array */
  for(i=0; i<nLine; i++){
    a[i].z = z;
    for(j=0; z[j] && z[j]!='\n'; j++){}
    k = j;
    while( ignoreWS && k>0 && vcs_isspace(z[k-1]) ){ k--; }
    for(h=0, x=0; x<k; x++){
      h = h ^ (h<<2) ^ z[x];
    }
    a[i].h = h = (h<<LENGTH_MASK_SZ) | k;;
    h2 = h % nLine;
    a[i].iNext = a[h2].iHash;
    a[h2].iHash = i+1;
    z += j+1;
  }

  /* Return results */
  *pnLine = nLine;
  return a;
}

/*
** Return true if two DLine elements are identical.
*/
static int same_dline(DLine *pA, DLine *pB){
  return pA->h==pB->h && memcmp(pA->z,pB->z,pA->h & LENGTH_MASK)==0;
}

/*
** Append a single line of context-diff output to pOut.
*/
static void appendDiffLine(
  Blob *pOut,         /* Where to write the line of output */
  char cPrefix,       /* One of " ", "+",  or "-" */
  DLine *pLine,       /* The line to be output */
  int html            /* True if generating HTML.  False for plain text */
){
  blob_append(pOut, &cPrefix, 1);
  if( html ){
    char *zHtml;
    if( cPrefix=='+' ){
      blob_append(pOut, "<span class=\"diffadd\">", -1);
    }else if( cPrefix=='-' ){
      blob_append(pOut, "<span class=\"diffrm\">", -1);
    }
    zHtml = htmlize(pLine->z, (pLine->h & LENGTH_MASK));
    blob_append(pOut, zHtml, -1);
    vcs_free(zHtml);
    if( cPrefix!=' ' ){
      blob_append(pOut, "</span>", -1);
    }
  }else{
    blob_append(pOut, pLine->z, pLine->h & LENGTH_MASK);
  }
  blob_append(pOut, "\n", 1);
}

/*
** Add two line numbers to the beginning of an output line for a context
** diff.  One or of the other of the two numbers might be zero, which means
** to leave that number field blank.  The "html" parameter means to format
** the output for HTML.  
*/
static void appendDiffLineno(Blob *pOut, int lnA, int lnB, int html){
  if( html ) blob_append(pOut, "<span class=\"diffln\">", -1);
  if( lnA>0 ){
    blob_appendf(pOut, "%6d ", lnA);
  }else{
    blob_append(pOut, "       ", 7);
  }
  if( lnB>0 ){
    blob_appendf(pOut, "%6d  ", lnB);
  }else{
    blob_append(pOut, "        ", 8);
  }
  if( html ) blob_append(pOut, "</span>", -1);
}


/*
** Given a diff context in which the aEdit[] array has been filled
** in, compute a context diff into pOut.
*/
static void contextDiff(
  DContext *p,      /* The difference */
  Blob *pOut,       /* Output a context diff to here */
  int nContext,     /* Number of lines of context */
  int showLn,       /* Show line numbers */
  int html          /* Render as HTML */
){
  DLine *A;     /* Left side of the diff */
  DLine *B;     /* Right side of the diff */  
  int a = 0;    /* Index of next line in A[] */
  int b = 0;    /* Index of next line in B[] */
  int *R;       /* Array of COPY/DELETE/INSERT triples */
  int r;        /* Index into R[] */
  int nr;       /* Number of COPY/DELETE/INSERT triples to process */
  int mxr;      /* Maximum value for r */
  int na, nb;   /* Number of lines shown from A and B */
  int i, j;     /* Loop counters */
  int m;        /* Number of lines to output */
  int skip;     /* Number of lines to skip */
  int nChunk = 0;  /* Number of diff chunks seen so far */

  A = p->aFrom;
  B = p->aTo;
  R = p->aEdit;
  mxr = p->nEdit;
  while( mxr>2 && R[mxr-1]==0 && R[mxr-2]==0 ){ mxr -= 3; }
  for(r=0; r<mxr; r += 3*nr){
    /* Figure out how many triples to show in a single block */
    for(nr=1; R[r+nr*3]>0 && R[r+nr*3]<nContext*2; nr++){}
    /* printf("r=%d nr=%d\n", r, nr); */

    /* For the current block comprising nr triples, figure out
    ** how many lines of A and B are to be displayed
    */
    if( R[r]>nContext ){
      na = nb = nContext;
      skip = R[r] - nContext;
    }else{
      na = nb = R[r];
      skip = 0;
    }
    for(i=0; i<nr; i++){
      na += R[r+i*3+1];
      nb += R[r+i*3+2];
    }
    if( R[r+nr*3]>nContext ){
      na += nContext;
      nb += nContext;
    }else{
      na += R[r+nr*3];
      nb += R[r+nr*3];
    }
    for(i=1; i<nr; i++){
      na += R[r+i*3];
      nb += R[r+i*3];
    }

    /* Show the header for this block, or if we are doing a modified
    ** context diff that contains line numbers, show the separate from
    ** the previous block.
    */
    nChunk++;
    if( showLn ){
      if( r==0 ){
        /* Do not show a top divider */
      }else if( html ){
        blob_appendf(pOut, "<span class=\"diffhr\">%.80c</span>\n", '.');
        blob_appendf(pOut, "<a name=\"chunk%d\"></a>\n", nChunk);
      }else{
        blob_appendf(pOut, "%.80c\n", '.');
      }
    }else{
      if( html ) blob_appendf(pOut, "<span class=\"diffln\">");
      /*
       * If the patch changes an empty file or results in an empty file,
       * the block header must use 0,0 as position indicator and not 1,0.
       * Otherwise, patch would be confused and may reject the diff.
       */
      blob_appendf(pOut,"@@ -%d,%d +%d,%d @@",
        na ? a+skip+1 : 0, na,
        nb ? b+skip+1 : 0, nb);
      if( html ) blob_appendf(pOut, "</span>");
      blob_append(pOut, "\n", 1);
    }

    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    for(j=0; j<m; j++){
      if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1, html);
      appendDiffLine(pOut, ' ', &A[a+j], html);
    }
    a += m;
    b += m;

    /* Show the differences */
    for(i=0; i<nr; i++){
      m = R[r+i*3+1];
      for(j=0; j<m; j++){
        if( showLn ) appendDiffLineno(pOut, a+j+1, 0, html);
        appendDiffLine(pOut, '-', &A[a+j], html);
      }
      a += m;
      m = R[r+i*3+2];
      for(j=0; j<m; j++){
        if( showLn ) appendDiffLineno(pOut, 0, b+j+1, html);
        appendDiffLine(pOut, '+', &B[b+j], html);
      }
      b += m;
      if( i<nr-1 ){
        m = R[r+i*3+3];
        for(j=0; j<m; j++){
          if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1, html);
          appendDiffLine(pOut, ' ', &B[b+j], html);
        }
        b += m;
        a += m;
      }
    }

    /* Show the final common area */
    assert( nr==i );
    m = R[r+nr*3];
    if( m>nContext ) m = nContext;
    for(j=0; j<m; j++){
      if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1, html);
      appendDiffLine(pOut, ' ', &B[b+j], html);
    }
  }
}

/*
** Status of a single output line
*/
typedef struct SbsLine SbsLine;
struct SbsLine {
  char *zLine;             /* The output line under construction */
  int n;                   /* Index of next unused slot in the zLine[] */
  int width;               /* Maximum width of a column in the output */
  unsigned char escHtml;   /* True to escape html characters */
  int iStart;              /* Write zStart prior to character iStart */
  const char *zStart;      /* A <span> tag */
  int iEnd;                /* Write </span> prior to character iEnd */
  int iStart2;             /* Write zStart2 prior to character iStart2 */
  const char *zStart2;     /* A <span> tag */
  int iEnd2;               /* Write </span> prior to character iEnd2 */
};

/*
** Flags for sbsWriteText()
*/
#define SBS_NEWLINE      0x0001   /* End with \n\000 */
#define SBS_PAD          0x0002   /* Pad output to width spaces */

/*
** Write up to width characters of pLine into z[].  Translate tabs into
** spaces.  Add a newline if SBS_NEWLINE is set.  Translate HTML characters
** if SBS_HTML is set.  Pad the rendering out width bytes if SBS_PAD is set.
*/
static void sbsWriteText(SbsLine *p, DLine *pLine, unsigned flags){
  int n = pLine->h & LENGTH_MASK;
  int i;   /* Number of input characters consumed */
  int j;   /* Number of output characters generated */
  int k;   /* Cursor position */
  int needEndSpan = 0;
  const char *zIn = pLine->z;
  char *z = &p->zLine[p->n];
  int w = p->width;
  for(i=j=k=0; k<w && i<n; i++, k++){
    char c = zIn[i];
    if( p->escHtml ){
      if( i==p->iStart ){
        int x = strlen(p->zStart);
        memcpy(z+j, p->zStart, x);
        j += x;
        needEndSpan = 1;
        if( p->iStart2 ){
          p->iStart = p->iStart2;
          p->zStart = p->zStart2;
          p->iStart2 = 0;
        }
      }else if( i==p->iEnd ){
        memcpy(z+j, "</span>", 7);
        j += 7;
        needEndSpan = 0;
        if( p->iEnd2 ){
          p->iEnd = p->iEnd2;
          p->iEnd2 = 0;
        }
      }
    }
    if( c=='\t' ){
      z[j++] = ' ';
      while( (k&7)!=7 && k<w ){ z[j++] = ' '; k++; }
    }else if( c=='\r' || c=='\f' ){
      z[j++] = ' ';
    }else if( c=='<' && p->escHtml ){
      memcpy(&z[j], "&lt;", 4);
      j += 4;
    }else if( c=='&' && p->escHtml ){
      memcpy(&z[j], "&amp;", 5);
      j += 5;
    }else if( c=='>' && p->escHtml ){
      memcpy(&z[j], "&gt;", 4);
      j += 4;
    }else{
      z[j++] = c;
    }
  }
  if( needEndSpan ){
    memcpy(&z[j], "</span>", 7);
    j += 7;
  }
  if( (flags & SBS_PAD)!=0 ){
    while( k<w ){ k++;  z[j++] = ' '; }
  }
  if( flags & SBS_NEWLINE ){
    z[j++] = '\n';
  }
  p->n += j;
}

/*
** Append a string to an SbSLine with coding, interpretation, or padding.
*/
static void sbsWrite(SbsLine *p, const char *zIn, int nIn){
  memcpy(p->zLine+p->n, zIn, nIn);
  p->n += nIn;
}

/*
** Append n spaces to the string.
*/
static void sbsWriteSpace(SbsLine *p, int n){
  while( n-- ) p->zLine[p->n++] = ' ';
}

/*
** Append a string to the output only if we are rendering HTML.
*/
static void sbsWriteHtml(SbsLine *p, const char *zIn){
  if( p->escHtml ) sbsWrite(p, zIn, strlen(zIn));
}

/*
** Write a 6-digit line number followed by a single space onto the line.
*/
static void sbsWriteLineno(SbsLine *p, int ln){
  sbsWriteHtml(p, "<span class=\"diffln\">");
  sqlite3_snprintf(7, &p->zLine[p->n], "%5d ", ln+1);
  p->n += 6;
  sbsWriteHtml(p, "</span>");
  p->zLine[p->n++] = ' ';
}

/*
** The two text segments zLeft and zRight are known to be different on 
** both ends, but they might have  a common segment in the middle.  If
** they do not have a common segment, return 0.  If they do have a large
** common segment, return 1 and before doing so set:
**
**   aLCS[0] = start of the common segment in zLeft
**   aLCS[1] = end of the common segment in zLeft
**   aLCS[2] = start of the common segment in zLeft
**   aLCS[3] = end of the common segment in zLeft
**
** This computation is for display purposes only and does not have to be
** optimal or exact.
*/
static int textLCS(
  const char *zLeft,  int nA,       /* String on the left */
  const char *zRight,  int nB,      /* String on the right */
  int *aLCS                         /* Identify bounds of LCS here */
){
  const unsigned char *zA = (const unsigned char*)zLeft;    /* left string */
  const unsigned char *zB = (const unsigned char*)zRight;   /* right string */
  int nt;                    /* Number of target points */
  int ti[3];                 /* Index for start of each 4-byte target */
  unsigned int target[3];    /* 4-byte alignment targets */
  unsigned int probe;        /* probe to compare against target */
  int iAS, iAE, iBS, iBE;    /* Range of common segment */
  int i, j;                  /* Loop counters */
  int rc = 0;                /* Result code.  1 for success */

  if( nA<6 || nB<6 ) return 0;
  memset(aLCS, 0, sizeof(int)*4);
  ti[0] = i = nB/2-2;
  target[0] = (zB[i]<<24) | (zB[i+1]<<16) | (zB[i+2]<<8) | zB[i+3];
  probe = 0;
  if( nB<16 ){
    nt = 1;
  }else{
    ti[1] = i = nB/4-2;
    target[1] = (zB[i]<<24) | (zB[i+1]<<16) | (zB[i+2]<<8) | zB[i+3];
    ti[2] = i = (nB*3)/4-2;
    target[2] = (zB[i]<<24) | (zB[i+1]<<16) | (zB[i+2]<<8) | zB[i+3];
    nt = 3;
  }
  probe = (zA[0]<<16) | (zA[1]<<8) | zA[2];
  for(i=3; i<nA; i++){
    probe = (probe<<8) | zA[i];
    for(j=0; j<nt; j++){
      if( probe==target[j] ){
        iAS = i-3;
        iAE = i+1;
        iBS = ti[j];
        iBE = ti[j]+4;
        while( iAE<nA && iBE<nB && zA[iAE]==zB[iBE] ){ iAE++; iBE++; }
        while( iAS>0 && iBS>0 && zA[iAS-1]==zB[iBS-1] ){ iAS--; iBS--; }
        if( iAE-iAS > aLCS[1] - aLCS[0] ){
          aLCS[0] = iAS;
          aLCS[1] = iAE;
          aLCS[2] = iBS;
          aLCS[3] = iBE;
          rc = 1;
        }
      }
    }
  }
  return rc;
}

/*
** Write out lines that have been edited.  Adjust the highlight to cover
** only those parts of the line that actually changed.
*/
static void sbsWriteLineChange(
  SbsLine *p,          /* The SBS output line */
  DLine *pLeft,        /* Left line of the change */
  int lnLeft,          /* Line number for the left line */
  DLine *pRight,       /* Right line of the change */
  int lnRight          /* Line number of the right line */
){
  int nLeft;           /* Length of left line in bytes */
  int nRight;          /* Length of right line in bytes */
  int nPrefix;         /* Length of common prefix */
  int nSuffix;         /* Length of common suffix */
  const char *zLeft;   /* Text of the left line */
  const char *zRight;  /* Text of the right line */
  int nLeftDiff;       /* nLeft - nPrefix - nSuffix */
  int nRightDiff;      /* nRight - nPrefix - nSuffix */
  int aLCS[4];         /* Bounds of common middle segment */
  static const char zClassRm[]   = "<span class=\"diffrm\">";
  static const char zClassAdd[]  = "<span class=\"diffadd\">";
  static const char zClassChng[] = "<span class=\"diffchng\">";

  nLeft = pLeft->h & LENGTH_MASK;
  zLeft = pLeft->z;
  nRight = pRight->h & LENGTH_MASK;
  zRight = pRight->z;

  nPrefix = 0;
  while( nPrefix<nLeft && nPrefix<nRight && zLeft[nPrefix]==zRight[nPrefix] ){
    nPrefix++;
  }
  nSuffix = 0;
  if( nPrefix<nLeft && nPrefix<nRight ){
    while( nSuffix<nLeft && nSuffix<nRight
           && zLeft[nLeft-nSuffix-1]==zRight[nRight-nSuffix-1] ){
      nSuffix++;
    }
    if( nSuffix==nLeft || nSuffix==nRight ) nPrefix = 0;
  }
  if( nPrefix+nSuffix > nLeft ) nSuffix = nLeft - nPrefix;
  if( nPrefix+nSuffix > nRight ) nSuffix = nRight - nPrefix;

  /* A single chunk of text inserted on the right */
  if( nPrefix+nSuffix==nLeft ){
    sbsWriteLineno(p, lnLeft);
    p->iStart2 = p->iEnd2 = 0;
    p->iStart = p->iEnd = -1;
    sbsWriteText(p, pLeft, SBS_PAD);
    sbsWrite(p, " | ", 3);
    sbsWriteLineno(p, lnRight);
    p->iStart = nPrefix;
    p->iEnd = nRight - nSuffix;
    p->zStart = zClassAdd;
    sbsWriteText(p, pRight, SBS_NEWLINE);
    return;
  }

  /* A single chunk of text deleted from the left */
  if( nPrefix+nSuffix==nRight ){
    /* Text deleted from the left */
    sbsWriteLineno(p, lnLeft);
    p->iStart2 = p->iEnd2 = 0;
    p->iStart = nPrefix;
    p->iEnd = nLeft - nSuffix;
    p->zStart = zClassRm;
    sbsWriteText(p, pLeft, SBS_PAD);
    sbsWrite(p, " | ", 3);
    sbsWriteLineno(p, lnRight);
    p->iStart = p->iEnd = -1;
    sbsWriteText(p, pRight, SBS_NEWLINE);
    return;
  }

  /* At this point we know that there is a chunk of text that has
  ** changed between the left and the right.  Check to see if there
  ** is a large unchanged section in the middle of that changed block.
  */
  nLeftDiff = nLeft - nSuffix - nPrefix;
  nRightDiff = nRight - nSuffix - nPrefix;
  if( p->escHtml
   && nLeftDiff >= 6
   && nRightDiff >= 6
   && textLCS(&zLeft[nPrefix], nLeftDiff, &zRight[nPrefix], nRightDiff, aLCS)
  ){
    sbsWriteLineno(p, lnLeft);
    p->iStart = nPrefix;
    p->iEnd = nPrefix + aLCS[0];
    p->zStart = aLCS[2]==0 ? zClassRm : zClassChng;
    p->iStart2 = nPrefix + aLCS[1];
    p->iEnd2 = nLeft - nSuffix;
    p->zStart2 = aLCS[3]==nRightDiff ? zClassRm : zClassChng;
    if( p->iStart2==p->iEnd2 ) p->iStart2 = p->iEnd2 = 0;
    if( p->iStart==p->iEnd ){
      p->iStart = p->iStart2;
      p->iEnd = p->iEnd2;
      p->zStart = p->zStart2;
      p->iStart2 = 0;
      p->iEnd2 = 0;
    }
    if( p->iStart==p->iEnd ) p->iStart = p->iEnd = -1;
    sbsWriteText(p, pLeft, SBS_PAD);
    sbsWrite(p, " | ", 3);
    sbsWriteLineno(p, lnRight);
    p->iStart = nPrefix;
    p->iEnd = nPrefix + aLCS[2];
    p->zStart = aLCS[0]==0 ? zClassAdd : zClassChng;
    p->iStart2 = nPrefix + aLCS[3];
    p->iEnd2 = nRight - nSuffix;
    p->zStart2 = aLCS[1]==nLeftDiff ? zClassAdd : zClassChng;
    if( p->iStart2==p->iEnd2 ) p->iStart2 = p->iEnd2 = 0;
    if( p->iStart==p->iEnd ){
      p->iStart = p->iStart2;
      p->iEnd = p->iEnd2;
      p->zStart = p->zStart2;
      p->iStart2 = 0;
      p->iEnd2 = 0;
    }
    if( p->iStart==p->iEnd ) p->iStart = p->iEnd = -1; 
    sbsWriteText(p, pRight, SBS_NEWLINE);
    return;
  }

  /* If all else fails, show a single big change between left and right */
  sbsWriteLineno(p, lnLeft);
  p->iStart2 = p->iEnd2 = 0;
  p->iStart = nPrefix;
  p->iEnd = nLeft - nSuffix;
  p->zStart = zClassChng;
  sbsWriteText(p, pLeft, SBS_PAD);
  sbsWrite(p, " | ", 3);
  sbsWriteLineno(p, lnRight);
  p->iEnd = nRight - nSuffix;
  sbsWriteText(p, pRight, SBS_NEWLINE);
}

/*
** Minimum of two values
*/
static int minInt(int a, int b){ return a<b ? a : b; }

/*
** Return the number between 0 and 100 that is smaller the closer pA and
** pB match.  Return 0 for a perfect match.  Return 100 if pA and pB are
** completely different.
*/
static int match_dline(DLine *pA, DLine *pB){
  const char *zA;            /* Left string */
  const char *zB;            /* right string */
  int nA;                    /* Bytes in zA[] */
  int nB;                    /* Bytes in zB[] */
  int avg;                   /* Average length of A and B */
  int i, j, k;               /* Loop counters */
  int best = 0;              /* Longest match found so far */
  int score;                 /* Final score.  0..100 */
  unsigned char c;           /* Character being examined */
  unsigned char aFirst[256]; /* aFirst[X] = index in zB[] of first char X */
  unsigned char aNext[252];  /* aNext[i] = index in zB[] of next zB[i] char */

  zA = pA->z;
  zB = pB->z;
  nA = pA->h & LENGTH_MASK;
  nB = pB->h & LENGTH_MASK;
  while( nA>0 && vcs_isspace(zA[0]) ){ nA--; zA++; }
  while( nA>0 && vcs_isspace(zA[nA-1]) ){ nA--; }
  while( nB>0 && vcs_isspace(zB[0]) ){ nB--; zB++; }
  while( nB>0 && vcs_isspace(zB[nB-1]) ){ nB--; }
  if( nA>250 ) nA = 250;
  if( nB>250 ) nB = 250;
  avg = (nA+nB)/2;
  if( avg==0 ) return 0;
  if( nA==nB && memcmp(zA, zB, nA)==0 ) return 0;
  memset(aFirst, 0, sizeof(aFirst));
  zA--; zB--;   /* Make both zA[] and zB[] 1-indexed */
  for(i=nB; i>0; i--){
    c = (unsigned char)zB[i];
    aNext[i] = aFirst[c];
    aFirst[c] = i;
  }
  best = 0;
  for(i=1; i<=nA-best; i++){
    c = (unsigned char)zA[i];
    for(j=aFirst[c]; j>0 && j<nB-best; j = aNext[j]){
      int limit = minInt(nA-i, nB-j);
      for(k=1; k<=limit && zA[k+i]==zB[k+j]; k++){}
      if( k>best ) best = k;
    }
  }
  score = (best>avg) ? 0 : (avg - best)*100/avg;

#if 0
  fprintf(stderr, "A: [%.*s]\nB: [%.*s]\nbest=%d avg=%d score=%d\n",
  nA, zA+1, nB, zB+1, best, avg, score);
#endif

  /* Return the result */
  return score;
}

/*
** There is a change block in which nLeft lines of text on the left are
** converted into nRight lines of text on the right.  This routine computes
** how the lines on the left line up with the lines on the right.
**
*/
static unsigned char *sbsAlignment(
   DLine *aLeft, int nLeft,       /* Text on the left */
   DLine *aRight, int nRight      /* Text on the right */
){
  int i, j, k;                 /* Loop counters */
  int *a;                      /* One row of the Wagner matrix */
  int *pToFree;                /* Space that needs to be freed */
  unsigned char *aM;           /* Wagner result matrix */
  int aBuf[100];               /* Stack space for a[] if nRight not to big */

  aM = vcs_malloc( (nLeft+1)*(nRight+1) );
  if( nLeft==0 ){
    memset(aM, 3, nRight);
    return aM;
  }
  if( nRight==0 ){
    memset(aM, 1, nLeft);
    return aM;
  }
  if( nRight < (sizeof(aBuf)/sizeof(aBuf[0]))-1 ){
    pToFree = 0;
    a = aBuf;
  }else{
    a = pToFree = vcs_malloc( sizeof(a[0])*(nRight+1) );
  }

  /* Compute the best alignment */
  for(i=0; i<=nRight; i++){
    aM[i] = 3;
    a[i] = i*50;
  }
  aM[0] = 0;
  for(j=1; j<=nLeft; j++){
    int p = a[0];
    a[0] = p+50;
    aM[j*(nRight+1)] = 1;
    for(i=1; i<=nRight; i++){
      int m = a[i-1]+50;
      int d = 3;
      if( m>a[i]+50 ){
        m = a[i]+50;
        d = 1;
      }
      if( m>p ){
        int score = match_dline(&aLeft[j-1], &aRight[i-1]);
        if( (score<66 || (i<j+1 && i>j-1)) && m>p+score ){
          m = p+score;
          d = 2;
        }
      }
      p = a[i];
      a[i] = m;
      aM[j*(nRight+1)+i] = d;
    }
  }

  /* Compute the lowest-cost path back through the matrix */
  i = nRight;
  j = nLeft;
  k = (nRight+1)*(nLeft+1)-1;
  while( i+j>0 ){
    unsigned char c = aM[k--];
    if( c==2 ){
      assert( i>0 && j>0 );
      i--;
      j--;
    }else if( c==3 ){
      assert( i>0 );
      i--;
    }else{
      assert( j>0 );
      j--;
    }
    aM[k] = aM[j*(nRight+1)+i];
  }
  k++;
  i = (nRight+1)*(nLeft+1) - k;
  memmove(aM, &aM[k], i);

  /* Return the result */
  vcs_free(pToFree);
  return aM;
}

/*
** Given a diff context in which the aEdit[] array has been filled
** in, compute a side-by-side diff into pOut.
*/
static void sbsDiff(
  DContext *p,       /* The computed diff */
  Blob *pOut,        /* Write the results here */
  int nContext,      /* Number of lines of context around each change */
  int width,         /* Width of each column of output */
  int escHtml        /* True to generate HTML output */
){
  DLine *A;     /* Left side of the diff */
  DLine *B;     /* Right side of the diff */  
  int a = 0;    /* Index of next line in A[] */
  int b = 0;    /* Index of next line in B[] */
  int *R;       /* Array of COPY/DELETE/INSERT triples */
  int r;        /* Index into R[] */
  int nr;       /* Number of COPY/DELETE/INSERT triples to process */
  int mxr;      /* Maximum value for r */
  int na, nb;   /* Number of lines shown from A and B */
  int i, j;     /* Loop counters */
  int m, ma, mb;/* Number of lines to output */
  int skip;     /* Number of lines to skip */
  int nChunk = 0; /* Number of chunks of diff output seen so far */
  SbsLine s;    /* Output line buffer */

  memset(&s, 0, sizeof(s));
  s.zLine = vcs_malloc( 10*width + 200 );
  if( s.zLine==0 ) return;
  s.width = width;
  s.escHtml = escHtml;
  s.iStart = -1;
  s.iStart2 = 0;
  s.iEnd = -1;
  A = p->aFrom;
  B = p->aTo;
  R = p->aEdit;
  mxr = p->nEdit;
  while( mxr>2 && R[mxr-1]==0 && R[mxr-2]==0 ){ mxr -= 3; }
  for(r=0; r<mxr; r += 3*nr){
    /* Figure out how many triples to show in a single block */
    for(nr=1; R[r+nr*3]>0 && R[r+nr*3]<nContext*2; nr++){}
    /* printf("r=%d nr=%d\n", r, nr); */

    /* For the current block comprising nr triples, figure out
    ** how many lines of A and B are to be displayed
    */
    if( R[r]>nContext ){
      na = nb = nContext;
      skip = R[r] - nContext;
    }else{
      na = nb = R[r];
      skip = 0;
    }
    for(i=0; i<nr; i++){
      na += R[r+i*3+1];
      nb += R[r+i*3+2];
    }
    if( R[r+nr*3]>nContext ){
      na += nContext;
      nb += nContext;
    }else{
      na += R[r+nr*3];
      nb += R[r+nr*3];
    }
    for(i=1; i<nr; i++){
      na += R[r+i*3];
      nb += R[r+i*3];
    }

    /* Draw the separator between blocks */
    if( r>0 ){
      if( escHtml ){
        blob_appendf(pOut, "<span class=\"diffhr\">%.*c</span>\n",
                           width*2+16, '.');
      }else{
        blob_appendf(pOut, "%.*c\n", width*2+16, '.');
      }
    }
    nChunk++;
    if( escHtml ){
      blob_appendf(pOut, "<a name=\"chunk%d\"></a>\n", nChunk);
    }

    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    for(j=0; j<m; j++){
      s.n = 0;
      sbsWriteLineno(&s, a+j);
      s.iStart = s.iEnd = -1;
      sbsWriteText(&s, &A[a+j], SBS_PAD);
      sbsWrite(&s, "   ", 3);
      sbsWriteLineno(&s, b+j);
      sbsWriteText(&s, &B[b+j], SBS_NEWLINE);
      blob_append(pOut, s.zLine, s.n);
    }
    a += m;
    b += m;

    /* Show the differences */
    for(i=0; i<nr; i++){
      unsigned char *alignment;
      ma = R[r+i*3+1];   /* Lines on left but not on right */
      mb = R[r+i*3+2];   /* Lines on right but not on left */
      alignment = sbsAlignment(&A[a], ma, &B[b], mb);
      for(j=0; ma+mb>0; j++){
        if( alignment[j]==1 ){
          s.n = 0;
          sbsWriteLineno(&s, a);
          s.iStart = 0;
          s.zStart = "<span class=\"diffrm\">";
          s.iEnd = s.width;
          sbsWriteText(&s, &A[a], SBS_PAD);
          sbsWrite(&s, " <\n", 3);
          blob_append(pOut, s.zLine, s.n);
          assert( ma>0 );
          ma--;
          a++;
        }else if( alignment[j]==2 ){
          s.n = 0;
          sbsWriteLineChange(&s, &A[a], a, &B[b], b);
          blob_append(pOut, s.zLine, s.n);
          assert( ma>0 && mb>0 );
          ma--;
          mb--;
          a++;
          b++;
        }else{
          s.n = 0;
          sbsWriteSpace(&s, width + 7);
          sbsWrite(&s, " > ", 3);
          sbsWriteLineno(&s, b);
          s.iStart = 0;
          s.zStart = "<span class=\"diffadd\">";
          s.iEnd = s.width;
          sbsWriteText(&s, &B[b], SBS_NEWLINE);
          blob_append(pOut, s.zLine, s.n);
          assert( mb>0 );
          mb--;
          b++;
        }
      }
      vcs_free(alignment);
      if( i<nr-1 ){
        m = R[r+i*3+3];
        for(j=0; j<m; j++){
          s.n = 0;
          sbsWriteLineno(&s, a+j);
          s.iStart = s.iEnd = -1;
          sbsWriteText(&s, &A[a+j], SBS_PAD);
          sbsWrite(&s, "   ", 3);
          sbsWriteLineno(&s, b+j);
          sbsWriteText(&s, &B[b+j], SBS_NEWLINE);
          blob_append(pOut, s.zLine, s.n);
        }
        b += m;
        a += m;
      }
    }

    /* Show the final common area */
    assert( nr==i );
    m = R[r+nr*3];
    if( m>nContext ) m = nContext;
    for(j=0; j<m; j++){
      s.n = 0;
      sbsWriteLineno(&s, a+j);
      s.iStart = s.iEnd = -1;
      sbsWriteText(&s, &A[a+j], SBS_PAD);
      sbsWrite(&s, "   ", 3);
      sbsWriteLineno(&s, b+j);
      sbsWriteText(&s, &B[b+j], SBS_NEWLINE);
      blob_append(pOut, s.zLine, s.n);
    }
  }
  free(s.zLine);
}

/*
** Compute the optimal longest common subsequence (LCS) using an
** exhaustive search.  This version of the LCS is only used for
** shorter input strings since runtime is O(N*N) where N is the
** input string length.
*/
static void optimalLCS(
  DContext *p,               /* Two files being compared */
  int iS1, int iE1,          /* Range of lines in p->aFrom[] */
  int iS2, int iE2,          /* Range of lines in p->aTo[] */
  int *piSX, int *piEX,      /* Write p->aFrom[] common segment here */
  int *piSY, int *piEY       /* Write p->aTo[] common segment here */
){
  int mxLength = 0;          /* Length of longest common subsequence */
  int i, j;                  /* Loop counters */
  int k;                     /* Length of a candidate subsequence */
  int iSXb = iS1;            /* Best match so far */
  int iSYb = iS2;            /* Best match so far */

  for(i=iS1; i<iE1-mxLength; i++){
    for(j=iS2; j<iE2-mxLength; j++){
      if( !same_dline(&p->aFrom[i], &p->aTo[j]) ) continue;
      if( mxLength && !same_dline(&p->aFrom[i+mxLength], &p->aTo[j+mxLength]) ){
        continue;
      }
      k = 1;
      while( i+k<iE1 && j+k<iE2 && same_dline(&p->aFrom[i+k],&p->aTo[j+k]) ){
        k++;
      }
      if( k>mxLength ){
        iSXb = i;
        iSYb = j;
        mxLength = k;
      }
    }
  }
  *piSX = iSXb;
  *piEX = iSXb + mxLength;
  *piSY = iSYb;
  *piEY = iSYb + mxLength;
}

/*
** Compare two blocks of text on lines iS1 through iE1-1 of the aFrom[]
** file and lines iS2 through iE2-1 of the aTo[] file.  Locate a sequence
** of lines in these two blocks that are exactly the same.  Return
** the bounds of the matching sequence.
*/
static void longestCommonSequence(
  DContext *p,               /* Two files being compared */
  int iS1, int iE1,          /* Range of lines in p->aFrom[] */
  int iS2, int iE2,          /* Range of lines in p->aTo[] */
  int *piSX, int *piEX,      /* Write p->aFrom[] common segment here */
  int *piSY, int *piEY       /* Write p->aTo[] common segment here */
){
  double bestScore = -1e30;  /* Best score seen so far */
  int i, j, k;               /* Loop counters */
  int n;                     /* Loop limit */
  DLine *pA, *pB;            /* Pointers to lines */
  int iSX, iSY, iEX, iEY;    /* Current match */
  double score;              /* Current score */
  int skew;                  /* How lopsided is the match */
  int dist;                  /* Distance of match from center */
  int mid;                   /* Center of the span */
  int iSXb, iSYb, iEXb, iEYb;   /* Best match so far */
  int iSXp, iSYp, iEXp, iEYp;   /* Previous match */


  iSXb = iSXp = iS1;
  iEXb = iEXp = iS1;
  iSYb = iSYp = iS2;
  iEYb = iEYp = iS2;
  mid = (iE1 + iS1)/2;
  for(i=iS1; i<iE1; i++){
    int limit = 0;
    j = p->aTo[p->aFrom[i].h % p->nTo].iHash;
    while( j>0 
      && (j-1<iS2 || j>=iE2 || !same_dline(&p->aFrom[i], &p->aTo[j-1]))
    ){
      if( limit++ > 10 ){
        j = 0;
        break;
      }
      j = p->aTo[j-1].iNext;
    }
    if( j==0 ) continue;
    assert( i>=iSXb && i>=iSXp );
    if( i<iEXb && j>=iSYb && j<iEYb ) continue;
    if( i<iEXp && j>=iSYp && j<iEYp ) continue;
    iSX = i;
    iSY = j-1;
    pA = &p->aFrom[iSX-1];
    pB = &p->aTo[iSY-1];
    n = minInt(iSX-iS1, iSY-iS2);
    for(k=0; k<n && same_dline(pA,pB); k++, pA--, pB--){}
    iSX -= k;
    iSY -= k;
    iEX = i+1;
    iEY = j;
    pA = &p->aFrom[iEX];
    pB = &p->aTo[iEY];
    n = minInt(iE1-iEX, iE2-iEY);
    for(k=0; k<n && same_dline(pA,pB); k++, pA++, pB++){}
    iEX += k;
    iEY += k;
    skew = (iSX-iS1) - (iSY-iS2);
    if( skew<0 ) skew = -skew;
    dist = (iSX+iEX)/2 - mid;
    if( dist<0 ) dist = -dist;
    score = (iEX - iSX) - 0.05*skew - 0.05*dist;
    if( score>bestScore ){
      bestScore = score;
      iSXb = iSX;
      iSYb = iSY;
      iEXb = iEX;
      iEYb = iEY;
    }else{
      iSXp = iSX;
      iSYp = iSY;
      iEXp = iEX;
      iEYp = iEY;
    }
  }
  if( iSXb==iEXb && (iE1-iS1)*(iE2-iS2)<400 ){
    /* If no common sequence is found using the hashing heuristic and
    ** the input is not too big, use the expensive exact solution */
    optimalLCS(p, iS1, iE1, iS2, iE2, piSX, piEX, piSY, piEY);
  }else{
    *piSX = iSXb;
    *piSY = iSYb;
    *piEX = iEXb;
    *piEY = iEYb;
  }
  /* printf("LCS(%d..%d/%d..%d) = %d..%d/%d..%d\n", 
     iS1, iE1, iS2, iE2, *piSX, *piEX, *piSY, *piEY);  */
}

/*
** Expand the size of aEdit[] array to hold at least nEdit elements.
*/
static void expandEdit(DContext *p, int nEdit){
  p->aEdit = vcs_realloc(p->aEdit, nEdit*sizeof(int));
  p->nEditAlloc = nEdit;
}

/*
** Append a new COPY/DELETE/INSERT triple.
*/
static void appendTriple(DContext *p, int nCopy, int nDel, int nIns){
  /* printf("APPEND %d/%d/%d\n", nCopy, nDel, nIns); */
  if( p->nEdit>=3 ){
    if( p->aEdit[p->nEdit-1]==0 ){
      if( p->aEdit[p->nEdit-2]==0 ){
        p->aEdit[p->nEdit-3] += nCopy;
        p->aEdit[p->nEdit-2] += nDel;
        p->aEdit[p->nEdit-1] += nIns;
        return;
      }
      if( nCopy==0 ){
        p->aEdit[p->nEdit-2] += nDel;
        p->aEdit[p->nEdit-1] += nIns;
        return;
      }
    }
    if( nCopy==0 && nDel==0 ){
      p->aEdit[p->nEdit-1] += nIns;
      return;
    }
  }  
  if( p->nEdit+3>p->nEditAlloc ){
    expandEdit(p, p->nEdit*2 + 15);
    if( p->aEdit==0 ) return;
  }
  p->aEdit[p->nEdit++] = nCopy;
  p->aEdit[p->nEdit++] = nDel;
  p->aEdit[p->nEdit++] = nIns;
}

/*
** Do a single step in the difference.  Compute a sequence of
** copy/delete/insert steps that will convert lines iS1 through iE1-1 of
** the input into lines iS2 through iE2-1 of the output and write
** that sequence into the difference context.
*/
static void diff_step(DContext *p, int iS1, int iE1, int iS2, int iE2){
  int iSX, iEX, iSY, iEY;

  if( iE1<=iS1 ){
    /* The first segment is empty */
    if( iE2>iS2 ){
      appendTriple(p, 0, 0, iE2-iS2);
    }
    return;
  }
  if( iE2<=iS2 ){
    /* The second segment is empty */
    appendTriple(p, 0, iE1-iS1, 0);
    return;
  }

  /* Find the longest matching segment between the two sequences */
  longestCommonSequence(p, iS1, iE1, iS2, iE2, &iSX, &iEX, &iSY, &iEY);

  if( iEX>iSX ){
    /* A common segment has been found.
    ** Recursively diff either side of the matching segment */
    diff_step(p, iS1, iSX, iS2, iSY);
    if( iEX>iSX ){
      appendTriple(p, iEX - iSX, 0, 0);
    }
    diff_step(p, iEX, iE1, iEY, iE2);
  }else{
    /* The two segments have nothing in common.  Delete the first then
    ** insert the second. */
    appendTriple(p, 0, iE1-iS1, iE2-iS2);
  }
}

/*
** Compute the differences between two files already loaded into
** the DContext structure.
*/
static void diff_all(DContext *p){
  int mnE, iS, iE1, iE2;

  /* Carve off the common header and footer */
  iE1 = p->nFrom;
  iE2 = p->nTo;
  while( iE1>0 && iE2>0 && same_dline(&p->aFrom[iE1-1], &p->aTo[iE2-1]) ){
    iE1--;
    iE2--;
  }
  mnE = iE1<iE2 ? iE1 : iE2;
  for(iS=0; iS<mnE && same_dline(&p->aFrom[iS],&p->aTo[iS]); iS++){}

  /* do the difference */
  if( iS>0 ){
    appendTriple(p, iS, 0, 0);
  }
  diff_step(p, iS, iE1, iS, iE2);
  if( iE1<p->nFrom ){
    appendTriple(p, p->nFrom - iE1, 0, 0);
  }

  /* Terminate the COPY/DELETE/INSERT triples with three zeros */
  expandEdit(p, p->nEdit+3);
  if( p->aEdit ){
    p->aEdit[p->nEdit++] = 0;
    p->aEdit[p->nEdit++] = 0;
    p->aEdit[p->nEdit++] = 0;
  }
}

static void diff_optimize(DContext *p){
  int r;       /* Index of current triple */
  int lnFrom;  /* Line number in p->aFrom */
  int lnTo;    /* Line number in p->aTo */
  int cpy, del, ins;

  lnFrom = lnTo = 0;
  for(r=0; r<p->nEdit; r += 3){
    cpy = p->aEdit[r];
    del = p->aEdit[r+1];
    ins = p->aEdit[r+2];
    lnFrom += cpy;
    lnTo += cpy;

    /* Shift insertions toward the beginning of the file */
    while( cpy>0 && del==0 && ins>0 ){
      DLine *pTop = &p->aFrom[lnFrom-1];  /* Line before start of insert */
      DLine *pBtm = &p->aTo[lnTo+ins-1];  /* Last line inserted */
      if( same_dline(pTop, pBtm)==0 ) break;
      if( LENGTH(pTop+1)+LENGTH(pBtm)<=LENGTH(pTop)+LENGTH(pBtm-1) ) break;
      lnFrom--;
      lnTo--;
      p->aEdit[r]--;
      p->aEdit[r+3]++;
      cpy--;
    }

    /* Shift insertions toward the end of the file */
    while( r+3<p->nEdit && p->aEdit[r+3]>0 && del==0 && ins>0 ){
      DLine *pTop = &p->aTo[lnTo];       /* First line inserted */
      DLine *pBtm = &p->aTo[lnTo+ins];   /* First line past end of insert */
      if( same_dline(pTop, pBtm)==0 ) break;
      if( LENGTH(pTop)+LENGTH(pBtm-1)<=LENGTH(pTop+1)+LENGTH(pBtm) ) break;
      lnFrom++;
      lnTo++;
      p->aEdit[r]++;
      p->aEdit[r+3]--;
      cpy++;
    }

    /* Shift deletions toward the beginning of the file */
    while( cpy>0 && del>0 && ins==0 ){
      DLine *pTop = &p->aFrom[lnFrom-1];     /* Line before start of delete */
      DLine *pBtm = &p->aFrom[lnFrom+del-1]; /* Last line deleted */
      if( same_dline(pTop, pBtm)==0 ) break;
      if( LENGTH(pTop+1)+LENGTH(pBtm)<=LENGTH(pTop)+LENGTH(pBtm-1) ) break;
      lnFrom--;
      lnTo--;
      p->aEdit[r]--;
      p->aEdit[r+3]++;
      cpy--;
    }

    /* Shift deletions toward the end of the file */
    while( r+3<p->nEdit && p->aEdit[r+3]>0 && del>0 && ins==0 ){
      DLine *pTop = &p->aFrom[lnFrom];     /* First line deleted */
      DLine *pBtm = &p->aFrom[lnFrom+del]; /* First line past end of delete */
      if( same_dline(pTop, pBtm)==0 ) break;
      if( LENGTH(pTop)+LENGTH(pBtm-1)<=LENGTH(pTop)+LENGTH(pBtm) ) break;
      lnFrom++;
      lnTo++;
      p->aEdit[r]++;
      p->aEdit[r+3]--;
      cpy++;
    }

    lnFrom += del;
    lnTo += ins;
  }
}

/*
** Extract the number of lines of context from diffFlags.  Supply an
** appropriate default if no context width is specified.
*/
int diff_context_lines(int diffFlags){
  int n = diffFlags & DIFF_CONTEXT_MASK;
  if( n==0 ) n = 5;
  return n;
}

/*
** Extract the width of columns for side-by-side diff.  Supply an
** appropriate default if no width is given.
*/
int diff_width(int diffFlags){
  int w = (diffFlags & DIFF_WIDTH_MASK)/(DIFF_CONTEXT_MASK+1);
  if( w==0 ) w = 80;
  return w;
}

/*
** Generate a report of the differences between files pA and pB.
*/
int *text_diff(
  Blob *pA_Blob,   /* FROM file */
  Blob *pB_Blob,   /* TO file */
  Blob *pOut,      /* Write diff here if not NULL */
  int diffFlags    /* DIFF_* flags defined above */
){
  int ignoreEolWs; /* Ignore whitespace at the end of lines */
  int nContext;    /* Amount of context to display */	
  DContext c;

  if( diffFlags & DIFF_INVERT ){
    Blob *pTemp = pA_Blob;
    pA_Blob = pB_Blob;
    pB_Blob = pTemp;
  }
  nContext = diff_context_lines(diffFlags);
  ignoreEolWs = (diffFlags & DIFF_IGNORE_EOLWS)!=0;

  /* Prepare the input files */
  memset(&c, 0, sizeof(c));
  c.aFrom = break_into_lines(blob_str(pA_Blob), blob_size(pA_Blob),
                             &c.nFrom, ignoreEolWs);
  c.aTo = break_into_lines(blob_str(pB_Blob), blob_size(pB_Blob),
                           &c.nTo, ignoreEolWs);
  if( c.aFrom==0 || c.aTo==0 ){
    free(c.aFrom);
    free(c.aTo);
    if( pOut ){
      blob_appendf(pOut, "cannot compute difference between binary files\n");
    }
    return 0;
  }

  /* Compute the difference */
  diff_all(&c);
  if( (diffFlags & DIFF_NOOPT)==0 ) diff_optimize(&c);

  if( pOut ){
    /* Compute a context or side-by-side diff into pOut */
    int escHtml = (diffFlags & DIFF_HTML)!=0;
    if( diffFlags & DIFF_SIDEBYSIDE ){
      int width = diff_width(diffFlags);
      sbsDiff(&c, pOut, nContext, width, escHtml);
    }else{
      int showLn = (diffFlags & DIFF_LINENO)!=0;
      contextDiff(&c, pOut, nContext, showLn, escHtml);
    }
    free(c.aFrom);
    free(c.aTo);
    free(c.aEdit);
    return 0;
  }else{
    /* If a context diff is not requested, then return the
    ** array of COPY/DELETE/INSERT triples.
    */
    free(c.aFrom);
    free(c.aTo);
    return c.aEdit;
  }
}

/*
** Process diff-related command-line options and return an appropriate
** "diffFlags" integer.  
**
**   --brief                Show filenames only    DIFF_BRIEF
**   --context|-c N         N lines of context.    DIFF_CONTEXT_MASK
**   --html                 Format for HTML        DIFF_HTML
**   --invert               Invert the diff        DIFF_INVERT
**   --linenum|-n           Show line numbers      DIFF_LINENO
**   --noopt                Disable optimization   DIFF_NOOPT
**   --side-by-side|-y      Side-by-side diff.     DIFF_SIDEBYSIDE
**   --width|-W N           N character lines.     DIFF_WIDTH_MASK
*/
int diff_options(void){
  int diffFlags = 0;
  const char *z;
  int f;
  if( find_option("side-by-side","y",0)!=0 ) diffFlags |= DIFF_SIDEBYSIDE;
  if( (z = find_option("context","c",1))!=0 && (f = atoi(z))>0 ){
    if( f > DIFF_CONTEXT_MASK ) f = DIFF_CONTEXT_MASK;
    diffFlags |= f;
  }
  if( (z = find_option("width","W",1))!=0 && (f = atoi(z))>0 ){
    f *= DIFF_CONTEXT_MASK+1;
    if( f > DIFF_WIDTH_MASK ) f = DIFF_CONTEXT_MASK;
    diffFlags |= f;
  }
  if( find_option("html",0,0)!=0 ) diffFlags |= DIFF_HTML;
  if( find_option("linenum","n",0)!=0 ) diffFlags |= DIFF_LINENO;
  if( find_option("noopt",0,0)!=0 ) diffFlags |= DIFF_NOOPT;
  if( find_option("invert",0,0)!=0 ) diffFlags |= DIFF_INVERT;
  if( find_option("brief",0,0)!=0 ) diffFlags |= DIFF_BRIEF;
  return diffFlags;
}
