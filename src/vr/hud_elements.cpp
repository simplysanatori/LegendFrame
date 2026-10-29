// Per-element HUD layout: the item wheel and the ammo counter, moved,
// resized or hidden one by one.
//
// The mod only ever sees the HUD as batched draws, one per texture page
// (ui_space.cpp), so one draw can hold parts of several elements. The split
// is made earlier, in the game's data-driven interface (tr7.pdb types,
// addresses remapped to this build and checked below):
//
//   Every interface item starts with { u16 itemFlags; u16 itemType;
//   UIItemFnTable* fnTable; }. Each item type has one static fnTable in
//   .data, and every draw goes through fnTable->drawFn(item, UIDrawInfo&).
//   Groups (type 1) hold child items; the player HUD (UIScreenID 5000,
//   kUIScreenPlayerHUD) is one screen whose item tree holds every element.
//
//   The visible items carry a fade group (UIFadeGroupType): 3 weapons (the
//   ammo counter), 4 gear, 5 inventory selection (the d-pad cross, triggered
//   by UIHUDInventoryMenuHandler 0x004F5910 for each direction), 14 its
//   low-light warning. Health is 1, hints 7 and 8, and so on.
//
//   Each leaf draw takes its anchor from UIItem::GetAnchor and then passes
//   that anchor through UIDrawInfo::GetTransXYZ (0x004EBC20), which adds the
//   animation offsets; its size goes through UIDrawInfo::GetScaleXYZ
//   (0x004EBCF0). Anchors are fractions of the screen (0 to 1, x right,
//   y down; sizes are given in 1/512 and 1/448 of it).
//
// So: the drawFn of every item type is swapped (data only, no code patched)
// for a wrapper that classifies the item by the fade groups below it. The
// outermost item whose fade groups all belong to one element (the item wheel
// or the ammo counter) becomes that element's root; while it draws, the two
// UIDrawInfo getters are hooked to move every anchor (around the element's
// centre for a scale) and to scale every size. A hidden element's root is
// simply not drawn. Items with no fade group in a mixed group (an icon next
// to the ammo text, say) stay where they are; hud_debug = 1 logs what was
// found so a wrong guess can be seen in trlvr.log.

#include "hud_elements.h"

#include "hook.h"

