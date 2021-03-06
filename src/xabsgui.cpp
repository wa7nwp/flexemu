/*
    xabsgui.cpp: abstract user interface for X11


    flexemu, an MC6809 emulator running FLEX
    Copyright (C) 1997-2020  W. Schwotzer

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#include <misc1.h>
#ifdef ultrix
    #include <errno.h>
#endif
#include <limits.h>

#ifdef UNIX
    // autoconf adaption for sys/wait.h
    #if HAVE_SYS_WAIT_H
        #include <sys/wait.h>
    #endif
    #ifndef WEXITSTATUS
        #define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
    #endif
    #ifndef WIFEXITED
        #define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
    #endif
#endif // ifdef UNIX

#include "xabsgui.h"

#ifdef HAVE_X11

#include "vico1.h"
#include "vico2.h"
#include "mc6809.h"
#include "inout.h"
#include "schedule.h"
#include "joystick.h"
#include "keyboard.h"

void XAbstractGui::initialize(struct sGuiOptions &x_guiOptions)
{
    AbstractGui::initialize(x_guiOptions);
    bp_input[0] = 0;
    bp_input[1] = 0;
    visual = nullptr;
    depth = 0;
    own_cmap = 0;
}

Display *XAbstractGui::getDisplay()
{
    return (Display *)nullptr;
}

Window XAbstractGui::getWindow(tWindowType)
{
    return (Window)0;
}

void XAbstractGui::set_cursor(int type /* = FLX_DEFAULT_CURSOR */)
{
    Display *dpy;

    dpy = getDisplay();

    switch (type)
    {
        case FLX_DEFAULT_CURSOR:
            XUndefineCursor(dpy, getWindow());
            break;

        case FLX_INVISIBLE_CURSOR:
        default:
            if (cursor == None)
            {
                Pixmap cursormask;
                XGCValues xgc;
                XColor dummycolour;
                GC gc;

                cursormask = XCreatePixmap(dpy, getWindow(), 1, 1, 1);
                xgc.function = GXclear;
                gc = XCreateGC(dpy, cursormask, GCFunction, &xgc);
                XFillRectangle(dpy, cursormask, gc, 0, 0, 1, 1);
                dummycolour.pixel = 0;
                dummycolour.red   = 0;
                dummycolour.green = 0;
                dummycolour.blue  = 0;
                dummycolour.flags = DoBlue;
                cursor = XCreatePixmapCursor(dpy, cursormask, cursormask,
                                             &dummycolour, &dummycolour, 0, 0);
                XFreeGC(dpy, gc);
                XFreePixmap(dpy, cursormask);
            }

            XDefineCursor(dpy, getWindow(), cursor);
            break;
    }
}

const char *XAbstractGui::get_title()
{
    if (cursor_type == FLX_DEFAULT_CURSOR)
    {
        return PROGRAMNAME " V" VERSION " - Press SHIFT F10 to capture mouse";
    }
    else
    {
        return PROGRAMNAME " V" VERSION " - Press SHIFT F10 to release mouse";
    }
}

void XAbstractGui::toggle_mouse_capture()
{
    XTextProperty titleProperty;
    Display       *dpy;
    const char    *title;
    char          **list;

    dpy = getDisplay();
    cursor_type = (cursor_type == FLX_DEFAULT_CURSOR) ?
                  FLX_INVISIBLE_CURSOR : FLX_DEFAULT_CURSOR;
    title = get_title();
    list  = const_cast<char **>(&title);

    if (XStringListToTextProperty(list, 1, &titleProperty) != 0)
    {
        XSetWMName(dpy, getWindow(FLX_MAIN), &titleProperty);
        XFree(titleProperty.value);
    }

    if (cursor_type == FLX_DEFAULT_CURSOR)
    {
        XUngrabPointer(dpy, CurrentTime);
    }
    else
    {
        unsigned int event_mask;

        event_mask = ButtonPressMask | ButtonReleaseMask |
                     EnterWindowMask | LeaveWindowMask | PointerMotionMask |
                     PointerMotionHintMask | Button1MotionMask |
                     Button2MotionMask | Button3MotionMask |
                     Button4MotionMask | Button5MotionMask |
                     ButtonMotionMask | KeymapStateMask;
        XGrabPointer(dpy, getWindow(), True, event_mask,
                     GrabModeAsync, GrabModeAsync, getWindow(),
                     None, CurrentTime);
    }

    set_cursor(cursor_type);
}

