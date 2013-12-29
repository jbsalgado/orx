/* Orx - Portable Game Engine
 *
 * Copyright (c) 2008-2013 Orx-Project
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *    1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 *
 *    2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 *
 *    3. This notice may not be removed or altered from any source
 *    distribution.
 */

/**
 * @file orxResource.c
 * @date 20/01/2013
 * @author iarwain@orx-project.org
 *
 */


#include "core/orxResource.h"

#include "debug/orxDebug.h"
#include "core/orxClock.h"
#include "core/orxConfig.h"
#include "core/orxEvent.h"
#include "core/orxThread.h"
#include "debug/orxProfiler.h"
#include "io/orxFile.h"
#include "memory/orxBank.h"
#include "memory/orxMemory.h"
#include "utils/orxHashTable.h"
#include "utils/orxLinkList.h"
#include "utils/orxString.h"

#ifdef __orxANDROID__

#include "main/orxAndroid.h"

#endif /* __orxANDROID__ */

/** Module flags
 */
#define orxRESOURCE_KU32_STATIC_FLAG_NONE             0x00000000                      /**< No flags */

#define orxRESOURCE_KU32_STATIC_FLAG_READY            0x00000001                      /**< Ready flag */
#define orxRESOURCE_KU32_STATIC_FLAG_CONFIG_LOADED    0x00000002                      /**< Config loaded flag */
#define orxRESOURCE_KU32_STATIC_FLAG_WATCH_SET        0x00000004                      /**< Watch set flag */

#define orxRESOURCE_KU32_STATIC_MASK_ALL              0xFFFFFFFF                      /**< All mask */


/** Misc
 */
#define orxRESOURCE_KU32_CACHE_TABLE_SIZE             32                              /**< Resource info cache table size */

#define orxRESOURCE_KU32_RESOURCE_INFO_BANK_SIZE      128                             /**< Resource info bank size */

#define orxRESOURCE_KU32_STORAGE_BANK_SIZE            16                              /**< Storage bank size */
#define orxRESOURCE_KU32_GROUP_BANK_SIZE              8                               /**< Group bank size */
#define orxRESOURCE_KU32_TYPE_BANK_SIZE               8                               /**< Type bank size */

#define orxRESOURCE_KU32_OPEN_INFO_BANK_SIZE          8                               /**< Open resource info bank size */

#define orxRESOURCE_KU32_WATCH_ITERATION_LIMIT        2                               /**< Watch iteration limit */
#define orxRESOURCE_KU32_WATCH_TIME_UNINITIALIZED     -1                              /**< Watch time uninitialized */

#define orxRESOURCE_KZ_DEFAULT_STORAGE                "."                             /**< Default storage */

#define orxRESOURCE_KZ_TYPE_TAG_FILE                  "file"                          /**< Resource type file tag */

#define orxRESOURCE_KU32_BUFFER_SIZE                  256                             /**< Buffer size */

#define orxRESOURCE_KZ_CONFIG_SECTION                 "Resource"                      /**< Config section name */
#define orxRESOURCE_KZ_CONFIG_WATCH_LIST              "WatchList"                     /**< Config watch list */

#define orxRESOURCE_KU32_REQUEST_LIST_SIZE            128                             /**< Request list size */


/***************************************************************************
 * Structure declaration                                                   *
 ***************************************************************************/

/** Resource type
 */
typedef struct __orxRESOURCE_TYPE_t
{
  orxLINKLIST_NODE          stNode;                                                   /**< Linklist node */
  orxRESOURCE_TYPE_INFO     stInfo;                                                   /**< Type info */

} orxRESOURCE_TYPE;

/** Resource group
 */
typedef struct __orxRESOURCE_GROUP_t
{
  orxU32                    u32ID;                                                    /**< Group name ID */
  orxLINKLIST               stStorageList;                                            /**< Group storage list */
  orxBANK                  *pstStorageBank;                                           /**< Group storage bank */
  orxHASHTABLE             *pstCacheTable;                                            /**< Group cache table */

} orxRESOURCE_GROUP;

/** Storage info
 */
typedef struct __orxRESOURCE_STORAGE_t
{
  orxLINKLIST_NODE          stNode;                                                   /**< Linklist node */
  orxU32                    u32StorageID;                                             /**< Literal storage ID */

} orxRESOURCE_STORAGE;

/** Resource info
 */
typedef struct __orxRESOURCE_INFO_t
{
  orxSTRING                 zLocation;                                                /**< Resource literal location */
  orxRESOURCE_TYPE_INFO    *pstTypeInfo;                                              /**< Resource type info */
  orxS64                    s64Time;                                                  /**< Resource modification time */
  orxU32                    u32NameID;                                                /**< Resource name ID */

} orxRESOURCE_INFO;

/** Open resource info
 */
typedef struct __orxRESOURCE_OPEN_INFO_t
{
  orxRESOURCE_TYPE_INFO    *pstTypeInfo;                                              /**< Resource type info */
  orxHANDLE                 hResource;                                                /**< Resource handle */
  volatile orxU32           u32OpCounter;                                             /**< Operation counter */

} orxRESOURCE_OPEN_INFO;

/** Request type enum
 */
typedef enum __orxRESOURCE_REQUEST_TYPE_t
{
  orxRESOURCE_REQUEST_TYPE_READ = 0,
  orxRESOURCE_REQUEST_TYPE_WRITE,
  orxRESOURCE_REQUEST_TYPE_CLOSE,

  orxRESOURCE_REQUEST_TYPE_NUMBER,

  orxRESOURCE_REQUEST_TYPE_NONE = orxENUM_NONE

} orxRESOURCE_REQUEST_TYPE;

/** Request
 */
typedef struct __orxRESOURCE_REQUEST_t
{
  orxS64                    s64Size;                                                  /**< Request buffer size */
  void                     *pBuffer;                                                  /**< Request buffer */
  orxRESOURCE_OP_FUNCTION   pfnCallback;                                              /**< Request completion callback */
  void                     *pContext;                                                 /**< Request context */
  orxRESOURCE_OPEN_INFO    *pstResourceInfo;                                          /**< Request open resource info */
  orxRESOURCE_REQUEST_TYPE  eType;                                                    /**< Request type */

} orxRESOURCE_REQUEST;

/** Static structure
 */
typedef struct __orxRESOURCE_STATIC_t
{
  orxBANK                  *pstGroupBank;                                             /**< Group bank */
  orxBANK                  *pstTypeBank;                                              /**< Type info bank */
  orxBANK                  *pstResourceInfoBank;                                      /**< Resource info bank */
  orxBANK                  *pstOpenInfoBank;                                          /**< Open resource table size */
  orxLINKLIST               stTypeList;                                               /**< Type list */
  orxSTRING                 zLastUncachedLocation;                                    /**< Last uncached location */
  orxCHAR                   acFileLocationBuffer[orxRESOURCE_KU32_BUFFER_SIZE];       /**< File location buffer size */
  volatile orxRESOURCE_REQUEST astRequestList[orxRESOURCE_KU32_REQUEST_LIST_SIZE];    /**< Request list */
  volatile orxU32           u32RequestInIndex;                                        /**< Request in index */
  volatile orxU32           u32RequestProcessIndex;                                   /**< Request process index */
  volatile orxU32           u32RequestOutIndex;                                       /**< Request out index */
  orxU32                    u32RequestThreadID;                                       /**< Request thread ID */
  orxU32                    u32Flags;                                                 /**< Control flags */

} orxRESOURCE_STATIC;


/***************************************************************************
 * Static variables                                                        *
 ***************************************************************************/

/** Static data
 */
static orxRESOURCE_STATIC sstResource;


/***************************************************************************
 * Private functions                                                       *
 ***************************************************************************/

static const orxSTRING orxFASTCALL orxResource_File_Locate(const orxSTRING _zStorage, const orxSTRING _zName, orxBOOL _bRequireExistence)
{
  const orxSTRING zResult = orxNULL;

  /* Default storage? */
  if(orxString_Compare(_zStorage, orxRESOURCE_KZ_DEFAULT_STORAGE) == 0)
  {
    /* Uses name as path */
    orxString_NPrint(sstResource.acFileLocationBuffer, orxRESOURCE_KU32_BUFFER_SIZE - 1, "%s", _zName);
  }
  else
  {
    /* Composes full name */
    orxString_NPrint(sstResource.acFileLocationBuffer, orxRESOURCE_KU32_BUFFER_SIZE - 1, "%s%c%s", _zStorage, orxCHAR_DIRECTORY_SEPARATOR_LINUX, _zName);
  }

  /* Exists or doesn't require existence? */
  if((_bRequireExistence == orxFALSE)
  || (orxFile_Exists(sstResource.acFileLocationBuffer) != orxFALSE))
  {
    /* Updates result */
    zResult = sstResource.acFileLocationBuffer;
  }

  /* Done! */
  return zResult;
}

