/* strutil.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* haeufig benoetigte String-Funktionen                                      */
/*                                                                           */
/*****************************************************************************/

#include "stdinc.h"
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

#include "strutil.h"
#undef strlen   /* VORSICHT, Rekursion!!! */

char HexStartCharacter;	    /* characters to use for 10,11,...35 */

/*--------------------------------------------------------------------------*/
/* eine bestimmte Anzahl Leerzeichen liefern */

const char *Blanks(int cnt)
{
  static const char *BlkStr = "                                                                                                           ";
  static int BlkStrLen = 0;

  if (!BlkStrLen)
    BlkStrLen = strlen(BlkStr);

  if (cnt < 0)
    cnt = 0;
  if (cnt > BlkStrLen)
    cnt = BlkStrLen;

  return BlkStr + (BlkStrLen - cnt);
}

/*!------------------------------------------------------------------------
 * \fn     SysString(char *pDest, int DestSize, LargeWord i, int System, int Stellen, Boolean ForceLeadZero, char StartCharacter)
 * \brief  convert number to string in given number system, leading zeros
 * \param  pDest where to write
 * \param  DestSize size of dest buffer
 * \param  i number to convert
 * \param  Stellen minimum length of output
 * \param  ForceLeadZero prepend zero if first character is no number
 * \param  System number system
 * \param  StartCharacter 'a' or 'A' for hex digits
 * ------------------------------------------------------------------------ */

int SysString(char *pDest, int DestSize, LargeWord i, int System, int Stellen, Boolean ForceLeadZero, char StartCharacter)
{
  int Len = 0, Cnt;
  LargeWord digit;
  char *ptr;

  if (DestSize < 1)
    return 0;

  if (Stellen > DestSize - 1)
    Stellen = DestSize - 1;

  ptr = pDest + DestSize - 1;
  *ptr = '\0';
  Cnt = Stellen;
  do
  {
    if (ptr <= pDest)
      break;

    digit = i % System;
    if (digit < 10)
      *(--ptr) = digit + '0';
    else
      *(--ptr) = digit - 10 + StartCharacter;
    i /= System;
    Cnt--;
    Len++;
  }
  while ((Cnt > 0) || (i != 0));

  if (ForceLeadZero && !isdigit(*ptr) && (ptr > pDest))
  {
    *(--ptr) = '0';
    Len++;
  }

  if (ptr != pDest)
    strmov(pDest, ptr);
  return Len;
}

/*---------------------------------------------------------------------------*/
/* strdup() is not part of ANSI C89 */

char *as_strdup(const char *s)
{
  char *ptr = (char *) malloc(strlen(s) + 1);
#ifdef CKMALLOC
  if (!ptr)
  {
    fprintf(stderr, "strdup: out of memory?\n");
    exit(255);
  }
#endif
  if (ptr != 0)
    strcpy(ptr, s);
  return ptr;
}
/*---------------------------------------------------------------------------*/
/* ...so is snprintf... */

typedef struct
{
  enum { eNotSet, eSet, eFinished } ArgState[3];
  Boolean InFormat, LeadZero, Signed, LeftAlign, AddPlus, ForceLeadZero;
  int Arg[3], CurrArg, IntSize;
} tFormatContext;

static void ResetFormatContext(tFormatContext *pContext)
{
  int z;

  for (z = 0; z < 3; z++)
  {
    pContext->Arg[z] = 0;
    pContext->ArgState[z] = eNotSet;
  }
  pContext->CurrArg = 0;
  pContext->IntSize = 0;
  pContext->InFormat =
  pContext->LeadZero =
  pContext->ForceLeadZero =
  pContext->Signed =
  pContext->LeftAlign =
  pContext->AddPlus = False;
}

static int AppendPad(char **ppDest, int *pDestSize, char Src, int Cnt)
{
  int AddCnt = Cnt;

  if (AddCnt + 1 > *pDestSize)
    AddCnt = *pDestSize - 1;
  memset(*ppDest, Src, AddCnt);
  *ppDest += AddCnt;
  *pDestSize -= AddCnt;
  return Cnt;
}

