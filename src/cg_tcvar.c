#include "cg_tcvar.h"
#include "cg_local.h"


void init_tcvars(trackTableItem const* cvars, size_t size)
{
  for (uint32_t i = 0; i < size; ++i)
  {
    trap_Cvar_Register(cvars[i].vmTCvar.cvar, cvars[i].cvarName, cvars[i].defaultString, cvars[i].cvarFlags);
  }
}

void update_tcvars(trackTableItem const* cvars, size_t size)
{
  for (uint32_t i = 0; i < size; ++i)
  {
    trap_Cvar_Update(cvars[i].vmTCvar.cvar);
  }
}
