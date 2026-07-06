#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: PERIMETER DRAWING HELPERS ---
// ============================================================================
static void fill_rect_safe(GContext *ctx, int x, int y, int w, int h) { 
    graphics_fill_rect(ctx, GRect(x, y, w, h), 0, GCornerNone);
} 

void draw_perimeter_segment(GContext *ctx, int p_start, int p_end, int thickness, GColor color) { 
    if (p_start >= p_end) return;
    graphics_context_set_fill_color(ctx, color);
    int segs[6] = {0, 100, 328, 528, 756, 856};

    for(int i = 0; i < 5; i++) { 
        int s = (p_start > segs[i]) ? p_start : segs[i]; 
        int e = (p_end < segs[i+1]) ? p_end : segs[i+1];
        if (s < e) { 
            int len = e - s;
            if (i == 0) fill_rect_safe(ctx, 100 + s, 0, len, thickness);
            else if (i == 1) fill_rect_safe(ctx, 200 - thickness, s - 100, thickness, len);
            else if (i == 2) fill_rect_safe(ctx, 200 - (s - 328) - len, 228 - thickness, len, thickness);
            else if (i == 3) fill_rect_safe(ctx, 0, 228 - (s - 528) - len, thickness, len);
            else if (i == 4) fill_rect_safe(ctx, s - 756, 0, len, thickness);
        } 
    } 
} 

GPoint get_perimeter_point(int p, int thickness) { 
    int ht = thickness / 2;
    GPoint pt;
    if (p <= 100) pt = GPoint(100 + p, ht);
    else if (p <= 328) pt = GPoint(200 - ht, p - 100);
    else if (p <= 528) pt = GPoint(200 - (p - 328), 228 - ht);
    else if (p <= 756) pt = GPoint(ht, 228 - (p - 528)); 
    else pt = GPoint(p - 756, ht);

    if (pt.x < ht) pt.x = ht;
    if (pt.x > 200 - ht) pt.x = 200 - ht;
    if (pt.y < ht) pt.y = ht;
    if (pt.y > 228 - ht) pt.y = 228 - ht;
    return pt;
}