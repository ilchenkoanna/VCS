/*
** This module implements the interface to the delta generator.
*/
#include "config.h"
#include "deltacmd.h"
 
/*
** Create a delta that describes the change from pOriginal to pTarget
** and put that delta in pDelta.  The pDelta blob is assumed to be
** uninitialized.
*/
int blob_delta_create(Blob *pOriginal, Blob *pTarget, Blob *pDelta){
  const char *zOrig, *zTarg;
  int lenOrig, lenTarg;
  int len;
  char *zRes;
  blob_zero(pDelta);
  zOrig = blob_buffer(pOriginal);
  lenOrig = blob_size(pOriginal);
  zTarg = blob_buffer(pTarget);
  lenTarg = blob_size(pTarget);
  blob_resize(pDelta, lenTarg+16);
  zRes = blob_buffer(pDelta);
  len = delta_create(zOrig, lenOrig, zTarg, lenTarg, zRes);
  blob_resize(pDelta, len);
  return 0;
}

/*
** Apply the delta in pDelta to the original file pOriginal to generate
** the target file pTarget.  The pTarget blob is initialized by this
** routine.
*/
int blob_delta_apply(Blob *pOriginal, Blob *pDelta, Blob *pTarget){
  int len, n;
  Blob out;

  n = delta_output_size(blob_buffer(pDelta), blob_size(pDelta));
  blob_zero(&out);
  if( n<0 ) return -1;
  blob_resize(&out, n);
  len = delta_apply(
     blob_buffer(pOriginal), blob_size(pOriginal),
     blob_buffer(pDelta), blob_size(pDelta),
     blob_buffer(&out));
  if( len<0 ){
    blob_reset(&out);
  }else if( len!=n ){
    blob_resize(&out, len);
  }
  if( pTarget==pOriginal ){
    blob_reset(pOriginal);
  }
  *pTarget = out;
  return len;
}
