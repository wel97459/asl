#ifndef _NATPSEUDO_H
#define _NATPSEUDO_H
/* natpseudo.h */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Port                                                                   */
/*                                                                           */
/* Pseudo Instructions commonly used on National targets                     */
/*                                                                           */
/*****************************************************************************/
/* $Id: natpseudo.h,v 1.1 2006/04/09 12:40:11 alfred Exp $                   */
/***************************************************************************** 
 * $Log: natpseudo.h,v $
 * Revision 1.1  2006/04/09 12:40:11  alfred
 * - unify COP pseudo instructions
 *
 *****************************************************************************/

/*****************************************************************************
 * Global Functions
 *****************************************************************************/

extern Boolean DecodeNatPseudo(Boolean *pBigFlag);

#endif /* _NATPSEUDO_H */
