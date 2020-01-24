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
#include "disk.hpp"

enum class Operation
{
  Flash,
  Save,
  Trash
};

class MainDialog
{
  HWND _handle;
  std::vector<disk_info> _disks;
  bool _is_running = false;
  bool _to_be_stopped = false;

  void UpdateDisks();
  void LocalizeStrings();
  void StartOperation();
  void OnClose();

  HWND GetCtl(int ctl) { return GetDlgItem(_handle, ctl); }

public:
  enum Messages : UINT
  {
    // WPARAM: magic  LPARAM: unused                            LRESULT: unused
    WM_FLASH_STARTED = WM_USER + 1,
    // WPARAM: magic  LPARAM: current progress in (0, MAXLONG)  LRESULT: TRUE if stop requested, FALSE otherwise
    WM_FLASH_PROGRESS,
    // WPARAM: magic  LPARAM: result error code                 LRESULT: unused
    WM_FLASH_FINISHED
  };

  // apparently some badly behaving applications can send random global window messages
  constexpr static auto message_magic_wparam = (WPARAM)0x622438b46201f765;

  MainDialog(HWND hDlg, void*) : _handle(hDlg) {}

  INT_PTR DlgProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
