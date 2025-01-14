/* $XTermId: input.c,v 1.269 2006/11/28 23:12:47 tom Exp $ */

/* $XFree86: xc/programs/xterm/input.c,v 3.76 2006/06/19 00:36:51 dickey Exp $ */

/*
 * Copyright 1999-2005,2006 by Thomas E. Dickey
 *
 *                         All Rights Reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization.
 *
 *
 * Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.
 *
 *                         All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and that
 * both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of Digital Equipment
 * Corporation not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 *
 *
 * DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
 * ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
 * DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
 * ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/* input.c */

#include <xterm.h>

#include <X11/keysym.h>

#ifdef VMS
#include <X11/keysymdef.h>
#endif

#if HAVE_X11_DECKEYSYM_H
#include <X11/DECkeysym.h>
#endif

#if HAVE_X11_SUNKEYSYM_H
#include <X11/Sunkeysym.h>
#endif

#include <X11/Xutil.h>
#include <ctype.h>

#include <xutf8.h>

#include <data.h>
#include <fontutils.h>

/*
 * Xutil.h has no macro to check for the complete set of function- and
 * modifier-keys that might be returned.  Fake it.
 */
#ifdef XK_ISO_Lock
#define IsPredefinedKey(n) ((n) >= XK_ISO_Lock && (n) <= XK_Delete)
#else
#define IsPredefinedKey(n) ((n) >= XK_BackSpace && (n) <= XK_Delete)
#endif

#ifdef XK_ISO_Left_Tab
#define IsTabKey(n) ((n) == XK_Tab || (n) == XK_ISO_Left_Tab)
#else
#define IsTabKey(n) ((n) == XK_Tab)
#endif

#ifndef IsPrivateKeypadKey
#define IsPrivateKeypadKey(k) (0)
#endif

#define XK_Fn(n)	(XK_F1 + (n) - 1)

#define IsBackarrowToggle(keyboard, keysym, state) \
	((((keyboard->flags & MODE_DECBKM) == 0) \
	    ^ ((state & ControlMask) != 0)) \
	&& (keysym == XK_BackSpace))

#define MAP(from, to) case from: result = to; break

#define KEYSYM_FMT "0x%04lX"	/* simplify matching <X11/keysymdef.h> */

#define TEK4014_GIN(tw) (tw != 0 && tw->screen.TekGIN)

typedef struct {
    KeySym keysym;
    Bool is_fkey;
    int nbytes;
#define STRBUFSIZE 500
    char strbuf[STRBUFSIZE];
} KEY_DATA;

/*                       0123456789 abc def0123456789abcdef0123456789abcdef0123456789abcd */
static char *kypd_num = " XXXXXXXX\tXXX\rXXXxxxxXXXXXXXXXXXXXXXXXXXXX*+,-./0123456789XXX=";

/*                       0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcd */
static char *kypd_apl = " ABCDEFGHIJKLMNOPQRSTUVWXYZ??????abcdefghijklmnopqrstuvwxyzXXX";

static char *curfinal = "HDACB  FE";

static int decfuncvalue(KEY_DATA *);
static void sunfuncvalue(ANSI *, KEY_DATA *);
static void hpfuncvalue(ANSI *, KEY_DATA *);
static void scofuncvalue(ANSI *, KEY_DATA *);

#if OPT_TRACE
static char *
ModifierName(unsigned modifier)
{
    char *s = "";
    if (modifier & ShiftMask)
	s = " Shift";
    else if (modifier & LockMask)
	s = " Lock";
    else if (modifier & ControlMask)
	s = " Control";
    else if (modifier & Mod1Mask)
	s = " Mod1";
    else if (modifier & Mod2Mask)
	s = " Mod2";
    else if (modifier & Mod3Mask)
	s = " Mod3";
    else if (modifier & Mod4Mask)
	s = " Mod4";
    else if (modifier & Mod5Mask)
	s = " Mod5";
    return s;
}

#define FMT_MODIFIER_NAMES "%s%s%s%s%s%s%s%s"
#define ARG_MODIFIER_NAMES(state) \
	   ModifierName(state & ShiftMask), \
	   ModifierName(state & LockMask), \
	   ModifierName(state & ControlMask), \
	   ModifierName(state & Mod1Mask), \
	   ModifierName(state & Mod2Mask), \
	   ModifierName(state & Mod3Mask), \
	   ModifierName(state & Mod4Mask), \
	   ModifierName(state & Mod5Mask)
#endif

static void
AdjustAfterInput(XtermWidget xw)
{
    TScreen *screen = &(xw->screen);

    if (screen->scrollkey && screen->topline != 0)
	WindowScroll(xw, 0);
    if (screen->marginbell) {
	int col = screen->max_col - screen->nmarginbell;
	if (screen->bellarmed >= 0) {
	    if (screen->bellarmed == screen->cur_row) {
		if (screen->cur_col >= col) {
		    Bell(XkbBI_MarginBell, 0);
		    screen->bellarmed = -1;
		}
	    } else
		screen->bellarmed =
		    screen->cur_col < col ? screen->cur_row : -1;
	} else if (screen->cur_col < col)
	    screen->bellarmed = screen->cur_row;
    }
}

/*
 * Return true if the key is on the editing keypad.  This overlaps with
 * IsCursorKey() and IsKeypadKey() and must be tested before those macro to
 * distinguish it from them.
 */
static Bool
IsEditFunctionKey(KeySym keysym)
{
    switch (keysym) {
    case XK_Prior:		/* editing keypad */
    case XK_Next:		/* editing keypad */
    case XK_Insert:		/* editing keypad */
    case XK_Find:		/* editing keypad */
    case XK_Select:		/* editing keypad */
#ifdef DXK_Remove
    case DXK_Remove:		/* editing keypad */
#endif
#ifdef XK_KP_Delete
    case XK_KP_Delete:		/* editing key on numeric keypad */
    case XK_KP_Insert:		/* editing key on numeric keypad */
#endif
#ifdef XK_ISO_Left_Tab
    case XK_ISO_Left_Tab:
#endif
	return True;
    default:
	return False;
    }
}

#if OPT_MOD_FKEYS
#define IS_CTRL(n) ((n) < 0x20 || ((n) >= 0x7f && (n) <= 0x9f))

/*
 * Return true if the keysym corresponds to one of the control characters,
 * or one of the common ASCII characters that is combined with control to
 * make a control character.
 */
static Bool
IsControlInput(KEY_DATA * kd)
{
    return ((kd->keysym) >= 0x40 && (kd->keysym) <= 0x7f);
}

static Bool
IsControlOutput(KEY_DATA * kd)
{
    return IS_CTRL(kd->keysym);
}

/*
 * X "normally" has some built-in translations, which the user may want to
 * suppress when processing the modifyOtherKeys resource.  In particular, the
 * control modifier applied to some of the keyboard digits gives results for
 * control characters.
 *
 * control 2   0    NUL
 * control SPC 0    NUL
 * control @   0    NUL
 * control `   0    NUL
 * control 3   0x1b ESC
 * control 4   0x1c FS
 * control \   0x1c FS
 * control 5   0x1d GS
 * control 6   0x1e RS
 * control ^   0x1e RS
 * control ~   0x1e RS
 * control 7   0x1f US
 * control /   0x1f US
 * control _   0x1f US
 * control 8   0x7f DEL
 *
 * It is possible that some other keyboards do not work for these combinations,
 * but they do work with modifyOtherKeys=2 for the US keyboard:
 *
 * control `   0    NUL
 * control [   0x1b ESC
 * control \   0x1c FS
 * control ]   0x1d GS
 * control ?   0x7f DEL
 */
static Bool
IsControlAlias(KEY_DATA * kd)
{
    Bool result = False;

    if (kd->nbytes == 1) {
	result = IS_CTRL(CharOf(kd->strbuf[0]));
    }
    return result;
}

/*
 * If we are in the non-VT220/VT52 keyboard state, allow modifiers to add a
 * parameter to the function-key control sequences.
 *
 * Note that we generally cannot capture the Shift-modifier for the numeric
 * keypad since this is commonly used to act as a type of NumLock, e.g.,
 * making the keypad send "7" (actually XK_KP_7) where the unshifted code
 * would be Home (XK_KP_Home).  The other modifiers work, subject to the
 * usual window-manager assignments.
 */
static Bool
allowModifierParm(XtermWidget xw, KEY_DATA * kd)
{
    TKeyboard *keyboard = &(xw->keyboard);
    TScreen *screen = &(xw->screen);
    int keypad_mode = ((keyboard->flags & MODE_DECKPAM) != 0);

    Bool result = False;

    (void) screen;
    if (!(IsKeypadKey(kd->keysym) && keypad_mode)
#if OPT_SUNPC_KBD
	&& keyboard->type != keyboardIsVT220
#endif
#if OPT_VT52_MODE
	&& screen->vtXX_level != 0
#endif
	) {
	result = True;
    }
    return result;
}

