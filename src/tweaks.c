/*
 * tweaks.c -- [tweaks] (include/tweaks.h, docs/MODS.md §4).
 */
#include "tweaks.h"

static uint32_t s_mask;

void Tweaks_Set(uint32_t mask) { s_mask = mask; }
uint32_t Tweaks_Mask(void) { return s_mask; }

#if SWOS_TWEAKS
#ifdef SWOS_SDL_BUILD
#include "replay_test.h"    /* ReplayTest_Running */
#include "srp_resim.h"      /* SrpResim_Armed */
#include "rollback_rig.h"   /* Rollback_Enabled */
extern int32_t NetplayMatch_Enabled(void);
extern int32_t CareerAutopilot_Active(void);
#endif

int32_t Tweaks_On(uint32_t tweak)
{
    if (!(s_mask & tweak))
        return 0;
#ifdef SWOS_SDL_BUILD
    /* Online the other player would play the original; the rigs prove the original. */
    if (NetplayMatch_Enabled() || ReplayTest_Running() || SrpResim_Armed() || Rollback_Enabled() ||
        CareerAutopilot_Active())
        return 0;
#endif
    return 1;
}
#endif
