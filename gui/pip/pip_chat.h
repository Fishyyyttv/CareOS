/* gui/pip/pip_chat.h -- Pip's conversation: this session's history, and the
 * bridge from an intent to a real CareOS action. The language layer is
 * pip_assist.c; swap pip_chat_understand() for a smarter backend later. */
#ifndef CAREOS_PIP_CHAT_H
#define CAREOS_PIP_CHAT_H
#include "kernel.h"

#define PIP_CHAT_MAX 40
#define PIP_MSG_LEN  520

typedef struct { bool pip; char text[PIP_MSG_LEN]; } pip_msg_t;

u32               pip_chat_count(void);
const pip_msg_t  *pip_chat_get(u32 i);          /* 0 = oldest */
u32               pip_chat_serial(void);        /* changes whenever the history does */
bool              pip_chat_busy(void);          /* a reply is on its way ("typing...") */
bool              pip_chat_waiting_confirm(void);
void              pip_chat_submit(const char *text);   /* the user said something */
void              pip_chat_tick(void);          /* deliver a due reply; call every frame */
void              pip_chat_intro(void);         /* first words when Home opens with no history */
void              pip_chat_reset(void);         /* end of session */
void              pip_chat_pip_says(const char *text);

/* Opens or focuses an app window by pip_app_t. Returns the window or NULL. */
window_t         *pip_open_app(u32 pip_app);
i32               pip_app_of_window(const window_t *w);   /* pip_app_t, or -1 */

#endif