/*
* Modifier codes:
*       None                  1
*       Shift                 2 = 1(None)+1(Shift)
*       Alt                   3 = 1(None)+2(Alt)
*       Alt+Shift             4 = 1(None)+1(Shift)+2(Alt)
*       Ctrl                  5 = 1(None)+4(Ctrl)
*       Ctrl+Shift            6 = 1(None)+1(Shift)+4(Ctrl)
*       Ctrl+Alt              7 = 1(None)+2(Alt)+4(Ctrl)
*       Ctrl+Alt+Shift        8 = 1(None)+1(Shift)+2(Alt)+4(Ctrl)
*       Meta                  9 = 1(None)+8(Meta)
*       Meta+Shift           10 = 1(None)+8(Meta)+1(Shift)
*       Meta+Alt             11 = 1(None)+8(Meta)+2(Alt)
*       Meta+Alt+Shift       12 = 1(None)+8(Meta)+1(Shift)+2(Alt)
*       Meta+Ctrl            13 = 1(None)+8(Meta)+4(Ctrl)
*       Meta+Ctrl+Shift      14 = 1(None)+8(Meta)+1(Shift)+4(Ctrl)
*       Meta+Ctrl+Alt        15 = 1(None)+8(Meta)+2(Alt)+4(Ctrl)
*       Meta+Ctrl+Alt+Shift  16 = 1(None)+8(Meta)+1(Shift)+2(Alt)+4(Ctrl)
*/
#define	UNMOD	1
#define	SHIFT	1
#define	ALT	2
#define	CTRL	4
#define	META	8
#define MODIFIER_NAME(parm, name) (((parm - UNMOD) & name) ? " "#name : "")
static short
computeModifierParm(XtermWidget xw, int state)
{
    short modify_parm = UNMOD;

#if OPT_NUM_LOCK
    if ((state & xw->misc.other_mods) == 0) {
	if (state & ShiftMask) {
	    modify_parm += SHIFT;
	    state &= ~ShiftMask;
	}
	if (state & ControlMask) {
	    modify_parm += CTRL;
	    state &= ~ControlMask;
	}
	if ((state & xw->misc.alt_mods) != 0) {
	    modify_parm += ALT;
	    state &= ~xw->misc.alt_mods;
	}
	if ((state & xw->misc.meta_mods) != 0) {
	    modify_parm += META;
	    state &= ~xw->misc.meta_mods;
	}
    }
#else
    (void) xw;
    (void) state;
#endif
    TRACE(("...computeModifierParm %d%s%s%s%s\n", modify_parm,
	   MODIFIER_NAME(modify_parm, SHIFT),
	   MODIFIER_NAME(modify_parm, ALT),
	   MODIFIER_NAME(modify_parm, CTRL),
	   MODIFIER_NAME(modify_parm, META)));
    return modify_parm;
}

/*
 * Single characters (not function-keys) are allowed fewer modifiers when
 * interpreting modifyOtherKeys due to pre-existing associations with some
 * modifiers.
 */
static unsigned
allowedCharModifiers(XtermWidget xw, unsigned state, KEY_DATA * kd)
{
#if OPT_NUM_LOCK
    unsigned a_or_m = (state & (xw->misc.meta_mods | xw->misc.alt_mods));
#else
    unsigned a_or_m = 0;
#endif
    /*
     * Start by limiting the result to the modifiers we might want to use.
     */
    unsigned result = (state & (ControlMask
				| ShiftMask
				| a_or_m));

    /*
     * If modifyOtherKeys is off or medium (0 or 1), moderate its effects by
     * excluding the common cases for modifiers.
     */
    if (xw->keyboard.modify_now.other_keys <= 1) {
	if (IsControlInput(kd)
	    && (result & ~ControlMask) == 0) {
	    /* These keys are already associated with the control-key */
	    if (xw->keyboard.modify_now.other_keys == 0) {
		result &= ~ControlMask;
	    }
	} else if (kd->keysym == XK_Tab || kd->keysym == XK_Return) {
	    ;
	} else if (IsControlAlias(kd)) {
	    /* Things like "^_" work here... */
	    if ((result & ~(ControlMask | ShiftMask)) == 0) {
		result = 0;
	    }
	} else if (!IsControlOutput(kd) && !IsPredefinedKey(kd->keysym)) {
	    /* Printable keys are already associated with the shift-key */
	    if (!(result & ControlMask)) {
		result &= ~ShiftMask;
	    }
	}
#if OPT_NUM_LOCK
	if ((result & xw->misc.meta_mods) != 0) {
	    /*
	     * metaSendsEscape makes the meta key independent of
	     * modifyOtherKeys.
	     */
	    if (xw->screen.meta_sends_esc) {
		result &= ~xw->misc.meta_mods;
	    }
	    /*
	     * A bare meta-modifier is independent of modifyOtherKeys.  If it
	     * is combined with other modifiers, make it depend.
	     */
	    if ((result & ~(xw->misc.meta_mods)) == 0) {
		result &= ~xw->misc.meta_mods;
	    }
	    /*
	     * Check for special cases of control+meta which are used by some
	     * applications, e.g., emacs.
	     */
	    if ((IsControlInput(kd)
		 || IsControlOutput(kd))
		&& (result & ControlMask) != 0) {
		result &= ~(xw->misc.meta_mods | ControlMask);
	    }
	    if (kd->keysym == XK_Return || kd->keysym == XK_Tab) {
		result &= ~(xw->misc.meta_mods | ControlMask);
	    }
	}
#endif
    }
    TRACE(("...allowedCharModifiers(state=%u" FMT_MODIFIER_NAMES
	   ", ch=" KEYSYM_FMT ") ->"
	   "%u" FMT_MODIFIER_NAMES "\n",
	   state, ARG_MODIFIER_NAMES(state), kd->keysym,
	   result, ARG_MODIFIER_NAMES(result)));
    return result;
}

/*
 * Decide if we should generate a special escape sequence for "other" keys
 * than cursor-, function-keys, etc., as per the modifyOtherKeys resource.
 */
static Bool
ModifyOtherKeys(XtermWidget xw,
		unsigned state,
		KEY_DATA * kd,
		int modify_parm)
{
    TKeyboard *keyboard = &(xw->keyboard);
    Bool result = False;

    /*
     * Exclude the keys already covered by a modifier.
     */
    if (kd->is_fkey
	|| IsEditFunctionKey(kd->keysym)
	|| IsKeypadKey(kd->keysym)
	|| IsCursorKey(kd->keysym)
	|| IsPFKey(kd->keysym)
	|| IsMiscFunctionKey(kd->keysym)
	|| IsPrivateKeypadKey(kd->keysym)
#if OPT_NUM_LOCK
	|| (state & xw->misc.other_mods) != 0
#endif
	) {
	result = False;
    } else if (modify_parm != 0) {
	if (IsBackarrowToggle(keyboard, kd->keysym, state)) {
	    kd->keysym = XK_Delete;
	    state &= ~ControlMask;
	}
	if (!IsPredefinedKey(kd->keysym)) {
	    state = allowedCharModifiers(xw, state, kd);
	}
	if (state != 0) {
	    switch (keyboard->modify_now.other_keys) {
	    default:
		break;
	    case 1:
		switch (kd->keysym) {
		case XK_BackSpace:
		case XK_Delete:
		    result = False;
		    break;
#ifdef XK_ISO_Left_Tab
		case XK_ISO_Left_Tab:
		    if (computeModifierParm(xw, state & ~ShiftMask) > 1)
			result = True;
		    break;
#endif
		case XK_Return:
		case XK_Tab:
		    result = (modify_parm > 1);
		    break;
		default:
		    if (IsControlInput(kd)) {
			if (state == ControlMask || state == ShiftMask) {
			    result = False;
			} else {
			    result = (modify_parm > 1);
			}
		    } else if (IsControlAlias(kd)) {
			if (state == ShiftMask)
			    result = False;
			else if (computeModifierParm(xw,
						     (state & ~ControlMask))
				 > 1) {
			    result = True;
			}
		    } else {
			result = True;
		    }
		    break;
		}
		break;
	    case 2:
		switch (kd->keysym) {
		case XK_BackSpace:
		    /* strip ControlMask as per IsBackarrowToggle() */
		    if (computeModifierParm(xw, state & ~ControlMask) > 1)
			result = True;
		    break;
		case XK_Delete:
		    result = (computeModifierParm(xw, state) > 1);
		    break;
#ifdef XK_ISO_Left_Tab
		case XK_ISO_Left_Tab:
		    if (computeModifierParm(xw, state & ~ShiftMask) > 1)
			result = True;
		    break;
#endif
		case XK_Return:
		case XK_Tab:
		    result = (modify_parm > 1);
		    break;
		default:
		    if (IsControlInput(kd)) {
			result = True;
		    } else if (state == ShiftMask) {
			result = (kd->keysym == ' ' || kd->keysym == XK_Return);
		    } else if (computeModifierParm(xw, state & ~ShiftMask) > 1) {
			result = True;
		    }
		    break;
		}
		break;
	    }
	}
    }
    TRACE(("...ModifyOtherKeys(%d,%d) %s\n",
	   keyboard->modify_now.other_keys,
	   modify_parm,
	   BtoS(result)));
    return result;
}

