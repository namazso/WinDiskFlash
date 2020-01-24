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

#include <SetupAPI.h>

#pragma comment(lib, "Setupapi.lib")

static bool IsHex(char c)
{
  const auto u = uint8_t(c);
  return (u >= uint8_t('0') && u <= uint8_t('9'))
    || (u >= uint8_t('A') && u <= uint8_t('F'))
    || (u >= uint8_t('a') && u <= uint8_t('f'));
}

static uint8_t UnhexChar(char c)
{
  const auto u = uint8_t(c);
  if (u >= uint8_t('0') && u <= uint8_t('9'))
    return u - uint8_t('0');
  if (u >= uint8_t('A') && u <= uint8_t('F'))
    return u - uint8_t('A') + 0xA;
  if (u >= uint8_t('a') && u <= uint8_t('f'))
    return u - uint8_t('a') + 0xa;
  return 0xFF;
}

static uint8_t UnhexByte(char high, char low)
{
  const auto vhigh = UnhexChar(high);
  const auto vlow = UnhexChar(low);
  return (vhigh << 4) + vlow;
}

static std::string FixIDEString(const char* begin, const char* end, bool flip)
{
  // probably invalid offset
  if (begin >= end)
    return {};

  for (auto it = begin; it != end; ++it)
    if (!*it)
    {
      end = it;
      break;
    }

  auto hex = (end - begin) % 2 == 0;
  for (auto it = begin; hex && it != end; ++it)
    if (!IsHex(*it))
      hex = false;

  std::string str;
  if (hex)
    for (auto it = begin; it != end; it += 2)
      str.push_back((char)UnhexByte(it[0], it[1]));
  else
    for (auto it = begin; it != end; ++it)
      str.push_back(*it);

  const auto startpos = str.find_first_not_of(" \t\n\v\f\r");
  const auto endpos = str.find_last_not_of(" \t\n\v\f\r");

  if (startpos == std::string::npos)
    return {};

  str = str.substr(startpos, endpos - startpos + 1);

  for (auto& c : str)
    if (::isspace((uint8_t)c))
      c = ' ';
    else if (!::isprint((uint8_t)c))
      c = '?';

  if (flip)
    for (auto i = 0u; i < (str.size() & ~1); ++i)
      std::swap(str[i], str[i + 1]);

  return str;
}

// check if return empty. if so thats an error, get GetLastError() for extended info
static std::vector<uint8_t> GrowingDeviceIoControl(
  HANDLE        hDevice,
  DWORD         dwIoControlCode,
  LPVOID        lpInBuffer,
  DWORD         nInBufferSize
)
{
  std::vector<uint8_t> buf;
  buf.resize(8); // start off with 16 bytes
  DWORD BytesReturned;
  DWORD status;
  while (true)
  {
    buf.resize(buf.size() << 1);
    if (DeviceIoControl(
      hDevice,
      dwIoControlCode,
      lpInBuffer,
      nInBufferSize,
      buf.data(),
      (DWORD)buf.size(),
      &BytesReturned,
      nullptr
    ))
      status = ERROR_SUCCESS;
    else
      status = GetLastError();

    enum ResultState
    {
      TryAgain,
      ReturnError,
      Return
    } res;

    switch (status)
    {
    case ERROR_SUCCESS:
      // some shitty ioctls will silently trim the data, so grow until BytesReturned doesn't seem to grow anymore
      res = BytesReturned << 1 <= buf.size() ? Return : TryAgain;
      break;
    // some shitty ioctls return invalid parameter despite what the doc says, so just try until 1 MB
    case ERROR_INVALID_PARAMETER:
      res = buf.size() > 1 << 20 ? ReturnError : TryAgain;
      break;
    case ERROR_MORE_DATA:
    case ERROR_INSUFFICIENT_BUFFER:
      res = TryAgain;
      break;
    default:
      res = ReturnError;
      break;
    }

    if (res == Return)
    {
      buf.resize(BytesReturned);
      return buf;
    }
    if(res == ReturnError)
      return {};
  }
}