#if 0
static int FloatConvert(char *pDest, int DestSize, double Src, int Digits, Boolean TruncateTrailingZeros, char FormatType)
{
  int DecPt;
  int Sign, Result = 0;
  char *pBuf, *pEnd, *pRun;

  (void)FormatType;

  if (DestSize < Digits + 6)
  {
    *pDest = '\0';
    return Result;
  }

  if (Digits < 0)
    Digits = 6;

  pBuf = ecvt(Src, Digits + 1, &DecPt, &Sign);
  puts(pBuf);
  pEnd = pBuf + strlen(pBuf) - 1;
  if (TruncateTrailingZeros)
  {
    for (; pEnd > pBuf + 1; pEnd--)
      if (*pEnd != '0')
        break;
  }

  pRun = pDest;
  if (Sign)
    *pRun++ = '-';
  *pRun++ = *pBuf;
  *pRun++ = '.';
  memcpy(pRun, pBuf + 1, pEnd - pBuf); pRun += pEnd - pBuf;
  *pRun = '\0';
  Result = pRun - pDest;
  Result += as_snprintf(pRun, DestSize - Result, "e%+02d", DecPt - 1);
  return Result;
}
#else
static int FloatConvert(char *pDest, int DestSize, double Src, int Digits, Boolean TruncateTrailingZeros, char FormatType)
{
  char Format[10];

  (void)DestSize;
  (void)TruncateTrailingZeros;
  strcpy(Format, "%0.*e");
  Format[4] = (HexStartCharacter == 'a') ? FormatType : toupper(FormatType);
  sprintf(pDest, Format, Digits, Src);
  return strlen(pDest);
}
#endif

static int Append(char **ppDest, int *pDestSize, const char *pSrc, int Cnt, tFormatContext *pFormatContext)
{
  int AddCnt = Cnt, PadLen, Result = 0;

  PadLen = pFormatContext->Arg[0] - Cnt;
  if (PadLen < 0)
    PadLen = 0;

  if ((PadLen > 0) && !pFormatContext->LeftAlign)
    Result += AppendPad(ppDest, pDestSize, ' ', PadLen);

  if (AddCnt + 1 > *pDestSize)
    AddCnt = *pDestSize - 1;
  if (AddCnt > 0)
    memcpy(*ppDest, pSrc, AddCnt);
  *ppDest += AddCnt;
  *pDestSize -= AddCnt;

  if ((PadLen > 0) && pFormatContext->LeftAlign)
    Result += AppendPad(ppDest, pDestSize, ' ', PadLen);

  if (pFormatContext->InFormat)
    ResetFormatContext(pFormatContext);

  return Result + Cnt;
}

