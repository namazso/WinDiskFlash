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

#include "worker_thread.hpp"
#include "file_helpers.hpp"

#define SECTOR_SIZE 512

DWORD WINAPI WorkerThread::Callback(LPVOID lpThreadParameter)
{
  const auto wt = (WorkerThread*)lpThreadParameter;
  wt->Thread();
  delete wt;
  return 0;
}

void WorkerThread::Thread()
{
  NotifyStarted();

  DWORD status;

  switch (operation)
  {
  case Operation::Trash:
    status = DoTrash();
    break;
  case Operation::Flash:
    status = DoFlash();
    break;
  case Operation::Save:
    status = DoSave();
    break;
  // should never happen since operation is sanitized by main dialog
  // regardless, we need to make the compiler shut up
  default:
    status = ERROR_INVALID_OPERATION;
    assert(false);
    break;
  }

  if (status == ERROR_SUCCESS)
    NotifyProgress(1.);

  // always try to unlock all of them, unlocking non-locked will just fail silently
  for (const auto h : volumes)
  {
    vol::Unlock(h);
    CloseHandle(h);
  }

  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);

  NotifyFinish(status);
}

DWORD WorkerThread::DoTrash()
{
  char empty[SECTOR_SIZE]{};

  for (auto i = 0u; i < 35; ++i)
  {
    const auto should_stop = NotifyProgress(i / 70.);

    if (should_stop)
      return ERROR_CANCELLED;

    if (SECTOR_SIZE != file::WriteAtOffset(disk, i * SECTOR_SIZE, empty, SECTOR_SIZE))
      return GetLastError();
  }

  for (auto i = 0u; i < 35; ++i)
  {
    const auto should_stop = NotifyProgress((i + 35) / 70.);

    if (should_stop)
      return ERROR_CANCELLED;
    const auto offset = disk_size_in_bytes - (35 - i) * SECTOR_SIZE;
    if (SECTOR_SIZE != file::WriteAtOffset(disk, offset, empty, SECTOR_SIZE))
      return GetLastError();
  }

  return ERROR_SUCCESS;
}

DWORD WorkerThread::DoFlash(uint64_t off)
{
  constexpr auto step = 16 << 20; // 16 MB at a time
  static_assert(step > SECTOR_SIZE);
  static_assert(step % SECTOR_SIZE == 0);
  // make it page aligned
  using storage = std::aligned_storage_t<step, 0x1000>;
  const auto buf = std::make_unique<storage>();

  const auto size = file::GetSize(file);
  if (!size || size == (uint64_t)-1)
    return GetLastError();
  const auto size_aligned = size & ~(uint64_t)(SECTOR_SIZE - 1);

  for(uint64_t i = 0; i < size_aligned; i += step)
  {
    auto curr_size = size_aligned - i;
    if (curr_size > step)
      curr_size = step;
    if (curr_size != file::Copy(disk, off + i, file, i, buf.get(), (size_t)curr_size))
      return GetLastError();
    if (NotifyProgress(double(off + i + curr_size) / double(off + size_aligned)))
      return ERROR_CANCELLED;
  }
  if(const auto remain = size - size_aligned)
  {
    memset(buf.get(), 0, SECTOR_SIZE);
    if (remain != file::ReadAtOffset(file, size_aligned, buf.get(), (size_t)remain))
      return GetLastError();
    if(SECTOR_SIZE != file::WriteAtOffset(disk, off + size_aligned, buf.get(), SECTOR_SIZE))
      return GetLastError();
  }
  return ERROR_SUCCESS;
}

DWORD WorkerThread::DoSave()
{
  constexpr auto step = 16 << 20; // 16 MB at a time
  static_assert(step > SECTOR_SIZE);
  static_assert(step % SECTOR_SIZE == 0);
  // make it page aligned
  using storage = std::aligned_storage_t<step, 0x1000>;
  const auto buf = std::make_unique<storage>();

  const auto size = disk_size_in_bytes;

  for (uint64_t i = 0; i < size; i += step)
  {
    auto curr_size = size - i;
    if (curr_size > step)
      curr_size = step;
    if (curr_size != file::Copy(file, i, disk, i, buf.get(), (size_t)curr_size))
      return GetLastError();
    if (NotifyProgress(double(i + curr_size) / double(size)))
      return ERROR_CANCELLED;
  }
  return ERROR_SUCCESS;
}

LRESULT WorkerThread::MagicMessage(UINT uMsg, LPARAM lParam)
{
  return SendMessage(
    window,
    uMsg,
    MainDialog::message_magic_wparam,
    lParam
  );
}

void WorkerThread::NotifyStarted()
{
  MagicMessage(MainDialog::WM_FLASH_STARTED, 0);
}

bool WorkerThread::NotifyProgress(double progress)
{
  return (bool)MagicMessage(MainDialog::WM_FLASH_PROGRESS, (LPARAM)round(progress * MAXLONG));
}

void WorkerThread::NotifyFinish(DWORD status)
{
  MagicMessage(MainDialog::WM_FLASH_FINISHED, status);
}
