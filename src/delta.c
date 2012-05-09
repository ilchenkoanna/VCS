/*
** This module implements the delta compress algorithm.
**
** Though developed specifically for vcs, the code in this file
** is generally appliable and is thus easily separated from the
** vcs source code base.  Nothing in this file depends on anything
** else in vcs.
*/
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "delta.h"

/*
** Macros for turning debugging printfs on and off
*/
#if 0
# define DEBUG1(X) X
#else
# define DEBUG1(X)
#endif
#if 0
#define DEBUG2(X) X
/*
** For debugging:
** Print 16 characters of text from zBuf
*/
static const char *print16(const char *z){
  int i;
  static char zBuf[20];
  for(i=0; i<16; i++){
    if( z[i]>=0x20 && z[i]<=0x7e ){
      zBuf[i] = z[i];
    }else{
      zBuf[i] = '.';
    }
  }
  zBuf[i] = 0;
  return zBuf;
}
#else
# define DEBUG2(X)
#endif

#if INTERFACE
/*
** The "u32" type must be an unsigned 32-bit integer.  Adjust this
*/
typedef unsigned int u32;

/*
** Must be a 16-bit value 
*/
typedef short int s16;
typedef unsigned short int u16;

#endif /* INTERFACE */

/*
** The width of a hash window in bytes.  The algorithm only works if this
** is a power of 2.
*/
#define NHASH 16

/*
** The current state of the rolling hash.
**
** z[] holds the values that have been hashed.  z[] is a circular buffer.
** z[i] is the first entry and z[(i+NHASH-1)%NHASH] is the last entry of 
** the window.
**
** Hash.a is the sum of all elements of hash.z[].  Hash.b is a weighted
** sum.  Hash.b is z[i]*NHASH + z[i+1]*(NHASH-1) + ... + z[i+NHASH-1]*1.
** (Each index for z[] should be module NHASH, of course.  The %NHASH operator
** is omitted in the prior expression for brevity.)
*/
typedef struct hash hash;
struct hash {
  u16 a, b;         /* Hash values */
  u16 i;            /* Start of the hash window */
  char z[NHASH];    /* The values that have been hashed */
};

/*
** Initialize the rolling hash using the first NHASH characters of z[]
*/
static void hash_init(hash *pHash, const char *z){
  u16 a, b, i;
  a = b = 0;
  for(i=0; i<NHASH; i++){
    a += z[i];
    b += (NHASH-i)*z[i];
    pHash->z[i] = z[i];
  }
  pHash->a = a & 0xffff;
  pHash->b = b & 0xffff;
  pHash->i = 0;
}

/*
** Advance the rolling hash by a single character "c"
*/
static void hash_next(hash *pHash, int c){
  u16 old = pHash->z[pHash->i];
  pHash->z[pHash->i] = c;
  pHash->i = (pHash->i+1)&(NHASH-1);
  pHash->a = pHash->a - old + c;
  pHash->b = pHash->b - NHASH*old + pHash->a;
}

/*
** Return a 32-bit hash value
*/
static u32 hash_32bit(hash *pHash){
  return (pHash->a & 0xffff) | (((u32)(pHash->b & 0xffff))<<16);
}

/*
** Write an base-64 integer into the given buffer.
*/
static void putInt(unsigned int v, char **pz){
  static const char zDigits[] = 
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~";
  /*  123456789 123456789 123456789 123456789 123456789 123456789 123 */
  int i, j;
  char zBuf[20];
  if( v==0 ){
    *(*pz)++ = '0';
    return;
  }
  for(i=0; v>0; i++, v>>=6){
    zBuf[i] = zDigits[v&0x3f];
  }
  for(j=i-1; j>=0; j--){
    *(*pz)++ = zBuf[j];
  }
}

