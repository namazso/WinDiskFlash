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
#include "main_dialog.hpp"

struct WorkerThread
{
  std::vector<HANDLE> volumes;
  HANDLE disk;
  HANDLE file;
  HWND window;
  uint64_t disk_size_in_bytes;
  Operation operation;

  static DWORD WINAPI Callback(LPVOID lpThreadParameter);

private:
  void Thread();

  DWORD DoTrash();
  DWORD DoFlash(uint64_t off = 0);
  DWORD DoSave();

  LRESULT MagicMessage(UINT uMsg, LPARAM lParam);

  void NotifyStarted();
  bool NotifyProgress(double progress);
  void NotifyFinish(DWORD status);
};