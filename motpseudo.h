#ifndef _MOTPSEUDO_H
#define _MOTPSEUDO_H
/* motpseudo.h */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* Haeufiger benutzte Motorola-Pseudo-Befehle                                */
/*                                                                           */
/*****************************************************************************/
  
/*****************************************************************************
 * Global Functions
 *****************************************************************************/

struct sStrComp;
    
extern Boolean DecodeMotoPseudo(Boolean Turn);

extern void ConvertMotoFloatDec(Double F, Byte *pDest, Boolean NeedsBig);

extern void AddMoto16PseudoONOFF(void);

extern Boolean DecodeMoto16Pseudo(ShortInt OpSize, Boolean Turn);

extern Boolean DecodeMoto16AttrSize(char SizeSpec, ShortInt *pResult, Boolean Allow24);

extern Boolean DecodeMoto16AttrSizeStr(const struct sStrComp *pSizeSpec, ShortInt *pResult, Boolean Allow24);

#endif /* _MOTPSEUDO_H */
