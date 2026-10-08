#ifndef DCB_DIALOG_H
#define DCB_DIALOG_H

#include "game.h"
#include "dcb/menu.h"

/* A message box, optionally with two choices ("Yes"/"No" or custom labels). */
typedef struct {
    /* 0x00 */ UiWindow win;
    /* 0x44 */ CursorHighlight cursor;
    /* 0x94 */ u8 *text;
#ifndef PC_PORT
    /* 0x98 */ char *yesLabel;
    /* 0x9C */ char *noLabel;
    /* 0xA0 */ void (*onFrame)(void);
    /* 0xA4 */ u8 type; /* 0: message only, 1: Yes/No, 2: labels set by the caller */
    /* 0xA5 */ s8 choice; /* 0: cancelled, 1: yes, 2: no, 3: closed from outside (closed set) */
#else
    /* PC_PORT: the callers' views of a dialog (below) name these fields too; on the host every view is this Dialog,
       so their names are members of the same storage */
    union {
        struct {
            /* 0x98 */ char *yesLabel;
            /* 0x9C */ char *noLabel;
        };
        char *options[2]; /* ChoiceDialog (OPENSEG) */
        struct {
            char *yes; /* DialogK (KAWSEG) */
            char *no;
        };
    };
    union {
        /* 0xA0 */ void (*onFrame)(void);
        void (*draw)(void); /* DialogK */
    };
    /* 0xA4 */ u8 type; /* 0: message only, 1: Yes/No, 2: labels set by the caller */
    union {
        /* 0xA5 */ s8 choice; /* 0: cancelled, 1: yes, 2: no, 3: closed from outside (closed set) */
        s8 result; /* DialogK */
    };
#endif
    /* 0xA6 */ u8 pad;
    /* 0xA7 */ u8 halfTextWidth;
    /* 0xA8 */ s16 width;
    /* 0xAA */ s16 height;
    /* 0xAC */ s16 yesX;
    /* 0xAE */ s16 yesWidth;
    /* 0xB0 */ s16 noX;
    /* 0xB2 */ s16 noWidth;
    /* 0xB4 */ u8 closed;
    /* 0xB5 */ u8 cancelDisabled;
    /* 0xB6 */ u8 unkB6; /* flag 0x80 of initDialog's flags */
} Dialog;

#ifdef PC_PORT
/* The callers keep their dialogs in types of their own that name Dialog's fields by their PS1 offsets (ChoiceDialog,
   DialogK, EvoDialog, Window, the u8[0xB8] buffers; issue #23). On the host Dialog has pointers before those fields,
   so each view is the Dialog itself: the views' field names are Dialog's (its unions above), and the objects declared
   as views have the host's size. */
typedef Dialog Window; /* game.h's view: choice at 0xA5 */
extern Dialog DUEL_DIALOG; /* game.h declares it as an s32 the C reads as a Window */
/* a dialog buffer, u8 name[size] on the PS1, and its fields the C reads by their offsets */
#define DIALOG_BUFFER(name, size) Dialog name[1]
#define DIALOG_BUFFER_CHOICE(buf) ((buf)->choice) /* + 0xA5 */
#define DIALOG_BUFFER_PAD(buf) ((buf)->pad)       /* + 0xA6 */
#else
#define DIALOG_BUFFER(name, size) u8 name[size]
#define DIALOG_BUFFER_CHOICE(buf) buf[0xA5]
#define DIALOG_BUFFER_PAD(buf) buf[0xA6]
#endif

/* (Dialog *dialog, u8 *text, u32 flags): left without a prototype because the
   callers still pass their dialogs as other types */
void initDialog();
void dialogTask();
void drawDialogBody(Dialog *dialog);
s32 runDialogForPad(void *dialog, s32 pad);
s8 runDialog(void *dialog);

#endif /* DCB_DIALOG_H */
