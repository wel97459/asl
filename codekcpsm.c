/* codekcpsm.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* Codegenerator Xilinx kcpsm                                                */
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

#include "codekcpsm.h"

#undef DEBUG_PRINTF

typedef struct
{
  char *Name;
  Word Code;
} Condition;


#define WorkOfs 0xe0


#define IOopCnt 2
#define CondCnt 5

#define ModNone  (-1)
#define ModWReg   0
#define MModWReg   (1 << ModWReg)
#define ModAbs    1
#define MModAbs    (1 << ModAbs)
#define ModImm    4
#define MModImm    (1 << ModImm)
#define ModIRReg  6
#define MModIRReg  (1 << ModIRReg)
#define ModInd    7
#define MModInd    (1 << ModInd)

static ShortInt AdrType;
static Word AdrMode,AdrIndex;

static Condition *Conditions;
static int TrueCond;

static CPUVar CPUKCPSM;

/*--------------------------------------------------------------------------*/ 
/* code helpers */

static Boolean IsWReg(const char *Asc, Word *Erg)
{
  Boolean Err;
  Boolean retValue;
  char *s;

  if (FindRegDef(Asc, &s))
    Asc = s;

  if ((strlen(Asc) < 2) || (mytoupper(*Asc) != 'S')) 
    retValue = False;
  else
  {
    *Erg = ConstLongInt(Asc + 1, &Err, 10);
    if (!Err) 
      retValue = False;
    else 
      retValue = (*Erg <= 15);
  }
#ifdef DEBUG_PRINTF
  fprintf( stderr, "IsWReg: %s %d\n", Asc, retValue );
#endif
  return retValue;
}

static void DecodeAdr(const tStrComp *pArg, Byte Mask, int Segment)
{
  Boolean OK;
  char *p;
  int ArgLen;

  AdrType = ModNone;

  /* immediate ? */

  if (*pArg->Str == '#')
  {
    AdrMode = EvalStrIntExpressionOffs(pArg, 1, UInt8, &OK);
    if (OK)
      AdrType = ModImm;
    goto chk;
  }

  /* Register ? */

  if (IsWReg(pArg->Str, &AdrMode))
  {
    AdrType = ModWReg;
    goto chk;
  }

  /* indiziert ? */

  ArgLen = strlen(pArg->Str);
  if ((ArgLen >= 4) && (pArg->Str[ArgLen - 1] == ')'))
  {
    p = pArg->Str + ArgLen - 1;
    while ((p >= pArg->Str) && (*p != '('))
      p--;
    if (*p != '(') WrError(ErrNum_BrackErr);
    else
    {
      tStrComp RegComp, DispComp;

      StrCompSplitRef(&DispComp, &RegComp, pArg, p);
      StrCompShorten(&RegComp, 1);
      if (!IsWReg(RegComp.Str, &AdrMode)) WrStrErrorPos(ErrNum_InvReg, &RegComp);
      else
      {
        AdrIndex = EvalStrIntExpression(&DispComp, UInt8, &OK);
        if (OK)
        {
          AdrType = ModInd;
          ChkSpace(SegData);
        }
        goto chk;
      }
    }
  }

  /* einfache direkte Adresse ? */

  AdrMode = EvalStrIntExpression(pArg, UInt8, &OK);
  if (OK)
  {
    AdrType = ModAbs;
    if (Segment != SegNone)
      ChkSpace(Segment);
    goto chk;
  }

chk:
  if ((AdrType != ModNone) && ((Mask & (1 << AdrType)) == 0))
  {
    WrError(ErrNum_InvAddrMode);
    AdrType = ModNone;
  }
}

static int DecodeCond(char *Asc)
{
  int Cond = 0;

  NLS_UpString(Asc);
  while ((Cond < CondCnt) && (strcmp(Conditions[Cond].Name, Asc)))
    Cond++;
  return Cond;
}

/*--------------------------------------------------------------------------*/
/* instruction decoders */

static void DecodeFixed(Word Code)
{
  if (ChkArgCnt(0, 0))
  {
    CodeLen = 1; 
    WAsmCode[0] = Code;
  }
}

static void DecodeLOAD(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(2, 2))
  {
    DecodeAdr(&ArgStr[1], MModWReg, SegNone);
    switch (AdrType)
    {
      case ModWReg:
      {
        Word Save = AdrMode;
        DecodeAdr(&ArgStr[2], MModWReg | MModAbs | MModImm, SegNone);
        switch (AdrType)
        {
          case ModWReg:
#ifdef DEBUG_PRINTF
            fprintf( stderr, "LOAD-->ModWReg %d %d\n", AdrMode, Save );
#endif
            WAsmCode[0] = 0xc000 | (Save << 8) | ( AdrMode << 4 );
            CodeLen = 1;
            break;
          case ModAbs:
#ifdef DEBUG_PRINTF
            fprintf( stderr, "LOAD-->ModAbs %d %d\n", AdrMode, Save );
#endif
            WAsmCode[0] = 0xc000 | (Save << 8) | ( AdrMode << 4 );
            CodeLen = 1;
            break;
          case ModImm:
#ifdef DEBUG_PRINTF
            fprintf( stderr, "LOAD-->ModImm %d %d\n", AdrMode, Save );
#endif
            WAsmCode[0] = (Save << 8) | AdrMode;
            CodeLen = 1;
            break;
        }
        break;
      }
    }
  }
}

