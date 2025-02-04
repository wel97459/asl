/* code48.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* Codegeneratormodul MCS-48-Familie                                         */
/*                                                                           */
/*****************************************************************************/

#include "stdinc.h"
#include "bpemu.h"
#include <string.h>
#include <ctype.h>

#include "nls.h"
#include "strutil.h"
#include "stringlists.h"
#include "asmdef.h"
#include "asmsub.h"
#include "asmpars.h"
#include "asmitree.h"
#include "intpseudo.h"
#include "codevars.h"
#include "errmsg.h"

#include "code48.h"

typedef struct
{
  char *Name;
  Byte Code;
} SelOrder;

enum
{
  ModImm = 0,
  ModReg = 1,
  ModInd = 2,
  ModAcc = 3,
  ModNone = -1
};

#define eCPUFlagCMOS (1ul << 0)
#define eCPUFlagSiemens (1ul << 1)
#define eCPUFlagDEC_DJNZ_IREG (1ul << 2)
#define eCPUFlagXMem (1ul << 3)
#define eCPUFlagUPIPort (1ul << 4)
#define eCPUFlagPort0 (1ul << 5)
#define eCPUFlagPort1 (1ul << 6)
#define eCPUFlagPort2 (1ul << 7)
#define eCPUFlagIOExpander (1ul << 8)
#define eCPUFlagUserFlags (1ul << 9)
#define eCPUFlagT0 (1ul << 10)
#define eCPUFlagT0CLK (1ul << 11)
#define eCPUFlagCondBitJmp (1ul << 12)
#define eCPUFlagTransferA_PSW (1ul << 13)
#define eCPUFlagBUS (1ul << 14)
#define eCPUFlagRegBanks (1ul << 15)
#define eCPUFlagADConv (1ul << 16)
#define eCPUFlagLogToPort (1ul << 17)
#define eCPUFlagDEC_REG (1ul << 18)
#define eCPUFlagMOVP3 (1ul << 19)
#define eCPUFlagINTLogic (1ul << 20)
#define eCPUFlagOKI (1ul << 21)
#define eCPUFlagSerial (1ul << 22)
#define eCPUFlag84xx (1ul << 23)

typedef struct
{
  const char *pName;
  Word CodeSize;
  LongWord Flags;
} tCPUProps;

#define ClrCplCnt 4
#define SelOrderCnt 8

static const tCPUProps *pCurrCPUProps;
static ShortInt AdrMode;
static Byte AdrVal;
static char **ClrCplVals;
static Byte *ClrCplCodes;
static SelOrder *SelOrders;

/****************************************************************************/

static Boolean DecodeReg(const char *pAsc, Byte *pErg)
{
  char *s;

  if (FindRegDef(pAsc, &s))
    pAsc = s;

  if ((strlen(pAsc) != 2)
   || (mytoupper(pAsc[0]) != 'R')
   || (!isdigit(pAsc[1])))
    return False;

  *pErg = pAsc[1] - '0';
  return (*pErg <= 7);
}

static Boolean IsPort(const char *pArg, Word PortMask, Byte *pPortNum)
{
  if (!as_strcasecmp(pArg, "BUS"))
    *pPortNum = 8;
  else if ((strlen(pArg) == 2)
        && (mytoupper(pArg[0]) == 'P')
        && isdigit(pArg[1]))
    *pPortNum = pArg[1] - '0';
  else
    return False;

  return !!(PortMask & (1 << *pPortNum));
}

static Boolean IsSerialPort(const char *pArg, Word PortMask, Byte *pPortNum)
{
  if ((strlen(pArg) == 2)
   && (mytoupper(pArg[0]) == 'S')
   && isdigit(pArg[1]))
    *pPortNum = pArg[1] - '0';
  else
    return False;

  return !!(PortMask & (1 << *pPortNum));
}

static void DecodeAdr(const tStrComp *pArg)
{
  Boolean OK;

  AdrMode = ModNone;

  if (*pArg->Str == '\0') return;

  if (!as_strcasecmp(pArg->Str, "A"))
    AdrMode = ModAcc;

  else if (*pArg->Str == '#')
  {
    AdrVal = EvalStrIntExpressionOffs(pArg, 1, Int8, &OK);
    if (OK)
    {
      AdrMode = ModImm;
      BAsmCode[1] = AdrVal;
    }
  }

  else if (DecodeReg(pArg->Str, &AdrVal))
    AdrMode = ModReg;

  else if ((*pArg->Str == '@')
        && (DecodeReg(pArg->Str + 1, &AdrVal))
        && (AdrVal <= 1))
    AdrMode = ModInd;
}

static Boolean ChkCPUFlags(LongWord CPUFlags)
{
  if (pCurrCPUProps->Flags & CPUFlags)
    return True;
  WrStrErrorPos(ErrNum_InstructionNotSupported, &OpPart);
  return False;
}

static Boolean AChkCPUFlags(LongWord CPUFlags, const tStrComp *pArg)
{
  if (pCurrCPUProps->Flags & CPUFlags)
    return True;
  WrStrErrorPos(ErrNum_InvAddrMode, pArg);
  return False;
}

static void ChkPx(Byte PortNum, const tStrComp *pArg)
{
  if (!(pCurrCPUProps->Flags & (eCPUFlagPort0 << PortNum)))
  {
    WrStrErrorPos(ErrNum_InvAddrMode, pArg);
    CodeLen = 0;
  }
}

