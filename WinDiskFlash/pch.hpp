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
#include "targetver.h"

// Windows Header Files
#define NOMINMAX
#include <Windows.h>

// we don't require std::aligned_storage in any binary compatible way
#define _ENABLE_EXTENDED_ALIGNED_STORAGE

#include <string>
#include <sstream>
#include <vector>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <locale>
#include <cstdlib>

#define FMT_HEADER_ONLY
#include "../fmt/format.h"