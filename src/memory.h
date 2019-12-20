/*
    memory.h


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



#ifndef MEMORY_INCLUDED
#define MEMORY_INCLUDED

#include "misc1.h"
#include <stdio.h>
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <tsl/robin_map.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#include <functional>
#include <memory>
#include "iodevice.h"
#include "ioaccess.h"
#include "memtgt.h"
#include "e2.h"
#include "bobserv.h"

// Maximum number of video RAM pointers supported.
// Each video RAM page has as size of 16KByte.

#define MAX_VRAM        (4 * 16)


class Memory : public MemoryTarget<size_t>, public BObserver
{
public:
    Memory(const struct sOptions &options);
    virtual ~Memory();

private:
    Byte *ppage[16];
    bool isRamExtension;
    bool isHiMem;
    bool isFlexibleMmu;
    bool isEurocom2V5; // Emulate an Eurocom II/V5 (instead of Eurocom II/V7)
    DWord memory_size;
    DWord video_ram_size;
    std::unique_ptr<Byte[]> memory;
    std::unique_ptr<Byte[]> video_ram;

    // I/O device access
    std::vector<std::reference_wrapper<IoDevice> > ioDevices;
    tsl::robin_map<
        Word,
        IoAccess,
        std::hash<Word>,
        std::equal_to<Word>,
        std::allocator<std::pair<Word, IoAccess>>,
        false,
        tsl::rh::power_of_two_growth_policy<16>> ioAccessForAddressMap;

    // interface to video display
    Byte *vram_ptrs[MAX_VRAM];
    Word video_ram_active_bits; // 16-bit, one for each video memory page
    bool changed[YBLOCKS];

private:
    void init_memory();
    void init_vram_ptr(Byte vram_ptr_index, Byte *ram_address);
    static Byte initial_content[8];

    // Initialisation functions

public:

    bool add_io_device(IoDevice &device, Word base_address, int size = -1);

    // memory interface
public:
    void reset_io();
    void switch_mmu(Word offset, Byte val);
    void init_blocks_to_update();

    // BObserver interface
public:
    void UpdateFrom(NotifyId id, void *param = nullptr) override;

    // memory target interface
public:
    void CopyFrom(const Byte *buffer, size_t address, size_t aSize);

public:
    void write_ram_rom(Word address, Byte value);
    Byte read_ram_rom(Word address);
    void dump_ram_rom(Word min = 0, Word max = 0xffff);

    // The following memory Byte/Word access methods are
    // inlined for optimized performance.
    inline void write_byte(Word address, Byte value)
    {
        if ((address & GENIO_BASE) == GENIO_BASE)
        {
            auto iterator = ioAccessForAddressMap.find(address);

            if (iterator != ioAccessForAddressMap.end())
            {
                iterator.value().write(value);
                return;
            }
        }

        if (video_ram_active_bits & (1 << (address >> 12)))
        {
            changed[(address & 0x3fff) / YBLOCK_SIZE] = true;
            *(ppage[address >> 12] + (address & 0x3fff)) = value;
        }
        else
        {
            if (address < ROM_BASE)
            {
                // Use paged memory access to be able to mirror
                // RAM banks (e.g. for Eurocom V5).
                *(ppage[address >> 12] + (address & 0x3fff)) = value;
                if (!isRamExtension)
                {
                    changed[(address & 0x3fff) / YBLOCK_SIZE] = true;
                }
            }
        }
    }

    inline Byte read_byte(Word address)
    {
        if ((address & GENIO_BASE) == GENIO_BASE)
        {
            auto iterator = ioAccessForAddressMap.find(address);

            if (iterator != ioAccessForAddressMap.end())
            {
                // read one Byte from memory mapped I/O device
                return iterator.value().read();
            }
        }

        return *(ppage[address >> 12] + (address & 0x3fff));
    } // read_byte

    inline void write_word(Word address, Word value)
    {
        write_byte(address, static_cast<Byte>(value >> 8));
        write_byte(address + 1, static_cast<Byte>(value));
    }

    inline Word read_word(Word address)
    {
        Word value;

        value = static_cast<Word>(read_byte(address)) << 8;
        value |= static_cast<Word>(read_byte(address + 1));

        return value;
    }

    inline bool has_changed(int block_number) const
    {
        return changed[block_number];
    }

    inline void reset_changed(int block_number)
    {
        changed[block_number] = false;
    }

    // Get read-only access to video RAM.
    // This can be used by the GUI to update the video display.
    inline Byte const *get_video_ram(int bank, int block_number) const
    {
        if (isRamExtension)
        {
            if ((bank & 0x01) == 1)
            {
                return vram_ptrs[0x08] + block_number * YBLOCK_SIZE;
            }
            else
            {
                return vram_ptrs[0x0C] + block_number * YBLOCK_SIZE;
            }
        }
        else
        {
            int offset = (bank & 0x03) * VIDEORAM_SIZE;

            return &memory[offset] + block_number * YBLOCK_SIZE;
        }
    }

    inline bool is_video_bank_valid(int bank) const
    {
        if (isRamExtension)
        {
            return (bank & 0x2) == 0;
        }
        else
        {
            return (bank & 0x3) != 3;
        }
    }
};
#endif // MEMORY_INCLUDED

