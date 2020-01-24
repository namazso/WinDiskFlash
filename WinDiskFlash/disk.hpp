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
#pragma once

struct disk_info
{
  // A path that is guaranteed to point at this specific device at any time, or be invalid if device was removed.
  std::wstring stable_path;

  // Cleaned product id string
  std::string product_id;

  // Cleaned serial string
  std::string serial;

  // List of letter mount points (like C:) pointing at partitions on this disk
  std::vector<char> letters;

  // Stable paths to volumes that are at least on this disk. Can have 0..N mount points, not necessarily lettered.
  std::vector<std::wstring> stable_volumes;

  // Size of the disk, in bytes. Always multiple of 512.
  uint64_t size_in_bytes{};
};

std::vector<disk_info> GetDisks();

namespace vol
{
  BOOL Lock(HANDLE h);
  BOOL Unlock(HANDLE h);
  BOOL Unmount(HANDLE h);
}
