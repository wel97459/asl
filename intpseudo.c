/* intpseudo.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS                                                                        */
/*                                                                           */
/* Commonly Used Intel-Style Pseudo Instructions                             */
/*                                                                           */
/*****************************************************************************/

/*****************************************************************************
 * Includes
 *****************************************************************************/

#include "stdinc.h"
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "bpemu.h"
#include "endian.h"
#include "strutil.h"
#include "asmdef.h"
#include "asmsub.h"
#include "asmpars.h"
#include "errmsg.h"

#include "intpseudo.h"

/*****************************************************************************
 * Local Types
 *****************************************************************************/

struct sLayoutCtx;

typedef Boolean (*TLayoutFunc)(
#ifdef __PROTOS__
                               const tStrComp *pArg, struct sLayoutCtx *pCtx
#endif
                               );

typedef enum
{
  DSNone, DSConstant, DSSpace
} tDSFlag;

struct sCurrCodeFill
{
  LongInt FullWordCnt;
  int LastWordFill;
};
typedef struct sCurrCodeFill tCurrCodeFill;

struct sLayoutCtx
{
  tDSFlag DSFlag;
  TLayoutFunc LayoutFunc;
  int BaseElemLenBits, FullWordSize, ElemsPerFullWord;
  Boolean (*Put4I)(Byte b, struct sLayoutCtx *pCtx);
  Boolean (*Put8I)(Byte b, struct sLayoutCtx *pCtx);
  Boolean (*Put16I)(Word w, struct sLayoutCtx *pCtx);
  Boolean (*Put32I)(LongWord l, struct sLayoutCtx *pCtx);
  Boolean (*Put32F)(Double f, struct sLayoutCtx *pCtx);
  Boolean (*Put64I)(LargeWord q, struct sLayoutCtx *pCtx);
  Boolean (*Put64F)(Double f, struct sLayoutCtx *pCtx);
  Boolean (*Put80F)(Double t, struct sLayoutCtx *pCtx);
  Boolean (*Replicate)(const tCurrCodeFill *pStartPos, const tCurrCodeFill *pEndPos, struct sLayoutCtx *pCtx);
  tCurrCodeFill CurrCodeFill, FillIncPerElem;
  const tStrComp *pCurrComp;
  int LoHiMap;
};
typedef struct sLayoutCtx tLayoutCtx;

/*****************************************************************************
 * Local Functions
 *****************************************************************************/

void _DumpCodeFill(const char *pTitle, const tCurrCodeFill *pFill)
{
  fprintf(stderr, "%s %u %d\n", pTitle, (unsigned)pFill->FullWordCnt, pFill->LastWordFill);
}

/*!------------------------------------------------------------------------
 * \fn     Boolean SetDSFlag(struct sLayoutCtx *pCtx, tDSFlag Flag)
 * \brief  check set data disposition/reservation flag in context
 * \param  pCtx context
 * \param  Flag operation to be set
 * \return True if operation could be set or was alreday set
 * ------------------------------------------------------------------------ */

static Boolean SetDSFlag(struct sLayoutCtx *pCtx, tDSFlag Flag)
{
  if ((pCtx->DSFlag != DSNone) && (pCtx->DSFlag != Flag))
  {
    WrStrErrorPos(ErrNum_MixDBDS, pCtx->pCurrComp);
    return False;
  }
  pCtx->DSFlag = Flag;
  return True;
}

/*!------------------------------------------------------------------------
 * \fn     IncMaxCodeLen(struct sLayoutCtx *pCtx, LongWord NumFullWords)
 * \brief  assure xAsmCode has space for at moleast n more full words
 * \param  pCtxcontext
 * \param  NumFullWords # of additional words intended to write
 * \return True if success
 * ------------------------------------------------------------------------ */

static Boolean IncMaxCodeLen(struct sLayoutCtx *pCtx, LongWord NumFullWords)
{
  if (SetMaxCodeLen((pCtx->CurrCodeFill.FullWordCnt + NumFullWords) * pCtx->FullWordSize))
  {
    WrStrErrorPos(ErrNum_CodeOverflow, pCtx->pCurrComp);
    return False;
  }
  else
    return True;
}

static LargeWord ByteInWord(Byte b, int Pos)
{
  return ((LargeWord)b) << (Pos << 3);
}

static Byte NibbleInByte(Byte n, int Pos)
{
  return (n & 15) << (Pos << 2);
}

static Word NibbleInWord(Byte n, int Pos)
{
  return ((Word)(n & 15)) << (Pos << 2);
}

static Byte ByteFromWord(LargeWord w, int Pos)
{
  return (w >> (Pos << 3)) & 0xff;
}

static Byte NibbleFromByte(Byte b, int Pos)
{
  return (b >> (Pos << 2)) & 0x0f;
}

static Byte NibbleFromWord(Word w, int Pos)
{
  return (w >> (Pos << 2)) & 0x0f;
}

/*!------------------------------------------------------------------------
 * \fn     SubCodeFill
 * \brief  perform 'c = a - b' on tCurrCodeFill structures
 * \param  c result
 * \param  b, c arguments
 * ------------------------------------------------------------------------ */

static void SubCodeFill(tCurrCodeFill *c, const tCurrCodeFill *a, const tCurrCodeFill *b, struct sLayoutCtx *pCtx)
{
  c->FullWordCnt = a->FullWordCnt - b->FullWordCnt;
  if ((c->LastWordFill = a->LastWordFill - b->LastWordFill) < 0)
  {
    c->LastWordFill += pCtx->ElemsPerFullWord;
    c->FullWordCnt--;
  }
}