static void DecodeALU2(Word Code)
{
  if (ChkArgCnt(2, 2))
  {
    DecodeAdr(&ArgStr[1], MModWReg, SegNone);
    switch (AdrType)
    {
      case ModWReg:
      {
        Word Save = AdrMode;
        DecodeAdr(&ArgStr[2], MModAbs | MModWReg | MModImm, SegNone);
        switch (AdrType)
        {
          case ModWReg:
            WAsmCode[0] = 0xc000 | (Save << 8) | ( AdrMode << 4 ) | Code;
            CodeLen = 1;
            break;
          case ModImm:
          case ModAbs:
            WAsmCode[0] = (Code << 12 ) | (Save << 8) | AdrMode;
            CodeLen = 1;
            break;
        }
        break;
      }
    }
  }
}

static void DecodeALU1(Word Code)
{
  if (ChkArgCnt(1, 1))
  {
    DecodeAdr(&ArgStr[1], MModWReg, SegNone);
    switch (AdrType)
    {
      case ModWReg:
        WAsmCode[0] = 0xd000 | (AdrMode << 8) | Code; 
        CodeLen = 1;
        break;
    }
  }
}

static void DecodeCALL(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 2))
  {
    int Cond = (ArgCnt == 1) ? TrueCond : DecodeCond(ArgStr[1].Str);

    if (Cond >= CondCnt) WrStrErrorPos(ErrNum_UndefCond, &ArgStr[1]);
    else
    {
      DecodeAdr(&ArgStr[ArgCnt], MModAbs | ModImm, SegCode);
      switch (AdrType)
      {
        case ModAbs:
        case ModImm:
          WAsmCode[0] = 0x8300 | (Conditions[Cond].Code << 10) | Lo(AdrMode);
          CodeLen = 1;
          break;
      }
    }
  }
}

static void DecodeJUMP(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 2))
  {
    int Cond = (ArgCnt == 1) ? TrueCond : DecodeCond(ArgStr[1].Str);

    if (Cond >= CondCnt) WrStrErrorPos(ErrNum_UndefCond, &ArgStr[1]);
    else
    {
      DecodeAdr(&ArgStr[ArgCnt], MModAbs | MModImm, SegCode);
      switch (AdrType)
      {
        case ModAbs:
        case ModImm:
          WAsmCode[0] = 0x8100 | (Conditions[Cond].Code << 10) | Lo(AdrMode);
          CodeLen = 1;
          break;
      }
    }
  }
}

static void DecodeRETURN(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(0, 1))
  {
    int Cond = (ArgCnt == 0) ? TrueCond : DecodeCond(ArgStr[1].Str);

    if (Cond >= CondCnt) WrStrErrorPos(ErrNum_UndefCond, &ArgStr[1]);
    else
    {
      WAsmCode[0] = 0x8080 | (Conditions[Cond].Code << 10);
      CodeLen = 1;
    }
  }
}

static void DecodeIOop(Word Code)
{
  if (ChkArgCnt(2, 2))
  {
    DecodeAdr(&ArgStr[1], MModWReg, SegNone);
    switch (AdrType)
    {
      case ModWReg:
      {
        Word Save = AdrMode;
        DecodeAdr(&ArgStr[2], MModInd | MModImm | MModAbs, SegData);
        switch (AdrType)
        {
          case ModInd:
            WAsmCode[0] = 0x1000 | ((Code | Save) << 8) | ( AdrMode << 4);
            CodeLen = 1;
            break;
          case ModImm:
          case ModAbs:
            WAsmCode[0] = ((Code | Save) << 8) | AdrMode;
            CodeLen = 1;
            break;
        }
        break;
      }
    }
  }
}

static void DecodeRETURNI(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 1))
  {
    NLS_UpString(ArgStr[1].Str);      
    if (!strcmp(ArgStr[1].Str, "ENABLE"))
    {
      WAsmCode[0] = 0x80f0;
      CodeLen = 1;
    }
    else if (!strcmp(ArgStr[1].Str, "DISABLE"))
    {
      WAsmCode[0] =  0x80d0;
      CodeLen = 1;
    }
  }
}

static void DecodeENABLE_DISABLE(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 1))
  {
    NLS_UpString(ArgStr[1].Str);      
    if (!as_strcasecmp(ArgStr[1].Str, "INTERRUPT"))
    {
      WAsmCode[0] = Code;
      CodeLen = 1;
    }
  }
}

static void DecodeREG(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(1, 1))
    AddRegDef(&LabPart, &ArgStr[1]);
}

static void DecodeNAMEREG(Word Code)
{
  UNUSED(Code);

  if (ChkArgCnt(2, 2))
    AddRegDef(&ArgStr[2], &ArgStr[1]);
}