static orxHANDLE orxFASTCALL orxResource_File_Open(const orxSTRING _zPath, orxBOOL _bEraseMode)
{
  orxFILE  *pstFile;
  orxHANDLE hResult;

  /* Opens file */
  pstFile = orxFile_Open(_zPath, (_bEraseMode != orxFALSE) ? orxFILE_KU32_FLAG_OPEN_WRITE | orxFILE_KU32_FLAG_OPEN_BINARY : orxFILE_KU32_FLAG_OPEN_READ | orxFILE_KU32_FLAG_OPEN_WRITE | orxFILE_KU32_FLAG_OPEN_BINARY);

  /* Not in erase mode and couldn't open it? */
  if((_bEraseMode == orxFALSE) && (pstFile == orxNULL))
  {
    /* Opens it in read-only mode */
    pstFile = orxFile_Open(_zPath, orxFILE_KU32_FLAG_OPEN_READ | orxFILE_KU32_FLAG_OPEN_BINARY);
  }

  /* Updates result */
  hResult = (pstFile != orxNULL) ? (orxHANDLE)pstFile : orxHANDLE_UNDEFINED;

  /* Done! */
  return hResult;
}

static void orxFASTCALL orxResource_File_Close(orxHANDLE _hResource)
{
  orxFILE *pstFile;

  /* Gets file */
  pstFile = (orxFILE *)_hResource;

  /* Closes it */
  orxFile_Close(pstFile);

  /* Done! */
  return;
}

static orxS64 orxFASTCALL orxResource_File_GetSize(orxHANDLE _hResource)
{
  orxFILE  *pstFile;
  orxS64    s64Result;

  /* Gets file */
  pstFile = (orxFILE *)_hResource;

  /* Updates result */
  s64Result = orxFile_GetSize(pstFile);

  /* Done! */
  return s64Result;
}

static orxS64 orxFASTCALL orxResource_File_GetTime(const orxSTRING _zPath)
{
  orxFILE_INFO  stFileInfo;
  orxS64        s64Result;

  /* Gets file info */
  if(orxFile_GetInfo(_zPath, &stFileInfo) != orxSTATUS_FAILURE)
  {
    /* Updates result */
    s64Result = stFileInfo.s64TimeStamp;
  }
  else
  {
    /* Updates result */
    s64Result = 0;
  }

  /* Done! */
  return s64Result;
}

static orxS64 orxFASTCALL orxResource_File_Seek(orxHANDLE _hResource, orxS64 _s64Offset, orxSEEK_OFFSET_WHENCE _eWhence)
{
  orxFILE  *pstFile;
  orxS64    s64Result;

  /* Gets file */
  pstFile = (orxFILE *)_hResource;

  /* Updates result */
  s64Result = orxFile_Seek(pstFile, _s64Offset, _eWhence);

  /* Done! */
  return s64Result;
}

static orxS64 orxFASTCALL orxResource_File_Tell(orxHANDLE _hResource)
{
  orxFILE  *pstFile;
  orxS64    s64Result;

  /* Gets file */
  pstFile = (orxFILE *)_hResource;

  /* Updates result */
  s64Result = orxFile_Tell(pstFile);

  /* Done! */
  return s64Result;
}

static orxS64 orxFASTCALL orxResource_File_Read(orxHANDLE _hResource, orxS64 _s64Size, void *_pBuffer)
{
  orxFILE  *pstFile;
  orxS64    s64Result;

  /* Gets file */
  pstFile = (orxFILE *)_hResource;

  /* Updates result */
  s64Result = orxFile_Read(_pBuffer, sizeof(orxCHAR), _s64Size, pstFile);

  /* Done! */
  return s64Result;
}

static orxS64 orxFASTCALL orxResource_File_Write(orxHANDLE _hResource, orxS64 _s64Size, const void *_pBuffer)
{
  orxFILE  *pstFile;
  orxS64    s64Result;

  /* Gets file */
  pstFile = (orxFILE *)_hResource;

  /* Updates result */
  s64Result = orxFile_Write(_pBuffer, sizeof(orxCHAR), _s64Size, pstFile);

  /* Done! */
  return s64Result;
}

static orxINLINE void orxResource_DeleteGroup(orxRESOURCE_GROUP *_pstGroup)
{
  orxRESOURCE_INFO *pstResourceInfo;
  orxU32            u32Key;
  orxHANDLE         hIterator;

  /* For all cached resources */
  for(hIterator = orxHashTable_GetNext(_pstGroup->pstCacheTable, orxHANDLE_UNDEFINED, &u32Key, (void **)&pstResourceInfo);
      hIterator != orxHANDLE_UNDEFINED;
      hIterator = orxHashTable_GetNext(_pstGroup->pstCacheTable, hIterator, &u32Key, (void **)&pstResourceInfo))
  {
    /* Deletes its location */
    orxMemory_Free(pstResourceInfo->zLocation);

    /* Frees it */
    orxBank_Free(sstResource.pstResourceInfoBank, pstResourceInfo);
  }

  /* Deletes cache table */
  orxHashTable_Delete(_pstGroup->pstCacheTable);

  /* Deletes storage bank */
  orxBank_Delete(_pstGroup->pstStorageBank);

  /* Deletes it */
  orxBank_Free(sstResource.pstGroupBank, _pstGroup);

  /* Done! */
  return;
}

static orxINLINE orxRESOURCE_GROUP *orxResource_CreateGroup(orxU32 _u32GroupID)
{
  orxRESOURCE_GROUP *pstResult;

  /* Creates group */
  pstResult = (orxRESOURCE_GROUP *)orxBank_Allocate(sstResource.pstGroupBank);

  /* Success? */
  if(pstResult != orxNULL)
  {
    orxRESOURCE_STORAGE *pstStorage;

    /* Inits it */
    pstResult->u32ID          = _u32GroupID;
    pstResult->pstStorageBank = orxBank_Create(orxRESOURCE_KU32_STORAGE_BANK_SIZE, sizeof(orxRESOURCE_STORAGE), orxBANK_KU32_FLAG_NONE, orxMEMORY_TYPE_MAIN);
    pstResult->pstCacheTable  = orxHashTable_Create(orxRESOURCE_KU32_CACHE_TABLE_SIZE, orxHASHTABLE_KU32_FLAG_NONE, orxMEMORY_TYPE_MAIN);
    orxMemory_Zero(&(pstResult->stStorageList), sizeof(orxLINKLIST));

    /* Creates storage */
    pstStorage = (orxRESOURCE_STORAGE *)orxBank_Allocate(pstResult->pstStorageBank);

    /* Success? */
    if(pstStorage != orxNULL)
    {
      /* Stores its content */
      pstStorage->u32StorageID = orxString_GetID(orxRESOURCE_KZ_DEFAULT_STORAGE);

      /* Clears node */
      orxMemory_Zero(&(pstStorage->stNode), sizeof(orxLINKLIST_NODE));

      /* Adds it first */
      orxLinkList_AddStart(&(pstResult->stStorageList), &(pstStorage->stNode));
    }
    else
    {
      /* Deletes it */
      orxResource_DeleteGroup(pstResult);

      /* Updates result */
      pstResult = orxNULL;
    }
  }

  /* Done! */
  return pstResult;
}

static void orxFASTCALL orxResource_Watch(const orxCLOCK_INFO *_pstClockInfo, void *_pContext)
{
  static orxS32             ss32GroupIndex = 0;
  orxS32                    s32ListCounter;
  orxU32                    u32WatchCounter = 0;
  orxBOOL                   bAbort = orxFALSE;
  orxRESOURCE_EVENT_PAYLOAD stPayload;

  /* Profiles */
  orxPROFILER_PUSH_MARKER("orxResource_Watch");

  /* Clears payload */
  orxMemory_Zero(&stPayload, sizeof(orxRESOURCE_EVENT_PAYLOAD));

  /* Pushes config section */
  orxConfig_PushSection(orxRESOURCE_KZ_CONFIG_SECTION);

  /* For all watched groups */
  for(s32ListCounter = orxConfig_GetListCounter(orxRESOURCE_KZ_CONFIG_WATCH_LIST); ss32GroupIndex < s32ListCounter; ss32GroupIndex++)
  {
    orxRESOURCE_GROUP  *pstGroup;
    orxU32              u32GroupID;
    static orxU32       su32LastGroupID = 0;

    /* Gets its ID */
    u32GroupID = orxString_ToCRC(orxConfig_GetListString(orxRESOURCE_KZ_CONFIG_WATCH_LIST, ss32GroupIndex));

    /* Looks for it in registered groups */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        (pstGroup != orxNULL) && (pstGroup->u32ID != u32GroupID);
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup));

    /* Found? */
    if(pstGroup != orxNULL)
    {
      static orxHANDLE  hIterator;
      orxU32            u32Key;
      orxRESOURCE_INFO *pstResourceInfo;

      /* Inits payload group */
      stPayload.u32GroupID = u32GroupID;

      /* New group? */
      if(u32GroupID != su32LastGroupID)
      {
        /* Resets iterator */
        hIterator = orxHANDLE_UNDEFINED;

        /* Stores group ID */
        su32LastGroupID = u32GroupID;
      }

      /* For all its cached resources */
      for(hIterator = orxHashTable_GetNext(pstGroup->pstCacheTable, hIterator, &u32Key, (void **)&pstResourceInfo);
          hIterator != orxHANDLE_UNDEFINED;
          hIterator = orxHashTable_GetNext(pstGroup->pstCacheTable, hIterator, &u32Key, (void **)&pstResourceInfo))
      {
        /* Does its type support time? */
        if(pstResourceInfo->pstTypeInfo->pfnGetTime != orxNULL)
        {
          orxS64 s64Time;
          orxU32 u32PathOffset;

          /* Gets path offset */
          u32PathOffset = orxString_GetLength(pstResourceInfo->pstTypeInfo->zTag) + 1;

          /* Gets its modification time */
          s64Time = pstResourceInfo->pstTypeInfo->pfnGetTime(pstResourceInfo->zLocation + u32PathOffset);

          /* Has been modified since last access? */
          if(s64Time != pstResourceInfo->s64Time)
          {
            /* Not first inspection? */
            if(pstResourceInfo->s64Time != orxRESOURCE_KU32_WATCH_TIME_UNINITIALIZED)
            {
              /* Inits rest of payload */
              stPayload.s64Time     = s64Time;
              stPayload.zLocation   = pstResourceInfo->zLocation;
              stPayload.zPath       = pstResourceInfo->zLocation + u32PathOffset;
              stPayload.pstTypeInfo = pstResourceInfo->pstTypeInfo;
              stPayload.u32NameID   = u32Key;

              /* Removed? */
              if(s64Time == 0)
              {
                /* Sends event */
                orxEVENT_SEND(orxEVENT_TYPE_RESOURCE, orxRESOURCE_EVENT_REMOVE, orxNULL, orxNULL, &stPayload);
              }
              else
              {
                /* Added? */
                if(pstResourceInfo->s64Time == 0)
                {
                  /* Sends event */
                  orxEVENT_SEND(orxEVENT_TYPE_RESOURCE, orxRESOURCE_EVENT_ADD, orxNULL, orxNULL, &stPayload);
                }
                /* Updated */
                else
                {
                  /* Sends event */
                  orxEVENT_SEND(orxEVENT_TYPE_RESOURCE, orxRESOURCE_EVENT_UPDATE, orxNULL, orxNULL, &stPayload);
                }
              }
            }

            /* Stores its new modification time */
            pstResourceInfo->s64Time = s64Time;
          }

          /* Updates watch counter */
          u32WatchCounter++;

          /* Reached limit? */
          if(u32WatchCounter >= orxRESOURCE_KU32_WATCH_ITERATION_LIMIT)
          {
            /* Aborts */
            bAbort = orxTRUE;

            break;
          }
        }
      }

      /* Abort? */
      if(bAbort != orxFALSE)
      {
        /* Stops */
        break;
      }
    }
  }

  /* Watched all resources? */
  if(ss32GroupIndex >= s32ListCounter)
  {
    /* Reset */
    ss32GroupIndex = 0;
  }

  /* Profiles */
  orxPROFILER_POP_MARKER();

  /* Pops config section */
  orxConfig_PopSection();

  /* Done! */
  return;
}

