/* codekcp3.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* Codegenerator Xilinx kcpsm3                                               */
/*                                                                           */
/*****************************************************************************/

#include "stdinc.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "nls.h"
#include "strutil.h"
#include "bpemu.h"
#include "asmdef.h"
#include "asmsub.h"
#include "asmpars.h"
#include "asmitree.h"
#include "intpseudo.h"
#include "codevars.h"
#include "headids.h"
#include "errmsg.h"
#include "codepseudo.h"

#include "codekcp3.h"

#define RegOrderCnt 10
#define ALUOrderCnt 10

typedef struct
        {
          LongWord Code;
        } FixedOrder;

static FixedOrder *RegOrders, *ALUOrders;

static CPUVar CPUKCPSM3;


/*--------------------------------------------------------------------------
 * Address Expression Parsing
 *--------------------------------------------------------------------------*/

static Boolean IsWReg(const char *Asc, LongWord *pErg)
{
  Boolean OK;
  char *s;

  if (FindRegDef(Asc, &s)) Asc = s;

  if ((strlen(Asc) < 2) || (mytoupper(*Asc) != 'S')) 
    return False;

  *pErg = ConstLongInt(Asc + 1, &OK, 16);
  if (!OK)
    return False;

  return (*pErg < 16);
}

static Boolean IsIWReg(const char *Asc, LongWord *pErg)
{
  char Tmp[10];
  int l = strlen(Asc);

  if ((l < 3) || (Asc[0] != '(') || (Asc[l - 1] != ')'))
    return False;

  memcpy(Tmp, Asc + 1, l - 2); Tmp[l - 2] = '\0';
  return IsWReg(Tmp, pErg);
}

static Boolean IsCond(int OtherArgCnt, LongWord *pErg)
{
  static const char *Conds[4] = { "Z", "NZ", "C", "NC" };

  if (ArgCnt <= OtherArgCnt)
  {
    *pErg = 0;
    return True;
  }

  for (*pErg = 0; *pErg < (sizeof(Conds) / sizeof(*Conds)); (*pErg)++)
    if (!as_strcasecmp(Conds[*pErg], ArgStr[1].Str))
    {
      *pErg |= 4;
      return True;
    }

  return False;
}

/*--------------------------------------------------------------------------
 * Code Handlers
 *--------------------------------------------------------------------------*/

static void DecodePort(Word Index)
{
  UNUSED(Index);

  CodeEquate(SegIO, 0, SegLimits[SegIO]);
}

static void DecodeReg(Word Index)
{
  UNUSED(Index);

  if (ChkArgCnt(1, 1))
    AddRegDef(&LabPart, &ArgStr[1]);
}

static void DecodeNameReg(Word Index)
{
  UNUSED(Index);

  if (ChkArgCnt(2, 2))
    AddRegDef(&ArgStr[2], &ArgStr[1]);
}

static void DecodeConstant(Word Index)
{
  UNUSED(Index);

  if (ChkArgCnt(2, 2))
  {   
    TempResult t;
    Boolean OK;

    FirstPassUnknown = FALSE;
    t.Contents.Int = EvalStrIntExpression(&ArgStr[2], Int32, &OK);
    if ((OK) && (!FirstPassUnknown))
    {
      t.Typ = TempInt;
      SetListLineVal(&t);
      PushLocHandle(-1); 
      EnterIntSymbol(&ArgStr[1], t.Contents.Int, SegNone, False);
      PopLocHandle();
    }
  }
}

static void DecodeOneReg(Word Index)
{
  FixedOrder *pOrder = RegOrders + Index;
  LongWord Reg;

  if (!ChkArgCnt(1, 1));
  else if (!IsWReg(ArgStr[1].Str, &Reg)) WrError(ErrNum_InvAddrMode);
  else
  {
    DAsmCode[0] = pOrder->Code | (Reg << 8);
    CodeLen = 1;
  }
}

static void DecodeALU(Word Index)
{
  FixedOrder *pOrder = ALUOrders + Index;
  LongWord Src, DReg;
  Boolean OK;

  if (!ChkArgCnt(2, 2));
  else if (!IsWReg(ArgStr[1].Str, &DReg)) WrError(ErrNum_InvAddrMode);
  else if (IsWReg(ArgStr[2].Str, &Src))
  {
    DAsmCode[0] = pOrder->Code | 0x1000 | (DReg << 8) | (Src << 4);
    CodeLen = 1;
  }
  else
  {
    Src = EvalStrIntExpression(&ArgStr[2], Int8, &OK);
    if (OK)
    {
      DAsmCode[0] = pOrder->Code | (DReg << 8) | (Src & 0xff);
      CodeLen = 1;
    }
  }
}