/*
** Read bytes from *pz and convert them into a positive integer.  When
** finished, leave *pz pointing to the first character past the end of
** the integer.  The *pLen parameter holds the length of the string
** in *pz and is decremented once for each character in the integer.
*/
static unsigned int getInt(const char **pz, int *pLen){
  static const signed char zValue[] = {
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
     0,  1,  2,  3,  4,  5,  6,  7,    8,  9, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, 16,   17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32,   33, 34, 35, -1, -1, -1, -1, 36,
    -1, 37, 38, 39, 40, 41, 42, 43,   44, 45, 46, 47, 48, 49, 50, 51,
    52, 53, 54, 55, 56, 57, 58, 59,   60, 61, 62, -1, -1, -1, 63, -1,
  };
  unsigned int v = 0;
  int c;
  unsigned char *z = (unsigned char*)*pz;
  unsigned char *zStart = z;
  while( (c = zValue[0x7f&*(z++)])>=0 ){
     v = (v<<6) + c;
  }
  z--;
  *pLen -= z - zStart;
  *pz = (char*)z;
  return v;
}

/*
** Return the number digits in the base-64 representation of a positive integer
*/
static int digit_count(int v){
  unsigned int i, x;
  for(i=1, x=64; v>=x; i++, x <<= 6){}
  return i;
}

/*
** Compute a 32-bit checksum on the N-byte buffer.  Return the result.
*/
static unsigned int checksum(const char *zIn, size_t N){
  const unsigned char *z = (const unsigned char *)zIn;
  unsigned sum0 = 0;
  unsigned sum1 = 0;
  unsigned sum2 = 0;
  unsigned sum3 = 0;
  while(N >= 16){
    sum0 += ((unsigned)z[0] + z[4] + z[8] + z[12]);
    sum1 += ((unsigned)z[1] + z[5] + z[9] + z[13]);
    sum2 += ((unsigned)z[2] + z[6] + z[10]+ z[14]);
    sum3 += ((unsigned)z[3] + z[7] + z[11]+ z[15]);
    z += 16;
    N -= 16;
  }
  while(N >= 4){
    sum0 += z[0];
    sum1 += z[1];
    sum2 += z[2];
    sum3 += z[3];
    z += 4;
    N -= 4;
  }
  sum3 += (sum2 << 8) + (sum1 << 16) + (sum0 << 24);
  switch(N){
    case 3:   sum3 += (z[2] << 8);
    case 2:   sum3 += (z[1] << 16);
    case 1:   sum3 += (z[0] << 24);
    default:  ;
  }
  return sum3;
}