/*!------------------------------------------------------------------------
 * \fn     MultCodeFill(tCurrCodeFill *b, LongWord a, struct sLayoutCtx *pCtx)
 * \brief  perform 'b *= a' on tCurrCodeFill structures
 * \param  b what to multiply
 * \param  a scaling factor
 * ------------------------------------------------------------------------ */

static void MultCodeFill(tCurrCodeFill *b, LongWord a, struct sLayoutCtx *pCtx)
{
  b->FullWordCnt *= a;
  b->LastWordFill *= a;
  if (pCtx->ElemsPerFullWord > 1)
  {
    LongWord div = b->LastWordFill / pCtx->ElemsPerFullWord,
             mod = b->LastWordFill % pCtx->ElemsPerFullWord;
    b->FullWordCnt += div;
    b->LastWordFill = mod;
  }
}

/*!------------------------------------------------------------------------
 * \fn     IncCodeFill(tCurrCodeFill *a, struct sLayoutCtx *pCtx)
 * \brief  advance tCurrCodeFill pointer by one base element
 * \param  a pointer to increment
 * \param  pCtx context
 * ------------------------------------------------------------------------ */

static void IncCodeFill(tCurrCodeFill *a, struct sLayoutCtx *pCtx)
{
  if (++a->LastWordFill >= pCtx->ElemsPerFullWord)
  {
    a->LastWordFill -= pCtx->ElemsPerFullWord;
    a->FullWordCnt++;
  }
}

/*!------------------------------------------------------------------------
 * \fn     IncCurrCodeFill(struct sLayoutCtx *pCtx)
 * \brief  advance CodeFill pointer in context and reserve memory
 * \param  pCtx context
 * \return True if success
 * ------------------------------------------------------------------------ */

static Boolean IncCurrCodeFill(struct sLayoutCtx *pCtx)
{
  LongInt OldFullWordCnt = pCtx->CurrCodeFill.FullWordCnt;

  IncCodeFill(&pCtx->CurrCodeFill, pCtx);
  if (OldFullWordCnt == pCtx->CurrCodeFill.FullWordCnt)
    return True;
  else if (!IncMaxCodeLen(pCtx, 1))
    return False;
  else
  {
    WAsmCode[pCtx->CurrCodeFill.FullWordCnt] = 0;
    return True;
  }
}

/*!------------------------------------------------------------------------
 * \fn     IncCodeFillBy(tCurrCodeFill *a, const tCurrCodeFill *inc, struct sLayoutCtx *pCtx)
 * \brief  perform 'a += inc' on tCurrCodeFill structures
 * \param  a what to advance
 * \param  inc by what to advance
 * \param  pCtx context
 * ------------------------------------------------------------------------ */

static void IncCodeFillBy(tCurrCodeFill *a, const tCurrCodeFill *inc, struct sLayoutCtx *pCtx)
{
  a->LastWordFill += inc->LastWordFill;
  if ((pCtx->ElemsPerFullWord > 1) && (a->LastWordFill >= pCtx->ElemsPerFullWord))
  {
    a->LastWordFill -= pCtx->ElemsPerFullWord;
    a->FullWordCnt++;
  }
  a->FullWordCnt += inc->FullWordCnt;
}

/*****************************************************************************
 * Function:    LayoutNibble
 * Purpose:     parse argument, interprete as nibble,
 *              and put into result buffer
 * Result:      TRUE if no errors occured
 *****************************************************************************/

static Boolean Put4I_To_8(Byte b, struct sLayoutCtx *pCtx)
{
  tCurrCodeFill Pos = pCtx->CurrCodeFill;
  if (!IncCurrCodeFill(pCtx))
    return False;
  if (!Pos.LastWordFill)
    BAsmCode[Pos.FullWordCnt] = NibbleInByte(b, Pos.LastWordFill ^ pCtx->LoHiMap);
  else
    BAsmCode[Pos.FullWordCnt] |= NibbleInByte(b, Pos.LastWordFill ^ pCtx->LoHiMap);
  return True;
}

static Boolean Replicate4_To_8(const tCurrCodeFill *pStartPos, const tCurrCodeFill *pEndPos, struct sLayoutCtx *pCtx)
{
  Byte b;
  tCurrCodeFill CurrPos;

  CurrPos = *pStartPos;
  while ((CurrPos.FullWordCnt != pEndPos->FullWordCnt) || (CurrPos.LastWordFill != pEndPos->LastWordFill))
  {
    b = NibbleFromByte(BAsmCode[CurrPos.FullWordCnt], CurrPos.LastWordFill ^ pCtx->LoHiMap);
    if (!Put4I_To_8(b, pCtx))
      return False;
    IncCodeFill(&CurrPos, pCtx);
  }

  return True;
}

static Boolean Put4I_To_16(Byte b, struct sLayoutCtx *pCtx)
{
  tCurrCodeFill Pos = pCtx->CurrCodeFill;
  if (!IncCurrCodeFill(pCtx))
    return False;
  if (!Pos.LastWordFill)
    WAsmCode[Pos.FullWordCnt] = NibbleInWord(b, Pos.LastWordFill ^ pCtx->LoHiMap);
  else
    WAsmCode[Pos.FullWordCnt] |= NibbleInWord(b, Pos.LastWordFill ^ pCtx->LoHiMap);
  return True;
}

static Boolean Replicate4_To_16(const tCurrCodeFill *pStartPos, const tCurrCodeFill *pEndPos, struct sLayoutCtx *pCtx)
{
  Byte b;
  tCurrCodeFill CurrPos;

  CurrPos = *pStartPos;
  while ((CurrPos.FullWordCnt != pEndPos->FullWordCnt) || (CurrPos.LastWordFill != pEndPos->LastWordFill))
  {
    b = NibbleFromWord(WAsmCode[CurrPos.FullWordCnt], CurrPos.LastWordFill ^ pCtx->LoHiMap);
    if (!Put4I_To_16(b, pCtx))
      return False;
    IncCodeFill(&CurrPos, pCtx);
  }

  return True;
}