static void DecodeJmp(Word Index)
{
  LongWord Addr, Cond;
  Boolean OK;

  if (ChkArgCnt(1, 2)
   && IsCond(1, &Cond))
  {
    Addr = EvalStrIntExpression(&ArgStr[ArgCnt], UInt10, &OK);
    if (OK)
    {
      ChkSpace(SegCode);
      DAsmCode[0] = 0x30000 | Index | (Cond << 10) | (Addr & 0x3ff);
      CodeLen = 1;
    }
  }
}

static void DecodeRet(Word Index)
{
  LongWord Cond;

  UNUSED(Index);

  if (ChkArgCnt(0, 1)
   && IsCond(0, &Cond))
  {
    DAsmCode[0] = 0x2a000 | (Cond << 10);
    CodeLen = 1;
  }  
}

static void DecodeReti(Word Index)
{
  UNUSED(Index);

  if (ChkArgCnt(1, 1))
  {
    if (!as_strcasecmp(ArgStr[1].Str, "DISABLE"))
    {
      DAsmCode[0] = 0x38000;
      CodeLen = 1;
    }
    else if (!as_strcasecmp(ArgStr[1].Str, "ENABLE"))
    {
      DAsmCode[0] = 0x38001;
      CodeLen = 1;
    }
    else
      WrError(ErrNum_InvAddrMode);
  }
}

static void DecodeInt(Word Index)
{
  if (ChkArgCnt(1, 1))
  {
    if (as_strcasecmp(ArgStr[1].Str, "INTERRUPT")) WrError(ErrNum_InvAddrMode);
    else
    {
      DAsmCode[0] = 0x3c000 | Index;
      CodeLen = 1;
    }
  }
}

static void DecodeMem(Word Index)
{
  LongWord Reg, Addr;
  Boolean OK;

  if (!ChkArgCnt(2, 2));
  else if (!IsWReg(ArgStr[1].Str, &Reg)) WrError(ErrNum_InvAddrMode);
  else 
  {
    DAsmCode[0] = (((LongWord)Index) << 13) | (Reg << 8);
    if (IsIWReg(ArgStr[2].Str, &Addr))
    {
      DAsmCode[0] |= 0x01000 | (Addr << 4);
      CodeLen = 1;
    }
    else
    {
      Addr = EvalStrIntExpression(&ArgStr[2], UInt6, &OK);
      if (OK)
      {
        ChkSpace(SegData);
        DAsmCode[0] |= Addr & 0x3f;
        CodeLen = 1;
      }
    }
  }
}

static void DecodeIO(Word Index)
{
  LongWord Reg, Addr;
  Boolean OK;

  if (!ChkArgCnt(2, 2));
  else if (!IsWReg(ArgStr[1].Str, &Reg)) WrError(ErrNum_InvAddrMode);
  else 
  {
    DAsmCode[0] = (((LongWord)Index) << 13) | (Reg << 8);
    if (IsIWReg(ArgStr[2].Str, &Addr))
    {
      DAsmCode[0] |= 0x01000 | (Addr << 4);
      CodeLen = 1;
    }
    else
    {
      Addr = EvalStrIntExpression(&ArgStr[2], UInt8, &OK);
      if (OK)
      {
        ChkSpace(SegIO);
        DAsmCode[0] |= Addr & 0xff;
        CodeLen = 1;
      }
    }
  }
}

static void DecodeNop(Word Index)
{
  UNUSED (Index);

  if (ChkArgCnt(0, 0))
  {
    DAsmCode[0] = NOPCode;
    CodeLen = 1;
  }
}

/*--------------------------------------------------------------------------
 * Instruction Table Handling
 *--------------------------------------------------------------------------*/

static void AddReg(char *NName, LongWord NCode)
{
   if (InstrZ >= RegOrderCnt)
    exit(255);

   RegOrders[InstrZ].Code = NCode;
   AddInstTable(InstTable, NName, InstrZ++, DecodeOneReg);
}

static void AddALU(char *NName, LongWord NCode)
{
   if (InstrZ >= ALUOrderCnt)
    exit(255);

   ALUOrders[InstrZ].Code = NCode;
   AddInstTable(InstTable, NName, InstrZ++, DecodeALU);
}

