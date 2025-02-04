/* code96.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* Codegenerator MCS/96-Familie                                              */
/*                                                                           */
/*****************************************************************************/

#include "stdinc.h"
#include <string.h>

#include "bpemu.h"
#include "asmdef.h"
#include "asmsub.h"
#include "asmpars.h"
#include "asmitree.h"
#include "codepseudo.h" 
#include "intpseudo.h"
#include "codevars.h"
#include "errmsg.h"
#include "strutil.h"

#include "code96.h"

enum { ModNone = -1, ModDir = 0, ModMem = 1, ModImm = 2 };

#define ModNone (-1)
#define MModDir (1 << ModDir)
#define MModMem (1 << ModMem)
#define MModImm (1 << ModImm)

#define SFRStart 2
#define SFRStop 0x17

typedef enum
{
  eForceNone = 0,
  eForceShort = 1,
  eForceLong = 2
} tForceSize;

static CPUVar CPU8096, CPU80196, CPU80196N, CPU80296;

static Byte AdrMode;
static ShortInt AdrType;
static Byte AdrVals[4];
static ShortInt OpSize;

static LongInt WSRVal, WSR1Val;
static Word WinStart, WinStop, WinEnd, WinBegin;
static Word Win1Start, Win1Stop, Win1Begin, Win1End;

static IntType MemInt;

/*---------------------------------------------------------------------------*/

static void ChkSFR(Word Adr)
{
  if ((Adr >= SFRStart) && (Adr <= SFRStop))
    WrError(ErrNum_IOAddrNotAllowed);
}

static void Chk296(Word Adr)
{
  if ((MomCPU == CPU80296) && (Adr <= 1))
    WrError(ErrNum_IOAddrNotAllowed);
}

static Boolean ChkWork(Word *Adr)
{
  /* Registeradresse, die von Fenstern ueberdeckt wird ? */

  if ((*Adr >= WinBegin) && (*Adr <= WinEnd))
    return False;

  else if ((*Adr >= Win1Begin) && (*Adr <= Win1End))
    return False;

  /* Speicheradresse in Fenster ? */

  else if ((*Adr >= WinStart) && (*Adr <= WinStop))
  {
    *Adr = (*Adr) - WinStart + WinBegin;
    return True;
  }

  else if ((*Adr >= Win1Start) && (*Adr <= Win1Stop))
  {
    *Adr = (*Adr) - Win1Start + Win1Begin;
    return True;
  }

  /* Default */

  else
    return (*Adr <= 0xff);
}

static void ChkAlign(Byte Adr)
{
  if (((OpSize == 0) && (Adr & 1))
   || ((OpSize == 1) && (Adr & 3)))
    WrError(ErrNum_AddrNotAligned);
}

static void ChkAdr(Byte Mask)
{
  if ((AdrType == ModDir) && (!(Mask & MModDir)))
  {
    AdrType = ModMem;
    AdrMode = 0;
  }

  if ((AdrType != ModNone) && (!(Mask & (1 << AdrType))))
  {
    WrError(ErrNum_InvAddrMode);
    AdrType = ModNone;
    AdrCnt = 0;
  }
}

static int SplitForceSize(const char *pArg, tForceSize *pForceSize)
{
  switch (*pArg)
  {
    case '>': *pForceSize = eForceLong; return 1;
    case '<': *pForceSize = eForceShort; return 1;
    default: return 0;
  }
}