static Boolean LayoutNibble(const tStrComp *pExpr, struct sLayoutCtx *pCtx)
{
  Boolean Result = False;
  TempResult t;

  FirstPassUnknown = False;
  EvalStrExpression(pExpr, &t);
  switch (t.Typ)
  {
    case TempInt:
      if (FirstPassUnknown) t.Contents.Int &= 0xf;
      if (!SymbolQuestionable && !RangeCheck(t.Contents.Int, Int4)) WrStrErrorPos(ErrNum_OverRange, pExpr);
      else
      {
        if (!pCtx->Put4I(t.Contents.Int, pCtx))
          return Result;
        Result = True;
      }
      break;
    case TempFloat:
      WrStrErrorPos(ErrNum_IntButFloat, pExpr);
      break;
    case TempString:
      WrStrErrorPos(ErrNum_IntButString, pExpr);
      break;
    default:
      break;
  }

  return Result;
}

/*****************************************************************************
 * Function:    LayoutByte
 * Purpose:     parse argument, interprete as byte,
 *              and put into result buffer
 * Result:      TRUE if no errors occured
 *****************************************************************************/

static Boolean Put8I_To_8(Byte b, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 1))
    return False;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt++] = b;
  return True;
}

static Boolean Put8I_To_16(Byte b, struct sLayoutCtx *pCtx)
{
  tCurrCodeFill Pos = pCtx->CurrCodeFill;
  if (!IncCurrCodeFill(pCtx))
    return False;
  if (!Pos.LastWordFill)
    WAsmCode[Pos.FullWordCnt] = ByteInWord(b, Pos.LastWordFill ^ pCtx->LoHiMap);
  else
    WAsmCode[Pos.FullWordCnt] |= ByteInWord(b, Pos.LastWordFill ^ pCtx->LoHiMap);
  return True;
}

static Boolean Replicate8ToN_To_8(const tCurrCodeFill *pStartPos, const tCurrCodeFill *pEndPos, struct sLayoutCtx *pCtx)
{
  tCurrCodeFill Pos;

  if (!IncMaxCodeLen(pCtx, pEndPos->FullWordCnt - pStartPos->FullWordCnt))
    return False;

  for (Pos = *pStartPos; Pos.FullWordCnt < pEndPos->FullWordCnt; Pos.FullWordCnt += pCtx->BaseElemLenBits / 8)
  {
    memcpy(&BAsmCode[pCtx->CurrCodeFill.FullWordCnt], &BAsmCode[Pos.FullWordCnt], pCtx->BaseElemLenBits / 8);
    pCtx->CurrCodeFill.FullWordCnt += pCtx->BaseElemLenBits / 8;
  }
  if (Pos.FullWordCnt != pEndPos->FullWordCnt)
  {
    WrXError(ErrNum_InternalError, "DUP replication inconsistency");
    return False;
  }

  return True;
}

static Boolean Replicate8_To_16(const tCurrCodeFill *pStartPos, const tCurrCodeFill *pEndPos, struct sLayoutCtx *pCtx)
{
  Byte b;
  tCurrCodeFill CurrPos;

  CurrPos = *pStartPos;
  while ((CurrPos.FullWordCnt != pEndPos->FullWordCnt) || (CurrPos.LastWordFill != pEndPos->LastWordFill))
  {
    b = ByteFromWord(WAsmCode[CurrPos.FullWordCnt], CurrPos.LastWordFill ^ pCtx->LoHiMap);
    if (!Put8I_To_16(b, pCtx))
      return False;
    IncCodeFill(&CurrPos, pCtx);
  }

  return True;
}

static Boolean LayoutByte(const tStrComp *pExpr, struct sLayoutCtx *pCtx)
{
  Boolean Result = False;
  TempResult t;

  FirstPassUnknown = False;
  EvalStrExpression(pExpr, &t);
  switch (t.Typ)
  {
    case TempInt:
    ToInt:
      if (FirstPassUnknown) t.Contents.Int &= 0xff;
      if (!SymbolQuestionable && !RangeCheck(t.Contents.Int, Int8)) WrStrErrorPos(ErrNum_OverRange, pExpr);
      else
      {
        if (!pCtx->Put8I(t.Contents.Int, pCtx))
          return Result;
        Result = True;
      }
      break;
    case TempFloat:
      WrStrErrorPos(ErrNum_StringOrIntButFloat, pExpr);
      break;
    case TempString:
    {
      unsigned z;

      if (MultiCharToInt(&t, 4))
        goto ToInt;

      TranslateString(t.Contents.Ascii.Contents, t.Contents.Ascii.Length);

      for (z = 0; z < t.Contents.Ascii.Length; z++)
        if (!pCtx->Put8I(t.Contents.Ascii.Contents[z], pCtx))
          return Result;

      Result = True;
      break;
    }
    default:
      break;
  }

  return Result;
}

/*****************************************************************************
 * Function:    LayoutWord
 * Purpose:     parse argument, interprete as 16-bit word,
 *              and put into result buffer
 * Result:      TRUE if no errors occured
 *****************************************************************************/

static Boolean Put16I_To_8(Word w, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 2))
    return False;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (0 ^ pCtx->LoHiMap)] = Lo(w);
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (1 ^ pCtx->LoHiMap)] = Hi(w);
  pCtx->CurrCodeFill.FullWordCnt += 2;
  return True;
}

