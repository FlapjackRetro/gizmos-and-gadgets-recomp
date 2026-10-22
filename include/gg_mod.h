/* gg_mod.h -- the mod API of the Gizmos & Gadgets recompilation.
 *
 * A mod is a shared library (gizmos-mod.dll / .so / .dylib) that exports
 *
 *     GG_MOD_EXPORT int gg_mod_init(const GGApi *api);
 *
 * gizmos loads every library in <game dir>/mods/ and any given with --mod <path>,
 * calls gg_mod_init once before the game starts, and the mod registers the hooks
 * it wants. Return 0 on success; anything else unloads the mod.
 *
 * This header is the whole contract: it contains no game code or data, is MIT
 * licensed like the rest of the recomp, and is all a mod needs to build against.
 * Check api->version >= GG_API_VERSION before using anything newer than you know.
 *
 * Threading: everything runs on the game's one thread. Hooks run in the middle of
 * the game's own code, so keep them short.
 */
#ifndef GG_MOD_H
#define GG_MOD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GG_API_VERSION 1

#if defined(_WIN32)
#  define GG_MOD_EXPORT __declspec(dllexport)
#else
#  define GG_MOD_EXPORT __attribute__((visibility("default")))
#endif

/* ---- Events ---------------------------------------------------------------
 *
 * Fields marked (in/out) may be changed by the callback and the game uses the new
 * value. The callback's return value is GG_CONTINUE unless the event says
 * otherwise. Buildings: 0 = Automotive, 1 = Aircraft, 2 = Alternative Energy.
 * Levels are 0-based (level 0 = the game's "Level-1").
 */
typedef enum GGEventType {
    /* Clicked a building on the Technology Center screen. The game refuses a
     * building whose five races are all won. Return GG_ALLOW to let the player in
     * anyway, GG_REFUSE to refuse, GG_CONTINUE for the game's own rule.
     * building */
    GG_EV_BUILDING_CLICK = 1,
    /* A building was chosen and is about to be entered. building */
    GG_EV_BUILDING_SELECTED,
    /* About to load a level. building, level (in/out), restore (in/out: 1 = bring
     * back the saved warehouse of this building, 0 = generate a fresh one). The
     * game picks level = races won in this building; a mod may pick another. */
    GG_EV_LEVEL_ENTER,
    /* The warehouse is built or restored; parts and boxes exist. building, level */
    GG_EV_LEVEL_READY,
    /* Once per pass of the warehouse loop (many times a second while the player
     * is in the warehouse rooms). A safe point for actions. building, level */
    GG_EV_WAREHOUSE_TICK,
    /* The same for the assembly room and the blueprint. building, level */
    GG_EV_ASSEMBLY_TICK,
    /* The player opens a box. room, object, contents (in/out: part id, or
     * GG_BOX_EMPTY for +5 bananas). Return GG_SUPPRESS to handle it yourself: the
     * box is removed and the game gives nothing. */
    GG_EV_BOX_OPEN,
    /* A door puzzle finished. room, door, puzzle_type (0-7), puzzle_variant,
     * result (in/out: 0 = failed and the door stays shut). */
    GG_EV_PUZZLE_RESULT,
    /* A locked door opens. room, door, dest (room on the other side). */
    GG_EV_DOOR_OPEN,
    /* A race begins. building, level */
    GG_EV_RACE_START,
    /* A race ended. building, level, aborted, won (in/out). The game counts the
     * win (race index, total races) only if won is 1 after the callbacks. */
    GG_EV_RACE_END,
    /* About 60 times a second, from the presentation timer -- also outside the
     * game's code paths (on menus, during loads). Memory reads and writes only:
     * no GGApi actions that run game code (use api->defer for those). */
    GG_EV_FRAME,
    GG_EV_COUNT
} GGEventType;

enum { GG_CONTINUE = 0, GG_ALLOW = 1, GG_REFUSE = 2, GG_SUPPRESS = 3 };

#define GG_BOX_EMPTY 1000

typedef struct GGEvent {
    int type;                     /* GGEventType */
    int building, level;
    int room, object, door, dest;
    int contents;
    int puzzle_type, puzzle_variant, result;
    int won, aborted, restore;
} GGEvent;

