/*
    e2floppy.cpp

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


#include "misc1.h"
#include <string>
#include <sstream>
#include <iterator>
#include "e2floppy.h"
#include "ffilecnt.h"
#include "rfilecnt.h"
#include "ndircont.h"
#include "fcinfo.h"
#include "flexerr.h"
#include "bdir.h"
#include "crc.h"


E2floppy::E2floppy() : selected(4), pfs(nullptr),
    writeTrackState(WriteTrackState::Inactive)
{
    Word i;

    disk_dir = "";

    for (i = 0; i <= 4; i++)
    {
        track[i] = 1; // position all drives to track != 0  !!!
        drive_status[i] = DiskStatus::EMPTY;
    }

    memset(sector_buffer, 0, sizeof(sector_buffer));
} // E2floppy


E2floppy::~E2floppy()
{
    std::lock_guard<std::mutex> guard(status_mutex);

    for (Word drive_nr = 0; drive_nr < 4; drive_nr++)
    {
        if (floppy[drive_nr].get() != nullptr)
        {
            try
            {
                floppy[drive_nr].reset(nullptr);
                drive_status[drive_nr] = DiskStatus::EMPTY;
            }
            catch (...)
            {
                // ignore errors
            }
        }
    }
} // ~E2floppy

bool E2floppy::umount_drive(Word drive_nr)
{
    if (drive_nr > 3 || (floppy[drive_nr].get() == nullptr))
    {
        return 0;
    }

    std::lock_guard<std::mutex> guard(status_mutex);

    try
    {
        floppy[drive_nr].reset(nullptr);
        drive_status[drive_nr] = DiskStatus::EMPTY;
    }
    catch (FlexException &)
    {
        // ignore errors
    }

    return 1;
} // umount_drive

bool E2floppy::mount_drive(const char *path, Word drive_nr, tMountOption option)
{
    int i = 0;
    FileContainerIfSectorPtr pfloppy;
    struct stat sbuf;

    if (drive_nr > 3 || path == nullptr || strlen(path) == 0)
    {
        return false;
    }

    // check if already mounted
    if (floppy[drive_nr].get() != nullptr)
    {
        return false;
    }

    track[drive_nr] = 1;    // position to a track != 0  !!!

    std::string containerPath(path);

    // first try with given path
#ifdef _WIN32
    std::string::iterator it;

    for (it = containerPath.begin(); it != containerPath.end(); ++it)
    {
        if (*it == '|')
        {
            *it = ':';
        }
        if (*it == '/')
        {
            *it = '\\';
        }
    }
#endif

    for (i = 0; i < 2; i++)
    {
        // A file which does not exist or has file size zero
        // is marked as unformatted.
        bool is_formatted = !stat(containerPath.c_str(), &sbuf) &&
                            (S_ISREG(sbuf.st_mode) && sbuf.st_size);
#ifdef NAFS

        if (BDirectory::Exists(containerPath))
        {
            try
            {
                pfloppy = FileContainerIfSectorPtr(
                 new NafsDirectoryContainer(containerPath.c_str()));
            }
            catch (FlexException &)
            {
                // just ignore
            }
        }
        else
#endif
            if (is_formatted && option == MOUNT_RAM)
            {
                try
                {
                    pfloppy = FileContainerIfSectorPtr(
                     new FlexRamFileContainer(containerPath.c_str(), "rb+"));
                }
                catch (FlexException &)
                {
                    try
                    {
                        pfloppy = FileContainerIfSectorPtr(
                         new FlexRamFileContainer(containerPath.c_str(), "rb"));
                    }
                    catch (FlexException &)
                    {
                        // just ignore
                    }
                }
            }
            else
            {
                const char *mode = is_formatted ? "rb+" : "wb+";
                try
                {
                    pfloppy = FileContainerIfSectorPtr(
                      new FlexFileContainer(containerPath.c_str(), mode));
                }
                catch (FlexException &)
                {
                    if (is_formatted)
                    {
                        try
                        {
                            pfloppy = FileContainerIfSectorPtr(
                             new FlexFileContainer(containerPath.c_str(),
                                                   "rb"));
                        }
                        catch (FlexException &)
                        {
                            // just ignore
                        }
                    }
                }
            }

        std::lock_guard<std::mutex> guard(status_mutex);
        floppy[drive_nr] = std::move(pfloppy);

        if (floppy[drive_nr].get() != nullptr)
        {
            drive_status[drive_nr] = DiskStatus::ACTIVE;
            return true;
        }

        // second try with path within disk_dir directory
        containerPath = disk_dir;

        if (containerPath.length() > 0 &&
            containerPath[containerPath.length()-1] != PATHSEPARATOR)
        {
            containerPath += PATHSEPARATORSTRING;
        }

        containerPath += path;
    } // for

    return floppy[drive_nr].get() != nullptr;
} // mount_drive

void E2floppy::disk_directory(const char *x_disk_dir)
{
    disk_dir = x_disk_dir;
}

void E2floppy::mount_all_drives(std::string drive[])
{
    Word drive_nr;

    for (drive_nr = 0; drive_nr < 4; drive_nr++)
    {
        mount_drive(drive[drive_nr].c_str(), drive_nr);
    }

    selected = 4;           // deselect all drives
    pfs = nullptr;
}  // mount_all_drives

bool E2floppy::umount_all_drives()
{
    Word drive_nr;
    bool result;

    result = true;

    for (drive_nr = 0; drive_nr < 4; drive_nr++)
        if (!umount_drive(drive_nr))
        {
            result = false;
        }

    return result;
}  // umount_all_drives

// get info for corresponding drive or nullptr
// the info string should not exceed 512 Bytes
// it is dynamically allocated and should be freed
// by the calling program

std::string E2floppy::drive_info(Word drive_nr)
{
    std::stringstream stream;

    if (drive_nr <= 3)
    {
        std::lock_guard<std::mutex> guard(status_mutex);

        if (floppy[drive_nr].get() == nullptr)
        {
            stream << "drive #" << drive_nr << " not ready" << std::endl;
        }
        else
        {
            FlexContainerInfo info;
            int trk, sec;
            bool is_write_protected = false;

            try
            {
                floppy[drive_nr]->GetInfo(info);
                is_write_protected = floppy[drive_nr]->IsWriteProtected();
            }
            catch (FlexException &ex)
            {
                stream << ex.what() << std::endl;
                return stream.str().c_str();
            }

            info.GetTrackSector(trk, sec);
            stream << "drive       #" << drive_nr << std::endl
                << "type:       " << info.GetTypeString().c_str() << std::endl;

            if (info.GetIsFlexFormat())
            {
                stream << "name:       " << info.GetName() << " #" <<
                                            info.GetNumber() << std::endl;
            }
            stream << "path:       " << info.GetPath().c_str() << std::endl
                   << "tracks:     " << trk << std::endl
                   << "sectors:    " << sec << std::endl
                   << "write-prot: " << (is_write_protected ? "yes" : "no")
                   << std::endl
                   << "FLEX format:" << (info.GetIsFlexFormat() ? "yes" : "no")
                   << std::endl;
        }
    }

    return stream.str().c_str();
}

const char *E2floppy::open_mode(char *path)
{
    int wp;    // write protect flag
    const char *mode;

    wp = access(path, W_OK);
    mode = wp ? "rb" : "rb+";
    return mode;
} // open_mode


bool E2floppy::update_all_drives()
{
    Word drive_nr;
    bool result = true;

    for (drive_nr = 0; drive_nr < 4; drive_nr++)
    {
        if (floppy[drive_nr].get() == nullptr)
        {
            // no error if drive not ready
            continue;
        }

        if (!update_drive(drive_nr))
        {
            result = false;
        }
    }

    return result;
} // update_all_drives

bool E2floppy::update_drive(Word drive_nr)
{
    if (drive_nr > 3)
    {
        return false;
    }

    if (floppy[drive_nr].get() == nullptr)
    {
        // error if drive not ready
        return false;
    }

    return true;
} // update_drive

void E2floppy::resetIo()
{
    Wd1793::resetIo();
}

void E2floppy::select_drive(Byte new_selected)
{
    if (new_selected > 4)
    {
        new_selected = 4;
    }

    if (new_selected != selected)
    {
        track[selected] = getTrack();
        selected = new_selected;
        pfs = floppy[selected].get();
        setTrack(track[selected]);
    }
}

Byte E2floppy::readByte(Word index, Byte command_un)
{
    if (pfs != nullptr)
    {
        std::lock_guard<std::mutex> guard(status_mutex);

        switch (command_un)
        {
            case CMD_READSECTOR:
            case CMD_READSECTOR_MULT:
                return readByteInSector(index);

            case CMD_READTRACK:
                return readByteInTrack(index);

            case CMD_READADDRESS:
                return readByteInAddress(index);
        }
    }

    return 0U;
}

Byte E2floppy::readByteInSector(Word index)
{
    if (index == pfs->GetBytesPerSector())
    {
        drive_status[selected] = DiskStatus::ACTIVE;

        if (!pfs->ReadSector(&sector_buffer[0], getTrack(), getSector(),
                             getSide() ? 1 : 0))
        {
            setStatusReadError();
        }
    }

    return sector_buffer[pfs->GetBytesPerSector() - index];
}

Byte E2floppy::readByteInTrack(Word)
{
    // TODO unfinished
    return 0U;
}

Byte E2floppy::readByteInAddress(Word index)
{
    if (index == 6)
    {
        Crc<Word> crc16(0x1021);

        sector_buffer[0] = getTrack();
        sector_buffer[1] = getSide() ? 1 : 0;
        sector_buffer[2] = 1; // sector address
        sector_buffer[3] = getSizeCode(); // sector sizecode

        auto crc = crc16.GetResult(&sector_buffer[0], &sector_buffer[4]);

        sector_buffer[4] = static_cast<Byte>(crc >> 8);
        sector_buffer[5] = static_cast<Byte>(crc & 0xFF);
    }

    return sector_buffer[6 - index];
}

bool E2floppy::startCommand(Byte command_un)
{
    // Special actions may be needed when starting a command.
    // command_un is the upper nibble of the WD1793 command.
    if (command_un == CMD_WRITETRACK)
    {
        // CMD_WRITETRACK means a new disk has to be formatted
        // within the emulation.
        // Only unformatted file containers, if IsFlexFormat()
        // returns false, can be formatted.
        if (pfs->IsFlexFormat())
        {
            return false;
        }

        writeTrackState = WriteTrackState::Inactive;
    }

    return true;
}

void E2floppy::writeByte(Word &index, Byte command_un)
{
    if (pfs != nullptr)
    {
        std::lock_guard<std::mutex> guard(status_mutex);

        switch (command_un)
        {
            case CMD_WRITESECTOR:
            case CMD_WRITESECTOR_MULT:
                writeByteInSector(index);
                break;

            case CMD_WRITETRACK:
                writeByteInTrack(index);
                break;
        }
    }
}

void E2floppy::writeByteInTrack(Word &index)
{
    Word i;
    int sizecode;


    switch (writeTrackState)
    {
        case WriteTrackState::Inactive:
            drive_status[selected] = DiskStatus::ACTIVE;
            writeTrackState = WriteTrackState::WaitForIdAddressMark;
            index = 256U;
            break;

        case WriteTrackState::WaitForIdAddressMark:
            if (getDataRegister() == ID_ADDRESS_MARK)
            {
                writeTrackState = WriteTrackState::IdAddressMark;
                offset = sizeof(idAddressMark);
            }
            break;

        case WriteTrackState::IdAddressMark:
            idAddressMark[sizeof(idAddressMark) - offset] = getDataRegister();
            if (--offset == 0)
            {
                writeTrackState = WriteTrackState::WaitForDataAddressMark;
                index = 64U;
            }
            break;

        case WriteTrackState::WaitForDataAddressMark:
            if (getDataRegister() == DATA_ADDRESS_MARK)
            {
                writeTrackState = WriteTrackState::WriteData;
                sizecode = idAddressMark[3] & 0x03;
                offset = ::getBytesPerSector(sizecode);
                index = offset + 2;
            }
            break;

        case WriteTrackState::WriteData:
            sizecode = idAddressMark[3] & 0x03;
            i = ::getBytesPerSector(sizecode) - offset;
            sector_buffer[i] = getDataRegister();
            if (--offset == 0U)
            {
                pfs->FormatSector(&sector_buffer[0],
                        idAddressMark[0], idAddressMark[2],
                        idAddressMark[1], idAddressMark[3] & 0x03);

                writeTrackState = WriteTrackState::WaitForCrc;
                index = 2U;
            }
            break;

        case WriteTrackState::WaitForCrc:
            if (getDataRegister() == TWO_CRCS)
            {
                drive_status[selected] = DiskStatus::ACTIVE;
                writeTrackState = WriteTrackState::WaitForIdAddressMark;
                index = 96U;
            }
            break;
    }
}

void E2floppy::writeByteInSector(Word index)
{
    sector_buffer[pfs->GetBytesPerSector() - index] = getDataRegister();

    if (index == 1)
    {
        drive_status[selected] = DiskStatus::ACTIVE;

        if (!pfs->WriteSector(&sector_buffer[0], getTrack(), getSector(),
                              getSide() ? 1 : 0))
        {
            setStatusWriteError();
        }
    }
}


bool E2floppy::isRecordNotFound() const
{
    if (pfs == nullptr)
    {
        return true;
    }

    return !pfs->IsSectorValid(getTrack(), getSector());
} // isRecordNotFound

bool E2floppy::isSeekError(Byte new_track) const
{
    if (pfs == nullptr)
    {
        return true;
    }

    return !pfs->IsTrackValid(new_track);
} // isSeekError

bool E2floppy::isDriveReady() const
{
    return pfs != nullptr;
}  // isDriveReady

bool E2floppy::isWriteProtect() const
{
    if (pfs == nullptr)
    {
        return true;
    }

    return pfs->IsWriteProtected();
}  // isWriteProtect

void E2floppy::get_drive_status(DiskStatus stat[4])
{
    Word i;

    std::lock_guard<std::mutex> guard(status_mutex);

    for (i = 0; i < 4; ++i)
    {
        stat[i] = drive_status[i];

        if (drive_status[i] != DiskStatus::EMPTY)
        {
            drive_status[i] = DiskStatus::INACTIVE;
        }
    }
}

bool E2floppy::format_disk(SWord trk, SWord sec, const char *name,
                           int type /* = TYPE_DSK_CONTAINER */)
{
    FileContainerIfSectorPtr pfloppy;

    try
    {
        switch (type)
        {
#ifdef NAFS

            case TYPE_NAFS_DIRECTORY:
                pfloppy = FileContainerIfSectorPtr(
                NafsDirectoryContainer::Create(disk_dir, name, trk, sec, type));
                break;
#endif

            case TYPE_DSK_CONTAINER:
            case TYPE_FLX_CONTAINER:
                pfloppy = FileContainerIfSectorPtr(
                FlexFileContainer::Create(disk_dir, name, trk, sec, type));
                break;
        }
    }
    catch (FlexException &)
    {
        return false;
    }

    return true;
} // format_disk

Word E2floppy::getBytesPerSector() const
{
    if (pfs == nullptr)
    {
        return 0U;
    }

    return static_cast<Word>(pfs->GetBytesPerSector());
}

Byte E2floppy::getSizeCode() const
{
    switch (getBytesPerSector())
    {
        case 256U: return 1U;
        case 128U: return 0U;
        case 512U: return 2U;
        case 1024U: return 3U;
        default: return 0U;
    }
}

