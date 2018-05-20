/*
    schedule.h


    flexemu, an MC6809 emulator running FLEX
    Copyright (C) 1997-2018  W. Schwotzer

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



#ifndef __schedule_h__
#define __schedule_h__

#include "misc1.h"
#include <signal.h>
#include "bthread.h"
#include "cpustate.h"
#include "schedcpu.h"

#define DO_SYNCEXEC     0x80
#define DO_TIMER        0x100
#define DO_SET_STATUS       0x200

#define MAX_COMMANDS            10


class BMutex;
class BCommand;
class BTime;
class BTimer;
class Inout;

class Scheduler : public BThread
{
public:
    Scheduler(sOptions *pOptions);
    virtual     ~Scheduler();
    void        set_cpu(ScheduledCpu *x_cpu)
    {
        cpu = x_cpu;
    };
    void        set_inout(Inout *x_io)
    {
        io  = x_io;
    };
    Byte        statemachine(Byte initial_state);
    bool        is_finished(void);
    void        set_new_state(Byte x_user_input);
    void        process_events(void);
    Byte        idleloop();
    Byte        runloop(Word mode);

    // Thread support
public:
    void        sync_exec(BCommand *newCommand);
protected:
    void        Execute(void);
    virtual void    Run(void);
    BMutex          *commandMutex;
    BMutex          *statusMutex;
    BMutex           *irqStatMutex;
    BCommand        *command[MAX_COMMANDS];

    // Timer interface:
public:
    QWord       get_total_cycles(void)
    {
        return total_cycles;
    };
protected:
    static void timer_elapsed(void *p);
    void timer_elapsed(void);
    void set_timer(void);

    Byte        state;
    Word        events;
    Byte        user_input;
    QWord       total_cycles;
    QWord       time0sec;
    ScheduledCpu    *cpu;
    Inout       *io;
    BTime       *systemTime;
    static Scheduler *instance;

    // CPU status
public:
    void        get_interrupt_status(tInterruptStatus &s);
    bool        status_available(void);
    CpuStatus  *get_status();
protected:
    tInterruptStatus interrupt_status;
    void        do_reset();
    CpuStatus   *pCurrent_status;

    // CPU frequency
public:
    void        set_frequency(float target_freq);
    float       get_frequency(void)
    {
        return frequency;
    };
protected:
    void        update_frequency(void);
    void        frequency_control(QWord time1);
    float       target_frequency;
    float       frequency;      // current frequency
    QWord       time0;          // time for freq control
    QWord       cycles0;        // cycle count for freq calc
};

#endif // __schedule_h__