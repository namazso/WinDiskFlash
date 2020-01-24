//	Copyright 2020 namazso <admin@namazso.eu>
//	WinDiskFlash - Disk image flasher
//	
//	WinDiskFlash is free software: you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//	
//	WinDiskFlash is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//	
//	You should have received a copy of the GNU General Public License
//	along with WinDiskFlash.  If not, see <https://www.gnu.org/licenses/>.
#include "pch.hpp"

#include "disk.hpp"

BOOL vol::Lock(HANDLE h)
{
  DWORD bytes_returned;
  return DeviceIoControl(
    h,
    FSCTL_LOCK_VOLUME,
    nullptr,
    0,
    nullptr,
    0,
    &bytes_returned,
    nullptr
  );
}

BOOL vol::Unlock(HANDLE h)
{
  DWORD bytes_returned;
  return DeviceIoControl(
    h,
    FSCTL_UNLOCK_VOLUME,
    nullptr,
    0,
    nullptr,
    0,
    &bytes_returned,
    nullptr
  );
}

BOOL vol::Unmount(HANDLE h)
{
  DWORD bytes_returned;
  return DeviceIoControl(
    h,
    FSCTL_DISMOUNT_VOLUME,
    nullptr,
    0,
    nullptr,
    0,
    &bytes_returned,
    nullptr
  );
}