static void DecodeAdr(const tStrComp *pArg, Byte Mask, Boolean AddrWide)
{
  LongInt AdrInt;
  LongWord AdrWord;
  Word BReg;
  Boolean OK;
  char *p, *p2;
  int ArgLen;
  Byte Reg;
  LongWord OMask;

  AdrType = ModNone;
  AdrCnt = 0;
  OMask = (1 << OpSize) - 1;

  if (*pArg->Str == '#')
  {
    switch (OpSize)
    {
      case -1:
        WrError(ErrNum_UndefOpSizes);
        break;
      case 0:
        AdrVals[0] = EvalStrIntExpressionOffs(pArg, 1, Int8, &OK);
        if (OK)
        {
          AdrType = ModImm;
          AdrCnt = 1;
          AdrMode = 1;
        }
        break;
      case 1:
        AdrWord = EvalStrIntExpressionOffs(pArg, 1, Int16, &OK);
        if (OK)
        {
          AdrType = ModImm;
          AdrCnt = 2;
          AdrMode = 1;
          AdrVals[0] = Lo(AdrWord);
          AdrVals[1] = Hi(AdrWord);
        }
        break;
    }
    goto chk;
  }

  p = QuotPos(pArg->Str, '[');
  if (p)
  {
    tStrComp Left, Mid, Right;

    StrCompSplitRef(&Left, &Mid, pArg, p);
    p2 = RQuotPos(Mid.Str, ']');
    ArgLen = strlen(Mid.Str);
    if (!p2 || (p2 > Mid.Str + ArgLen - 1) || (p2 < Mid.Str + ArgLen - 2)) WrError(ErrNum_InvAddrMode);
    else
    {
      StrCompSplitRef(&Mid, &Right, &Mid, p2);
      FirstPassUnknown = False;
      BReg = EvalStrIntExpression(&Mid, Int16, &OK);
      if (FirstPassUnknown)
        BReg = 0;
      if (OK)
      {
        if (!ChkWork(&BReg)) WrError(ErrNum_OverRange);
        else
        {
          Reg = Lo(BReg);
          ChkSFR(Reg);
          if (Reg & 1) WrError(ErrNum_AddrMustBeEven);
          else if ((strlen(Left.Str) == 0) && !strcmp(Right.Str, "+"))
          {
            AdrType = ModMem;
            AdrMode = 2;
            AdrCnt = 1;
            AdrVals[0] = Reg + 1;
          }
          else if (strlen(Right.Str) != 0) WrError(ErrNum_InvAddrMode);
          else if (strlen(Left.Str) == 0)
          {
            AdrType = ModMem;
            AdrMode = 2;
            AdrCnt = 1;
            AdrVals[0] = Reg;
          }
          else
          {
            tForceSize ForceSize = eForceNone;
            int Offset = SplitForceSize(Left.Str, &ForceSize);

            AdrInt = EvalStrIntExpressionOffs(&Left, Offset, AddrWide ? Int24 : Int16, &OK);
            if (OK)
            {
              if ((AdrInt == 0) && !ForceSize)
              {
                AdrType = ModMem;
                AdrMode = 2;
                AdrCnt = 1;
                AdrVals[0] = Reg;
              }
              else if (AddrWide)
              {
                AdrType = ModMem;
                AdrMode= 3;
                AdrCnt = 4;
                AdrVals[0] = Reg;
                AdrVals[1] = AdrInt & 0xff;
                AdrVals[2] = (AdrInt >> 8) & 0xff;
                AdrVals[3] = (AdrInt >> 16) & 0xff;
              }
              else
              {
                Boolean IsShort = (AdrInt >= -128) && (AdrInt <= 127);

                if (!ForceSize)
                  ForceSize = IsShort ? eForceShort : eForceLong;
                if (ForceSize == eForceShort)
                {
                  if ((AdrInt > 127) && !SymbolQuestionable) WrStrErrorPos(ErrNum_OverRange, &Left);
                  else if ((AdrInt < -128) && !SymbolQuestionable) WrStrErrorPos(ErrNum_UnderRange, &Left);
                  else
                  {
                    AdrType = ModMem;
                    AdrMode = 3;
                    AdrCnt = 2;
                    AdrVals[0] = Reg;
                    AdrVals[1] = Lo(AdrInt);
                  }
                }
                else
                {
                  AdrType = ModMem;
                  AdrMode = 3;
                  AdrCnt = 3;
                  AdrVals[0] = Reg + 1;
                  AdrVals[1] = Lo(AdrInt);
                  AdrVals[2] = Hi(AdrInt);
                }
              }
            }
          }
        }
      }
    }
  }
  else
  {
    tForceSize ForceSize = eForceNone;
    int Offset = SplitForceSize(pArg->Str, &ForceSize);

    FirstPassUnknown = False;
    AdrWord = EvalStrIntExpressionOffs(pArg, Offset, MemInt, &OK);
    if (FirstPassUnknown)
      AdrWord &= (0xffffffff - OMask);
    if (OK)
    {
      if (AdrWord & OMask) WrError(ErrNum_NotAligned);
      else
      {
        BReg = AdrWord & 0xffff;
        if ((!(BReg & 0xffff0000)) && ChkWork(&BReg) && !ForceSize)
        {
          AdrType = ModDir;
          AdrCnt = 1; 
          AdrVals[0] = Lo(BReg);
        }
        else if (AddrWide)
        {
          AdrType = ModMem;
          AdrMode = 3;
          AdrCnt = 4;
          AdrVals[0] = 0;
          AdrVals[1] = AdrWord & 0xff; 
          AdrVals[2] = (AdrWord >> 8) & 0xff;
          AdrVals[3] = (AdrWord >> 16) & 0xff;
        }
        else
        {
          Boolean IsShort = AdrWord >= 0xff80;

          if (!ForceSize)
            ForceSize = IsShort ? eForceShort : eForceLong;
          if (ForceSize == eForceShort)
          {
            if (!IsShort) WrStrErrorPos(ErrNum_UnderRange, pArg);
            else
            {
              AdrType = ModMem;
              AdrMode = 3;
              AdrCnt = 2;
              AdrVals[0] = 0;
              AdrVals[1] = Lo(AdrWord);
            }
          }
          else
          {
            AdrType = ModMem;
            AdrMode = 3;
            AdrCnt = 3;
            AdrVals[0] = 1;
            AdrVals[1] = Lo(AdrWord);
            AdrVals[2] = Hi(AdrWord);
          }
        }
      }
    }
  }

chk:
  ChkAdr(Mask);
}

static void CalcWSRWindow(void)
{
  WSRVal &= 0x7f;
  if (WSRVal <= 0x0f)
  {
    WinStart = 0xffff;
    WinStop = 0;
    WinBegin = 0xff;
    WinEnd = 0;
  }
  else if (WSRVal <= 0x1f)
  {
    WinBegin = 0x80;
    WinEnd = 0xff;
    WinStart = (WSRVal < 0x18) ? ((WSRVal - 0x10) << 7) : ((WSRVal + 0x20) << 7);
    WinStop = WinStart + 0x7f;
  }
  else if (WSRVal <= 0x3f)
  {
    WinBegin = 0xc0;
    WinEnd = 0xff;
    WinStart = (WSRVal < 0x30) ? ((WSRVal - 0x20) << 6) : ((WSRVal + 0x40) << 6);
    WinStop = WinStart + 0x3f;
  }
  else if (WSRVal <= 0x7f)
  {
    WinBegin = 0xe0;
    WinEnd = 0xff;
    WinStart = (WSRVal < 0x60) ? ((WSRVal - 0x40) << 5) : ((WSRVal + 0x80) << 5);
    WinStop = WinStart + 0x1f;
  }
  if ((WinStop > 0x1fdf) && (MomCPU < CPU80296))
    WinStop = 0x1fdf;
}