static Boolean IsIReg3(const char *pAsc)
{
  Byte RegNum;

  if (*pAsc != '@')
    return False;
  return DecodeReg(pAsc + 1, &RegNum) && (RegNum == 3);
}

/****************************************************************************/

static void DecodeADD_ADDC(Word Code)
{
  if (!ChkArgCnt(2, 2));
  else if (as_strcasecmp(ArgStr[1].Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else
  {
    DecodeAdr(&ArgStr[2]);
    if ((AdrMode == ModNone) || (AdrMode == ModAcc)) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
    else
    {
      switch (AdrMode)
      {
        case ModImm:
          CodeLen = 2;
          BAsmCode[0] = Code + 0x03;
          break;
        case ModReg:
          CodeLen = 1;
          BAsmCode[0] = Code + 0x68 + AdrVal;
          break;
        case ModInd:
          CodeLen = 1;
          BAsmCode[0] = Code + 0x60 + AdrVal;
          break;
      }
    }
  }
}

static void DecodeANL_ORL_XRL(Word Code)
{
  Byte PortNum;
  Word PortMask = 0x06;

  if (pCurrCPUProps->Flags & eCPUFlagBUS)
    PortMask |= 0x100;
  if (pCurrCPUProps->Flags & eCPUFlag84xx)
    PortMask |= 0x01;

  if (!ChkArgCnt(2, 2));
  else if (!as_strcasecmp(ArgStr[1].Str, "A"))
  {
    DecodeAdr(&ArgStr[2]);
    if ((AdrMode == -1) || (AdrMode == ModAcc)) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
    else
    {
      switch (AdrMode)
      {
        case ModImm:
          CodeLen = 2;
          BAsmCode[0] = Code + 0x43;
          break;
        case ModReg:
          CodeLen = 1;
          BAsmCode[0] = Code + 0x48 + AdrVal;
          break;
        case ModInd:
          CodeLen = 1;
          BAsmCode[0] = Code + 0x40 + AdrVal;
          break;
      }
    }
  }
  else if (IsPort(ArgStr[1].Str, PortMask, &PortNum))
  {
    if (Code == 0x90) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]); /* no XRL to ports */
    else if (AChkCPUFlags(eCPUFlagLogToPort, &ArgStr[1]))
    {
      DecodeAdr(&ArgStr[2]);
      if (AdrMode != ModImm) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
      else
      {
        CodeLen = 2;
        BAsmCode[0] = Code + 0x88 + (PortNum & 3);
        if (PortNum)
          ChkPx(PortNum, &ArgStr[1]);
      }
    }
  }
  else
    WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
}

static void DecodeCALL_JMP(Word Code)
{
  if (!ChkArgCnt(1, 1));
  else if ((EProgCounter() & 0x7fe) == 0x7fe) WrError(ErrNum_NotOnThisAddress);
  else
  {
    Boolean OK;
    Word AdrWord = EvalStrIntExpression(&ArgStr[1], Int16, &OK);

    if (OK)
    {
      if (AdrWord > SegLimits[SegCode]) WrStrErrorPos(ErrNum_OverRange, &ArgStr[1]);
      else
      {
        if ((((int)EProgCounter()) & 0x800) != (AdrWord & 0x800))
        {
          BAsmCode[0] = 0xe5 + ((AdrWord & 0x800) >> 7);
          CodeLen = 1;
        }
        BAsmCode[CodeLen + 1] = AdrWord & 0xff;
        BAsmCode[CodeLen] = Code + ((AdrWord & 0x700) >> 3);
        CodeLen += 2;
        ChkSpace(SegCode);
      }
    }
  }
}

static void DecodeCLR_CPL(Word Code)
{
  if (!ChkArgCnt(1, 1));
  else
  {
    int z = 0;
    Boolean OK = False;

    NLS_UpString(ArgStr[1].Str);
    do
    {
      if (!strcmp(ClrCplVals[z], ArgStr[1].Str))
      {
        if ((*ArgStr[1].Str == 'F') && !(pCurrCPUProps->Flags & eCPUFlagUserFlags)) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
        else
        {
          CodeLen = 1;
          BAsmCode[0] = ClrCplCodes[z];
          OK = True;
        }
      }
      z++;
    }
    while ((z < ClrCplCnt) && (CodeLen != 1));
    if (!OK)
      WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
    else
      BAsmCode[0] += Code;
  }
}

