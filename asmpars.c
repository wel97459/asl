/* asmpars.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* Verwaltung von Symbolen und das ganze Drumherum...                        */
/*                                                                           */
/*****************************************************************************/

#include "stdinc.h"
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "endian.h"
#include "bpemu.h"
#include "nls.h"
#include "nlmessages.h"
#include "as.rsc"
#include "strutil.h"
#include "strcomp.h"

#include "asmdef.h"
#include "asmsub.h"
#include "errmsg.h"
#include "asmfnums.h"
#include "asmrelocs.h"
#include "asmstructs.h"
#include "chunks.h"
#include "trees.h"
#include "operator.h"
#include "function.h"

#include "asmpars.h"

#define LOCSYMSIGHT 3       /* max. sight for nameless temporary symbols */

/* Mask, Min 6 Max are computed at initialization */

tIntTypeDef IntTypeDefs[IntTypeCnt] =
{
  { 0x0001, 0, 0, 0 }, /* UInt1 */
  { 0x0002, 0, 0, 0 }, /* UInt2 */
  { 0x0003, 0, 0, 0 }, /* UInt3 */
  { 0x8004, 0, 0, 0 }, /* SInt4 */
  { 0x0004, 0, 0, 0 }, /* UInt4 */
  { 0xc004, 0, 0, 0 }, /* Int4 */
  { 0x8005, 0, 0, 0 }, /* SInt5 */
  { 0x0005, 0, 0, 0 }, /* UInt5 */
  { 0xc005, 0, 0, 0 }, /* Int5 */
  { 0x8006, 0, 0, 0 }, /* SInt6 */
  { 0x0006, 0, 0, 0 }, /* UInt6 */
  { 0x8007, 0, 0, 0 }, /* SInt7 */
  { 0x0007, 0, 0, 0 }, /* UInt7 */
  { 0x8008, 0, 0, 0 }, /* SInt8 */
  { 0x0008, 0, 0, 0 }, /* UInt8 */
  { 0xc008, 0, 0, 0 }, /* Int8 */
  { 0x8009, 0, 0, 0 }, /* SInt9 */
  { 0x0009, 0, 0, 0 }, /* UInt9 */
  { 0x000a, 0, 0, 0 }, /* UInt10 */
  { 0xc00a, 0, 0, 0 }, /* Int10 */
  { 0x000b, 0, 0, 0 }, /* UInt11 */
  { 0x000c, 0, 0, 0 }, /* UInt12 */
  { 0xc00c, 0, 0, 0 }, /* Int12 */
  { 0x000d, 0, 0, 0 }, /* UInt13 */
  { 0x000e, 0, 0, 0 }, /* UInt14 */
  { 0xc00e, 0, 0, 0 }, /* Int14 */
  { 0x800f, 0, 0, 0 }, /* SInt15 */
  { 0x000f, 0, 0, 0 }, /* UInt15 */
  { 0x8010, 0, 0, 0 }, /* SInt16 */
  { 0x0010, 0, 0, 0 }, /* UInt16 */
  { 0xc010, 0, 0, 0 }, /* Int16 */
  { 0x0011, 0, 0, 0 }, /* UInt17 */
  { 0x0012, 0, 0, 0 }, /* UInt18 */
  { 0x0013, 0, 0, 0 }, /* UInt19 */
  { 0x8014, 0, 0, 0 }, /* SInt20 */
  { 0x0014, 0, 0, 0 }, /* UInt20 */
  { 0xc014, 0, 0, 0 }, /* Int20 */
  { 0x0015, 0, 0, 0 }, /* UInt21 */
  { 0x0016, 0, 0, 0 }, /* UInt22 */
  { 0x0017, 0, 0, 0 }, /* UInt23 */
  { 0x8018, 0, 0, 0 }, /* SInt24 */
  { 0x0018, 0, 0, 0 }, /* UInt24 */
  { 0xc018, 0, 0, 0 }, /* Int24 */
  { 0x8020, 0, 0, 0 }, /* SInt32 */
  { 0x0020, 0, 0, 0 }, /* UInt32 */
  { 0xc020, 0, 0, 0 }, /* Int32 */
#ifdef HAS64
  { 0x8040, 0, 0, 0 }, /* SInt64 */
  { 0x0040, 0, 0, 0 }, /* UInt64 */
  { 0xc040, 0, 0, 0 }, /* Int64 */
#endif
};

typedef struct
{
  Boolean Back;
  LongInt Counter;
} TTmpSymLog;

Boolean FirstPassUnknown;      /* Hinweisflag: evtl. im ersten Pass unbe-
                                  kanntes Symbol, Ausdruck nicht ausgewertet */
Boolean SymbolQuestionable;    /* Hinweisflag:  Dadurch, dass Phasenfehler
                                  aufgetreten sind, ist dieser Symbolwert evtl.
                                  nicht mehr aktuell                         */
Boolean UsesForwards;          /* Hinweisflag: benutzt Vorwaertsdefinitionen */
LongInt MomLocHandle;          /* Merker, den lokale Symbole erhalten        */
LongInt TmpSymCounter,         /* counters for local symbols                 */
        FwdSymCounter,
        BackSymCounter;
char TmpSymCounterVal[10];     /* representation as string                   */
TTmpSymLog TmpSymLog[LOCSYMSIGHT];
LongInt TmpSymLogDepth;

LongInt LocHandleCnt;          /* mom. verwendeter lokaler Handle            */

static char BaseIds[3] =
{
  '%', '@', '$'
};
static char BaseLetters[4] =
{
  'B', 'O', 'H', 'Q'
};
static Byte BaseVals[4] =
{
  2, 8, 16, 8
};

typedef struct sSymbolEntry
{
  TTree Tree;
  Byte SymType;
  ShortInt SymSize;
  Boolean Defined, Used, Changeable;
  SymbolVal SymWert;
  PCrossRef RefList;
  Byte FileNum;
  LongInt LineNum;
  tSymbolFlags Flags;
  TRelocEntry *Relocs;
} TSymbolEntry, *PSymbolEntry;

typedef struct sSymbolStackEntry
{
  struct sSymbolStackEntry *Next;
  SymbolVal Contents;
} TSymbolStackEntry, *PSymbolStackEntry;

typedef struct sSymbolStack
{
  struct sSymbolStack *Next;
  char *Name;
  PSymbolStackEntry Contents;
} TSymbolStack, *PSymbolStack;

typedef struct sDefSymbol
{
  struct sDefSymbol *Next;
  char *SymName;
  TempResult Wert;
} TDefSymbol, *PDefSymbol;

typedef struct sCToken
{
  struct sCToken *Next;
  char *Name;
  LongInt Parent;
  ChunkList Usage;
} TCToken, *PCToken;

typedef struct sLocHeap
{
  struct sLocHeap *Next;
  LongInt Cont;
} TLocHeap, *PLocHandle;

typedef struct sRegDefList
{
  struct sRegDefList *Next;
  LongInt Section;
  char *Value;
  Boolean Used;
} TRegDefList, *PRegDefList;

typedef struct sRegDef
{
  struct sRegDef *Left, *Right;
  char *Orig;
  PRegDefList Defs, DoneDefs;
} TRegDef, *PRegDef;

static PSymbolEntry FirstSymbol, FirstLocSymbol;
static PDefSymbol FirstDefSymbol;
/*static*/ PCToken FirstSection;
static PRegDef FirstRegDef;
static Boolean DoRefs;              /* Querverweise protokollieren */
static PLocHandle FirstLocHandle;
static PSymbolStack FirstStack;
static PCToken MomSection;
static char *LastGlobSymbol;
static PFunction FirstFunction;	        /* Liste definierter Funktionen */

void AsmParsInit(void)
{
  FirstSymbol = NULL;

  FirstLocSymbol = NULL; MomLocHandle = -1; SetMomSection(-1);
  FirstSection = NULL;
  FirstLocHandle = NULL;
  FirstStack = NULL;
  FirstRegDef = NULL;
  FirstFunction = NULL;
  DoRefs = True;
  RadixBase = 10;
  OutRadixBase = 16;
}


Boolean RangeCheck(LargeInt Wert, IntType Typ)
{
#ifndef HAS64
  if (((int)Typ) >= ((int)SInt32))
    return True;
#else
  if (((int)Typ) >= ((int)Int64))
    return True;
#endif
  else
    return ((Wert >= IntTypeDefs[(int)Typ].Min) && (Wert <= IntTypeDefs[(int)Typ].Max));
}

Boolean FloatRangeCheck(Double Wert, FloatType Typ)
{
  switch (Typ)
  {
    case Float32:
      return (fabs(Wert) <= 3.4e38);
    case Float64:
      return (fabs(Wert) <= 1.7e308);
/**     case FloatCo: return fabs(Wert) <= 9.22e18; */
    case Float80:
      return True;
    case FloatDec:
      return True;
    default:
      return False;
  }
/**   if (Typ == FloatDec) && (fabs(Wert) > 1e1000) WrError(ErrNum_BigDecFloat);**/
}

Boolean SingleBit(LargeInt Inp, LargeInt *Erg)
{
  *Erg = 0;
  do
  {
    if (!Odd(Inp))
      (*Erg)++;
    if (!Odd(Inp))
      Inp = Inp >> 1;
  }
  while ((*Erg != LARGEBITS) && (!Odd(Inp)));
  return (*Erg != LARGEBITS) && (Inp == 1);
}	

IntType GetSmallestUIntType(LargeWord MaxValue)
{
  IntType Result;

  Result = (IntType) 0;
  for (Result = (IntType) 0; Result < IntTypeCnt; Result++)
  {
    if (IntTypeDefs[Result].Min < 0)
      continue;
    if (IntTypeDefs[Result].Max >= (LargeInt)MaxValue)
      return Result;
  }
  return UInt32;
}

static Boolean ProcessBk(char **Start, char *Erg)
{
  LongInt System = 0, Acc = 0, Digit = 0;
  char ch;
  int cnt;
  Boolean Finish;

  switch (mytoupper(**Start))
  {
    case '\'': case '\\': case '"':
      *Erg = **Start;
      (*Start)++;
      return True;
    case 'H':
      *Erg = '\'';
      (*Start)++;
      return True;
    case 'I':
      *Erg = '"';
      (*Start)++;
    return True;
    case 'B':
      *Erg = Char_BS;
      (*Start)++;
      return True;
    case 'A':
      *Erg = Char_BEL;
      (*Start)++;
      return True;
    case 'E':
      *Erg = Char_ESC;
      (*Start)++;
       return True;
    case 'T':
      *Erg = Char_HT;
      (*Start)++;
       return True;
    case 'N':
      *Erg = Char_LF;
      (*Start)++;
      return True;
    case 'R':
      *Erg = Char_CR;
      (*Start)++;
      return True;
    case 'X':
      System = 16;
      (*Start)++;
      /* fall-through */
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      if (System == 0)
        System = (**Start == '0') ? 8 : 10;
      cnt = (System == 16) ? 1 : ((System == 10) ? 0 : -1);
      do
      {
        ch = mytoupper(**Start);
        Finish = False;
        if ((ch >= '0') && (ch <= '9'))
          Digit = ch - '0';
        else if ((System == 16) && (ch >= 'A') && (ch <= 'F'))
          Digit = (ch - 'A') + 10;
        else
          Finish = True;
        if (!Finish)
        {
          (*Start)++;
          cnt++;
          if (Digit >= System)
          {
            WrError(ErrNum_OverRange);
            return False;
          }
          Acc = (Acc * System) + Digit;
        }
      }
      while ((!Finish) && (cnt < 3));
      if (!ChkRange(Acc, 0, 255))
        return False;
      *Erg = Acc;
      return True;
    default:
      WrError(ErrNum_InvEscSequence);
      return False;
  }
}

/*!------------------------------------------------------------------------
 * \fn     DynString2Int(const struct sDynString *pDynString)
 * \brief  convert string to its "ASCII representation"
 * \param  pDynString string containing characters
 * \return -1 or converted int
 * ------------------------------------------------------------------------ */

LargeInt DynString2Int(const struct sDynString *pDynString)
{
  if ((pDynString->Length > 0) && (pDynString->Length <= 4))
  {
    const char *pRun;
    Byte Digit;
    LargeInt Result;

    Result = 0;
    for (pRun = pDynString->Contents;
         pRun < pDynString->Contents + pDynString->Length;
         pRun++)
    {
      Digit = (usint) *pRun;
      Result = (Result << 8) | CharTransTable[Digit & 0xff];
    }
    return Result;
  }
  return -1;
}

Boolean Int2DynString(struct sDynString *pDynString, LargeInt Src)
{
  int Search;
  Byte Digit;
  char *pDest = &pDynString->Contents[sizeof(pDynString->Contents)];

  pDynString->Length = 0;
  while (Src)
  {
    Digit = Src & 0xff;
    Src = (Src >> 8) & 0xfffffful;
    for (Search = 0; Search < 256; Search++)
      if (CharTransTable[Search] == Digit)
      {
        *(--pDest) = Search;
        pDynString->Length++;
        break;
      }
  }
  memmove(pDynString->Contents, pDest, pDynString->Length);
  return True;
}

/*!------------------------------------------------------------------------
 * \fn     TempResultToInt(TempResult *pResult)
 * \brief  convert TempResult to integer
 * \param  pResult tempresult to convert
 * \return 0 or error code
 * ------------------------------------------------------------------------ */

int TempResultToInt(TempResult *pResult)
{
  switch (pResult->Typ)
  {
    case TempInt:
      break;
    case TempString:
    {
      LargeInt Result = DynString2Int(&pResult->Contents.Ascii);
      if (Result >= 0)
      {
        pResult->Typ = TempInt;
        pResult->Contents.Int = Result;
        break;
      }
      /* else */
    }
    /* fall-through */
    default:
      pResult->Typ = TempNone;
      return -1;
  }
  return 0;
}

/*!------------------------------------------------------------------------
 * \fn     MultiCharToInt(TempResult *pResult, unsigned MaxLen)
 * \brief  optionally convert multi-character constant to integer
 * \param  pResult holding value
 * \param  MaxLen maximum lenght of multi-character constant
 * \return True if converted
 * ------------------------------------------------------------------------ */

Boolean MultiCharToInt(TempResult *pResult, unsigned MaxLen)
{
  if ((pResult->Contents.Ascii.Length <= MaxLen) && (pResult->Flags & eSymbolFlag_StringSingleQuoted))
  {
    TempResultToInt(pResult);
    return True;
  }
  return False;
}

/*!------------------------------------------------------------------------
 * \fn     ExpandStrSymbol(char *pDest, unsigned DestSize, const tStrComp *pSrc)
 * \brief  expand symbol name from string component
 * \param  pDest dest buffer
 * \param  DestSize size of dest buffer
 * \param  pSrc source component
 * \return True if success
 * ------------------------------------------------------------------------ */