int as_vsnprcatf(char *pDest, int DestSize, const char *pFormat, va_list ap)
{
  const char *pFormatStart = pFormat;
  int Result = 0, OrigLen = strlen(pDest);
  tFormatContext FormatContext;
  LargeInt IntArg;

  if (DestSize == (int)sizeof(char*))
  {
    fprintf(stderr, "pointer size passed to as_vsnprcatf\n");
    exit(2);
  }

  DestSize -= OrigLen;
  if (DestSize < 0)
    DestSize = 0;
  pDest += OrigLen;

  ResetFormatContext(&FormatContext);
  for (; *pFormat; pFormat++)
    if (FormatContext.InFormat)
      switch (*pFormat)
      {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        {
          if (!FormatContext.CurrArg && !FormatContext.ArgState[FormatContext.CurrArg] && (*pFormat == '0'))
            FormatContext.LeadZero = True;
          FormatContext.Arg[FormatContext.CurrArg] = (FormatContext.Arg[FormatContext.CurrArg] * 10) + (*pFormat - '0');
          FormatContext.ArgState[FormatContext.CurrArg] = eSet;
          break;
        }
        case '-':
          if (!FormatContext.CurrArg && !FormatContext.ArgState[FormatContext.CurrArg])
            FormatContext.LeftAlign = True;
          break;
        case '+':
          FormatContext.AddPlus = True;
          break;
        case '~':
          FormatContext.ForceLeadZero = True;
          break;
        case '*':
          FormatContext.Arg[FormatContext.CurrArg] = va_arg(ap, int);
          FormatContext.ArgState[FormatContext.CurrArg] = eFinished;
          break;
        case '.':
          if (FormatContext.CurrArg < 3)
            FormatContext.CurrArg++;
          break;
        case 'c':
        {
          char ch = va_arg(ap, int);

          Result += Append(&pDest, &DestSize, &ch, 1, &FormatContext);
          break;
        }
        case '%':
          Result += Append(&pDest, &DestSize, "%", 1, &FormatContext);
          break;
        case 'l':
        {
          FormatContext.IntSize++;
          FormatContext.CurrArg = 2;
          break;
        }
        case 'd':
        {
          if (FormatContext.IntSize >= 3)
            IntArg = va_arg(ap, LargeInt);
          else
#ifndef NOLONGLONG
          if (FormatContext.IntSize >= 2)
            IntArg = va_arg(ap, long long);
          else
#endif
          if (FormatContext.IntSize >= 1)
            IntArg = va_arg(ap, long);
          else
            IntArg = va_arg(ap, int);
          FormatContext.Arg[1] = 10;
          FormatContext.Signed = True;
          goto IntCommon;
        }
        case 'u':
        {
          if (FormatContext.IntSize >= 3)
            IntArg = va_arg(ap, LargeWord);
          else
#ifndef NOLONGLONG
          if (FormatContext.IntSize >= 2)
            IntArg = va_arg(ap, unsigned long long);
          else
#endif
          if (FormatContext.IntSize >= 1)
            IntArg = va_arg(ap, unsigned long);
          else
            IntArg = va_arg(ap, unsigned);
          goto IntCommon;
        }
        case 'x':
        {
          if (FormatContext.IntSize >= 3)
            IntArg = va_arg(ap, LargeWord);
          else
#ifndef NOLONGLONG
          if (FormatContext.IntSize >= 2)
            IntArg = va_arg(ap, unsigned long long);
          else
#endif
          if (FormatContext.IntSize)
            IntArg = va_arg(ap, unsigned long);
          else
            IntArg = va_arg(ap, unsigned);
          FormatContext.Arg[1] = 16;
          goto IntCommon;
        }
        IntCommon:
        {
          char Str[100], *pStr = Str;
          int Cnt;
          int NumPadZeros = 0;

          if (FormatContext.Signed)
          {
            if (IntArg < 0)
            {
              *pStr++ = '-';
              IntArg = 0 - IntArg;
            }
            else if (FormatContext.AddPlus)
              *pStr++ = '+';
          }
          if (FormatContext.LeadZero)
          {
            NumPadZeros = FormatContext.Arg[0];
            FormatContext.Arg[0] = 0;
          }
          Cnt = (pStr - Str)
              + SysString(pStr, sizeof(Str) - (pStr - Str), IntArg,
                          FormatContext.Arg[1] ? FormatContext.Arg[1] : 10,
                          NumPadZeros, FormatContext.ForceLeadZero, HexStartCharacter);
          if (Cnt > (int)sizeof(Str))
            Cnt = sizeof(Str);
          Result += Append(&pDest, &DestSize, Str, Cnt, &FormatContext);
          break;
        }
        case 'e':
        case 'f':
        case 'g':
        {
          char Str[100];
          int Cnt;

          Cnt = FloatConvert(Str, sizeof(Str), va_arg(ap, double), FormatContext.Arg[1], False, *pFormat);
          if (Cnt > (int)sizeof(Str))
            Cnt = sizeof(Str);
          Result += Append(&pDest, &DestSize, Str, Cnt, &FormatContext);
          break;
        }
        case 's':
        {
          const char *pStr = va_arg(ap, char*);

          Result += Append(&pDest, &DestSize, pStr, strlen(pStr), &FormatContext);
          break;
        }
        default:
          fprintf(stderr, "invalid format: '%c' in '%s'\n", *pFormat, pFormatStart);
          exit(255);
      }
    else if (*pFormat == '%')
      FormatContext.InFormat = True;
    else
      Result += Append(&pDest, &DestSize, pFormat, 1, &FormatContext);
  if (DestSize > 0)
    *pDest++ = '\0';
  return Result;
}

int as_vsnprintf(char *pDest, int DestSize, const char *pFormat, va_list ap)
{
  if (DestSize > 0)
    *pDest = '\0';
  return as_vsnprcatf(pDest, DestSize, pFormat, ap);
}

int as_snprintf(char *pDest, int DestSize, const char *pFormat, ...)
{
  va_list ap;
  int Result;

  va_start(ap, pFormat);
  if (DestSize > 0)
    *pDest = '\0';
  Result = as_vsnprcatf(pDest, DestSize, pFormat, ap);
  va_end(ap);
  return Result;
}