#include "../common/config.h"
#include "../common/log.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace trlvr
{
    namespace
    {
        enum Element
        {
            kNone = 0,
            kItems = 1,   // the item wheel (d-pad gear cross)
            kAmmo = 2,    // the ammo counter
        };

        // Fade groups (UIFadeGroupType) per element.
        const unsigned kItemsFade = (1u << 4) | (1u << 5) | (1u << 14);
        const unsigned kAmmoFade = 1u << 3;
        const unsigned kFadeCount = 0x12;

        // One fnTable per item type (UIItemType 1..15), with the drawFn each
        // must hold before it is swapped (read from trl.exe; the init
        // switch at 0x004EBE20 sets item->fnTable).
        struct TypeTable
        {
            uintptr_t table;
            uintptr_t draw;
            const char* name;
        };
        const TypeTable kTables[15] = {
            { 0x00F169DC, 0x004EC260, "group" },
            { 0x00F16C6C, 0x004F24A0, "static text" },
            { 0x00F16CE4, 0x004F2650, "dynamic text" },
            { 0x00F16D6C, 0x004F33D0, "text selector" },
            { 0x00F16CA8, 0x004F25F0, "edit text" },
            { 0x00F16B24, 0x004EE740, "model" },
            { 0x00F16A5C, 0x004ECDA0, "image" },
            { 0x00F16B6C, 0x004EF6B0, "rect" },
            { 0x00F16AE4, 0x004EE4B0, "meter" },
            { 0x00F16A1C, 0x004ECA30, "group selector" },
            { 0x00F16A9C, 0x004ED6A0, "inventory selector" },
            { 0x00F16C2C, 0x004F1360, "slider" },
            { 0x00F16BAC, 0x004F0270, "rounded rect" },
            { 0x00F16BEC, 0x004F1110, "rect outline" },
            { 0x00F16D2C, 0x004F2E00, "text log" },
        };
        const unsigned kDrawFnOffset = 0x10;

        using PFN_Draw = int(__cdecl*)(void*, void*);
        PFN_Draw g_original[15] = {};

        // UIDrawInfo::GetTransXYZ / GetScaleXYZ(float*, float*, float*),
        // thiscall on the UIDrawInfo.
        const uintptr_t kGetTransXYZ = 0x004EBC20;
        const unsigned char kGetTransSignature[7] =
            { 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x0C };
        const uintptr_t kGetScaleXYZ = 0x004EBCF0;
        const unsigned char kGetScaleSignature[7] =
            { 0x53, 0x56, 0x8B, 0xD9, 0x8B, 0x73, 0x0C };
        typedef void(__thiscall* PFN_GetXYZ)(void*, float*, float*, float*);
        PFN_GetXYZ g_get_trans = nullptr;
        PFN_GetXYZ g_get_scale = nullptr;

        // UIScreenManager::gHUDScreens (3 UIScreen*) and sHUDScreenCount;
        // UIScreen begins with its short screenID.
        const uintptr_t kHUDScreens = 0x011161D0;
        const uintptr_t kHUDScreenCount = 0x011161F8;

        struct Accum
        {
            float min_x, min_y, max_x, max_y;
            int count;
        };

        // The element being drawn, while its root item draws.
        struct Context
        {
            int element;
            bool own_pivot;     // a single leaf: scale in place
            float pivot_x, pivot_y;
            float dx, dy, scale;
            Accum* accum;
        };
        Context g_ctx = {};

        // Each root's centre from its previous frame (the scale pivot).
        struct PivotSlot
        {
            const void* root;
            float x, y;
            bool valid;
            unsigned draws, moved;
        };
        PivotSlot g_pivots[16] = {};
        unsigned g_pivot_next = 0;

        struct Settings
        {
            float dx, dy, scale;
            bool hide;
        };

        Settings settings_for(int element)
        {
            const Config& c = config();
            // ini units are hud_offset's (NDC: 2 = the whole view, +y up);
            // the interface works in screen fractions with y down.
            if (element == kItems)
                return { c.hud_items_x * 0.5f, -c.hud_items_y * 0.5f,
                         c.hud_items_scale, c.hud_items_hide };
            return { c.hud_ammo_x * 0.5f, -c.hud_ammo_y * 0.5f,
                     c.hud_ammo_scale, c.hud_ammo_hide };
        }

        bool neutral(const Settings& s)
        {
            return s.dx == 0.0f && s.dy == 0.0f && s.scale == 1.0f && !s.hide;
        }

        unsigned item_type(const unsigned char* item)
        {
            return *reinterpret_cast<const unsigned short*>(item + 2);
        }

        unsigned fade_bit(unsigned char id)
        {
            return id > 0 && id < kFadeCount ? 1u << id : 0u;
        }

        // The fade groups of one item itself (not its children).
        unsigned own_fade_mask(const unsigned char* item, unsigned type)
        {
            switch (type)
            {
            case 2: case 3: case 5:     // UIItemStaticText and derived
                return fade_bit(item[8]) | fade_bit(item[9]);
            case 6:                     // UIItemModel
            case 9:                     // UIItemMeter
                return fade_bit(item[8]);
            case 8:                     // UIItemRect
                return fade_bit(item[0xD]) | fade_bit(item[0xE]);
            case 13: case 14:           // UIItemRoundedRect, RRectOutline
                return fade_bit(item[0xA]) | fade_bit(item[0xB]);
            default:
                return 0;
            }
        }

        bool is_container(unsigned type)
        {
            return type == 1 || type == 9 || type == 10 || type == 11 ||
                   type == 12;
        }

        // Fade groups of an item and everything below it. Data is walked
        // with bounds on depth, width and total count.
        unsigned subtree_fade_mask(const unsigned char* item, int depth,
                                   int& budget)
        {
            if (!item || depth > 10 || --budget < 0)
                return 0;
            const unsigned type = item_type(item);
            unsigned mask = own_fade_mask(item, type);
            switch (type)
            {
            case 1:                     // UIItemGroup: itemCount, items[]
            {
                const int count = *reinterpret_cast<const int*>(item + 0x20);
                if (count <= 0 || count > 256)
                    break;
                const unsigned char* const* children =
                    reinterpret_cast<const unsigned char* const*>(item + 0x24);
                for (int i = 0; i < count && budget > 0; ++i)
                    mask |= subtree_fade_mask(children[i], depth + 1, budget);
                break;
            }
            case 9:                     // UIItemMeter: backgroundItems
                mask |= subtree_fade_mask(*reinterpret_cast<
                    const unsigned char* const*>(item + 0x48), depth + 1,
                    budget);
                break;
            case 10:                    // UIItemGroupSelector
            case 11:                    // UIItemInventorySelector
            {
                const int count = *reinterpret_cast<const short*>(item + 0xA);
                if (count <= 0 || count > 64)
                    break;
                const unsigned char* const* selectors =
                    reinterpret_cast<const unsigned char* const*>(
                        item + (type == 10 ? 0x1C : 0xC));
                for (int i = 0; i < count && budget > 0; ++i)
                {
                    const unsigned char* sel = selectors[i];
                    if (!sel)
                        continue;
                    // { UIItemGroup* group; float value; } or
                    // { short type, subType; UIItem* drawItems; }
                    mask |= subtree_fade_mask(*reinterpret_cast<
                        const unsigned char* const*>(sel + (type == 10 ? 0
                                                                      : 4)),
                        depth + 1, budget);
                }
                break;
            }
            case 12:                    // UIItemSlider: sliderItem
                mask |= subtree_fade_mask(*reinterpret_cast<
                    const unsigned char* const*>(item + 0x28), depth + 1,
                    budget);
                break;
            default:
                break;
            }
            return mask;
        }

        unsigned fade_mask_of(const unsigned char* item, bool* ok)
        {
            unsigned mask = 0;
            *ok = false;
            __try
            {
                int budget = 2048;
                mask = subtree_fade_mask(item, 0, budget);
                *ok = true;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            return mask;
        }

        int element_of(unsigned mask)
        {
            const unsigned other = mask & ~(kItemsFade | kAmmoFade);
            if (other)
                return kNone;
            if ((mask & kItemsFade) && !(mask & kAmmoFade))
                return kItems;
            if ((mask & kAmmoFade) && !(mask & kItemsFade))
                return kAmmo;
            return kNone;
        }

        // Where a container is anchored (UIItem::GetAnchor, 0x004EBED0:
        // itemFlags 0x1000 adds the parent's anchor; 0 takes the parent's).
        void anchor_of(const unsigned char* item, unsigned type,
                       const unsigned char* info, float* x, float* y)
        {
            const float ix = *reinterpret_cast<const float*>(info + 0x0);
            const float iy = *reinterpret_cast<const float*>(info + 0x4);
            unsigned offset = 0;
            if (type == 1)
                offset = 0x14;          // UIItemGroup anchorX / anchorY
            else if (type == 10)
                offset = 0x0C;          // UIItemGroupSelector
            if (!offset)
            {
                *x = ix;
                *y = iy;
                return;
            }
            const float gx = *reinterpret_cast<const float*>(item + offset);
            const float gy = *reinterpret_cast<const float*>(item + offset + 4);
            const bool relative =
                (*reinterpret_cast<const unsigned short*>(item) & 0x1000) != 0;
            *x = relative ? gx + ix : gx == 0.0f ? ix : gx;
            *y = relative ? gy + iy : gy == 0.0f ? iy : gy;
        }

        PivotSlot* pivot_slot(const void* root, bool create)
        {
            for (PivotSlot& slot : g_pivots)
                if (slot.root == root)
                    return &slot;
            if (!create)
                return nullptr;
            PivotSlot& slot = g_pivots[g_pivot_next++ % 16];
            slot.root = root;
            slot.valid = false;
            slot.draws = slot.moved = 0;
            return &slot;
        }

        // --- hud_debug ---------------------------------------------------

        const char* element_name(int element)
        {
            return element == kItems ? "item wheel"
                 : element == kAmmo ? "ammo counter" : "none";
        }

        void fade_list(unsigned mask, char* out, size_t size)
        {
            int n = 0;
            out[0] = '\0';
            for (unsigned id = 1; id < kFadeCount; ++id)
                if (mask & (1u << id))
                    n += snprintf(out + n, size - n, "%s%u", n ? "," : "",
                                  id);
            if (!n)
                snprintf(out, size, "none");
        }

        const char* screen_name(int id)
        {
            switch (id)
            {
            case 5000: return "player HUD";
            case 5001: return "binoculars HUD";
            case 5002: return "vehicle HUD";
            case 5003: return "cinematic HUD";
            default: return "screen";
            }
        }

        void debug_screens()
        {
            static int last[4] = { -1, -1, -1, -1 };
            int now[4] = { 0, 0, 0, 0 };
            __try
            {
                int count = *reinterpret_cast<const int*>(kHUDScreenCount);
                if (count < 0)
                    count = 0;
                if (count > 3)
                    count = 3;
                now[0] = count;
                const unsigned char* const* screens =
                    reinterpret_cast<const unsigned char* const*>(kHUDScreens);
                for (int i = 0; i < count; ++i)
                    now[1 + i] = screens[i]
                        ? *reinterpret_cast<const short*>(screens[i]) : 0;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                return;
            }
            if (memcmp(now, last, sizeof(now)) == 0)
                return;
            memcpy(last, now, sizeof(now));
            char text[160];
            int n = snprintf(text, sizeof(text), "%d:", now[0]);
            for (int i = 0; i < now[0]; ++i)
                n += snprintf(text + n, sizeof(text) - n, " %d (%s)",
                              now[1 + i], screen_name(now[1 + i]));
            log("hud elements: HUD screens %s", text);
        }

        // Once per item (bounded), so a frame never floods the log.
        bool debug_first_sight(const void* item, int element)
        {
            static struct { const void* item; int element; } seen[96];
            static unsigned count = 0;
            for (unsigned i = 0; i < count; ++i)
                if (seen[i].item == item && seen[i].element == element)
                    return false;
            if (count >= 96)
                return false;
            seen[count].item = item;
            seen[count].element = element;
            ++count;
            return true;
        }

        void debug_item(const unsigned char* item, unsigned type,
                        const unsigned char* info, unsigned mask,
                        int element, const char* what)
        {
            if (!debug_first_sight(item, element))
                return;
            float x = 0.0f, y = 0.0f;
            int children = -1;
            __try
            {
                anchor_of(item, type, info, &x, &y);
                if (type == 1)
                    children = *reinterpret_cast<const int*>(item + 0x20);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            char fades[64];
            fade_list(mask, fades, sizeof(fades));
            char kind[48];
            const char* name = type >= 1 && type <= 15
                ? kTables[type - 1].name : "?";
            if (children >= 0)
                snprintf(kind, sizeof(kind), "%s, %d items", name, children);
            else
                snprintf(kind, sizeof(kind), "%s", name);
            log("hud elements: %s %s %p (%s), fade groups %s, anchor %.3f, "
                "%.3f", what, element_name(element), (const void*)item, kind,
                fades, x, y);
        }

        // --- the wrapper -------------------------------------------------

        PFN_Draw original_for(const unsigned char* item, unsigned type)
        {
            const uintptr_t table =
                *reinterpret_cast<const uintptr_t*>(item + 4);
            if (type >= 1 && type <= 15 && kTables[type - 1].table == table)
                return g_original[type - 1];
            for (unsigned i = 0; i < 15; ++i)
                if (kTables[i].table == table)
                    return g_original[i];
            return nullptr;
        }

        int __cdecl draw_item(void* item_ptr, void* info_ptr)
        {
            const unsigned char* item =
                static_cast<const unsigned char*>(item_ptr);
            const unsigned char* info =
                static_cast<const unsigned char*>(info_ptr);
            const unsigned type = item_type(item);
            const PFN_Draw original = original_for(item, type);
            if (!original)
                return 0;

            const bool debug = config().hud_debug;
            if (g_ctx.element != kNone)
                return original(item_ptr, info_ptr);   // inside a root
            if (!debug && neutral(settings_for(kItems)) &&
                neutral(settings_for(kAmmo)))
                return original(item_ptr, info_ptr);

            static int s_depth = 0;
            if (debug && s_depth == 0)
                debug_screens();

            bool ok = false;
            const unsigned mask = fade_mask_of(item, &ok);
            const int element = ok ? element_of(mask) : kNone;
            if (element == kNone)
            {
                if (debug && mask && is_container(type))
                    debug_item(item, type, info, mask, kNone,
                               "mixed (left alone):");
                ++s_depth;
                const int result = original(item_ptr, info_ptr);
                --s_depth;
                return result;
            }

            if (debug)
                debug_item(item, type, info, mask, element, "root of the");
            const Settings s = settings_for(element);
            if (s.hide)
                return 0;
            if (neutral(s))
            {
                ++s_depth;
                const int result = original(item_ptr, info_ptr);
                --s_depth;
                return result;
            }

            Accum accum = { 1e30f, 1e30f, -1e30f, -1e30f, 0 };
            const Context saved = g_ctx;
            g_ctx.element = element;
            g_ctx.dx = s.dx;
            g_ctx.dy = s.dy;
            g_ctx.scale = s.scale;
            g_ctx.own_pivot = !is_container(type);
            g_ctx.accum = nullptr;
            PivotSlot* slot = nullptr;
            if (!g_ctx.own_pivot)
            {
                slot = pivot_slot(item, true);
                if (slot->valid)
                {
                    g_ctx.pivot_x = slot->x;
                    g_ctx.pivot_y = slot->y;
                }
                else
                {
                    anchor_of(item, type, info, &g_ctx.pivot_x,
                              &g_ctx.pivot_y);
                }
                g_ctx.accum = &accum;
            }
            ++s_depth;
            const int result = original(item_ptr, info_ptr);
            --s_depth;
            g_ctx = saved;
            if (slot && slot->root == item)
            {
                ++slot->draws;
                slot->moved += accum.count > 0 ? 1u : 0u;
                if (debug && slot->draws == 600 && slot->moved == 0)
                    log("hud elements: %s %p has drawn 600 times without a "
                        "part that can move (hidden by its fade, or its "
                        "parts skip the position getter)",
                        element_name(element), (const void*)item);
            }
            // The slot may have been reused by a nested root meanwhile.
            if (slot && slot->root == item && accum.count > 0)
            {
                slot->x = 0.5f * (accum.min_x + accum.max_x);
                slot->y = 0.5f * (accum.min_y + accum.max_y);
                slot->valid = true;
            }
            return result;
        }

        void __fastcall detour_get_trans(void* info, void*, float* x,
                                         float* y, float* z)
        {
            g_get_trans(info, x, y, z);
            if (g_ctx.element == kNone || !x || !y)
                return;
            const float ox = *x, oy = *y;
            if (Accum* a = g_ctx.accum)
            {
                a->min_x = ox < a->min_x ? ox : a->min_x;
                a->min_y = oy < a->min_y ? oy : a->min_y;
                a->max_x = ox > a->max_x ? ox : a->max_x;
                a->max_y = oy > a->max_y ? oy : a->max_y;
                ++a->count;
            }
            const float px = g_ctx.own_pivot ? ox : g_ctx.pivot_x;
            const float py = g_ctx.own_pivot ? oy : g_ctx.pivot_y;
            *x = px + g_ctx.scale * (ox - px) + g_ctx.dx;
            *y = py + g_ctx.scale * (oy - py) + g_ctx.dy;
        }

        void __fastcall detour_get_scale(void* info, void*, float* x,
                                         float* y, float* z)
        {
            g_get_scale(info, x, y, z);
            if (g_ctx.element == kNone || g_ctx.scale == 1.0f || !x || !y)
                return;
            *x *= g_ctx.scale;
            *y *= g_ctx.scale;
        }
    }

    void hud_elements_init()
    {
        void* tramp = nullptr;
        if (!hook_install((void*)kGetTransXYZ, (void*)&detour_get_trans,
                          &tramp, kGetTransSignature,
                          sizeof(kGetTransSignature),
                          "UIDrawInfo::GetTransXYZ (HUD elements)"))
        {
            log("hud elements: off (interface position getter not found)");
            return;
        }
        g_get_trans = reinterpret_cast<PFN_GetXYZ>(tramp);
        tramp = nullptr;
        if (hook_install((void*)kGetScaleXYZ, (void*)&detour_get_scale,
                         &tramp, kGetScaleSignature,
                         sizeof(kGetScaleSignature),
                         "UIDrawInfo::GetScaleXYZ (HUD elements)"))
            g_get_scale = reinterpret_cast<PFN_GetXYZ>(tramp);
        else
            log("hud elements: scale unavailable (size getter not found)");

        // Check every table first: either all are swapped or none.
        for (const TypeTable& t : kTables)
        {
            uintptr_t draw = 0;
            __try
            {
                draw = *reinterpret_cast<const uintptr_t*>(t.table +
                                                           kDrawFnOffset);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            if (draw != t.draw)
            {
                log("hud elements: off (the %s draw table at 0x%08X holds "
                    "0x%08X, expected 0x%08X)", t.name, (unsigned)t.table,
                    (unsigned)draw, (unsigned)t.draw);
                return;
            }
        }
        for (unsigned i = 0; i < 15; ++i)
        {
            uintptr_t* slot = reinterpret_cast<uintptr_t*>(kTables[i].table +
                                                           kDrawFnOffset);
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
            {
                log("hud elements: could not unprotect the %s draw table",
                    kTables[i].name);
                for (unsigned j = 0; j < i; ++j)
                {
                    uintptr_t* back = reinterpret_cast<uintptr_t*>(
                        kTables[j].table + kDrawFnOffset);
                    *back = kTables[j].draw;
                }
                return;
            }
            g_original[i] = reinterpret_cast<PFN_Draw>(kTables[i].draw);
            *slot = reinterpret_cast<uintptr_t>(&draw_item);
            DWORD unused = 0;
            VirtualProtect(slot, sizeof(*slot), old, &unused);
        }
        const Config& c = config();
        log("hud elements: on (15 item draws wrapped); item wheel x=%+.2f "
            "y=%+.2f scale %.2f%s, ammo counter x=%+.2f y=%+.2f scale "
            "%.2f%s%s", c.hud_items_x, c.hud_items_y, c.hud_items_scale,
            c.hud_items_hide ? " hidden" : "", c.hud_ammo_x, c.hud_ammo_y,
            c.hud_ammo_scale, c.hud_ammo_hide ? " hidden" : "",
            c.hud_debug ? ", hud_debug on" : "");
    }
}