/*
** Create a new delta.
*/
int delta_create(
  const char *zSrc,      /* The source or pattern file */
  unsigned int lenSrc,   /* Length of the source file */
  const char *zOut,      /* The target file */
  unsigned int lenOut,   /* Length of the target file */
  char *zDelta           /* Write the delta into this buffer */
){
  int i, base;
  char *zOrigDelta = zDelta;
  hash h;
  int nHash;                 /* Number of hash table entries */
  int *landmark;             /* Primary hash table */
  int *collide;              /* Collision chain */
  int lastRead = -1;         /* Last byte of zSrc read by a COPY command */

  /* Add the target file size to the beginning of the delta
  */
  putInt(lenOut, &zDelta);
  *(zDelta++) = '\n';

  /* If the source file is very small, it means that we have no
  ** chance of ever doing a copy command.  Just output a single
  ** literal segment for the entire target and exit.
  */
  if( lenSrc<=NHASH ){
    putInt(lenOut, &zDelta);
    *(zDelta++) = ':';
    memcpy(zDelta, zOut, lenOut);
    zDelta += lenOut;
    putInt(checksum(zOut, lenOut), &zDelta);
    *(zDelta++) = ';';
    return zDelta - zOrigDelta;
  }

  /* Compute the hash table used to locate matching sections in the
  ** source file.
  */
  nHash = lenSrc/NHASH;
  collide = vcs_malloc( nHash*2*sizeof(int) );
  landmark = &collide[nHash];
  memset(landmark, -1, nHash*sizeof(int));
  memset(collide, -1, nHash*sizeof(int));
  for(i=0; i<lenSrc-NHASH; i+=NHASH){
    int hv;
    hash_init(&h, &zSrc[i]);
    hv = hash_32bit(&h) % nHash;
    collide[i/NHASH] = landmark[hv];
    landmark[hv] = i/NHASH;
  }

  /* Begin scanning the target file and generating copy commands and
  ** literal sections of the delta.
  */
  base = 0;    /* We have already generated everything before zOut[base] */
  while( base+NHASH<lenOut ){
    int iSrc, iBlock;
    unsigned int bestCnt, bestOfst=0, bestLitsz=0;
    hash_init(&h, &zOut[base]);
    i = 0;     /* Trying to match a landmark against zOut[base+i] */
    bestCnt = 0;
    while( 1 ){
      int hv;
      int limit = 250;

      hv = hash_32bit(&h) % nHash;
      DEBUG2( printf("LOOKING: %4d [%s]\n", base+i, print16(&zOut[base+i])); )
      iBlock = landmark[hv];
      while( iBlock>=0 && (limit--)>0 ){
        /*
        ** The hash window has identified a potential match against 
        ** landmark block iBlock.  But we need to investigate further.
        */
        int cnt, ofst, litsz;
        int j, k, x, y;
        int sz;

        /* Beginning at iSrc, match forwards as far as we can.  j counts
        ** the number of characters that match */
        iSrc = iBlock*NHASH;
        for(j=0, x=iSrc, y=base+i; x<lenSrc && y<lenOut; j++, x++, y++){
          if( zSrc[x]!=zOut[y] ) break;
        }
        j--;

        /* Beginning at iSrc-1, match backwards as far as we can.  k counts
        ** the number of characters that match */
        for(k=1; k<iSrc && k<=i; k++){
          if( zSrc[iSrc-k]!=zOut[base+i-k] ) break;
        }
        k--;

        /* Compute the offset and size of the matching region */
        ofst = iSrc-k;
        cnt = j+k+1;
        litsz = i-k;  /* Number of bytes of literal text before the copy */
        DEBUG2( printf("MATCH %d bytes at %d: [%s] litsz=%d\n",
                        cnt, ofst, print16(&zSrc[ofst]), litsz); )
        /* sz will hold the number of bytes needed to encode the "insert"
        ** command and the copy command, not counting the "insert" text */
        sz = digit_count(i-k)+digit_count(cnt)+digit_count(ofst)+3;
        if( cnt>=sz && cnt>bestCnt ){
          /* Remember this match only if it is the best so far and it
          ** does not increase the file size */
          bestCnt = cnt;
          bestOfst = iSrc-k;
          bestLitsz = litsz;
          DEBUG2( printf("... BEST SO FAR\n"); )
        }

        /* Check the next matching block */
        iBlock = collide[iBlock];
      }

      /* We have a copy command that does not cause the delta to be larger
      ** than a literal insert.  So add the copy command to the delta.
      */
      if( bestCnt>0 ){
        if( bestLitsz>0 ){
          /* Add an insert command before the copy */
          putInt(bestLitsz,&zDelta);
          *(zDelta++) = ':';
          memcpy(zDelta, &zOut[base], bestLitsz);
          zDelta += bestLitsz;
          base += bestLitsz;
          DEBUG2( printf("insert %d\n", bestLitsz); )
        }
        base += bestCnt;
        putInt(bestCnt, &zDelta);
        *(zDelta++) = '@';
        putInt(bestOfst, &zDelta);
        DEBUG2( printf("copy %d bytes from %d\n", bestCnt, bestOfst); )
        *(zDelta++) = ',';
        if( bestOfst + bestCnt -1 > lastRead ){
          lastRead = bestOfst + bestCnt - 1;
          DEBUG2( printf("lastRead becomes %d\n", lastRead); )
        }
        bestCnt = 0;
        break;
      }

      /* If we reach this point, it means no match is found so far */
      if( base+i+NHASH>=lenOut ){
        /* We have reached the end of the file and have not found any
        ** matches.  Do an "insert" for everything that does not match */
        putInt(lenOut-base, &zDelta);
        *(zDelta++) = ':';
        memcpy(zDelta, &zOut[base], lenOut-base);
        zDelta += lenOut-base;
        base = lenOut;
        break;
      }

      /* Advance the hash by one character.  Keep looking for a match */
      hash_next(&h, zOut[base+i+NHASH]);
      i++;
    }
  }
  /* Output a final "insert" record to get all the text at the end of
  ** the file that does not match anything in the source file.
  */
  if( base<lenOut ){
    putInt(lenOut-base, &zDelta);
    *(zDelta++) = ':';
    memcpy(zDelta, &zOut[base], lenOut-base);
    zDelta += lenOut-base;
  }
  /* Output the final checksum record. */
  putInt(checksum(zOut, lenOut), &zDelta);
  *(zDelta++) = ';';
  free(collide);
  return zDelta - zOrigDelta; 
}

