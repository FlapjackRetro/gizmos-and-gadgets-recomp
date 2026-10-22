/*
 * example_mod.c - a minimal mod: logs every game event the API offers.
 *
 * Build: it is built with the game (CMake target gg_example_mod) as
 * example_mod.dll / example_mod.so. Copy it into <game dir>/mods/ or run
 *     gizmos --mod path/to/example_mod.dll
 *
 * With EXAMPLE_MOD_CHEATS=1 in the environment it also shows the ways a mod can
 * change the game:
 *   - finished buildings can be entered again (GG_EV_BUILDING_CLICK -> GG_ALLOW)
 *   - every door you open gives 5 bananas (an action from an event)
 *   - every 30 s, a deferred action adds 1 banana while you are in a building
 *     (GG_EV_FRAME schedules it; it runs at the next safe point)
 */
#include <stdio.h>
#include <stdlib.h>
#include "gg_mod.h"

static const GGApi *api;
static int cheats;
static const char *bname[] = { "Automotive", "Aircraft", "Alt-Energy" };

static const char *building(int b) { return b >= 0 && b < 3 ? bname[b] : "?"; }

static int on_event(void *user, GGEvent *e)
{
    (void)user;
    switch (e->type) {
    case GG_EV_BUILDING_CLICK:
        api->log("building clicked: %s", building(e->building));
        return cheats ? GG_ALLOW : GG_CONTINUE;
    case GG_EV_BUILDING_SELECTED:
        api->log("entering %s", building(e->building));
        break;
    case GG_EV_LEVEL_ENTER:
        api->log("loading %s level %d (%s)", building(e->building), e->level + 1,
                 e->restore ? "saved warehouse" : "new warehouse");
        break;
    case GG_EV_LEVEL_READY:
        api->log("level ready: %d parts needed, %d bananas",
                 api->ds_read16(GG_DS_PARTS_NEEDED), api->ds_read16(GG_DS_BANANAS));
        break;
    case GG_EV_BOX_OPEN:
        if (e->contents == GG_BOX_EMPTY) api->log("box %d in room %d: empty", e->object, e->room);
        else api->log("box %d in room %d: part %d", e->object, e->room, e->contents);
        break;
    case GG_EV_PUZZLE_RESULT:
        api->log("puzzle type %d variant %d in room %d: %s", e->puzzle_type, e->puzzle_variant,
                 e->room, e->result ? "solved" : "failed");
        break;
    case GG_EV_DOOR_OPEN:
        api->log("door %d opened: room %d -> room %d", e->door, e->room, e->dest);
        if (cheats) api->add_bananas(5);
        break;
    case GG_EV_RACE_START:
        api->log("race: %s level %d", building(e->building), e->level + 1);
        break;
    case GG_EV_RACE_END:
        api->log("race over: %s", e->aborted ? "aborted" : e->won ? "won" : "lost");
        break;
    }
    return GG_CONTINUE;
}

static void give_banana(void *user)
{
    (void)user;
    if (api->ds_read16(GG_DS_BUILDING) < 3) api->add_bananas(1);
}

static int frames;
static int on_frame(void *user, GGEvent *e)
{
    (void)user; (void)e;
    if (cheats && ++frames % (60 * 30) == 0) api->defer(give_banana, NULL);
    return GG_CONTINUE;
}

/* A raw site hook: the registers just before an instruction. */
static int on_selected_site(void *user, uint32_t addr, GGRegs *r)
{
    (void)user;
    api->log("site %05X: ax=%04X (the UI event code of the click)", addr, r->ax);
    return 0;                                  /* 1 would skip the instruction */
}

GG_MOD_EXPORT int gg_mod_init(const GGApi *a)
{
    if (a->version < GG_API_VERSION) return 1;
    api = a;
    const char *c = getenv("EXAMPLE_MOD_CHEATS");
    cheats = c && *c == '1';
    for (int t = GG_EV_BUILDING_CLICK; t < GG_EV_FRAME; t++) api->on_event(t, on_event, NULL);
    api->on_event(GG_EV_FRAME, on_frame, NULL);
    api->on_site(api->site_addr("building_selected"), on_selected_site, NULL);
    api->log("example mod ready%s (game dir %s)", cheats ? ", cheats on" : "", api->game_dir());
    return 0;
}
