/* operator.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* defintion of operators                                                    */
/*                                                                           */
/*****************************************************************************/

#include <math.h>

#include "stdinc.h"
#include "errmsg.h"
#include "asmdef.h"
#include "asmerr.h"
#include "asmpars.h"
#include "asmrelocs.h"
#include "operator.h"

static void DummyOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  UNUSED(pLVal);
  UNUSED(pRVal);
  UNUSED(pErg);
}

static void OneComplOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  UNUSED(pLVal);
  pErg->Typ = TempInt;
  pErg->Contents.Int = ~(pRVal->Contents.Int);
}

static void ShLeftOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  pErg->Contents.Int = pLVal->Contents.Int << pRVal->Contents.Int;
}

static void ShRightOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  pErg->Contents.Int = pLVal->Contents.Int >> pRVal->Contents.Int;
}

static void BitMirrorOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  int z;

  if ((pRVal->Contents.Int < 1) || (pRVal->Contents.Int > 32)) WrError(ErrNum_OverRange);
  else
  {
    pErg->Typ = TempInt;
    pErg->Contents.Int = (pLVal->Contents.Int >> pRVal->Contents.Int) << pRVal->Contents.Int;
    pRVal->Contents.Int--;
    for (z = 0; z <= pRVal->Contents.Int; z++)
    {
      if ((pLVal->Contents.Int & (1 << (pRVal->Contents.Int - z))) != 0)
        pErg->Contents.Int |= (1 << z);
    }
  }
}

static void BinAndOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  pErg->Contents.Int = pLVal->Contents.Int & pRVal->Contents.Int;
}

static void BinOrOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  pErg->Contents.Int = pLVal->Contents.Int | pRVal->Contents.Int;
}

static void BinXorOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  pErg->Contents.Int = pLVal->Contents.Int ^ pRVal->Contents.Int;
}

static void PotOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  LargeInt HVal;

  switch (pErg->Typ = pLVal->Typ)
  {
    case TempInt:
      if (pRVal->Contents.Int < 0) pErg->Contents.Int = 0;
      else
      {
        pErg->Contents.Int = 1;
        while (pRVal->Contents.Int > 0)
        {
          if (pRVal->Contents.Int & 1)
            pErg->Contents.Int *= pLVal->Contents.Int;
          pRVal->Contents.Int >>= 1;
          if (pRVal->Contents.Int != 0)
            pLVal->Contents.Int *= pLVal->Contents.Int;
        }
      }
      break;
    case TempFloat:
      if (pRVal->Contents.Float == 0.0)
        pErg->Contents.Float = 1.0;
      else if (pLVal->Contents.Float == 0.0)
        pErg->Contents.Float = 0.0;
      else if (pLVal->Contents.Float > 0)
        pErg->Contents.Float = pow(pLVal->Contents.Float, pRVal->Contents.Float);
      else if ((fabs(pRVal->Contents.Float) <= ((double)MaxLongInt)) && (floor(pRVal->Contents.Float) == pRVal->Contents.Float))
      {
        HVal = (LongInt) floor(pRVal->Contents.Float + 0.5);
        if (HVal < 0)
        {
          pLVal->Contents.Float = 1 / pLVal->Contents.Float;
          HVal = -HVal;
        }
        pErg->Contents.Float = 1.0;
        while (HVal > 0)
        {
          if ((HVal & 1) == 1)
            pErg->Contents.Float *= pLVal->Contents.Float;
          pLVal->Contents.Float *= pLVal->Contents.Float;
          HVal >>= 1;
        }
      }
      else
      {
        WrError(ErrNum_InvArgPair);
        pErg->Typ = TempNone;
      }
      break;
    default:
      break;
  }
}

static void MultOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  switch (pErg->Typ = pLVal->Typ)
  {
    case TempInt:
      pErg->Contents.Int = pLVal->Contents.Int * pRVal->Contents.Int;
      break;
    case TempFloat:
      pErg->Contents.Float = pLVal->Contents.Float * pRVal->Contents.Float;
      break;
    default:
      break;
  }
}

static void DivOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  switch (pLVal->Typ)
  {
    case TempInt:
      if (pRVal->Contents.Int == 0) WrError(ErrNum_DivByZero);
      else
      {
        pErg->Typ = TempInt;
        pErg->Contents.Int = pLVal->Contents.Int / pRVal->Contents.Int;
      }
      break;
    case TempFloat:
      if (pRVal->Contents.Float == 0.0) WrError(ErrNum_DivByZero);
      else
      {
        pErg->Typ = TempFloat;
        pErg->Contents.Float = pLVal->Contents.Float / pRVal->Contents.Float;
      }
    default:
      break;
  }
}

static void ModOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  if (pRVal->Contents.Int == 0) WrError(ErrNum_DivByZero);
  else
  {
    pErg->Typ = TempInt;
    pErg->Contents.Int = pLVal->Contents.Int % pRVal->Contents.Int;
  }
}

