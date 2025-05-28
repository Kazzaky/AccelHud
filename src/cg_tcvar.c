#include "cg_tcvar.h"


void init_tcvars(trackTableItem *cvars, size_t size)
{
  for (size_t i = 0; i < size; ++i)
  {
    // save the last modificationCount
    cvars[i].track = cvars[i].vmCvar->modificationCount;

    // call callback
    cvars[i].callback(&cvars[i], cvars[i].data);
  }
}

void update_tcvars(trackTableItem *cvars, size_t size)
{
  for (size_t i = 0; i < size; ++i)
  {
    // check modificationCount
    if(cvars[i].track != cvars[i].vmCvar->modificationCount)
    {
      cvars[i].track = cvars[i].vmCvar->modificationCount;
      
      // call callback
      cvars[i].callback(&cvars[i], cvars[i].data);
    }
  }
}