#define APPEND_PARM(number) \
	    reply->a_param[(int) reply->a_nparam] = number, \
	    reply->a_nparam += 1

/*
 * Function-key code 27 happens to not be used in the vt220-style encoding.
 * xterm uses this to represent modified non-function-keys such as control/+ in
 * the Sun/PC keyboard layout.  See the modifyOtherKeys resource in the manpage
 * for more information.
 */
static Bool
modifyOtherKey(ANSI * reply, int input_char, int modify_parm)
{
    Bool result = False;

    if (input_char >= 0) {
	reply->a_type = CSI;
	APPEND_PARM(27);
	APPEND_PARM(modify_parm);
	APPEND_PARM(input_char);
	reply->a_final = '~';

	result = True;
    }
    return result;
}

static void
modifyCursorKey(ANSI * reply, int modify, int *modify_parm)
{
    if (*modify_parm > 1) {
	if (modify < 0) {
	    *modify_parm = 0;
	}
	if (modify > 0) {
	    reply->a_type = CSI;	/* SS3 should not have params */
	}
	if (modify > 1 && reply->a_nparam == 0) {
	    APPEND_PARM(1);	/* force modifier to 2nd param */
	}
	if (modify > 2) {
	    reply->a_pintro = '>';	/* mark this as "private" */
	}
    }
}
#else
#define modifyCursorKey(reply, modify, parm)	/* nothing */
#endif /* OPT_MOD_FKEYS */

#if OPT_SUNPC_KBD
/*
 * If we have told xterm that our keyboard is really a Sun/PC keyboard, this is
 * enough to make a reasonable approximation to DEC vt220 numeric and editing
 * keypads.
 */
static KeySym
TranslateFromSUNPC(KeySym keysym)
{
    /* *INDENT-OFF* */
    static struct {
	    KeySym before, after;
    } table[] = {
#ifdef DXK_Remove
	{ XK_Delete,       DXK_Remove },
#endif
	{ XK_Home,         XK_Find },
	{ XK_End,          XK_Select },
#ifdef XK_KP_Home
	{ XK_Delete,       XK_KP_Decimal },
	{ XK_KP_Delete,    XK_KP_Decimal },
	{ XK_KP_Insert,    XK_KP_0 },
	{ XK_KP_End,       XK_KP_1 },
	{ XK_KP_Down,      XK_KP_2 },
	{ XK_KP_Next,      XK_KP_3 },
	{ XK_KP_Left,      XK_KP_4 },
	{ XK_KP_Begin,     XK_KP_5 },
	{ XK_KP_Right,     XK_KP_6 },
	{ XK_KP_Home,      XK_KP_7 },
	{ XK_KP_Up,        XK_KP_8 },
	{ XK_KP_Prior,     XK_KP_9 },
#endif
    };
    /* *INDENT-ON* */

    unsigned n;

    for (n = 0; n < sizeof(table) / sizeof(table[0]); n++) {
	if (table[n].before == keysym) {
	    TRACE(("...Input keypad before was " KEYSYM_FMT "\n", keysym));
	    keysym = table[n].after;
	    TRACE(("...Input keypad changed to " KEYSYM_FMT "\n", keysym));
	    break;
	}
    }
    return keysym;
}
#endif /* OPT_SUNPC_KBD */

#define VT52_KEYPAD \
	if_OPT_VT52_MODE(screen,{ \
		reply.a_type = ESC; \
		reply.a_pintro = '?'; \
		})

#define VT52_CURSOR_KEYS \
	if_OPT_VT52_MODE(screen,{ \
		reply.a_type = ESC; \
		})

#undef  APPEND_PARM
#define APPEND_PARM(number) \
	    reply.a_param[(int) reply.a_nparam] = number, \
	    reply.a_nparam += 1

#if OPT_MOD_FKEYS
#define MODIFIER_PARM \
	if (modify_parm > 1) APPEND_PARM(modify_parm)
#else
#define MODIFIER_PARM		/*nothing */
#endif

/*
 * Determine if we use the \E[3~ sequence for Delete, or the legacy ^?.  We
 * maintain the delete_is_del value as 3 states:  unspecified(2), true and
 * false.  If unspecified, it is handled differently according to whether the
 * legacy keyboard support is enabled, or if xterm emulates a VT220.
 *
 * Once the user (or application) has specified delete_is_del via resource
 * setting, popup menu or escape sequence, it overrides the keyboard type
 * rather than the reverse.
 */
Bool
xtermDeleteIsDEL(XtermWidget xw)
{
    Bool result = True;

    if (xw->keyboard.type == keyboardIsDefault
	|| xw->keyboard.type == keyboardIsVT220)
	result = (xw->screen.delete_is_del == True);

    if (xw->keyboard.type == keyboardIsLegacy)
	result = (xw->screen.delete_is_del != False);

    TRACE(("xtermDeleteIsDEL(%d/%d) = %d\n",
	   xw->keyboard.type,
	   xw->screen.delete_is_del,
	   result));

    return result;
}