static void orxFASTCALL orxResource_NotifyRequest(const orxCLOCK_INFO *_pstClockInfo, void *_pContext)
{
  /* While there are processed requests */
  while(sstResource.u32RequestOutIndex != sstResource.u32RequestProcessIndex)
  {
    volatile orxRESOURCE_REQUEST *pstRequest;

    /* Gets request */
    pstRequest = &(sstResource.astRequestList[sstResource.u32RequestOutIndex]);

    /* Has callback? */
    if(pstRequest->pfnCallback != orxNULL)
    {
      /* Notifies it */
      pstRequest->pfnCallback((orxHANDLE)pstRequest->pstResourceInfo, pstRequest->s64Size, pstRequest->pBuffer, pstRequest->pContext);
    }

    /* Decrements operation counter */
    if(pstRequest->pstResourceInfo != orxNULL)
    {
      pstRequest->pstResourceInfo->u32OpCounter--;
    }

    /* Updates request out index */
    orxMEMORY_BARRIER();
    sstResource.u32RequestOutIndex = (sstResource.u32RequestOutIndex + 1) & (orxRESOURCE_KU32_REQUEST_LIST_SIZE - 1);
  }

  /* Done! */
  return;
}

static orxSTATUS orxFASTCALL orxResource_ProcessRequests(void *_pContext)
{
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* While there are pending requests */
  while(sstResource.u32RequestProcessIndex != sstResource.u32RequestInIndex)
  {
    volatile orxRESOURCE_REQUEST *pstRequest;

    /* Gets request */
    pstRequest = &(sstResource.astRequestList[sstResource.u32RequestProcessIndex]);

    /* Depending on request type */
    switch(pstRequest->eType)
    {
      case orxRESOURCE_REQUEST_TYPE_READ:
      {
        /* Services it */
        pstRequest->s64Size = pstRequest->pstResourceInfo->pstTypeInfo->pfnRead(pstRequest->pstResourceInfo->hResource, pstRequest->s64Size, pstRequest->pBuffer);

        break;
      }

      case orxRESOURCE_REQUEST_TYPE_WRITE:
      {
        /* Services it */
        pstRequest->s64Size = pstRequest->pstResourceInfo->pstTypeInfo->pfnWrite(pstRequest->pstResourceInfo->hResource, pstRequest->s64Size, pstRequest->pBuffer);

        break;
      }

      case orxRESOURCE_REQUEST_TYPE_CLOSE:
      {
        /* Services it */
        pstRequest->pstResourceInfo->pstTypeInfo->pfnClose(pstRequest->pstResourceInfo->hResource);

        /* Frees open info */
        orxBank_Free(sstResource.pstOpenInfoBank, pstRequest->pstResourceInfo);
        pstRequest->pstResourceInfo = orxNULL;

        break;
      }

      default:
      {
        break;
      }
    }

    /* Updates request process index */
    orxMEMORY_BARRIER();
    sstResource.u32RequestProcessIndex = (sstResource.u32RequestProcessIndex + 1) & (orxRESOURCE_KU32_REQUEST_LIST_SIZE - 1);
  }

  /* Done! */
  return eResult;
}

static void orxResource_AddRequest(orxRESOURCE_REQUEST_TYPE _eType, orxS64 _s64Size, void *_pBuffer, orxRESOURCE_OP_FUNCTION _pfnCallback, void *_pContext, orxRESOURCE_OPEN_INFO *_pstResourceInfo)
{
  volatile orxRESOURCE_REQUEST *pstRequest;
  orxU32                        u32NextRequestIndex;

  /* Checks */
  orxASSERT(orxThread_GetCurrent() == orxTHREAD_KU32_MAIN_THREAD_ID);

  /* Gets next request index */
  u32NextRequestIndex = (sstResource.u32RequestInIndex + 1) & (orxRESOURCE_KU32_REQUEST_LIST_SIZE - 1);

  /* Waits for a free slot */
  while(u32NextRequestIndex == sstResource.u32RequestOutIndex)
  {
    /* Manually pumps some request notifications */
    orxResource_NotifyRequest(orxNULL, orxNULL);
  }

  /* Gets current request */
  pstRequest = &(sstResource.astRequestList[sstResource.u32RequestInIndex]);

  /* Inits it */
  _pstResourceInfo->u32OpCounter++;
  pstRequest->s64Size         = _s64Size;
  pstRequest->pBuffer         = _pBuffer;
  pstRequest->pfnCallback     = _pfnCallback;
  pstRequest->pContext        = _pContext;
  pstRequest->pstResourceInfo = _pstResourceInfo;
  pstRequest->eType           = _eType;

  /* Commits request */
  orxMEMORY_BARRIER();
  sstResource.u32RequestInIndex = u32NextRequestIndex;
}


/***************************************************************************
 * Public functions                                                        *
 ***************************************************************************/

/** Resource module setup
 */
void orxFASTCALL orxResource_Setup()
{
  /* Adds module dependencies */
  orxModule_AddDependency(orxMODULE_ID_RESOURCE, orxMODULE_ID_MEMORY);
  orxModule_AddDependency(orxMODULE_ID_RESOURCE, orxMODULE_ID_BANK);
  orxModule_AddDependency(orxMODULE_ID_RESOURCE, orxMODULE_ID_THREAD);
  orxModule_AddDependency(orxMODULE_ID_RESOURCE, orxMODULE_ID_STRING);
  orxModule_AddDependency(orxMODULE_ID_RESOURCE, orxMODULE_ID_EVENT);
  orxModule_AddDependency(orxMODULE_ID_RESOURCE, orxMODULE_ID_FILE);
  orxModule_AddDependency(orxMODULE_ID_RESOURCE, orxMODULE_ID_PROFILER);

  /* Done! */
  return;
}