static void CalcWSR1Window(void)
{
  if (WSR1Val <= 0x1f)
  {
    Win1Start = 0xffff;
    Win1Stop = 0;
    Win1Begin = 0xff;
    Win1End = 0;
  }
  else if (WSR1Val <= 0x3f)
  {
    Win1Begin = 0x40;
    Win1End = 0x7f;
    Win1Start = (WSR1Val < 0x30) ? ((WSR1Val - 0x20) << 6) : ((WSR1Val + 0x40) << 6);
    Win1Stop = Win1Start + 0x3f;
  }
  else if (WSR1Val <= 0x7f)
  {
    Win1Begin = 0x60;
    Win1End = 0x7f;
    Win1Start = (WSR1Val < 0x60) ? ((WSR1Val - 0x40) << 5) : ((WSR1Val + 0x80) << 5);
    Win1Stop = Win1Start + 0x1f;
  }
  else
  {
    Win1Begin = 0x40;
    Win1End = 0x7f;
    Win1Start = (WSR1Val + 0x340) << 6;
    Win1Stop = Win1Start + 0x3f;
  }
}

static Boolean IsShortBranch(LongInt Dist)
{
  return ((Dist >= -1024) && (Dist < 1023));
}

static Boolean GetShort(Word Code, LongInt Dist)
{
  switch (Code)
  {
    case 0: return True;
    case 1: return False;
    default: return IsShortBranch(Dist);
  }
}

/*---------------------------------------------------------------------------*/

static void DecodeFixed(Word Code)
{
  if (ChkArgCnt(0, 0))
    BAsmCode[(CodeLen = 1) - 1] = Code;
}

static void DecodeALU3(Word Code)
{
  Boolean OK, Special;
  int Start;

  OpSize = Hi(Code) & 3;

  if (ChkArgCnt(2, 3))
  {
    Start = 0;
    Special = (Hi(Code) & 0x40) || FALSE;
    if (Hi(Code) & 0x80)
      BAsmCode[Start++] = 0xfe;
    BAsmCode[Start++] = 0x40 + (Ord(ArgCnt==2) << 5)
                      + ((1 - OpSize) << 4)
                      + (Lo(Code) << 2);
    DecodeAdr(&ArgStr[ArgCnt], MModImm | MModMem, False);
    if (AdrType != ModNone)
    {
      BAsmCode[Start - 1] += AdrMode;
      memcpy(BAsmCode + Start, AdrVals, AdrCnt);
      Start += AdrCnt;
      if ((Special) && (AdrMode == 0))
        ChkSFR(AdrVals[0]);
      if (ArgCnt == 3)
      {
        DecodeAdr(&ArgStr[2], MModDir, False);
        OK = (AdrType != ModNone);
        if (OK)
        {
          BAsmCode[Start++] = AdrVals[0];
          if (Special)
            ChkSFR(AdrVals[0]);
        }
      }
      else
        OK = True;
      if (OK)
      {
        DecodeAdr(&ArgStr[1], MModDir, False);
        if (AdrType != ModNone)
        {
          BAsmCode[Start] = AdrVals[0];
          CodeLen = Start + 1;
          if (Special)
          {
            ChkSFR(AdrVals[0]);
            Chk296(AdrVals[0]);
            ChkAlign(AdrVals[0]);
          }
        }
      }
    }
  }
}

static void DecodeALU2(Word Code)
{
  int Start;
  Byte HReg, Mask;
  Boolean Special;

  OpSize = Hi(Code) & 3;

  if (ChkArgCnt(2, 2))
  {
    Start = 0;
    Special = (Hi(Code) & 0x40) || FALSE;
    if (Hi(Code) & 0x80)
      BAsmCode[Start++] = 0xfe;
    HReg = ((Hi(Code) & 0x20) ? 2 : 1) << 1;
    BAsmCode[Start++] = Lo(Code) + ((1 - OpSize) << HReg);
    Mask = MModMem | ((Hi(Code) & 0x20) ? MModImm : 0);
    DecodeAdr(&ArgStr[2], Mask, False);
    if (AdrType != ModNone)
    {
      BAsmCode[Start - 1] += AdrMode;
      memcpy(BAsmCode + Start, AdrVals, AdrCnt);
      Start += AdrCnt;
      if ((Special) && (AdrMode == 0))
        ChkSFR(AdrVals[0]);
      DecodeAdr(&ArgStr[1], MModDir, False);
      if (AdrType != ModNone)
      {
        BAsmCode[Start] = AdrVals[0];
        CodeLen = 1 + Start;
        if (Special)
        {
          ChkSFR(AdrVals[0]);
          ChkAlign(AdrVals[0]);
        }
      }
    }
  }
}

static void DecodeCMPL(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(2, 2)
   && ChkMinCPU(CPU80196))
  {
    OpSize = 2;
    DecodeAdr(&ArgStr[1], MModDir, False);
    if (AdrType != ModNone)
    {
      BAsmCode[2] = AdrVals[0];
      DecodeAdr(&ArgStr[2], MModDir, False);
      if (AdrType != ModNone)
      {
        BAsmCode[1] = AdrVals[0];
        BAsmCode[0] = 0xc5;
        CodeLen = 3;
      }
    }
  }
}

static void DecodePUSH_POP(Word IsPOP)
{
  OpSize = 1;

  if (ChkArgCnt(1, 1))
  {
    DecodeAdr(&ArgStr[1], MModMem | (IsPOP ? 0 : MModImm), False);
    if (AdrType != ModNone)
    {
      CodeLen = 1 + AdrCnt;
      BAsmCode[0] = 0xc8 + AdrMode + (IsPOP << 2);
      memcpy(BAsmCode + 1, AdrVals, AdrCnt);
    }
  }
}

static void DecodeBMOV(Word Code)
{
  if (ChkArgCnt(2, 2))
  {
    OpSize = 2;
    DecodeAdr(&ArgStr[1], MModDir, False);
    if (AdrType != ModNone)
    {
      BAsmCode[2] = AdrVals[0];
      OpSize = 1;
      DecodeAdr(&ArgStr[2], MModDir, False);
      if (AdrType != ModNone)
      {
        BAsmCode[1] = AdrVals[0];
        BAsmCode[0] = Code;
        CodeLen = 3;
      }
    }
  }
}

