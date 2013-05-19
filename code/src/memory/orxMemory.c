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
 * @file orxMemory.c
 * @date 02/04/2005
 * @author bestel@arcallians.org
 *
 */


#include "memory/orxMemory.h"
#include "debug/orxDebug.h"


#define USE_DL_PREFIX
#define USE_BUILTIN_FFS 1
#include "malloc.c"


#define orxMEMORY_KU32_STATIC_FLAG_NONE         0x00000000  /**< No flags have been set */
#define orxMEMORY_KU32_STATIC_FLAG_READY        0x00000001  /**< The module has been initialized */

#define orxMEMORY_KU32_DEFAULT_CACHE_LINE_SIZE  8


/***************************************************************************
 * Structure declaration                                                   *
 ***************************************************************************/

typedef struct __orxMEMORY_TRACKER_t
{
  orxU32 u32Counter, u32PeakCounter;
  orxU32 u32Size, u32PeakSize;
  orxU32 u32OperationCounter;

} orxMEMORY_TRACKER;

typedef struct __orxMEMORY_STATIC_t
{
#ifdef __orxPROFILER__

  orxMEMORY_TRACKER astMemoryTrackerList[orxMEMORY_TYPE_NUMBER];

#endif /* __orxPROFILER__ */

  orxU32 u32Flags;   /**< Flags set by the memory module */

} orxMEMORY_STATIC;


/***************************************************************************
 * Module global variable                                                  *
 ***************************************************************************/

static orxMEMORY_STATIC sstMemory;


/***************************************************************************
 * Private functions                                                       *
 ***************************************************************************/

#if defined(__orxWINDOWS__)

orxINLINE orxU32 orxMemory_CacheLineSize()
{
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION *astProcessorInfoList;
  orxU32                                u32InfoListSize = 0, u32Result = orxMEMORY_KU32_DEFAULT_CACHE_LINE_SIZE, i, iNumber;

  /* Requests total size of processors info */
  GetLogicalProcessorInformation(0, &u32InfoListSize);

  /* Allocates info list */
  astProcessorInfoList = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)orxMemory_Allocate(u32InfoListSize, orxMEMORY_TYPE_TEMP);

  /* Gets processors info */
  GetLogicalProcessorInformation(astProcessorInfoList, &u32InfoListSize);

  /* For all processor info */
  for(i = 0, iNumber = u32InfoListSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
      i < iNumber;
      i++)
  {
    /* Found first level cache info? */
    if((astProcessorInfoList[i].Relationship == RelationCache)
    && (astProcessorInfoList[i].Cache.Level == 1))
    {
      /* Updates result */
      u32Result = astProcessorInfoList[i].Cache.LineSize;

      break;
    }
  }

  /* Frees info list */
  orxMemory_Free(astProcessorInfoList);

  /* Done! */
  return u32Result;
}

#elif defined(__orxMAC__) || defined(__orxIOS__)

#include <sys/sysctl.h>

orxINLINE orxU32 orxMemory_CacheLineSize()
{
  size_t stLineSize = 0, stSizeOfLineSize;

  /* Size of variable */
  stSizeOfLineSize = sizeof(stLineSize);

  /* Gets cache line size */
  sysctlbyname("hw.cachelinesize", &st2LineSize, &stSizeOfLineSize, 0, 0);

  /* Done! */
  return (orxU32)stLineSize;
}

#elif defined(__orxLINUX__)

#include <unistd.h>

orxINLINE orxU32 orxMemory_CacheLineSize()
{
  /* Done! */
  return (orxU32)sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
}

#else

orxINLINE orxU32 orxMemory_CacheLineSize()
{
  /* Done! */
  return orxMEMORY_KU32_DEFAULT_CACHE_LINE_SIZE;
}

#endif


/***************************************************************************
 * Public functions                                                        *
 ***************************************************************************/

/** Memory module setup
 */
void orxFASTCALL orxMemory_Setup()
{
  /* Adds module dependencies */

  return;
}

/** Initializes memory allocation module
 */
orxSTATUS orxFASTCALL orxMemory_Init()
{
  orxSTATUS eResult = orxSTATUS_FAILURE;

  /* Not already Initialized? */
  if(!(sstMemory.u32Flags & orxMEMORY_KU32_STATIC_FLAG_READY))
  {
    /* Cleans static controller */
    orxMemory_Zero(&sstMemory, sizeof(orxMEMORY_STATIC));

    /* Module initialized */
    sstMemory.u32Flags = orxMEMORY_KU32_STATIC_FLAG_READY;

    /* Success */
    eResult = orxSTATUS_SUCCESS;
  }
  else
  {
    /* Logs message */
    orxDEBUG_PRINT(orxDEBUG_LEVEL_MEMORY, "Tried to initialize memory module when it was already initialized.");

    /* Already initialized */
    eResult = orxSTATUS_SUCCESS;
  }

  /* Done */
  return eResult;
}

