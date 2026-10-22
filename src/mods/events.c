/*
 * events.c - what the named hook sites in data/hook_sites.txt mean.
 *
 * Each handler runs just before its instruction, reads the game state it needs,
 * fires the typed event, and writes back what the mods changed. Addresses and the
 * memory they read come from the AP project's reverse-engineering notes; the
 * instruction each comment shows is the one the site sits on.
 */
#include "mods.h"

#define DS GG_DGROUP_SEG
#define RD(off)     mem_read16(cpu, DS, (uint16_t)(off))
#define WR(off, v)  mem_write16(cpu, DS, (uint16_t)(off), (uint16_t)(v))

GGEvent gg_event_new(int type)
{
    GGEvent e;
    e.type = type;
    e.building = e.level = e.room = e.object = e.door = e.dest = -1;
    e.contents = e.puzzle_type = e.puzzle_variant = e.result = -1;
    e.won = e.aborted = e.restore = -1;
    return e;
}

static GGEvent ev_here(CPU *cpu, int type)
{
    GGEvent e = gg_event_new(type);
    e.building = (int16_t)RD(0x4592);
    e.level = (int16_t)RD(0x4594);
    return e;
}

static int pending_restore = -1;                  /* LEVEL_ENTER -> level_restore_check */

/* Make the conditional jump after a skipped `cmp` go the way we want. */
static void set_zf(CPU *cpu, int on)
{
    if (on) cpu->flags |= FLAG_ZF; else cpu->flags &= (uint16_t)~FLAG_ZF;
}

static int building_click(CPU *cpu, int b)
{
    /* cmp word [4596+2b], 4 ; je refuse */
    if (!gg_has_listeners(GG_EV_BUILDING_CLICK)) return 0;
    GGEvent e = gg_event_new(GG_EV_BUILDING_CLICK);
    e.building = b;
    int r = gg_fire(&e);
    if (r == GG_ALLOW)  { set_zf(cpu, 0); return 1; }
    if (r == GG_REFUSE) { set_zf(cpu, 1); return 1; }
    return 0;
}

int gg_event_site(CPU *cpu, unsigned addr)
{
    switch (addr) {
    case 0x03234: return building_click(cpu, 1);
    case 0x03245: return building_click(cpu, 2);
    case 0x03256: return building_click(cpu, 0);

    case 0x03263: {                               /* mov [bp-6], 1: leave the select loop */
        GGEvent e = ev_here(cpu, GG_EV_BUILDING_SELECTED);
        e.level = -1;
        gg_fire(&e);
        return 0;
    }
    case 0x03648: {                               /* mov [4594], ax: ax = level */
        GGEvent e = ev_here(cpu, GG_EV_LEVEL_ENTER);
        e.level = (int16_t)cpu->ax;
        e.restore = RD(0x459C + 2 * e.building) != 0;
        int lv0 = e.level, rs0 = e.restore;
        gg_fire(&e);
        if (e.level != lv0) cpu->ax = (uint16_t)e.level;
        pending_restore = e.restore != rs0 ? (e.restore != 0) : -1;
        return 0;
    }
    case 0x03670:                                 /* cmp [459C+2b], 0 ; je fresh */
        if (pending_restore < 0) return 0;
        set_zf(cpu, !pending_restore);
        pending_restore = -1;
        return 1;
    case 0x03765: {                               /* mov ax, 5: level set up */
        GGEvent e = ev_here(cpu, GG_EV_LEVEL_READY);
        gg_fire(&e);
        return 0;
    }
    case 0x0349C:                                 /* warehouse loop head */
    case 0xC4413: {                               /* assembly room / blueprint loop head */
        int t = addr == 0x0349C ? GG_EV_WAREHOUSE_TICK : GG_EV_ASSEMBLY_TICK;
        if (!gg_has_listeners(t)) return 0;
        GGEvent e = ev_here(cpu, t);
        gg_fire(&e);
        return 0;
    }
    case 0x08F81: {                               /* cmp es:[bx+8], 1 ; jne open ; (exit) */
        /* es:bx = the box object (just loaded from [bp-4]). +8 = taken, +1E = contents. */
        uint16_t oseg = cpu->es, ooff = cpu->bx;
        if (mem_read16(cpu, oseg, (uint16_t)(ooff + 8)) == 1) return 0;
        GGEvent e = ev_here(cpu, GG_EV_BOX_OPEN);
        e.room = (int16_t)RD(0x7AA4);
        e.object = (int16_t)RD(0x7ACA);
        int c0 = (int16_t)mem_read16(cpu, oseg, (uint16_t)(ooff + 0x1E));
        e.contents = c0;
        int r = gg_fire(&e);
        if (r == GG_SUPPRESS) {
            /* Remove the box the way the game does (R0622_0D8F(actor+1A, box)) and
             * take the routine's exit. The actor is the routine's far argument. */
            uint16_t aoff = mem_read16(cpu, cpu->ss, (uint16_t)(cpu->bp + 6));
            uint16_t aseg = mem_read16(cpu, cpu->ss, (uint16_t)(cpu->bp + 8));
            uint16_t args[3] = { mem_read16(cpu, aseg, (uint16_t)(aoff + 0x1A)), ooff, oseg };
            gg_call_far(0x0622, 0x0D8F, 3, args);
            if (c0 == GG_BOX_EMPTY) WR(0x7A00, RD(0x7A00) - 1);   /* empty boxes left */
            set_zf(cpu, 1);
            return 1;
        }
        if (e.contents != c0) mem_write16(cpu, oseg, (uint16_t)(ooff + 0x1E), (uint16_t)e.contents);
        return 0;
    }
    case 0x0382B: {                               /* cmp [bp+6], 0: the puzzle result */
        GGEvent e = ev_here(cpu, GG_EV_PUZZLE_RESULT);
        e.room = (int16_t)RD(0x7AA4);
        e.door = (int16_t)RD(0x7AC0);
        e.puzzle_type = (int16_t)RD(0xB13A);
        e.puzzle_variant = (int16_t)RD(0xB138);
        e.result = (int16_t)mem_read16(cpu, cpu->ss, (uint16_t)(cpu->bp + 6));
        int r0 = e.result;
        gg_fire(&e);
        if (e.result != r0) mem_write16(cpu, cpu->ss, (uint16_t)(cpu->bp + 6), (uint16_t)e.result);
        return 0;
    }
    case 0x03871: {                               /* mov es:[bx+10h], 9: door opens */
        GGEvent e = ev_here(cpu, GG_EV_DOOR_OPEN);
        e.room = (int16_t)RD(0x7AA4);
        e.door = (int16_t)RD(0x7AC0);
        e.dest = mem_read16(cpu, cpu->es, (uint16_t)(cpu->bx + 0x12)) & 0x0F;
        gg_fire(&e);
        return 0;
    }
    case 0x0337F: {                               /* mov ax, [4592]: pick the race engine */
        GGEvent e = ev_here(cpu, GG_EV_RACE_START);
        gg_fire(&e);
        return 0;
    }
    case 0x033A3: {                               /* cmp [455E], 0: [4560] = 0 if won */
        GGEvent e = ev_here(cpu, GG_EV_RACE_END);
        e.aborted = RD(0x455E) != 0;
        e.won = !e.aborted && RD(0x4560) == 0;
        int w0 = e.won;
        gg_fire(&e);
        if (e.won != w0) {
            if (e.won) { WR(0x455E, 0); WR(0x4560, 0); }
            else WR(0x4560, 1);
        }
        return 0;
    }
    }
    return 0;
}
