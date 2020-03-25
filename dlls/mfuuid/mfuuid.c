/*
 * GUID definitions
 *
 * Copyright 2017 Fabian Maurer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* Don't define those GUIDs here */
#include "propsys.h"
#include "strmif.h"
#include "mediaobj.h"

#undef EXTERN_GUID
#define EXTERN_GUID DEFINE_GUID

#include "initguid.h"

#include "mfapi.h"
#include "mfidl.h"
#include "mfreadwrite.h"
#include "mfmediaengine.h"

DEFINE_GUID(MF_SCRUBBING_SERVICE, 0xdd0ac3d8,0x40e3,0x4128,0xac,0x48,0xc0,0xad,0xd0,0x67,0xb7,0x14);