int as_snprcatf(char *pDest, int DestSize, const char *pFormat, ...)
{
  va_list ap;
  int Result;

  va_start(ap, pFormat);
  Result = as_vsnprcatf(pDest, DestSize, pFormat, ap);
  va_end(ap);
  return Result;
}

int as_strcasecmp(const char *src1, const char *src2)
{
  if (!src1)
    src1 = "";
  if (!src2)
    src2 = "";
  while (tolower(*src1) == tolower(*src2))
  {
    if ((!*src1) && (!*src2))
      return 0;
    src1++;
    src2++;
  }
  return ((int) tolower(*src1)) - ((int) tolower(*src2));
}	

int as_strncasecmp(const char *src1, const char *src2, size_t len)
{
  if (!src1)
    src1 = "";
  if (!src2)
    src2 = "";
  while (tolower(*src1) == tolower(*src2))
  {
    if (--len == 0)
      return 0;
    if ((!*src1) && (!*src2))
      return 0;
    src1++;
    src2++;
  }
  return ((int) tolower(*src1)) - ((int) tolower(*src2));
}	

#ifdef NEEDS_STRSTR
char *strstr(const char *haystack, const char *needle)
{
  int lh = strlen(haystack), ln = strlen(needle);
  int z;
  char *p;

  for (z = 0; z <= lh - ln; z++)
    if (strncmp(p = haystack + z, needle, ln) == 0)
      return p;
  return NULL;
}
#endif

/*!------------------------------------------------------------------------
 * \fn     strrmultchr(const char *haystack, const char *needles)
 * \brief  find the last occurence of either character in string
 * \param  haystack string to search in
 * \param  needles characters to search for
 * \return last occurence or NULL
 * ------------------------------------------------------------------------ */

char *strrmultchr(const char *haystack, const char *needles)
{
  const char *pPos;

  for (pPos = haystack + strlen(haystack) - 1; pPos >= haystack; pPos--)
    if (strchr(needles, *pPos))
      return (char*)pPos;
  return NULL;
}

/*---------------------------------------------------------------------------*/
/* das originale strncpy plaettet alle ueberstehenden Zeichen mit Nullen */

int strmaxcpy(char *dest, const char *src, int Max)
{
  int cnt = strlen(src);

  /* leave room for terminating NUL */

  if (cnt > (Max - 1))
    cnt = Max - 1;
  memcpy(dest, src, cnt);
  dest[cnt] = '\0';
  return cnt;
}

/*---------------------------------------------------------------------------*/
/* einfuegen, mit Begrenzung */

int strmaxcat(char *Dest, const char *Src, int MaxLen)
{
  int TLen = strlen(Src), DLen = strlen(Dest);

  if (TLen > MaxLen - 1 - DLen)
    TLen = MaxLen - DLen - 1;
  if (TLen > 0)
  {
    memcpy(Dest + DLen, Src, TLen);
    Dest[DLen + TLen] = '\0';
    return DLen + TLen;
  }
  else
    return DLen;
}

void strprep(char *Dest, const char *Src)
{
  memmove(Dest + strlen(Src), Dest, strlen(Dest) + 1);
  memmove(Dest, Src, strlen(Src));
}

void strmaxprep(char *Dest, const char *Src, int MaxLen)
{
  int RLen, DestLen;

  RLen = strlen(Src);
  DestLen = strlen(Dest);
  if (RLen > MaxLen - DestLen - 1)
    RLen = MaxLen - DestLen - 1;
  memmove(Dest + RLen, Dest, DestLen + 1);
  memmove(Dest, Src, RLen);
}

void strins(char *Dest, const char *Src, int Pos)
{
  memmove(Dest + Pos + strlen(Src), Dest + Pos, strlen(Dest) + 1 - Pos);
  memmove(Dest + Pos, Src, strlen(Src));
}

void strmaxins(char *Dest, const char *Src, int Pos, int MaxLen)
{
  int RLen;

  RLen = strlen(Src);
  if (RLen > MaxLen - ((int)strlen(Dest)))
    RLen = MaxLen - strlen(Dest);
  memmove(Dest + Pos + RLen, Dest + Pos, strlen(Dest) + 1 - Pos);
  memmove(Dest + Pos, Src, RLen);
}

