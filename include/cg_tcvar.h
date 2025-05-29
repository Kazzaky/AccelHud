#ifndef CG_TCVAR_H
#define CG_TCVAR_H

// helper to notice the cvar change

#include "q_shared.h"

typedef struct trackTableItem_
{
  vmCvar_t  *vmCvar;
  char      *name; // redundant but we need it to work with quake natural cvars
  int32_t   track; // last count track
  void      (*callback)(struct trackTableItem_ const *, void *);
  void      *data; // are passed to callback
} trackTableItem;

#define TRACK_CALLBACK_NAME(n) _tcb_##n
// since the following macro is used for special case handling, the data are mostly unused thats why the name is "_"
#define TRACK_CALLBACK(n) void TRACK_CALLBACK_NAME(n)(trackTableItem const *item, void *_)

void init_tcvars(trackTableItem *cvars, size_t size);
void update_tcvars(trackTableItem *cvars, size_t size);

#endif // CG_TCVAR_H
