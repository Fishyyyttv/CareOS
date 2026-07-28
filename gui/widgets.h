/* CareOS gui/widgets.h -- desktop widgets (clock, calendar, system meter).
 * Public API lives in gui.h (widgets_init / widgets_draw / widgets_handle_mouse);
 * this header exists for the implementation's own internal declarations. */
#ifndef CAREOS_WIDGETS_H
#define CAREOS_WIDGETS_H

#include "kernel.h"
#include "gui.h"

/* Widget identity. Order is also the top-to-bottom stacking order set up by
 * widgets_init() and the iteration order used for hit-testing. */
typedef enum {
    WIDGET_CLOCK = 0,
    WIDGET_CALENDAR,
    WIDGET_SYSTEM,
    WIDGET_KIND_COUNT
} dwidget_kind_t;

/* One movable desktop card. rect is absolute screen space; the drag state is
 * only meaningful while this card is the one being dragged. */
typedef struct {
    rect_t rect;        /* absolute position + size on the desktop  */
    i32    grab_dx;     /* cursor offset from rect origin at grab    */
    i32    grab_dy;
} dwidget_t;

#endif /* CAREOS_WIDGETS_H */
