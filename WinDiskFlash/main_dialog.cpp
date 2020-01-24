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

#include "main_dialog.hpp"
#include "resource.h"
#include "dialog_binder.hpp"
#include "license_dialog.hpp"
#include "main.hpp"
#include "worker_thread.hpp"
#include "resource_helper.hpp"
#include "file_helpers.hpp"

#include <WindowsX.h>
#include <CommCtrl.h>

template <typename Begin, typename End, typename Delim>
auto join(Begin begin, End end, Delim delim) -> std::basic_string<std::decay_t<decltype(*delim)>>
{
  using Ch = std::decay_t<decltype(*delim)>;
  std::basic_stringstream<Ch> str;
  if (begin != end)
  {
    str << *begin;
    while (++begin != end)
      str << delim << *begin;
  }
  return str.str();
}

static std::wstring ErrorToString(DWORD error)
{
  wchar_t buf[0x1000];

  FormatMessageW(
    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr,
    error,
    MAKELANGID(LANG_USER_DEFAULT, SUBLANG_DEFAULT),
    buf,
    (DWORD)std::size(buf),
    nullptr
  );
  std::wstring wstr{ buf };
  const auto pos = wstr.find_last_not_of(L"\r\n");
  if (pos != std::wstring::npos)
    wstr.resize(pos);
  return wstr;
}

static std::wstring GetWindowTextStr(HWND hwnd)
{
  const auto len = GetWindowTextLengthW(hwnd);
  std::wstring str;
  str.resize(len);
  GetWindowTextW(hwnd, str.data(), len + 1);
  return str;
}

static BOOL SetWindowTextFromResourceString(HWND hwnd, UINT str)
{
  return SetWindowText(hwnd, res::String(str).c_str());
}

static BOOL SetDlgItemTextFromResourceString(HWND dlg, int id, UINT str)
{
  const auto hwnd = GetDlgItem(dlg, id);
  if (hwnd)
    return SetWindowTextFromResourceString(hwnd, str);
  return false;
}

static void SaveDialog(HWND hWnd, HWND hEdit)
{
  // we have to make a huge buffer since GetOpenFileName will fail with throwing away the path if buffer too small
  wchar_t name[MAXWORD / 2];
  Edit_GetText(hEdit, name, (int)std::size(name));
  
  OPENFILENAME of = { sizeof(OPENFILENAME), hWnd };
  of.lpstrFile = name;
  of.nMaxFile = (DWORD)std::size(name);
  of.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
  if (!GetOpenFileName(&of))
  {
    const auto error = CommDlgExtendedError();
    // if error is 0 the user just cancelled the action
    if (error)
    {
      MessageBoxW(
        hWnd,
        fmt::format(L"GetSaveFileName: {:04X}", error).c_str(),
        res::String(IDS_ERROR).c_str(),
        MB_ICONERROR | MB_OK
      );
      name[0] = 0;
    }
    else
      return; // Don't change if user cancelled
  }
  Edit_SetText(hEdit, name);
}