static void DecodeALU1(Word Code)
{
  OpSize = Hi(Code) & 3;

  if (ChkArgCnt(1, 1))
  {
    DecodeAdr(&ArgStr[1], MModDir, False);
    if (AdrType != ModNone)
    {
      CodeLen = 1 + AdrCnt;
      BAsmCode[0] = Code + ((1 - OpSize) << 4);
      memcpy(BAsmCode + 1, AdrVals, AdrCnt);
      if (Hi(Code) & 0x80)
        ChkAlign(AdrVals[0]);
    }
  }
}

static void DecodeXCH(Word Code)
{
  Byte HReg;

  OpSize = Hi(Code) & 3;

  if (ChkArgCnt(2, 2)
   && ChkMinCPU(CPU80196))
  {
    DecodeAdr(&ArgStr[1], MModMem | MModDir, False);
    switch (AdrType)
    {
      case ModMem:
        if (AdrMode == 1) WrError(ErrNum_InvAddrMode);
        else
        {
          memcpy(BAsmCode + 1, AdrVals, AdrCnt);
          HReg = AdrCnt;
          BAsmCode[0] = 0x04 + ((1 - OpSize) << 4) + AdrMode;
          DecodeAdr(&ArgStr[2], MModDir, False);
          if (AdrType != ModNone)
          {
            BAsmCode[1 + HReg] = AdrVals[0];
            CodeLen = 2 + HReg;
          }
        }
        break;
      case ModDir:
        HReg = AdrVals[0];
        DecodeAdr(&ArgStr[2], MModMem, False);
        if (AdrType != ModNone)
        {
          if (AdrMode == 1) WrError(ErrNum_InvAddrMode);
          else
          {
            BAsmCode[0] = 0x04 + ((1 - OpSize) << 4) + AdrMode;
            memcpy(BAsmCode + 1, AdrVals, AdrCnt);
            BAsmCode[1 + AdrCnt] = HReg;
            CodeLen = 2 + AdrCnt;
          }
        }
        break;
    }
  }
}

static void DecodeLDBZE_LDBSE(Word Code)
{
  if (ChkArgCnt(2, 2))
  {
    OpSize = 0;
    DecodeAdr(&ArgStr[2], MModMem | MModImm, False);
    if (AdrType != ModNone)
    {
      int Start;

      BAsmCode[0] = Code + AdrMode;
      memcpy(BAsmCode + 1, AdrVals, AdrCnt);
      Start = 1 + AdrCnt;
      OpSize = 1;
      DecodeAdr(&ArgStr[1], MModDir, False);
      if (AdrType != ModNone)
      {
        BAsmCode[Start] = AdrVals[0];
        CodeLen = 1 + Start;
      }
    }
  }
}

static void DecodeNORML(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(2, 2))
  {
    OpSize = 0;
    DecodeAdr(&ArgStr[2], MModDir, False);
    if (AdrType != ModNone)
    {
      BAsmCode[1] = AdrVals[0];
      OpSize = 1;
      DecodeAdr(&ArgStr[1], MModDir, False);
      if (AdrType != ModNone)
      {
        CodeLen = 3;
        BAsmCode[0] = 0x0f;
        BAsmCode[2] = AdrVals[0];
        ChkAlign(AdrVals[0]);
      }
    }
  }
}

static void DecodeIDLPD(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 1)
   && ChkMinCPU(CPU80196))
  {
    OpSize = 0;
    DecodeAdr(&ArgStr[1], MModImm, False);
    if (AdrType != ModNone)
    {
      CodeLen = 2;
      BAsmCode[0] = 0xf6;
      BAsmCode[1] = AdrVals[0];
    }
  }
}

static void DecodeShift(Word Code)
{
  OpSize = Hi(Code) & 3;

  if (ChkArgCnt(2, 2))
  {
    DecodeAdr(&ArgStr[1], MModDir, False);
    if (AdrType != ModNone)
    {
      BAsmCode[0] = 0x08 + Lo(Code) + (Ord(OpSize == 0) << 4) + (Ord(OpSize == 2) << 2);
      BAsmCode[2] = AdrVals[0];
      OpSize = 0;
      DecodeAdr(&ArgStr[2], MModDir | MModImm, False);
      if (AdrType != ModNone)
      {
        if ((AdrType == ModImm) && (AdrVals[0] > 15)) WrError(ErrNum_OverRange);
        else if ((AdrType == ModDir) && (AdrVals[0] < 16)) WrError(ErrNum_UnderRange);
        else
        {
          BAsmCode[1] = AdrVals[0];
          CodeLen = 3;
        }
      }
    }
  }
}

static void DecodeSKIP(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 1))
  {
    OpSize = 0;
    DecodeAdr(&ArgStr[1], MModDir, False);
    if (AdrType != ModNone)
    {
      CodeLen = 2;
      BAsmCode[0] = 0;
      BAsmCode[1] = AdrVals[0];
    }
  }
}

static void DecodeELD_EST(Word Code)
{
  OpSize = Hi(Code) & 3;

  if (ChkArgCnt(2, 2)
   && ChkMinCPU(CPU80196N))
  {
    DecodeAdr(&ArgStr[2], MModMem, True);
    if (AdrType == ModMem)
    {
      if ((AdrMode == 2) && (Odd(AdrVals[0]))) WrError(ErrNum_InvAddrMode); /* kein Autoincrement */
      else
      {
        Byte HReg;

        BAsmCode[0] = Code + (AdrMode & 1) + ((1 - OpSize) << 1);
        memcpy(BAsmCode + 1, AdrVals, AdrCnt);
        HReg = 1 + AdrCnt;
        DecodeAdr(&ArgStr[1], MModDir, False);
        if (AdrType == ModDir)
        {
          BAsmCode[HReg] = AdrVals[0];
          CodeLen = HReg + 1;
        }
      }
    }
  }
}