std::vector<DWORD> GetDisksForVolume(HANDLE h)
{
  const auto extents_buf = GrowingDeviceIoControl(
    h,
    IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
    nullptr,
    0
  );

  std::vector<DWORD> disks;

  if(!extents_buf.empty())
  {
    const auto extents = (PVOLUME_DISK_EXTENTS)extents_buf.data();

    for (auto j = 0u; j < extents->NumberOfDiskExtents; ++j)
    {
      const auto& extent = extents->Extents[j];

      // Workaround for RamPhantom EX 1.1 from CrystalDiskInfo
      // https://crystalmark.info/bbs/c-board.cgi?cmd=one;no=1178;id=diskinfo#1178
      if (extent.ExtentLength.QuadPart == 0)
        continue;

      disks.push_back(extent.DiskNumber);
    }
  }

  return disks;
}

static std::list<std::wstring> GetStablePathsForDevInterface(const GUID& guid)
{
  std::list<std::wstring> devices;

  // create a HDEVINFO with all present devices
  const auto devinfo_handle = SetupDiGetClassDevs(
    &guid,
    nullptr,
    nullptr,
    DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
  );
  if (devinfo_handle != INVALID_HANDLE_VALUE)
  {
    // enumerate through all devices in the set
    for (auto member_index = 0u; ; ++member_index)
    {
      // get device info
      SP_DEVINFO_DATA devinfo_data;
      devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
      if (!SetupDiEnumDeviceInfo(devinfo_handle, member_index, &devinfo_data))
        break; // either reached end or error happened. we don't care which happened

      // get device interfaces
      SP_DEVICE_INTERFACE_DATA dev_iface_data;
      dev_iface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
      if (SetupDiEnumDeviceInterfaces(
        devinfo_handle,
        nullptr,
        &guid,
        member_index,
        &dev_iface_data
      ))
      {
        // get device path
        ULONG required_size;
        std::unique_ptr<char[]> dev_iface_detail_data_buf;
        if(!SetupDiGetDeviceInterfaceDetail(
          devinfo_handle,
          &dev_iface_data,
          nullptr,
          0,
          &required_size,
          nullptr
        ) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
          dev_iface_detail_data_buf = std::make_unique<char[]>(required_size);
          const auto dev_iface_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)dev_iface_detail_data_buf.get();
          dev_iface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
          if (SetupDiGetDeviceInterfaceDetail(
            devinfo_handle,
            &dev_iface_data,
            dev_iface_detail_data,
            required_size,
            &required_size,
            nullptr
          ))
          {
            devices.emplace_back(dev_iface_detail_data->DevicePath);
          }
        }
      }
    }

    // destroy device info list
    SetupDiDestroyDeviceInfoList(devinfo_handle);
  }

  return devices;
}

static std::unordered_map<DWORD, std::vector<char>> GetDiskIdLetterMap()
{
  constexpr static auto number_of_letters = 'Z' - 'A' + 1;

  std::unordered_map<DWORD, std::vector<char>> disk_letters_map;

  const auto letter_bitmap = GetLogicalDrives();
  wchar_t letter_path[] = L"\\\\.\\A:";
  for (auto i = 0u; i < number_of_letters; ++i)
  {
    if (!(letter_bitmap & (1 << i)))
      continue;

    letter_path[4] = L'A' + i;

    const auto volume_handle = CreateFile(
      letter_path,
      0,
      FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );

    if (volume_handle == INVALID_HANDLE_VALUE)
      continue;

    const auto disks = GetDisksForVolume(volume_handle);

    CloseHandle(volume_handle);

    if (disks.empty())
      continue;

    for (const auto disk : disks)
      disk_letters_map[disk].push_back('A' + i);
  }

  return disk_letters_map;
}

static std::unordered_map<DWORD, std::vector<std::wstring>> GetDiskIdVolumeMap()
{
  std::unordered_map<DWORD, std::vector<std::wstring>> disk_volumes_map;

  const auto volume_stable_paths = GetStablePathsForDevInterface(GUID_DEVINTERFACE_VOLUME);

  for(const auto& vol : volume_stable_paths)
  {
    const auto volume_handle = CreateFile(
      vol.c_str(),
      0,
      FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );

    if (volume_handle == INVALID_HANDLE_VALUE)
      continue;

    const auto disks = GetDisksForVolume(volume_handle);

    CloseHandle(volume_handle);

    if (disks.empty())
      continue;

    for (const auto disk : disks)
      disk_volumes_map[disk].push_back(vol);
  }

  return disk_volumes_map;
}

