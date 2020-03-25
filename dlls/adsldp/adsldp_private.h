/*
 * Copyright 2020 Dmitry Timoshkov
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

#ifndef _ADSLDP_PRIVATE_H
#define _ADSLDP_PRIVATE_H

#include "wine/heap.h"

static inline WCHAR *strdupW(const WCHAR *src)
{
    WCHAR *dst;
    if (!src) return NULL;
    if ((dst = heap_alloc((wcslen(src) + 1) * sizeof(WCHAR)))) wcscpy(dst, src);
    return dst;
}

DWORD map_ldap_error(DWORD) DECLSPEC_HIDDEN;

#endif