Boolean ExpandStrSymbol(char *pDest, unsigned DestSize, const tStrComp *pSrc)
{
  tStrComp SrcComp;
  const char *pStart;

  *pDest = '\0'; StrCompRefRight(&SrcComp, pSrc, 0);
  while (True)
  {
    pStart = strchr(SrcComp.Str, '{');
    if (pStart)
    {
      unsigned ls = pStart - SrcComp.Str, ld = strlen(pDest);
      String Expr, Result;
      tStrComp ExprComp;
      Boolean OK;
      const char *pStop;

      if (ld + ls + 1 > DestSize)
        ls = DestSize - 1 - ld;
      memcpy(pDest + ld, SrcComp.Str, ls);
      pDest[ld + ls] = '\0';

      pStop = QuotPos(pStart + 1, '}');
      if (!pStop)
      {
        WrStrErrorPos(ErrNum_InvSymName, pSrc);
        return False;
      }
      StrCompMkTemp(&ExprComp, Expr);
      StrCompCopySub(&ExprComp, &SrcComp, pStart + 1 - SrcComp.Str, pStop - pStart - 1);
      FirstPassUnknown = False;
      EvalStrStringExpression(&ExprComp, &OK, Result);
      if (!OK)
        return False;
      if (FirstPassUnknown)
      {
        WrStrErrorPos(ErrNum_FirstPassCalc, &ExprComp);
        return False;
      }
      if (!CaseSensitive)
        UpString(Result);
      strmaxcat(pDest, Result, DestSize);
      StrCompIncRefLeft(&SrcComp, pStop + 1 - SrcComp.Str);
    }
    else
    {
      strmaxcat(pDest, SrcComp.Str, DestSize);
      return True;
    }
  }
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
/* check whether this is a local symbol and expand local counter if yes.  They
   have to be handled in different places of the parser, therefore two separate
   functions */

void InitTmpSymbols(void)
{
  TmpSymCounter = FwdSymCounter = BackSymCounter = 0;
  *TmpSymCounterVal = '\0';
  TmpSymLogDepth = 0;
  *LastGlobSymbol = '\0';
}

static void AddTmpSymLog(Boolean Back, LongInt Counter)
{
  /* shift out oldest value */

  if (TmpSymLogDepth)
  {
    LongInt ShiftCnt = min(TmpSymLogDepth, LOCSYMSIGHT - 1);

    memmove(TmpSymLog + 1, TmpSymLog, sizeof(TTmpSymLog) * (ShiftCnt));
  }

  /* insert new one */

  TmpSymLog[0].Back = Back;
  TmpSymLog[0].Counter = Counter;
  if (TmpSymLogDepth < LOCSYMSIGHT)
    TmpSymLogDepth++;
}

static Boolean ChkTmp1(char *Name, Boolean Define)
{
  char *Src, *Dest;
  Boolean Result = FALSE;

  /* $$-Symbols: append current $$-counter */

  if (!strncmp(Name, "$$", 2))
  {
    /* manually copy since this will implicitly give us the point to append
       the number */

    for (Src = Name + 2, Dest = Name; *Src; *(Dest++) = *(Src++));

    /* append number. only generate the number once */

    if (*TmpSymCounterVal == '\0')
      as_snprintf(TmpSymCounterVal, sizeof(TmpSymCounterVal), "%d", TmpSymCounter);
    strcpy(Dest, TmpSymCounterVal);
    Result = TRUE;
  }

  /* no special local symbol: increment $$-counter */

  else if (Define)
  {
    TmpSymCounter++;
    *TmpSymCounterVal = '\0';
  }

  return Result;
}

static Boolean ChkTmp2(char *pDest, const char *pSrc, Boolean Define)
{
  const char *pRun, *pBegin, *pEnd;
  int Cnt;
  Boolean Result = FALSE;

  for (pBegin = pSrc; myisspace(*pBegin); pBegin++);
  for (pEnd = pSrc + strlen(pSrc); (pEnd > pBegin) && myisspace(*(pEnd - 1)); pEnd--);

  /* Note: We have to deal with three symbol definitions:

      "-" for backward-only referencing
      "+" for forward-only referencing
      "/" for either way of referencing

      "/" and "+" are both expanded to forward symbol names, so the
      forward refencing to both types is unproblematic, however
      only "/" and "-" are stored in the backlog of the three
      most-recent symbols for backward referencing.
  */

  /* backward references ? */

  if (*pBegin == '-')
  {
    for (pRun = pBegin; *pRun; pRun++)
      if (*pRun != '-')
        break;
    Cnt = pRun - pBegin;
    if (pRun == pEnd)
    {
      if ((Define) && (Cnt == 1))
      {
        as_snprintf(pDest, STRINGSIZE, "__back%d", (int)BackSymCounter);
        AddTmpSymLog(TRUE, BackSymCounter);
        BackSymCounter++;
        Result = TRUE;
      }

      /* TmpSymLogDepth cannot become larger than LOCSYMSIGHT, so we only
         have to check against the log's actual depth. */

      else if (Cnt <= TmpSymLogDepth)
      {
        Cnt--;
        as_snprintf(pDest, STRINGSIZE, "__%s%d",
                    TmpSymLog[Cnt].Back ? "back" : "forw",
                    (int)TmpSymLog[Cnt].Counter);
        Result = TRUE;
      }
    }
  }

  /* forward references ? */

  else if (*pBegin == '+')
  {
    for (pRun = pBegin; *pRun; pRun++)
      if (*pRun != '+')
        break;
    Cnt = pRun - pBegin;
    if (pRun == pEnd)
    {
      if ((Define) && (Cnt == 1))
      {
        as_snprintf(pDest, STRINGSIZE, "__forw%d", (int)FwdSymCounter++);
        Result = TRUE;
      }
      else if (Cnt <= LOCSYMSIGHT)
      {
        as_snprintf(pDest, STRINGSIZE, "__forw%d", (int)(FwdSymCounter + (Cnt - 1)));
        Result = TRUE;
      }
    }
  }

  /* slash: only allowed for definition, but add to log for backward ref. */

  else if ((pEnd - pBegin == 1) && (*pBegin == '/') && Define)
  {
    AddTmpSymLog(FALSE, FwdSymCounter);
    as_snprintf(pDest, STRINGSIZE, "__forw%d", (int)FwdSymCounter);
    FwdSymCounter++;
    Result = TRUE;
  }

  return Result;
}

static Boolean ChkTmp3(char *Name, Boolean Define)
{
  Boolean Result = FALSE;

  if ('.' == *Name)
  {
    String Tmp;

    strmaxcpy(Tmp, LastGlobSymbol, STRINGSIZE);
    strmaxcat(Tmp, Name, STRINGSIZE);
    strmaxcpy(Name, Tmp, STRINGSIZE);

    Result = TRUE;
  }
  else if (Define)
  {
    strmaxcpy(LastGlobSymbol, Name, STRINGSIZE);
  }

  return Result;
}

static Boolean ChkTmp(char *Name, Boolean Define)
{
  Boolean IsTmp1, IsTmp2, IsTmp3;

  IsTmp1 = ChkTmp1(Name, Define);
  IsTmp2 = ChkTmp2(Name, Name, Define);
  IsTmp3 = ChkTmp3(Name, Define && !IsTmp2);
  return IsTmp1 || IsTmp2 || IsTmp3;
}

Boolean IdentifySection(const tStrComp *pName, LongInt *Erg)
{
  PSaveSection SLauf;
  String ExpName;
  sint Depth;

  if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pName))
    return False;
  if (!CaseSensitive)
    NLS_UpString(ExpName);

  if (*ExpName == '\0')
  {
    *Erg = -1;
    return True;
  }
  else if (((strlen(ExpName) == 6) || (strlen(ExpName) == 7))
       && (!as_strncasecmp(ExpName, "PARENT", 6))
       && ((strlen(ExpName) == 6) || ((ExpName[6] >= '0') && (ExpName[6] <= '9'))))
  {
    Depth = (strlen(ExpName) == 6) ? 1 : ExpName[6] - AscOfs;
    SLauf = SectionStack;
    *Erg = MomSectionHandle;
    while ((Depth > 0) && (*Erg != -2))
    {
      if (!SLauf) *Erg = -2;
      else
      {
        *Erg = SLauf->Handle;
        SLauf = SLauf->Next;
      }
      Depth--;
    }
    if (*Erg == -2)
    {
      WrError(ErrNum_InvSection);
      return False;
    }
    else
      return True;
  }
  else if (!strcmp(ExpName, GetSectionName(MomSectionHandle)))
  {
    *Erg = MomSectionHandle;
    return True;
  }
  else
  {
    SLauf = SectionStack;
    while ((SLauf) && (strcmp(GetSectionName(SLauf->Handle), ExpName)))
      SLauf = SLauf->Next;
    if (!SLauf)
    {
      WrError(ErrNum_InvSection);
      return False;
    }
    else
    {
      *Erg = SLauf->Handle;
      return True;
    }
  }
}

static Boolean GetSymSection(char *Name, LongInt *Erg, const tStrComp *pUnexpComp)
{
  String Part;
  tStrComp TmpComp;
  char *q;
  int l = strlen(Name);

  if (Name[l - 1] != ']')
  {
    *Erg = -2;
    return True;
  }

  Name[l - 1] = '\0';
  q = RQuotPos(Name, '[');
  Name[l - 1] = ']';
  if (Name + l - q <= 1)
  {
    if (pUnexpComp)
      WrStrErrorPos(ErrNum_InvSymName, pUnexpComp);
    else
      WrXError(ErrNum_InvSymName, Name);
    return False;
  }

  Name[l - 1] = '\0';
  strmaxcpy(Part, q + 1, STRINGSIZE);
  *q = '\0';

  StrCompMkTemp(&TmpComp, Part);
  return IdentifySection(&TmpComp, Erg);
}

/*****************************************************************************
 * Function:    ConstIntVal
 * Purpose:     evaluate integer constant
 * Result:      integer value
 *****************************************************************************/

static LargeInt ConstIntVal(const char *pExpr, IntType Typ, Boolean *pResult)
{
  LargeInt Wert;
  int l;
  Boolean NegFlag = False;
  TConstMode ActMode = ConstModeC;
  unsigned BaseIdx;
  int Digit;
  int Base;
  char ch;
  Boolean Found;


  /* empty string is interpreted as 0 */

  if (!*pExpr)
  {
    *pResult = True;
    return 0;
  }

  *pResult = False;
  Wert = 0;

  /* sign: */

  switch (*pExpr)
  {
    case '-':
      NegFlag = True;
      /* else fall-through */
    case '+':
      pExpr++;
      break;
  }
  l = strlen(pExpr);

  /* automatic syntax determination: */

  if (RelaxedMode)
  {
    Found = False;

    if ((l >= 2) && (*pExpr == '0') && (mytoupper(pExpr[1]) == 'X'))
    {
      ActMode = ConstModeC;
      Found = True;
    }

    if ((!Found) && (l >= 2))
    {
      for (BaseIdx = 0; BaseIdx < 3; BaseIdx++)
        if (*pExpr == BaseIds[BaseIdx])
        {
          ActMode = ConstModeMoto;
          Found = True;
          break;
        }
    }

    if ((!Found) && (l >= 2) && (*pExpr >= '0') && (*pExpr <= '9'))
    {
      ch = mytoupper(pExpr[l - 1]);
      if (DigitVal(ch, RadixBase) == -1)
      {
        for (BaseIdx = 0; BaseIdx < sizeof(BaseLetters) / sizeof(*BaseLetters); BaseIdx++)
          if (ch == BaseLetters[BaseIdx])
          {
            ActMode = ConstModeIntel;
            Found = True;
            break;
          }
      }
    }

    if ((!Found) && (l >= 3) && (pExpr[1] == '\'') && (pExpr[l - 1] == '\''))
    {
      switch (mytoupper(*pExpr))
      {
        case 'H':
        case 'X':
        case 'B':
        case 'O':
          ActMode = ConstModeWeird;
          Found = True;
          break;
      }
    }

    if (!Found)
      ActMode = ConstModeC;
  }
  else /* !RelaxedMode */
    ActMode = ConstMode;

  /* Zahlensystem ermitteln/pruefen */

  Base = RadixBase;
  switch (ActMode)
  {
    case ConstModeIntel:
      ch = mytoupper(pExpr[l - 1]);
      if (DigitVal(ch, RadixBase) == -1)
      {
        for (BaseIdx = 0; BaseIdx < sizeof(BaseLetters) / sizeof(*BaseLetters); BaseIdx++)
          if (ch == BaseLetters[BaseIdx])
          {
            Base = BaseVals[BaseIdx];
            l--;
            break;
          }
      }
      break;
    case ConstModeMoto:
      for (BaseIdx = 0; BaseIdx < 3; BaseIdx++)
        if (*pExpr == BaseIds[BaseIdx])
        {
          Base = BaseVals[BaseIdx];
          pExpr++; l--;
          break;
        }
      break;
    case ConstModeC:
      if (!strcmp(pExpr, "0"))
      {
        *pResult = True;
        return 0;
      }
      if (*pExpr != '0') Base = RadixBase;
      else if (l < 2) return -1;
      else
      {
        pExpr++; l--;
        ch = mytoupper(*pExpr);
        if ((RadixBase != 10) && (DigitVal(ch, RadixBase) != -1))
          Base = RadixBase;
        else
          switch (mytoupper(*pExpr))
          {
            case 'X':
              pExpr++;
              l--;
              Base = 16;
              break;
            case 'B':
              pExpr++;
              l--;
              Base = 2;
              break;
            default:
              Base = 8;
          }
      }
      break;
    case ConstModeWeird:
      if (isdigit(*pExpr)) break;
      if ((l < 3) || (pExpr[1] != '\'') || (pExpr[l - 1] != '\''))
        return -1;
      switch (mytoupper(*pExpr))
      {
        case 'X':
        case 'H':
          Base = 16;
          break;
        case 'B':
          Base = 2;
          break;
        case 'O':
          Base = 8;
          break;
        default:
          return -1;
      }
      pExpr += 2;
      l -= 3;
      break;
  }

  if (!*pExpr)
    return -1;

  if (ActMode == ConstModeIntel)
  {
    if ((*pExpr < '0') || (*pExpr > '9'))
      return -1;
  }

  /* we may have decremented l, so do not run until string end */

  while (l > 0)
  {
    Digit = DigitVal(mytoupper(*pExpr), Base);
    if (Digit == -1)
      return -1;
    Wert = Wert * Base + Digit;
    pExpr++; l--;
  }

  if (NegFlag)
    Wert = -Wert;

  /* post-processing, range check */

  *pResult = RangeCheck(Wert, Typ);
  if (*pResult)
    return Wert;
  else if (HardRanges)
  {
    WrError(ErrNum_OverRange);
    return -1;
  }
  else
  {
    *pResult = True;
    WrError(ErrNum_WOverRange);
    return Wert&IntTypeDefs[(int)Typ].Mask;
  }
}

/*****************************************************************************
 * Function:    ConstFloatVal
 * Purpose:     evaluate floating point constant
 * Result:      value
 *****************************************************************************/

static Double ConstFloatVal(const char *pExpr, FloatType Typ, Boolean *pResult)
{
  Double Erg;
  char *pEnd;

  UNUSED(Typ);

  if (*pExpr)
  {
    /* Some strtod() implementations interpret hex constants starting with '0x'.  We
       don't want this here.  Either 0x for hex constants is allowed, then it should
       have been parsed before by ConstIntVal(), or not, then we don't want the constant
       be stored as float. */

    if ((strlen(pExpr) >= 2)
     && (pExpr[0] == '0')
     && (toupper(pExpr[1]) == 'X'))
    {
      Erg = 0;
      *pResult = False;
    }

    else
    {
      Erg = strtod(pExpr, &pEnd);
      *pResult = (*pEnd == '\0');
    }
  }
  else
  {
    Erg = 0.0;
    *pResult = True;
  }
  return Erg;
}

/*****************************************************************************
 * Function:    ConstStringVal
 * Purpose:     evaluate string constant
 * Result:      value
 *****************************************************************************/