int strlencmp(const char *pStr1, unsigned Str1Len,
              const char *pStr2, unsigned Str2Len)
{
  const char *p1, *p2, *p1End, *p2End;
  int Diff;

  for (p1 = pStr1, p1End = p1 + Str1Len,
       p2 = pStr2, p2End = p2 + Str2Len;
       p1 < p1End && p2 < p2End; p1++, p2++)
  {
    Diff = ((int)*p1) - ((int)*p2);
    if (Diff)
      return Diff;
  }
  return ((int)Str1Len) - ((int)Str2Len);
}

unsigned fstrlenprint(FILE *pFile, const char *pStr, unsigned StrLen)
{
  unsigned Result = 0;
  const char *pRun, *pEnd;

  for (pRun = pStr, pEnd = pStr + StrLen; pRun < pEnd; pRun++)
    if ((*pRun == '\\') || (*pRun == '"') || (*pRun == ' ') || (!isprint(*pRun)))
    {
      fprintf(pFile, "\\%03d", *pRun);
      Result += 4;
    }
    else
    {
      fputc(*pRun, pFile);
      Result++;
    }

  return Result;
}

unsigned snstrlenprint(char *pDest, unsigned DestLen,
                       const char *pStr, unsigned StrLen,
                       char QuoteToEscape)
{
  unsigned Result = 0;
  const char *pRun, *pEnd;

  for (pRun = pStr, pEnd = pStr + StrLen; pRun < pEnd; pRun++)
    if ((*pRun == '\\') || (*pRun == QuoteToEscape))
    {
      if (DestLen < 3)
        break;
      *pDest++ = '\\';
      *pDest++ = *pRun;
      DestLen -= 2;
      Result += 2;
    }
    else if (!isprint(*pRun))
    {
      int cnt;

      if (DestLen < 5)
        break;
      cnt = as_snprintf(pDest, DestLen, "\\%03d", *pRun);
      pDest += cnt;
      DestLen -= cnt;
      Result += cnt;
    }
    else
    {
      if (DestLen < 2)
        break;
      *pDest++ = *pRun;
      DestLen--;
      Result++;
    }
  *pDest = '\0';

  return Result;
}

unsigned as_strnlen(const char *pStr, unsigned MaxLen)
{
  unsigned Res = 0;
  
  for (; (MaxLen > 0); MaxLen--, pStr++, Res++)
    if (!*pStr)
      break;
  return Res;
}

/*!------------------------------------------------------------------------
 * \fn     strreplace(char *pHaystack, const char *pFrom, const char *pTo, int ToMaxLen, unsigned HaystackSize)
 * \brief  replaces all occurences of From to To in Haystack
 * \param  pHaystack string to search in
 * \param  pFrom what to find
 * \param  pFrom what to find
 * \param  pTo what to replace it with
 * \param  ToMaxLen if not -1, max. length of pTo (not NUL-terminated)
 * \param  HaystackSize buffer capacity
 * \return # of occurences
 * ------------------------------------------------------------------------ */

int strreplace(char *pHaystack, const char *pFrom, const char *pTo, int ToMaxLen, unsigned HaystackSize)
{
  int HaystackLen = -1, FromLen = -1, ToLen = -1, Count = 0;
  int HeadLen, TailLen;
  char *pSearch, *pPos;

  pSearch = pHaystack;
  while (True)
  {
    /* find an occurence */

    pPos = strstr(pSearch, pFrom);
    if (!pPos)
      return Count;
      
    /* compute some stuff upon first occurence when needed */
      
    if (FromLen < 0)
    {
      HaystackLen = strlen(pHaystack);
      FromLen = strlen(pFrom);
    }
    ToLen = (ToMaxLen > 0) ? as_strnlen(pTo, ToMaxLen) : strlen(pTo);

    /* See how much of the remainder behind 'To' still fits into buffer after replacement,
       and move accordingly: */

    HeadLen = pPos - pHaystack;
    TailLen = HaystackLen - HeadLen - FromLen;
    if (HeadLen + ToLen + TailLen >= (int)HaystackSize)
    {
      TailLen = HaystackSize - 1 - HeadLen - ToLen;
      if (TailLen < 0)
        TailLen = 0;
    }
    if (TailLen > 0)
      memmove(pPos + ToLen, pPos + FromLen, TailLen); 

    /* See how much of 'To' still fits into buffer, and set accordingly: */
      
    if (HeadLen + ToLen >= (int)HaystackSize)
    {
      ToLen = HaystackSize - 1 - ToLen;
      if (ToLen < 0)
        ToLen = 0;
    }
    if (ToLen > 0)
      memcpy(pPos, pTo, ToLen);
      
    /* Update length & terminate new string */
      
    HaystackLen = HeadLen + ToLen + TailLen;
    pHaystack[HaystackLen] = '\0';

    /* continue searching behind replacement: */

    pSearch = &pHaystack[HeadLen + ToLen];
    
    Count++;
  }
}