void
Input(XtermWidget xw,
      XKeyEvent * event,
      Bool eightbit)
{
    Char *string;

    TKeyboard *keyboard = &(xw->keyboard);
    TScreen *screen = &(xw->screen);

    int j;
    int key = False;
    ANSI reply;
    int dec_code;
    int modify_parm = 0;
    int keypad_mode = ((keyboard->flags & MODE_DECKPAM) != 0);
    unsigned evt_state = event->state;
    unsigned mod_state;
    KEY_DATA kd;

    /* Ignore characters typed at the keyboard */
    if (keyboard->flags & MODE_KAM)
	return;

    kd.keysym = 0;
    kd.is_fkey = False;
#if OPT_TCAP_QUERY
    if (screen->tc_query_code >= 0) {
	kd.keysym = screen->tc_query_code;
	kd.is_fkey = screen->tc_query_fkey;
	if (kd.keysym != XK_BackSpace) {
	    kd.nbytes = 0;
	    kd.strbuf[0] = 0;
	} else {
	    kd.nbytes = 1;
	    kd.strbuf[0] = 8;
	}
    } else
#endif
    {
#if OPT_I18N_SUPPORT
	if (screen->xic) {
	    Status status_return;
#if OPT_WIDE_CHARS
	    if (screen->utf8_mode) {
		kd.nbytes = Xutf8LookupString(screen->xic, event,
					      kd.strbuf, sizeof(kd.strbuf),
					      &kd.keysym, &status_return);
	    } else
#endif
	    {
		kd.nbytes = XmbLookupString(screen->xic, event,
					    kd.strbuf, sizeof(kd.strbuf),
					    &kd.keysym, &status_return);
	    }
#if OPT_MOD_FKEYS
	    /*
	     * Fill-in some code useful with IsControlAlias():
	     */
	    if (status_return == XLookupBoth
		&& kd.nbytes <= 1
		&& !IsPredefinedKey(kd.keysym)
		&& (keyboard->modify_now.other_keys > 1)
		&& !IsControlInput(&kd)) {
		kd.nbytes = 1;
		kd.strbuf[0] = kd.keysym;
	    }
#endif /* OPT_MOD_FKEYS */
	} else
#endif /* OPT_I18N_SUPPORT */
	{
	    static XComposeStatus compose_status =
	    {NULL, 0};
	    kd.nbytes = XLookupString(event, kd.strbuf, sizeof(kd.strbuf),
				      &kd.keysym, &compose_status);
	}
	kd.is_fkey = IsFunctionKey(kd.keysym);
    }

    memset(&reply, 0, sizeof(reply));

    TRACE(("Input keysym "
	   KEYSYM_FMT
	   ", %d:'%s'%s" FMT_MODIFIER_NAMES "%s%s%s%s%s%s\n",
	   kd.keysym,
	   kd.nbytes,
	   visibleChars(PAIRED_CHARS((Char *) kd.strbuf, 0), kd.nbytes),
	   ARG_MODIFIER_NAMES(evt_state),
	   eightbit ? " 8bit" : " 7bit",
	   IsKeypadKey(kd.keysym) ? " KeypadKey" : "",
	   IsCursorKey(kd.keysym) ? " CursorKey" : "",
	   IsPFKey(kd.keysym) ? " PFKey" : "",
	   kd.is_fkey ? " FKey" : "",
	   IsMiscFunctionKey(kd.keysym) ? " MiscFKey" : "",
	   IsEditFunctionKey(kd.keysym) ? " EditFkey" : ""));

#if OPT_SUNPC_KBD
    /*
     * DEC keyboards don't have keypad(+), but do have keypad(,) instead.
     * Other (Sun, PC) keyboards commonly have keypad(+), but no keypad(,)
     * - it's a pain for users to work around.
     */
    if (keyboard->type == keyboardIsVT220
	&& (evt_state & ShiftMask) == 0) {
	if (kd.keysym == XK_KP_Add) {
	    kd.keysym = XK_KP_Separator;
	    evt_state &= ~ShiftMask;
	    TRACE(("...Input keypad(+), change keysym to "
		   KEYSYM_FMT
		   "\n",
		   kd.keysym));
	}
	if ((evt_state & ControlMask) != 0
	    && kd.keysym == XK_KP_Separator) {
	    kd.keysym = XK_KP_Subtract;
	    evt_state &= ~ControlMask;
	    TRACE(("...Input control/keypad(,), change keysym to "
		   KEYSYM_FMT
		   "\n",
		   kd.keysym));
	}
    }
#endif

    /*
     * The keyboard tables may give us different keypad codes according to
     * whether NumLock is pressed.  Use this check to simplify the process
     * of determining whether we generate an escape sequence for a keypad
     * key, or force it to the value kypd_num[].  There is no fixed
     * modifier for this feature, so we assume that it is the one assigned
     * to the NumLock key.
     *
     * This check used to try to return the contents of strbuf, but that
     * does not work properly when a control modifier is given (trash is
     * returned in the buffer in some cases -- perhaps an X bug).
     */
#if OPT_NUM_LOCK
    if (kd.nbytes == 1
	&& IsKeypadKey(kd.keysym)
	&& xw->misc.real_NumLock
	&& (xw->misc.num_lock & evt_state) != 0) {
	keypad_mode = 0;
	TRACE(("...Input num_lock, force keypad_mode off\n"));
    }
#endif

#if OPT_MOD_FKEYS
    if (evt_state != 0
	&& allowModifierParm(xw, &kd)) {
	modify_parm = computeModifierParm(xw, evt_state);
    }

    /*
     * Shift-tab is often mapped to XK_ISO_Left_Tab which is classified as
     * IsEditFunctionKey(), and the conversion does not produce any bytes.
     * Check for this special case so we have data when handling the
     * modifyOtherKeys resource.
     */
    if (keyboard->modify_now.other_keys > 1) {
	if (IsTabKey(kd.keysym) && kd.nbytes == 0) {
	    kd.nbytes = 1;
	    kd.strbuf[0] = '\t';
	}
    }
#endif /* OPT_MOD_FKEYS */

    /* VT300 & up: backarrow toggle */
    if ((kd.nbytes == 1)
	&& IsBackarrowToggle(keyboard, kd.keysym, evt_state)) {
	kd.strbuf[0] = DEL;
	TRACE(("...Input backarrow changed to %d\n", kd.strbuf[0]));
    }
#if OPT_SUNPC_KBD
    /* make an DEC editing-keypad from a Sun or PC editing-keypad */
    if (keyboard->type == keyboardIsVT220
	&& (kd.keysym != XK_Delete || !xtermDeleteIsDEL(xw)))
	kd.keysym = TranslateFromSUNPC(kd.keysym);
    else
#endif
    {
#ifdef XK_KP_Home
	if (kd.keysym >= XK_KP_Home && kd.keysym <= XK_KP_Begin) {
	    TRACE(("...Input keypad before was " KEYSYM_FMT "\n", kd.keysym));
	    kd.keysym += XK_Home - XK_KP_Home;
	    TRACE(("...Input keypad changed to " KEYSYM_FMT "\n", kd.keysym));
	}
#endif
    }

    /*
     * Map the Sun afterthought-keys in as F36 and F37.
     */
#ifdef SunXK_F36
    if (!kd.is_fkey) {
	if (kd.keysym == SunXK_F36) {
	    kd.keysym = XK_Fn(36);
	    kd.is_fkey = True;
	}
	if (kd.keysym == SunXK_F37) {
	    kd.keysym = XK_Fn(37);
	    kd.is_fkey = True;
	}
    }
#endif

    /*
     * Use the control- and shift-modifiers to obtain more function keys than
     * the keyboard provides.  We can do this if there is no conflicting use of
     * those modifiers:
     *
     * a) for VT220 keyboard, we use only the control-modifier.  The keyboard
     * uses shift-modifier for UDK's.
     *
     * b) for non-VT220 keyboards, we only have to check if the
     * modifyFunctionKeys resource is inactive.
     *
     * Thereafter, we note when we have a function-key and keep that
     * distinction when testing for "function-key" values.
     */
    if ((evt_state & (ControlMask | ShiftMask)) != 0
	&& kd.is_fkey) {

	/* VT220 keyboard uses shift for UDK */
	if (keyboard->type == keyboardIsVT220
	    || keyboard->type == keyboardIsLegacy) {

	    TRACE(("...map XK_F%ld", kd.keysym - XK_Fn(1) + 1));
	    if (evt_state & ControlMask) {
		kd.keysym += xw->misc.ctrl_fkeys;
		evt_state &= ~ControlMask;
	    }
	    TRACE((" to XK_F%ld\n", kd.keysym - XK_Fn(1) + 1));

	}
#if OPT_MOD_FKEYS
	else if (keyboard->modify_now.function_keys < 0) {

	    TRACE(("...map XK_F%ld", kd.keysym - XK_Fn(1) + 1));
	    if (evt_state & ShiftMask) {
		kd.keysym += xw->misc.ctrl_fkeys * 1;
		evt_state &= ~ShiftMask;
	    }
	    if (evt_state & ControlMask) {
		kd.keysym += xw->misc.ctrl_fkeys * 2;
		evt_state &= ~ControlMask;
	    }
	    TRACE((" to XK_F%ld\n", kd.keysym - XK_Fn(1) + 1));

	}
	/*
	 * Reevaluate the modifier parameter, stripping off the modifiers
	 * that we just used.
	 */
	if (modify_parm)
	    modify_parm = computeModifierParm(xw, evt_state);
#endif /* OPT_MOD_FKEYS */
    }

    /*
     * Test for one of the keyboard variants.
     */
    switch (keyboard->type) {
    case keyboardIsHP:
	hpfuncvalue(&reply, &kd);
	break;
    case keyboardIsSCO:
	scofuncvalue(&reply, &kd);
	break;
    case keyboardIsSun:
	sunfuncvalue(&reply, &kd);
	break;
    case keyboardIsDefault:
    case keyboardIsLegacy:
    case keyboardIsVT220:
	break;
    }

    if (reply.a_final) {
	/*
	 * The key symbol matches one of the variants.  Most of those are
	 * function-keys, though some cursor- and editing-keys are mixed in.
	 */
	modifyCursorKey(&reply,
			((kd.is_fkey
			  || IsMiscFunctionKey(kd.keysym)
			  || IsEditFunctionKey(kd.keysym))
			 ? keyboard->modify_now.function_keys
			 : keyboard->modify_now.cursor_keys),
			&modify_parm);
	MODIFIER_PARM;
	unparseseq(xw, &reply);
    } else if (((kd.is_fkey
		 || IsMiscFunctionKey(kd.keysym)
		 || IsEditFunctionKey(kd.keysym))
#if OPT_MOD_FKEYS
		&& !ModifyOtherKeys(xw, evt_state, &kd, modify_parm)
#endif
	       ) || (kd.keysym == XK_Delete
		     && ((modify_parm > 1)
			 || !xtermDeleteIsDEL(xw)))) {
	dec_code = decfuncvalue(&kd);
	if ((evt_state & ShiftMask)
#if OPT_SUNPC_KBD
	    && keyboard->type == keyboardIsVT220
#endif
	    && ((string = (Char *) udk_lookup(dec_code, &kd.nbytes)) != 0)) {
	    evt_state &= ~ShiftMask;
	    while (kd.nbytes-- > 0)
		unparseputc(xw, CharOf(*string++));
	}
#if OPT_VT52_MODE
	/*
	 * Interpret F1-F4 as PF1-PF4 for VT52, VT100
	 */
	else if (keyboard->type != keyboardIsLegacy
		 && (dec_code >= 11 && dec_code <= 14)) {
	    reply.a_type = SS3;
	    VT52_CURSOR_KEYS;
	    reply.a_final = A2E(dec_code - 11 + E2A('P'));
	    modifyCursorKey(&reply,
			    keyboard->modify_now.function_keys,
			    &modify_parm);
	    MODIFIER_PARM;
	    unparseseq(xw, &reply);
	}
#endif
	else {
	    reply.a_type = CSI;
	    reply.a_final = 0;

#ifdef XK_ISO_Left_Tab
	    if (kd.keysym == XK_ISO_Left_Tab) {
		reply.a_nparam = 0;
		reply.a_final = 'Z';
#if OPT_MOD_FKEYS
		if (keyboard->modify_now.other_keys > 1
		    && computeModifierParm(xw, evt_state & ~ShiftMask) > 1)
		    modifyOtherKey(&reply, '\t', modify_parm);
#endif
	    } else
#endif /* XK_ISO_Left_Tab */
	    {
		reply.a_nparam = 1;
#if OPT_MOD_FKEYS
		if (kd.is_fkey) {
		    modifyCursorKey(&reply,
				    keyboard->modify_now.function_keys,
				    &modify_parm);
		}
		MODIFIER_PARM;
#endif
		reply.a_param[0] = dec_code;
		reply.a_final = '~';
	    }
	    if (reply.a_final != 0
		&& (reply.a_nparam == 0 || reply.a_param[0] >= 0))
		unparseseq(xw, &reply);
	}
	key = True;
    } else if (IsPFKey(kd.keysym)) {
	reply.a_type = SS3;
	reply.a_final = kd.keysym - XK_KP_F1 + 'P';
	VT52_CURSOR_KEYS;
	MODIFIER_PARM;
	unparseseq(xw, &reply);
	key = True;
    } else if (IsKeypadKey(kd.keysym)) {
	if (keypad_mode) {
	    reply.a_type = SS3;
	    reply.a_final = kypd_apl[kd.keysym - XK_KP_Space];
	    VT52_KEYPAD;
	    MODIFIER_PARM;
	    unparseseq(xw, &reply);
	} else {
	    unparseputc(xw, kypd_num[kd.keysym - XK_KP_Space]);
	}
	key = True;
    } else if (IsCursorKey(kd.keysym)) {
	if (keyboard->flags & MODE_DECCKM) {
	    reply.a_type = SS3;
	} else {
	    reply.a_type = CSI;
	}
	modifyCursorKey(&reply, keyboard->modify_now.cursor_keys, &modify_parm);
	reply.a_final = curfinal[kd.keysym - XK_Home];
	VT52_CURSOR_KEYS;
	MODIFIER_PARM;
	unparseseq(xw, &reply);
	key = True;
    } else if (kd.nbytes > 0) {
	int prefix = 0;

#if OPT_TEK4014
	if (TEK4014_GIN(tekWidget)) {
	    TekEnqMouse(tekWidget, kd.strbuf[0]);
	    TekGINoff(tekWidget);
	    kd.nbytes--;
	    for (j = 0; j < kd.nbytes; ++j) {
		kd.strbuf[j] = kd.strbuf[j + 1];
	    }
	}
#endif
#if OPT_MOD_FKEYS
	if ((keyboard->modify_now.other_keys > 0)
	    && ModifyOtherKeys(xw, evt_state, &kd, modify_parm)
	    && (mod_state = allowedCharModifiers(xw, evt_state, &kd)) != 0) {
	    int input_char;

	    evt_state = mod_state;

	    modify_parm = computeModifierParm(xw, evt_state);

	    /*
	     * We want to show a keycode that corresponds to the 8-bit value
	     * of the key.  If the keysym is less than 256, that is good
	     * enough.  Special keys such as Tab may result in a value that
	     * is usable as well.  For the latter (special cases), try to use
	     * the result from the X library lookup.
	     */
	    input_char = ((kd.keysym < 256)
			  ? (int) kd.keysym
			  : ((kd.nbytes == 1)
			     ? CharOf(kd.strbuf[0])
			     : -1));

	    TRACE(("...modifyOtherKeys %d;%d\n", modify_parm, input_char));
	    if (modifyOtherKey(&reply, input_char, modify_parm)) {
		unparseseq(xw, &reply);
	    } else {
		Bell(XkbBI_MinorError, 0);
	    }
	} else
#endif /* OPT_MOD_FKEYS */
	{
#if OPT_NUM_LOCK
	    /*
	     * Send ESC if we have a META modifier and metaSendsEcape is true.
	     * Like eightBitInput, except that it is not associated with
	     * terminal settings.
	     */
	    if (kd.nbytes != 0
		&& screen->meta_sends_esc
		&& (evt_state & xw->misc.meta_mods) != 0) {
		TRACE(("...input-char is modified by META\n"));
		/*
		 * If we cannot distinguish between the Alt/Meta keys, disallow
		 * the corresponding shift for eightBitInput that would happen
		 * in the next chunk of code.
		 */
		if ((evt_state & xw->misc.alt_mods & xw->misc.meta_mods) != 0)
		    eightbit = False;
		prefix = ESC;
		evt_state &= ~xw->misc.meta_mods;
	    }
#endif
	    /*
	     * If metaSendsEscape is false, fall through to this chunk, which
	     * implements the eightBitInput resource.
	     *
	     * It is normally executed when the user presses Meta plus a
	     * printable key, e.g., Meta+space.  The presence of the Meta
	     * modifier is not guaranteed since what really happens is the
	     * "insert-eight-bit" or "insert-seven-bit" action, which we
	     * distinguish by the eightbit parameter to this function.  So the
	     * eightBitInput resource really means that we use this shifting
	     * logic in the "insert-eight-bit" action.
	     */
	    if (eightbit && (kd.nbytes == 1) && screen->input_eight_bits) {
		IChar ch = CharOf(kd.strbuf[0]);
		if (ch < 128) {
		    kd.strbuf[0] |= 0x80;
		    TRACE(("...input shift from %d to %d (%#x to %#x)\n",
			   ch, CharOf(kd.strbuf[0]),
			   ch, CharOf(kd.strbuf[0])));
#if OPT_WIDE_CHARS
		    if (screen->utf8_mode) {
			/*
			 * We could interpret the incoming code as "in the
			 * current locale", but it's simpler to treat it as
			 * a Unicode value to translate to UTF-8.
			 */
			ch = CharOf(kd.strbuf[0]);
			kd.nbytes = 2;
			kd.strbuf[0] = 0xc0 | ((ch >> 6) & 0x3);
			kd.strbuf[1] = 0x80 | (ch & 0x3f);
			TRACE(("...encoded %#x in UTF-8 as %#x,%#x\n",
			       ch, CharOf(kd.strbuf[0]), CharOf(kd.strbuf[1])));
		    }
#endif
		}
		eightbit = False;
	    }
#if OPT_WIDE_CHARS
	    if (kd.nbytes == 1)	/* cannot do NRC on UTF-8, for instance */
#endif
	    {
		/* VT220 & up: National Replacement Characters */
		if ((xw->flags & NATIONAL) != 0) {
		    int cmp = xtermCharSetIn(CharOf(kd.strbuf[0]),
					     screen->keyboard_dialect[0]);
		    TRACE(("...input NRC %d, %s %d\n",
			   CharOf(kd.strbuf[0]),
			   (CharOf(kd.strbuf[0]) == cmp)
			   ? "unchanged"
			   : "changed to",
			   CharOf(cmp)));
		    kd.strbuf[0] = cmp;
		} else if (eightbit) {
		    prefix = ESC;
		} else if (kd.strbuf[0] == '?'
			   && (evt_state & ControlMask) != 0) {
		    kd.strbuf[0] = DEL;
		    evt_state &= ~ControlMask;
		}
	    }
	    if (prefix != 0)
		unparseputc(xw, prefix);	/* escape */
	    for (j = 0; j < kd.nbytes; ++j)
		unparseputc(xw, CharOf(kd.strbuf[j]));
	}
	key = True;
    }
    unparse_end(xw);

    if (key && !TEK4014_ACTIVE(xw))
	AdjustAfterInput(xw);

    return;
}