static Boolean Put16I_To_16(Word w, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 1))
    return False;
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt++] = w;
  return True;
}

static Boolean Replicate16ToN_To_16(const tCurrCodeFill *pStartPos, const tCurrCodeFill *pEndPos, struct sLayoutCtx *pCtx)
{
  tCurrCodeFill Pos;

  if (!IncMaxCodeLen(pCtx, pEndPos->FullWordCnt - pStartPos->FullWordCnt))
    return False;

  for (Pos = *pStartPos; Pos.FullWordCnt < pEndPos->FullWordCnt; Pos.FullWordCnt += pCtx->BaseElemLenBits / 16)
  {
    memcpy(&WAsmCode[pCtx->CurrCodeFill.FullWordCnt], &WAsmCode[Pos.FullWordCnt], pCtx->BaseElemLenBits / 8);
    pCtx->CurrCodeFill.FullWordCnt += pCtx->BaseElemLenBits / 16;
  }
  if (Pos.FullWordCnt != pEndPos->FullWordCnt)
  {
    WrXError(ErrNum_InternalError, "DUP replication inconsistency");
    return False;
  }

  return True;
}

static Boolean LayoutWord(const tStrComp *pExpr, struct sLayoutCtx *pCtx)
{
  Boolean Result = False;
  TempResult t;

  FirstPassUnknown = False;
  EvalStrExpression(pExpr, &t);
  Result = True;
  switch (t.Typ)
  {
    case TempInt:
    ToInt:
      if (FirstPassUnknown)
        t.Contents.Int &= 0xffff;
      if (!SymbolQuestionable && !RangeCheck(t.Contents.Int, Int16)) WrStrErrorPos(ErrNum_OverRange, pExpr);
      else
      {
        if (!pCtx->Put16I(t.Contents.Int, pCtx))
          return Result;
        Result = True;
      }
      break;
    case TempFloat:
      WrStrErrorPos(ErrNum_StringOrIntButFloat, pExpr);
      break;
    case TempString:
    {
      unsigned z;

      if (MultiCharToInt(&t, 4))
        goto ToInt;

      TranslateString(t.Contents.Ascii.Contents, t.Contents.Ascii.Length);

      for (z = 0; z < t.Contents.Ascii.Length; z++)
        if (!pCtx->Put16I(t.Contents.Ascii.Contents[z], pCtx))
          return Result;

      Result = True;
      break;
    }
    default:
      break;
  }

  return Result;
}

/*****************************************************************************
 * Function:    LayoutDoubleWord
 * Purpose:     parse argument, interprete as 32-bit word or
                single precision float, and put into result buffer
 * Result:      TRUE if no errors occured
 *****************************************************************************/

static Boolean Put32I_To_8(LongWord l, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 4))
    return False;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (0 ^ pCtx->LoHiMap)] = (l      ) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (1 ^ pCtx->LoHiMap)] = (l >>  8) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (2 ^ pCtx->LoHiMap)] = (l >> 16) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (3 ^ pCtx->LoHiMap)] = (l >> 24) & 0xff;
  pCtx->CurrCodeFill.FullWordCnt += 4;
  return True;
}

static Boolean Put32F_To_8(Double t, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 4))
    return False;
  Double_2_ieee4(t, BAsmCode + pCtx->CurrCodeFill.FullWordCnt, !!pCtx->LoHiMap);
  pCtx->CurrCodeFill.FullWordCnt += 4;
  return True;
}

static Boolean Put32I_To_16(LongWord l, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 2))
    return False;
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + (0 ^ pCtx->LoHiMap)] = LoWord(l);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + (1 ^ pCtx->LoHiMap)] = HiWord(l);
  pCtx->CurrCodeFill.FullWordCnt += 2;
  return True;
}

static Boolean Put32F_To_16(Double t, struct sLayoutCtx *pCtx)
{
  Byte Tmp[4];

  if (!IncMaxCodeLen(pCtx, 2))
    return False;
  Double_2_ieee4(t, Tmp, !!pCtx->LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 0] = ByteInWord(Tmp[0], 0 ^ pCtx->LoHiMap) | ByteInWord(Tmp[1], 1 ^ pCtx->LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 1] = ByteInWord(Tmp[2], 0 ^ pCtx->LoHiMap) | ByteInWord(Tmp[3], 1 ^ pCtx->LoHiMap);
  pCtx->CurrCodeFill.FullWordCnt += 2;
  return True;
}

static Boolean LayoutDoubleWord(const tStrComp *pExpr, struct sLayoutCtx *pCtx)
{
  TempResult erg;
  Boolean Result = False;
  Word Cnt = 0;

  FirstPassUnknown = False;
  EvalStrExpression(pExpr, &erg);
  Result = False;
  switch (erg.Typ)
  {
    case TempNone:
      break;
    case TempInt:
    ToInt:
      if (FirstPassUnknown)
        erg.Contents.Int &= 0xfffffffful;
      if (!SymbolQuestionable && !RangeCheck(erg.Contents.Int, Int32)) WrStrErrorPos(ErrNum_OverRange, pExpr);
      else
      {
        if (!pCtx->Put32I(erg.Contents.Int, pCtx))
          return Result;
        Cnt = 4;
        Result = True;
      }
      break;
    case TempFloat:
      if (!FloatRangeCheck(erg.Contents.Float, Float32)) WrStrErrorPos(ErrNum_OverRange, pExpr);
      else
      {
        if (!pCtx->Put32F(erg.Contents.Float, pCtx))
          return Result;
        Cnt = 4;
        Result = True;
      }
      break;
    case TempString:
    {
      unsigned z;

      if (MultiCharToInt(&erg, 4))
        goto ToInt;

      TranslateString(erg.Contents.Ascii.Contents, erg.Contents.Ascii.Length);

      for (z = 0; z < erg.Contents.Ascii.Length; z++)
        if (!pCtx->Put32I(erg.Contents.Ascii.Contents[z], pCtx))
          return Result;

      Cnt = erg.Contents.Ascii.Length * 4;
      Result = True;
      break;
    }
    case TempAll:
      assert(0);
  }

  if (Result && Cnt)
  {
    if (BigEndian)
      DSwap(BAsmCode + pCtx->CurrCodeFill.FullWordCnt - Cnt, Cnt);
  }

  return Result;
}


