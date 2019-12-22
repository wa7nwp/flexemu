/*
    inout.cpp


    flexemu, an MC6809 emulator running FLEX
    Copyright (C) 1997-2019  W. Schwotzer

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


#include "misc1.h"

#include "inout.h"
#include "e2floppy.h"
#include "mc6821.h"
#include "mc146818.h"
#include "absgui.h"
#include "joystick.h"
#include "terminal.h"
#include "memory.h"

#ifdef HAVE_XTK
    #include "xtgui.h"
#endif

#ifdef _WIN32
    #include "win32gui.h"
#endif


Inout::Inout(const struct sOptions &x_options, Memory &x_memory) :
     memory(x_memory)
     , options(x_options)
     , fdc(nullptr)
     , rtc(nullptr)
     , gui(nullptr)
     , local_serpar_address(-1)
{
}

Inout::~Inout()
{
}

void Inout::set_fdc(E2floppy *x_fdc)
{
    fdc = x_fdc;
}

void Inout::get_drive_status(DiskStatus status[4])
{
    if (fdc)
    {
        fdc->get_drive_status(status);
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            status[i] = DiskStatus::EMPTY;
        }
    }
}

std::string Inout::get_drive_info(Word drive_nr)
{
    if (fdc)
    {
        return fdc->drive_info(drive_nr);
    }

    return std::string();
}

void Inout::create_gui(JoystickIO &x_joystickIO,
                       KeyboardIO &x_keyboardIO, TerminalIO &x_terminalIO,
                       Pia1 &x_pia1,
                       Memory &x_memory, Scheduler &x_scheduler,
                       Mc6809 &x_cpu,
                       VideoControl1 &x_vico1, VideoControl2 &x_vico2,
                       struct sGuiOptions &x_options)
{
#ifdef UNIT_TEST
    (void)x_joystickIO;
    (void)x_keyboardIO;
    (void)x_terminalIO;
    (void)x_pia1;
    (void)x_memory;
    (void)x_scheduler;
    (void)x_cpu;
    (void)x_vico1;
    (void)x_vico2;
    (void)x_options;
#else
    // Only allow to open Gui once.
    if (gui == nullptr)
    {
        switch (x_options.guiType)
        {
#ifdef HAVE_XTK

            case GuiType::XTOOLKIT:
                gui = std::unique_ptr<AbstractGui>(
                       new XtGui(x_cpu, x_memory, x_scheduler, *this, x_vico1,
                                 x_vico2, x_joystickIO, x_keyboardIO,
                                 x_terminalIO, x_pia1, x_options));
                break;
#endif
#ifdef _WIN32

            case GuiType::WINDOWS:
                gui = std::unique_ptr<AbstractGui>(
                       new Win32Gui(x_cpu, x_memory, x_scheduler, *this, x_vico1,
                                    x_vico2, x_joystickIO, x_keyboardIO,
                                    x_terminalIO, x_pia1, x_options));
                break;
#endif

#ifndef HAVE_XTK
            case GuiType::XTOOLKIT:
#endif
#ifndef _WIN32
            case GuiType::WINDOWS:
#endif
            default:
                throw FlexException(FERR_UNSUPPORTED_GUI_TYPE,
                        static_cast<int>(x_options.guiType));
            case GuiType::NONE:
                break;
        }
    }
#endif // UNIT_TEST
}

// one second updates are generated by the cpu
// in this method they will be transmitted to all objects
// which need it
void Inout::update_1_second()
{
    if (rtc != nullptr)
    {
        rtc->update_1_second();
    }
}

bool Inout::is_gui_present()
{
    return gui != nullptr;
}

bool Inout::output_to_terminal()
{
#ifdef HAVE_TERMIOS_H

    if (is_gui_present())
    {
        gui->output_to_terminal();
    }

    return true;
#else
    return false;
#endif // #ifdef HAVE_TERMIOS_H
}

bool Inout::output_to_graphic()
{
    if (is_gui_present())
    {
        gui->output_to_graphic();
        return true;
    }

    return false;
}

void Inout::main_loop()
{
    if (is_gui_present())
    {
        gui->main_loop();
    }
}

Word Inout::serpar_address() const
{
    return static_cast<Word>(local_serpar_address);
}

bool Inout::is_serpar_address_valid() const
{
    return local_serpar_address >= 0 && local_serpar_address <= 0xffff;
}

// Set address of SERPAR label. A value < 0 invalidates the address.
void Inout::serpar_address(int value)
{
    local_serpar_address = value;
}

void Inout::set_rtc(Mc146818 *x_rtc)
{
    rtc = x_rtc;
}

void Inout::UpdateFrom(NotifyId id, void *)
{
    if (id == NotifyId::FirstKeyboardRequest &&
        options.term_mode &&
        is_serpar_address_valid())
    {
        memory.write_byte(serpar_address(), Byte(0xFF));
    }
}