void
StringInput(XtermWidget xw, Char * string, size_t nbytes)
{
    TRACE(("InputString (%s,%d)\n",
	   visibleChars(PAIRED_CHARS(string, 0), nbytes),
	   nbytes));
#if OPT_TEK4014
    if (nbytes && TEK4014_GIN(tekWidget)) {
	TekEnqMouse(tekWidget, *string++);
	TekGINoff(tekWidget);
	nbytes--;
    }
#endif
    while (nbytes-- != 0)
	unparseputc(xw, *string++);
    if (!TEK4014_ACTIVE(xw))
	AdjustAfterInput(xw);
    unparse_end(xw);
}

/* These definitions are DEC-style (e.g., vt320) */
static int
decfuncvalue(KEY_DATA * kd)
{
    int result;

    if (kd->is_fkey) {
	switch (kd->keysym) {
	    MAP(XK_Fn(1), 11);
	    MAP(XK_Fn(2), 12);
	    MAP(XK_Fn(3), 13);
	    MAP(XK_Fn(4), 14);
	    MAP(XK_Fn(5), 15);
	    MAP(XK_Fn(6), 17);
	    MAP(XK_Fn(7), 18);
	    MAP(XK_Fn(8), 19);
	    MAP(XK_Fn(9), 20);
	    MAP(XK_Fn(10), 21);
	    MAP(XK_Fn(11), 23);
	    MAP(XK_Fn(12), 24);
	    MAP(XK_Fn(13), 25);
	    MAP(XK_Fn(14), 26);
	    MAP(XK_Fn(15), 28);
	    MAP(XK_Fn(16), 29);
	    MAP(XK_Fn(17), 31);
	    MAP(XK_Fn(18), 32);
	    MAP(XK_Fn(19), 33);
	    MAP(XK_Fn(20), 34);
	default:
	    /* after F20 the codes are made up and do not correspond to any
	     * real terminal.  So they are simply numbered sequentially.
	     */
	    result = 42 + (kd->keysym - XK_Fn(21));
	    break;
	}
    } else {
	switch (kd->keysym) {
	    MAP(XK_Find, 1);
	    MAP(XK_Insert, 2);
	    MAP(XK_Delete, 3);
#ifdef XK_KP_Insert
	    MAP(XK_KP_Insert, 2);
	    MAP(XK_KP_Delete, 3);
#endif
#ifdef DXK_Remove
	    MAP(DXK_Remove, 3);
#endif
	    MAP(XK_Select, 4);
	    MAP(XK_Prior, 5);
	    MAP(XK_Next, 6);
#ifdef XK_ISO_Left_Tab
	    MAP(XK_ISO_Left_Tab, 'Z');
#endif
	    MAP(XK_Help, 28);
	    MAP(XK_Menu, 29);
	default:
	    result = -1;
	    break;
	}
    }
    return result;
}

