/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#ifndef _LIBRARY_DATABASE_H
#define _LIBRARY_DATABASE_H

#include "Types.h"
#include "FileMan.h"

namespace wiz8 { class File; }

#define FILENAME_SIZE 256

//#define	FILENAME_SIZE									40 + PATH_SIZE
#define PATH_SIZE 80

typedef struct {
    CHAR8 sLibraryName[FILENAME_SIZE]; // The name of the library file on the disk
    BOOLEAN fOnCDrom; // A flag specifying if its a cdrom library ( not implemented yet )
    BOOLEAN
    fInitOnStart; // Flag specifying if the library is to Initialized at the begining of the game

} LibraryInitHeader;

#include "WizLibs.h"

#ifdef __cplusplus
extern "C" {
#endif
extern CHAR8 gzCdDirectory[SGPFILENAME_LEN];
#ifdef __cplusplus
}
#endif

typedef struct {
    STR pFileName;
    UINT32 uiFileLength;
    UINT32 uiFileOffset;
    SGP_FILETIME sFileTime;
} FileHeaderStruct;

typedef struct {
    STR sLibraryPath;
    wiz8::File* hLibraryHandle; // Owned by the slot; explicitly delete on close.
    UINT16 usNumberOfEntries;
    BOOLEAN fLibraryOpen;
    BOOLEAN fPatchLibrary;
    FileHeaderStruct* pFileHeader;

    //
    //	Temp:	Total memory used for each library ( all memory allocated
    //

} LibraryHeaderStruct;

typedef struct {
    STR sManagerName;
    LibraryHeaderStruct* pLibraries;
    UINT16 usNumberOfLibraries;
    BOOLEAN fInitialized;
} DatabaseManagerHeaderStruct;

//typedef UINT32	HLIBFILE;

//*************************************************************************
//
//  NOTE!  The following structs are also used by the datalib98 utility
//
//*************************************************************************

#define FILE_OK 0
#define FILE_DELETED 0xff
#define FILE_OLD 1
#define FILE_DOESNT_EXIST 0xfe

typedef struct {
    CHAR8 sLibName[FILENAME_SIZE];
    CHAR8 sPathToLibrary[FILENAME_SIZE];
    INT32 iEntries;
    INT32 iUsed;
    UINT16 iSort;
    UINT16 iVersion;
    BOOLEAN fContainsSubDirectories;
    INT32 iReserved;
} LIBHEADER;

typedef struct {
    CHAR8 sFileName[FILENAME_SIZE];
    UINT32 uiOffset;
    UINT32 uiLength;
    UINT8 ubState;
    UINT8 ubReserved;
    SGP_FILETIME sFileTime;
    UINT16 usReserved2;
} DIRENTRY;

#ifdef __cplusplus
extern "C" {
#endif

//The FileDatabaseHeader
extern DatabaseManagerHeaderStruct gFileDataBase;

//Function Prototypes

BOOLEAN InitializeLibrary(STR pLibraryName, LibraryHeaderStruct* pLibheader, BOOLEAN fCanBeOnCDrom);
// Independent owned stream; callers should immediately adopt into unique_ptr.
// Starts at the entry's physical offset. Caller enforces its entry length.
// nullptr on failure; it remains usable after the database entry is closed.
wiz8::File* OpenLibraryStream(HWFILE file);

BOOLEAN InitializeFileDatabase(void);
INT32 LoadPatchSlfArchives(const CHAR8* directory);
BOOLEAN ReopenCDLibraries(void);
BOOLEAN ShutDownFileDatabase();
BOOLEAN CheckIfFileExistInLibrary(STR pFileName);
INT16 GetLibraryIDFromFileName(const char* pFileName);
HWFILE OpenFileFromLibrary(const char* pName);
//used to open and close libraries during the game
BOOLEAN CloseLibrary(INT16 sLibraryID);
BOOLEAN OpenLibrary(INT16 sLibraryID);

BOOLEAN IsLibraryOpened(INT16 sLibraryID);
#ifdef __cplusplus
}
#endif

#endif