/** Inits resource module
 * @return orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxResource_Init()
{
  orxSTATUS eResult = orxSTATUS_FAILURE;

  /* Checks */
  orxASSERT(orxMath_IsPowerOfTwo(orxRESOURCE_KU32_REQUEST_LIST_SIZE) != orxFALSE);

  /* Not already Initialized? */
  if(!(sstResource.u32Flags & orxRESOURCE_KU32_STATIC_FLAG_READY))
  {
    /* Cleans control structure */
    orxMemory_Zero(&sstResource, sizeof(orxRESOURCE_STATIC));

    /* Inits request thread ID */
    sstResource.u32RequestThreadID = orxU32_UNDEFINED;

    /* Creates resource info bank */
    sstResource.pstResourceInfoBank = orxBank_Create(orxRESOURCE_KU32_RESOURCE_INFO_BANK_SIZE, sizeof(orxRESOURCE_INFO), orxBANK_KU32_FLAG_NONE, orxMEMORY_TYPE_MAIN);

    /* Creates open resource info bank */
    sstResource.pstOpenInfoBank     = orxBank_Create(orxRESOURCE_KU32_OPEN_INFO_BANK_SIZE, sizeof(orxRESOURCE_OPEN_INFO), orxBANK_KU32_FLAG_NONE, orxMEMORY_TYPE_MAIN);

    /* Creates group bank */
    sstResource.pstGroupBank        = orxBank_Create(orxRESOURCE_KU32_GROUP_BANK_SIZE, sizeof(orxRESOURCE_GROUP), orxBANK_KU32_FLAG_NONE, orxMEMORY_TYPE_MAIN);

    /* Creates type info bank */
    sstResource.pstTypeBank         = orxBank_Create(orxRESOURCE_KU32_TYPE_BANK_SIZE, sizeof(orxRESOURCE_TYPE), orxBANK_KU32_FLAG_NONE, orxMEMORY_TYPE_MAIN);

    /* Success? */
    if((sstResource.pstResourceInfoBank != orxNULL) && (sstResource.pstOpenInfoBank != orxNULL) && (sstResource.pstGroupBank != orxNULL) && (sstResource.pstTypeBank != orxNULL))
    {
      orxRESOURCE_TYPE_INFO stTypeInfo;

      /* Inits flags */
      sstResource.u32Flags = orxRESOURCE_KU32_STATIC_FLAG_READY;

      /* Inits file type */
      stTypeInfo.zTag       = orxRESOURCE_KZ_TYPE_TAG_FILE;
      stTypeInfo.pfnLocate  = orxResource_File_Locate;
      stTypeInfo.pfnGetTime = orxResource_File_GetTime;
      stTypeInfo.pfnOpen    = orxResource_File_Open;
      stTypeInfo.pfnClose   = orxResource_File_Close;
      stTypeInfo.pfnGetSize = orxResource_File_GetSize;
      stTypeInfo.pfnSeek    = orxResource_File_Seek;
      stTypeInfo.pfnTell    = orxResource_File_Tell;
      stTypeInfo.pfnRead    = orxResource_File_Read;
      stTypeInfo.pfnWrite   = orxResource_File_Write;

      /* Registers it */
      eResult = orxResource_RegisterType(&stTypeInfo);

      /* Success? */
      if(eResult != orxSTATUS_FAILURE)
      {
        /* Starts request processing thread */
        sstResource.u32RequestThreadID = orxThread_Start(&orxResource_ProcessRequests, orxNULL);

        /* Success? */
        if(sstResource.u32RequestThreadID != orxU32_UNDEFINED)
        {
#ifdef __orxANDROID__

          /* Registers APK type */
          eResult = orxAndroid_RegisterAPKResource();

#endif /* __orxANDROID__ */
        }
      }
    }

    /* Failed? */
    if(eResult != orxSTATUS_SUCCESS)
    {
      /* Removes Flags */
      sstResource.u32Flags &= ~orxRESOURCE_KU32_STATIC_FLAG_READY;

      /* Deletes info bank */
      if(sstResource.pstResourceInfoBank != orxNULL)
      {
        orxBank_Delete(sstResource.pstResourceInfoBank);
      }

      /* Deletes open info bank */
      if(sstResource.pstOpenInfoBank != orxNULL)
      {
        orxBank_Delete(sstResource.pstOpenInfoBank);
      }

      /* Deletes group bank */
      if(sstResource.pstGroupBank != orxNULL)
      {
        orxBank_Delete(sstResource.pstGroupBank);
      }

      /* Deletes type bank */
      if(sstResource.pstTypeBank != orxNULL)
      {
        orxBank_Delete(sstResource.pstTypeBank);
      }

      /* Has request thread? */
      if(sstResource.u32RequestThreadID != orxU32_UNDEFINED)
      {
        /* Joins it */
        orxThread_Join(sstResource.u32RequestThreadID);
        sstResource.u32RequestThreadID = orxU32_UNDEFINED;
      }

      /* Logs message */
      orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Couldn't init resource module: can't allocate internal banks.");
    }
  }
  else
  {
    /* Logs message */
    orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Tried to initialize resource module when it was already initialized.");

    /* Already initialized */
    eResult = orxSTATUS_SUCCESS;
  }

  /* Done! */
  return eResult;
}

/** Exits from resource module
 */
void orxFASTCALL orxResource_Exit()
{
  /* Initialized? */
  if(sstResource.u32Flags & orxRESOURCE_KU32_STATIC_FLAG_READY)
  {
    orxRESOURCE_GROUP      *pstGroup;
    orxRESOURCE_OPEN_INFO  *pstOpenInfo;

    /* Waits for all pending operations to complete */
    while(sstResource.u32RequestProcessIndex != sstResource.u32RequestInIndex);

    /* Joins request thread */
    orxThread_Join(sstResource.u32RequestThreadID);
    sstResource.u32RequestThreadID = orxU32_UNDEFINED;

    /* Don't unregister clock callbacks as the clock module has already exited */

    /* Has uncached location? */
    if(sstResource.zLastUncachedLocation != orxNULL)
    {
      /* Deletes it */
      orxMemory_Free(sstResource.zLastUncachedLocation);
    }

    /* For all groups */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        pstGroup != orxNULL;
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup))
    {
      /* Deletes it */
      orxResource_DeleteGroup(pstGroup);
    }

    /* Deletes group bank */
    orxBank_Delete(sstResource.pstGroupBank);

    /* Deletes type bank */
    orxBank_Delete(sstResource.pstTypeBank);

    /* For all open resources */
    while((pstOpenInfo = (orxRESOURCE_OPEN_INFO *)orxBank_GetNext(sstResource.pstOpenInfoBank, orxNULL)) != orxNULL)
    {
      /* Closes it */
      orxResource_Close((orxHANDLE)pstOpenInfo);
    }

    /* Deletes open info bank */
    orxBank_Delete(sstResource.pstOpenInfoBank);

    /* Checks */
    orxASSERT(orxBank_GetCounter(sstResource.pstResourceInfoBank) == 0);

    /* Deletes info bank */
    orxBank_Delete(sstResource.pstResourceInfoBank);

    /* Updates flags */
    sstResource.u32Flags &= ~orxRESOURCE_KU32_STATIC_FLAG_READY;
  }

  /* Done! */
  return;
}

/** Gets number of resource groups
 * @return Number of resource groups
 */
orxU32 orxFASTCALL orxResource_GetGroupCounter()
{
  orxU32 u32Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Updates result */
  u32Result = orxBank_GetCounter(sstResource.pstGroupBank);

  /* Done! */
  return u32Result;
}

/** Gets resource group at given index
 * @param[in] _u32Index         Index of resource group
 * @return Resource group if index is valid, orxNULL otherwise
 */
const orxSTRING orxFASTCALL orxResource_GetGroup(orxU32 _u32Index)
{
  const orxSTRING zResult = orxNULL;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Valid index? */
  if(_u32Index < orxBank_GetCounter(sstResource.pstGroupBank))
  {
    orxRESOURCE_GROUP *pstGroup;

    /* Finds requested group */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        _u32Index > 0;
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup), _u32Index--);

    /* Checks */
    orxASSERT(pstGroup != orxNULL);

    /* Updates result */
    zResult = orxString_GetFromID(pstGroup->u32ID);
  }

  /* Done! */
  return zResult;
}

/** Adds a storage for a given resource group, this storage will be used when looking for resources prior to any previously added storage
 * @param[in] _zGroup           Concerned resource group
 * @param[in] _zStorage         Description of the storage, as understood by one of the resource type
 * @param[in] _bAddFirst        If true this storage will be used *before* any already added ones, otherwise it'll be used *after* all those
 * @return orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxResource_AddStorage(const orxSTRING _zGroup, const orxSTRING _zStorage, orxBOOL _bAddFirst)
{
  orxSTATUS eResult = orxSTATUS_FAILURE;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zGroup != orxNULL);
  orxASSERT(_zStorage != orxNULL);

  /* Valid? */
  if(*_zGroup != orxCHAR_NULL)
  {
    orxRESOURCE_GROUP  *pstGroup;
    orxU32              u32GroupID;

    /* Gets group ID */
    u32GroupID = orxString_GetID(_zGroup);

    /* Gets group */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        (pstGroup != orxNULL) && (pstGroup->u32ID != u32GroupID);
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup));

    /* Not found? */
    if(pstGroup == orxNULL)
    {
      /* Creates it */
      pstGroup = orxResource_CreateGroup(u32GroupID);
    }

    /* Success? */
    if(pstGroup != orxNULL)
    {
      orxRESOURCE_STORAGE *pstStorage;

      /* Creates storage */
      pstStorage = (orxRESOURCE_STORAGE *)orxBank_Allocate(pstGroup->pstStorageBank);

      /* Success? */
      if(pstStorage != orxNULL)
      {
        /* Stores its content */
        pstStorage->u32StorageID = orxString_GetID(_zStorage);

        /* Clears its node */
        orxMemory_Zero(&(pstStorage->stNode), sizeof(orxLINKLIST_NODE));

        /* Should be added first? */
        if(_bAddFirst != orxFALSE)
        {
          /* Adds it first */
          orxLinkList_AddStart(&(pstGroup->stStorageList), &(pstStorage->stNode));
        }
        else
        {
          /* Adds it last */
          orxLinkList_AddEnd(&(pstGroup->stStorageList), &(pstStorage->stNode));
        }

        /* Updates result */
        eResult = orxSTATUS_SUCCESS;
      }
      else
      {
        /* Logs message */
        orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Can't add storage <%s> to resource group <%s>: unable to allocate memory for storage.", _zStorage, _zGroup);
      }
    }
    else
    {
      /* Logs message */
      orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Can't add storage <%s> to resource group <%s>: unable to allocate memory for group.", _zStorage, _zGroup);
    }
  }

  /* Done! */
  return eResult;
}