typedef int (*GGEventFn)(void *user, GGEvent *ev);

/* ---- Raw hook sites -------------------------------------------------------
 *
 * Any instruction listed in data/hook_sites.txt can be hooked directly. The hook
 * runs just before the instruction with the CPU registers; change them and the game
 * continues with the new values. Return 1 to skip the instruction. Hooking an
 * address that was not compiled in as a site fails (on_site returns 0).
 * Addresses are image offsets (see docs/MODDING.md).
 */
typedef struct GGRegs {
    uint16_t ax, bx, cx, dx, si, di, bp, sp;
    uint16_t cs, ds, es, ss, flags;
} GGRegs;

#define GG_FLAG_CF 0x0001
#define GG_FLAG_ZF 0x0040
#define GG_FLAG_SF 0x0080
#define GG_FLAG_OF 0x0800

typedef int (*GGSiteFn)(void *user, uint32_t addr, GGRegs *regs);

/* ---- Game state ------------------------------------------------------------
 * The current player's record lives in the data segment (DGROUP); these are DS
 * offsets for api->ds_read16 / ds_write16. See docs/MODDING.md for more. */
#define GG_DS_PLAYER_NAME     0x4562   /* char[16] */
#define GG_DS_SCORE           0x4572   /* u32 */
#define GG_DS_BANANAS         0x457E   /* u16 */
#define GG_DS_PARTS_NEEDED    0x4582   /* u16, HUD counter */
#define GG_DS_BUILDING        0x4592   /* u16, current building */
#define GG_DS_LEVEL           0x4594   /* u16, current level (0-based) */
#define GG_DS_RACES_WON_IDX   0x4596   /* s16[3], last race won per building (-1 none .. 4) */
#define GG_DS_WAREHOUSE_SAVED 0x459C   /* u16[3], building has a saved warehouse */
#define GG_DS_TOTAL_RACES_WON 0x5C0E   /* u16, 15 = game complete */
#define GG_DS_PLAYER_ROOM     0x7AA4   /* u16, warehouse room the player is in */

typedef struct GGApi {
    int version;                                  /* GG_API_VERSION of the host */

    /* Hooks. Callbacks run in registration order; the first non-GG_CONTINUE
     * answer wins. Return 1 on success. */
    int  (*on_event)(int type, GGEventFn fn, void *user);
    int  (*on_site)(uint32_t addr, GGSiteFn fn, void *user);
    /* Site by its name in data/hook_sites.txt; 0 if unknown. */
    uint32_t (*site_addr)(const char *name);

    /* Guest memory. Linear addresses (segment*16 + offset) into 1 MB. */
    uint8_t  *(*mem)(void);
    uint32_t (*ds_base)(void);                    /* linear address of DGROUP */
    uint16_t (*ds_read16)(uint16_t off);
    void     (*ds_write16)(uint16_t off, uint16_t v);
    uint8_t  (*ds_read8)(uint16_t off);
    void     (*ds_write8)(uint16_t off, uint8_t v);

    /* Actions that run the game's own code. Only from an event or site callback
     * (not GG_EV_FRAME) -- or schedule them with defer. */
    void (*add_bananas)(int n);                   /* also refreshes the HUD */
    void (*add_score)(int points);
    /* Give the player a part of the current level (warehouse only). Returns 1 if it
     * counted towards PARTS NEEDED, 0 if not needed or already held, -1 refused
     * (already holding two of that kind). */
    int  (*give_part)(int part);
    /* Far-call any game routine (image-space seg:off) with up to 8 word arguments,
     * cdecl. Returns DX:AX. */
    uint32_t (*call_far)(uint16_t seg, uint16_t off, int nargs, const uint16_t *args);

    /* Run fn at the next safe point (the next event or site callback that is not
     * GG_EV_FRAME). For work triggered from outside, e.g. a network message. */
    void (*defer)(void (*fn)(void *user), void *user);

    void (*log)(const char *fmt, ...);
    const char *(*game_dir)(void);
} GGApi;

typedef int (*GGModInitFn)(const GGApi *api);

#ifdef __cplusplus
}
#endif
#endif