void XAbstractGui::release_mouse_capture()
{
    cursor_type = FLX_INVISIBLE_CURSOR;
    toggle_mouse_capture();
}

void XAbstractGui::c_focusIn(XEvent *)
{
}

void XAbstractGui::c_focusOut(XEvent *)
{
    release_mouse_capture();
}

void XAbstractGui::update_block(int block_number)
{
    Display     *dpy;
    Window       win;
    XImage      *img;

    if (!memory.has_changed(block_number))
    {
        return;
    }
    memory.reset_changed(block_number);

    dpy = getDisplay();
    win = getWindow();

    Byte const *src =
        memory.get_video_ram(vico1.get_value(), block_number);

    img = image[pixelSizeX - 1][pixelSizeY - 1];

    if (memory.is_video_bank_valid(vico1.get_value()))
    {
        CopyToZPixmap((Byte *)img->data, src, depth);

        auto divided_block = get_divided_block();
        if (divided_block >= 0 && (divided_block == block_number))
        {
            // first half display on the bottom of the window
            XPutImage(dpy, win, e2gc,
                      img, 0, 0, 0, (WINDOWHEIGHT -
                                (vico2.get_value() % BLOCKHEIGHT)) * pixelSizeY,
                      BLOCKWIDTH * pixelSizeX,
                      (vico2.get_value() % BLOCKHEIGHT) * pixelSizeY);
            // second half display on the top of window
            XPutImage(dpy, win, e2gc,
                      img, 0, (vico2.get_value() % YBLOCKS) * pixelSizeY,
                      0, 0, BLOCKWIDTH * pixelSizeX,
                      (BLOCKHEIGHT - (vico2.get_value() % BLOCKHEIGHT)) *
                      pixelSizeY);
        }
        else
        {
            XPutImage(dpy, win, e2gc,
                      img, 0, 0, 0,
                      ((block_number * BLOCKHEIGHT + WINDOWHEIGHT -
                        vico2.get_value()) % WINDOWHEIGHT) * pixelSizeY,
                      BLOCKWIDTH * pixelSizeX, BLOCKHEIGHT * pixelSizeY);
        }
    }
    else
    {
        // Invalid video bank selected: Always display an empty screen.
        CopyToZPixmap((Byte *)img->data, nullptr, depth);

        XPutImage(dpy, win, e2gc,
                  image[pixelSizeX - 1][pixelSizeY - 1], 0, 0, 0,
                  block_number * BLOCKHEIGHT * pixelSizeX,
                  BLOCKWIDTH * pixelSizeX, BLOCKHEIGHT * pixelSizeY);
    }
}