/** Removes a storage for a given resource group
 * @param[in] _zGroup           Concerned resource group
 * @param[in] _zStorage         Concerned storage
 * @return orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxResource_RemoveStorage(const orxSTRING _zGroup, const orxSTRING _zStorage)
{
  orxSTATUS eResult = orxSTATUS_FAILURE;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zGroup != orxNULL);
  orxASSERT(_zStorage != orxNULL);

  /* Valid? */
  if(*_zGroup != orxCHAR_NULL)
  {
    orxRESOURCE_GROUP  *pstGroup;
    orxU32              u32GroupID;

    /* Gets group ID */
    u32GroupID = orxString_ToCRC(_zGroup);

    /* Gets group */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        (pstGroup != orxNULL) && (pstGroup->u32ID != u32GroupID);
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup));

    /* Success? */
    if(pstGroup != orxNULL)
    {
      orxRESOURCE_STORAGE  *pstStorage;
      orxU32                u32StorageID;

      /* Gets storage ID */
      u32StorageID = orxString_ToCRC(_zStorage);

      /* For all storages in group */
      for(pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetFirst(&(pstGroup->stStorageList));
          pstStorage != orxNULL;
          pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetNext(&(pstStorage->stNode)))
      {
        /* Matches? */
        if(pstStorage->u32StorageID == u32StorageID)
        {
          /* Removes it from list */
          orxLinkList_Remove(&(pstStorage->stNode));

          /* Frees it */
          orxBank_Free(pstGroup->pstStorageBank, pstStorage);

          /* Updates result */
          eResult = orxSTATUS_SUCCESS;

          break;
        }
      }
    }
  }

  /* Done! */
  return eResult;
}

/** Gets number of storages for a given resource group
 * @param[in] _zGroup           Concerned resource group
 * @return Number of storages for this resource group
 */
orxU32 orxFASTCALL orxResource_GetStorageCounter(const orxSTRING _zGroup)
{
  orxU32 u32Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zGroup != orxNULL);

  /* Valid? */
  if(*_zGroup != orxCHAR_NULL)
  {
    orxRESOURCE_GROUP  *pstGroup = orxNULL;
    orxU32              u32GroupID;

    /* Gets group ID */
    u32GroupID = orxString_GetID(_zGroup);

    /* Gets group */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        (pstGroup != orxNULL) && (pstGroup->u32ID != u32GroupID);
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup));

    /* Success? */
    if(pstGroup != orxNULL)
    {
      /* Updates result */
      u32Result = orxLinkList_GetCounter(&(pstGroup->stStorageList));
    }
  }

  /* Done! */
  return u32Result;
}

/** Gets storage at given index for a given resource group
 * @param[in] _zGroup           Concerned resource group
 * @param[in] _u32Index         Index of storage
 * @return Storage if index is valid, orxNULL otherwise
 */
const orxSTRING orxFASTCALL orxResource_GetStorage(const orxSTRING _zGroup, orxU32 _u32Index)
{
  const orxSTRING zResult = orxNULL;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zGroup != orxNULL);

  /* Valid? */
  if(*_zGroup != orxCHAR_NULL)
  {
    orxRESOURCE_GROUP  *pstGroup = orxNULL;
    orxU32              u32GroupID;

    /* Gets group ID */
    u32GroupID = orxString_GetID(_zGroup);

    /* Gets group */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        (pstGroup != orxNULL) && (pstGroup->u32ID != u32GroupID);
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup));

    /* Success? */
    if(pstGroup != orxNULL)
    {
      /* Valid index? */
      if(_u32Index < orxBank_GetCounter(pstGroup->pstStorageBank))
      {
        orxRESOURCE_STORAGE *pstStorage;

        /* Finds requested storage */
        for(pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetFirst(&(pstGroup->stStorageList));
            _u32Index > 0;
            pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetNext(&(pstStorage->stNode)), _u32Index--);

        /* Checks */
        orxASSERT(pstStorage != orxNULL);

        /* Updates result */
        zResult = orxString_GetFromID(pstStorage->u32StorageID);
      }
    }
  }

  /* Done! */
  return zResult;
}

/** Reloads storage from config
 * @return orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxResource_ReloadStorage()
{
  orxU32    i, u32SectionCounter;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Pushes resource config section */
  orxConfig_PushSection(orxRESOURCE_KZ_CONFIG_SECTION);

  /* For all keys */
  for(i = 0, u32SectionCounter = orxConfig_GetKeyCounter(); i < u32SectionCounter; i++)
  {
    orxRESOURCE_GROUP  *pstGroup = orxNULL;
    const orxSTRING     zGroup;
    orxU32              u32GroupID;
    orxS32              j, jCounter;

    /* Gets group */
    zGroup = orxConfig_GetKey(i);

    /* Gets group ID */
    u32GroupID = orxString_ToCRC(zGroup);

    /* Is not watch list? */
    if(u32GroupID != orxString_ToCRC(orxRESOURCE_KZ_CONFIG_WATCH_LIST))
    {
      /* Finds it */
      for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
          (pstGroup != orxNULL) && (pstGroup->u32ID != u32GroupID);
          pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup));

      /* For all storages in list */
      for(j = 0, jCounter = orxConfig_GetListCounter(zGroup); j < jCounter; j++)
      {
        const orxSTRING zStorage;
        orxBOOL         bAdd = orxTRUE;

        /* Gets storage */
        zStorage = orxConfig_GetListString(zGroup, j);

        /* Did the group exist? */
        if(pstGroup != orxNULL)
        {
          orxRESOURCE_STORAGE  *pstStorage;
          orxU32                u32StorageID;

          /* Gets storage ID */
          u32StorageID = orxString_ToCRC(zStorage);

          /* For all storages in group */
          for(pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetFirst(&(pstGroup->stStorageList));
              pstStorage != orxNULL;
              pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetNext(&(pstStorage->stNode)))
          {
            /* Found? */
            if(pstStorage->u32StorageID == u32StorageID)
            {
              /* Don't add it */
              bAdd = orxFALSE;

              break;
            }
          }
        }

        /* Should add storage? */
        if(bAdd != orxFALSE)
        {
          /* Adds storage to group */
          if(orxResource_AddStorage(zGroup, zStorage, orxFALSE) == orxSTATUS_FAILURE)
          {
            /* Updates */
            eResult = orxSTATUS_FAILURE;
          }
        }
      }
    }

    /* Updates status */
    orxFLAG_SET(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_CONFIG_LOADED, orxRESOURCE_KU32_STATIC_FLAG_NONE);
  }

  /* Pops config section */
  orxConfig_PopSection();

  /* Done! */
  return eResult;
}

/** Gets the location of an *existing* resource for a given group, location gets cached if found
 * @param[in] _zGroup           Concerned resource group
 * @param[in] _zName            Name of the resource to locate
 * @return Location string if found, orxSTRING_EMPTY otherwise
 */