static void ConstStringVal(const tStrComp *pExpr, TempResult *pDest, Boolean *pResult)
{
  String CopyStr;
  tStrComp Copy, Remainder;
  char *pPos, QuoteChar;
  int l, TLen;

  StrCompMkTemp(&Copy, CopyStr);
  *pResult = False;

  l = strlen(pExpr->Str);
  if (l < 2)
    return;
  switch (*pExpr->Str)
  {
    case '"':
    case '\'':
      QuoteChar = *pExpr->Str;
      if (pExpr->Str[l - 1] == QuoteChar)
      {
        if ('\'' == QuoteChar)
          pDest->Flags |= eSymbolFlag_StringSingleQuoted;
        break;
      }
      /* conditional fall-through */
    default:
      return;
  }

  StrCompCopy(&Copy, pExpr);
  StrCompIncRefLeft(&Copy, 1);
  StrCompShorten(&Copy, 1);

  /* go through source */

  pDest->Typ = TempNone;
  pDest->Contents.Ascii.Length = 0;
  while (1)
  {
    pPos = strchr(Copy.Str, '\\');
    if (pPos)
      StrCompSplitRef(&Copy, &Remainder, &Copy, pPos);

    /* " before \ -> not a simple string but something like "...." ... " */

    if (strchr(Copy.Str, QuoteChar))
      return;

    /* copy part up to next '\' verbatim: */

    DynStringAppend(&pDest->Contents.Ascii, Copy.Str, strlen(Copy.Str));

    /* are we done? If not, advance pointer to behind '\' */

    if (!pPos)
      break;
    Copy = Remainder;

    /* treat escaped section: stringification? */

    if (*Copy.Str == '{')
    {
      TempResult t;
      char *pStr;
      String Str;

      StrCompIncRefLeft(&Copy, 1);

      /* cut out part in {...} */

      pPos = QuotPos(Copy.Str, '}');
      if (!pPos)
        return;
      StrCompSplitRef(&Copy, &Remainder, &Copy, pPos);
      KillPrefBlanksStrCompRef(&Copy);
      KillPostBlanksStrComp(&Copy);

      /* evaluate expression */

      FirstPassUnknown = False;
      EvalStrExpression(&Copy, &t);
      if (t.Relocs)
      {
        WrStrErrorPos(ErrNum_NoRelocs, &Copy);
        FreeRelocs(&t.Relocs);
        *pResult = True;
        return;
      }

      /* append result */

      switch (t.Typ)
      {
        case TempInt:
          TLen = SysString(Str, sizeof(Str), t.Contents.Int, OutRadixBase, 0, False, HexStartCharacter);
          pStr = Str;
          break;
        case TempFloat:
          FloatString(Str, sizeof(Str), t.Contents.Float);
          pStr = Str;
          TLen = strlen(pStr);
          break;
        case TempString:
          pStr = t.Contents.Ascii.Contents;
          TLen = t.Contents.Ascii.Length;
          break;
        default:
          *pResult = True;
          return;
      }
      DynStringAppend(&pDest->Contents.Ascii, pStr, TLen);

      /* advance source pointer to behind '}' */

      Copy = Remainder;
    }

    /* simple character escape: */

    else
    {
      char Res, *pNext = Copy.Str;

      if (!ProcessBk(&pNext, &Res))
        return;
      DynStringAppend(&pDest->Contents.Ascii, &Res, 1);
      StrCompIncRefLeft(&Copy, pNext - Copy.Str);
    }
  }

  pDest->Typ = TempString;
  *pResult = True;
}


static PSymbolEntry FindLocNode(
#ifdef __PROTOS__
const char *Name, TempType SearchType
#endif
);

static PSymbolEntry FindNode(
#ifdef __PROTOS__
const char *Name, TempType SearchType
#endif
);

/*****************************************************************************
 * Function:    EvalStrExpression
 * Purpose:     evaluate expression
 * Result:      implicitly in pErg
 *****************************************************************************/

#define LEAVE goto func_exit

static tErrorNum DeduceExpectTypeErrMsgMask(unsigned Mask, TempType ActType)
{
  switch (ActType)
  {
    case TempInt:
      switch (Mask)
      {
        case (1 << TempString):
          return ErrNum_StringButInt;
        /* int is convertible to float, so combinations are impossible: */
        case (1 << TempFloat):
        case (1 << TempFloat) | (1 << TempString):
        default:
          return ErrNum_InternalError;
      }
    case TempFloat:
      switch (Mask)
      {
        case (1 << TempInt):
          return ErrNum_IntButFloat;
        case (1 << TempString):
          return ErrNum_StringButFloat;
        case (1 << TempInt) | (1 << TempString):
          return ErrNum_StringOrIntButFloat;
        default:
          return ErrNum_InternalError;
      }
    case TempString:
      switch (Mask)
      {
        case (1 << TempInt):
          return ErrNum_IntButString;
        case (1 << TempFloat):
          return ErrNum_FloatButString;
        case (1 << TempInt) | (1 << TempFloat):
          return ErrNum_IntOrFloatButString;
        default:
          return ErrNum_InternalError;
      }
    default:
      return ErrNum_InternalError;
  }
}

static Byte GetOpTypeMask(Byte TotMask, int OpIndex)
{
  return (TotMask >> (OpIndex * 4)) & 15;
}

static Byte TryConvert(Byte TypeMask, TempType ActType, int OpIndex)
{
  if (TypeMask & ActType)
    return 0 << (4 * OpIndex);
  if ((TypeMask & TempFloat) && (ActType == TempInt))
    return 1 << (4 * OpIndex);
  if ((TypeMask & TempInt) && (ActType == TempString))
    return 2 << (4 * OpIndex);
  if ((TypeMask & TempFloat) && (ActType == TempString))
    return (1|2) << (4 * OpIndex);
  return 255;
}

void EvalStrExpression(const tStrComp *pExpr, TempResult *pErg)
{
  const Operator *pOp;
  const Operator *FOps[OPERATOR_MAXCNT];
  LongInt FOpCnt = 0;

  Boolean OK;
  tStrComp InArgs[3];
  TempResult InVals[3];
  int z1, cnt;
  char Save = '\0';
  sint LKlamm, RKlamm, WKlamm, zop;
  sint OpMax, OpPos = -1;
  Boolean InSgl, InDbl, NextEscaped, ThisEscaped;
  PFunction ValFunc;
  tStrComp CopyComp, STempComp;
  String CopyStr, stemp;
  char *KlPos, *zp, *DummyPtr, *pOpPos;
  const tFunction *pFunction;
  PRelocEntry TReloc;

  ChkStack();

  StrCompMkTemp(&CopyComp, CopyStr);
  StrCompMkTemp(&STempComp, stemp);

  if (MakeDebug)
    fprintf(Debug, "Parse '%s'\n", pExpr->Str);

  memset(InVals, 0, sizeof(InVals));

  /* Annahme Fehler */

  pErg->Typ = TempNone;
  pErg->Relocs = NULL;
  pErg->Flags = 0;

  StrCompCopy(&CopyComp, pExpr);
  KillPrefBlanksStrComp(&CopyComp);
  KillPostBlanksStrComp(&CopyComp);

  /* sort out local symbols like - and +++.  Do it now to get them out of the
     formula parser's way. */

  ChkTmp2(CopyComp.Str, CopyComp.Str, FALSE);
  StrCompCopy(&STempComp, &CopyComp);

  /* Programmzaehler ? */

  if ((PCSymbol) && (!as_strcasecmp(CopyComp.Str, PCSymbol)))
  {
    pErg->Typ = TempInt;
    pErg->Contents.Int = EProgCounter();
    pErg->Relocs = NULL;
    LEAVE;
  }

  /* Konstanten ? */

  pErg->Contents.Int = ConstIntVal(CopyComp.Str, (IntType) (IntTypeCnt - 1), &OK);
  if (OK)
  {
    pErg->Typ = TempInt;
    pErg->Relocs = NULL;
    LEAVE;
  }

  pErg->Contents.Float = ConstFloatVal(CopyComp.Str, Float80, &OK);
  if (OK)
  {
    pErg->Typ = TempFloat;
    pErg->Relocs = NULL;
    LEAVE;
  }

  ConstStringVal(&CopyComp, pErg, &OK);
  if (OK)
  {
    pErg->Relocs = NULL;
    LEAVE;
  }

  /* durch Codegenerator gegebene Konstanten ? */

  pErg->Relocs = NULL;
  InternSymbol(CopyComp.Str, pErg);
  if (pErg->Typ != TempNone)
    LEAVE;

  /* find out which operators *might* occur in expression */

  OpMax = 0;
  LKlamm = 0;
  RKlamm = 0;
  WKlamm = 0;
  InSgl =
  InDbl =
  ThisEscaped =
  NextEscaped = False;
  for (pOp = Operators + 1; pOp->Id; pOp++)
  {
    pOpPos = (pOp->IdLen == 1) ? (strchr(CopyComp.Str, *pOp->Id)) : (strstr(CopyComp.Str, pOp->Id));
    if (pOpPos)
      FOps[FOpCnt++] = pOp;
  }

  /* nach Operator hoechster Rangstufe ausserhalb Klammern suchen */

  for (zp = CopyComp.Str; *zp; zp++, ThisEscaped = NextEscaped)
  {
    NextEscaped = False;
    switch (*zp)
    {
      case '(':
        if (!(InSgl || InDbl))
          LKlamm++;
        break;
      case ')':
        if (!(InSgl || InDbl))
          RKlamm++;
        break;
      case '{':
        if (!(InSgl || InDbl))
          WKlamm++;
        break;
      case '}':
        if (!(InSgl || InDbl))
          WKlamm--;
        break;
      case '"':
        if (!InSgl && !ThisEscaped)
          InDbl = !InDbl;
        break;
      case '\'':
        if (!InDbl && !ThisEscaped)
          InSgl = !InSgl;
        break;
      case '\\':
        if ((InDbl || InSgl) && !ThisEscaped)
          NextEscaped = True;
        break;
      default:
        if ((LKlamm == RKlamm) && (WKlamm == 0) && (!InSgl) && (!InDbl))
        {
          Boolean OpFnd = False;
          sint OpLen = 0, LocOpMax = 0;

          for (zop = 0; zop < FOpCnt; zop++)
          {
            pOp = FOps[zop];
            if ((!strncmp(zp, pOp->Id, pOp->IdLen)) && (pOp->IdLen >= OpLen))
            {
              OpFnd = True;
              OpLen = pOp->IdLen;
              LocOpMax = pOp - Operators;
              if (Operators[LocOpMax].Priority >= Operators[OpMax].Priority)
              {
                OpMax = LocOpMax;
                OpPos = zp - CopyComp.Str;
              }
            }
          }
          if (OpFnd)
            zp += Operators[LocOpMax].IdLen - 1;
        }
    }
  }

  /* Klammerfehler ? */

  if (LKlamm != RKlamm)
  {
    WrStrErrorPos(ErrNum_BrackErr, &CopyComp);
    LEAVE;
  }

  /* Operator gefunden ? */

  if (OpMax)
  {
    int ThisArgCnt, CompLen, z, z2;
    Byte ThisOpMatch, BestOpMatch, BestOpMatchIdx, SumCombinations, TypeMask;

    pOp = Operators + OpMax;

    /* Minuszeichen sowohl mit einem als auch 2 Operanden */

    if (!strcmp(pOp->Id, "-"))
    {
      if (!OpPos)
        pOp = &MinusMonadicOperator;
    }

    /* Operandenzahl pruefen */

    CompLen = strlen(CopyComp.Str);
    if (CompLen <= 1)
      ThisArgCnt = 0;
    else if (!OpPos || (OpPos == (int)strlen(CopyComp.Str) - 1))
      ThisArgCnt = 1;
    else
      ThisArgCnt = 2;
    if (!ChkArgCntExtPos(ThisArgCnt, pOp->Dyadic ? 2 : 1, pOp->Dyadic ? 2 : 1, &CopyComp.Pos))
      LEAVE;

    /* Teilausdruecke rekursiv auswerten */

    Save = StrCompSplitRef(&InArgs[0], &InArgs[1], &CopyComp, CopyComp.Str + OpPos);
    StrCompIncRefLeft(&InArgs[1], strlen(pOp->Id) - 1);
    EvalStrExpression(&InArgs[1], &InVals[1]);
    if (pOp->Dyadic)
      EvalStrExpression(&InArgs[0], &InVals[0]);
    else if (InVals[1].Typ == TempFloat)
    {
      InVals[0].Typ = TempFloat;
      InVals[0].Contents.Float = 0.0;
    }
    else
    {
      InVals[0].Typ = TempInt;
      InVals[0].Contents.Int = 0;
      InVals[0].Relocs = NULL;
    }
    CopyComp.Str[OpPos] = Save;

    /* Abbruch, falls dabei Fehler */

    if ((InVals[0].Typ == TempNone) || (InVals[1].Typ == TempNone))
      LEAVE;

    /* relokatible Symbole nur fuer + und - erlaubt */

    if ((OpMax != 12) && (OpMax != 13) && (InVals[0].Relocs || InVals[1].Relocs))
    {
      WrStrErrorPos(ErrNum_NoRelocs, &CopyComp);
      LEAVE;
    }

    /* see whether data types match operator's restrictions: */

    BestOpMatch = 255; BestOpMatchIdx = OPERATOR_MAXCOMB;
    SumCombinations = 0;
    for (z = 0; z < OPERATOR_MAXCOMB; z++)
    {
      if (!pOp->TypeCombinations[z])
        break;
      SumCombinations |= pOp->TypeCombinations[z];

      ThisOpMatch = 0;
      for (z2 = pOp->Dyadic ? 0 : 1; z2 < 2; z2++)
        ThisOpMatch |= TryConvert(GetOpTypeMask(pOp->TypeCombinations[z], z2), InVals[z2].Typ, z2);
      if (ThisOpMatch < BestOpMatch)
      {
        BestOpMatch = ThisOpMatch;
        BestOpMatchIdx = z;
      }
      if (!BestOpMatch)
        break;
    }

    /* did not find a way to satisfy restrictions, even by conversions? */

    if (BestOpMatch >= 255)
    {
      for (z2 = pOp->Dyadic ? 0 : 1; z2 < 2; z2++)
      {
        TypeMask = GetOpTypeMask(SumCombinations, z2);
        if (!(TypeMask & InVals[z2].Typ))
          WrStrErrorPos(DeduceExpectTypeErrMsgMask(TypeMask, InVals[z2].Typ), &InArgs[z2]);
      }
      LEAVE;
    }

    /* necessary conversions: */

    for (z2 = pOp->Dyadic ? 0 : 1; z2 < 2; z2++)
    {
      TypeMask = (BestOpMatch >> (z2 * 4)) & 15;
      if (TypeMask & 2)  /* String -> Int */
        TempResultToInt(&InVals[z2]);
      if (TypeMask & 1) /* Int -> Float */
        TempResultToFloat(&InVals[z2]);
    }

    /* actual operation */

    (void)BestOpMatchIdx;
    pOp->pFunc(pErg, &InVals[0], &InVals[1]);
    LEAVE;
  } /* if (OpMax) */

  /* kein Operator gefunden: Klammerausdruck ? */

  if (LKlamm != 0)
  {
    tStrComp FName, FArg, Remainder;
    
    /* erste Klammer suchen, Funktionsnamen abtrennen */

    KlPos = strchr(CopyComp.Str, '(');

    /* Funktionsnamen abschneiden */

    StrCompSplitRef(&FName, &FArg, &CopyComp, KlPos);
    StrCompShorten(&FArg, 1);
    KillPostBlanksStrComp(&FName);

    /* Nullfunktion: nur Argument */

    if (*FName.Str == '\0')
    {
      EvalStrExpression(&FArg, &InVals[0]);
      *pErg = InVals[0];
      LEAVE;
    }

    /* selbstdefinierte Funktion ? */

    ValFunc = FindFunction(FName.Str);
    if (ValFunc)
    {
      String CompArgStr;
      tStrComp CompArg;

      StrCompMkTemp(&CompArg, CompArgStr);
      strmaxcpy(CompArg.Str, ValFunc->Definition, STRINGSIZE);
      for (z1 = 1; z1 <= ValFunc->ArguCnt; z1++)
      {
        if (!*FArg.Str)
        {
          WrError(ErrNum_InvFuncArgCnt);
          LEAVE;
        }

        KlPos = QuotPos(FArg.Str, ',');
        if (KlPos)
          StrCompSplitRef(&FArg, &Remainder, &FArg, KlPos);

        EvalStrExpression(&FArg, &InVals[0]);
        if (InVals[0].Relocs)
        {
          WrStrErrorPos(ErrNum_NoRelocs, &FArg);
          FreeRelocs(&InVals[0].Relocs);
          return;
        }

        if (KlPos)
          FArg = Remainder;
        else
          StrCompReset(&FArg);

        strmaxcpy(stemp, "(", STRINGSIZE);
        if (TempResultToPlainString(stemp + 1, &InVals[0], STRINGSIZE - 1))
          LEAVE;
        strmaxcat(stemp,")", STRINGSIZE);
        ExpandLine(stemp, z1, CompArg.Str, sizeof(CompArgStr));
      }
      if (*FArg.Str)
      {
        WrError(ErrNum_InvFuncArgCnt);
        LEAVE;
      }
      EvalStrExpression(&CompArg, pErg);
      LEAVE;
    }

    /* hier einmal umwandeln ist effizienter */

    NLS_UpString(FName.Str);

    /* symbolbezogene Funktionen */

    if (!strcmp(FName.Str, "SYMTYPE"))
    {
      pErg->Typ = TempInt;
      pErg->Contents.Int = FindRegDef(FArg.Str, &DummyPtr) ? 0x80 : GetSymbolType(&FArg);
      LEAVE;
    }

    else if (!strcmp(FName.Str, "DEFINED"))
    {
      pErg->Typ = TempInt;
      if (FindRegDef(FArg.Str, &DummyPtr))
        pErg->Contents.Int = 1;
      else
        pErg->Contents.Int = !!IsSymbolDefined(&FArg);
      LEAVE;
    }

    else if (!strcmp(FName.Str, "ASSUMEDVAL"))
    {
      unsigned IdxAssume;

      for (IdxAssume = 0; IdxAssume < ASSUMERecCnt; IdxAssume++)
        if (!as_strcasecmp(FArg.Str, pASSUMERecs[IdxAssume].Name))
        {
          pErg->Typ = TempInt;
          pErg->Contents.Int = *(pASSUMERecs[IdxAssume].Dest);
          LEAVE;
        }
      WrStrErrorPos(ErrNum_SymbolUndef, &FArg);
      LEAVE;
    }

    /* Unterausdruck auswerten (interne Funktionen maxmimal mit drei Argumenten) */

    cnt = 0;
    do
    {
      zp = QuotPos(FArg.Str, ',');
      if (zp)
        StrCompSplitRef(&InArgs[cnt], &Remainder, &FArg, zp);
      else
        InArgs[cnt] = FArg;
      if (cnt < 3)
      {
        EvalStrExpression(&InArgs[cnt], &InVals[cnt]);
        if (InVals[cnt].Typ == TempNone)
          LEAVE;
        TReloc = InVals[cnt].Relocs;
      }
      else
      {
        WrError(ErrNum_InvFuncArgCnt);
        LEAVE;
      }
      if (TReloc)
      {
        WrStrErrorPos(ErrNum_NoRelocs, &InArgs[cnt]);
        FreeRelocs(&TReloc);
        LEAVE;
      }
      if (zp)
        FArg = Remainder;
      cnt++;
    }
    while (zp);

    /* search function */

    for (pFunction = Functions; pFunction->pName; pFunction++)
      if (!strcmp(FName.Str, pFunction->pName))
        break;
    if (!pFunction->pName)
    {
      WrStrErrorPos(ErrNum_UnknownFunc, &FName);
      LEAVE;
    }

    /* argument checking */

    if ((cnt < pFunction->MinNumArgs) || (cnt > pFunction->MaxNumArgs))
    {
      WrError(ErrNum_InvFuncArgCnt);
      LEAVE;
    }
    for (z1 = 0; z1 < cnt; z1++)
    {
      if ((InVals[z1].Typ == TempInt) && (!(pFunction->ArgTypes[z1] & (1 << TempInt))))
        TempResultToFloat(&InVals[z1]);
      if (!(pFunction->ArgTypes[z1] & (1 << InVals[z1].Typ)))
      {
        WrStrErrorPos(DeduceExpectTypeErrMsgMask(pFunction->ArgTypes[z1], InVals[z1].Typ), &InArgs[z1]);
        LEAVE;
      }
    }
    pFunction->pFunc(pErg, InVals, cnt);
    LEAVE;
  }

  /* nichts dergleichen, dann einfaches Symbol: urspruenglichen Wert wieder
     herstellen, dann Pruefung auf $$-temporaere Symbole */

  StrCompCopy(&CopyComp, &STempComp);
  KillPrefBlanksStrComp(&CopyComp);
  KillPostBlanksStrComp(&CopyComp);

  ChkTmp1(CopyComp.Str, FALSE);

  /* interne Symbole ? */

  if (!as_strcasecmp(CopyComp.Str, "MOMFILE"))
  {
    pErg->Typ = TempString;
    CString2DynString(&pErg->Contents.Ascii, CurrFileName);
    LEAVE;
  }

  if (!as_strcasecmp(CopyComp.Str, "MOMLINE"))
  {
    pErg->Typ = TempInt;
    pErg->Contents.Int = CurrLine;
    LEAVE;
  }

  if (!as_strcasecmp(CopyComp.Str, "MOMPASS"))
  {
    pErg->Typ = TempInt;
    pErg->Contents.Int = PassNo;
    LEAVE;
  }

  if (!as_strcasecmp(CopyComp.Str, "MOMSECTION"))
  {
    pErg->Typ = TempString;
    CString2DynString(&pErg->Contents.Ascii, GetSectionName(MomSectionHandle));
    LEAVE;
  }

  if (!as_strcasecmp(CopyComp.Str, "MOMSEGMENT"))
  {
    pErg->Typ = TempString;
    CString2DynString(&pErg->Contents.Ascii, SegNames[ActPC]);
    LEAVE;
  }

  /* plain symbol */

  LookupSymbol(&CopyComp, pErg, True, TempAll);

func_exit:

  for (z1 = 0; z1 < 3; z1++)
    if (InVals[z1].Relocs)
      FreeRelocs(&InVals[z1].Relocs);
}

