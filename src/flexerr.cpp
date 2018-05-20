/*
    flexerr.cpp


    FLEXplorer, An explorer for any FLEX file or disk container
    Copyright (C) 1998-2018  W. Schwotzer

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
#include <exception>
#include <sstream>
#include <stdio.h>
#include "flexerr.h"
#include "cvtwchar.h"
#include "sprinter.h"


#ifdef _
    #undef _
#endif

#define _(p) p


const std::string FlexException::what() const throw()
{
    return errorString;
}

#ifdef UNICODE
const std::wstring FlexException::wwhat() const throw()
{
    return ConvertToUtf16String(errorString);
}
#else
const std::string FlexException::wwhat() const throw()
{
    return errorString;
}
#endif

FlexException::FlexException(int ec) throw() : errorCode(ec)
{
    errorString = errString[ec];
}

FlexException::FlexException(int ec, int ip1) throw() : errorCode(ec)
{
    errorString = sprinter::print(errString[ec], ip1);
}

FlexException::FlexException(int ec, const std::string &sp1) throw()
    : errorCode(ec)
{
    errorString = sprinter::print(errString[ec], sp1);
}

FlexException::FlexException(int ec, const std::string &sp1, const std::string &sp2) throw()
    : errorCode(ec)
{
    errorString = sprinter::print(errString[ec], sp1, sp2);
}

FlexException::FlexException(int ec, int ip1, int ip2, const std::string &sp1) throw()
    : errorCode(ec)
{
    errorString = sprinter::print(errString[ec], ip1, ip2, sp1);
}

#ifdef _WIN32
FlexException::FlexException(unsigned long lastError, const std::string &sp1) throw()
{
    LPVOID lpMsgBuf;

    if (!FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL, lastError, 0, (LPTSTR) &lpMsgBuf, 0, NULL))
    {
        errorCode = FERR_UNSPEC_WINDOWS_ERROR;
        errorString = sprinter::print(errString[errorCode], sp1);
        return;
    }

    errorCode = FERR_WINDOWS_ERROR;

    errorString = (char *)lpMsgBuf;
    errorString += sp1;

    LocalFree(lpMsgBuf);
}
#endif

const char *FlexException::errString[] =
{
    _("No Error"),
    _("Unable to open {0}"),
    _("{0} is no file container"),
    _("No container opened"),
    _("No file opened"),
    _("Unable to format {0}"),
    _("Invalid container format #{0}"),
    _("Error reading from {0}"),
    _("Error writing to {0}"),
    _("Directory already opened"),
    _("No directory opened"),
    _("File already opened"),
    _("No free file handle available"),
    _("File {0} already exists"),
    _("Invalid file handle #{0}"),
    _("Invalid open mode \"{0}\""),
    _("Directory full"),
    _("Error reading trk/sec {0}/{1} in {2}"),
    _("Error writing trk/sec {0}/{1} in {2}"),
    _("No file \"{0}\" (container {1})"),
    _("Record map of {0} is full (container {1})"),
    _("Container {0} full when writing {1}"),
    _("Unable to create {0}"),
    _("Unable to rename {0} (container {1})"),
    _("Unable to remove {0} (container {1})"),
    _("Error reading disk space (container {0})"),
    _("Unable to copy {0} on itself"),
    _("Wrong parameter"),
    _("Error creating process ({0} {1})"),
    _("Error reading FLEX binary format"),
    _("Error creating temporary file {0}"),
    _("Container {0} is read-only"),
    _("An unspecified Windows error occured (#{1})"),
    _(""),
    _("{0} is an invalid NULL pointer")
};