SWord XAbstractGui::translate_to_ascii(XKeyEvent *pevent)
{
    KeySym      keysym;
    char        charString[4];
    int     count;

    // first check for control character
    count = XLookupString(pevent, charString, sizeof(charString), &keysym,
                          nullptr);

    if ((pevent->state & ControlMask) && (pevent->state & ShiftMask))
    {
        switch (keysym)
        {
            case XK_Break:
            case XK_Pause:
                request_new_state(CpuState::ResetRun);
                return -1;

            case XK_Home:
            case XK_KP_Home:
            case XK_KP_7:
                return 0xa1;

            case XK_Up:
            case XK_KP_8:
            case XK_KP_Up:
                return 0xa8;

            case XK_Prior:
            case XK_KP_9:
            case XK_KP_Prior:
                return 0xa7;

            case XK_Left:
            case XK_KP_4:
            case XK_KP_Left:
                return 0xa4;

            case XK_Begin:
            case XK_KP_5:
            case XK_KP_Begin:
                return 0xa5;

            case XK_Right:
            case XK_KP_6:
            case XK_KP_Right:
                return 0xa6;

            case XK_End:
            case XK_KP_1:
            case XK_KP_End:
                return 0xa9;

            case XK_Down:
            case XK_KP_2:
            case XK_KP_Down:
                return 0xa2;

            case XK_Next:
            case XK_KP_3:
            case XK_KP_Next:
                return 0xa3;
        }
    }

    if (pevent->state & ControlMask)
    {
        switch (keysym)
        {
            case XK_a:
                return 0x01;

            case XK_b:
                return 0x02;

            case XK_c:
                return 0x03;

            case XK_d:
                return 0x04;

            case XK_e:
                return 0x05;

            case XK_f:
                return 0x06;

            case XK_g:
                return 0x07;

            case XK_h:
                return 0x08;

            case XK_i:
                return 0x09;

            case XK_j:
                return 0x0a;

            case XK_k:
                return 0x0b;

            case XK_l:
                return 0x0c;

            case XK_m:
                return 0x0d;

            case XK_n:
                return 0x0e;

            case XK_o:
                return 0x0f;

            case XK_p:
                return 0x10;

            case XK_q:
                return 0x11;

            case XK_r:
                return 0x12;

            case XK_s:
                return 0x13;

            case XK_t:
                return 0x14;

            case XK_u:
                return 0x15;

            case XK_v:
                return 0x16;

            case XK_w:
                return 0x17;

            case XK_x:
                return 0x18;

            case XK_y:
                return 0x19;

            case XK_z:
                return 0x1a;

            case XK_8:
                return 0x1b;

            case XK_ssharp:
                return 0x1c;

            case XK_9:
                return 0x1d;

            case XK_asciicircum:
                return 0x1e;

            case XK_minus:
                return 0x1f;

            case XK_Return:
                return 0x0d;

            case XK_Escape:
                return 0x1b;

            case XK_Tab:
                return 0x09;

            case XK_Delete:
            case XK_KP_Delete:
                return 0x1f;

            case XK_BackSpace:
                return 0x08;

            case XK_Break:
            case XK_Pause:
                cpu.set_nmi();

                return -1;

            case XK_Home:
            case XK_KP_7:
            case XK_KP_Home:
                return 0xb1;

            case XK_Up:
            case XK_KP_8:
            case XK_KP_Up:
                return 0xb8;

            case XK_Prior:
            case XK_KP_9:
            case XK_KP_Prior:
                return 0xb7;

            case XK_Left:
            case XK_KP_4:
            case XK_KP_Left:
                return 0xb4;

            case XK_Begin:
            case XK_KP_5:
            case XK_KP_Begin:
                return 0xb5;

            case XK_Right:
            case XK_KP_6:
            case XK_KP_Right:
                return 0xb6;

            case XK_End:
            case XK_KP_1:
            case XK_KP_End:
                return 0xb9;

            case XK_Down:
            case XK_KP_2:
            case XK_KP_Down:
                return 0xb2;

            case XK_Next:
            case XK_KP_3:
            case XK_KP_Next:
                return 0xb3;

            case XK_F11:
                return 0xfb; // PAT09: RIGHT MOST

            case XK_F12:
                return 0x92; // PAT09: LEFT  MOST

            default:
                return -1;
        } // switch
    } // if

    if (pevent->state & ShiftMask)
    {
        switch (keysym)
        {
            case XK_F1:
                return 0xca; // PAT09: F11

            case XK_F2:
                return 0xcb; // PAT09: F12

            case XK_F3:
                return 0xcc; // PAT09: F13

            case XK_F4:
                return 0xcd; // PAT09: F14

            case XK_F5:
                return 0xce; // PAT09: F15

            case XK_F6:
                return 0xcf; // PAT09: F16

            case XK_F7:
                return 0xd0; // PAT09: F17

            case XK_F8:
                return 0xd1; // PAT09: F18

            case XK_F9:
                return 0xd2; // PAT09: F19

            case XK_F10:
                toggle_mouse_capture();
                return -1;

            case XK_F11:
                return 0xea; // PAT09: RIGHT MOST

            case XK_F12:
                return 0x81; // PAT09: LEFT  MOST

            case XK_Break:
            case XK_Pause:
                cpu.set_nmi();
                return -1;

            case XK_Delete:
            case XK_KP_Delete:
            case XK_KP_Separator:
                return 0x7f; // PAT09: SHIFT DEL

            case XK_BackSpace:
                return 0x08; // PAT09: SHIFT BS

            case XK_Home:
            case XK_KP_7:
            case XK_KP_Home:
                return 0xe1; // PAT09: SHIFT ARR L

            case XK_Up:
            case XK_KP_8:
            case XK_KP_Up:
                return 0xe8; // PAT09: SHIFT CUR U

            case XK_Prior:
            case XK_KP_9:
            case XK_KP_Prior:
                return 0xe7; // PAT09: SHIFT ARR UL

            case XK_Left:
            case XK_KP_4:
            case XK_KP_Left:
                return 0xe4; // PAT09: SHIFT CUR L

            case XK_Begin:
            case XK_KP_5:
            case XK_KP_Begin:
                return 0xe5; // PAT09: SHIFT MODE

            case XK_Right:
            case XK_KP_6:
            case XK_KP_Right:
                return 0xe6; // PAT09: SHIFT CUR R

            case XK_End:
            case XK_KP_1:
            case XK_KP_End:
                return 0xe9; // PAT09: SHIFT ARR R

            case XK_Down:
            case XK_KP_2:
            case XK_KP_Down:
                return 0xe2; // PAT09: SHIFT CUR D

            case XK_Next:
            case XK_KP_3:
            case XK_KP_Next:
                return 0xe3; // PAT09: SHIFT ARR DR
        } // switch
    } // if

    switch (keysym)
    {
        case XK_KP_0:
            return 0x30;

        case XK_KP_1:
            return 0x31;

        case XK_KP_2:
            return 0x32;

        case XK_KP_3:
            return 0x33;

        case XK_KP_4:
            return 0x34;

        case XK_KP_5:
            return 0x35;

        case XK_KP_6:
            return 0x36;

        case XK_KP_7:
            return 0x37;

        case XK_KP_8:
            return 0x38;

        case XK_KP_9:
            return 0x39;

        case XK_Return:
            return 0x0d;

        case XK_Escape:
            return 0x1b;

        case XK_Tab:
            return 0x09;

        case XK_space:
            return ' ';

        case XK_Delete:
        case XK_KP_Delete:
            return 0x7f;

        case XK_BackSpace:
            return 0x08;

        case XK_Break:
        case XK_Pause:
            return 0x03;

        case XK_F1:
            return 0xc0;

        case XK_F2:
            return 0xc1;

        case XK_F3:
            return 0xc2;

        case XK_F4:
            return 0xc3;

        case XK_F5:
            return 0xc4;

        case XK_F6:
            return 0xc5;

        case XK_F7:
            return 0xc6;

        case XK_F8:
            return 0xc7;

        case XK_F9:
            return 0xc8;

        case XK_F10:
            return 0xc9;

        case XK_F11:
            return 0xfa; // PAT09: RIGHT MOST

        case XK_F12:
            return 0x91; // PAT09: LEFT  MOST

        case XK_Home:
        case XK_KP_Home:
            return 0xf1;

        case XK_Up:
        case XK_KP_Up:
            return 0xf8;

        case XK_Prior:
        case XK_KP_Prior:
            return 0xf7;

        case XK_Left:
        case XK_KP_Left:
            return 0xf4;

        case XK_Begin:
        case XK_KP_Begin:
            return 0xf5;

        case XK_Right:
        case XK_KP_Right:
            return 0xf6;

        case XK_End:
        case XK_KP_End:
            return 0xf9;

        case XK_Down:
        case XK_KP_Down:
            return 0xf2;

        case XK_Next:
        case XK_KP_Next:
            return 0xf3;

        case XK_KP_Separator:
            return '.';

        case XK_KP_Enter:
            return 0x0d;
    } // switch

    if (count == 1 && !(charString[0] & '\x80'))
    {
        return charString[0];
    }

    return -1;
} // translate_to_ascii