void EvalExpression(const char *pExpr, TempResult *pErg)
{
  tStrComp Expr;

  StrCompMkTemp(&Expr, (char*)pExpr);
  EvalStrExpression(&Expr, pErg);
}

LargeInt EvalStrIntExpressionWithFlags(const tStrComp *pComp, IntType Type, Boolean *pResult, tSymbolFlags *pFlags)
{
  TempResult t;
  LargeInt Result;

  *pResult = False;
  TypeFlag = 0;
  SizeFlag = -1;
  UsesForwards = False;
  SymbolQuestionable = False;
  FirstPassUnknown = False;
  if (pFlags)
    *pFlags = 0;

  EvalStrExpression(pComp, &t);
  SetRelocs(t.Relocs);
  switch (t.Typ)
  {
    case TempInt:
      Result = t.Contents.Int;
      if (pFlags)
        *pFlags = t.Flags;
      break;
    case TempString:
    {
      int l = t.Contents.Ascii.Length;

      if ((l > 0) && (l <= 4))
      {
        char *pRun;
        Byte Digit;

        Result = 0;
        for (pRun = t.Contents.Ascii.Contents;
             pRun < t.Contents.Ascii.Contents + l;
             pRun++)
        {
          Digit = (usint) *pRun;
          Result = (Result << 8) | CharTransTable[Digit & 0xff];
        }
        break;
      }
    }
    /* else fall-through */
    default:
      if (t.Typ != TempNone)
        WrStrErrorPos(DeduceExpectTypeErrMsgMask((1 << TempInt) | (1 << TempString), t.Typ), pComp);
      FreeRelocs(&LastRelocs);
      return -1;
  }

  if (FirstPassUnknown)
    Result &= IntTypeDefs[(int)Type].Mask;

  if (!RangeCheck(Result, Type))
  {
    if (HardRanges)
    {
      FreeRelocs(&LastRelocs);
      WrStrErrorPos(ErrNum_OverRange, pComp);
      return -1;
    }
    else
    {
      WrStrErrorPos(ErrNum_WOverRange, pComp);
      *pResult = True;
      return Result & IntTypeDefs[(int)Type].Mask;
    }
  }
  else
  {
    *pResult = True;
    return Result;
  }
}

LargeInt EvalStrIntExpressionOffsWithFlags(const tStrComp *pExpr, int Offset, IntType Type, Boolean *pResult, tSymbolFlags *pFlags)
{
  if (Offset)
  {
    tStrComp Comp;

    StrCompRefRight(&Comp, pExpr, Offset);
    return EvalStrIntExpressionWithFlags(&Comp, Type, pResult, pFlags);
  }
  else
    return EvalStrIntExpressionWithFlags(pExpr, Type, pResult, pFlags);
}

Double EvalStrFloatExpression(const tStrComp *pExpr, FloatType Type, Boolean *pResult)
{
  TempResult t;

  *pResult = False;
  TypeFlag = 0; SizeFlag = -1;
  UsesForwards = False;
  SymbolQuestionable = False;
  FirstPassUnknown = False;

  EvalStrExpression(pExpr, &t);
  switch (t.Typ)
  {
    case TempNone:
      return -1;
    case TempInt:
      t.Contents.Float = t.Contents.Int;
      break;
    case TempString:
    {
      WrStrErrorPos(ErrNum_FloatButString, pExpr);
      return -1;
    }
    default:
      break;
  }

  if (!FloatRangeCheck(t.Contents.Float, Type))
  {
    WrStrErrorPos(ErrNum_OverRange, pExpr);
    return -1;
  }

  *pResult = True;
  return t.Contents.Float;
}

void EvalStrStringExpression(const tStrComp *pExpr, Boolean *pResult, char *pEvalResult)
{
  TempResult t;

  *pResult = False;
  TypeFlag = 0;
  SizeFlag = -1;
  UsesForwards = False;
  SymbolQuestionable = False;
  FirstPassUnknown = False;

  EvalStrExpression(pExpr, &t);
  if (t.Typ != TempString)
  {
    *pEvalResult = '\0';
    if (t.Typ != TempNone)
    {
      if (FirstPassUnknown)
      {
        *pEvalResult = '\0';
        *pResult = True;
      }
      else
        WrStrErrorPos(DeduceExpectTypeErrMsgMask(1 << TempString, t.Typ), pExpr);
    }
  }
  else
  {
    DynString2CString(pEvalResult, &t.Contents.Ascii, STRINGSIZE);
    *pResult = True;
  }
}


/*!------------------------------------------------------------------------
 * \fn     GetIntelSuffix(unsigned Radix)
 * \brief  return Intel-style suffix letter fitting to number system
 * \param  Radix req'd number system
 * \return * to suffix string (may be empty)
 * ------------------------------------------------------------------------ */

const char *GetIntelSuffix(unsigned Radix)
{
  unsigned BaseIdx;

  for (BaseIdx = 0; BaseIdx < sizeof(BaseLetters) / sizeof(*BaseLetters); BaseIdx++)
    if (Radix == BaseVals[BaseIdx])
    {
      static char Result[2] = { '\0', '\0' };

      Result[0] = BaseLetters[BaseIdx] + (HexStartCharacter - 'A');
      return Result;
    }
  return "";
}


static void FreeSymbolEntry(PSymbolEntry *Node, Boolean Destroy)
{
  PCrossRef Lauf;

  if ((*Node)->Tree.Name)
  {
    free((*Node)->Tree.Name);
   (*Node)->Tree.Name = NULL;
  }

  if ((*Node)->SymWert.Typ == TempString)
    free((*Node)->SymWert.Contents.String.Contents);

  while ((*Node)->RefList)
  {
    Lauf = (*Node)->RefList->Next;
    free((*Node)->RefList);
    (*Node)->RefList = Lauf;
  }

  FreeRelocs(&((*Node)->Relocs));

  if (Destroy)
  {
    free(*Node);
    Node = NULL;
  }
}

static char *serr, *snum;
typedef struct
{
  Boolean MayChange, DoCross;
} TEnterStruct, *PEnterStruct;

static Boolean SymbolAdder(PTree *PDest, PTree Neu, void *pData)
{
  PSymbolEntry NewEntry = (PSymbolEntry)Neu, *Node;
  PEnterStruct EnterStruct = (PEnterStruct) pData;

  /* added to an empty leaf ? */

  if (!PDest)
  {
    NewEntry->Defined = True;
    NewEntry->Used = False;
    NewEntry->Changeable = EnterStruct->MayChange;
    NewEntry->RefList = NULL;
    if (EnterStruct->DoCross)
    {
      NewEntry->FileNum = GetFileNum(CurrFileName);
      NewEntry->LineNum = CurrLine;
    }
    return True;
  }

  /* replace en entry: check for validity */

  Node = (PSymbolEntry*)PDest;

  /* tried to redefine a symbol with EQU ? */

  if (((*Node)->Defined) && (!(*Node)->Changeable) && (!EnterStruct->MayChange))
  {
    strmaxcpy(serr, (*Node)->Tree.Name, STRINGSIZE);
    if (EnterStruct->DoCross)
      as_snprcatf(serr, STRINGSIZE, ",%s %s:%ld",
                  getmessage(Num_PrevDefMsg),
                  GetFileName((*Node)->FileNum), (long)((*Node)->LineNum));
    WrXError(ErrNum_DoubleDef, serr);
    FreeSymbolEntry(&NewEntry, TRUE);
    return False;
  }

  /* tried to reassign a constant (EQU) a value with SET and vice versa ? */

  else if ( ((*Node)->Defined) && (EnterStruct->MayChange != (*Node)->Changeable) )
  {
    strmaxcpy(serr, (*Node)->Tree.Name, STRINGSIZE);
    if (EnterStruct->DoCross)
      as_snprcatf(serr, STRINGSIZE, ",%s %s:%ld",
                  getmessage(Num_PrevDefMsg),
                  GetFileName((*Node)->FileNum), (long)((*Node)->LineNum));
    WrXError((*Node)->Changeable ? 2035 : 2030, serr);
    FreeSymbolEntry(&NewEntry, TRUE);
    return False;
  }

  else
  {
    if (!EnterStruct->MayChange)
    {
      if ((NewEntry->SymWert.Typ != (*Node)->SymWert.Typ)
       || ((NewEntry->SymWert.Typ == TempString) && (strlencmp(NewEntry->SymWert.Contents.String.Contents, NewEntry->SymWert.Contents.String.Length, (*Node)->SymWert.Contents.String.Contents, (*Node)->SymWert.Contents.String.Length)))
       || ((NewEntry->SymWert.Typ == TempFloat ) && (NewEntry->SymWert.Contents.FWert != (*Node)->SymWert.Contents.FWert))
       || ((NewEntry->SymWert.Typ == TempInt   ) && (NewEntry->SymWert.Contents.IWert != (*Node)->SymWert.Contents.IWert)))
       {
         if ((!Repass) && (JmpErrors>0))
         {
           if (ThrowErrors)
             ErrorCount -= JmpErrors;
           JmpErrors = 0;
         }
         Repass = True;
         if ((MsgIfRepass) && (PassNo >= PassNoForMessage))
         {
           strmaxcpy(serr, Neu->Name, STRINGSIZE);
           if (Neu->Attribute != -1)
           {
             strmaxcat(serr, "[", STRINGSIZE);
             strmaxcat(serr, GetSectionName(Neu->Attribute), STRINGSIZE);
             strmaxcat(serr, "]", STRINGSIZE);
           }
           WrXError(ErrNum_PhaseErr, serr);
         }
       }
    }
    if (EnterStruct->DoCross)
    {
      NewEntry->LineNum = (*Node)->LineNum;
      NewEntry->FileNum = (*Node)->FileNum;
    }
    NewEntry->RefList = (*Node)->RefList;
    (*Node)->RefList = NULL;
    NewEntry->Defined = True;
    NewEntry->Used = (*Node)->Used;
    NewEntry->Changeable = EnterStruct->MayChange;
    FreeSymbolEntry(Node, False);
    return True;
  }
}

static void EnterLocSymbol(PSymbolEntry Neu)
{
  TEnterStruct EnterStruct;
  PTree TreeRoot;

  Neu->Tree.Attribute = MomLocHandle;
  if (!CaseSensitive)
    NLS_UpString(Neu->Tree.Name);
  EnterStruct.MayChange = EnterStruct.DoCross = FALSE;
  TreeRoot = &FirstLocSymbol->Tree;
  EnterTree(&TreeRoot, (&Neu->Tree), SymbolAdder, &EnterStruct);
  FirstLocSymbol = (PSymbolEntry)TreeRoot;
}

static void EnterSymbol_Search(PForwardSymbol *Lauf, PForwardSymbol *Prev,
                               PForwardSymbol **RRoot, PSymbolEntry Neu,
                               PForwardSymbol *Root, Byte ResCode, Byte *SearchErg)
{
  *Lauf = (*Root);
  *Prev = NULL;
  *RRoot = Root;
  while ((*Lauf) && (strcmp((*Lauf)->Name, Neu->Tree.Name)))
  {
    *Prev = (*Lauf);
    *Lauf = (*Lauf)->Next;
  }
  if (*Lauf)
    *SearchErg = ResCode;
}