/*
** Return the size (in bytes) of the output from applying
** a delta. 
*/
int delta_output_size(const char *zDelta, int lenDelta){
  int size;
  size = getInt(&zDelta, &lenDelta);
  if( *zDelta!='\n' ){
    /* ERROR: size integer not terminated by "\n" */
    return -1;
  }
  return size;
}


/*
** Apply a delta.
*/
int delta_apply(
  const char *zSrc,      /* The source or pattern file */
  int lenSrc,            /* Length of the source file */
  const char *zDelta,    /* Delta to apply to the pattern */
  int lenDelta,          /* Length of the delta */
  char *zOut             /* Write the output into this preallocated buffer */
){
  unsigned int limit;
  unsigned int total = 0;
#ifndef vcs_OMIT_DELTA_CKSUM_TEST
  char *zOrigOut = zOut;
#endif

  limit = getInt(&zDelta, &lenDelta);
  if( *zDelta!='\n' ){
    /* ERROR: size integer not terminated by "\n" */
    return -1;
  }
  zDelta++; lenDelta--;
  while( *zDelta && lenDelta>0 ){
    unsigned int cnt, ofst;
    cnt = getInt(&zDelta, &lenDelta);
    switch( zDelta[0] ){
      case '@': {
        zDelta++; lenDelta--;
        ofst = getInt(&zDelta, &lenDelta);
        if( lenDelta>0 && zDelta[0]!=',' ){
          /* ERROR: copy command not terminated by ',' */
          return -1;
        }
        zDelta++; lenDelta--;
        DEBUG1( printf("COPY %d from %d\n", cnt, ofst); )
        total += cnt;
        if( total>limit ){
          /* ERROR: copy exceeds output file size */
          return -1;
        }
        if( ofst+cnt > lenSrc ){
          /* ERROR: copy extends past end of input */
          return -1;
        }
        memcpy(zOut, &zSrc[ofst], cnt);
        zOut += cnt;
        break;
      }
      case ':': {
        zDelta++; lenDelta--;
        total += cnt;
        if( total>limit ){
          /* ERROR:  insert command gives an output larger than predicted */
          return -1;
        }
        DEBUG1( printf("INSERT %d\n", cnt); )
        if( cnt>lenDelta ){
          /* ERROR: insert count exceeds size of delta */
          return -1;
        }
        memcpy(zOut, zDelta, cnt);
        zOut += cnt;
        zDelta += cnt;
        lenDelta -= cnt;
        break;
      }
      case ';': {
        zDelta++; lenDelta--;
        zOut[0] = 0;
#ifndef vcs_OMIT_DELTA_CKSUM_TEST
        if( cnt!=checksum(zOrigOut, total) ){
          /* ERROR:  bad checksum */
          return -1;
        }
#endif
        if( total!=limit ){
          /* ERROR: generated size does not match predicted size */
          return -1;
        }
        return total;
      }
      default: {
        /* ERROR: unknown delta operator */
        return -1;
      }
    }
  }
  /* ERROR: unterminated delta */
  return -1;
}