int XAbstractGui::convert_buttonmask(int newButtonMask)
{
    int mask;

    mask = 0;

    // convert to platform independant flags
    if (newButtonMask & Button1Mask)
    {
        mask |= L_MB;
    }

    if (newButtonMask & Button2Mask)
    {
        mask |= M_MB;
    }

    if (newButtonMask & Button3Mask)
    {
        mask |= R_MB;
    }

    if (newButtonMask & ShiftMask)
    {
        mask |= SHIFT_KEY;
    }

    if (newButtonMask & ControlMask)
    {
        mask |= CONTROL_KEY;
    }

    return mask;
}

int XAbstractGui::convert_keymask(int newButtonMask)
{
    int mask;

    mask = 0;

    // convert to platform independant flags
    if (newButtonMask & ShiftMask)
    {
        mask |= SHIFT_KEY;
    }

    if (newButtonMask & ControlMask)
    {
        mask |= CONTROL_KEY;
    }

    return mask;
}

void XAbstractGui::set_bell(int percent)
{
    XBell(getDisplay(), percent);
} // set_bell

// return: -1 = failure
int XAbstractGui::popup_help()
{
    pid_t child_pid;
    int status;
    int success;
    char helpfile[PATH_MAX];
    const char *args[3];

    strcpy(helpfile, options.doc_dir.c_str());

    if (helpfile[strlen(helpfile) - 1] != '/')
    {
        strcat(helpfile, "/");
    }

    // Use xdg-open to open a file of mime type text/html.
    // For detail see: https://www.freedesktop.org/wiki/Software/xdg-utils/
    strcat(helpfile, "flexemu.htm");
    args[0] = "xdg-open";
    args[1] = helpfile;
    args[2] = nullptr;

    if ((child_pid = fork()) == 0)
    {
        success = execvp(args[0], const_cast<char **>(args));
        // if it fails exit with errorcode
        exit(255);
    }
    else
    {
        sleep(1);
        success = waitpid(child_pid, &status, WNOHANG);

        if (success == -1 ||
            (success > 0 &&
             WIFEXITED(status) != 0 &&
             WEXITSTATUS(status) == 255)) // check for errorcode
        {
            fprintf(stderr, "xdg-open failed. Try installing xdg-utils.\n");
            return -1;
        }
    }

    return 0;
} // popup_help