static void AddOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempNone;
  switch (pLVal->Typ)
  {
    case TempInt:
      switch (pRVal->Typ)
      {
        case TempInt:
          pErg->Typ = TempInt;
          pErg->Contents.Int = pLVal->Contents.Int + pRVal->Contents.Int;
          pErg->Relocs = MergeRelocs(&(pLVal->Relocs), &(pRVal->Relocs), TRUE);
          break;
        case TempString:
        {
          LargeInt RIntVal = DynString2Int(&pRVal->Contents.Ascii);

          if ((RIntVal >= 0) && Int2DynString(&pErg->Contents.Ascii, RIntVal + pLVal->Contents.Int))
            pErg->Typ = TempString;
          break;
        }
        default:
          break;
      }
      break;
    case TempFloat:
      pErg->Typ = TempFloat;
      if (TempFloat == pRVal->Typ)
        pErg->Contents.Float = pLVal->Contents.Float + pRVal->Contents.Float;
      break;
    case TempString:
      switch (pRVal->Typ)
      {
        case TempString:
          DynString2DynString(&pErg->Contents.Ascii, &pLVal->Contents.Ascii);
          DynStringAppendDynString(&pErg->Contents.Ascii, &pRVal->Contents.Ascii);
          pErg->Typ = TempString;
          break;
        case TempInt:
        {
          LargeInt LIntVal = DynString2Int(&pLVal->Contents.Ascii);

          if ((LIntVal >= 0) && Int2DynString(&pErg->Contents.Ascii, LIntVal + pRVal->Contents.Int))
            pErg->Typ = TempString;
          break;
        }
        default:
          break;
      }
      break;
    default:
      break;
  }
}

static void SubOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  switch (pErg->Typ = pLVal->Typ)
  {
    case TempInt:
      pErg->Contents.Int = pLVal->Contents.Int - pRVal->Contents.Int;
      pErg->Relocs = MergeRelocs(&(pLVal->Relocs), &(pRVal->Relocs), FALSE);
      break;
    case TempFloat:
      pErg->Contents.Float = pLVal->Contents.Float - pRVal->Contents.Float;
      break;
    default:
      break;
  }
}

static void LogNotOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  UNUSED(pLVal);
  pErg->Typ = TempInt;
  pErg->Contents.Int = (pRVal->Contents.Int == 0) ? 1 : 0;
}

static void LogAndOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  pErg->Contents.Int = ((pLVal->Contents.Int != 0) && (pRVal->Contents.Int != 0)) ? 1 : 0;
}

static void LogOrOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  pErg->Contents.Int = ((pLVal->Contents.Int != 0) || (pRVal->Contents.Int != 0)) ? 1 : 0;
}

static void LogXorOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  pErg->Contents.Int = ((pLVal->Contents.Int != 0) != (pRVal->Contents.Int != 0)) ? 1 : 0;
}

static void EqOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  switch (pLVal->Typ)
  {
    case TempInt:
      pErg->Contents.Int = (pLVal->Contents.Int == pRVal->Contents.Int) ? 1 : 0;
      break;
    case TempFloat:
      pErg->Contents.Int = (pLVal->Contents.Float == pRVal->Contents.Float) ? 1 : 0;
      break;
    case TempString:
      pErg->Contents.Int = (DynStringCmp(&pLVal->Contents.Ascii, &pRVal->Contents.Ascii) == 0) ? 1 : 0;
      break;
    default:
      break;
  }
}

static void GtOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  switch (pLVal->Typ)
  {
    case TempInt:
      pErg->Contents.Int = (pLVal->Contents.Int > pRVal->Contents.Int) ? 1 : 0;
      break;
    case TempFloat:
      pErg->Contents.Int = (pLVal->Contents.Float > pRVal->Contents.Float) ? 1 : 0;
      break;
    case TempString:
      pErg->Contents.Int = (DynStringCmp(&pLVal->Contents.Ascii, &pRVal->Contents.Ascii) > 0) ? 1 : 0;
      break;
    default:
      break;
  }
}

static void LtOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  switch (pLVal->Typ)
  {
    case TempInt:
      pErg->Contents.Int = (pLVal->Contents.Int < pRVal->Contents.Int) ? 1 : 0;
      break;
    case TempFloat:
      pErg->Contents.Int = (pLVal->Contents.Float < pRVal->Contents.Float) ? 1 : 0;
      break;
    case TempString:
      pErg->Contents.Int = (DynStringCmp(&pLVal->Contents.Ascii, &pRVal->Contents.Ascii) < 0) ? 1 : 0;
      break;
    default:
      break;
  }
}

static void LeOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  switch (pLVal->Typ)
  {
    case TempInt:
      pErg->Contents.Int = (pLVal->Contents.Int <= pRVal->Contents.Int) ? 1 : 0;
      break;
    case TempFloat:
      pErg->Contents.Int = (pLVal->Contents.Float <= pRVal->Contents.Float) ? 1 : 0;
      break;
    case TempString:
      pErg->Contents.Int = (DynStringCmp(&pLVal->Contents.Ascii, &pRVal->Contents.Ascii) <= 0) ? 1 : 0;
      break;
    default:
      break;
  }
}

