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

#include "file_helpers.hpp"

// returns -1 on failure, get error via GetLastError()
static int64_t xSetFilePointerEx(HANDLE file, int64_t distance, DWORD move_method)
{
  LARGE_INTEGER to_move;
  to_move.QuadPart = distance;
  LARGE_INTEGER new_pos;
  return SetFilePointerEx(
    file,
    to_move,
    &new_pos,
    move_method
  ) ? new_pos.QuadPart : -1;
}

using tpRtlNtStatusToDosError = ULONG(NTAPI*)(
  _In_ NTSTATUS Status
  );

typedef struct _IO_STATUS_BLOCK
{
  union
  {
    NTSTATUS Status;
    PVOID Pointer;
  };
  ULONG_PTR Information;
} IO_STATUS_BLOCK, * PIO_STATUS_BLOCK;

using tpNtWriteFile = NTSTATUS(NTAPI*)(
  _In_ HANDLE FileHandle,
  _In_opt_ HANDLE Event,
  _In_opt_ /*PIO_APC_ROUTINE*/ PVOID ApcRoutine,
  _In_opt_ PVOID ApcContext,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_reads_bytes_(Length) PVOID Buffer,
  _In_ ULONG Length,
  _In_opt_ PLARGE_INTEGER ByteOffset,
  _In_opt_ PULONG Key
  );

using tpNtReadFile = NTSTATUS(NTAPI*)(
  _In_ HANDLE FileHandle,
  _In_opt_ HANDLE Event,
  _In_opt_ /*PIO_APC_ROUTINE*/ PVOID ApcRoutine,
  _In_opt_ PVOID ApcContext,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _Out_writes_bytes_(Length) PVOID Buffer,
  _In_ ULONG Length,
  _In_opt_ PLARGE_INTEGER ByteOffset,
  _In_opt_ PULONG Key
  );

static tpRtlNtStatusToDosError pRtlNtStatusToDosError = (tpRtlNtStatusToDosError)-1;
static tpNtWriteFile pNtWriteFile = nullptr;
static tpNtReadFile pNtReadFile = nullptr;

static bool IsNtAPIAvailable()
{
  if(pRtlNtStatusToDosError == (tpRtlNtStatusToDosError)-1)
  {
    if (const auto ntdll = GetModuleHandle(TEXT("ntdll")))
    {
      pRtlNtStatusToDosError = (tpRtlNtStatusToDosError)GetProcAddress(ntdll, "RtlNtStatusToDosError");
      pNtWriteFile = (tpNtWriteFile)GetProcAddress(ntdll, "NtWriteFile");
      pNtReadFile = (tpNtReadFile)GetProcAddress(ntdll, "NtReadFile");
      if (!pNtWriteFile || !pNtReadFile)
        pRtlNtStatusToDosError = nullptr;
    }
  }

  return pRtlNtStatusToDosError;
}

static uint32_t FileWriteAtOffset32(HANDLE file, uint64_t offset, const void* buf, uint32_t size)
{
  DWORD written = 0;

  // if available use the more sane NtWriteFile
  if (IsNtAPIAvailable())
  {
    IO_STATUS_BLOCK ios;
    LARGE_INTEGER offset_li;
    offset_li.QuadPart = offset;
    const auto status = pNtWriteFile(
      file,
      nullptr,
      nullptr,
      nullptr,
      &ios,
      (PVOID)buf,
      size,
      &offset_li,
      nullptr
    );
    const auto error = pRtlNtStatusToDosError(status);
    SetLastError(error);
    if(!error)
      written = (DWORD)ios.Information;
  }
  else
  {
    if (-1 != xSetFilePointerEx(file, offset, FILE_BEGIN))
      WriteFile(file, buf, size, &written, nullptr);
  }
  return written;
}

static uint32_t FileReadAtOffset32(HANDLE file, uint64_t offset, void* buf, uint32_t size)
{
  DWORD read = 0;

  // if available use the more sane NtWriteFile
  if (IsNtAPIAvailable())
  {
    IO_STATUS_BLOCK ios;
    LARGE_INTEGER offset_li;
    offset_li.QuadPart = offset;
    const auto status = pNtReadFile(
      file,
      nullptr,
      nullptr,
      nullptr,
      &ios,
      buf,
      size,
      &offset_li,
      nullptr
    );
    const auto error = pRtlNtStatusToDosError(status);
    SetLastError(error);
    if(!error)
      read = (DWORD)ios.Information;
  }
  else
  {
    if (-1 != xSetFilePointerEx(file, offset, FILE_BEGIN))
      ReadFile(file, buf, size, &read, nullptr);
  }
  return read;
}

size_t file::WriteAtOffset(HANDLE file, uint64_t offset, const void* buf, size_t size)
{
  size_t written = 0;
  // page align steps for performance
  constexpr static auto step = MAXDWORD & ~0xFFF;
  for (uint64_t i = 0u; i < size; i += step)
  {
    const auto remain = size - i;
    const auto dwsize = (DWORD)(remain > step ? step : remain);
    const auto dwwritten = FileWriteAtOffset32(file, offset + i, (char*)buf + i, dwsize);
    written += dwwritten;
    if (dwwritten != dwsize)
      break;
  }
  return written;
}

size_t file::ReadAtOffset(HANDLE file, uint64_t offset, void* buf, size_t size)
{
  size_t read = 0;
  // page align steps for performance
  constexpr static auto step = MAXDWORD & ~0xFFF;
  for (uint64_t i = 0u; i < size; i += step)
  {
    const auto remain = size - i;
    const auto dwsize = (DWORD)(remain > step ? step : remain);
    const auto dwread = FileReadAtOffset32(file, offset + i, (char*)buf + i, dwsize);
    read += dwread;
    if (dwread != dwsize)
      break;
  }
  return read;
}

uint64_t file::GetSize(HANDLE file)
{
  LARGE_INTEGER li;
  return GetFileSizeEx(file, &li) ? li.QuadPart : (uint64_t)-1;
}

size_t file::Copy(HANDLE dst, uint64_t dst_off, HANDLE src, uint64_t src_off, void* buf, size_t size)
{
  const auto readsize = ReadAtOffset(src, src_off, buf, size);
  const auto readerr = GetLastError();
  if (readsize)
  {
    const auto writesize = WriteAtOffset(dst, dst_off, buf, readsize);
    if (readsize != size)
      SetLastError(readerr);
    return writesize;
  }
  return 0;
}