/** Exits from the memory module
 */
void orxFASTCALL orxMemory_Exit()
{
  /* Module initialized ? */
  if((sstMemory.u32Flags & orxMEMORY_KU32_STATIC_FLAG_READY) == orxMEMORY_KU32_STATIC_FLAG_READY)
  {
    /* Module uninitialized */
    sstMemory.u32Flags = orxMEMORY_KU32_STATIC_FLAG_NONE;
  }

  return;
}

/** Allocates a portion of memory in the system and returns a pointer on it
 * @param[in] _u32Size    size of the memory to allocate
 * @param[in] _eMemType   Memory zone where datas will be allocated
 * @return  returns a pointer on the memory allocated, or orxNULL if an error has occured
 */
void *orxFASTCALL orxMemory_Allocate(orxU32 _u32Size, orxMEMORY_TYPE _eMemType)
{
  void *pResult;

  /* Module initialized ? */
  orxASSERT((sstMemory.u32Flags & orxMEMORY_KU32_STATIC_FLAG_READY) == orxMEMORY_KU32_STATIC_FLAG_READY);

  /* Valid parameters ? */
  orxASSERT(_eMemType < orxMEMORY_TYPE_NUMBER);

#ifdef __orxPROFILER__

  /* Allocates memory */
  pResult = dlmalloc((size_t)(_u32Size + sizeof(orxMEMORY_TYPE)));

  /* Success? */
  if(pResult != NULL)
  {
    size_t uMemoryChunkSize;

    /* Gets memory chunk size */
    uMemoryChunkSize = dlmalloc_usable_size(pResult);

    /* Tags memory chunk */
    *(orxMEMORY_TYPE *)((orxU8 *)pResult + uMemoryChunkSize - sizeof(orxMEMORY_TYPE)) = _eMemType;

    /* Updates memory tracker */
    sstMemory.astMemoryTrackerList[_eMemType].u32Size += uMemoryChunkSize - sizeof(orxMEMORY_TYPE);
    sstMemory.astMemoryTrackerList[_eMemType].u32Counter++;
    if(sstMemory.astMemoryTrackerList[_eMemType].u32Counter > sstMemory.astMemoryTrackerList[_eMemType].u32PeakCounter)
    {
      sstMemory.astMemoryTrackerList[_eMemType].u32PeakCounter = sstMemory.astMemoryTrackerList[_eMemType].u32Counter;
    }
    if(sstMemory.astMemoryTrackerList[_eMemType].u32Size > sstMemory.astMemoryTrackerList[_eMemType].u32PeakSize)
    {
      sstMemory.astMemoryTrackerList[_eMemType].u32PeakSize = sstMemory.astMemoryTrackerList[_eMemType].u32Size;
    }
    sstMemory.astMemoryTrackerList[_eMemType].u32OperationCounter++;
  }

#else /* __orxPROFILER__ */

  /* Allocates memory */
  pResult = dlmalloc((size_t)_u32Size);

#endif /* __orxPROFILER__ */

  /* Done! */
  return pResult;
}

/** Frees a portion of memory allocated with orxMemory_Allocateate
 * @param[in] _pMem       Pointer on the memory allocated by orx
 */
void orxFASTCALL orxMemory_Free(void *_pMem)
{
  /* Module initialized ? */
  orxASSERT((sstMemory.u32Flags & orxMEMORY_KU32_STATIC_FLAG_READY) == orxMEMORY_KU32_STATIC_FLAG_READY);

#ifdef __orxPROFILER__

  /* Valid? */
  if(_pMem != NULL)
  {
    orxMEMORY_TYPE eMemType;
    size_t         uMemoryChunkSize;

    /* Gets memory chunk size */
    uMemoryChunkSize = dlmalloc_usable_size(_pMem);

    /* Gets memory type from memory chunk tag */
    eMemType = *(orxMEMORY_TYPE *)((orxU8 *)_pMem + uMemoryChunkSize - sizeof(orxMEMORY_TYPE));

    /* Updates memory tracker */
    sstMemory.astMemoryTrackerList[eMemType].u32Size -= uMemoryChunkSize - sizeof(orxMEMORY_TYPE);
    sstMemory.astMemoryTrackerList[eMemType].u32Counter--;
    sstMemory.astMemoryTrackerList[eMemType].u32OperationCounter++;
  }

#endif /* __orxPROFILER__ */

  /* System call to free memory */
  dlfree(_pMem);

  return;
}

/** Copies a portion of memory into another one
 * @param[out] _pDest     Destination pointer
 * @param[in] _pSrc       Pointer of memory from where data are read
 * @param[in] _u32Size    Size of data
 * @return returns a pointer on _pDest
 * @note if _pSrc and _pDest overlap, use orxMemory_Move instead
 */