static void GeOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  switch (pLVal->Typ)
  {
    case TempInt:
      pErg->Contents.Int = (pLVal->Contents.Int >= pRVal->Contents.Int) ? 1 : 0;
      break;
    case TempFloat:
      pErg->Contents.Int = (pLVal->Contents.Float >= pRVal->Contents.Float) ? 1 : 0;
      break;
    case TempString:
      pErg->Contents.Int = (DynStringCmp(&pLVal->Contents.Ascii, &pRVal->Contents.Ascii) >= 0) ? 1 : 0;
      break;
    default:
      break;
  }
}

static void UneqOp(TempResult *pErg, TempResult *pLVal, TempResult *pRVal)
{
  pErg->Typ = TempInt;
  switch (pLVal->Typ)
  {
    case TempInt:
      pErg->Contents.Int = (pLVal->Contents.Int != pRVal->Contents.Int) ? 1 : 0;
      break;
    case TempFloat:
      pErg->Contents.Int = (pLVal->Contents.Float != pRVal->Contents.Float) ? 1 : 0;
      break;
    case TempString:
      pErg->Contents.Int = (DynStringCmp(&pLVal->Contents.Ascii, &pRVal->Contents.Ascii) != 0) ? 1 : 0;
      break;
    default:
      break;
  }
}

#define Int2Int       (TempInt    | (TempInt << 4)   )
#define Float2Float   (TempFloat  | (TempFloat << 4) )
#define String2String (TempString | (TempString << 4))
#define Int2String    (TempInt    | (TempString << 4))
#define String2Int    (TempString | (TempInt << 4)   )

const Operator Operators[] =
{
  {" " , 1 , False,  0, { 0, 0, 0, 0, 0 }, DummyOp},
  {"~" , 1 , False,  1, { TempInt << 4, 0, 0, 0, 0 }, OneComplOp},
  {"<<", 2 , True ,  3, { Int2Int, 0, 0, 0, 0 }, ShLeftOp},
  {">>", 2 , True ,  3, { Int2Int, 0, 0, 0, 0 }, ShRightOp},
  {"><", 2 , True ,  4, { Int2Int, 0, 0, 0, 0 }, BitMirrorOp},
  {"&" , 1 , True ,  5, { Int2Int, 0, 0, 0, 0 }, BinAndOp},
  {"|" , 1 , True ,  6, { Int2Int, 0, 0, 0, 0 }, BinOrOp},
  {"!" , 1 , True ,  7, { Int2Int, 0, 0, 0, 0 }, BinXorOp},
  {"^" , 1 , True ,  8, { Int2Int, Float2Float, 0, 0, 0 }, PotOp},
  {"*" , 1 , True , 11, { Int2Int, Float2Float, 0, 0, 0 }, MultOp},
  {"/" , 1 , True , 11, { Int2Int, Float2Float, 0, 0, 0 }, DivOp},
  {"#" , 1 , True , 11, { Int2Int, 0, 0, 0, 0 }, ModOp},
  {"+" , 1 , True , 13, { Int2Int, Float2Float, String2String, Int2String, String2Int }, AddOp},
  {"-" , 1 , True , 13, { Int2Int, Float2Float, 0, 0, 0 }, SubOp},
  {"~~", 2 , False,  2, { TempInt << 4, 0, 0, 0, 0 }, LogNotOp},
  {"&&", 2 , True , 15, { Int2Int, 0, 0, 0, 0 }, LogAndOp},
  {"||", 2 , True , 16, { Int2Int, 0, 0, 0, 0 }, LogOrOp},
  {"!!", 2 , True , 17, { Int2Int, 0, 0, 0, 0 }, LogXorOp},
  {"=" , 1 , True , 23, { Int2Int, Float2Float, String2String, 0, 0 }, EqOp},
  {"==", 2 , True , 23, { Int2Int, Float2Float, String2String, 0, 0 }, EqOp},
  {">" , 1 , True , 23, { Int2Int, Float2Float, String2String, 0, 0 }, GtOp},
  {"<" , 1 , True , 23, { Int2Int, Float2Float, String2String, 0, 0 }, LtOp},
  {"<=", 2 , True , 23, { Int2Int, Float2Float, String2String, 0, 0 }, LeOp},
  {">=", 2 , True , 23, { Int2Int, Float2Float, String2String, 0, 0 }, GeOp},
  {"<>", 2 , True , 23, { Int2Int, Float2Float, String2String, 0, 0 }, UneqOp},
  /* termination marker */
  {NULL, 0 , False,  0, { 0, 0, 0, 0, 0 }, NULL}
},
/* minus may have one or two operands */
MinusMonadicOperator =
{
  "-" ,1 , False, 13, { TempInt << 4, TempFloat << 4, 0, 0, 0 }, SubOp
};