/*****************************************************************************
 * Function:    LayoutQuadWord
 * Purpose:     parse argument, interprete as 64-bit word or
                double precision float, and put into result buffer
 * Result:      TRUE if no errors occured
 *****************************************************************************/

static Boolean Put64I_To_8(LargeWord l, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 8))
    return False;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (0 ^ pCtx->LoHiMap)] = (l      ) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (1 ^ pCtx->LoHiMap)] = (l >>  8) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (2 ^ pCtx->LoHiMap)] = (l >> 16) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (3 ^ pCtx->LoHiMap)] = (l >> 24) & 0xff;
#ifdef HAS64
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (4 ^ pCtx->LoHiMap)] = (l >> 32) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (5 ^ pCtx->LoHiMap)] = (l >> 40) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (6 ^ pCtx->LoHiMap)] = (l >> 48) & 0xff;
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (7 ^ pCtx->LoHiMap)] = (l >> 56) & 0xff;
#else
  /* TempResult is TempInt, so sign-extend */
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (4 ^ pCtx->LoHiMap)] =
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (5 ^ pCtx->LoHiMap)] =
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (6 ^ pCtx->LoHiMap)] =
  BAsmCode[pCtx->CurrCodeFill.FullWordCnt + (7 ^ pCtx->LoHiMap)] = (l & 0x80000000ul) ? 0xff : 0x00;
#endif
  pCtx->CurrCodeFill.FullWordCnt += 8;
  return True;
}

static Boolean Put64F_To_8(Double t, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 8))
    return False;
  Double_2_ieee8(t, BAsmCode + pCtx->CurrCodeFill.FullWordCnt, !!pCtx->LoHiMap);
  pCtx->CurrCodeFill.FullWordCnt += 8;
  return True;
}

static Boolean Put64I_To_16(LargeWord l, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 4))
    return False;
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + (0 ^ pCtx->LoHiMap)] = (l      ) & 0xffff;
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + (1 ^ pCtx->LoHiMap)] = (l >> 16) & 0xffff;
#ifdef HAS64
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + (2 ^ pCtx->LoHiMap)] = (l >> 32) & 0xffff;
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + (3 ^ pCtx->LoHiMap)] = (l >> 48) & 0xffff;
#else
  /* TempResult is TempInt, so sign-extend */
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + (2 ^ pCtx->LoHiMap)] =
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + (3 ^ pCtx->LoHiMap)] = (l & 0x80000000ul) ? 0xffff : 0x0000;
#endif
  pCtx->CurrCodeFill.FullWordCnt += 4;
  return True;
}

static Boolean Put64F_To_16(Double t, struct sLayoutCtx *pCtx)
{
  Byte Tmp[8];
  int LoHiMap = pCtx->LoHiMap & 1;

  if (!IncMaxCodeLen(pCtx, 4))
    return False;
  Double_2_ieee8(t, Tmp, !!pCtx->LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 0] = ByteInWord(Tmp[0], 0 ^ LoHiMap) | ByteInWord(Tmp[1], 1 ^ LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 1] = ByteInWord(Tmp[2], 0 ^ LoHiMap) | ByteInWord(Tmp[3], 1 ^ LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 2] = ByteInWord(Tmp[4], 0 ^ LoHiMap) | ByteInWord(Tmp[5], 1 ^ LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 3] = ByteInWord(Tmp[6], 0 ^ LoHiMap) | ByteInWord(Tmp[7], 1 ^ LoHiMap);
  pCtx->CurrCodeFill.FullWordCnt += 4;
  return True;
}

static Boolean LayoutQuadWord(const tStrComp *pExpr, struct sLayoutCtx *pCtx)
{
  Boolean Result = False;
  TempResult erg;
  Word Cnt  = 0;

  EvalStrExpression(pExpr, &erg);
  Result = False;
  switch(erg.Typ)
  {
    case TempNone:
      break;
    case TempInt:
    ToInt:
      if (!pCtx->Put64I(erg.Contents.Int, pCtx))
        return Result;
      Cnt = 8;
      Result = True;
      break;
    case TempFloat:
      if (!pCtx->Put64F(erg.Contents.Float, pCtx))
        return Result;
      Cnt = 8;
      Result = True;
      break;
    case TempString:
    {
      unsigned z;

      if (MultiCharToInt(&erg, 8))
        goto ToInt;

      TranslateString(erg.Contents.Ascii.Contents, erg.Contents.Ascii.Length);

      for (z = 0; z < erg.Contents.Ascii.Length; z++)
        if (!pCtx->Put64I(erg.Contents.Ascii.Contents[z], pCtx))
          return Result;

      Cnt = erg.Contents.Ascii.Length * 8;
      Result = True;
      break;
    }
    case TempAll:
      assert(0);
  }

  if (Result)
  {
    if (BigEndian)
      QSwap(BAsmCode + pCtx->CurrCodeFill.FullWordCnt - Cnt, Cnt);
  }
  return Result;
}