static void DecodeMac(Word Code)
{
  if (ChkArgCnt(1, 2)
   && ChkMinCPU(CPU80296))
  {
    OpSize = 1;
    BAsmCode[0] = 0x4c + (Ord(ArgCnt == 1) << 5);
    DecodeAdr(&ArgStr[ArgCnt], MModMem | (Hi(Code) ? 0 : MModImm), False);
    if (AdrType != ModNone)
    {
      int HReg;

      BAsmCode[0] += AdrMode;
      memcpy(BAsmCode + 1, AdrVals, AdrCnt);
      HReg = 1 + AdrCnt;
      if (ArgCnt == 2)
      {
        DecodeAdr(&ArgStr[1], MModDir, False);
        if (AdrType == ModDir)
        {
          BAsmCode[HReg] = AdrVals[0];
          HReg++;
        }
      }
      if (AdrType != ModNone)
      {
        BAsmCode[HReg] = Lo(Code);
        CodeLen = 1 + HReg;
      }
    }  
  }
}

static void DecodeMVAC_MSAC(Word Code)
{
  if (ChkArgCnt(2, 2)
   && ChkMinCPU(CPU80296))
  {
    OpSize = 2;
    DecodeAdr(&ArgStr[1], MModDir, False);
    if (AdrType == ModDir)
    {
      BAsmCode[0] = 0x0d;
      BAsmCode[2] = AdrVals[0] + Code;
      OpSize = 0;
      DecodeAdr(&ArgStr[2], MModImm | MModDir, False);
      BAsmCode[1] = AdrVals[0];
      switch (AdrType)
      {
        case ModImm:
          if (AdrVals[0] > 31) WrError(ErrNum_OverRange);
          else CodeLen = 3;
          break;
        case ModDir:
          if (AdrVals[0] < 32) WrError(ErrNum_UnderRange);
          else CodeLen = 3;
      }
    } 
  }      
}        

static void DecodeRpt(Word Code)
{
  if (ChkArgCnt(1, 1)
   && ChkMinCPU(CPU80296))
  {
    OpSize = 1;
    DecodeAdr(&ArgStr[1], MModImm | MModMem, False);
    if (AdrType != ModNone)
    {
      if (AdrMode == 3) WrError(ErrNum_InvAddrMode);
      else   
      {
        BAsmCode[0] = 0x40 + AdrMode;
        memcpy(BAsmCode + 1, AdrVals, AdrCnt);
        BAsmCode[1 + AdrCnt] = Code;
        BAsmCode[2 + AdrCnt] = 4;
        CodeLen = 3 + AdrCnt;
      }
    }
  }
}

static void DecodeRel(Word Code)
{
  if (ChkArgCnt(1, 1))
  {
    Boolean OK;
    LongInt AdrInt = EvalStrIntExpression(&ArgStr[1], MemInt, &OK) - (EProgCounter() + 2);

    if (OK)
    {
      if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt > 127))) WrError(ErrNum_JmpDistTooBig);
      else
      {
        CodeLen = 2;
        BAsmCode[0] = Code;
        BAsmCode[1] = AdrInt & 0xff;
      }
    }
  }
}

static void DecodeSCALL_LCALL_CALL(Word Code)
{
  if (ChkArgCnt(1, 1))
  {
    Boolean OK;
    LongWord AdrWord = EvalStrIntExpression(&ArgStr[1], MemInt, &OK);

    if (OK)
    {
      LongInt AdrInt = AdrWord - (EProgCounter() + 2);
      Boolean IsShort = GetShort(Code, AdrInt);

      if (IsShort)
      {
        if ((!SymbolQuestionable) && (!IsShortBranch(AdrInt))) WrError(ErrNum_JmpDistTooBig);
        else
        {
          CodeLen = 2;
          BAsmCode[1] = AdrInt & 0xff;
          BAsmCode[0] = 0x28 + ((AdrInt & 0x700) >> 8);
        }
      }
      else
      {
        CodeLen = 3;
        BAsmCode[0] = 0xef;
        AdrInt--;
        BAsmCode[1] = Lo(AdrInt);
        BAsmCode[2] = Hi(AdrInt);
        if ((!SymbolQuestionable) && (IsShortBranch(AdrInt)))
          WrError(ErrNum_ShortJumpPossible);
      }
    }
  }
}

static void DecodeBR_LJMP_SJMP(Word Code)
{
  OpSize = 1;
  if (!ChkArgCnt(1, 1));
  else if ((Code == 0xff) && (QuotPos(ArgStr[1].Str, '[')))
  {
    DecodeAdr(&ArgStr[1], MModMem, False);
    if (AdrType != ModNone)
    {
      if ((AdrMode != 2) || (AdrVals[0] & 1)) WrError(ErrNum_InvAddrMode);
      else
      {
        CodeLen = 2;
        BAsmCode[0] = 0xe3;
        BAsmCode[1] = AdrVals[0];
      }
    }
  }
  else
  {
    Boolean OK;
    LongWord AdrWord = EvalStrIntExpression(&ArgStr[1], MemInt, &OK);
    if (OK)
    {
      LongInt AdrInt = AdrWord - (EProgCounter() + 2);
      Boolean IsShort = GetShort(Code, AdrInt);

      if (IsShort)
      {
        if ((!SymbolQuestionable) && (!IsShortBranch(AdrInt))) WrError(ErrNum_JmpDistTooBig);
        else
        {
          CodeLen = 2;
          BAsmCode[1] = AdrInt & 0xff;
          BAsmCode[0] = 0x20 + ((AdrInt & 0x700) >> 8);
        }
      }
      else
      {
        CodeLen = 3;
        BAsmCode[0] = 0xe7;
        AdrInt--;
        BAsmCode[1] = Lo(AdrInt);
        BAsmCode[2] = Hi(AdrInt);
        if ((!SymbolQuestionable) && (IsShortBranch(AdrInt)))
          WrError(ErrNum_ShortJumpPossible);
      }
    }
  }
}