const orxSTRING orxFASTCALL orxResource_Locate(const orxSTRING _zGroup, const orxSTRING _zName)
{
  const orxSTRING zResult = orxNULL;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zGroup != orxNULL);
  orxASSERT(_zName != orxNULL);

  /* Isn't config already loaded? */
  if(!orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_CONFIG_LOADED))
  {
    /* Reloads storage */
    orxResource_ReloadStorage();
  }
  else
  {
    /* Doesn't have watch */
    if(!orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_WATCH_SET))
    {
      /* Is clock module initialized? */
      if(orxModule_IsInitialized(orxMODULE_ID_CLOCK) != orxFALSE)
      {
        orxSTATUS eResult;

        /* Pushes resource config section */
        orxConfig_PushSection(orxRESOURCE_KZ_CONFIG_SECTION);

        /* Has watch list? */
        if(orxConfig_HasValue(orxRESOURCE_KZ_CONFIG_WATCH_LIST) != orxFALSE)
        {
          /* Registers watch callbacks */
          orxClock_Register(orxClock_FindFirst(orx2F(-1.0f), orxCLOCK_TYPE_CORE), orxResource_Watch, orxNULL, orxMODULE_ID_RESOURCE, orxCLOCK_PRIORITY_LOWEST);
        }

        /* Pops config section */
        orxConfig_PopSection();

        /* Registers request notification callback */
        eResult = orxClock_Register(orxClock_FindFirst(orx2F(-1.0f), orxCLOCK_TYPE_CORE), orxResource_NotifyRequest, orxNULL, orxMODULE_ID_RESOURCE, orxCLOCK_PRIORITY_LOWEST);

        /* Checks */
        orxASSERT(eResult != orxSTATUS_FAILURE);

        /* Updates flags */
        orxFLAG_SET(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_WATCH_SET, orxRESOURCE_KU32_STATIC_FLAG_NONE);
      }
    }
  }

  /* Valid? */
  if(*_zGroup != orxCHAR_NULL)
  {
    orxRESOURCE_GROUP  *pstGroup = orxNULL;
    orxU32              u32GroupID;

    /* Gets group ID */
    u32GroupID = orxString_GetID(_zGroup);

    /* Gets group */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        (pstGroup != orxNULL) && (pstGroup->u32ID != u32GroupID);
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup));

    /* Not found? */
    if(pstGroup == orxNULL)
    {
      /* Creates it */
      pstGroup = orxResource_CreateGroup(u32GroupID);
    }

    /* Success? */
    if(pstGroup != orxNULL)
    {
      orxU32            u32Key;
      orxRESOURCE_INFO *pstResourceInfo;

      /* Gets resource info key */
      u32Key = orxString_ToCRC(_zName);

      /* Gets resource info from cache */
      pstResourceInfo = (orxRESOURCE_INFO *)orxHashTable_Get(pstGroup->pstCacheTable, u32Key);

      /* Found? */
      if(pstResourceInfo != orxNULL)
      {
        /* Updates result */
        zResult = pstResourceInfo->zLocation;
      }
      else
      {
        orxRESOURCE_STORAGE *pstStorage;

        /* For all storages in group */
        for(pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetFirst(&(pstGroup->stStorageList));
            (zResult == orxNULL) && (pstStorage != orxNULL);
            pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetNext(&(pstStorage->stNode)))
        {
          orxRESOURCE_TYPE *pstType;

          /* For all registered types */
          for(pstType = (orxRESOURCE_TYPE *)orxLinkList_GetFirst(&(sstResource.stTypeList));
              pstType != orxNULL;
              pstType = (orxRESOURCE_TYPE *)orxLinkList_GetNext(&(pstType->stNode)))
          {
            const orxSTRING zLocation;

            /* Locates resource */
            zLocation = pstType->stInfo.pfnLocate(orxString_GetFromID(pstStorage->u32StorageID), _zName, orxTRUE);

            /* Success? */
            if(zLocation != orxNULL)
            {
              orxRESOURCE_INFO *pstResourceInfo;

              /* Allocates resource info */
              pstResourceInfo = (orxRESOURCE_INFO *)orxBank_Allocate(sstResource.pstResourceInfoBank);

              /* Checks */
              orxASSERT(pstResourceInfo != orxNULL);

              /* Inits it */
              pstResourceInfo->pstTypeInfo  = &(pstType->stInfo);
              pstResourceInfo->s64Time      = orxRESOURCE_KU32_WATCH_TIME_UNINITIALIZED;
              pstResourceInfo->zLocation    = (orxSTRING)orxMemory_Allocate(orxString_GetLength(pstType->stInfo.zTag) + orxString_GetLength(zLocation) + 2, orxMEMORY_TYPE_MAIN);
              orxASSERT(pstResourceInfo->zLocation != orxNULL);
              orxString_Print(pstResourceInfo->zLocation, "%s%c%s", pstType->stInfo.zTag, orxRESOURCE_KC_LOCATION_SEPARATOR, zLocation);

              /* Adds it to cache */
              orxHashTable_Add(pstGroup->pstCacheTable, u32Key, pstResourceInfo);

              /* Updates result */
              zResult = pstResourceInfo->zLocation;

              break;
            }
          }
        }
      }
    }
  }

  /* Done! */
  return zResult;
}

/** Gets the location for a resource (existing or not) in a *specific storage*, for a given group. The location doesn't get cached and thus needs to be copied by the caller before the next call
 * @param[in] _zGroup           Concerned resource group
 * @param[in] _zStorage         Concerned storage, if orxNULL then the highest priority storage will be used
 * @param[in] _zName            Name of the resource
 * @return Location string if found, orxNULL otherwise
 */
const orxSTRING orxFASTCALL orxResource_GetLocation(const orxSTRING _zGroup, const orxSTRING _zStorage, const orxSTRING _zName)
{
  const orxSTRING zResult = orxNULL;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zGroup != orxNULL);
  orxASSERT(_zName != orxNULL);

  /* Isn't config already loaded? */
  if(!orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_CONFIG_LOADED))
  {
    /* Reloads storage */
    orxResource_ReloadStorage();
  }

  /* Valid? */
  if(*_zGroup != orxCHAR_NULL)
  {
    orxRESOURCE_GROUP  *pstGroup = orxNULL;
    orxU32              u32GroupID;

    /* Gets group ID */
    u32GroupID = orxString_GetID(_zGroup);

    /* Gets group */
    for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
        (pstGroup != orxNULL) && (pstGroup->u32ID != u32GroupID);
        pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup));

    /* Not found? */
    if(pstGroup == orxNULL)
    {
      /* Creates it */
      pstGroup = orxResource_CreateGroup(u32GroupID);
    }

    /* Success? */
    if(pstGroup != orxNULL)
    {
      orxRESOURCE_STORAGE  *pstStorage;
      orxU32                u32StorageID;

      /* Gets storage ID */
      u32StorageID = (_zStorage != orxNULL) ? orxString_ToCRC(_zStorage) : 0;

      /* For all storages in group */
      for(pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetFirst(&(pstGroup->stStorageList));
          (zResult == orxNULL) && (pstStorage != orxNULL);
          pstStorage = (orxRESOURCE_STORAGE *)orxLinkList_GetNext(&(pstStorage->stNode)))
      {
        /* Is the requested storage? */
        if((_zStorage == orxNULL)
        || (pstStorage->u32StorageID == u32StorageID))
        {
          orxRESOURCE_TYPE *pstType;

          /* For all registered types */
          for(pstType = (orxRESOURCE_TYPE *)orxLinkList_GetFirst(&(sstResource.stTypeList));
              pstType != orxNULL;
              pstType = (orxRESOURCE_TYPE *)orxLinkList_GetNext(&(pstType->stNode)))
          {
            const orxSTRING zLocation;

            /* Locates resource */
            zLocation = pstType->stInfo.pfnLocate(orxString_GetFromID(pstStorage->u32StorageID), _zName, orxFALSE);

            /* Success? */
            if(zLocation != orxNULL)
            {
              /* Has previous uncached location? */
              if(sstResource.zLastUncachedLocation != orxNULL)
              {
                /* Deletes it */
                orxMemory_Free(sstResource.zLastUncachedLocation);
              }

              /* Creates new location */
              sstResource.zLastUncachedLocation = (orxSTRING)orxMemory_Allocate(orxString_GetLength(pstType->stInfo.zTag) + orxString_GetLength(zLocation) + 2, orxMEMORY_TYPE_MAIN);
              orxASSERT(sstResource.zLastUncachedLocation != orxNULL);
              orxString_Print(sstResource.zLastUncachedLocation, "%s%c%s", pstType->stInfo.zTag, orxRESOURCE_KC_LOCATION_SEPARATOR, zLocation);

              /* Updates result */
              zResult = sstResource.zLastUncachedLocation;

              break;
            }
          }

          break;
        }
      }
    }
  }

  /* Done! */
  return zResult;
}

/** Gets the resource path from a location
 * @param[in] _zLocation        Location of the concerned resource
 * @return Path string if valid, orxSTRING_EMPTY otherwise
 */
const orxSTRING orxFASTCALL orxResource_GetPath(const orxSTRING _zLocation)
{
  const orxSTRING zResult = orxSTRING_EMPTY;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zLocation != orxNULL);

  /* Valid? */
  if(*_zLocation != orxCHAR_NULL)
  {
    orxRESOURCE_TYPE *pstType;

    /* For all registered types */
    for(pstType = (orxRESOURCE_TYPE *)orxLinkList_GetFirst(&(sstResource.stTypeList));
        pstType != orxNULL;
        pstType = (orxRESOURCE_TYPE *)orxLinkList_GetNext(&(pstType->stNode)))
    {
      orxU32 u32TagLength;

      /* Gets tag length */
      u32TagLength = orxString_GetLength(pstType->stInfo.zTag);

      /* Match tag? */
      if(orxString_NICompare(_zLocation, pstType->stInfo.zTag, u32TagLength) == 0)
      {
        /* Valid? */
        if(*(_zLocation + u32TagLength) == orxRESOURCE_KC_LOCATION_SEPARATOR)
        {
          /* Updates result */
          zResult = _zLocation + u32TagLength + 1;
        }

        break;
      }
    }
  }

  /* Done! */
  return zResult;
}

/** Gets the resource type from a location
 * @param[in] _zLocation        Location of the concerned resource
 * @return orxRESOURCE_TYPE_INFO if valid, orxNULL otherwise
 */
const orxRESOURCE_TYPE_INFO *orxFASTCALL orxResource_GetType(const orxSTRING _zLocation)
{
  const orxRESOURCE_TYPE_INFO *pstResult = orxNULL;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zLocation != orxNULL);

  /* Valid? */
  if(*_zLocation != orxCHAR_NULL)
  {
    orxRESOURCE_TYPE *pstType;

    /* For all registered types */
    for(pstType = (orxRESOURCE_TYPE *)orxLinkList_GetFirst(&(sstResource.stTypeList));
        pstType != orxNULL;
        pstType = (orxRESOURCE_TYPE *)orxLinkList_GetNext(&(pstType->stNode)))
    {
      orxU32 u32TagLength;

      /* Gets tag length */
      u32TagLength = orxString_GetLength(pstType->stInfo.zTag);

      /* Match tag? */
      if(orxString_NICompare(_zLocation, pstType->stInfo.zTag, u32TagLength) == 0)
      {
        /* Valid? */
        if(*(_zLocation + u32TagLength) == orxRESOURCE_KC_LOCATION_SEPARATOR)
        {
          /* Updates result */
          pstResult = &(pstType->stInfo);
        }

        break;
      }
    }
  }

  /* Done! */
  return pstResult;
}

