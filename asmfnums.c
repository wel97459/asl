/* asmfnums.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/* Verwaltung von Datei-Nummern                                              */
/*                                                                           */
/* Historie: 15. 5.1996 Grundsteinlegung                                     */
/*           25. 7.1998 GetFileName jetzt mit int statt Byte                 */
/*                      Verwaltung Adressbereiche                            */
/*                      Caching FileCount                                    */
/*           16. 8.1998 Ruecksetzen Adressbereiche                           */
/*                                                                           */
/*****************************************************************************/

#include "stdinc.h"
#include <string.h>

#include "strutil.h"
#include "chunks.h"
#include "asmdef.h"
#include "asmsub.h"
#include "asmpars.h"

#include "asmfnums.h"

typedef struct sToken
{
  struct sToken *Next;
  LargeWord FirstAddr, LastAddr;
  char *Name;
} TToken, *PToken;

static PToken FirstFile;
static int FileCount;

void InitFileList(void)
{
  FirstFile = NULL;
  FileCount = 0;
}

void ClearFileList(void)
{
  PToken F;

  while (FirstFile)
  {
    F = FirstFile->Next;
    free(FirstFile->Name);
    free(FirstFile);
    FirstFile = F;
  }
  FileCount = 0;
}

static PToken SearchToken(int Num)
{
  PToken Lauf = FirstFile;

  while (Num > 0)
  {
    if (!Lauf)
      return NULL;
    Num--;
    Lauf = Lauf->Next;
  }
  return Lauf;
}

void AddFile(char *FName)
{
  PToken Lauf, Neu;

  if (GetFileNum(FName) != -1)
    return;

  Neu = (PToken) malloc(sizeof(TToken));
  Neu->Next = NULL;
  Neu->Name = as_strdup(FName);
  Neu->FirstAddr = IntTypeDefs[LargeWordType].Max;
  Neu->LastAddr  = IntTypeDefs[LargeWordType].Min;
  if (!FirstFile)
    FirstFile = Neu;
  else
  {
    Lauf = FirstFile;
    while (Lauf->Next)
      Lauf = Lauf->Next;
    Lauf->Next = Neu;
  }
  FileCount++;
}

Integer GetFileNum(char *Name)
{
  PToken FLauf = FirstFile;
  int Cnt = 0;

  while ((FLauf) && (strcmp(FLauf->Name,Name)))
  {
    Cnt++;
    FLauf = FLauf->Next;
  }
  return FLauf ? Cnt : -1;
}

char *GetFileName(int Num)
{
  PToken Lauf = SearchToken(Num);
  static char *Dummy = "";

  return Lauf ? Lauf->Name : Dummy;
}

Integer GetFileCount(void)
{
  return FileCount;
}

void AddAddressRange(int File, LargeWord Start, LargeWord Len)
{
  PToken Lauf = SearchToken(File);

  if (!Lauf)
    return;

  if (Start < Lauf->FirstAddr)
    Lauf->FirstAddr = Start;
  if ((Len += Start - 1) > Lauf->LastAddr)
    Lauf->LastAddr = Len;
}

void GetAddressRange(int File, LargeWord *Start, LargeWord *End)
{
  PToken Lauf = SearchToken(File);

  if (!Lauf)
  {
    *Start = IntTypeDefs[LargeWordType].Max;
    *End   = IntTypeDefs[LargeWordType].Min;
  }
  else
  {
    *Start = Lauf->FirstAddr;
    *End   = Lauf->LastAddr;
  }
}

void ResetAddressRanges(void)
{
  PToken Run;

  for (Run = FirstFile; Run; Run = Run->Next)
  {
    Run->FirstAddr = IntTypeDefs[LargeWordType].Max;
    Run->LastAddr  = IntTypeDefs[LargeWordType].Min;
  }
}

void asmfnums_init(void)
{
   FirstFile = NULL;
   FileCount = 0;
}