void *orxFASTCALL orxMemory_Copy(void *_pDest, const void *_pSrc, orxU32 _u32Size)
{
  return((void *)memcpy(_pDest, _pSrc, (size_t)_u32Size));
}

/** Moves a portion of memory into another one
 * @param[out] _pDest     Destination pointer
 * @param[in] _pSrc       Pointer of memory from where data are read
 * @param[in] _u32Size    Size of data
 * @return returns a pointer on _pDest
 */
void *orxFASTCALL orxMemory_Move(void *_pDest, void *_pSrc, orxU32 _u32Size)
{
  return((void *)memmove(_pDest, _pSrc, (size_t)_u32Size));
}

/** Compares two portions of memory
 * @param[in] _pMem1      First potion to test
 * @param[in] _pMem2      Second portion to test
 * @param[in] _u32Size    Size of data to test
 * @return returns a value less, equals or greater that 0 if _pMem1 is respectively smaller, equal or greater than _pMem2
 */
orxU32 orxFASTCALL orxMemory_Compare(const void *_pMem1, const void *_pMem2, orxU32 _u32Size)
{
  return((orxU32)memcmp(_pMem1, _pMem2, (size_t)_u32Size));
}

/** Fills a portion of memory with _u32Data
 * @param[out] _pDest     Destination pointer
 * @param[in] _u8Data     Values of the data that will fill the memory
 * @param[in] _u32Size    Size of data
 * @return returns a pointer on _pDest
 */
void *orxFASTCALL orxMemory_Set(void *_pDest, orxU8 _u8Data, orxU32 _u32Size)
{
  return((void *)memset(_pDest, _u8Data, (size_t)_u32Size));
}

/** Fills a portion of memory with zeroes
 * @param[out] _pDest     Destination pointer
 * @param[in] _u32Size    Size of data
 * @return returns a pointer on _pDest
 */
void *orxFASTCALL orxMemory_Zero(void *_pDest, orxU32 _u32Size)
{
  return((void *)memset(_pDest, 0, (size_t)_u32Size));
}

/** Reallocates a portion of memory if the already allocated memory is not large enough
 * @param[in] _pMem    Memory to reallocate
 * @param[in] _u32Size Requested size
 * @return Pointer to reallocated memory
 */
void *orxFASTCALL orxMemory_Reallocate(void *_pMem, orxU32 _u32Size)
{
  return((void *)dlrealloc(_pMem, (size_t)_u32Size));
}

/** Gets L1 data cache line size
 * @return Cache line size
 */
orxU32 orxFASTCALL orxMemory_GetCacheLineSize()
{
  orxU32 u32Result;

  /* Updates result */
  u32Result = orxMemory_CacheLineSize();

  /* Done! */
  return u32Result;
}

#ifdef __orxPROFILER__

/** Gets memory usage for a given type
 * @param[in] _eMemType         Concerned memory type
 * @param[out] _pu32Counter     Current memory allocation counter
 * @param[out] _pu32PeakCounter Peak memory allocation counter
 * @param[out] _pu32Size        Current memory allocation size
 * @param[out] _pu32PeakSize    Peak memory allocation size
 * @param[out] _pu32OperationCounter  Total number of memory operations (malloc/free)
 * @return The pointer reallocated.
 */
orxSTATUS orxFASTCALL orxMemory_GetUsage(orxMEMORY_TYPE _eMemType, orxU32 *_pu32Counter, orxU32 *_pu32PeakCounter, orxU32 *_pu32Size, orxU32 *_pu32PeakSize, orxU32 *_pu32OperationCounter)
{
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Valid? */
  if(_eMemType < orxMEMORY_TYPE_NUMBER)
  {
    /* Asked for current counter? */
    if(_pu32Counter != orxNULL)
    {
      /* Updates it */
      *_pu32Counter = sstMemory.astMemoryTrackerList[_eMemType].u32Counter;
    }

    /* Asked for peak counter? */
    if(_pu32PeakCounter != orxNULL)
    {
      /* Updates it */
      *_pu32PeakCounter = sstMemory.astMemoryTrackerList[_eMemType].u32PeakCounter;
    }

    /* Asked for current size? */
    if(_pu32Size != orxNULL)
    {
      /* Updates it */
      *_pu32Size = sstMemory.astMemoryTrackerList[_eMemType].u32Size;
    }

    /* Asked for peak size? */
    if(_pu32PeakSize != orxNULL)
    {
      /* Updates it */
      *_pu32PeakSize = sstMemory.astMemoryTrackerList[_eMemType].u32PeakSize;
    }

    /* Asked for total operation counter? */
    if(_pu32OperationCounter != orxNULL)
    {
      /* Updates it */
      *_pu32OperationCounter = sstMemory.astMemoryTrackerList[_eMemType].u32OperationCounter;
    }
  }
  else
  {
    /* Updates result */
    eResult = orxSTATUS_FAILURE;
  }

  /* Done! */
  return eResult;
}

#endif /* __orxPROFILER__ */