static void
hpfuncvalue(ANSI * reply, KEY_DATA * kd)
{
#if OPT_HP_FUNC_KEYS
    int result;

    if (kd->is_fkey) {
	switch (kd->keysym) {
	    MAP(XK_Fn(1), 'p');
	    MAP(XK_Fn(2), 'q');
	    MAP(XK_Fn(3), 'r');
	    MAP(XK_Fn(4), 's');
	    MAP(XK_Fn(5), 't');
	    MAP(XK_Fn(6), 'u');
	    MAP(XK_Fn(7), 'v');
	    MAP(XK_Fn(8), 'w');
	default:
	    result = -1;
	    break;
	}
    } else {
	switch (kd->keysym) {
	    MAP(XK_Up, 'A');
	    MAP(XK_Down, 'B');
	    MAP(XK_Right, 'C');
	    MAP(XK_Left, 'D');
	    MAP(XK_End, 'F');
	    MAP(XK_Clear, 'J');
	    MAP(XK_Delete, 'P');
	    MAP(XK_Insert, 'Q');
	    MAP(XK_Next, 'S');
	    MAP(XK_Prior, 'T');
	    MAP(XK_Home, 'h');
#ifdef XK_KP_Insert
	    MAP(XK_KP_Delete, 'P');
	    MAP(XK_KP_Insert, 'Q');
#endif
#ifdef DXK_Remove
	    MAP(DXK_Remove, 'P');
#endif
	    MAP(XK_Select, 'F');
	    MAP(XK_Find, 'h');
	default:
	    result = -1;
	    break;
	}
    }
    if (result > 0) {
	reply->a_type = ESC;
	reply->a_final = result;
    }
#else
    (void) reply;
    (void) kd;
#endif /* OPT_HP_FUNC_KEYS */
}

static void
scofuncvalue(ANSI * reply, KEY_DATA * kd)
{
#if OPT_SCO_FUNC_KEYS
    int result;

    if (kd->is_fkey) {
	switch (kd->keysym) {
	    MAP(XK_Fn(1), 'M');
	    MAP(XK_Fn(2), 'N');
	    MAP(XK_Fn(3), 'O');
	    MAP(XK_Fn(4), 'P');
	    MAP(XK_Fn(5), 'Q');
	    MAP(XK_Fn(6), 'R');
	    MAP(XK_Fn(7), 'S');
	    MAP(XK_Fn(8), 'T');
	    MAP(XK_Fn(9), 'U');
	    MAP(XK_Fn(10), 'V');
	    MAP(XK_Fn(11), 'W');
	    MAP(XK_Fn(12), 'X');
	    MAP(XK_Fn(13), 'Y');
	    MAP(XK_Fn(14), 'Z');
	    MAP(XK_Fn(15), 'a');
	    MAP(XK_Fn(16), 'b');
	    MAP(XK_Fn(17), 'c');
	    MAP(XK_Fn(18), 'd');
	    MAP(XK_Fn(19), 'e');
	    MAP(XK_Fn(20), 'f');
	    MAP(XK_Fn(21), 'g');
	    MAP(XK_Fn(22), 'h');
	    MAP(XK_Fn(23), 'i');
	    MAP(XK_Fn(24), 'j');
	    MAP(XK_Fn(25), 'k');
	    MAP(XK_Fn(26), 'l');
	    MAP(XK_Fn(27), 'm');
	    MAP(XK_Fn(28), 'n');
	    MAP(XK_Fn(29), 'o');
	    MAP(XK_Fn(30), 'p');
	    MAP(XK_Fn(31), 'q');
	    MAP(XK_Fn(32), 'r');
	    MAP(XK_Fn(33), 's');
	    MAP(XK_Fn(34), 't');
	    MAP(XK_Fn(35), 'u');
	    MAP(XK_Fn(36), 'v');
	    MAP(XK_Fn(37), 'w');
	    MAP(XK_Fn(38), 'x');
	    MAP(XK_Fn(39), 'y');
	    MAP(XK_Fn(40), 'z');
	    MAP(XK_Fn(41), '@');
	    MAP(XK_Fn(42), '[');
	    MAP(XK_Fn(43), '\\');
	    MAP(XK_Fn(44), ']');
	    MAP(XK_Fn(45), '^');
	    MAP(XK_Fn(46), '_');
	    MAP(XK_Fn(47), '`');
	    MAP(XK_Fn(48), '{');	/* no matching '}' */
	default:
	    result = -1;
	    break;
	}
    } else {
	switch (kd->keysym) {
	    MAP(XK_Up, 'A');
	    MAP(XK_Down, 'B');
	    MAP(XK_Right, 'C');
	    MAP(XK_Left, 'D');
	    MAP(XK_Begin, 'E');
	    MAP(XK_End, 'F');
	    MAP(XK_Insert, 'L');
	    MAP(XK_Next, 'G');
	    MAP(XK_Prior, 'I');
	    MAP(XK_Home, 'H');
#ifdef XK_KP_Insert
	    MAP(XK_KP_Insert, 'L');
#endif
	default:
	    result = -1;
	    break;
	}
    }
    if (result > 0) {
	reply->a_type = CSI;
	reply->a_final = result;
    }
#else
    (void) reply;
    (void) kd;
#endif /* OPT_SCO_FUNC_KEYS */
}