static void DecodeTIJMP(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(3, 3)
   && ChkMinCPU(CPU80196))
  {
    OpSize = 1;
    DecodeAdr(&ArgStr[1], MModDir, False);
    if (AdrType != ModNone)
    {
      BAsmCode[3] = AdrVals[0];
      DecodeAdr(&ArgStr[2], MModDir, False);
      if (AdrType != ModNone)
      {
        BAsmCode[1] = AdrVals[0];
        OpSize = 0;
        DecodeAdr(&ArgStr[3], MModImm, False);
        if (AdrType != ModNone)
        {
          BAsmCode[2] = AdrVals[0];
          BAsmCode[0] = 0xe2;
          CodeLen = 4;
        }
      }
    }
  }
}

static void DecodeDJNZ_DJNZW(Word Size)
{
  if (ChkArgCnt(2, 2)
   && (!Size || ChkMinCPU(CPU80196)))
  {
    OpSize = Size;
    DecodeAdr(&ArgStr[1], MModDir, False);
    if (AdrType != ModNone)
    {
      Boolean OK;
      LongInt AdrInt;

      BAsmCode[0] = 0xe0 + OpSize;
      BAsmCode[1] = AdrVals[0];
      AdrInt = EvalStrIntExpression(&ArgStr[2], MemInt, &OK) - (EProgCounter() + 3);
      if (OK)
      {
        if ((!SymbolQuestionable) && ((AdrInt < -128) || (AdrInt > 127))) WrError(ErrNum_JmpDistTooBig);
        else
        {
          CodeLen = 3;
          BAsmCode[2] = AdrInt & 0xff;
        }
      }
    }
  }
  return;
}

static void DecodeJBC_JBS(Word Code)
{
  if (ChkArgCnt(3, 3))
  {
    Boolean OK;

    BAsmCode[0] = Code + EvalStrIntExpression(&ArgStr[2], UInt3, &OK);
    if (OK)
    {
      OpSize = 0;
      DecodeAdr(&ArgStr[1], MModDir, False);
      if (AdrType != ModNone)
      {
        LongInt AdrInt;

        BAsmCode[1] = AdrVals[0];
        AdrInt = EvalStrIntExpression(&ArgStr[3], MemInt, &OK) - (EProgCounter() + 3);
        if (OK)
        {
          if ((!SymbolQuestionable) && ((AdrInt<-128) || (AdrInt>127))) WrError(ErrNum_JmpDistTooBig);
          else
          {
            CodeLen = 3;
            BAsmCode[2] = AdrInt & 0xff;
          }
        }
      }
    }
  }
}

static void DecodeECALL(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 1)
   && ChkMinCPU(CPU80196N))
  {
    Boolean OK;
    LongInt AdrInt = EvalStrIntExpression(&ArgStr[1], MemInt, &OK) - (EProgCounter() + 4);
    if (OK)
    {
      BAsmCode[0] = 0xf1;
      BAsmCode[1] = AdrInt & 0xff;
      BAsmCode[2] = (AdrInt >> 8) & 0xff; 
      BAsmCode[3] = (AdrInt >> 16) & 0xff;
      CodeLen = 4;
    }
  }
}

static void DecodeEJMP_EBR(Word Code)
{
  UNUSED(Code);

  OpSize = 1;
  if (!ChkArgCnt(1, 1));
  else if (!ChkMinCPU(CPU80196N));
  else if (*ArgStr[1].Str == '[')
  {
    DecodeAdr(&ArgStr[1], MModMem, False);
    if (AdrType == ModMem)
    {
      if (AdrMode != 2) WrError(ErrNum_InvAddrMode);
      else if (Odd(AdrVals[0])) WrError(ErrNum_InvAddrMode);
      else
      {
        BAsmCode[0] = 0xe3;
        BAsmCode[1] = AdrVals[0] + 1;
        CodeLen = 2;
      }
    }
  }
  else
  {
    Boolean OK;
    LongInt AdrInt = EvalStrIntExpression(&ArgStr[1], MemInt, &OK) - (EProgCounter() + 4);
    if (OK)
    {
      BAsmCode[0] = 0xe6;
      BAsmCode[1] = AdrInt & 0xff;
      BAsmCode[2] = (AdrInt >> 8) & 0xff;
      BAsmCode[3] = (AdrInt >> 16) & 0xff;
      CodeLen = 4;
    }
  }  
}    

/*---------------------------------------------------------------------------*/

static void AddSize(char *NName, Word NCode, InstProc Proc, Word SizeMask)
{
  int l;
  char SizeName[20];

  if (SizeMask & 2)
    AddInstTable(InstTable, NName, 0x0100 | NCode, Proc);

  l = as_snprintf(SizeName, sizeof(SizeName), "%sB", NName);
  if (SizeMask & 1)
    AddInstTable(InstTable, SizeName, 0x0000 | NCode, Proc);

  if (SizeMask & 4)
  {
    SizeName[l - 1] = 'L';
    AddInstTable(InstTable, SizeName, 0x0200 | NCode, Proc);
  }
}

static void AddFixed(char *NName, Byte NCode, CPUVar NMin, CPUVar NMax)
{
  if ((MomCPU >= NMin) && (MomCPU <= NMax))
    AddInstTable(InstTable, NName, NCode, DecodeFixed);
}