struct relevant_disk_data
{
  std::string product_id;
  std::string serial;
  uint64_t size_in_bytes;
  uint32_t disk_id;
};

static bool GetRelevantDiskData(HANDLE h, relevant_disk_data& data)
{
  STORAGE_PROPERTY_QUERY query{};
  query.PropertyId = StorageDeviceProperty;
  query.QueryType = PropertyStandardQuery;

  const auto storage_property_buf = GrowingDeviceIoControl(
    h,
    IOCTL_STORAGE_QUERY_PROPERTY,
    &query,
    sizeof(query)
  );

  if (!storage_property_buf.empty())
  {
    const auto descriptor = (STORAGE_DEVICE_DESCRIPTOR*)storage_property_buf.data();

    /*data.vendor_id = FixIDEString(
      (char*)(storage_property_buf.data() + descriptor->VendorIdOffset),
      (char*)(storage_property_buf.data() + storage_property_buf.size()),
      false
    );*/
    data.product_id = FixIDEString(
      (char*)(storage_property_buf.data() + descriptor->ProductIdOffset),
      (char*)(storage_property_buf.data() + storage_property_buf.size()),
      false
    );
    /*data.revision = FixIDEString(
      (char*)(storage_property_buf.data() + descriptor->ProductRevisionOffset),
      (char*)(storage_property_buf.data() + storage_property_buf.size()),
      false
    );*/
    data.serial = descriptor->SerialNumberOffset == 0 ? "" : FixIDEString(
      (char*)(storage_property_buf.data() + descriptor->SerialNumberOffset),
      (char*)(storage_property_buf.data() + storage_property_buf.size()),
      true
    );

    const auto geometry_buf = GrowingDeviceIoControl(
      h,
      IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
      nullptr,
      0
    );

    if (!geometry_buf.empty())
    {
      const auto geometry = (PDISK_GEOMETRY_EX)geometry_buf.data();

      //const auto is_fixed = (geometry->Geometry.MediaType == FixedMedia);
      data.size_in_bytes = geometry->DiskSize.QuadPart;

      // get disk id
      STORAGE_DEVICE_NUMBER dev_number;
      DWORD returned_size;
      if (DeviceIoControl(
        h,
        IOCTL_STORAGE_GET_DEVICE_NUMBER,
        nullptr,
        0,
        &dev_number,
        sizeof(dev_number),
        &returned_size,
        nullptr
      ))
      {
        data.disk_id = dev_number.DeviceNumber;

        return true;
      }
    }
  }

  return false;
}

std::vector<disk_info> GetDisks()
{
  std::vector<disk_info> disks;

  const auto id_letter_map = GetDiskIdLetterMap();
  const auto id_volume_map = GetDiskIdVolumeMap();

  const auto disk_stable_paths = GetStablePathsForDevInterface(GUID_DEVINTERFACE_DISK);

  for(const auto& disk_path : disk_stable_paths)
  {
    const auto h = CreateFile(
      disk_path.c_str(),
      0,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      0,
      nullptr
    );

    if(h != INVALID_HANDLE_VALUE)
    {
      relevant_disk_data disk_data;
      GetRelevantDiskData(h, disk_data);

      disk_info disk;
      disk.size_in_bytes = disk_data.size_in_bytes;
      disk.product_id = std::move(disk_data.product_id);
      disk.serial = std::move(disk_data.serial);
      disk.stable_path = disk_path;
      //disk.stable_path = std::wstring{ L"\\\\.\\PhysicalDrive" } + std::to_wstring(disk_data.disk_id);
      const auto let_it = id_letter_map.find(disk_data.disk_id);
      if (let_it != end(id_letter_map))
        disk.letters = let_it->second;
      const auto vol_it = id_volume_map.find(disk_data.disk_id);
      if (vol_it != end(id_volume_map))
        disk.stable_volumes = vol_it->second;

      disks.push_back(std::move(disk));
    }
  }

  return disks;
}
