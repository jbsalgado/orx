#include "io/interaction.h"

#include "core/orxClock.h"


#define INTERACTION_KS32_GET_TIMER    50

//orxSTATIC orxVOID interaction_get()
//{
//
//  return;
//}

orxU32 interaction_init()
{
//  return ((clock_pfn_function_add(interaction_get, INTERACTION_KS32_GET_TIMER) == orxFALSE) ? orxSTATUS_SUCCESS : orxSTATUS_FAILED);
 return 0;
}

orxVOID interaction_exit()
{
//  clock_pfn_function_remove(interaction_get, INTERACTION_KS32_GET_TIMER);

  return;
}