static void
sunfuncvalue(ANSI * reply, KEY_DATA * kd)
{
#ifdef OPT_SUN_FUNC_KEYS
    int result;

    if (kd->is_fkey) {
	switch (kd->keysym) {
	    /* kf1-kf20 are numbered sequentially */
	    MAP(XK_Fn(1), 224);
	    MAP(XK_Fn(2), 225);
	    MAP(XK_Fn(3), 226);
	    MAP(XK_Fn(4), 227);
	    MAP(XK_Fn(5), 228);
	    MAP(XK_Fn(6), 229);
	    MAP(XK_Fn(7), 230);
	    MAP(XK_Fn(8), 231);
	    MAP(XK_Fn(9), 232);
	    MAP(XK_Fn(10), 233);
	    MAP(XK_Fn(11), 192);
	    MAP(XK_Fn(12), 193);
	    MAP(XK_Fn(13), 194);
	    MAP(XK_Fn(14), 195);	/* kund */
	    MAP(XK_Fn(15), 196);
	    MAP(XK_Fn(16), 197);	/* kcpy */
	    MAP(XK_Fn(17), 198);
	    MAP(XK_Fn(18), 199);
	    MAP(XK_Fn(19), 200);	/* kfnd */
	    MAP(XK_Fn(20), 201);

	    /* kf31-kf36 are numbered sequentially */
	    MAP(XK_Fn(21), 208);	/* kf31 */
	    MAP(XK_Fn(22), 209);
	    MAP(XK_Fn(23), 210);
	    MAP(XK_Fn(24), 211);
	    MAP(XK_Fn(25), 212);
	    MAP(XK_Fn(26), 213);	/* kf36 */

	    /* kf37-kf47 are interspersed with keypad keys */
	    MAP(XK_Fn(27), 214);	/* khome */
	    MAP(XK_Fn(28), 215);	/* kf38 */
	    MAP(XK_Fn(29), 216);	/* kpp */
	    MAP(XK_Fn(30), 217);	/* kf40 */
	    MAP(XK_Fn(31), 218);	/* kb2 */
	    MAP(XK_Fn(32), 219);	/* kf42 */
	    MAP(XK_Fn(33), 220);	/* kend */
	    MAP(XK_Fn(34), 221);	/* kf44 */
	    MAP(XK_Fn(35), 222);	/* knp */
	    MAP(XK_Fn(36), 234);	/* kf46 */
	    MAP(XK_Fn(37), 235);	/* kf47 */
	default:
	    result = -1;
	    break;
	}
    } else {
	switch (kd->keysym) {
	    MAP(XK_Help, 196);	/* khlp */
	    MAP(XK_Menu, 197);

	    MAP(XK_Find, 1);
	    MAP(XK_Insert, 2);	/* kich1 */
	    MAP(XK_Delete, 3);
#ifdef XK_KP_Insert
	    MAP(XK_KP_Insert, 2);
	    MAP(XK_KP_Delete, 3);
#endif
#ifdef DXK_Remove
	    MAP(DXK_Remove, 3);
#endif
	    MAP(XK_Select, 4);

	    MAP(XK_Prior, 216);
	    MAP(XK_Next, 222);
	    MAP(XK_Home, 214);
	    MAP(XK_End, 220);
	    MAP(XK_Begin, 218);	/* kf41=kb2 */

	default:
	    result = -1;
	    break;
	}
    }
    if (result > 0) {
	reply->a_type = CSI;
	reply->a_nparam = 1;
	reply->a_param[0] = result;
	reply->a_final = 'z';
    } else if (IsCursorKey(kd->keysym)) {
	reply->a_type = SS3;
	reply->a_final = curfinal[kd->keysym - XK_Home];
    }
#else
    (void) reply;
    (void) kd;
#endif /* OPT_SUN_FUNC_KEYS */
}

#if OPT_NUM_LOCK
/*
 * Note that this can only retrieve translations that are given as resource
 * values; the default translations in charproc.c for example are not
 * retrievable by any interface to X.
 *
 * Also:  We can retrieve only the most-specified translation resource.  For
 * example, if the resource file specifies both "*translations" and
 * "XTerm*translations", we see only the latter.
 */
static Bool
TranslationsUseKeyword(Widget w, const char *keyword)
{
    static String data;
    static XtResource key_resources[] =
    {
	{XtNtranslations, XtCTranslations, XtRString,
	 sizeof(data), 0, XtRString, (XtPointer) NULL}
    };
    Bool result = False;

    XtGetSubresources(w,
		      (XtPointer) &data,
		      "vt100",
		      "VT100",
		      key_resources,
		      XtNumber(key_resources),
		      NULL,
		      (Cardinal) 0);

    if (data != 0) {
	char *p = data;
	int state = 0;
	int now = ' ', prv;
	TRACE(("TranslationsUseKeyword(%p):%s\n", w, p));
	while (*p != 0) {
	    prv = now;
	    now = char2lower(*p++);
	    if (now == ':'
		|| now == '!') {
		state = -1;
	    } else if (now == '\n') {
		state = 0;
	    } else if (state >= 0) {
		if (isgraph(now)
		    && now == keyword[state]) {
		    if ((state != 0
			 || !isalnum(prv))
			&& ((keyword[++state] == 0)
			    && !isalnum(CharOf(*p)))) {
			result = True;
			break;
		    }
		} else {
		    state = 0;
		}
	    }
	}
    }
    TRACE(("TranslationsUseKeyword(%p, %s) = %d\n", w, keyword, result));
    return result;
}

#define SaveMask(name)	xw->misc.name |= mask;\
			TRACE(("SaveMask(%s) %#lx (%#lx is%s modifier)\n", \
				#name, \
				xw->misc.name, mask, \
				ModifierName(mask)));
/*
 * Determine which modifier mask (if any) applies to the Num_Lock keysym.
 *
 * Also, determine which modifiers are associated with the ALT keys, so we can
 * send that information as a parameter for special keys in Sun/PC keyboard
 * mode.  However, if the ALT modifier is used in translations, we do not want
 * to confuse things by sending the parameter.
 */
void
VTInitModifiers(XtermWidget xw)
{
    int i, j, k;
    Display *dpy = XtDisplay(xw);
    XModifierKeymap *keymap = XGetModifierMapping(dpy);
    unsigned long mask;
    int min_keycode, max_keycode, keysyms_per_keycode = 0;

    if (keymap != 0) {
	KeySym *theMap;
	int keycode_count;

	TRACE(("VTInitModifiers\n"));

	XDisplayKeycodes(dpy, &min_keycode, &max_keycode);
	keycode_count = (max_keycode - min_keycode + 1);
	theMap = XGetKeyboardMapping(dpy,
				     min_keycode,
				     keycode_count,
				     &keysyms_per_keycode);

	if (theMap != 0) {
	    for (i = k = 0, mask = 1; i < 8; i++, mask <<= 1) {
		for (j = 0; j < keymap->max_keypermod; j++) {
		    KeyCode code = keymap->modifiermap[k];
		    if (code != 0) {
			KeySym keysym;
			int l = 0;
			do {
			    keysym = XKeycodeToKeysym(dpy, code, l);
			    l++;
			} while (!keysym && l < keysyms_per_keycode);
			if (keysym == XK_Num_Lock) {
			    SaveMask(num_lock);
			} else if (keysym == XK_Alt_L || keysym == XK_Alt_R) {
			    SaveMask(alt_mods);
			} else if (keysym == XK_Meta_L || keysym == XK_Meta_R) {
			    SaveMask(meta_mods);
			} else if (mask == ShiftMask
				   && (keysym == XK_Shift_L
				       || keysym == XK_Shift_R)) {
			    ;	/* ignore */
			} else if (mask == ControlMask
				   && (keysym == XK_Control_L
				       || keysym == XK_Control_R)) {
			    ;	/* ignore */
			} else if (mask == LockMask
				   && (keysym == XK_Caps_Lock)) {
			    ;	/* ignore */
			} else if (keysym == XK_Mode_switch
#ifdef XK_ISO_Level3_Shift
				   || keysym == XK_ISO_Level3_Shift
#endif
			    ) {
			    SaveMask(other_mods);
			}
		    }
		    k++;
		}
	    }
	    XFree(theMap);
	}

	/* Don't disable any mods if "alwaysUseMods" is true. */
	if (!xw->misc.alwaysUseMods) {
	    /*
	     * If the Alt modifier is used in translations, we would rather not
	     * use it to modify function-keys when NumLock is active.
	     */
	    if ((xw->misc.alt_mods != 0)
		&& (TranslationsUseKeyword(toplevel, "alt")
		    || TranslationsUseKeyword((Widget) xw, "alt"))) {
		TRACE(("ALT is used as a modifier in translations (ignore mask)\n"));
		xw->misc.alt_mods = 0;
	    }

	    /*
	     * If the Meta modifier is used in translations, we would rather not
	     * use it to modify function-keys.
	     */
	    if ((xw->misc.meta_mods != 0)
		&& (TranslationsUseKeyword(toplevel, "meta")
		    || TranslationsUseKeyword((Widget) xw, "meta"))) {
		TRACE(("META is used as a modifier in translations\n"));
		xw->misc.meta_mods = 0;
	    }
	}

	XFreeModifiermap(keymap);
    }
}
#endif /* OPT_NUM_LOCK */

#if OPT_TCAP_QUERY
static int
hex2int(int c)
{
    if (c >= '0' && c <= '9')
	return c - '0';
    if (c >= 'a' && c <= 'f')
	return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
	return c - 'A' + 10;
    return -1;
}

/*
 * Parse the termcap/terminfo name from the string, returning a positive number
 * (the keysym) if found, otherwise -1.  Update the string pointer.
 * Returns the (shift, control) state in *state.
 *
 * This does not attempt to construct control/shift modifiers to construct
 * function-key values.  Instead, it sets the *fkey flag to pass to Input()
 * and bypass the lookup of keysym altogether.
 */