static void InitFields(void)
{
  InstTable = CreateInstTable(97);

  InstrZ = 0;
  RegOrders = (FixedOrder*) malloc(sizeof(FixedOrder) * RegOrderCnt);
  AddReg("RL" , 0x20002);
  AddReg("RR" , 0x2000c);
  AddReg("SL0", 0x20006);
  AddReg("SL1", 0x20007);
  AddReg("SLA", 0x20000);
  AddReg("SLX", 0x20004);
  AddReg("SR0", 0x2000e);
  AddReg("SR1", 0x2000f);
  AddReg("SRA", 0x20008);
  AddReg("SRX", 0x2000a);

  InstrZ = 0;
  ALUOrders = (FixedOrder*) malloc(sizeof(FixedOrder) * ALUOrderCnt);
  AddALU("ADD"    , 0x18000);
  AddALU("ADDCY"  , 0x1a000);
  AddALU("AND"    , 0x0a000);
  AddALU("COMPARE", 0x14000);
  AddALU("LOAD"   , 0x00000);
  AddALU("OR"     , 0x0c000);
  AddALU("SUB"    , 0x1c000);
  AddALU("SUBCY"  , 0x1e000);
  AddALU("TEST"   , 0x12000);
  AddALU("XOR"    , 0x0e000);

  AddInstTable(InstTable, "CALL", 0x0000, DecodeJmp);
  AddInstTable(InstTable, "JUMP", 0x4000, DecodeJmp);
  AddInstTable(InstTable, "RETURN", 0, DecodeRet);
  AddInstTable(InstTable, "RETURNI", 0, DecodeReti);
  AddInstTable(InstTable, "ENABLE", 1, DecodeInt);
  AddInstTable(InstTable, "DISABLE", 0, DecodeInt);
  AddInstTable(InstTable, "FETCH", 0x03, DecodeMem);
  AddInstTable(InstTable, "STORE", 0x17, DecodeMem);
  AddInstTable(InstTable, "INPUT", 0x02, DecodeIO);
  AddInstTable(InstTable, "OUTPUT", 0x16, DecodeIO);

  AddInstTable(InstTable, "PORT", 0, DecodePort);
  AddInstTable(InstTable, "REG", 0, DecodeReg);
  AddInstTable(InstTable, "NAMEREG", 0, DecodeNameReg);
  AddInstTable(InstTable, "CONSTANT", 0, DecodeConstant);

  AddInstTable(InstTable, "NOP", 0, DecodeNop);
}

static void DeinitFields(void)
{
  DestroyInstTable(InstTable);
  free(RegOrders); 
  free(ALUOrders);
}

/*--------------------------------------------------------------------------
 * Semipublic Functions
 *--------------------------------------------------------------------------*/

static Boolean IsDef_KCPSM3(void)
{
   return (Memo("REG")) || (Memo("PORT")); 
}

static void SwitchFrom_KCPSM3(void)
{
   DeinitFields();
}

static void MakeCode_KCPSM3(void)
{
  CodeLen = 0; DontPrint = False;

  /* zu ignorierendes */

   if (Memo("")) return;

   /* Pseudoanweisungen */

   if (DecodeIntelPseudo(True)) return;

   if (!LookupInstTable(InstTable, OpPart.Str))
     WrStrErrorPos(ErrNum_UnknownInstruction, &OpPart);
}

static void SwitchTo_KCPSM3(void)
{
   PFamilyDescr FoundDescr;

   FoundDescr = FindFamilyByName("KCPSM3");

   TurnWords = True; ConstMode = ConstModeIntel;

   PCSymbol = "$"; HeaderID = FoundDescr->Id;

   /* NOP = load s0,s0 */

   NOPCode = 0x01000;
   DivideChars = ","; HasAttrs = False;

   ValidSegs = (1 << SegCode) | (1 << SegData) | (1 << SegIO);
   Grans[SegCode] = 4; ListGrans[SegCode] = 4; SegInits[SegCode] = 0;
   SegLimits[SegCode] = 0x3ff;
   Grans[SegData] = 1; ListGrans[SegData] = 1; SegInits[SegData] = 0;
   SegLimits[SegData] = 0x3f;
   Grans[SegIO] = 1; ListGrans[SegIO] = 1; SegInits[SegIO] = 0;
   SegLimits[SegIO] = 0xff;

   MakeCode = MakeCode_KCPSM3; IsDef = IsDef_KCPSM3;
   SwitchFrom = SwitchFrom_KCPSM3; InitFields();
}

/*--------------------------------------------------------------------------
 * Initialization
 *--------------------------------------------------------------------------*/

void codekcpsm3_init(void)
{
   CPUKCPSM3 = AddCPU("KCPSM3", SwitchTo_KCPSM3);
}