void XAbstractGui::create_message_dialog(Widget)
{
} // create_message_dialog

XAbstractGui::XAbstractGui(
    Mc6809 &x_cpu,
    Memory &x_memory,
    Scheduler &x_scheduler,
    Inout &x_inout,
    VideoControl1 &x_vico1,
    VideoControl2 &x_vico2,
    JoystickIO &x_joystickIO,
    KeyboardIO &x_keyboardIO,
    TerminalIO &x_terminalIO,
    struct sGuiOptions &x_options)
        : AbstractGui(
                  x_cpu
                , x_memory
                , x_scheduler
                , x_inout
                , x_vico1
                , x_vico2
                , x_joystickIO
                , x_keyboardIO
                , x_terminalIO
                , x_options)
        , cursor(None)
        , cursor_type(FLX_DEFAULT_CURSOR)
{
}

XAbstractGui::~XAbstractGui()
{
}

Visual *XAbstractGui::GetBestVisual(Display *dpy, int *vClass, int *pdepth)
{
    int default_depth;
    XVisualInfo vTemplate;
    XVisualInfo *visualList = nullptr;
    Visual *best_visual = nullptr;
    int nrOfVisuals;

    vTemplate.screen = DefaultScreen(dpy);
    default_depth = DefaultDepth(dpy, vTemplate.screen);

    switch (default_depth)
    {
        case 8:
            *vClass = vTemplate.c_class  = PseudoColor;
            visualList = XGetVisualInfo(dpy,
                                        VisualScreenMask | VisualClassMask,
                                        &vTemplate, &nrOfVisuals);

            if (nrOfVisuals > 0)
            {
                *pdepth = visualList[0].depth;
                best_visual = visualList[0].visual;
            }
            else
            {
                *vClass = vTemplate.c_class = TrueColor;
                visualList = XGetVisualInfo(dpy,
                                            VisualScreenMask | VisualClassMask,
                                            &vTemplate, &nrOfVisuals);

                if (nrOfVisuals > 0)
                {
                    *pdepth = visualList[0].depth;
                    best_visual = visualList[0].visual;
                }
                else
                {
                    *vClass = vTemplate.c_class  = GrayScale;
                    visualList = XGetVisualInfo(dpy, VisualScreenMask |
                                                VisualClassMask, &vTemplate,
                                                &nrOfVisuals);

                    if (nrOfVisuals > 0)
                    {
                        *pdepth = visualList[0].depth;
                        best_visual = visualList[0].visual;
                    }
                }
            }

            break;

        case 16:
        case 24:
        case 32:
            *vClass = vTemplate.c_class  = TrueColor;
            visualList = XGetVisualInfo(dpy,
                                        VisualScreenMask | VisualClassMask,
                                        &vTemplate, &nrOfVisuals);

            if (nrOfVisuals > 0)
            {
                *pdepth = visualList[0].depth;
                best_visual = visualList[0].visual;
            }

            break;
    }

    XFree(visualList);
    return best_visual;
}