static void AddALU3(char *NName, Word NCode)
{
  AddSize(NName, NCode, DecodeALU3, 3);
}

static void AddALU2(char *NName, Word NCode)
{
  AddSize(NName, NCode, DecodeALU2, 3);
}

static void AddALU1(char *NName, Word NCode)
{
  AddSize(NName, NCode, DecodeALU1, 3);
}

static void AddRel(char *NName, Byte NCode)
{
  AddInstTable(InstTable, NName, NCode, DecodeRel);
}

static void AddMac(char *NName, Word NCode, Boolean NRel)
{
  AddInstTable(InstTable, NName, NCode | (NRel ? 0x100 : 0), DecodeMac);
}

static void AddRpt(char *NName, Byte NCode)
{
  AddInstTable(InstTable, NName, NCode, DecodeRpt);
}

static void InitFields(void)
{
  InstTable = CreateInstTable(207);
  SetDynamicInstTable(InstTable);
  AddInstTable(InstTable, "CMPL", 0, DecodeCMPL);
  AddInstTable(InstTable, "PUSH", 0, DecodePUSH_POP);
  AddInstTable(InstTable, "POP" , 1, DecodePUSH_POP);
  if (MomCPU >= CPU80196)
  {
    AddInstTable(InstTable, "BMOV", 0xc1, DecodeBMOV);
    AddInstTable(InstTable, "BMOVI", 0xcd, DecodeBMOV);
  }
  if (MomCPU >= CPU80196N)
    AddInstTable(InstTable, "EBMOVI", 0xe4, DecodeBMOV);
  AddSize("XCH", 0, DecodeXCH, 3);
  AddInstTable(InstTable, "LDBZE", 0xac, DecodeLDBZE_LDBSE);
  AddInstTable(InstTable, "LDBSE", 0xbc, DecodeLDBZE_LDBSE);
  AddInstTable(InstTable, "NORML", 0, DecodeNORML);
  AddInstTable(InstTable, "IDLPD", 0, DecodeIDLPD);
  AddSize("SHR", 0, DecodeShift, 7);
  AddSize("SHL", 1, DecodeShift, 7);
  AddSize("SHRA", 2, DecodeShift, 7);
  AddInstTable(InstTable, "SKIP", 0, DecodeSKIP);
  AddSize("ELD", 0xe8, DecodeELD_EST, 3);
  AddSize("EST", 0x1c, DecodeELD_EST, 3);
  AddInstTable(InstTable, "MVAC", 1, DecodeMVAC_MSAC);
  AddInstTable(InstTable, "MSAC", 3, DecodeMVAC_MSAC);
  AddInstTable(InstTable, "CALL", 0xff, DecodeSCALL_LCALL_CALL);
  AddInstTable(InstTable, "LCALL", 1, DecodeSCALL_LCALL_CALL);
  AddInstTable(InstTable, "SCALL", 0, DecodeSCALL_LCALL_CALL);
  AddInstTable(InstTable, "BR", 0xff, DecodeBR_LJMP_SJMP);
  AddInstTable(InstTable, "LJMP", 1, DecodeBR_LJMP_SJMP);
  AddInstTable(InstTable, "SJMP", 0, DecodeBR_LJMP_SJMP);
  AddInstTable(InstTable, "TIJMP", 0, DecodeTIJMP);
  AddInstTable(InstTable, "DJNZ", 0, DecodeDJNZ_DJNZW);
  AddInstTable(InstTable, "DJNZW", 1, DecodeDJNZ_DJNZW);
  AddInstTable(InstTable, "JBC", 0x30, DecodeJBC_JBS);
  AddInstTable(InstTable, "JBS", 0x38, DecodeJBC_JBS);
  AddInstTable(InstTable, "ECALL", 0, DecodeECALL);
  AddInstTable(InstTable, "EJMP", 0, DecodeEJMP_EBR);
  AddInstTable(InstTable, "EBR", 0, DecodeEJMP_EBR);

  AddFixed("CLRC" , 0xf8, CPU8096  , CPU80296 );
  AddFixed("CLRVT", 0xfc, CPU8096  , CPU80296 );
  AddFixed("DI"   , 0xfa, CPU8096  , CPU80296 );
  AddFixed("DPTS" , 0xec, CPU80196 , CPU80196N);
  AddFixed("EI"   , 0xfb, CPU8096  , CPU80296 );
  AddFixed("EPTS" , 0xed, CPU80196 , CPU80196N);
  AddFixed("NOP"  , 0xfd, CPU8096  , CPU80296 );
  AddFixed("POPA" , 0xf5, CPU80196 , CPU80296 );
  AddFixed("POPF" , 0xf3, CPU8096  , CPU80296 );
  AddFixed("PUSHA", 0xf4, CPU80196 , CPU80296 );
  AddFixed("PUSHF", 0xf2, CPU8096  , CPU80296 );
  AddFixed("RET"  , 0xf0, CPU8096  , CPU80296 );
  AddFixed("RSC"  , 0xff, CPU8096  , CPU80296 );
  AddFixed("SETC" , 0xf9, CPU8096  , CPU80296 );
  AddFixed("TRAP" , 0xf7, CPU8096  , CPU80296 );
  AddFixed("RETI" , 0xe5, CPU80196N, CPU80296 );

  AddALU3("ADD" , 0x0001);
  AddALU3("AND" , 0x0000);
  AddALU3("MUL" , 0xc003);
  AddALU3("MULU", 0x4003);
  AddALU3("SUB" , 0x0002);

  AddALU2("ADDC", 0x20a4);
  AddALU2("CMP" , 0x2088);
  AddALU2("DIV" , 0xe08c);
  AddALU2("DIVU", 0x608c);
  AddALU2("LD"  , 0x20a0);
  AddALU2("OR"  , 0x2080);
  AddALU2("ST"  , 0x00c0);
  AddALU2("SUBC", 0x20a8);
  AddALU2("XOR" , 0x2084);

  AddALU1("CLR", 0x0001);
  AddALU1("DEC", 0x0005);
  AddALU1("EXT", 0x8006);
  AddALU1("INC", 0x0007);
  AddALU1("NEG", 0x0003);
  AddALU1("NOT", 0x0002);

  AddRel("JC"   , 0xdb);
  AddRel("JE"   , 0xdf);
  AddRel("JGE"  , 0xd6);
  AddRel("JGT"  , 0xd2);
  AddRel("JH"   , 0xd9);
  AddRel("JLE"  , 0xda);
  AddRel("JLT"  , 0xde);
  AddRel("JNC"  , 0xd3);
  AddRel("JNE"  , 0xd7);
  AddRel("JNH"  , 0xd1);
  AddRel("JNST" , 0xd0);
  AddRel("JNV"  , 0xd5);
  AddRel("JNVT" , 0xd4);
  AddRel("JST"  , 0xd8);
  AddRel("JV"   , 0xdd);
  AddRel("JVT"  , 0xdc);

  AddMac("MAC"   , 0x00, False); AddMac("SMAC"  , 0x01, False);
  AddMac("MACR"  , 0x04, True ); AddMac("SMACR" , 0x05, True ); 
  AddMac("MACZ"  , 0x08, False); AddMac("SMACZ" , 0x09, False);
  AddMac("MACRZ" , 0x0c, True ); AddMac("SMACRZ", 0x0d, True );

  AddRpt("RPT"    , 0x00); AddRpt("RPTNST" , 0x10); AddRpt("RPTNH"  , 0x11);
  AddRpt("RPTGT"  , 0x12); AddRpt("RPTNC"  , 0x13); AddRpt("RPTNVT" , 0x14);
  AddRpt("RPTNV"  , 0x15); AddRpt("RPTGE"  , 0x16); AddRpt("RPTNE"  , 0x17);
  AddRpt("RPTST"  , 0x18); AddRpt("RPTH"   , 0x19); AddRpt("RPTLE"  , 0x1a);
  AddRpt("RPTC"   , 0x1b); AddRpt("RPTVT"  , 0x1c); AddRpt("RPTV"   , 0x1d);
  AddRpt("RPTLT"  , 0x1e); AddRpt("RPTE"   , 0x1f); AddRpt("RPTI"   , 0x20);
  AddRpt("RPTINST", 0x30); AddRpt("RPTINH" , 0x31); AddRpt("RPTIGT" , 0x32);
  AddRpt("RPTINC" , 0x33); AddRpt("RPTINVT", 0x34); AddRpt("RPTINV" , 0x35);
  AddRpt("RPTIGE" , 0x36); AddRpt("RPTINE" , 0x37); AddRpt("RPTIST" , 0x38);
  AddRpt("RPTIH"  , 0x39); AddRpt("RPTILE" , 0x3a); AddRpt("RPTIC"  , 0x3b);
  AddRpt("RPTIVT" , 0x3c); AddRpt("RPTIV"  , 0x3d); AddRpt("RPTILT" , 0x3e);
  AddRpt("RPTIE"  , 0x3f);
}