static void DecodeAcc(Word Code)
{
  if (!ChkArgCnt(1, 1));
  else if (as_strcasecmp(ArgStr[1].Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else
  {
    CodeLen = 1;
    BAsmCode[0] = Code;
  }
}

static void DecodeDEC(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(1, 1));
  else
  {
    DecodeAdr(&ArgStr[1]);
    switch (AdrMode)
    {
      case ModAcc:
        CodeLen = 1;
        BAsmCode[0] = 0x07;
        break;
      case ModReg:
        if (AChkCPUFlags(eCPUFlagDEC_REG, &ArgStr[1]))
        {
          CodeLen = 1;
          BAsmCode[0] = 0xc8 + AdrVal;
        }
        break;
      case ModInd:
        if (AChkCPUFlags(eCPUFlagDEC_DJNZ_IREG, &ArgStr[1]))
        {
          CodeLen = 1;
          BAsmCode[0] = 0xc0 | AdrVal;
        }
        break;
      default:
        WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
    }
  }
}

static void DecodeDIS_EN(Word Code)
{
  if (ChkArgCnt(1, 1))
  {
    NLS_UpString(ArgStr[1].Str);
    if (!strcmp(ArgStr[1].Str, "I"))
    {
      if (AChkCPUFlags(eCPUFlagINTLogic, &ArgStr[1]))
      {
        CodeLen = 1;
        BAsmCode[0] = Code + 0x05;
      }
    }
    else if (!strcmp(ArgStr[1].Str, "TCNTI"))
    {
      if (AChkCPUFlags(eCPUFlagINTLogic, &ArgStr[1]))
      {
        CodeLen = 1;
        BAsmCode[0] = Code + 0x25;
      }
    }
    else if (!strcmp(ArgStr[1].Str, "SI"))
    {
      if (AChkCPUFlags(eCPUFlagSerial, &ArgStr[1]))
      {
        CodeLen = 1;
        BAsmCode[0] = Code + 0x85;
      }
    }
    else if ((Memo("EN")) && (!strcmp(ArgStr[1].Str, "DMA")))
    {
      if (AChkCPUFlags(eCPUFlagUPIPort, &ArgStr[1]))
      {
        BAsmCode[0] = Code + 0xe5;
        CodeLen = 1;
      }
    }
    else if ((Memo("EN")) && (!strcmp(ArgStr[1].Str, "FLAGS")))
    {
      if (AChkCPUFlags(eCPUFlagUPIPort, &ArgStr[1]))
      {
        BAsmCode[0] = Code + 0xf5;
        CodeLen = 1;
      }
    }
    else
      WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  }
}

static void DecodeDJNZ(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(2, 2));
  else
  {
    DecodeAdr(&ArgStr[1]);
    switch (AdrMode)
    {
      case ModReg:
        CodeLen = 1;
        BAsmCode[0] = 0xe8 + AdrVal;
        break;
      case ModInd:
        if (AChkCPUFlags(eCPUFlagDEC_DJNZ_IREG, &ArgStr[1]))
        {
          CodeLen = 1;
          BAsmCode[0] = 0xe0 + AdrVal;
        }
        break;
      default:
        WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
    }
    if (CodeLen > 0)
    {
      Boolean OK;
      Word AdrWord;

      FirstPassUnknown = False;
      AdrWord = EvalStrIntExpression(&ArgStr[2], Int16, &OK);
      if (OK)
      {
        if (ChkSamePage(EProgCounter() + CodeLen, AdrWord, 8))
          BAsmCode[CodeLen++] = AdrWord & 0xff;
      }
    }
  }
}

static void DecodeENT0(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 1) && ChkCPUFlags(eCPUFlagT0CLK))
  {
    if (as_strcasecmp(ArgStr[1].Str, "CLK")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
    else
    {
      CodeLen = 1;
      BAsmCode[0] = 0x75;
    }
  }
}

static void DecodeINC(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(1, 1));
  else
  {
    DecodeAdr(&ArgStr[1]);
    switch (AdrMode)
    {
      case ModAcc:
        CodeLen = 1;
        BAsmCode[0] = 0x17;
        break;
      case ModReg:
        CodeLen = 1;
        BAsmCode[0] = 0x18 + AdrVal;
        break;
      case ModInd:
        CodeLen = 1;
        BAsmCode[0] = 0x10 + AdrVal;
        break;
      default:
        WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
    }
  }
}