/** Gets the time of last modification of a resource
 * @param[in] _zLocation        Location of the concerned resource
 * @return Time of last modification, in seconds since epoch, if found, 0 otherwise
 */
orxS64 orxFASTCALL orxResource_GetTime(const orxSTRING _zLocation)
{
  orxS64 s64Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zLocation != orxNULL);

  /* Valid? */
  if(*_zLocation != orxCHAR_NULL)
  {
    orxRESOURCE_TYPE *pstType;
    orxU32            u32TagLength;

    /* For all registered types */
    for(pstType = (orxRESOURCE_TYPE *)orxLinkList_GetFirst(&(sstResource.stTypeList));
        pstType != orxNULL;
        pstType = (orxRESOURCE_TYPE *)orxLinkList_GetNext(&(pstType->stNode)))
    {
      /* Gets tag length */
      u32TagLength = orxString_GetLength(pstType->stInfo.zTag);

      /* Match tag? */
      if(orxString_NICompare(_zLocation, pstType->stInfo.zTag, u32TagLength) == 0)
      {
        /* Selects it */
        break;
      }
    }

    /* Found? */
    if(pstType != orxNULL)
    {
      /* Supports time? */
      if(pstType->stInfo.pfnGetTime != orxNULL)
      {
        /* Updates result */
        s64Result = pstType->stInfo.pfnGetTime(_zLocation + u32TagLength + 1);
      }
    }
  }

  /* Done! */
  return s64Result;
}

/** Opens the resource at the given location
 * @param[in] _zLocation        Location of the resource to open
 * @param[in] _bEraseMode       If true, the file will be erased if existing or created otherwise, if false, no content will get destroyed when opening
 * @return Handle to the open location, orxHANDLE_UNDEFINED otherwise
 */
orxHANDLE orxFASTCALL orxResource_Open(const orxSTRING _zLocation, orxBOOL _bEraseMode)
{
  orxHANDLE hResult = orxHANDLE_UNDEFINED;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_zLocation != orxNULL);

  /* Valid? */
  if(*_zLocation != orxCHAR_NULL)
  {
    orxRESOURCE_TYPE *pstType;
    orxU32            u32TagLength;

    /* For all registered types */
    for(pstType = (orxRESOURCE_TYPE *)orxLinkList_GetFirst(&(sstResource.stTypeList));
        pstType != orxNULL;
        pstType = (orxRESOURCE_TYPE *)orxLinkList_GetNext(&(pstType->stNode)))
    {
      /* Gets tag length */
      u32TagLength = orxString_GetLength(pstType->stInfo.zTag);

      /* Match tag? */
      if(orxString_NICompare(_zLocation, pstType->stInfo.zTag, u32TagLength) == 0)
      {
        /* Selects it */
        break;
      }
    }

    /* Found? */
    if(pstType != orxNULL)
    {
      orxRESOURCE_OPEN_INFO *pstOpenInfo;

      /* Allocates open info */
      pstOpenInfo = (orxRESOURCE_OPEN_INFO *)orxBank_Allocate(sstResource.pstOpenInfoBank);

      /* Checks */
      orxASSERT(pstOpenInfo != orxNULL);

      /* Inits it */
      pstOpenInfo->pstTypeInfo  = &(pstType->stInfo);
      pstOpenInfo->u32OpCounter = 0;

      /* Opens it */
      pstOpenInfo->hResource = pstType->stInfo.pfnOpen(_zLocation + u32TagLength + 1, _bEraseMode);

      /* Valid? */
      if((pstOpenInfo->hResource != orxHANDLE_UNDEFINED) && (pstOpenInfo->hResource != orxNULL))
      {
        /* Updates result */
        hResult = (orxHANDLE)pstOpenInfo;
      }
      else
      {
        /* Frees open info */
        orxBank_Free(sstResource.pstOpenInfoBank, pstOpenInfo);

        /* Logs message */
        orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Can't open resource <%s> of type <%s>: unable to open the location.", _zLocation);
      }
    }
    else
    {
      /* Logs message */
      orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Can't open resource <%s>: unknown resource type.", _zLocation);
    }
  }

  /* Done! */
  return hResult;
}

/** Closes a resource
 * @param[in] _hResource        Concerned resource
 */
void orxFASTCALL orxResource_Close(orxHANDLE _hResource)
{
  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Valid? */
  if((_hResource != orxHANDLE_UNDEFINED) && (_hResource != orxNULL))
  {
    orxRESOURCE_OPEN_INFO *pstOpenInfo;

    /* Gets open info */
    pstOpenInfo = (orxRESOURCE_OPEN_INFO *)_hResource;

    /* Has pending operations? */
    if(pstOpenInfo->u32OpCounter != 0)
    {
      /* Adds request */
      orxResource_AddRequest(orxRESOURCE_REQUEST_TYPE_CLOSE, 0, orxNULL, orxNULL, orxNULL, pstOpenInfo);
    }
    else
    {
      /* Closes resource */
      pstOpenInfo->pstTypeInfo->pfnClose(pstOpenInfo->hResource);

      /* Frees open info */
      orxBank_Free(sstResource.pstOpenInfoBank, pstOpenInfo);
    }
  }

  /* Done! */
  return;
}

/** Gets the size, in bytes, of a resource
 * @param[in] _hResource        Concerned resource
 * @return Size of the resource, in bytes
 */
orxS64 orxFASTCALL orxResource_GetSize(orxHANDLE _hResource)
{
  orxS64 s64Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Valid? */
  if((_hResource != orxHANDLE_UNDEFINED) && (_hResource != orxNULL))
  {
    orxRESOURCE_OPEN_INFO *pstOpenInfo;

    /* Gets open info */
    pstOpenInfo = (orxRESOURCE_OPEN_INFO *)_hResource;

    /* Checks */
    orxASSERT(pstOpenInfo->u32OpCounter == 0);

    /* Updates result */
    s64Result = pstOpenInfo->pstTypeInfo->pfnGetSize(pstOpenInfo->hResource);
  }

  /* Done! */
  return s64Result;
}

/** Seeks a position in a given resource (moves cursor)
 * @param[in] _hResource        Concerned resource
 * @param[in] _s64Offset        Number of bytes to offset from 'origin'
 * @param[in] _eWhence          Starting point for the offset computation (start, current position or end)
 * @return Absolute cursor position
*/
orxS64 orxFASTCALL orxResource_Seek(orxHANDLE _hResource, orxS64 _s64Offset, orxSEEK_OFFSET_WHENCE _eWhence)
{
  orxS64 s64Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_eWhence < orxSEEK_OFFSET_WHENCE_NUMBER);

  /* Valid? */
  if((_hResource != orxHANDLE_UNDEFINED) && (_hResource != orxNULL))
  {
    orxRESOURCE_OPEN_INFO *pstOpenInfo;

    /* Gets open info */
    pstOpenInfo = (orxRESOURCE_OPEN_INFO *)_hResource;

    /* Checks */
    orxASSERT(pstOpenInfo->u32OpCounter == 0);

    /* Updates result */
    s64Result = pstOpenInfo->pstTypeInfo->pfnSeek(pstOpenInfo->hResource, _s64Offset, _eWhence);
  }

  /* Done! */
  return s64Result;
}

/** Tells the position of the cursor in a given resource
 * @param[in] _hResource        Concerned resource
 * @return Position (offset), in bytes
 */
orxS64 orxFASTCALL orxResource_Tell(orxHANDLE _hResource)
{
  orxS64 s64Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Valid? */
  if((_hResource != orxHANDLE_UNDEFINED) && (_hResource != orxNULL))
  {
    orxRESOURCE_OPEN_INFO *pstOpenInfo;

    /* Gets open info */
    pstOpenInfo = (orxRESOURCE_OPEN_INFO *)_hResource;

    /* Checks */
    orxASSERT(pstOpenInfo->u32OpCounter == 0);

    /* Updates result */
    s64Result = pstOpenInfo->pstTypeInfo->pfnTell(pstOpenInfo->hResource);
  }

  /* Done! */
  return s64Result;
}

/** Reads data from a resource
 * @param[in] _hResource        Concerned resource
 * @param[in] _s64Size          Size to read (in bytes)
 * @param[out] _pBuffer         Buffer that will be filled by the read data
 * @param[in] _pfnCallback      Callback that will get called after asynchronous operation; if orxNULL, operation will be synchronous
 * @param[in] _pContext         Context that will be transmitted to the callback when called
 * @return Size of the read data, in bytes or -1 for successful asynchronous call
 */
orxS64 orxFASTCALL orxResource_Read(orxHANDLE _hResource, orxS64 _s64Size, void *_pBuffer, orxRESOURCE_OP_FUNCTION _pfnCallback, void *_pContext)
{
  orxS64 s64Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_pBuffer != orxNULL);

  /* Valid? */
  if((_hResource != orxHANDLE_UNDEFINED) && (_hResource != orxNULL))
  {
    orxRESOURCE_OPEN_INFO *pstOpenInfo;

    /* Gets open info */
    pstOpenInfo = (orxRESOURCE_OPEN_INFO *)_hResource;

    /* Has a callback (asynchronous call) */
    if(_pfnCallback != orxNULL)
    {
      /* Adds request */
      orxResource_AddRequest(orxRESOURCE_REQUEST_TYPE_READ, _s64Size, _pBuffer, _pfnCallback, _pContext, pstOpenInfo);

      /* Updates result */
      s64Result = -1;
    }
    else
    {
      /* Updates result */
      s64Result = pstOpenInfo->pstTypeInfo->pfnRead(pstOpenInfo->hResource, _s64Size, _pBuffer);
    }
  }

  /* Done! */
  return s64Result;
}