static void EnterSymbol(PSymbolEntry Neu, Boolean MayChange, LongInt ResHandle)
{
  PForwardSymbol Lauf, Prev;
  PForwardSymbol *RRoot;
  Byte SearchErg;
  String CombName;
  PSaveSection RunSect;
  LongInt MSect;
  PSymbolEntry Copy;
  TEnterStruct EnterStruct;
  PTree TreeRoot = &(FirstSymbol->Tree);

  if (!CaseSensitive)
    NLS_UpString(Neu->Tree.Name);

  SearchErg = 0;
  EnterStruct.MayChange = MayChange;
  EnterStruct.DoCross = MakeCrossList;
  Neu->Tree.Attribute = (ResHandle == -2) ? MomSectionHandle : ResHandle;
  if ((SectionStack) && (Neu->Tree.Attribute == MomSectionHandle))
  {
    EnterSymbol_Search(&Lauf, &Prev, &RRoot, Neu, &(SectionStack->LocSyms),
                       1, &SearchErg);
    if (!Lauf)
      EnterSymbol_Search(&Lauf, &Prev, &RRoot, Neu,
                         &(SectionStack->GlobSyms), 2, &SearchErg);
    if (!Lauf)
      EnterSymbol_Search(&Lauf, &Prev, &RRoot, Neu,
                         &(SectionStack->ExportSyms), 3, &SearchErg);
    if (SearchErg == 2)
      Neu->Tree.Attribute = Lauf->DestSection;
    if (SearchErg == 3)
    {
      strmaxcpy(CombName, Neu->Tree.Name, STRINGSIZE);
      RunSect = SectionStack;
      MSect = MomSectionHandle;
      while ((MSect != Lauf->DestSection) && (RunSect))
      {
        strmaxprep(CombName, "_", STRINGSIZE);
        strmaxprep(CombName, GetSectionName(MSect), STRINGSIZE);
        MSect = RunSect->Handle;
        RunSect = RunSect->Next;
      }
      Copy = (PSymbolEntry) calloc(1, sizeof(TSymbolEntry));
      *Copy = (*Neu);
      Copy->Tree.Name = as_strdup(CombName);
      Copy->Tree.Attribute = Lauf->DestSection;
      Copy->Relocs = DupRelocs(Neu->Relocs);
      if (Copy->SymWert.Typ == TempString)
      {
        Copy->SymWert.Contents.String.Contents = (char*)malloc(Neu->SymWert.Contents.String.Length);
        memcpy(Copy->SymWert.Contents.String.Contents, Neu->SymWert.Contents.String.Contents,
               Copy->SymWert.Contents.String.Length = Neu->SymWert.Contents.String.Length);
      }
      EnterTree(&TreeRoot, &(Copy->Tree), SymbolAdder, &EnterStruct);
    }
    if (Lauf)
    {
      free(Lauf->Name);
      free(Lauf->pErrorPos);
      if (!Prev)
        *RRoot = Lauf->Next;
      else
        Prev->Next = Lauf->Next;
      free(Lauf);
    }
  }
  EnterTree(&TreeRoot, &(Neu->Tree), SymbolAdder, &EnterStruct);
  FirstSymbol = (PSymbolEntry)TreeRoot;
}

void PrintSymTree(char *Name)
{
  fprintf(Debug, "---------------------\n");
  fprintf(Debug, "Enter Symbol %s\n\n", Name);
  PrintSymbolTree();
  PrintSymbolDepth();
}

/*!------------------------------------------------------------------------
 * \fn     ChangeSymbol(PSymbolEntry pEntry, LargeInt Value)
 * \brief  change value of symbol in symbol table (use with caution)
 * \param  pEntry symbol entry to modify
 * \param  Value new (integer)value
 * ------------------------------------------------------------------------ */

void ChangeSymbol(PSymbolEntry pEntry, LargeInt Value)
{
  pEntry->SymWert.Typ = TempInt;
  pEntry->SymWert.Contents.IWert = Value;
}

/*!------------------------------------------------------------------------
 * \fn     EnterIntSymbolWithFlags(const tStrComp *pName, LargeInt Wert, Byte Typ, Boolean MayChange, tSymbolFlags Flags)
 * \brief  add integer symbol to symbol table
 * \param  pName unexpanded name
 * \param  Wert integer value
 * \param  Typ symbol type
 * \param  MayChange constant or variable?
 * \param  Flags additional flags
 * \return * to newly created entry in tree
 * ------------------------------------------------------------------------ */

PSymbolEntry CreateSymbolEntry(const tStrComp *pName, LongInt *pDestHandle)
{
  PSymbolEntry pNeu;
  String ExtName;
  
  if (!ExpandStrSymbol(ExtName, sizeof(ExtName), pName))
    return NULL;
  if (!GetSymSection(ExtName, pDestHandle, pName))
    return NULL;
  (void)ChkTmp(ExtName, TRUE);
  if (!ChkSymbName(ExtName))
  {
    WrStrErrorPos(ErrNum_InvSymName, pName);
    return NULL;
  }
  pNeu = (PSymbolEntry) calloc(1, sizeof(TSymbolEntry));
  pNeu->Tree.Name = as_strdup(ExtName);
  return pNeu;
}

PSymbolEntry EnterIntSymbolWithFlags(const tStrComp *pName, LargeInt Wert, Byte Typ, Boolean MayChange, tSymbolFlags Flags)
{
  LongInt DestHandle;
  PSymbolEntry pNeu = CreateSymbolEntry(pName, &DestHandle);

  if (!pNeu)
    return NULL;

  pNeu->SymWert.Typ = TempInt;
  pNeu->SymWert.Contents.IWert = Wert;
  pNeu->SymType = Typ;
  pNeu->Flags = Flags;
  pNeu->SymSize = -1;
  pNeu->RefList = NULL;
  pNeu->Relocs = NULL;

  if ((MomLocHandle == -1) || (DestHandle != -2))
  {
    EnterSymbol(pNeu, MayChange, DestHandle);
    if (MakeDebug)
      PrintSymTree(pNeu->Tree.Name);
  }
  else
    EnterLocSymbol(pNeu);
  return pNeu;
}

/*!------------------------------------------------------------------------
 * \fn     EnterExtSymbol(const tStrComp *pName, LargeInt Wert, Byte Typ, Boolean MayChange)
 * \brief  create extended symbol
 * \param  pName unexpanded name
 * \param  Wert symbol value
 * \param  MayChange variable or constant?
 * ------------------------------------------------------------------------ */

void EnterExtSymbol(const tStrComp *pName, LargeInt Wert, Byte Typ, Boolean MayChange)
{
  LongInt DestHandle;
  PSymbolEntry pNeu = CreateSymbolEntry(pName, &DestHandle);
    
  if (!pNeu)
    return;
    
  pNeu = (PSymbolEntry) calloc(1, sizeof(TSymbolEntry));
  pNeu->SymWert.Typ = TempInt;
  pNeu->SymWert.Contents.IWert = Wert;
  pNeu->SymType = Typ;
  pNeu->Flags = eSymbolFlag_None;
  pNeu->SymSize = -1;
  pNeu->RefList = NULL;
  pNeu->Relocs = (PRelocEntry) malloc(sizeof(TRelocEntry));
  pNeu->Relocs->Next = NULL;
  pNeu->Relocs->Ref = as_strdup(pNeu->Tree.Name);
  pNeu->Relocs->Add = True;

  if ((MomLocHandle == -1) || (DestHandle != -2))
  {
    EnterSymbol(pNeu, MayChange, DestHandle);
    if (MakeDebug)
      PrintSymTree(pNeu->Tree.Name);
  }
  else
    EnterLocSymbol(pNeu);
}

/*!------------------------------------------------------------------------
 * \fn     EnterRelSymbol(const tStrComp *pName, LargeInt Wert, Byte Typ, Boolean MayChange)
 * \brief  enter relocatable symbol
 * \param  pName unexpanded name
 * \param  Wert symbol value
 * \param  Typ symbol type
 * \param  MayChange variable or constant?
 * \return * to created entry in tree
 * ------------------------------------------------------------------------ */

PSymbolEntry EnterRelSymbol(const tStrComp *pName, LargeInt Wert, Byte Typ, Boolean MayChange)
{
  LongInt DestHandle;
  PSymbolEntry pNeu = CreateSymbolEntry(pName, &DestHandle);

  if (!pNeu)
    return NULL;

  pNeu->SymWert.Typ = TempInt;
  pNeu->SymWert.Contents.IWert = Wert;
  pNeu->SymType = Typ;
  pNeu->Flags = eSymbolFlag_None;
  pNeu->SymSize = -1;
  pNeu->RefList = NULL;
  pNeu->Relocs = (PRelocEntry) malloc(sizeof(TRelocEntry));
  pNeu->Relocs->Next = NULL;
  pNeu->Relocs->Ref = as_strdup(RelName_SegStart);
  pNeu->Relocs->Add = True;

  if ((MomLocHandle == -1) || (DestHandle != -2))
  {
    EnterSymbol(pNeu, MayChange, DestHandle);
    if (MakeDebug)
      PrintSymTree(pNeu->Tree.Name);
  }
  else
    EnterLocSymbol(pNeu);
  return pNeu;
}

/*!------------------------------------------------------------------------
 * \fn     EnterFloatSymbol(const tStrComp *pName, Double Wert, Boolean MayChange)
 * \brief  enter floating point symbol
 * \param  pName unexpanded name
 * \param  Wert symbol value
 * \param  MayChange variable or constant?
 * ------------------------------------------------------------------------ */

void EnterFloatSymbol(const tStrComp *pName, Double Wert, Boolean MayChange)
{
  LongInt DestHandle;
  PSymbolEntry pNeu = CreateSymbolEntry(pName, &DestHandle);

  if (!pNeu)
    return;

  pNeu->SymWert.Typ = TempFloat;
  pNeu->SymWert.Contents.FWert = Wert;
  pNeu->SymType = 0;
  pNeu->Flags = eSymbolFlag_None;
  pNeu->SymSize = -1;
  pNeu->RefList = NULL;
  pNeu->Relocs = NULL;

  if ((MomLocHandle == -1) || (DestHandle != -2))
  {
    EnterSymbol(pNeu, MayChange, DestHandle);
    if (MakeDebug)
      PrintSymTree(pNeu->Tree.Name);
  }
  else
    EnterLocSymbol(pNeu);
}

/*!------------------------------------------------------------------------
 * \fn     EnterDynStringSymbolWithFlags(const tStrComp *pName, const tDynString *pValue, Boolean MayChange, tSymbolFlags Flags)
 * \brief  enter string symbol
 * \param  pName unexpanded name
 * \param  pValue symbol value
 * \param  MayChange variable or constant?
 * \param  Flags special symbol flags to store
 * ------------------------------------------------------------------------ */

void EnterDynStringSymbolWithFlags(const tStrComp *pName, const tDynString *pValue, Boolean MayChange, tSymbolFlags Flags)
{
  LongInt DestHandle;
  PSymbolEntry pNeu = CreateSymbolEntry(pName, &DestHandle);

  if (!pNeu)
    return;

  pNeu->SymWert.Contents.String.Contents = (char*)malloc(pValue->Length);
  memcpy(pNeu->SymWert.Contents.String.Contents, pValue->Contents, pValue->Length);
  pNeu->SymWert.Contents.String.Length = pValue->Length;
  pNeu->SymWert.Typ = TempString;
  pNeu->SymType = 0;
  pNeu->Flags = Flags;
  pNeu->SymSize = -1;
  pNeu->RefList = NULL;
  pNeu->Relocs = NULL;

  if ((MomLocHandle == -1) || (DestHandle != -2))
  {
    EnterSymbol(pNeu, MayChange, DestHandle);
    if (MakeDebug)
      PrintSymTree(pNeu->Tree.Name);
  }
  else
    EnterLocSymbol(pNeu);
}

/*!------------------------------------------------------------------------
 * \fn     EnterStringSymbol(const tStrComp *pName, const char *pValue, Boolean MayChange)
 * \brief  enter string symbol
 * \param  pName unexpanded name
 * \param  pValue symbol value
 * \param  MayChange variable or constant?
 * ------------------------------------------------------------------------ */

void EnterStringSymbol(const tStrComp *pName, const char *pValue, Boolean MayChange)
{
  tDynString DynString;

  DynString.Length = 0;
  DynStringAppend(&DynString, pValue, -1);
  EnterDynStringSymbol(pName, &DynString, MayChange);
}

static void AddReference(PSymbolEntry Node)
{
  PCrossRef Lauf, Neu;

  /* Speicher belegen */

  Neu = (PCrossRef) malloc(sizeof(TCrossRef));
  Neu->LineNum = CurrLine;
  Neu->OccNum = 1;
  Neu->Next = NULL;

  /* passende Datei heraussuchen */

  Neu->FileNum = GetFileNum(CurrFileName);

  /* suchen, ob Eintrag schon existiert */

  Lauf = Node->RefList;
  while ((Lauf)
     && ((Lauf->FileNum != Neu->FileNum) || (Lauf->LineNum != Neu->LineNum)))
   Lauf = Lauf->Next;

  /* schon einmal in dieser Datei in dieser Zeile aufgetaucht: nur Zaehler
    rauf: */

  if (Lauf)
  {
    Lauf->OccNum++;
   free(Neu);
  }

  /* ansonsten an Kettenende anhaengen */

  else if (!Node->RefList) Node->RefList = Neu;

  else
  {
    Lauf = Node->RefList;
    while (Lauf->Next)
      Lauf = Lauf->Next;
    Lauf->Next = Neu;
  }
}

static PSymbolEntry FindNode_FNode(char *Name, TempType SearchType, LongInt Handle)
{
  PSymbolEntry Lauf;

  Lauf = (PSymbolEntry) SearchTree((PTree)FirstSymbol, Name, Handle);

  if (Lauf)
  {
    if (Lauf->SymWert.Typ & SearchType)
    {
      if (MakeCrossList && DoRefs)
        AddReference(Lauf);
    }
    else
      Lauf = NULL;
  }

  return Lauf;
}

static Boolean FindNode_FSpec(char *Name, PForwardSymbol Root)
{
  while ((Root) && (strcmp(Root->Name, Name)))
    Root = Root->Next;
  return (Root != NULL);
}

static PSymbolEntry FindNode(const char *Name_O, TempType SearchType)
{
  PSaveSection Lauf;
  LongInt DestSection;
  PSymbolEntry Result = NULL;
  String Name;

  strmaxcpy(Name, Name_O, STRINGSIZE);
  ChkTmp3(Name, FALSE);

  /* TODO: pass StrComp */
  if (!GetSymSection(Name, &DestSection, NULL))
    return NULL;

  if (!CaseSensitive)
    NLS_UpString(Name);

  if (SectionStack)
    if (PassNo <= MaxSymPass)
      if (FindNode_FSpec(Name, SectionStack->LocSyms)) DestSection = MomSectionHandle;

  if (DestSection == -2)
  {
    Result = FindNode_FNode(Name, SearchType, MomSectionHandle);
    if (Result)
      return Result;
    Lauf = SectionStack;
    while (Lauf)
    {
      Result = FindNode_FNode(Name, SearchType, Lauf->Handle);
      if (Result)
        break;
      Lauf = Lauf->Next;
    }
  }
  else
    Result = FindNode_FNode(Name, SearchType, DestSection);

  return Result;
}

static PSymbolEntry FindLocNode_FNode(char *Name, TempType SearchType, LongInt Handle)
{
  PSymbolEntry Lauf;

  Lauf = (PSymbolEntry) SearchTree((PTree)FirstLocSymbol, Name, Handle);

  if (Lauf)
  {
    if (!(Lauf->SymWert.Typ & SearchType))
      Lauf = NULL;
  }

  return Lauf;
}