static void DecodeIN(Word Code)
{
  Byte PortNum;

  UNUSED(Code);

  if (!ChkArgCnt(2, 2));
  else if (as_strcasecmp(ArgStr[1].Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else if (!as_strcasecmp(ArgStr[2].Str, "DBB"))
  {
    if (AChkCPUFlags(eCPUFlagUPIPort, &ArgStr[2]))
    {
      CodeLen = 1;
      BAsmCode[0] = 0x22;
    }
  }
  else if (!IsPort(ArgStr[2].Str, 0x07, &PortNum)) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
  else
  {
    CodeLen = 1;
    BAsmCode[0] = 0x08 + PortNum;
    ChkPx(PortNum, &ArgStr[2]);
  }
}

static void DecodeINS(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(2, 2));
  else if (as_strcasecmp(ArgStr[1].Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else if (as_strcasecmp(ArgStr[2].Str, "BUS")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
  else if (AChkCPUFlags(eCPUFlagBUS, &ArgStr[2]))
  {
    CodeLen = 1;
    BAsmCode[0] = 0x08;
  }
}

static void DecodeJMPP(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(1, 1));
  else if (as_strcasecmp(ArgStr[1].Str, "@A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else
  {
    CodeLen = 1;
    BAsmCode[0] = 0xb3;
  }
}

static void DecodeCond(Word Code)
{
  if (ChkArgCnt(1, 1))
  {
    Boolean OK;
    Word AdrWord;

    FirstPassUnknown = False;
    AdrWord = EvalStrIntExpression(&ArgStr[1], UInt12, &OK);
    if (OK && ChkSamePage(EProgCounter() + 1, AdrWord, 8))
    {
      CodeLen = 2;
      BAsmCode[0] = Code;
      BAsmCode[1] = AdrWord & 0xff;
      ChkSpace(SegCode);
    }
  }
}

static void DecodeJB(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(2, 2) && ChkCPUFlags(eCPUFlagCondBitJmp))
  {
    Boolean OK;
    AdrVal = EvalStrIntExpression(&ArgStr[1], UInt3, &OK);
    if (OK)
    {
      Word AdrWord;

      FirstPassUnknown = False;
      AdrWord = EvalStrIntExpression(&ArgStr[2], UInt12, &OK);
      if (OK && ChkSamePage(EProgCounter() + 1, AdrWord, 8))
      {
        CodeLen = 2;
        BAsmCode[0] = 0x12 + (AdrVal << 5);
        BAsmCode[1] = AdrWord & 0xff;
      }
    }
  }
}

static void DecodeMOV(Word Code)
{
  Byte PortNum;

  UNUSED(Code);

  if (!ChkArgCnt(2, 2));
  else if (!as_strcasecmp(ArgStr[1].Str, "A"))
  {
    if (!as_strcasecmp(ArgStr[2].Str, "T"))
    {
      CodeLen = 1;
      BAsmCode[0] = 0x42;
    }
    else if (IsPort(ArgStr[2].Str, 0x06, &PortNum))
    {
      if (AChkCPUFlags(eCPUFlagOKI, &ArgStr[2]))
      {
        CodeLen = 1;
        BAsmCode[0] = 0x53 + (PortNum << 4);
      }
    }
    else if (!as_strcasecmp(ArgStr[2].Str, "PSW"))
    {
      if (AChkCPUFlags(eCPUFlagTransferA_PSW, &ArgStr[2]))
      {
        CodeLen = 1;
        BAsmCode[0] = 0xc7;
      }
    }
    else if (IsSerialPort(ArgStr[2].Str, 0x03, &PortNum))
    {
      if (AChkCPUFlags(eCPUFlagSerial, &ArgStr[2]))
      {
        CodeLen = 1;
        BAsmCode[0] = 0x0c + PortNum;
      }
    }
    else
    {
       DecodeAdr(&ArgStr[2]);
       switch (AdrMode)
       {
         case ModReg:
           CodeLen = 1;
           BAsmCode[0] = 0xf8 + AdrVal;
           break;
         case ModInd:
           CodeLen = 1;
           BAsmCode[0] = 0xf0 + AdrVal;
           break;
         case ModImm:
           CodeLen = 2;
           BAsmCode[0] = 0x23;
           break;
         default:
           WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
       }
    }
  }
  else if (IsPort(ArgStr[1].Str, 0x02, &PortNum))
  {
    if (IsIReg3(ArgStr[2].Str))
    {
      if (AChkCPUFlags(eCPUFlagOKI, &ArgStr[1]))
      {
        CodeLen = 1;
        BAsmCode[0] = 0xe3 | (PortNum << 4);
      }
    }
    else
      WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
  }
  else if (IsSerialPort(ArgStr[1].Str, 0x07, &PortNum))
  {
    if (AChkCPUFlags(eCPUFlagSerial, &ArgStr[1]))
    {
      DecodeAdr(&ArgStr[2]);
      switch (AdrMode)
      {
        case ModAcc:
          CodeLen = 1;
          BAsmCode[0] = 0x3c + PortNum;
          break;
        case ModImm:
          CodeLen = 2;
          BAsmCode[0] = 0x9c + PortNum;
          break;
        default:
          WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
      }
    }
  }
  else if (!as_strcasecmp(ArgStr[2].Str, "A"))
  {
    if (!as_strcasecmp(ArgStr[1].Str, "STS"))
    {
      if (AChkCPUFlags(eCPUFlagUPIPort, &ArgStr[1]))
      {
        CodeLen = 1;
        BAsmCode[0] = 0x90;
      }
    }
    else if (!as_strcasecmp(ArgStr[1].Str, "T"))
    {
      CodeLen = 1;
      BAsmCode[0] = 0x62;
    }
    else if (!as_strcasecmp(ArgStr[1].Str, "PSW"))
    {
      if (AChkCPUFlags(eCPUFlagTransferA_PSW, &ArgStr[1]))
      {
        CodeLen = 1;
        BAsmCode[0] = 0xd7;
      }
    }
    else
    {
      DecodeAdr(&ArgStr[1]);
      switch (AdrMode)
      {
        case ModReg:
          CodeLen = 1;
          BAsmCode[0] = 0xa8 + AdrVal;
          break;
        case ModInd:
          CodeLen = 1;
          BAsmCode[0] = 0xa0 + AdrVal;
          break;
        default:
          WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
      }
    }
  }
  else if (*ArgStr[2].Str == '#')
  {
    Boolean OK;
    Word AdrWord = EvalStrIntExpressionOffs(&ArgStr[2], 1, Int8, &OK);
    if (OK)
    {
      DecodeAdr(&ArgStr[1]);
      switch (AdrMode)
      {
        case ModReg:
          CodeLen = 2;
          BAsmCode[0] = 0xb8 + AdrVal;
          BAsmCode[1] = AdrWord;
          break;
        case ModInd:
          CodeLen = 2;
          BAsmCode[0] = 0xb0 + AdrVal;
          BAsmCode[1] = AdrWord;
          break;
        default:
          WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
      }
    }
  }
  else
    WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
}

static void DecodeANLD_ORLD_MOVD(Word Code)
{
  Byte PortNum;

  if (ChkArgCnt(2, 2) && ChkCPUFlags(eCPUFlagIOExpander))
  {
    const tStrComp *pArg1 = &ArgStr[1],
                   *pArg2 = &ArgStr[2];

    if ((Code == 0x3c) && (!as_strcasecmp(ArgStr[1].Str, "A"))) /* MOVD */
    {
      pArg1 = &ArgStr[2];
      pArg2 = &ArgStr[1];
      Code = 0x0c;
    }
    if (as_strcasecmp(pArg2->Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, pArg2);
    else if (!IsPort(pArg1->Str, 0xf0, &PortNum)) WrStrErrorPos(ErrNum_InvAddrMode, pArg1);
    else
    {
      PortNum -= 4;

      if ((PortNum == 3) && (pCurrCPUProps->Flags & eCPUFlagSiemens)) WrStrErrorPos(ErrNum_InvAddrMode, pArg2);
      else
      {
        CodeLen = 1;
        BAsmCode[0] = Code + PortNum;
      }
    }
  }
}

static void DecodeMOVP_MOVP3(Word Code)
{
  if (!ChkArgCnt(2, 2));
  else if ((Code == 0xe3) && !ChkCPUFlags(eCPUFlagMOVP3));
  else if (as_strcasecmp(ArgStr[1].Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else if (as_strcasecmp(ArgStr[2].Str, "@A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
  else
  {
    CodeLen = 1;
    BAsmCode[0] = Code;
  }
}

static void DecodeMOVP1(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(2, 2));
  else if (!ChkCPUFlags(eCPUFlagOKI));
  else if (as_strcasecmp(ArgStr[1].Str, "P")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else if (!IsIReg3(ArgStr[2].Str)) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
  else
  {
    CodeLen = 1;
    BAsmCode[0] = 0xc3;
  }
}

static void DecodeMOVX(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(2, 2)
   && ChkCPUFlags(eCPUFlagXMem))
  {
    const tStrComp *pArg1 = &ArgStr[1], *pArg2 = &ArgStr[2];
    Byte Code = 0x80;

    if (!as_strcasecmp(pArg2->Str, "A"))
    {
      pArg2 = &ArgStr[1];
      pArg1 = &ArgStr[2];
      Code += 0x10;
    }
    if (as_strcasecmp(pArg1->Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, pArg1);
    else
    {
      DecodeAdr(pArg2);
      if (AdrMode != ModInd) WrStrErrorPos(ErrNum_InvAddrMode, pArg2);
      else
      {
        CodeLen = 1;
        BAsmCode[0] = Code + AdrVal;
      }
    }
  }
}

static void DecodeNOP(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(0, 0));
  else
  {
    CodeLen = 1;
    BAsmCode[0] = 0x00;
  }
}

static void DecodeOUT(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(2, 2));
  else if (as_strcasecmp(ArgStr[1].Str, "DBB")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else if (as_strcasecmp(ArgStr[2].Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
  else if (AChkCPUFlags(eCPUFlagUPIPort, &ArgStr[1]))
  {
    BAsmCode[0] = 0x02;
    CodeLen = 1;
  }
}

static void DecodeOUTL(Word Code)
{
  UNUSED(Code);

  NLS_UpString(ArgStr[1].Str);
  if (!ChkArgCnt(2, 2));
  else
  {
    Word PortMask = 0x07;
    Byte PortNum;

    if (pCurrCPUProps->Flags & eCPUFlagBUS)
      PortMask |= 0x100;
    if (as_strcasecmp(ArgStr[2].Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[2]);
    else if (!IsPort(ArgStr[1].Str, PortMask, &PortNum)) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
    else if (PortNum == 8)
    {
      CodeLen = 1;
      BAsmCode[0] = 0x02;
    }
    else
    {
      CodeLen = 1;
      BAsmCode[0] = PortNum ? (0x38 + PortNum) : 0x90;
      ChkPx(PortNum, &ArgStr[1]);
    }
  }
}

static void DecodeRET_RETR(Word Code)
{
  if (ChkArgCnt(0, 0))
  {
    /* RETR not present if no interrupts at all (8021), or replaced by RETI (8022) */
    if ((Code == 0x93) && (!(pCurrCPUProps->Flags & eCPUFlagINTLogic) || (pCurrCPUProps->Flags & eCPUFlagADConv))) WrStrErrorPos(ErrNum_InstructionNotSupported, &OpPart);
    else
    {
      CodeLen = 1;
      BAsmCode[0] = Code;
    }
  }
}

static void DecodeSEL(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 1))
  {
    Boolean OK = False;
    int z;

    NLS_UpString(ArgStr[1].Str);
    for (z = 0; z < SelOrderCnt; z++)
      if (!strcmp(ArgStr[1].Str, SelOrders[z].Name))
      {
        /* SEL MBx not allowed if program memory cannot be larger than 2K.
           Similar is true for the Philips-specific MB2/MB3 arguments if
           less than 6K/8K ROM is present: */

        if (!strncmp(SelOrders[z].Name, "MB", 2) && (pCurrCPUProps->CodeSize <= 0x7ff));
        else if (!strcmp(SelOrders[z].Name, "MB2") && (pCurrCPUProps->CodeSize <= 0xfff));
        else if (!strcmp(SelOrders[z].Name, "MB32") && (pCurrCPUProps->CodeSize <= 0x17ff));

        else if (!strncmp(SelOrders[z].Name, "RB", 2) && !(pCurrCPUProps->Flags & eCPUFlagRegBanks));

        else if (!strncmp(SelOrders[z].Name, "AN", 2) && !(pCurrCPUProps->Flags & eCPUFlagADConv));

        else
        {
          CodeLen = 1;
          BAsmCode[0] = SelOrders[z].Code;
          OK = True;
        }
      }
    if (!OK)
      WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  }
}

static void DecodeSTOP(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(1, 1));
  else if (as_strcasecmp(ArgStr[1].Str, "TCNT")) WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  else
  {
    CodeLen = 1;
    BAsmCode[0] = 0x65;
  }
}

static void DecodeSTRT(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(1, 1));
  else
  {
    NLS_UpString(ArgStr[1].Str);
    if (!strcmp(ArgStr[1].Str, "CNT"))
    {
      CodeLen = 1;
      BAsmCode[0] = 0x45;
    }
    else if (!strcmp(ArgStr[1].Str, "T"))
    {
      CodeLen = 1;
      BAsmCode[0] = 0x55;
    }
    else
      WrStrErrorPos(ErrNum_InvAddrMode, &ArgStr[1]);
  }
}

static void DecodeXCH(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(2, 2));
  else
  {
    const tStrComp *pArg1 = &ArgStr[1], *pArg2 = &ArgStr[2];

    if (!as_strcasecmp(pArg2->Str, "A"))
    {
      pArg2 = &ArgStr[1];
      pArg1 = &ArgStr[2];
    }
    if (as_strcasecmp(pArg1->Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, pArg1);
    else
    {
      DecodeAdr(pArg2);
      switch (AdrMode)
      {
        case ModReg:
          CodeLen = 1;
          BAsmCode[0] = 0x28 + AdrVal;
          break;
        case ModInd:
          CodeLen = 1;
          BAsmCode[0] = 0x20 + AdrVal;
          break;
        default:
          WrStrErrorPos(ErrNum_InvAddrMode, pArg2);
      }
    }
  }
}

static void DecodeXCHD(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(2, 2));
  else
  {
    const tStrComp *pArg1 = &ArgStr[1], *pArg2 = &ArgStr[2];

    if (!as_strcasecmp(pArg2->Str, "A"))
    {
      pArg2 = &ArgStr[1];
      pArg1 = &ArgStr[2];
    }
    if (as_strcasecmp(pArg1->Str, "A")) WrStrErrorPos(ErrNum_InvAddrMode, pArg1);
    else
    {
      DecodeAdr(pArg2);
      if (AdrMode != ModInd) WrStrErrorPos(ErrNum_InvAddrMode, pArg2);
      else
      {
        CodeLen = 1;
        BAsmCode[0] = 0x30 + AdrVal;
      }
    }
  }
}

static void DecodeRAD(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(0, 0) && ChkCPUFlags(eCPUFlagADConv))
  {
    CodeLen = 1;
    BAsmCode[0] = 0x80;
  }
}

static void DecodeRETI(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(0, 0) && ChkCPUFlags(eCPUFlagADConv)) /* check for 8022 */
  {
    CodeLen = 1;
    BAsmCode[0] = 0x93;
  }
}

static void DecodeIDL_HALT(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(0, 0)
   && ChkCPUFlags(eCPUFlagCMOS))
  {
    CodeLen = 1;
    BAsmCode[0] = (pCurrCPUProps->Flags & eCPUFlagSiemens) ? 0xf3 : 0x01;
  }
}

static void DecodeOKIFixed(Word Code)
{
  if (ChkArgCnt(0, 0) && ChkCPUFlags(eCPUFlagOKI))
  {
    CodeLen = 1;
    BAsmCode[0] = Code;
  }
}

static void DecodeREG(Word Code)
{
  UNUSED(Code);

  if (!ChkArgCnt(1, 1));
  else
    AddRegDef(&LabPart, &ArgStr[1]);
}

/****************************************************************************/

static void AddAcc(char *Name, Byte Code)
{
  AddInstTable(InstTable, Name, Code, DecodeAcc);
}

static void AddCond(char *Name, Byte Code)
{
  AddInstTable(InstTable, Name, Code, DecodeCond);
}

static void AddSel(char *Name, Byte Code)
{
  if (InstrZ == SelOrderCnt) exit(255);
  SelOrders[InstrZ].Name = Name;
  SelOrders[InstrZ].Code = Code;
  InstrZ++;
}

static void InitFields(void)
{
  InstTable = CreateInstTable(203);
  AddInstTable(InstTable, "ADD", 0x00, DecodeADD_ADDC);
  AddInstTable(InstTable, "ADDC", 0x10, DecodeADD_ADDC);
  AddInstTable(InstTable, "ORL", 0x00, DecodeANL_ORL_XRL);
  AddInstTable(InstTable, "ANL", 0x10, DecodeANL_ORL_XRL);
  AddInstTable(InstTable, "XRL", 0x90, DecodeANL_ORL_XRL);
  AddInstTable(InstTable, "CALL", 0x14, DecodeCALL_JMP);
  AddInstTable(InstTable, "JMP", 0x04, DecodeCALL_JMP);
  AddInstTable(InstTable, "CLR", 0x00, DecodeCLR_CPL);
  AddInstTable(InstTable, "CPL", 0x10, DecodeCLR_CPL);
  AddInstTable(InstTable, "DEC", 0, DecodeDEC);
  AddInstTable(InstTable, "DIS", 0x10, DecodeDIS_EN);
  AddInstTable(InstTable, "EN", 0x00, DecodeDIS_EN);
  AddInstTable(InstTable, "DJNZ", 0x00, DecodeDJNZ);
  AddInstTable(InstTable, "ENT0", 0x00, DecodeENT0);
  AddInstTable(InstTable, "INC", 0x00, DecodeINC);
  AddInstTable(InstTable, "IN", 0x00, DecodeIN);
  AddInstTable(InstTable, "INS", 0x00, DecodeINS);
  AddInstTable(InstTable, "JMPP", 0x00, DecodeJMPP);
  AddInstTable(InstTable, "JB", 0x00, DecodeJB);
  AddInstTable(InstTable, "MOV", 0x00, DecodeMOV);
  AddInstTable(InstTable, "ANLD", 0x9c, DecodeANLD_ORLD_MOVD);
  AddInstTable(InstTable, "ORLD", 0x8c, DecodeANLD_ORLD_MOVD);
  AddInstTable(InstTable, "MOVD", 0x3c, DecodeANLD_ORLD_MOVD);
  AddInstTable(InstTable, "MOVP", 0xa3, DecodeMOVP_MOVP3);
  AddInstTable(InstTable, "MOVP3", 0xe3, DecodeMOVP_MOVP3);
  AddInstTable(InstTable, "MOVP1", 0x00, DecodeMOVP1);
  AddInstTable(InstTable, "MOVX", 0x00, DecodeMOVX);
  AddInstTable(InstTable, "NOP", 0x00, DecodeNOP);
  AddInstTable(InstTable, "OUT", 0x00, DecodeOUT);
  AddInstTable(InstTable, "OUTL", 0x00, DecodeOUTL);
  AddInstTable(InstTable, "RET", 0x83, DecodeRET_RETR);
  AddInstTable(InstTable, "RETR", 0x93, DecodeRET_RETR);
  AddInstTable(InstTable, "SEL", 0x00, DecodeSEL);
  AddInstTable(InstTable, "STOP", 0x00, DecodeSTOP);
  AddInstTable(InstTable, "STRT", 0x00, DecodeSTRT);
  AddInstTable(InstTable, "XCH", 0x00, DecodeXCH);
  AddInstTable(InstTable, "XCHD", 0x00, DecodeXCHD);
  AddInstTable(InstTable, "RAD", 0x00, DecodeRAD);
  AddInstTable(InstTable, "RETI", 0x00, DecodeRETI);
  AddInstTable(InstTable, "IDL", 0x00, DecodeIDL_HALT);
  AddInstTable(InstTable, "HALT", 0x00, DecodeIDL_HALT);
  AddInstTable(InstTable, "HLTS", 0x82, DecodeOKIFixed);
  AddInstTable(InstTable, "FLT", 0xa2, DecodeOKIFixed);
  AddInstTable(InstTable, "FLTT", 0xc2, DecodeOKIFixed);
  AddInstTable(InstTable, "FRES", 0xe2, DecodeOKIFixed);

  ClrCplVals = (char **) malloc(sizeof(char *)*ClrCplCnt);
  ClrCplCodes = (Byte *) malloc(sizeof(Byte)*ClrCplCnt);
  ClrCplVals[0] = "A"; ClrCplVals[1] = "C"; ClrCplVals[2] = "F0"; ClrCplVals[3] = "F1";
  ClrCplCodes[0] = 0x27; ClrCplCodes[1] = 0x97; ClrCplCodes[2] = 0x85; ClrCplCodes[3] = 0xa5;

  AddCond("JTF"  , 0x16);
  AddCond("JC"   , 0xf6);
  AddCond("JNC"  , 0xe6);
  AddCond("JZ"   , 0xc6);
  AddCond("JNZ"  , 0x96);
  if (pCurrCPUProps->Flags & eCPUFlagT0)
  {
    AddCond("JT0"  , 0x36);
    AddCond("JNT0" , 0x26);
  }
  AddCond("JT1"  , 0x56);
  AddCond("JNT1" , 0x46);
  if (pCurrCPUProps->Flags & eCPUFlagUserFlags)
  {
    AddCond("JF0"  , 0xb6);
    AddCond("JF1"  , 0x76);
  }
  if (pCurrCPUProps->Flags & eCPUFlagUPIPort)
  {
    AddCond("JNIBF", 0xd6);
    AddCond("JOBF" , 0x86);
  }
  else
    AddCond("JNI"  , (pCurrCPUProps->Flags & eCPUFlagSiemens) ? 0x66 : 0x86);
  if (pCurrCPUProps->Flags & eCPUFlagCondBitJmp)
  {
    AddCond("JB0"  , 0x12);
    AddCond("JB1"  , 0x32);
    AddCond("JB2"  , 0x52);
    AddCond("JB3"  , 0x72);
    AddCond("JB4"  , 0x92);
    AddCond("JB5"  , 0xb2);
    AddCond("JB6"  , 0xd2);
    AddCond("JB7"  , 0xf2);
  }
  if (pCurrCPUProps->Flags & eCPUFlag84xx)
    AddCond("JNTF", 0x06);

  AddAcc("DA"  , 0x57);
  AddAcc("RL"  , 0xe7);
  AddAcc("RLC" , 0xf7);
  AddAcc("RR"  , 0x77);
  AddAcc("RRC" , 0x67);
  AddAcc("SWAP", 0x47);

  SelOrders = (SelOrder *) malloc(sizeof(SelOrder) * SelOrderCnt); InstrZ = 0;
  AddSel("MB0" , 0xe5);
  AddSel("MB1" , 0xf5);
  AddSel("MB2" , 0xa5);
  AddSel("MB3" , 0xb5);
  AddSel("RB0" , 0xc5);
  AddSel("RB1" , 0xd5);
  AddSel("AN0" , 0x95);
  AddSel("AN1" , 0x85);

  AddInstTable(InstTable, "REG", 0, DecodeREG);
}

static void DeinitFields(void)
{
  DestroyInstTable(InstTable);
  free(ClrCplVals);
  free(ClrCplCodes);
  free(SelOrders);
}

static void MakeCode_48(void)
{
  CodeLen = 0;
  DontPrint = False;

  /* zu ignorierendes */

  if (Memo(""))
    return;

  /* Pseudoanweisungen */

  if (DecodeIntelPseudo(False))
    return;

  if (!LookupInstTable(InstTable, OpPart.Str))
    WrStrErrorPos(ErrNum_UnknownInstruction, &OpPart);
}

static Boolean IsDef_48(void)
{
  return Memo("REG");
}

static void SwitchFrom_48(void)
{
  DeinitFields();
}

static void SwitchTo_48(void *pUser)
{
  pCurrCPUProps = (const tCPUProps*)pUser;

  TurnWords = False;
  ConstMode = ConstModeIntel;

  PCSymbol = "$";
  HeaderID = 0x21;
  NOPCode = 0x00;
  DivideChars = ",";
  HasAttrs = False;

  /* limit code segement size only vor variants known to have no
     external program memory */

  ValidSegs = (1 << SegCode) | (1 << SegIData);
  Grans[SegCode ] = 1; ListGrans[SegCode ] = 1; SegInits[SegCode ] = 0;
  SegLimits[SegCode] = pCurrCPUProps->CodeSize;
  Grans[SegIData] = 1; ListGrans[SegIData] = 1; SegInits[SegIData] = 0x20;
  SegLimits[SegIData] = 0xff;
  if (pCurrCPUProps->Flags & eCPUFlagXMem)
  {
    ValidSegs |= (1 << SegXData);
    Grans[SegXData] = 1; ListGrans[SegXData] = 1; SegInits[SegXData] = 0;
    SegLimits[SegXData] = 0xff;
  }

  MakeCode = MakeCode_48;
  IsDef = IsDef_48;
  SwitchFrom = SwitchFrom_48;
  InitFields();
}

/* Limit code segment size only for variants known to have no
   external program memory: */

static tCPUProps CPUProps[] =
{
  { "8021"     , 0x3ff, eCPUFlagPort0 | eCPUFlagPort1 | eCPUFlagPort2 },
  { "8022"     , 0x7ff, eCPUFlagPort0 | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagT0 | eCPUFlagBUS | eCPUFlagADConv | eCPUFlagINTLogic },
  { "8401"     , 0x1fff,eCPUFlagDEC_DJNZ_IREG | eCPUFlagPort0 | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagT0 | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagINTLogic | eCPUFlagSerial | eCPUFlag84xx },
  { "8421"     , 0x7ff, eCPUFlagDEC_DJNZ_IREG | eCPUFlagPort0 | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagT0 | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagINTLogic | eCPUFlagSerial | eCPUFlag84xx },
  { "8441"     , 0xfff, eCPUFlagDEC_DJNZ_IREG | eCPUFlagPort0 | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagT0 | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagINTLogic | eCPUFlagSerial | eCPUFlag84xx },
  { "8461"     , 0x17ff,eCPUFlagDEC_DJNZ_IREG | eCPUFlagPort0 | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagT0 | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagINTLogic | eCPUFlagSerial | eCPUFlag84xx },
  { "8039"     , 0xfff, eCPUFlagXMem | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagIOExpander | eCPUFlagUserFlags | eCPUFlagT0 | eCPUFlagT0CLK | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic },
  { "8048"     , 0xfff, eCPUFlagXMem | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagIOExpander | eCPUFlagUserFlags | eCPUFlagT0 | eCPUFlagT0CLK | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagBUS | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic },
  { "80C39"    , 0xfff, eCPUFlagCMOS | eCPUFlagXMem | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagIOExpander | eCPUFlagUserFlags | eCPUFlagT0 | eCPUFlagT0CLK | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic },
  { "80C48"    , 0xfff, eCPUFlagCMOS | eCPUFlagXMem | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagIOExpander | eCPUFlagUserFlags | eCPUFlagT0 | eCPUFlagT0CLK | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagBUS | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic },
  { "8041"     , 0x3ff, eCPUFlagUPIPort | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagIOExpander | eCPUFlagUserFlags | eCPUFlagT0 | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagBUS | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic },
  { "8042"     , 0x7ff, eCPUFlagUPIPort | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagIOExpander | eCPUFlagUserFlags | eCPUFlagT0 | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagBUS | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic },
  { "80C382"   , 0xfff, eCPUFlagCMOS | eCPUFlagSiemens | eCPUFlagDEC_DJNZ_IREG | eCPUFlagXMem | eCPUFlagPort1 | eCPUFlagIOExpander | eCPUFlagT0 | eCPUFlagCondBitJmp | eCPUFlagBUS | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic },
  { "MSM80C39" , 0xfff, eCPUFlagCMOS | eCPUFlagDEC_DJNZ_IREG | eCPUFlagXMem | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagIOExpander | eCPUFlagUserFlags | eCPUFlagT0 | eCPUFlagT0CLK | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic | eCPUFlagOKI },
  { "MSM80C48" , 0xfff, eCPUFlagCMOS | eCPUFlagDEC_DJNZ_IREG | eCPUFlagXMem | eCPUFlagPort1 | eCPUFlagPort2 | eCPUFlagIOExpander | eCPUFlagUserFlags | eCPUFlagT0 | eCPUFlagT0CLK | eCPUFlagCondBitJmp | eCPUFlagTransferA_PSW | eCPUFlagBUS | eCPUFlagRegBanks | eCPUFlagLogToPort | eCPUFlagDEC_REG | eCPUFlagMOVP3 | eCPUFlagINTLogic | eCPUFlagOKI },
  { NULL, 0, 0 }
};

void code48_init(void)
{
  tCPUProps *pProp;

  for (pProp = CPUProps; pProp->pName; pProp++)
    (void)AddCPUUser(pProp->pName, SwitchTo_48, (void*)pProp, NULL);
}