/*****************************************************************************
 * Function:    LayoutTenBytes
 * Purpose:     parse argument, interprete extended precision float,
 *              and put into result buffer
 * Result:      TRUE if no errors occured
 *****************************************************************************/

static Boolean Put80F_To_8(Double t, struct sLayoutCtx *pCtx)
{
  if (!IncMaxCodeLen(pCtx, 10))
    return False;
  Double_2_ieee10(t, BAsmCode + pCtx->CurrCodeFill.FullWordCnt, !!pCtx->LoHiMap);
  pCtx->CurrCodeFill.FullWordCnt += 10;
  return True;
}

static Boolean Put80F_To_16(Double t, struct sLayoutCtx *pCtx)
{
  Byte Tmp[10];
  int LoHiMap = pCtx->LoHiMap & 1;

  if (!IncMaxCodeLen(pCtx, 5))
    return False;
  Double_2_ieee10(t, Tmp, !!pCtx->LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 0] = ByteInWord(Tmp[0], 0 ^ LoHiMap) | ByteInWord(Tmp[1], 1 ^ LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 1] = ByteInWord(Tmp[2], 0 ^ LoHiMap) | ByteInWord(Tmp[3], 1 ^ LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 2] = ByteInWord(Tmp[4], 0 ^ LoHiMap) | ByteInWord(Tmp[5], 1 ^ LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 3] = ByteInWord(Tmp[6], 0 ^ LoHiMap) | ByteInWord(Tmp[7], 1 ^ LoHiMap);
  WAsmCode[pCtx->CurrCodeFill.FullWordCnt + 4] = ByteInWord(Tmp[8], 0 ^ LoHiMap) | ByteInWord(Tmp[9], 1 ^ LoHiMap);
  pCtx->CurrCodeFill.FullWordCnt += 5;
  return True;
}

static Boolean LayoutTenBytes(const tStrComp *pExpr, struct sLayoutCtx *pCtx)
{
  Boolean Result = False;
  TempResult erg;
  Word Cnt;

  EvalStrExpression(pExpr, &erg);
  Result = False;
  switch(erg.Typ)
  {
    case TempNone:
      break;
    case TempInt:
    ToInt:
      erg.Contents.Float = erg.Contents.Int;
      erg.Typ = TempFloat;
      /* fall-through */
    case TempFloat:
      if (!pCtx->Put80F(erg.Contents.Float, pCtx))
        return Result;
      Cnt = 10;
      Result = True;
      break;
    case TempString:
    {
      unsigned z;

      if (MultiCharToInt(&erg, 4))
        goto ToInt;

      TranslateString(erg.Contents.Ascii.Contents, erg.Contents.Ascii.Length);

      for (z = 0; z < erg.Contents.Ascii.Length; z++)
        if (!pCtx->Put80F(erg.Contents.Ascii.Contents[z], pCtx))
          return Result;

      Cnt = erg.Contents.Ascii.Length * 10;
      Result = True;
      break;
    }
    case TempAll:
      assert(0);
  }

  if (Result)
  {
    if (BigEndian)
      TSwap(BAsmCode + pCtx->CurrCodeFill.FullWordCnt - Cnt, Cnt);
  }
  return Result;
}

/*****************************************************************************
 * Global Functions
 *****************************************************************************/

/*****************************************************************************
 * Function:    DecodeIntelPseudo
 * Purpose:     handle Intel-style pseudo instructions
 * Result:      TRUE if mnemonic was handled
 *****************************************************************************/

static Boolean DecodeIntelPseudo_ValidSymChar(char ch)
{
  ch = mytoupper(ch);

  return (((ch >= 'A') && (ch <= 'Z'))
       || ((ch >= '0') && (ch <= '9'))
       || (ch == '_')
       || (ch == '.'));
}

static void DecodeIntelPseudo_HandleQuote(int *pDepth, Byte *pQuote, char Ch)
{
  switch (Ch)
  {
    case '(':
      if (!(*pQuote))
        (*pDepth)++;
      break;
    case ')':
      if (!(*pQuote))
        (*pDepth)--;
      break;
    case '\'':
      if (!((*pQuote) & 2))
        (*pQuote) ^= 1;
      break;
    case '"':
      if (!((*pQuote) & 1))
        (*pQuote) ^= 2;
      break;
  }
}