static PSymbolEntry FindLocNode(const char *Name_O, TempType SearchType)
{
  PLocHandle RunLocHandle;
  PSymbolEntry Result = NULL;
  String Name;

  strmaxcpy(Name, Name_O, STRINGSIZE);
  ChkTmp3(Name, FALSE);
  if (!CaseSensitive)
    NLS_UpString(Name);

  if (MomLocHandle == -1)
    return NULL;

  Result = FindLocNode_FNode(Name, SearchType, MomLocHandle);
  if (Result)
    return Result;

  RunLocHandle = FirstLocHandle;
  while ((RunLocHandle) && (RunLocHandle->Cont != -1))
  {
    Result = FindLocNode_FNode(Name, SearchType, RunLocHandle->Cont);
    if (Result)
      break;
    RunLocHandle = RunLocHandle->Next;
  }

  return Result;
}
/**
void SetSymbolType(const tStrComp *pName, Byte NTyp)
{
  PSymbolEntry Lauf;
  Boolean HRef;
  String ExpName;

  if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pName))
    return;
  HRef = DoRefs;
  DoRefs = False;
  Lauf = FindLocNode(ExpName, TempInt);
  if (!Lauf)
    Lauf = FindNode(ExpName, TempInt);
  if (Lauf)
    Lauf->SymType = NTyp;
  DoRefs = HRef;
}
**/

void LookupSymbol(const struct sStrComp *pComp, TempResult *pValue, Boolean WantRelocs, TempType ReqType)
{
  PSymbolEntry pEntry;
  String ExpName;
  char Save = ' ', *pKlPos;
  Boolean NameOK;

  if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pComp))
  {
    pValue->Typ = TempNone;
    return;
  }

  pKlPos = strchr(ExpName, '[');
  if (pKlPos)
  {
    Save = (*pKlPos);
    *pKlPos = '\0';
  }
  NameOK = ChkSymbName(ExpName);
  if (pKlPos)
    *pKlPos = Save;
  if (!NameOK)
  {
    WrStrErrorPos(ErrNum_InvSymName, pComp);
    pValue->Typ = TempNone;
    return;
  }

  pEntry = FindLocNode(ExpName, ReqType);
  if (!pEntry)
    pEntry = FindNode(ExpName, ReqType);
  if (pEntry)
  {
    switch (pValue->Typ = pEntry->SymWert.Typ)
    {
      case TempInt:
        pValue->Contents.Int = pEntry->SymWert.Contents.IWert;
        break;
      case TempFloat:
        pValue->Contents.Float = pEntry->SymWert.Contents.FWert;
        break;
      case TempString:
        pValue->Contents.Ascii.Length = 0;
        DynStringAppend(&pValue->Contents.Ascii, pEntry->SymWert.Contents.String.Contents, pEntry->SymWert.Contents.String.Length);
        break;
      default:
        break;
    }
    if (pValue->Typ != TempNone)
    {
      if (WantRelocs)
        pValue->Relocs = DupRelocs(pEntry->Relocs);
      pValue->Flags = pEntry->Flags;
    }
    if (pEntry->SymType != 0)
      TypeFlag |= (1 << pEntry->SymType);
    if ((pEntry->SymSize != -1) && (SizeFlag == -1))
      SizeFlag = pEntry->SymSize;
    if (!pEntry->Defined)
    {
      if (Repass)
        SymbolQuestionable = True;
      UsesForwards = True;
    }
    pEntry->Used = True;
  }

  /* Symbol evtl. im ersten Pass unbekannt */

  else if (PassNo <= MaxSymPass) /* !pEntry */
  {
    pValue->Typ = TempInt;
    pValue->Contents.Int = EProgCounter();
    Repass = True;
    if ((MsgIfRepass) && (PassNo >= PassNoForMessage))
      WrStrErrorPos(ErrNum_RepassUnknown, pComp);
    FirstPassUnknown = True;
  }
  else
    WrStrErrorPos(ErrNum_SymbolUndef, pComp);
}

/*!------------------------------------------------------------------------
 * \fn     SetSymbolOrStructElemSize(const struct sStrComp *pName, ShortInt Size)
 * \brief  set (integer) data size associated with a symbol
 * \param  pName unexpanded name of symbol
 * \param  Size operand size to set
 * ------------------------------------------------------------------------ */

void SetSymbolOrStructElemSize(const struct sStrComp *pName, ShortInt Size)
{
  if (pInnermostNamedStruct)
    SetStructElemSize(pInnermostNamedStruct->StructRec, pName->Str, Size);
  else
  {
    PSymbolEntry pEntry;
    Boolean HRef;
    String ExpName;

    if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pName))
      return;
    HRef = DoRefs;
    DoRefs = False;
    pEntry = FindLocNode(ExpName, TempInt);
    if (!pEntry)
      pEntry = FindNode(ExpName, TempInt);
    if (pEntry)
      pEntry->SymSize = Size;
    DoRefs = HRef;
  }
}

/*!------------------------------------------------------------------------
 * \fn     GetSymbolSize(const struct sStrComp *pName)
 * \brief  get symbol's integer size
 * \param  pName unexpanded symbol name
 * \return symbol size or -1 if symbol does not exist
 * ------------------------------------------------------------------------ */

ShortInt GetSymbolSize(const struct sStrComp *pName)
{
  PSymbolEntry pEntry;
  String ExpName;

  if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pName))
     return -1;
  pEntry = FindLocNode(ExpName, TempInt);
  if (!pEntry)
    pEntry = FindNode(ExpName, TempInt);
  return pEntry ? pEntry->SymSize : -1;
}

/*!------------------------------------------------------------------------
 * \fn     IsSymbolDefined(const struct sStrComp *pName)
 * \brief  check whether symbol nas been used so far
 * \param  pName unexpanded symbol name
 * \return true if symbol exists and has been defined so far
 * ------------------------------------------------------------------------ */

Boolean IsSymbolDefined(const struct sStrComp *pName)
{
  PSymbolEntry pEntry;
  String ExpName;

  if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pName))
    return False;

  pEntry = FindLocNode(ExpName, TempAll);
  if (!pEntry)
    pEntry = FindNode(ExpName, TempAll);
  return pEntry && pEntry->Defined;
}

/*!------------------------------------------------------------------------
 * \fn     IsSymbolUsed(const struct sStrComp *pName)
 * \brief  check whether symbol nas been used so far
 * \param  pName unexpanded symbol name
 * \return true if symbol exists and has been used
 * ------------------------------------------------------------------------ */

Boolean IsSymbolUsed(const struct sStrComp *pName)
{
  PSymbolEntry pEntry;
  String ExpName;

  if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pName))
    return False;

  pEntry = FindLocNode(ExpName, TempAll);
  if (!pEntry)
    pEntry = FindNode(ExpName, TempAll);
  return pEntry && pEntry->Used;
}

/*!------------------------------------------------------------------------
 * \fn     IsSymbolChangeable(const struct sStrComp *pName)
 * \brief  check whether symbol's value may be changed or is constant
 * \param  pName unexpanded symbol name
 * \return true if symbol exists and is changeable
 * ------------------------------------------------------------------------ */

Boolean IsSymbolChangeable(const struct sStrComp *pName)
{
  PSymbolEntry pEntry;
  String ExpName;

  if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pName))
    return False;

  pEntry = FindLocNode(ExpName, TempAll);
  if (!pEntry)
    pEntry = FindNode(ExpName, TempAll);
  return pEntry && pEntry->Changeable;
}

/*!------------------------------------------------------------------------
 * \fn     GetSymbolType(const struct sStrComp *pName)
 * \brief  retrieve type (int/float/string) of symbol
 * \param  pName unexpanded name
 * \return type or -1 if non-existent
 * ------------------------------------------------------------------------ */

Integer GetSymbolType(const struct sStrComp *pName)
{
  PSymbolEntry pEntry;
  String ExpName;

  if (!ExpandStrSymbol(ExpName, sizeof(ExpName), pName))
    return -1;

  pEntry = FindLocNode(ExpName, TempAll);
  if (!pEntry)
    pEntry = FindNode(ExpName, TempAll);
  return pEntry ? pEntry->SymType : -1;
}

static void ConvertSymbolVal(const PSymbolEntry pInp, TempResult *Outp)
{
  switch (Outp->Typ = pInp->SymWert.Typ)
  {
    case TempInt:
      Outp->Contents.Int = pInp->SymWert.Contents.IWert;
      break;
    case TempFloat:
      Outp->Contents.Float = pInp->SymWert.Contents.FWert;
      break;
    case TempString:
      Outp->Contents.Ascii.Length = 0;
      DynStringAppend(&Outp->Contents.Ascii, pInp->SymWert.Contents.String.Contents, pInp->SymWert.Contents.String.Length);
      break;
    default:
      break;
  }
  Outp->Flags = pInp->Flags;
}

typedef struct
{
  int Width, cwidth;
  LongInt Sum, USum;
  String Zeilenrest;
  int ZeilenrestLen,
      ZeilenrestVisibleLen;
} TListContext;

static void PrintSymbolList_AddOut(char *s, TListContext *pContext)
{
  int AddVisibleLen = visible_strlen(s),
      AddLen = strlen(s);

  if (AddVisibleLen + pContext->ZeilenrestVisibleLen > pContext->Width)
  {
    pContext->Zeilenrest[pContext->ZeilenrestLen - 1] = '\0';
    WrLstLine(pContext->Zeilenrest);
    strmaxcpy(pContext->Zeilenrest, s, STRINGSIZE);
    pContext->ZeilenrestLen = AddLen;
    pContext->ZeilenrestVisibleLen = AddVisibleLen;
  }
  else
  {
    strmaxcat(pContext->Zeilenrest, s, STRINGSIZE);
    pContext->ZeilenrestLen += AddLen;
    pContext->ZeilenrestVisibleLen += AddVisibleLen;
  }
}

static void PrintSymbolList_PNode(PTree Tree, void *pData)
{
  PSymbolEntry Node = (PSymbolEntry) Tree;
  TListContext *pContext = (TListContext*) pData;
  String s1, sh;
  int l1, nBlanks;
  TempResult t;

  ConvertSymbolVal(Node, &t);
  if ((t.Typ == TempInt) && DissectBit && (Node->SymType == SegBData))
    DissectBit(s1, sizeof(s1), t.Contents.Int);
  else
    StrSym(&t, False, s1, sizeof(s1), ListRadixBase);

  as_snprintf(sh, STRINGSIZE, "%c%s : ", Node->Used ? ' ' : '*', Tree->Name);
  if (Tree->Attribute != -1)
    as_snprcatf(sh, STRINGSIZE, " [%s]", GetSectionName(Tree->Attribute));
  l1 = (strlen(s1) + visible_strlen(sh) + 4);
  for (nBlanks = pContext->cwidth - 1 - l1; nBlanks < 0; nBlanks += pContext->cwidth);
  as_snprcatf(sh, STRINGSIZE, "%s%s %c | ", Blanks(nBlanks), s1, SegShorts[Node->SymType]);
  PrintSymbolList_AddOut(sh, pContext);
  pContext->Sum++;
  if (!Node->Used)
    pContext->USum++;
}

void PrintSymbolList(void)
{
  int ActPageWidth;
  TListContext Context;

  Context.Width = (PageWidth == 0) ? 80 : PageWidth;
  NewPage(ChapDepth, True);
  WrLstLine(getmessage(Num_ListSymListHead1));
  WrLstLine(getmessage(Num_ListSymListHead2));
  WrLstLine("");

  Context.Zeilenrest[0] = '\0';
  Context.ZeilenrestLen =
  Context.ZeilenrestVisibleLen = 0;
  Context.Sum = Context.USum = 0;
  ActPageWidth = (PageWidth == 0) ? 80 : PageWidth;
  Context.cwidth = ActPageWidth >> 1;
  IterTree((PTree)FirstSymbol, PrintSymbolList_PNode, &Context);
  if (Context.Zeilenrest[0] != '\0')
  {
    Context.Zeilenrest[strlen(Context.Zeilenrest) - 1] = '\0';
    WrLstLine(Context.Zeilenrest);
  }
  WrLstLine("");
  as_snprintf(Context.Zeilenrest, sizeof(Context.Zeilenrest), "%7lu%s",
              (unsigned long)Context.Sum,
              getmessage((Context.Sum == 1) ? Num_ListSymSumMsg : Num_ListSymSumsMsg));
  WrLstLine(Context.Zeilenrest);
  as_snprintf(Context.Zeilenrest, sizeof(Context.Zeilenrest), "%7lu%s",
              (unsigned long)Context.USum,
              getmessage((Context.USum == 1) ? Num_ListUSymSumMsg : Num_ListUSymSumsMsg));
  WrLstLine(Context.Zeilenrest);
  WrLstLine("");
}

typedef struct
{
  FILE *f;
  Boolean HWritten;
  int Space;
} TDebContext;

static void PrintDebSymbols_PNode(PTree Tree, void *pData)
{
  PSymbolEntry Node = (PSymbolEntry) Tree;
  TDebContext *DebContext = (TDebContext*) pData;
  int l1;
  TempResult t;
  String s;

  if (Node->SymType != DebContext->Space)
    return;

  if (!DebContext->HWritten)
  {
    fprintf(DebContext->f, "\n"); ChkIO(ErrNum_FileWriteError);
    fprintf(DebContext->f, "Symbols in Segment %s\n", SegNames[DebContext->Space]); ChkIO(ErrNum_FileWriteError);
    DebContext->HWritten = True;
  }

  fprintf(DebContext->f, "%s", Node->Tree.Name); ChkIO(ErrNum_FileWriteError);
  l1 = strlen(Node->Tree.Name);
  if (Node->Tree.Attribute != -1)
  {
    as_snprintf(s, sizeof(s), "[%d]", (int)Node->Tree.Attribute);
    fprintf(DebContext->f, "%s", s); ChkIO(ErrNum_FileWriteError);
    l1 += strlen(s);
  }
  fprintf(DebContext->f, "%s ", Blanks(37 - l1)); ChkIO(ErrNum_FileWriteError);
  switch (Node->SymWert.Typ)
  {
    case TempInt:
      fprintf(DebContext->f, "Int    ");
      break;
    case TempFloat:
      fprintf(DebContext->f, "Float  ");
      break;
    case TempString:
      fprintf(DebContext->f, "String ");
      break;
    default:
      break;
  }
  ChkIO(ErrNum_FileWriteError);
  if (Node->SymWert.Typ == TempString)
  {
    errno = 0;
    l1 = fstrlenprint(DebContext->f, Node->SymWert.Contents.String.Contents, Node->SymWert.Contents.String.Length);
    ChkIO(ErrNum_FileWriteError);
  }
  else
  {
    ConvertSymbolVal(Node, &t);
    StrSym(&t, False, s, sizeof(s), 16);
    l1 = strlen(s);
    fprintf(DebContext->f, "%s", s); ChkIO(ErrNum_FileWriteError);
  }
  fprintf(DebContext->f, "%s %-3d %d %d\n", Blanks(25-l1), Node->SymSize, (int)Node->Used, (int)Node->Changeable);
  ChkIO(ErrNum_FileWriteError);
}

void PrintDebSymbols(FILE *f)
{
  TDebContext DebContext;

  DebContext.f = f;
  for (DebContext.Space = 0; DebContext.Space < PCMax; DebContext.Space++)
  {
    DebContext.HWritten = False;
    IterTree((PTree)FirstSymbol, PrintDebSymbols_PNode, &DebContext);
  }
}

typedef struct
{
  FILE *f;
  LongInt Handle;
} TNoISymContext;

static void PrNoISection(PTree Tree, void *pData)
{
  PSymbolEntry Node = (PSymbolEntry)Tree;
  TNoISymContext *pContext = (TNoISymContext*) pData;

  if (((1 << Node->SymType) & NoICEMask) && (Node->Tree.Attribute == pContext->Handle) && (Node->SymWert.Typ == TempInt))
  {
    errno = 0; fprintf(pContext->f, "DEFINE %s 0x", Node->Tree.Name); ChkIO(ErrNum_FileWriteError);
    errno = 0; fprintf(pContext->f, LargeHIntFormat, Node->SymWert.Contents.IWert); ChkIO(ErrNum_FileWriteError);
    errno = 0; fprintf(pContext->f, "\n"); ChkIO(ErrNum_FileWriteError);
  }
}