static bool xIsWindowsVistaOrGreater()
{
  OSVERSIONINFOEXW osvi = { sizeof(osvi) };
  auto const dwlConditionMask = VerSetConditionMask(
    VerSetConditionMask(
      VerSetConditionMask(
        0, VER_MAJORVERSION, VER_GREATER_EQUAL),
      VER_MINORVERSION, VER_GREATER_EQUAL),
    VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
  osvi.dwMajorVersion = HIBYTE(_WIN32_WINNT_VISTA);
  osvi.dwMinorVersion = LOBYTE(_WIN32_WINNT_VISTA);
  osvi.wServicePackMajor = 0;

  return VerifyVersionInfoW(
    &osvi,
    VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR,
    dwlConditionMask
  ) != FALSE;
}

void MainDialog::UpdateDisks()
{
  const auto combo = GetCtl(IDC_DEVICES);

  // make sure noone points at disks
  ComboBox_ResetContent(combo);
  _disks = GetDisks();
  for (const auto& disk : _disks)
  {
    std::vector<std::string> colon(disk.letters.size());
    std::transform(
      begin(disk.letters),
      end(disk.letters),
      begin(colon),
      [](char c)
      {
        return std::string{ c } +":";
      });

    wchar_t name[0x100];
    swprintf_s(
      name,
      L"(%.2f GiB) %S (%S)",
      float(disk.size_in_bytes) / 1024.f / 1024.f / 1024.f,
      disk.product_id.c_str(),
      //disk.serial.c_str(),
      join(begin(colon), end(colon), " ").c_str()
    );
    const auto idx = ComboBox_AddString(combo, name);
    ComboBox_SetItemData(combo, idx, &disk);
  }
}

// 1. verify parameters
// 2. open file if operation needs one
// 3. open all volumes on target disk
// 4. lock all volumes
// 5. unmount all volumes
// 6. open disk for writing
// 7. start a thread for the operation
void MainDialog::StartOperation()
{
  Operation op;
  if (Button_GetCheck(GetCtl(IDC_OPERATION_RAW)))
    op = Operation::Flash;
  else if (Button_GetCheck(GetCtl(IDC_OPERATION_TRASH)))
    op = Operation::Trash;
  else
  {
    const auto err = ErrorToString(ERROR_INVALID_OPERATION);
    MessageBox(
      _handle,
      err.c_str(),
      err.c_str(),
      MB_OK | MB_ICONERROR
    );
    return;
  }

  disk_info* disk;
  {
    const auto ctl = GetCtl(IDC_DEVICES);
    disk = (disk_info*)ComboBox_GetItemData(ctl, ComboBox_GetCurSel(ctl));
  }
  if(!disk)
  {
    const auto err = ErrorToString(ERROR_BAD_DEVICE);
    MessageBox(
      _handle,
      err.c_str(),
      err.c_str(),
      MB_OK | MB_ICONERROR
    );
    return;
  }

  auto file_handle = INVALID_HANDLE_VALUE;
  if(op == Operation::Flash)
  {
    const auto file_path = GetWindowTextStr(GetCtl(IDC_FILE));

    if (file_path.empty())
      return;

    file_handle = CreateFile(
      file_path.c_str(),
      FILE_GENERIC_READ,
      0,
      nullptr,
      OPEN_EXISTING,
      0,
      nullptr
    );

    if(file_handle == INVALID_HANDLE_VALUE)
    {
      const auto err = ErrorToString(ERROR_OPEN_FAILED);
      MessageBox(
        _handle,
        fmt::format(L"{}: {}", file_path, ErrorToString(GetLastError())).c_str(),
        ErrorToString(ERROR_OPEN_FAILED).c_str(),
        MB_OK | MB_ICONERROR
      );
      return;
    }
  }

  std::vector<HANDLE> volume_handles;
  volume_handles.reserve(disk->stable_volumes.size());
  auto succeeded = true;
  for(const auto& volume_path : disk->stable_volumes)
  {
    const auto volume_handle = CreateFile(
        volume_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
      );

    if(volume_handle == INVALID_HANDLE_VALUE)
    {
      const auto err = ErrorToString(ERROR_OPEN_FAILED);
      MessageBox(
        _handle,
        fmt::format(L"{}: {}", volume_path, ErrorToString(GetLastError())).c_str(),
        ErrorToString(ERROR_OPEN_FAILED).c_str(),
        MB_OK | MB_ICONERROR
      );
      succeeded = false;
      break;
    }

    volume_handles.push_back(volume_handle);
  }

  if(succeeded)
  {
    for(auto i = 0u; i < volume_handles.size(); ++i)
    {
      if(!vol::Lock(volume_handles[i]))
      {
        MessageBox(
          _handle,
          fmt::format(L"{}: {}", disk->stable_volumes[i], ErrorToString(GetLastError())).c_str(),
          ErrorToString(ERROR_LOCK_FAILED).c_str(),
          MB_OK | MB_ICONERROR
        );
        succeeded = false;
        break;
      }
    }

    if(succeeded)
    {
      for (auto i = 0u; i < volume_handles.size(); ++i)
      {
        if (!vol::Unmount(volume_handles[i]))
        {
          MessageBox(
            _handle,
            fmt::format(L"{}: {}", disk->stable_volumes[i], ErrorToString(GetLastError())).c_str(),
            ErrorToString(ERROR_FLT_VOLUME_ALREADY_MOUNTED).c_str(),
            MB_OK | MB_ICONERROR
          );
          succeeded = false;
          break;
        }
      }

      if(succeeded)
      {
        const auto disk_handle = CreateFile(
          disk->stable_path.c_str(),
          GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE,
          nullptr,
          OPEN_EXISTING,
          0,
          nullptr
        );

        if(disk_handle == INVALID_HANDLE_VALUE)
        {
          MessageBox(
            _handle,
            fmt::format(L"{}: {}", disk->stable_path, ErrorToString(GetLastError())).c_str(),
            ErrorToString(ERROR_OPEN_FAILED).c_str(),
            MB_OK | MB_ICONERROR
          );
        }
        else
        {
          auto can_continue = true;

          if(file_handle != INVALID_HANDLE_VALUE)
          {
            auto write_size = file::GetSize(file_handle);
            if (write_size > disk->size_in_bytes)
            {
              const auto answer = MessageBox(
                _handle,
                fmt::format(res::String(IDS_WRITE_MORE_THAN_DISK), disk->size_in_bytes, write_size).c_str(),
                res::String(IDS_WARNING).c_str(),
                MB_YESNO | MB_ICONWARNING
              );
              if (answer != IDYES)
                can_continue = false;
            }
          }

          if(can_continue)
          {
            const auto params = new WorkerThread{
              volume_handles,
              disk_handle,
              file_handle,
              _handle,
              disk->size_in_bytes,
              op
            };

            const auto thread = CreateThread(
              nullptr,
              0,
              &WorkerThread::Callback,
              params,
              0,
              nullptr
            );

            if (thread)
            {
              // disable it here as a precaution
              Button_Enable(GetCtl(IDC_STARTSTOP), FALSE);

              return; // don't let the freeing code run
            }
            else
            {
              MessageBox(
                _handle,
                fmt::format(L"{}: {}", disk->stable_path, ErrorToString(GetLastError())).c_str(),
                ErrorToString(ERROR_INVALID_THREAD_ID).c_str(),
                MB_OK | MB_ICONERROR
              );

              delete params;

              // fall through, let the freeing code run
            }
          }
        }
      }
    }

    // always try to unlock all of them, unlocking non-locked will just fail silently
    for (const auto h : volume_handles)
      vol::Unlock(h);
  }

  for (auto h : volume_handles)
    CloseHandle(h);

  if (file_handle != INVALID_HANDLE_VALUE)
    CloseHandle(file_handle);
}

void MainDialog::OnClose()
{
  if(_is_running)
  {
    MessageBox(
      _handle,
      res::String(IDS_CANNOT_QUIT_WHILE_RUNNING).c_str(),
      res::String(IDS_ERROR).c_str(),
      MB_OK | MB_ICONERROR
    );
  }
  else
  {
    DestroyWindow(_handle);
  }
}

void MainDialog::LocalizeStrings()
{
#define LOCALIZE(x) SetDlgItemTextFromResourceString(_handle, IDC_ ## x, IDS_DLG_ ## x)

  LOCALIZE(0_LICENSE);
  LOCALIZE(1_DEVICE);
  LOCALIZE(2_OPERATION);
  LOCALIZE(3_FILE);
  LOCALIZE(4_FLASH);
  LOCALIZE(LICENSE_BTN);
  LOCALIZE(LICENSE_CHECK);
  LOCALIZE(WARRANTY_CHECK);
  LOCALIZE(REFRESH);
  LOCALIZE(OPERATION_RAW);
  LOCALIZE(OPERATION_TRASH);
  LOCALIZE(BROWSE);

  SetDlgItemTextFromResourceString(_handle, IDCLOSE, IDS_CLOSE);
  SetDlgItemTextFromResourceString(_handle, IDC_STARTSTOP, IDS_START);

#undef LOCALIZE
}

INT_PTR MainDialog::DlgProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  UNREFERENCED_PARAMETER(lParam);
  switch (uMsg)
  {
  case WM_INITDIALOG:
    UpdateDisks();
    LocalizeStrings();
    return TRUE;

  case WM_COMMAND:
    if (HIWORD(wParam) == BN_CLICKED)
    {
      switch (LOWORD(wParam))
      {
      case IDC_LICENSE_BTN:
        DialogBoxParam(
          g_instance,
          MAKEINTRESOURCE(IDD_LICENSE_DIALOG),
          _handle,
          &DlgProcClassBinder<LicenseDialog>,
          0
        );
        return TRUE;
      case IDC_LICENSE_CHECK:
      case IDC_WARRANTY_CHECK:
        Button_Enable((HWND)lParam, FALSE);
        if (Button_GetCheck(GetCtl(IDC_LICENSE_CHECK))
          && Button_GetCheck(GetCtl(IDC_WARRANTY_CHECK)))
          Button_Enable(GetCtl(IDC_STARTSTOP), TRUE);
        return TRUE;
      case IDC_REFRESH:
        UpdateDisks();
        return TRUE;
      case IDC_BROWSE:
        SaveDialog(_handle, GetCtl(IDC_FILE));
        return TRUE;
      case IDC_OPERATION_RAW:
        Button_Enable(GetCtl(IDC_BROWSE), TRUE);
        Edit_Enable(GetCtl(IDC_FILE), TRUE);
        return TRUE;
      case IDC_OPERATION_TRASH:
        Button_Enable(GetCtl(IDC_BROWSE), FALSE);
        Edit_Enable(GetCtl(IDC_FILE), FALSE);
        return TRUE;
      case IDC_STARTSTOP:
        if (_is_running)
          _to_be_stopped = true;
        else
          StartOperation();
        return TRUE;
      case IDCLOSE:
      case IDCANCEL:
        OnClose();
        return TRUE;
      }
    }
    break;

  case WM_FLASH_STARTED:
    if (wParam != message_magic_wparam)
      return FALSE;
    _is_running = true;
    _to_be_stopped = false;
    if (xIsWindowsVistaOrGreater())
      SendMessage(GetCtl(IDC_PROGRESS), (WM_USER + 16), 0x0001, 0); // SETSTATE -> NORMAL
    SetDlgItemText(_handle, IDC_PROGRESS, TEXT(""));
    SendMessage(GetCtl(IDC_PROGRESS), PBM_SETRANGE32, 0, MAXLONG);
    Button_Enable(GetCtl(IDC_STARTSTOP), TRUE);
    SetDlgItemTextFromResourceString(_handle, IDC_STARTSTOP, IDS_CANCEL);
    return TRUE;

  case WM_FLASH_PROGRESS:
    if (wParam != message_magic_wparam)
      return FALSE;
    SendMessage(GetCtl(IDC_PROGRESS), PBM_SETPOS, lParam, 0);
    SetWindowLong(_handle, DWLP_MSGRESULT, _to_be_stopped);
    return TRUE;

  case WM_FLASH_FINISHED:
  {
    _is_running = false;
    if (wParam != message_magic_wparam)
      return FALSE;
    if (xIsWindowsVistaOrGreater())
      SendMessage(GetCtl(IDC_PROGRESS), (WM_USER + 16), 0x0002, 0); // SETSTATE -> ERROR
    const auto str = ErrorToString((DWORD)lParam);
    SetDlgItemText(_handle, IDC_PROGRESS, str.c_str());
    MessageBox(_handle, str.c_str(), res::String(lParam ? IDS_ERROR : IDS_SUCCESS).c_str(), MB_OK);
    SetDlgItemTextFromResourceString(_handle, IDC_STARTSTOP, IDS_START);
    return TRUE;
  }

  case WM_CLOSE:
    OnClose();
    return TRUE;

  case WM_DESTROY:
    PostQuitMessage(0);
    return TRUE;
  }
  return FALSE;
}