/*---------------------------------------------------------------------------*/
/* Bis Zeilenende lesen */

void ReadLn(FILE *Datei, char *Zeile)
{
  char *ptr;
  int l;

  *Zeile = '\0';
  ptr = fgets(Zeile, 256, Datei);
  if ((!ptr) && (ferror(Datei) != 0))
    *Zeile = '\0';
  l = strlen(Zeile);
  if ((l > 0) && (Zeile[l - 1] == '\n'))
    Zeile[--l] = '\0';
  if ((l > 0) && (Zeile[l - 1] == '\r'))
    Zeile[--l] = '\0';
  if ((l > 0) && (Zeile[l - 1] == 26))
    Zeile[--l] = '\0';
}

#if 0

static void dump(const char *pLine, unsigned Cnt)
{
  unsigned z;

  fputc('\n', stderr);
  for (z = 0; z < Cnt; z++)
  {
    fprintf(stderr, " %02x", pLine[z]);
    if ((z & 15) == 15)
      fputc('\n', stderr);
  }
  fputc('\n', stderr);
}

#endif

int ReadLnCont(FILE *Datei, char *Zeile, int MaxLen)
{
  char *ptr, *pDest;
  int l, RemLen, Count;
  Boolean cont, Terminated;

  /* read from input until either string has reached maximum length,
     or no continuation is present */

  RemLen = MaxLen;
  pDest = Zeile;
  Count = 0;
  do
  {
    /* get a line from file */

    Terminated = False;
    *pDest = '\0';
    ptr = fgets(pDest, RemLen, Datei);
    if ((!ptr) && (ferror(Datei) != 0))
      *pDest = '\0';

    /* strip off trailing CR/LF */

    l = strlen(pDest);
    cont = False;
    if ((l > 0) && (pDest[l - 1] == '\n'))
    {
      pDest[--l] = '\0';
      Terminated = True;
    }
    if ((l > 0) && (pDest[l - 1] == '\r'))
      pDest[--l] = '\0';

    /* yes - this is necessary, when we get an old DOS textfile with
       Ctrl-Z as EOF */

    if ((l > 0) && (pDest[l - 1] == 26))
      pDest[--l] = '\0';

    /* optional line continuation */

    if ((l > 0) && (pDest[l - 1] == '\\'))
    {
      pDest[--l] = '\0';
      cont = True;
    }

    /* prepare for next chunk */

    RemLen -= l;
    pDest += l;
    Count++;
  }
  while ((RemLen > 2) && (cont));

  if (!Terminated)
  {
    char Tmp[100];

    while (TRUE)
    {
      Terminated = False;
      ptr = fgets(Tmp, sizeof(Tmp), Datei);
      if (!ptr)
        break;
      l = strlen(Tmp);
      if (!l)
        break;
      if ((l > 0) && (Tmp[l - 1] == '\n'))
        break;
    }
  }

  return Count;
}

/*!------------------------------------------------------------------------
 * \fn     DigitVal(char ch, int Base)
 * \brief  get value of hex digit
 * \param  ch digit
 * \param  Base Number System
 * \return 0..Base-1 or -1 if no valid digit
 * ------------------------------------------------------------------------ */

int DigitVal(char ch, int Base)
{
  int Result;
  
  /* Ziffern 0..9 ergeben selbiges */

  if ((ch >= '0') && (ch <= '9'))
    Result = ch - '0';

  /* Grossbuchstaben fuer Hexziffern */

  else if ((ch >= 'A') && (ch <= 'Z'))
    Result = ch - 'A' + 10;

  /* Kleinbuchstaben nicht vergessen...! */

  else if ((ch >= 'a') && (ch <= 'z'))
    Result = ch - 'a' + 10;

  /* alles andere ist Schrott */

  else
    Result = -1;
    
  return (Result >= Base) ? -1 : Result;
}

