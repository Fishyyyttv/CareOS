/* =============================================================================
 * CareOS gui/widget.c -- Structural UI Widget Framework
 * ============================================================================= */
#include "gui.h"

widget_t *widget_create(widget_type_t type, i32 x, i32 y, i32 w, i32 h) {
    widget_t *wi = (widget_t *)kmalloc(sizeof(widget_t));
    if (!wi) return NULL;
    kmemset(wi, 0, sizeof(widget_t));
    wi->type = type;
    wi->rect = rect_make(x, y, w, h);
    return wi;
}

void widget_add_child(widget_t *parent, widget_t *child) {
    if (!parent || !child) return;
    child->parent = parent;
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        widget_t *curr = parent->first_child;
        while (curr->next_sibling) curr = curr->next_sibling;
        curr->next_sibling = child;
    }
}

void widget_update_abs_rect(widget_t *wi, i32 px, i32 py) {
    if (!wi) return;
    wi->abs_rect.x = px + wi->rect.x;
    wi->abs_rect.y = py + wi->rect.y;
    wi->abs_rect.w = wi->rect.w;
    wi->abs_rect.h = wi->rect.h;
    
    widget_t *child = wi->first_child;
    while (child) {
        widget_update_abs_rect(child, wi->abs_rect.x, wi->abs_rect.y);
        child = child->next_sibling;
    }
}

void widget_draw_recursive(widget_t *wi, gfx_buffer_t *target) {
    if (!wi) return;
    
    /* Draw self */
    switch (wi->type) {
        case WIDGET_PANEL:
            gfx_rect_rounded(wi->abs_rect.x, wi->abs_rect.y, wi->abs_rect.w, wi->abs_rect.h, 8, wi->bg_color);
            break;
        case WIDGET_BUTTON: {
            button_t b;
            b.rect = wi->abs_rect;
            kstrcpy(b.label, wi->text);
            b.bg = wi->bg_color;
            b.fg = wi->color;
            b.active = true;
            button_draw(&b);
            break;
        }
        case WIDGET_LABEL:
            gfx_str(wi->abs_rect.x, wi->abs_rect.y, wi->text, wi->color, COL_TRANSPARENT);
            break;
        default: break;
    }
    
    /* Draw children */
    widget_t *child = wi->first_child;
    while (child) {
        widget_draw_recursive(child, target);
        child = child->next_sibling;
    }
}

/* -- Layout Helpers ------------------------------------------------------- */

void layout_vbox(widget_t *parent, i32 padding, i32 gap) {
    if (!parent) return;
    i32 curr_y = padding;
    widget_t *child = parent->first_child;
    while (child) {
        child->rect.y = curr_y;
        child->rect.x = padding;
        child->rect.w = parent->rect.w - padding * 2;
        curr_y += child->rect.h + gap;
        child = child->next_sibling;
    }
}

void layout_hbox(widget_t *parent, i32 padding, i32 gap) {
    if (!parent) return;
    i32 curr_x = padding;
    widget_t *child = parent->first_child;
    while (child) {
        child->rect.x = curr_x;
        child->rect.y = padding;
        child->rect.h = parent->rect.h - padding * 2;
        curr_x += child->rect.w + gap;
        child = child->next_sibling;
    }
}