int
xtermcapKeycode(XtermWidget xw, char **params, unsigned *state, Bool * fkey)
{
    /* *INDENT-OFF* */
#define DATA(tc,ti,x,y) { tc, ti, x, y }
    static struct {
	char *tc;
	char *ti;
	int code;
	unsigned state;
    } table[] = {
	/*	tcap	terminfo	keycode		masks */
	DATA(	"%1",	"khlp",		XK_Help,	0		),
	DATA(	"#1",	"kHLP",		XK_Help,	ShiftMask	),
	DATA(	"@0",	"kfnd",		XK_Find,	0		),
	DATA(	"*0",	"kFND",		XK_Find,	ShiftMask	),
	DATA(	"*6",	"kslt",		XK_Select,	0		),
	DATA(	"#6",	"kSLT",		XK_Select,	ShiftMask	),

	DATA(	"kh",	"khome",	XK_Home,	0		),
	DATA(	"#2",	"kHOM",		XK_Home,	ShiftMask	),
	DATA(	"@7",	"kend",		XK_End,		0		),
	DATA(	"*7",	"kEND",		XK_End,		ShiftMask	),

	DATA(	"kl",	"kcub1",	XK_Left,	0		),
	DATA(	"kr",	"kcuf1",	XK_Right,	0		),
	DATA(	"ku",	"kcuu1",	XK_Up,		0		),
	DATA(	"kd",	"kcud1",	XK_Down,	0		),

	DATA(	"#4",	"kLFT",		XK_Left,	ShiftMask	),
	DATA(	"%i",	"kRIT",		XK_Right,	ShiftMask	),
	DATA(	"%e",	"kPRV",		XK_Up,		ShiftMask	),
	DATA(	"%c",	"kNXT",		XK_Down,	ShiftMask	),

	DATA(	"k1",	"kf1",		XK_Fn(1),	0		),
	DATA(	"k2",	"kf2",		XK_Fn(2),	0		),
	DATA(	"k3",	"kf3",		XK_Fn(3),	0		),
	DATA(	"k4",	"kf4",		XK_Fn(4),	0		),
	DATA(	"k5",	"kf5",		XK_Fn(5),	0		),
	DATA(	"k6",	"kf6",		XK_Fn(6),	0		),
	DATA(	"k7",	"kf7",		XK_Fn(7),	0		),
	DATA(	"k8",	"kf8",		XK_Fn(8),	0		),
	DATA(	"k9",	"kf9",		XK_Fn(9),	0		),
	DATA(	"k;",	"kf10",		XK_Fn(10),	0		),

	DATA(	"F1",	"kf11",		XK_Fn(11),	0		),
	DATA(	"F2",	"kf12",		XK_Fn(12),	0		),
	DATA(	"F3",	"kf13",		XK_Fn(13),	0		),
	DATA(	"F4",	"kf14",		XK_Fn(14),	0		),
	DATA(	"F5",	"kf15",		XK_Fn(15),	0		),
	DATA(	"F6",	"kf16",		XK_Fn(16),	0		),
	DATA(	"F7",	"kf17",		XK_Fn(17),	0		),
	DATA(	"F8",	"kf18",		XK_Fn(18),	0		),
	DATA(	"F9",	"kf19",		XK_Fn(19),	0		),
	DATA(	"FA",	"kf20",		XK_Fn(20),	0		),
	DATA(	"FB",	"kf21",		XK_Fn(21),	0		),
	DATA(	"FC",	"kf22",		XK_Fn(22),	0		),
	DATA(	"FD",	"kf23",		XK_Fn(23),	0		),
	DATA(	"FE",	"kf24",		XK_Fn(24),	0		),
	DATA(	"FF",	"kf25",		XK_Fn(25),	0		),
	DATA(	"FG",	"kf26",		XK_Fn(26),	0		),
	DATA(	"FH",	"kf27",		XK_Fn(27),	0		),
	DATA(	"FI",	"kf28",		XK_Fn(28),	0		),
	DATA(	"FJ",	"kf29",		XK_Fn(29),	0		),
	DATA(	"FK",	"kf30",		XK_Fn(30),	0		),
	DATA(	"FL",	"kf31",		XK_Fn(31),	0		),
	DATA(	"FM",	"kf32",		XK_Fn(32),	0		),
	DATA(	"FN",	"kf33",		XK_Fn(33),	0		),
	DATA(	"FO",	"kf34",		XK_Fn(34),	0		),
	DATA(	"FP",	"kf35",		XK_Fn(35),	0		),

	DATA(	"FQ",	"kf36",		-36,		0		),
	DATA(	"FR",	"kf37",		-37,		0		),
	DATA(	"FS",	"kf38",		-38,		0		),
	DATA(	"FT",	"kf39",		-39,		0		),
	DATA(	"FU",	"kf40",		-40,		0		),
	DATA(	"FV",	"kf41",		-41,		0		),
	DATA(	"FW",	"kf42",		-42,		0		),
	DATA(	"FX",	"kf43",		-43,		0		),
	DATA(	"FY",	"kf44",		-44,		0		),
	DATA(	"FZ",	"kf45",		-45,		0		),
	DATA(	"Fa",	"kf46",		-46,		0		),
	DATA(	"Fb",	"kf47",		-47,		0		),
	DATA(	"Fc",	"kf48",		-48,		0		),
	DATA(	"Fd",	"kf49",		-49,		0		),
	DATA(	"Fe",	"kf50",		-50,		0		),
	DATA(	"Ff",	"kf51",		-51,		0		),
	DATA(	"Fg",	"kf52",		-52,		0		),
	DATA(	"Fh",	"kf53",		-53,		0		),
	DATA(	"Fi",	"kf54",		-54,		0		),
	DATA(	"Fj",	"kf55",		-55,		0		),
	DATA(	"Fk",	"kf56",		-56,		0		),
	DATA(	"Fl",	"kf57",		-57,		0		),
	DATA(	"Fm",	"kf58",		-58,		0		),
	DATA(	"Fn",	"kf59",		-59,		0		),
	DATA(	"Fo",	"kf60",		-60,		0		),
	DATA(	"Fp",	"kf61",		-61,		0		),
	DATA(	"Fq",	"kf62",		-62,		0		),
	DATA(	"Fr",	"kf63",		-63,		0		),

	DATA(	"K1",	"ka1",		XK_KP_Home,	0		),
	DATA(	"K4",	"kc1",		XK_KP_End,	0		),

#ifdef XK_ISO_Left_Tab
	DATA(	"kB",	"kcbt",		XK_ISO_Left_Tab, 0		),
#endif
	DATA(	"kC",	"kclr",		XK_Clear,	0		),
	DATA(	"kD",	"kdch1",	XK_Delete,	0		),
	DATA(	"kI",	"kich1",	XK_Insert,	0		),
	DATA(	"kN",	"knp",		XK_Next,	0		),
	DATA(	"kP",	"kpp",		XK_Prior,	0		),
	DATA(	"kb",	"kbs",		XK_BackSpace,	0		),
# if OPT_ISO_COLORS
	/* XK_COLORS is a fake code. */
	DATA(	"Co",	"colors",	XK_COLORS,	0		),
# endif
    };
    /* *INDENT-ON* */

    Cardinal n;
    unsigned len = 0;
    int code = -1;
#define MAX_TNAME_LEN 6
    char name[MAX_TNAME_LEN + 1];
    char *p;

    TRACE(("xtermcapKeycode(%s)\n", *params));

    /* Convert hex encoded name to ascii */
    for (p = *params; hex2int(p[0]) >= 0 && hex2int(p[1]) >= 0; p += 2) {
	if (len >= MAX_TNAME_LEN)
	    break;
	name[len++] = (hex2int(p[0]) << 4) + hex2int(p[1]);
    }
    name[len] = 0;
    *params = p;

    *state = 0;
    *fkey = False;

    if (*p == 0 || *p == ';') {
	for (n = 0; n < XtNumber(table); n++) {
	    if (!strcmp(table[n].ti, name) || !strcmp(table[n].tc, name)) {
		code = table[n].code;
		*state = table[n].state;
		if (IsFunctionKey(code)) {
		    *fkey = True;
		} else if (code < 0) {
		    *fkey = True;
		    code = XK_Fn((-code));
		}
#ifdef OPT_SUN_FUNC_KEYS
		if (*fkey && xw->keyboard.type == keyboardIsSun) {
		    int num = code - XK_Fn(0);

		    /* match function-key case in sunfuncvalue() */
		    if (num > 20) {
			if (num <= 30 || num > 47) {
			    code = -1;
			} else {
			    code -= 10;
			    switch (num) {
			    case 37:	/* khome */
			    case 39:	/* kpp */
			    case 41:	/* kb2 */
			    case 43:	/* kend */
			    case 45:	/* knp */
				code = -1;
				break;
			    }
			}
		    }
		}
#endif
		break;
	    }
	}
    }

    TRACE(("... xtermcapKeycode(%s, %u, %d) -> %#06x\n",
	   name, *state, *fkey, code));
    return code;
}
#endif