/*--------------------------------------------------------------------*/
/* Zahlenkonstante umsetzen: $ hex, % binaer, @ oktal */
/* inp: Eingabezeichenkette */
/* erg: Zeiger auf Ergebnis-Longint */
/* liefert TRUE, falls fehlerfrei, sonst FALSE */

LargeInt ConstLongInt(const char *inp, Boolean *pErr, LongInt Base)
{
  static const char Prefixes[4] = { '$', '@', '%', '\0' }; /* die moeglichen Zahlensysteme */
  static const char Postfixes[4] = { 'H', 'O', '\0', '\0' };
  static const LongInt Bases[3] = { 16, 8, 2 };            /* die dazugehoerigen Basen */
  LargeInt erg, val;
  int z, vorz = 1;  /* Vermischtes */
  int InpLen = strlen(inp);

  /* eventuelles Vorzeichen abspalten */

  if (*inp == '-')
  {
    vorz = -1;
    inp++;
    InpLen--;
  }

  /* Sonderbehandlung 0x --> $ */

  if ((InpLen >= 2)
   && (*inp == '0')
   && (mytoupper(inp[1]) == 'X'))
  {
    inp += 2;
    InpLen -= 2;
    Base = 16;
  }

  /* Jetzt das Zahlensystem feststellen.  Vorgabe ist dezimal, was
     sich aber durch den Initialwert von Base jederzeit aendern
     laesst.  Der break-Befehl verhindert, dass mehrere Basenzeichen
     hintereinander eingegeben werden koennen */

  else if (InpLen > 0)
  {
    for (z = 0; z < 3; z++)
      if (*inp == Prefixes[z])
      {
        Base = Bases[z];
        inp++;
        InpLen--;
        break;
      }
      else if (mytoupper(inp[InpLen - 1]) == Postfixes[z])
      {
        Base = Bases[z];
        InpLen--;
        break;
      }
  }

  /* jetzt die Zahlenzeichen der Reihe nach durchverwursten */

  erg = 0;
  *pErr = False;
  for(; InpLen > 0; inp++, InpLen--)
  {
    val = DigitVal(*inp, 16);
    if (val < -0)
      break;

    /* entsprechend der Basis zulaessige Ziffer ? */

    if (val >= Base)
      break;

    /* Zahl linksschieben, zusammenfassen, naechster bitte */

    erg = erg * Base + val;
  }

  /* bis zum Ende durchgelaufen ? */

  if (!InpLen)
  {
    /* Vorzeichen beruecksichtigen */

    erg *= vorz;
    *pErr = True;
  }

  return erg;
}

/*--------------------------------------------------------------------------*/
/* alle Leerzeichen aus einem String loeschen */

void KillBlanks(char *s)
{
  char *z, *dest;
  Boolean InSgl = False, InDbl = False, ThisEscaped = False, NextEscaped = False;

  dest = s;
  for (z = s; *z != '\0'; z++, ThisEscaped = NextEscaped)
  {
    NextEscaped = False;
    switch (*z)
    {
      case '\'':
        if (!InDbl && !ThisEscaped)
          InSgl = !InSgl;
        break;
      case '"':
        if (!InSgl && !ThisEscaped)
          InDbl = !InDbl;
        break;
      case '\\':
        if ((InSgl || InDbl) && !ThisEscaped)
          NextEscaped = True;
        break;
    }
    if (!isspace((unsigned char)*z) || InSgl || InDbl)
      *dest++ = *z;
  }
  *dest = '\0';
}

