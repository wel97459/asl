#ifndef _TEMPRESULT_H
#define _TEMPRESULT_H
/* tempresult.h */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* internal holder for int/float/string                                      */
/*                                                                           */
/*****************************************************************************/

#include "datatypes.h"
#include "dynstring.h"
#include "symflags.h"

typedef enum {TempNone = 0, TempInt = 1, TempFloat = 2, TempString = 4, TempAll = 7} TempType;

struct sRelocEntry;

struct sTempResult
{
  TempType Typ;
  tSymbolFlags Flags;
  struct sRelocEntry *Relocs;
  union
  {
    LargeInt Int;
    Double Float;
    tDynString Ascii;
  } Contents;
};
typedef struct sTempResult TempResult;

extern int TempResultToFloat(TempResult *pResult);

extern int TempResultToPlainString(char *pDest, const TempResult *pResult, unsigned DestSize);

#endif /* _TEMPRESULT_H */
