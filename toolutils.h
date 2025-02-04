#ifndef _TOOLUTILS_H
#define _TOOLUTILS_H
/* toolutils.c */
/*****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only                     */
/*                                                                           */
/* AS-Portierung                                                             */
/*                                                                           */
/*****************************************************************************/

#include "fileformat.h"

typedef struct
{
  LargeInt Addr;
  LongInt Type;
  char *Name;
} TRelocEntry, *PRelocEntry;

typedef struct
{
  char *Name;
  LargeInt Value;
  LongInt Flags;
} TExportEntry, *PExportEntry;

typedef struct
{
  LongInt RelocCount, ExportCount;
  PRelocEntry RelocEntries;
  PExportEntry ExportEntries;
  char *Strings;
} TRelocInfo, *PRelocInfo;

extern Word FileID;

extern char *OutName;

extern const char *SegNames[PCMax + 1];

extern void WrCopyRight(const char *Msg);

extern void DelSuffix(char *Name);

extern void AddSuffix(char *Name, unsigned NameSize, char *Suff);

extern void FormatError(const char *Name, const char *Detail);

extern void ChkIO(const char *Name);

extern Word Granularity(Byte Header, Byte Segment);

extern void ReadRecordHeader(Byte *Header, Byte *Target, Byte* Segment,
                             Byte *Gran, const char *Name, FILE *f);

extern void WriteRecordHeader(Byte *Header, Byte *Target, Byte* Segment,
                              Byte *Gran, const char *Name, FILE *f);

extern void SkipRecord(Byte Header, const char *Name, FILE *f);

extern PRelocInfo ReadRelocInfo(FILE *f);

extern void DestroyRelocInfo(PRelocInfo PInfo);

extern CMDResult CMD_FilterList(Boolean Negate, const char *Arg);

extern Boolean FilterOK(Byte Header);

extern Boolean RemoveOffset(char *Name, LongWord *Offset);


extern void EraseFile(const char *FileName, LongWord Offset);


extern Boolean AddressWildcard(const char *addr);


extern void toolutils_init(const char *ProgPath);

#endif /* _TOOLUTILS_H */