int CopyNoBlanks(char *pDest, const char *pSrc, int MaxLen)
{
  const char *pSrcRun;
  char *pDestRun = pDest;
  int Cnt = 0;
  Byte Flags = 0;
  char ch;
  Boolean ThisEscaped, PrevEscaped;

  /* leave space for NUL */

  MaxLen--;

  PrevEscaped = False;
  for (pSrcRun = pSrc; *pSrcRun; pSrcRun++)
  {
    ch = *pSrcRun;
    ThisEscaped = False;
    switch (ch)
    {
      case '\'':
        if (!(Flags & 2) && !PrevEscaped)
          Flags ^= 1;
        break;
      case '"':
        if (!(Flags & 1) && !PrevEscaped)
          Flags ^= 2;
        break;
      case '\\':
        if (!PrevEscaped)
          ThisEscaped = True;
        break;
    }
    if ((!isspace((unsigned char)ch)) || (Flags))
      *(pDestRun++) = ch;
    if (++Cnt >= MaxLen)
      break;
    PrevEscaped = ThisEscaped;
  }
  *pDestRun = '\0';

  return Cnt;
}

/*--------------------------------------------------------------------------*/
/* fuehrende Leerzeichen loeschen */

int KillPrefBlanks(char *s)
{
  char *z = s;

  while ((*z != '\0') && (isspace((unsigned char)*z)))
    z++;
  if (z != s)
    strmov(s, z);
  return z - s;
}

/*--------------------------------------------------------------------------*/
/* anhaengende Leerzeichen loeschen */

int KillPostBlanks(char *s)
{
  char *z = s + strlen(s) - 1;
  int count = 0;

  while ((z >= s) && (isspace((unsigned char)*z)))
  {
    *(z--) = '\0';
    count++;
  }
  return count;
}

/*--------------------------------------------------------------------------*/

int strqcmp(const char *s1, const char *s2)
{
  int erg = (*s1) - (*s2);

  return (erg != 0) ? erg : strcmp(s1, s2);
}

/*--------------------------------------------------------------------------*/

/* we need a strcpy() with a defined behaviour in case of overlapping source
   and destination: */

char *strmov(char *pDest, const char *pSrc)
{
  memmove(pDest, pSrc, strlen(pSrc) + 1);
  return pDest;
}

#ifdef __GNUC__

#ifdef strcpy
# undef strcpy
#endif
char *strcpy(char *pDest, const char *pSrc)
{
  int l = strlen(pSrc) + 1;
  int Overlap = 0;

  if (pSrc < pDest)
  {
    if (pSrc + l > pDest)
      Overlap = 1;
  }
  else if (pSrc > pDest)
  {
    if (pDest + l > pSrc)
      Overlap = 1;
  }
  else if (l > 0)
  {
    Overlap = 1;
  }

  if (Overlap)
  {
    fprintf(stderr, "overlapping strcpy() called from address %p, resolve this address with addr2line and report to author\n",
            __builtin_return_address(0));
    abort();
  }

  return strmov(pDest, pSrc);
}

#endif

/*!------------------------------------------------------------------------
 * \fn     strmemcpy(char *pDest, int DestSize, const char *pSrc, int SrcLen)
 * \brief  copy string with length limitation
 * \param  pDest where to write
 * \param  DestSize destination capacity
 * \param  pSrc copy source
 * \param  SrcLen # of characters to copy at most
 * \return actual, possibly limited length
 * ------------------------------------------------------------------------ */

int strmemcpy(char *pDest, int DestSize, const char *pSrc, int SrcLen)
{
  if (DestSize < SrcLen + 1)
    SrcLen = DestSize - 1;
  if (SrcLen < 0)
    SrcLen = 0;
  memmove(pDest, pSrc, SrcLen);
  pDest[SrcLen] = '\0';
  return SrcLen;
}

/*--------------------------------------------------------------------------*/

char *ParenthPos(char *pHaystack, char Needle)
{
  char *pRun;
  int Level = 0;

  for (pRun = pHaystack; *pRun; pRun++)
  {
    switch (*pRun)
    {
      case '(':
        Level++;
        break;
      case ')':
        if (Level < 1)
          return NULL;
        Level--;
        break;
      default:
        if (*pRun == Needle && !Level)
          return pRun;
    }
  }
  return NULL;
}

/*!------------------------------------------------------------------------
 * \fn     TabCompressed(char in)
 * \brief  replace TABs with spaces for error display
 * \param  in character to compress
 * \return compressed result
 * ------------------------------------------------------------------------ */

char TabCompressed(char in)
{
  return (in == '\t') ? ' ' : (myisprint(in) ? in : '*');
}

/*--------------------------------------------------------------------------*/

void strutil_init(void)
{
  HexStartCharacter = 'A';
}