static void DeinitFields(void)
{
  DestroyInstTable(InstTable);
}

/*-------------------------------------------------------------------------*/

static void MakeCode_96(void)
{
  CodeLen = 0;
  DontPrint = False;
  OpSize = -1;

  /* zu ignorierendes */

  if (Memo(""))
    return;

  /* Pseudoanweisungen */

  if (DecodeIntelPseudo(False))
    return;

  if (!LookupInstTable(InstTable, OpPart.Str))
    WrStrErrorPos(ErrNum_UnknownInstruction, &OpPart);
}

static void InitCode_96(void)
{
  WSRVal  = 0; CalcWSRWindow();
  WSR1Val = 0; CalcWSR1Window();
}

static Boolean IsDef_96(void)
{
  return False;
}

static void SwitchFrom_96(void)
{
  DeinitFields();
}

static ASSUMERec ASSUME96s[] =
{
  {"WSR" , &WSRVal , 0, 0xff, 0x00, CalcWSRWindow  },
  {"WSR1", &WSR1Val, 0, 0xbf, 0x00, CalcWSR1Window },
};

static void SwitchTo_96(void)
{
  TurnWords = False;
  ConstMode = ConstModeIntel;

  PCSymbol = "$";
  HeaderID = 0x39;
  NOPCode = 0xfd;
  DivideChars = ",";
  HasAttrs = False;

  ValidSegs = 1 << SegCode;
  Grans[SegCode ] = 1;
  ListGrans[SegCode ] = 1;
  SegInits[SegCode ] = 0;
  SegLimits[SegCode] = (MomCPU >= CPU80196N) ? 0xffffffl : 0xffff;

  MakeCode = MakeCode_96;
  IsDef = IsDef_96;
  SwitchFrom = SwitchFrom_96;

  MemInt = (MomCPU >= CPU80196N) ? UInt24 : UInt16;

  if (MomCPU >= CPU80196)
  {
    pASSUMERecs = ASSUME96s;
    ASSUMERecCnt = (MomCPU >= CPU80296) ? (sizeof(ASSUME96s) / sizeof(*ASSUME96s)) : 1;
  }

  InitFields();
}

void code96_init(void)
{
  CPU8096   = AddCPU("8096"  , SwitchTo_96);
  CPU80196  = AddCPU("80196" , SwitchTo_96);
  CPU80196N = AddCPU("80196N", SwitchTo_96);
  CPU80296  = AddCPU("80296" , SwitchTo_96);

  AddInitPassProc(InitCode_96);
}