static Boolean DecodeIntelPseudo_LayoutMult(const tStrComp *pArg, struct sLayoutCtx *pCtx)
{
  int z, Depth, Len;
  Boolean OK, LastValid, Result;
  Byte Quote;
  const char *pDupFnd, *pRun;
  const tStrComp *pSaveComp;

  pSaveComp = pCtx->pCurrComp;
  pCtx->pCurrComp = pArg;

  /* search for DUP: exclude parts in parentheses,
     and parts in quotation marks */

  Depth = Quote = 0;
  LastValid = FALSE;
  pDupFnd = NULL; Len = strlen(pArg->Str);
  for (pRun = pArg->Str; pRun < pArg->Str + Len - 2; pRun++)
  {
    DecodeIntelPseudo_HandleQuote(&Depth, &Quote, *pRun);
    if ((!Depth) && (!Quote))
    {
      if ((!LastValid)
      &&  (!DecodeIntelPseudo_ValidSymChar(pRun[3]))
      &&  (!as_strncasecmp(pRun, "DUP", 3)))
      {
        pDupFnd = pRun;
        break;
      }
    }
    LastValid = DecodeIntelPseudo_ValidSymChar(*pRun);
  }

  /* found DUP: */

  if (pDupFnd)
  {
    LongInt DupCnt;
    char *pSep, *pRun;
    String CopyStr;
    tStrComp Copy, DupArg, RemArg, ThisRemArg;
    tCurrCodeFill DUPStartFill, DUPEndFill;

    /* operate on copy */

    StrCompMkTemp(&Copy, CopyStr);
    StrCompCopy(&Copy, pArg);
    pSep = Copy.Str + (pDupFnd - pArg->Str);

    /* evaluate count */

    FirstPassUnknown = False;
    StrCompSplitRef(&DupArg, &RemArg, &Copy, pSep);
    DupCnt = EvalStrIntExpression(&DupArg, Int32, &OK);
    if (FirstPassUnknown)
    {
      WrStrErrorPos(ErrNum_FirstPassCalc, &DupArg); return False;
    }
    if (!OK)
    {
      Result = False;
      goto func_exit;
    }

    /* catch invalid counts */

    if (DupCnt <= 0)
    {
      if (DupCnt < 0)
        WrStrErrorPos(ErrNum_NegDUP, &DupArg);
      Result = True;
      goto func_exit;
    }

    /* split into parts and evaluate */

    StrCompIncRefLeft(&RemArg, 2);
    KillPrefBlanksStrCompRef(&RemArg);
    Len = strlen(RemArg.Str);
    if ((Len >= 2) && (*RemArg.Str == '(') && (RemArg.Str[Len - 1] == ')'))
    {
      StrCompIncRefLeft(&RemArg, 1);
      StrCompShorten(&RemArg, 1);
      Len -= 2;
    }
    DUPStartFill = pCtx->CurrCodeFill;
    do
    {
      pSep = NULL; Quote = Depth = 0;
      for (pRun = RemArg.Str; *pRun; pRun++)
      {
        DecodeIntelPseudo_HandleQuote(&Depth, &Quote, *pRun);
        if ((!Depth) && (!Quote) && (*pRun == ','))
        {
          pSep = pRun;
          break;
        }
      }
      if (pSep)
        StrCompSplitRef(&RemArg, &ThisRemArg, &RemArg, pSep);
      KillPrefBlanksStrCompRef(&RemArg);
      KillPostBlanksStrComp(&RemArg);
      if (!DecodeIntelPseudo_LayoutMult(&RemArg, pCtx))
      {
        Result = False;
        goto func_exit;
      }
      if (pSep)
        RemArg = ThisRemArg;
    }
    while (pSep);
    DUPEndFill = pCtx->CurrCodeFill;

    /* replicate result (data or reserve) */

    switch (pCtx->DSFlag)
    {
      case DSConstant:
        for (z = 1; z <= DupCnt - 1; z++)
          if (!pCtx->Replicate(&DUPStartFill, &DUPEndFill, pCtx))
          {
            Result = False;
            goto func_exit;
          }
        break;
      case DSSpace:
      {
        tCurrCodeFill Diff;

        SubCodeFill(&Diff, &DUPEndFill, &DUPStartFill, pCtx);
        MultCodeFill(&Diff, DupCnt - 1, pCtx);
        IncCodeFillBy(&pCtx->CurrCodeFill, &Diff, pCtx);
        break;
      }
      default:
        Result = False;
        goto func_exit;
    }

    Result = True;
  }

  /* no DUP: simple expression.  Differentiate space reservation & data disposition */

  else if (!strcmp(pArg->Str, "?"))
  {
    Result = SetDSFlag(pCtx, DSSpace);
    if (Result)
      IncCodeFillBy(&pCtx->CurrCodeFill, &pCtx->FillIncPerElem, pCtx);
  }

  else
    Result = SetDSFlag(pCtx, DSConstant) && pCtx->LayoutFunc(pArg, pCtx);

func_exit:
  pCtx->pCurrComp = pSaveComp;
  return Result;
}