static void DecodeCONSTANT(Word Code)
{
  UNUSED(Code);

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

/*--------------------------------------------------------------------------*/
/* code table handling */

static void AddFixed(char *NName, Word NCode)
{
  AddInstTable(InstTable, NName, NCode, DecodeFixed);
}

static void AddALU2(char *NName, Word NCode)
{
  AddInstTable(InstTable, NName, NCode, DecodeALU2);
}

static void AddALU1(char *NName, Word NCode)
{
  AddInstTable(InstTable, NName, NCode, DecodeALU1);
}

static void AddIOop(Char *NName, Word NCode)
{
  AddInstTable(InstTable, NName, NCode, DecodeIOop);
}

static void AddCondition(char *NName, Word NCode)
{
  if (InstrZ >= CondCnt) exit(255);
  Conditions[InstrZ].Name = NName;
  Conditions[InstrZ++].Code = NCode;
}
   
static void InitFields(void)
{
  InstTable = CreateInstTable(201);
  AddInstTable(InstTable, "LOAD", 0, DecodeLOAD);
  AddInstTable(InstTable, "CALL", 0, DecodeCALL);
  AddInstTable(InstTable, "JUMP", 0, DecodeJUMP);
  AddInstTable(InstTable, "RETURN", 0, DecodeRETURN);
  AddInstTable(InstTable, "RETURNI", 0, DecodeRETURNI);
  AddInstTable(InstTable, "ENABLE", 0x8030, DecodeENABLE_DISABLE);
  AddInstTable(InstTable, "DISABLE", 0x8010, DecodeENABLE_DISABLE);
  AddInstTable(InstTable, "REG", 0, DecodeREG);
  AddInstTable(InstTable, "NAMEREG", 0, DecodeNAMEREG);
  AddInstTable(InstTable, "CONSTANT", 0, DecodeCONSTANT);

  AddFixed("EI"     , 0x8030);  AddFixed("DI"     , 0x8010);
  AddFixed("RETIE"  , 0x80f0);  AddFixed("RETID"  , 0x80d0);
  AddFixed("NOP"    , 0xc000); /* fake */

  AddALU2("ADD"   , 0x04);
  AddALU2("ADDCY" , 0x05);
  AddALU2("SUB"   , 0x06);
  AddALU2("SUBCY" , 0x07);
  AddALU2("OR"    , 0x02);
  AddALU2("AND"   , 0x01);
  AddALU2("XOR"   , 0x03); 

  AddALU1("SR0" , 0x0e);
  AddALU1("SR1" , 0x0f);
  AddALU1("SRX" , 0x0a);
  AddALU1("SRA" , 0x08);
  AddALU1("RR"  , 0x0c);
  AddALU1("SL0" , 0x06);
  AddALU1("SL1" , 0x07);
  AddALU1("SLX" , 0x04);
  AddALU1("SLA" , 0x00);
  AddALU1("RL"  , 0x02);

  AddIOop("INPUT"  , 0xa0);
  AddIOop("OUTPUT" , 0xe0);

  Conditions = (Condition *) malloc(sizeof(Condition) * CondCnt); InstrZ = 0;
  TrueCond = InstrZ; AddCondition("T"  , 0);
  AddCondition("C"  , 6); AddCondition("NC" , 7);
  AddCondition("Z"  , 4); AddCondition("NZ" , 5);
}

static void DeinitFields(void)
{
  DestroyInstTable(InstTable);
  free(Conditions);
}

/*---------------------------------------------------------------------*/

static void MakeCode_KCPSM(void)
{
  CodeLen = 0; DontPrint = False;

  /* zu ignorierendes */

  if (Memo("")) return;

  /* Pseudoanweisungen */

  if (DecodeIntelPseudo(True)) return;

  if (!LookupInstTable(InstTable, OpPart.Str))
    WrStrErrorPos(ErrNum_UnknownInstruction, &OpPart);
}

static Boolean IsDef_KCPSM(void)
{
  return (Memo("REG")); 
}

static void SwitchFrom_KCPSM(void)
{
  DeinitFields();
}

static void SwitchTo_KCPSM(void)
{
  PFamilyDescr FoundDescr;

  FoundDescr = FindFamilyByName("KCPSM");

  TurnWords = True;
  ConstMode = ConstModeIntel;

  PCSymbol = "$";
  HeaderID = FoundDescr->Id;
  NOPCode = 0xc0; /* nop = load s0,s0 */
  DivideChars = ",";
  HasAttrs = False;

  ValidSegs = (1 << SegCode) | (1 << SegData);
  Grans[SegCode] = 2; ListGrans[SegCode] = 2; SegInits[SegCode] = 0; SegLimits[SegCode] = 0xff;
  Grans[SegData] = 1; ListGrans[SegData] = 1; SegInits[SegData] = 0; SegLimits[SegData] = 0xff;

  MakeCode = MakeCode_KCPSM;
  IsDef = IsDef_KCPSM;
  SwitchFrom = SwitchFrom_KCPSM;
  InitFields();
}

void codekcpsm_init(void)
{
  CPUKCPSM = AddCPU("KCPSM", SwitchTo_KCPSM);

  AddCopyright("XILINX KCPSM(Picoblaze)-Generator (C) 2003 Andreas Wassatsch");
}

