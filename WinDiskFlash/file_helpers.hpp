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

namespace file
{
  size_t WriteAtOffset(HANDLE file, uint64_t offset, const void* buf, size_t size);
  size_t ReadAtOffset(HANDLE file, uint64_t offset, void* buf, size_t size);
  // returns -1 on error, check GetLastError()
  uint64_t GetSize(HANDLE file);
  size_t Copy(HANDLE dst, uint64_t dst_off, HANDLE src, uint64_t src_off, void* buf, size_t size);
}