Boolean DecodeIntelPseudo(Boolean BigEndian)
{
  tStrComp *pArg;
  Boolean OK;
  LongInt HVal;
  char Ident;

  if ((strlen(OpPart.Str) != 2) || (*OpPart.Str != 'D'))
    return False;
  Ident = OpPart.Str[1];

  if ((Ident == 'B') || (Ident == 'W') || (Ident == 'D') || (Ident == 'Q') || (Ident == 'T') || (Ident == 'N'))
  {
    tLayoutCtx LayoutCtx;

    memset(&LayoutCtx, 0, sizeof(LayoutCtx));
    LayoutCtx.DSFlag = DSNone;
    LayoutCtx.FullWordSize = Grans[ActPC];
    switch (Ident)
    {
      case 'N':
        LayoutCtx.LayoutFunc = LayoutNibble;
        LayoutCtx.BaseElemLenBits = 4;
        switch (Grans[ActPC])
        {
          case 1:
            LayoutCtx.Put4I = Put4I_To_8;
            LayoutCtx.LoHiMap = BigEndian ? 1 : 0;
            LayoutCtx.Replicate = Replicate4_To_8;
            break;
          case 2:
            LayoutCtx.Put4I = Put4I_To_16;
            LayoutCtx.LoHiMap = BigEndian ? 3 : 0;
            LayoutCtx.Replicate = Replicate4_To_16;
            break;
        }
        break;
      case 'B':
        LayoutCtx.LayoutFunc = LayoutByte;
        LayoutCtx.BaseElemLenBits = 8;
        switch (Grans[ActPC])
        {
          case 1:
            LayoutCtx.Put8I = Put8I_To_8;
            LayoutCtx.Replicate = Replicate8ToN_To_8;
            break;
          case 2:
            LayoutCtx.Put8I = Put8I_To_16;
            LayoutCtx.LoHiMap = BigEndian ? 1 : 0;
            LayoutCtx.Replicate = Replicate8_To_16;
            break;
        }
        if (*LabPart.Str)
          SetSymbolOrStructElemSize(&LabPart, eSymbolSize8Bit);
        break;
      case 'W':
        LayoutCtx.LayoutFunc = LayoutWord;
        LayoutCtx.BaseElemLenBits = 16;
        switch (Grans[ActPC])
        {
          case 1:
            LayoutCtx.Put16I = Put16I_To_8;
            LayoutCtx.LoHiMap = BigEndian ? 1 : 0;
            LayoutCtx.Replicate = Replicate8ToN_To_8;
            break;
          case 2:
            LayoutCtx.Put16I = Put16I_To_16;
            LayoutCtx.Replicate = Replicate16ToN_To_16;
            break;
        }
        if (*LabPart.Str)
          SetSymbolOrStructElemSize(&LabPart, eSymbolSize16Bit);
        break;
      case 'D':
        LayoutCtx.LayoutFunc = LayoutDoubleWord;
        LayoutCtx.BaseElemLenBits = 32;
        switch (Grans[ActPC])
        {
          case 1:
            LayoutCtx.Put32I = Put32I_To_8;
            LayoutCtx.Put32F = Put32F_To_8;
            LayoutCtx.LoHiMap = BigEndian ? 3 : 0;
            LayoutCtx.Replicate = Replicate8ToN_To_8;
            break;
          case 2:
            LayoutCtx.Put32I = Put32I_To_16;
            LayoutCtx.Put32F = Put32F_To_16;
            LayoutCtx.LoHiMap = BigEndian ? 1 : 0;
            LayoutCtx.Replicate = Replicate16ToN_To_16;
            break;
        }
        if (*LabPart.Str)
          SetSymbolOrStructElemSize(&LabPart, eSymbolSize32Bit);
        break;
      case 'Q':
        LayoutCtx.LayoutFunc = LayoutQuadWord;
        LayoutCtx.BaseElemLenBits = 64;
        switch (Grans[ActPC])
        {
          case 1:
            LayoutCtx.Put64I = Put64I_To_8;
            LayoutCtx.Put64F = Put64F_To_8;
            LayoutCtx.LoHiMap = BigEndian ? 7 : 0;
            LayoutCtx.Replicate = Replicate8ToN_To_8;
            break;
          case 2:
            LayoutCtx.Put64I = Put64I_To_16;
            LayoutCtx.Put64F = Put64F_To_16;
            LayoutCtx.LoHiMap = BigEndian ? 3 : 0;
            LayoutCtx.Replicate = Replicate16ToN_To_16;
            break;
        }
        if (*LabPart.Str)
          SetSymbolOrStructElemSize(&LabPart, eSymbolSize64Bit);
        break;
      case 'T':
        LayoutCtx.LayoutFunc = LayoutTenBytes;
        LayoutCtx.BaseElemLenBits = 80;
        switch (Grans[ActPC])
        {
          case 1:
            LayoutCtx.Put80F = Put80F_To_8;
            LayoutCtx.LoHiMap = BigEndian ? 1 : 0;
            LayoutCtx.Replicate = Replicate8ToN_To_8;
            break;
          case 2:
            LayoutCtx.Put80F = Put80F_To_16;
            LayoutCtx.LoHiMap = BigEndian ? 1 : 0;
            LayoutCtx.Replicate = Replicate16ToN_To_16;
            break;
        }
        if (*LabPart.Str)
          SetSymbolOrStructElemSize(&LabPart, eSymbolSize80Bit);
        break;
      default:
        return False;
    }
    LayoutCtx.ElemsPerFullWord = (8 * LayoutCtx.FullWordSize) / LayoutCtx.BaseElemLenBits;
    if (LayoutCtx.ElemsPerFullWord > 1)
    {
      LayoutCtx.FillIncPerElem.FullWordCnt = 0;
      LayoutCtx.FillIncPerElem.LastWordFill = 1;
    }
    else
    {
      LayoutCtx.FillIncPerElem.FullWordCnt = LayoutCtx.BaseElemLenBits / (8 * LayoutCtx.FullWordSize);
      LayoutCtx.FillIncPerElem.LastWordFill = 0;
    }

    OK = True;
    forallargs(pArg, OK)
    {
      if (!*pArg->Str)
      {
        OK = FALSE;
        WrStrErrorPos(ErrNum_EmptyArgument, pArg);
      }
      else
        OK = DecodeIntelPseudo_LayoutMult(pArg, &LayoutCtx);
    }

    /* Finalize: add optional padding if fractions of full words
       remain unused & set code length */

    if (OK)
    {
      if (LayoutCtx.CurrCodeFill.LastWordFill)
      {
        WrError(ErrNum_PaddingAdded);
        LayoutCtx.CurrCodeFill.LastWordFill = 0;
        LayoutCtx.CurrCodeFill.FullWordCnt++;
      }
      CodeLen = LayoutCtx.CurrCodeFill.FullWordCnt;
    }


    DontPrint = (LayoutCtx.DSFlag == DSSpace);
    if (DontPrint)
    {
      BookKeeping();
      if (!CodeLen && OK) WrError(ErrNum_NullResMem);
    }
    if (OK && (LayoutCtx.FullWordSize == 1))
      ActListGran = 1;
    return True;
  }

  if (Ident == 'S')
  {
    if (ChkArgCnt(1, 1))
    {
      FirstPassUnknown = False;
      HVal = EvalStrIntExpression(&ArgStr[1], Int32, &OK);
      if (FirstPassUnknown) WrError(ErrNum_FirstPassCalc);
      else if (OK)
      {
        DontPrint = True;
        CodeLen = HVal;
        if (!HVal)
          WrError(ErrNum_NullResMem);
        BookKeeping();
      }
    }
    return True;
  }

  return False;
}