void PrintNoISymbols(FILE *f)
{
  PCToken CurrSection;
  TNoISymContext Context;

  Context.f = f;
  Context.Handle = -1;
  IterTree((PTree)FirstSymbol, PrNoISection, &Context);
  Context.Handle++;
  for (CurrSection = FirstSection; CurrSection; CurrSection = CurrSection->Next)
   if (ChunkSum(&CurrSection->Usage)>0)
   {
     fprintf(f, "FUNCTION %s ", CurrSection->Name); ChkIO(ErrNum_FileWriteError);
     fprintf(f, LargeIntFormat, ChunkMin(&CurrSection->Usage)); ChkIO(ErrNum_FileWriteError);
     fprintf(f, "\n"); ChkIO(ErrNum_FileWriteError);
     IterTree((PTree)FirstSymbol, PrNoISection, &Context);
     Context.Handle++;
     fprintf(f, "}FUNC "); ChkIO(ErrNum_FileWriteError);
     fprintf(f, LargeIntFormat, ChunkMax(&CurrSection->Usage)); ChkIO(ErrNum_FileWriteError);
     fprintf(f, "\n"); ChkIO(ErrNum_FileWriteError);
   }
}

void PrintSymbolTree(void)
{
  DumpTree((PTree)FirstSymbol);
}

static void ClearSymbolList_ClearNode(PTree Node, void *pData)
{
  PSymbolEntry SymbolEntry = (PSymbolEntry) Node;
  UNUSED(pData);

  FreeSymbolEntry(&SymbolEntry, FALSE);
}

void ClearSymbolList(void)
{
  PTree TreeRoot;

  TreeRoot = &(FirstSymbol->Tree);
  FirstSymbol = NULL;
  DestroyTree(&TreeRoot, ClearSymbolList_ClearNode, NULL);
  TreeRoot = &(FirstLocSymbol->Tree);
  FirstLocSymbol = NULL;
  DestroyTree(&TreeRoot, ClearSymbolList_ClearNode, NULL);
}

/*-------------------------------------------------------------------------*/
/* Stack-Verwaltung */

Boolean PushSymbol(const tStrComp *pSymName, const tStrComp *pStackName)
{
  PSymbolEntry pSrc;
  PSymbolStack LStack, NStack, PStack;
  PSymbolStackEntry Elem;
  String ExpSymName, ExpStackName;

  if (!ExpandStrSymbol(ExpSymName, sizeof(ExpSymName), pSymName))
    return False;

  pSrc = FindNode(ExpSymName, TempAll);
  if (!pSrc)
  {
    WrStrErrorPos(ErrNum_SymbolUndef, pSymName);
    return False;
  }

  if (*pStackName->Str)
  {
    if (!ExpandStrSymbol(ExpStackName, sizeof(ExpStackName), pStackName))
      return False;
  }
  else
    strmaxcpy(ExpStackName, DefStackName, STRINGSIZE);
  if (!ChkSymbName(ExpStackName))
  {
    WrStrErrorPos(ErrNum_InvSymName, pStackName);
    return False;
  }

  LStack = FirstStack;
  PStack = NULL;
  while ((LStack) && (strcmp(LStack->Name, ExpStackName) < 0))
  {
    PStack = LStack;
    LStack = LStack->Next;
  }

  if ((!LStack) || (strcmp(LStack->Name, ExpStackName) > 0))
  {
    NStack = (PSymbolStack) malloc(sizeof(TSymbolStack));
    NStack->Name = as_strdup(ExpStackName);
    NStack->Contents = NULL;
    NStack->Next = LStack;
    if (!PStack)
      FirstStack = NStack;
    else
      PStack->Next = NStack;
    LStack = NStack;
  }

  Elem = (PSymbolStackEntry) malloc(sizeof(TSymbolStackEntry));
  Elem->Next = LStack->Contents;
  Elem->Contents = pSrc->SymWert;
  LStack->Contents = Elem;

  return True;
}

Boolean PopSymbol(const tStrComp *pSymName, const tStrComp *pStackName)
{
  PSymbolEntry pDest;
  PSymbolStack LStack, PStack;
  PSymbolStackEntry Elem;
  String ExpSymName, ExpStackName;

  if (!ExpandStrSymbol(ExpSymName, sizeof(ExpSymName), pSymName))
    return False;

  pDest = FindNode(ExpSymName, TempAll);
  if (!pDest)
  {
    WrStrErrorPos(ErrNum_SymbolUndef, pSymName);
    return False;
  }

  if (*pStackName->Str)
  {
    if (!ExpandStrSymbol(ExpStackName, sizeof(ExpStackName), pStackName))
      return False;
  }
  else
    strmaxcpy(ExpStackName, DefStackName, STRINGSIZE);
  if (!ChkSymbName(ExpStackName))
  {
    WrStrErrorPos(ErrNum_InvSymName, pStackName);
    return False;
  }

  LStack = FirstStack;
  PStack = NULL;
  while ((LStack) && (strcmp(LStack->Name, ExpStackName) < 0))
  {
    PStack = LStack;
    LStack = LStack->Next;
  }

  if ((!LStack) || (strcmp(LStack->Name, ExpStackName) > 0))
  {
    WrStrErrorPos(ErrNum_StackEmpty, pStackName);
    return False;
  }

  Elem = LStack->Contents;
  pDest->SymWert = Elem->Contents;
  LStack->Contents = Elem->Next;
  if (!LStack->Contents)
  {
    if (!PStack)
      FirstStack = LStack->Next;
    else
      PStack->Next = LStack->Next;
    free(LStack->Name);
    free(LStack);
  }
  free(Elem);

  return True;
}

void ClearStacks(void)
{
  PSymbolStack Act;
  PSymbolStackEntry Elem;
  int z;
  String s;

  while (FirstStack)
  {
    z = 0;
    Act = FirstStack;
    while (Act->Contents)
    {
      Elem = Act->Contents;
      Act->Contents = Elem->Next;
      free(Elem);
      z++;
    }
    as_snprintf(s, sizeof(s), "%s(%d)", Act->Name, z);
    WrXError(ErrNum_StackNotEmpty, s);
    free(Act->Name);
    FirstStack = Act->Next;
    free(Act);
  }
}

/*-------------------------------------------------------------------------*/
/* Funktionsverwaltung */

void EnterFunction(const tStrComp *pComp, char *FDefinition, Byte NewCnt)
{
  PFunction Neu;
  String FName_N;
  const char *pFName;

  if (!CaseSensitive)
  {
    strmaxcpy(FName_N, pComp->Str, STRINGSIZE);
    NLS_UpString(FName_N);
    pFName = FName_N;
  }
  else
     pFName = pComp->Str;

  if (!ChkSymbName(pFName))
  {
    WrStrErrorPos(ErrNum_InvSymName, pComp);
    return;
  }

  if (FindFunction(pFName))
  {
    if (PassNo == 1)
      WrStrErrorPos(ErrNum_DoubleDef, pComp);
    return;
  }

  Neu = (PFunction) malloc(sizeof(TFunction));
  Neu->Next = FirstFunction;
  Neu->ArguCnt = NewCnt;
  Neu->Name = as_strdup(pFName);
  Neu->Definition = as_strdup(FDefinition);
  FirstFunction = Neu;
}

PFunction FindFunction(const char *Name)
{
  PFunction Lauf = FirstFunction;
  String Name_N;

  if (!CaseSensitive)
  {
    strmaxcpy(Name_N, Name, STRINGSIZE);
    NLS_UpString(Name_N);
    Name = Name_N;
  }

  while ((Lauf) && (strcmp(Lauf->Name, Name)))
    Lauf = Lauf->Next;
  return Lauf;
}

void PrintFunctionList(void)
{
  PFunction Lauf;
  String OneS;
  Boolean cnt;

  if (!FirstFunction)
    return;

  NewPage(ChapDepth, True);
  WrLstLine(getmessage(Num_ListFuncListHead1));
  WrLstLine(getmessage(Num_ListFuncListHead2));
  WrLstLine("");

  OneS[0] = '\0';
  Lauf = FirstFunction;
  cnt = False;
  while (Lauf)
  {
    strmaxcat(OneS, Lauf->Name, STRINGSIZE);
    if (strlen(Lauf->Name) < 37)
      strmaxcat(OneS, Blanks(37-strlen(Lauf->Name)), STRINGSIZE);
    if (!cnt) strmaxcat(OneS, " | ", STRINGSIZE);
    else
    {
      WrLstLine(OneS);
      OneS[0] = '\0';
    }
    cnt = !cnt;
    Lauf = Lauf->Next;
  }
  if (cnt)
  {
    OneS[strlen(OneS)-1] = '\0';
    WrLstLine(OneS);
  }
  WrLstLine("");
}

void ClearFunctionList(void)
{
  PFunction Lauf;

  while (FirstFunction)
  {
    Lauf = FirstFunction->Next;
    free(FirstFunction->Name);
    free(FirstFunction->Definition);
    free(FirstFunction);
    FirstFunction = Lauf;
  }
}

/*-------------------------------------------------------------------------*/

static void ResetSymbolDefines_ResetNode(PTree Node, void *pData)
{
  PSymbolEntry SymbolEntry = (PSymbolEntry) Node;
  UNUSED(pData);

  SymbolEntry->Defined = False;
  SymbolEntry->Used = False;
}

void ResetSymbolDefines(void)
{
  IterTree(&(FirstSymbol->Tree), ResetSymbolDefines_ResetNode, NULL);
  IterTree(&(FirstLocSymbol->Tree), ResetSymbolDefines_ResetNode, NULL);
}

void SetFlag(Boolean *Flag, const char *Name, Boolean Wert)
{
  tStrComp TmpComp;

  *Flag = Wert;
  StrCompMkTemp(&TmpComp, (char*)Name);
  EnterIntSymbol(&TmpComp, *Flag ? 1 : 0, 0, True);
}

void AddDefSymbol(char *Name, TempResult *Value)
{
  PDefSymbol Neu;

  Neu = FirstDefSymbol;
  while (Neu)
  {
    if (!strcmp(Neu->SymName, Name))
      return;
    Neu = Neu->Next;
  }

  Neu = (PDefSymbol) malloc(sizeof(TDefSymbol));
  Neu->Next = FirstDefSymbol;
  Neu->SymName = as_strdup(Name);
  Neu->Wert = (*Value);
  FirstDefSymbol = Neu;
}

void RemoveDefSymbol(char *Name)
{
  PDefSymbol Save, Lauf;

  if (!FirstDefSymbol)
    return;

  if (!strcmp(FirstDefSymbol->SymName, Name))
  {
    Save = FirstDefSymbol;
    FirstDefSymbol = FirstDefSymbol->Next;
  }
  else
  {
    Lauf = FirstDefSymbol;
    while ((Lauf->Next) && (strcmp(Lauf->Next->SymName, Name)))
      Lauf = Lauf->Next;
    if (!Lauf->Next)
      return;
    Save = Lauf->Next;
    Lauf->Next = Lauf->Next->Next;
  }
  free(Save->SymName);
  free(Save);
}

void CopyDefSymbols(void)
{
  PDefSymbol Lauf;
  tStrComp TmpComp;

  Lauf = FirstDefSymbol;
  while (Lauf)
  {
    StrCompMkTemp(&TmpComp, Lauf->SymName);
    switch (Lauf->Wert.Typ)
    {
      case TempInt:
        EnterIntSymbol(&TmpComp, Lauf->Wert.Contents.Int, 0, True);
        break;
      case TempFloat:
        EnterFloatSymbol(&TmpComp, Lauf->Wert.Contents.Float, True);
        break;
      case TempString:
        EnterDynStringSymbol(&TmpComp, &Lauf->Wert.Contents.Ascii, True);
        break;
      default:
        break;
    }
    Lauf = Lauf->Next;
  }
}

const TempResult *FindDefSymbol(const char *pName)
{
  PDefSymbol pRun;

  for (pRun = FirstDefSymbol; pRun; pRun = pRun->Next)
    if (!strcmp(pName, pRun->SymName))
      return &pRun->Wert;
  return NULL;
}

void PrintSymbolDepth(void)
{
  LongInt TreeMin, TreeMax;

  GetTreeDepth(&(FirstSymbol->Tree), &TreeMin, &TreeMax);
  fprintf(Debug, " MinTree %ld\n", (long)TreeMin);
  fprintf(Debug, " MaxTree %ld\n", (long)TreeMax);
}

LongInt GetSectionHandle(char *SName_O, Boolean AddEmpt, LongInt Parent)
{
  PCToken Lauf, Prev;
  LongInt z;
  String SName;

  strmaxcpy(SName, SName_O, STRINGSIZE);
  if (!CaseSensitive)
    NLS_UpString(SName);

  Lauf = FirstSection;
  Prev = NULL;
  z = 0;
  while ((Lauf) && ((strcmp(Lauf->Name, SName)) || (Lauf->Parent != Parent)))
  {
    z++;
    Prev = Lauf;
    Lauf = Lauf->Next;
  }

  if (!Lauf)
  {
    if (AddEmpt)
    {
      Lauf = (PCToken) malloc(sizeof(TCToken));
      Lauf->Parent = MomSectionHandle;
      Lauf->Name = as_strdup(SName);
      Lauf->Next = NULL;
      InitChunk(&(Lauf->Usage));
      if (!Prev)
        FirstSection = Lauf;
      else
        Prev->Next = Lauf;
    }
    else
      z = -2;
  }
  return z;
}

char *GetSectionName(LongInt Handle)
{
  PCToken Lauf = FirstSection;
  static char *Dummy = "";

  if (Handle == -1)
    return Dummy;
  while ((Handle > 0) && (Lauf))
  {
    Lauf = Lauf->Next;
    Handle--;
  }
  return Lauf ? Lauf->Name : Dummy;
}

void SetMomSection(LongInt Handle)
{
  LongInt z;

  MomSectionHandle = Handle;
  if (Handle < 0)
    MomSection = NULL;
  else
  {
    MomSection = FirstSection;
    for (z = 1; z <= Handle; z++)
      if (MomSection)
        MomSection = MomSection->Next;
  }
}

void AddSectionUsage(LongInt Start, LongInt Length)
{
  if ((ActPC != SegCode) || (!MomSection))
    return;
  AddChunk(&(MomSection->Usage), Start, Length, False);
}

void ClearSectionUsage(void)
{
  PCToken Tmp;

  for (Tmp = FirstSection; Tmp; Tmp = Tmp->Next)
    ClearChunk(&(Tmp->Usage));
}

static void PrintSectionList_PSection(LongInt Handle, int Indent)
{
  PCToken Lauf;
  LongInt Cnt;
  String h;

  ChkStack();
  if (Handle != -1)
  {
    strmaxcpy(h, Blanks(Indent << 1), STRINGSIZE);
    strmaxcat(h, GetSectionName(Handle), STRINGSIZE);
    WrLstLine(h);
  }
  Lauf = FirstSection;
  Cnt = 0;
  while (Lauf)
  {
    if (Lauf->Parent == Handle)
      PrintSectionList_PSection(Cnt, Indent + 1);
    Lauf = Lauf->Next;
    Cnt++;
  }
}

void PrintSectionList(void)
{
  if (!FirstSection)
    return;

  NewPage(ChapDepth, True);
  WrLstLine(getmessage(Num_ListSectionListHead1));
  WrLstLine(getmessage(Num_ListSectionListHead2));
  WrLstLine("");
  PrintSectionList_PSection(-1, 0);
}