int XAbstractGui::SetColors(Display *dpy)
{
    int screen;
    XcmsColor xcolor;
    XcmsColor exact;
    int i, scale;
    int rw_color;   /* is it a read/write color ?*/
    int visualClass;
    unsigned long pixels[1 << COLOR_PLANES];

    screen = DefaultScreen(dpy);
    visual = GetBestVisual(dpy, &visualClass, &depth);

    if (!visual)
    {
        fprintf(stderr, "Unable to find an appropriate visual\n");
        return 0;
    }

    rw_color = (visualClass == DirectColor || visualClass == PseudoColor ||
                visualClass == GrayScale);
    cmap = DefaultColormap(dpy, screen);

    if (rw_color)
    {
        /* on visuals with read/write color cells
           first allocate them */
        if (!XAllocColorCells(dpy, cmap, False, nullptr, 0, pixels,
                              1 << COLOR_PLANES))
        {
            /* try again with a own colormap */
            cmap = XCreateColormap(dpy,
                                   RootWindow(dpy, screen),
                                   visual, AllocNone);
            own_cmap = 1;

            if (!XAllocColorCells(dpy, cmap, False, nullptr, 0,
                                  pixels, 1 << COLOR_PLANES))
            {
                fprintf(stderr, "Unable to allocate %d colors\n",
                        1 << COLOR_PLANES);
                return 0; /* failed even with own colormap */
            }
        }
    }

    if (!XcmsLookupColor(dpy, cmap, color.c_str(), &xcolor, &exact,
                         XcmsRGBiFormat))
    {
        exact.spec.RGBi.blue  = 1.0;
        exact.spec.RGBi.red   = 1.0;
        exact.spec.RGBi.green = 1.0;
    }

    xcolor.format = XcmsRGBiFormat;

    for (i = 0; i < (1 << COLOR_PLANES); i++)
    {
        if (withColorScale)
        {
            // DEPENDANCIES:
            // the color plane masks used here depend on
            // the same masks used in CopyToZPixmap
            scale  = i & 0x20 ? 2 : 0;
            scale |= i & 0x04 ? 1 : 0;
            xcolor.spec.RGBi.green = 1.0 * scale / 3;
            scale  = i & 0x10 ? 2 : 0;
            scale |= i & 0x02 ? 1 : 0;
            xcolor.spec.RGBi.red   = 1.0 * scale / 3;
            scale  = i & 0x08 ? 2 : 0;
            scale |= i & 0x01 ? 1 : 0;
            xcolor.spec.RGBi.blue  = 1.0 * scale / 3;
        }
        else
        {
            xcolor.spec.RGBi.blue  =
                exact.spec.RGBi.blue  * i / (1 << COLOR_PLANES);
            xcolor.spec.RGBi.red   =
                exact.spec.RGBi.red   * i / (1 << COLOR_PLANES);
            xcolor.spec.RGBi.green =
                exact.spec.RGBi.green * i / (1 << COLOR_PLANES);
        }

        if (rw_color)
        {
            xcolor.pixel = pixels[i];

            if (XcmsStoreColor(dpy, cmap,
                               &xcolor) == XcmsFailure)
            {
                fprintf(stderr, "Error storing a color\n");
                return 0;
            }
        }
        else
        {
            if (XcmsAllocColor(dpy, cmap,
                               &xcolor, XcmsRGBiFormat) == XcmsFailure)
            {
                fprintf(stderr, "Error allocating a color\n");
                return 0;
            }
        }

        pens[i] = xcolor.pixel;
    }

    //gcVal.plane_mask = 0xFFFFFFFF;

    return 1;
}


#endif // #ifdef HAVE_X11
