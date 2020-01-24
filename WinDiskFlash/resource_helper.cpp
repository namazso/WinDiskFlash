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

#include "resource_helper.hpp"
#include "resource.h"
#include "main.hpp"

//extern "C" IMAGE_DOS_HEADER __ImageBase;

// could be std::string_view, however it has no c_str() and these aren't null terminated
std::wstring res::String(UINT uID)
{
  PCWSTR v = nullptr;
  const auto len = LoadString(g_instance, uID, (PWSTR)&v, 0);
  return { v, v + len };
}

template <typename T>
static void ReplaceStringInPlace(
  std::basic_string<T>& subject,
  const std::basic_string<T>& search,
  const std::basic_string<T>& replace
)
{
  size_t pos = 0;
  while ((pos = subject.find(search, pos)) != std::basic_string<T>::npos)
  {
    subject.replace(pos, search.length(), replace);
    pos += replace.length();
  }
}

std::pair<const char*, size_t> GetResource(WORD type, WORD id)
{
  const auto rc = FindResource(
    g_instance,
    MAKEINTRESOURCE(id),
    MAKEINTRESOURCE(type)
  );
  const auto rc_data = LoadResource(g_instance, rc);
  const auto size = SizeofResource(g_instance, rc);
  const auto data = static_cast<const char*>(LockResource(rc_data));
  return { data, size };
}

std::wstring res::GplText()
{
  const auto res = GetResource(RESTYPE_BLOB, IDR_GPL_TEXT);
  // this is ASCII, so we can allow ourselves to simply cast each char to wchar_t
  auto wstr = std::wstring{ res.first, res.first + res.second };
  ReplaceStringInPlace<wchar_t>(wstr, L"\n", L"\r\n");
  return wstr;
}