void PrintDebSections(FILE *f)
{
  PCToken Lauf;
  LongInt Cnt, z, l, s;
  char Str[30];

  Lauf = FirstSection; Cnt = 0;
  while (Lauf)
  {
    fputs("\nInfo for Section ", f); ChkIO(ErrNum_FileWriteError);
    fprintf(f, LongIntFormat, Cnt); ChkIO(ErrNum_FileWriteError);
    fputc(' ', f); ChkIO(ErrNum_FileWriteError);
    fputs(GetSectionName(Cnt), f); ChkIO(ErrNum_FileWriteError);
    fputc(' ', f); ChkIO(ErrNum_FileWriteError);
    fprintf(f, LongIntFormat, Lauf->Parent); ChkIO(ErrNum_FileWriteError);
    fputc('\n', f); ChkIO(ErrNum_FileWriteError);
    for (z = 0; z < Lauf->Usage.RealLen; z++)
    {
      l = Lauf->Usage.Chunks[z].Length;
      s = Lauf->Usage.Chunks[z].Start;
      HexString(Str, sizeof(Str), s, 0);
      fputs(Str, f);
      ChkIO(ErrNum_FileWriteError);
      if (l == 1)
        fprintf(f, "\n");
      else
      {
        HexString(Str, sizeof(Str), s + l - 1, 0);
        fprintf(f, "-%s\n", Str);
      }
      ChkIO(ErrNum_FileWriteError);
    }
    Lauf = Lauf->Next;
    Cnt++;
  }
}

void ClearSectionList(void)
{
  PCToken Tmp;

  while (FirstSection)
  {
    Tmp = FirstSection;
    free(Tmp->Name);
    ClearChunk(&(Tmp->Usage));
    FirstSection = Tmp->Next; free(Tmp);
  }
}

/*---------------------------------------------------------------------------------*/

static void PrintCrossList_PNode(PTree Node, void *pData)
{
  int FileZ;
  PCrossRef Lauf;
  String LinePart, LineAcc;
  String h, ValStr;
  char LineStr[30];
  TempResult t;
  PSymbolEntry SymbolEntry = (PSymbolEntry) Node;
  UNUSED(pData);

  if (!SymbolEntry->RefList)
    return;

  ConvertSymbolVal(SymbolEntry, &t);
  StrSym(&t, False, ValStr, sizeof(ValStr), ListRadixBase);
  as_snprintf(LineStr, sizeof(LineStr), LongIntFormat, SymbolEntry->LineNum);

  as_snprintf(h, sizeof(h), "%s%s",
              getmessage(Num_ListCrossSymName), Node->Name);
  if (Node->Attribute != -1)
    as_snprcatf(h, sizeof(h), "[%s]", GetSectionName(Node->Attribute));
  as_snprcatf(h, sizeof(h), " (=%s, %s:%s):",
              ValStr, GetFileName(SymbolEntry->FileNum), LineStr);


  WrLstLine(h);

  for (FileZ = 0; FileZ < GetFileCount(); FileZ++)
  {
    Lauf = SymbolEntry->RefList;

    while ((Lauf) && (Lauf->FileNum != FileZ))
      Lauf = Lauf->Next;

    if (Lauf)
    {
      strcpy(h, " ");
      strmaxcat(h, getmessage(Num_ListCrossFileName), STRINGSIZE);
      strmaxcat(h, GetFileName(FileZ), STRINGSIZE);
      strmaxcat(h, " :", STRINGSIZE);
      WrLstLine(h);
      strcpy(LineAcc, "   ");
      while (Lauf)
      {
        as_snprintf(LinePart, sizeof(LinePart), "%5ld", (long)Lauf->LineNum);
        strmaxcat(LineAcc, LinePart, STRINGSIZE);
        if (Lauf->OccNum != 1)
        {
          as_snprintf(LinePart, sizeof(LinePart), "(%2ld)", (long)Lauf->OccNum);
          strmaxcat(LineAcc, LinePart, STRINGSIZE);
        }
        else strmaxcat(LineAcc, "    ", STRINGSIZE);
        if (strlen(LineAcc) >= 72)
        {
          WrLstLine(LineAcc);
          strcpy(LineAcc, "  ");
        }
        Lauf = Lauf->Next;
      }
      if (strcmp(LineAcc, "  "))
        WrLstLine(LineAcc);
    }
  }
  WrLstLine("");
}

void PrintCrossList(void)
{
  WrLstLine("");
  WrLstLine(getmessage(Num_ListCrossListHead1));
  WrLstLine(getmessage(Num_ListCrossListHead2));
  WrLstLine("");
  IterTree(&(FirstSymbol->Tree), PrintCrossList_PNode, NULL);
  WrLstLine("");
}

static void ClearCrossList_CNode(PTree Tree, void *pData)
{
  PCrossRef Lauf;
  PSymbolEntry SymbolEntry = (PSymbolEntry) Tree;
  UNUSED(pData);

  while (SymbolEntry->RefList)
  {
    Lauf = SymbolEntry->RefList->Next;
    free(SymbolEntry->RefList);
    SymbolEntry->RefList = Lauf;
  }
}

void ClearCrossList(void)
{
  IterTree(&(FirstSymbol->Tree), ClearCrossList_CNode, NULL);
}

/*--------------------------------------------------------------------------*/

LongInt GetLocHandle(void)
{
  return LocHandleCnt++;
}

void PushLocHandle(LongInt NewLoc)
{
  PLocHandle NewLocHandle;

  NewLocHandle = (PLocHandle) malloc(sizeof(TLocHeap));
  NewLocHandle->Cont = MomLocHandle;
  NewLocHandle->Next = FirstLocHandle;
  FirstLocHandle = NewLocHandle; MomLocHandle = NewLoc;
}

void PopLocHandle(void)
{
  PLocHandle OldLocHandle;

  OldLocHandle = FirstLocHandle;
  if (!OldLocHandle) return;
  MomLocHandle = OldLocHandle->Cont;
  FirstLocHandle = OldLocHandle->Next;
  free(OldLocHandle);
}

void ClearLocStack()
{
  while (MomLocHandle != -1)
    PopLocHandle();
}

/*--------------------------------------------------------------------------*/

static PRegDef LookupReg(const char *Name, Boolean CreateNew)
{
  PRegDef Run, Neu, Prev;
  int cmperg = 0;

  Prev = NULL;
  Run = FirstRegDef;
  while (Run)
  {
    cmperg = strcmp(Run->Orig, Name);
    if (!cmperg)
      break;
    Prev = Run;
    Run = (cmperg < 0) ? Run->Left : Run->Right;
  }
  if ((!Run) && (CreateNew))
  {
    Neu = (PRegDef) malloc(sizeof(TRegDef));
    Neu->Orig = as_strdup(Name);
    Neu->Left = Neu->Right = NULL;
    Neu->Defs = NULL;
    Neu->DoneDefs = NULL;
    if (!Prev)
      FirstRegDef = Neu;
    else if (cmperg < 0)
      Prev->Left = Neu;
    else
      Prev->Right = Neu;
    return Neu;
  }
  else
    return Run;
}

void AddRegDef(const tStrComp *pOrigComp, const tStrComp *pReplComp)
{
  PRegDef Node;
  PRegDefList Neu;
  String Orig_N, Repl_N;
  const char *pOrig, *pRepl;

  if (!CaseSensitive)
  {
    strmaxcpy(Orig_N, pOrigComp->Str, STRINGSIZE);
    strmaxcpy(Repl_N, pReplComp->Str, STRINGSIZE);
    NLS_UpString(Orig_N);
    NLS_UpString(Repl_N);
    pOrig = Orig_N;
    pRepl = Repl_N;
  }
  else
  {
    pOrig = pOrigComp->Str;
    pRepl = pReplComp->Str;
  }
  if (!ChkSymbName(pOrig))
  {
    WrStrErrorPos(ErrNum_InvSymName, pOrigComp);
    return;
  }
  if (!ChkSymbName(pRepl))
  {
    WrStrErrorPos(ErrNum_InvSymName, pReplComp);
    return;
  }
  Node = LookupReg(pOrig, True);
  if ((Node->Defs) && (Node->Defs->Section == MomSectionHandle))
    WrStrErrorPos(ErrNum_DoubleDef, pOrigComp);
  else
  {
    Neu = (PRegDefList) malloc(sizeof(TRegDefList));
    Neu->Next = Node->Defs;
    Neu->Section = MomSectionHandle;
    Neu->Value = as_strdup(pRepl);
    Neu->Used = False;
    Node->Defs = Neu;
  }
}

Boolean FindRegDef(const char *Name_N, char **Erg)
{
  LongInt Sect;
  PRegDef Node;
  PRegDefList Def;
  String Name;

  if (*Name_N == '[')
    return FALSE;

  strmaxcpy(Name, Name_N, STRINGSIZE);

  /* TODO: get StrComp */
  if (!GetSymSection(Name, &Sect, NULL))
    return False;
  if (!CaseSensitive)
    NLS_UpString(Name);
  Node = LookupReg(Name, False);
  if (!Node)
    return False;
  Def = Node->Defs;
  if (Sect != -2)
    while ((Def) && (Def->Section != Sect))
      Def = Def->Next;
  if (!Def)
    return False;
  else
  {
    *Erg = Def->Value;
    Def->Used = True;
    return True;
  }
}

static void TossRegDefs_TossSingle(PRegDef Node, LongInt Sect)
{
  PRegDefList Tmp;

  if (!Node)
    return;
  ChkStack();

  if ((Node->Defs) && (Node->Defs->Section == Sect))
  {
    Tmp = Node->Defs;
    Node->Defs = Node->Defs->Next;
    Tmp->Next = Node->DoneDefs;
    Node->DoneDefs = Tmp;
  }

  TossRegDefs_TossSingle(Node->Left, Sect);
  TossRegDefs_TossSingle(Node->Right, Sect);
}

void TossRegDefs(LongInt Sect)
{
  TossRegDefs_TossSingle(FirstRegDef, Sect);
}

static void ClearRegDefList(PRegDefList Start)
{
  PRegDefList Tmp;

  while (Start)
  {
    Tmp = Start;
    Start = Start->Next;
    free(Tmp->Value);
    free(Tmp);
  }
}

static void CleanupRegDefs_CleanupNode(PRegDef Node)
{
  if (!Node)
    return;
  ChkStack();
  ClearRegDefList(Node->DoneDefs);
  Node->DoneDefs = NULL;
  CleanupRegDefs_CleanupNode(Node->Left);
  CleanupRegDefs_CleanupNode(Node->Right);
}

void CleanupRegDefs(void)
{
  CleanupRegDefs_CleanupNode(FirstRegDef);
}

static void ClearRegDefs_ClearNode(PRegDef Node)
{
  if (!Node)
    return;
  ChkStack();
  ClearRegDefList(Node->Defs);
  Node->Defs = NULL;
  ClearRegDefList(Node->DoneDefs);
  Node->DoneDefs = NULL;
  ClearRegDefs_ClearNode(Node->Left);
  ClearRegDefs_ClearNode(Node->Right);
  free(Node->Orig);
  free(Node);
}

void ClearRegDefs(void)
{
  ClearRegDefs_ClearNode(FirstRegDef);
}

static int cwidth;

static void PrintRegDefs_PNode(PRegDef Node, char *buf, LongInt *Sum, LongInt *USum)
{
  PRegDefList Lauf;
  String tmp, tmp2;

  for (Lauf = Node->DoneDefs; Lauf; Lauf = Lauf->Next)
  {
    if (Lauf->Section != -1)
      as_snprintf(tmp2, sizeof(tmp2), "[%s]", GetSectionName(Lauf->Section));
    else
      *tmp2 = '\0';
    as_snprintf(tmp, sizeof(tmp), "%c%s%s --> %s", (Lauf->Used) ? ' ' : '*', Node->Orig, tmp2, Lauf->Value);
    if ((int)strlen(tmp) > cwidth - 3)
    {
      if (*buf != '\0')
        WrLstLine(buf);
      *buf = '\0';
      WrLstLine(tmp);
    }
    else
    {
      strmaxcat(tmp, Blanks(cwidth - 3 - strlen(tmp)), STRINGSIZE);
      if (*buf == '\0')
        strcpy(buf, tmp);
      else
      {
        strcat(buf, " | ");
        strcat(buf, tmp);
        WrLstLine(buf);
        *buf = '\0';
      }
    }
    (*Sum)++;
    if (!Lauf->Used)
      (*USum)++;
  }
}

static void PrintRegDefs_PrintSingle(PRegDef Node, char *buf, LongInt *Sum, LongInt *USum)
{
  if (!Node)
    return;
  ChkStack();

  PrintRegDefs_PrintSingle(Node->Left, buf, Sum, USum);
  PrintRegDefs_PNode(Node, buf, Sum, USum);
  PrintRegDefs_PrintSingle(Node->Right, buf, Sum, USum);
}

void PrintRegDefs(void)
{
  String buf;
  LongInt Sum, USum;
  LongInt ActPageWidth;

  if (!FirstRegDef)
    return;

  NewPage(ChapDepth, True);
  WrLstLine(getmessage(Num_ListRegDefListHead1));
  WrLstLine(getmessage(Num_ListRegDefListHead2));
  WrLstLine("");

  *buf = '\0';
  Sum = 0;
  USum = 0;
  ActPageWidth = (PageWidth == 0) ? 80 : PageWidth;
  cwidth = ActPageWidth >> 1;
  PrintRegDefs_PrintSingle(FirstRegDef, buf, &Sum, &USum);

  if (*buf != '\0')
    WrLstLine(buf);
  WrLstLine("");
  as_snprintf(buf, sizeof(buf), "%7ld%s",
              (long) Sum,
              getmessage((Sum == 1) ? Num_ListRegDefSumMsg : Num_ListRegDefSumsMsg));
  WrLstLine(buf);
  as_snprintf(buf, sizeof(buf), "%7ld%s",
              (long)USum,
              getmessage((USum == 1) ? Num_ListRegDefUSumMsg : Num_ListRegDefUSumsMsg));
  WrLstLine("");
}

/*--------------------------------------------------------------------------*/

void ClearCodepages(void)
{
  PTransTable Old;

  while (TransTables)
  {
    Old = TransTables;
    TransTables = Old->Next;
    free(Old->Name);
    free(Old->Table);
    free(Old);
  }
}

void PrintCodepages(void)
{
  char buf[500];
  PTransTable Table;
  int z, cnt, cnt2;

  NewPage(ChapDepth, True);
  WrLstLine(getmessage(Num_ListCodepageListHead1));
  WrLstLine(getmessage(Num_ListCodepageListHead2));
  WrLstLine("");

  cnt2 = 0;
  for (Table = TransTables; Table; Table = Table->Next)
  {
    for (z = cnt = 0; z < 256; z++)
      if (Table->Table[z] != z)
        cnt++;
    as_snprintf(buf, sizeof(buf), "%s (%d%s)", Table->Name, cnt,
                getmessage((cnt == 1) ? Num_ListCodepageChange : Num_ListCodepagePChange));
    WrLstLine(buf);
    cnt2++;
  }
  WrLstLine("");
  as_snprintf(buf, sizeof(buf), "%d%s", cnt2,
              getmessage((cnt2 == 1) ? Num_ListCodepageSumMsg : Num_ListCodepageSumsMsg));
  WrLstLine(buf);
}

/*--------------------------------------------------------------------------*/

void asmpars_init(void)
{
  tIntTypeDef *pCurr;

  serr = (char*)malloc(sizeof(char) * STRINGSIZE);
  snum = (char*)malloc(sizeof(char) * STRINGSIZE);
  FirstDefSymbol = NULL;
  FirstFunction = NULL;
  BalanceTrees = False;

  for (pCurr = IntTypeDefs; pCurr < IntTypeDefs + (sizeof(IntTypeDefs) / sizeof(*IntTypeDefs)); pCurr++)
  {
    unsigned SignType = Hi(pCurr->SignAndWidth);
    unsigned Bits, Cnt;

    Bits = Lo(pCurr->SignAndWidth) - ((SignType == 0x80) ? 1 : 0);
    for (Cnt = 0, pCurr->Mask = 0; Cnt < Bits; Cnt++)
      pCurr->Mask = (pCurr->Mask << 1) | 1;

    pCurr->Max = (LargeInt)pCurr->Mask;

    switch (SignType & 0xc0)
    {
      case 0x80:
        pCurr->Min = -pCurr->Max - 1;
        break;
      case 0xc0:
        pCurr->Min = (LargeInt)(pCurr->Mask / 2);
        pCurr->Min = -pCurr->Min - 1;
        break;
      default:
        pCurr->Min = 0;
        break;
    }
  }

  LastGlobSymbol = (char*)malloc(sizeof(char) * STRINGSIZE);
}