/** Writes data to a resource
 * @param[in] _hResource        Concerned resource
 * @param[in] _s64Size          Size to write (in bytes)
 * @param[out] _pBuffer         Buffer that will be written
 * @param[in] _pfnCallback      Callback that will get called after asynchronous operation; if orxNULL, operation will be synchronous
 * @param[in] _pContext         Context that will be transmitted to the callback when called
 * @return Size of the written data, in bytes, 0 if nothing could be written/no write support for this resource type or -1 for successful asynchronous call
 */
orxS64 orxFASTCALL orxResource_Write(orxHANDLE _hResource, orxS64 _s64Size, const void *_pBuffer, orxRESOURCE_OP_FUNCTION _pfnCallback, void *_pContext)
{
  orxS64 s64Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_pBuffer != orxNULL);

  /* Valid? */
  if((_hResource != orxHANDLE_UNDEFINED) && (_hResource != orxNULL))
  {
    orxRESOURCE_OPEN_INFO *pstOpenInfo;

    /* Gets open info */
    pstOpenInfo = (orxRESOURCE_OPEN_INFO *)_hResource;

    /* Supports writing? */
    if(pstOpenInfo->pstTypeInfo->pfnWrite != orxNULL)
    {
      /* Has a callback (asynchronous call) */
      if(_pfnCallback != orxNULL)
      {
        /* Adds request */
        orxResource_AddRequest(orxRESOURCE_REQUEST_TYPE_WRITE, _s64Size, (void *)_pBuffer, _pfnCallback, _pContext, pstOpenInfo);

        /* Updates result */
        s64Result = -1;
      }
      else
      {
        /* Updates result */
        s64Result = pstOpenInfo->pstTypeInfo->pfnWrite(pstOpenInfo->hResource, _s64Size, _pBuffer);
      }
    }
  }

  /* Done! */
  return s64Result;
}

/** Gets pending operation counter for a given resource
 * @param[in] _hResource        Concerned resource
 * @return Number of pending asynchronous operations for that resource
 */
orxU32 orxFASTCALL orxResource_GetPendingOpCounter(const orxHANDLE _hResource)
{
  orxU32 u32Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Valid? */
  if((_hResource != orxHANDLE_UNDEFINED) && (_hResource != orxNULL))
  {
    orxRESOURCE_OPEN_INFO *pstOpenInfo;

    /* Gets open info */
    pstOpenInfo = (orxRESOURCE_OPEN_INFO *)_hResource;

    /* Updates result */
    u32Result = pstOpenInfo->u32OpCounter;
  }

  /* Done! */
  return u32Result;
}

/** Gets total pending operation counter
 * @return Number of total pending asynchronous operations
 */
orxU32 orxFASTCALL orxResource_GetTotalPendingOpCounter()
{
  orxU32 u32InIndex, u32ProcessIndex;
  orxU32 u32Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Gets indices */
  u32InIndex      = sstResource.u32RequestInIndex;
  u32ProcessIndex = sstResource.u32RequestProcessIndex;

  /* Update result */
  u32Result = (u32InIndex >= u32ProcessIndex) ? u32InIndex - u32ProcessIndex : u32InIndex + (orxRESOURCE_KU32_REQUEST_LIST_SIZE - u32ProcessIndex);

  /* Has pending operations? */
  if(u32Result != 0)
  {
    /* Main thread? */
    if(orxThread_GetCurrent() == orxTHREAD_KU32_MAIN_THREAD_ID)
    {
      /* Pumps some request notifications in case caller is waiting in a closed loop */
      orxResource_NotifyRequest(orxNULL, orxNULL);
    }
  }

  /* Done! */
  return u32Result;
}

/** Registers a new resource type
 * @param[in] _pstInfo          Info describing the new resource type and how to handle it
 * @return orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxResource_RegisterType(const orxRESOURCE_TYPE_INFO *_pstInfo)
{
  orxSTATUS eResult = orxSTATUS_FAILURE;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));
  orxASSERT(_pstInfo != orxNULL);

  /* Valid? */
  if((_pstInfo->zTag != orxNULL)
  && (*(_pstInfo->zTag) != orxCHAR_NULL)
  && (_pstInfo->pfnLocate != orxNULL)
  && (_pstInfo->pfnOpen != orxNULL)
  && (_pstInfo->pfnClose != orxNULL)
  && (_pstInfo->pfnGetSize != orxNULL)
  && (_pstInfo->pfnSeek != orxNULL)
  && (_pstInfo->pfnTell != orxNULL)
  && (_pstInfo->pfnRead != orxNULL))
  {
    orxRESOURCE_TYPE *pstType;

    /* For all registered types */
    for(pstType = (orxRESOURCE_TYPE *)orxLinkList_GetFirst(&(sstResource.stTypeList));
        (pstType != orxNULL) && (orxString_ICompare(pstType->stInfo.zTag, _pstInfo->zTag) != 0);
        pstType = (orxRESOURCE_TYPE *)orxLinkList_GetNext(&(pstType->stNode)));

    /* Not already registered? */
    if(pstType == orxNULL)
    {
      /* Allocates type */
      pstType = (orxRESOURCE_TYPE *)orxBank_Allocate(sstResource.pstTypeBank);

      /* Checks */
      orxASSERT(pstType != orxNULL);

      /* Inits it */
      orxMemory_Zero(&(pstType->stNode), sizeof(orxLINKLIST_NODE));
      orxMemory_Copy(&(pstType->stInfo), _pstInfo, sizeof(orxRESOURCE_TYPE_INFO));
      pstType->stInfo.zTag = orxString_GetFromID(orxString_GetID(_pstInfo->zTag));

      /* Checks */
      orxASSERT(pstType->stInfo.zTag != orxNULL);

      /* Adds it first */
      orxLinkList_AddStart(&(sstResource.stTypeList), &(pstType->stNode));

      /* Updates result */
      eResult = orxSTATUS_SUCCESS;
    }
    else
    {
      /* Logs message */
      orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Can't register resource type with tag <%s>: tag is already used by a registered type.", _pstInfo->zTag);
    }
  }
  else
  {
    /* Logs message */
    orxDEBUG_PRINT(orxDEBUG_LEVEL_SYSTEM, "Can't register resource type with tag <%s>: incomplete info.", _pstInfo->zTag);
  }

  /* Done! */
  return eResult;
}

/** Gets number of registered resource types
 * @return Number of registered resource types
 */
orxU32 orxFASTCALL orxResource_GetTypeCounter(const orxSTRING _zGroup)
{
  orxU32 u32Result = 0;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Updates result */
  u32Result = orxBank_GetCounter(sstResource.pstTypeBank);

  /* Done! */
  return u32Result;
}

/** Gets registered type info at given index
 * @param[in] _u32Index         Index of storage
 * @return Type tag string if index is valid, orxNULL otherwise
 */
const orxSTRING orxFASTCALL orxResource_GetTypeTag(orxU32 _u32Index)
{
  const orxSTRING zResult = orxNULL;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* Valid index? */
  if(_u32Index < orxBank_GetCounter(sstResource.pstTypeBank))
  {
    orxRESOURCE_TYPE *pstType;

    /* Finds requested group */
    for(pstType = (orxRESOURCE_TYPE *)orxBank_GetNext(sstResource.pstTypeBank, orxNULL);
        _u32Index > 0;
        pstType = (orxRESOURCE_TYPE *)orxBank_GetNext(sstResource.pstTypeBank, pstType), _u32Index--);

    /* Checks */
    orxASSERT(pstType != orxNULL);

    /* Updates result */
    zResult = pstType->stInfo.zTag;
  }

  /* Done! */
  return zResult;
}

/** Clears cache
 * @return orxSTATUS_SUCCESS / orxSTATUS_FAILURE
 */
orxSTATUS orxFASTCALL orxResource_ClearCache()
{
  orxRESOURCE_GROUP  *pstGroup;
  orxSTATUS           eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(orxFLAG_TEST(sstResource.u32Flags, orxRESOURCE_KU32_STATIC_FLAG_READY));

  /* For all groups */
  for(pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, orxNULL);
      pstGroup != orxNULL;
      pstGroup = (orxRESOURCE_GROUP *)orxBank_GetNext(sstResource.pstGroupBank, pstGroup))
  {
    orxHANDLE         hIterator;
    orxU32            u32Key;
    orxRESOURCE_INFO *pstResourceInfo;

    /* For all cached resources */
    for(hIterator = orxHashTable_GetNext(pstGroup->pstCacheTable, orxHANDLE_UNDEFINED, &u32Key, (void **)&pstResourceInfo);
        hIterator != orxHANDLE_UNDEFINED;
        hIterator = orxHashTable_GetNext(pstGroup->pstCacheTable, hIterator, &u32Key, (void **)&pstResourceInfo))
    {
      /* Deletes its location */
      orxMemory_Free(pstResourceInfo->zLocation);
    }

    /* Clears cache table */
    orxHashTable_Clear(pstGroup->pstCacheTable);
  }

  /* Clears info bank */
  orxBank_Clear(sstResource.pstResourceInfoBank);

  /* Done! */
  return eResult;
}
