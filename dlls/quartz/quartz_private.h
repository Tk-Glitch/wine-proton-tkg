/*              DirectShow private interfaces (QUARTZ.DLL)
 *
 * Copyright 2002 Lionel Ulmer
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

#ifndef __QUARTZ_PRIVATE_INCLUDED__
#define __QUARTZ_PRIVATE_INCLUDED__

#include <stdarg.h>
#include <wchar.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "wtypes.h"
#include "wingdi.h"
#include "winuser.h"
#include "dshow.h"
#include "wine/debug.h"
#include "wine/heap.h"
#include "wine/strmbase.h"
#include "wine/list.h"

static inline const char *debugstr_time(REFERENCE_TIME time)
{
    ULONGLONG abstime = time >= 0 ? time : -time;
    unsigned int i = 0, j = 0;
    char buffer[23], rev[23];

    while (abstime || i <= 8)
    {
        buffer[i++] = '0' + (abstime % 10);
        abstime /= 10;
        if (i == 7) buffer[i++] = '.';
    }
    if (time < 0) buffer[i++] = '-';

    while (i--) rev[j++] = buffer[i];
    rev[j] = 0;

    return wine_dbg_sprintf("%s", rev);
}

extern LONG object_locks;

/* see IAsyncReader::Request on MSDN for the explanation of this */
#define MEDIATIME_FROM_BYTES(x) ((LONGLONG)(x) * 10000000)
#define BYTES_FROM_MEDIATIME(time) ((time) / 10000000)

HRESULT acm_wrapper_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT avi_dec_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT async_reader_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT dsound_render_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT filter_graph_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT filter_graph_no_thread_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT filter_mapper_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT mem_allocator_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT system_clock_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT seeking_passthrough_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT video_renderer_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT video_renderer_default_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT vmr7_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;
HRESULT vmr9_create(IUnknown *outer, IUnknown **out) DECLSPEC_HIDDEN;

HRESULT EnumMonikerImpl_Create(IMoniker ** ppMoniker, ULONG nMonikerCount, IEnumMoniker ** ppEnum) DECLSPEC_HIDDEN;

HRESULT IEnumRegFiltersImpl_Construct(REGFILTER * pInRegFilters, const ULONG size, IEnumRegFilters ** ppEnum) DECLSPEC_HIDDEN;

extern const char * qzdebugstr_guid(const GUID * id) DECLSPEC_HIDDEN;
extern void video_unregister_windowclass(void) DECLSPEC_HIDDEN;

BOOL get_media_type(const WCHAR *filename, GUID *majortype, GUID *subtype, GUID *source_clsid) DECLSPEC_HIDDEN;

typedef struct tagBaseWindow
{
    HWND hWnd;
    LONG Width;
    LONG Height;

    const struct BaseWindowFuncTable* pFuncsTable;
} BaseWindow;

typedef RECT (WINAPI *BaseWindow_GetDefaultRect)(BaseWindow *This);
typedef BOOL (WINAPI *BaseWindow_OnSize)(BaseWindow *This, LONG Height, LONG Width);

typedef struct BaseWindowFuncTable
{
    /* Required */
    BaseWindow_GetDefaultRect pfnGetDefaultRect;
    /* Optional, WinProc Related */
    BaseWindow_OnSize pfnOnSize;
} BaseWindowFuncTable;

HRESULT WINAPI BaseWindow_Init(BaseWindow *pBaseWindow, const BaseWindowFuncTable* pFuncsTable) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseWindow_Destroy(BaseWindow *pBaseWindow) DECLSPEC_HIDDEN;

HRESULT WINAPI BaseWindowImpl_PrepareWindow(BaseWindow *This) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseWindowImpl_DoneWithWindow(BaseWindow *This) DECLSPEC_HIDDEN;

typedef struct tagBaseControlWindow
{
    BaseWindow baseWindow;
    IVideoWindow IVideoWindow_iface;

    BOOL AutoShow;
    HWND hwndDrain;
    HWND hwndOwner;
    struct strmbase_filter *pFilter;
    struct strmbase_pin *pPin;
} BaseControlWindow;

HRESULT video_window_init(BaseControlWindow *window, const IVideoWindowVtbl *vtbl,
        struct strmbase_filter *filter, struct strmbase_pin *pin, const BaseWindowFuncTable *func_table) DECLSPEC_HIDDEN;
void video_window_unregister_class(void) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindow_Destroy(BaseControlWindow *pControlWindow) DECLSPEC_HIDDEN;

BOOL WINAPI BaseControlWindowImpl_PossiblyEatMessage(BaseWindow *This, UINT uMsg, WPARAM wParam, LPARAM lParam) DECLSPEC_HIDDEN;

HRESULT WINAPI BaseControlWindowImpl_QueryInterface(IVideoWindow *iface, REFIID iid, void **out) DECLSPEC_HIDDEN;
ULONG WINAPI BaseControlWindowImpl_AddRef(IVideoWindow *iface) DECLSPEC_HIDDEN;
ULONG WINAPI BaseControlWindowImpl_Release(IVideoWindow *iface) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_GetTypeInfoCount(IVideoWindow *iface, UINT *pctinfo) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_GetTypeInfo(IVideoWindow *iface, UINT iTInfo, LCID lcid, ITypeInfo**ppTInfo) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_GetIDsOfNames(IVideoWindow *iface, REFIID riid, LPOLESTR*rgszNames, UINT cNames, LCID lcid, DISPID*rgDispId) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_Invoke(IVideoWindow *iface, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS*pDispParams, VARIANT*pVarResult, EXCEPINFO*pExepInfo, UINT*puArgErr) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_Caption(IVideoWindow *iface, BSTR strCaption) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_Caption(IVideoWindow *iface, BSTR *strCaption) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_WindowStyle(IVideoWindow *iface, LONG WindowStyle) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_WindowStyle(IVideoWindow *iface, LONG *WindowStyle) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_WindowStyleEx(IVideoWindow *iface, LONG WindowStyleEx) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_WindowStyleEx(IVideoWindow *iface, LONG *WindowStyleEx) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_AutoShow(IVideoWindow *iface, LONG AutoShow) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_AutoShow(IVideoWindow *iface, LONG *AutoShow) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_WindowState(IVideoWindow *iface, LONG WindowState) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_WindowState(IVideoWindow *iface, LONG *WindowState) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_BackgroundPalette(IVideoWindow *iface, LONG BackgroundPalette) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_BackgroundPalette(IVideoWindow *iface, LONG *pBackgroundPalette) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_Visible(IVideoWindow *iface, LONG Visible) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_Visible(IVideoWindow *iface, LONG *pVisible) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_Left(IVideoWindow *iface, LONG Left) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_Left(IVideoWindow *iface, LONG *pLeft) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_Width(IVideoWindow *iface, LONG Width) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_Width(IVideoWindow *iface, LONG *pWidth) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_Top(IVideoWindow *iface, LONG Top) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_Top(IVideoWindow *iface, LONG *pTop) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_Height(IVideoWindow *iface, LONG Height) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_Height(IVideoWindow *iface, LONG *pHeight) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_Owner(IVideoWindow *iface, OAHWND Owner) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_Owner(IVideoWindow *iface, OAHWND *Owner) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_MessageDrain(IVideoWindow *iface, OAHWND Drain) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_MessageDrain(IVideoWindow *iface, OAHWND *Drain) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_BorderColor(IVideoWindow *iface, LONG *Color) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_BorderColor(IVideoWindow *iface, LONG Color) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_get_FullScreenMode(IVideoWindow *iface, LONG *FullScreenMode) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_put_FullScreenMode(IVideoWindow *iface, LONG FullScreenMode) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_SetWindowForeground(IVideoWindow *iface, LONG Focus) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_SetWindowPosition(IVideoWindow *iface, LONG Left, LONG Top, LONG Width, LONG Height) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_GetWindowPosition(IVideoWindow *iface, LONG *pLeft, LONG *pTop, LONG *pWidth, LONG *pHeight) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_NotifyOwnerMessage(IVideoWindow *iface, OAHWND hwnd, LONG uMsg, LONG_PTR wParam, LONG_PTR lParam) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_GetMinIdealImageSize(IVideoWindow *iface, LONG *pWidth, LONG *pHeight) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_GetMaxIdealImageSize(IVideoWindow *iface, LONG *pWidth, LONG *pHeight) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_GetRestorePosition(IVideoWindow *iface, LONG *pLeft, LONG *pTop, LONG *pWidth, LONG *pHeight) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_HideCursor(IVideoWindow *iface, LONG HideCursor) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlWindowImpl_IsCursorHidden(IVideoWindow *iface, LONG *CursorHidden) DECLSPEC_HIDDEN;

typedef struct tagBaseControlVideo
{
    IBasicVideo IBasicVideo_iface;

    struct strmbase_filter *pFilter;
    struct strmbase_pin *pPin;

    const struct BaseControlVideoFuncTable *pFuncsTable;
} BaseControlVideo;

typedef HRESULT (WINAPI *BaseControlVideo_GetSourceRect)(BaseControlVideo* This, RECT *pSourceRect);
typedef HRESULT (WINAPI *BaseControlVideo_GetStaticImage)(BaseControlVideo* This, LONG *pBufferSize, LONG *pDIBImage);
typedef HRESULT (WINAPI *BaseControlVideo_GetTargetRect)(BaseControlVideo* This, RECT *pTargetRect);
typedef VIDEOINFOHEADER* (WINAPI *BaseControlVideo_GetVideoFormat)(BaseControlVideo* This);
typedef HRESULT (WINAPI *BaseControlVideo_IsDefaultSourceRect)(BaseControlVideo* This);
typedef HRESULT (WINAPI *BaseControlVideo_IsDefaultTargetRect)(BaseControlVideo* This);
typedef HRESULT (WINAPI *BaseControlVideo_SetDefaultSourceRect)(BaseControlVideo* This);
typedef HRESULT (WINAPI *BaseControlVideo_SetDefaultTargetRect)(BaseControlVideo* This);
typedef HRESULT (WINAPI *BaseControlVideo_SetSourceRect)(BaseControlVideo* This, RECT *pSourceRect);
typedef HRESULT (WINAPI *BaseControlVideo_SetTargetRect)(BaseControlVideo* This, RECT *pTargetRect);

typedef struct BaseControlVideoFuncTable
{
    BaseControlVideo_GetSourceRect pfnGetSourceRect;
    BaseControlVideo_GetStaticImage pfnGetStaticImage;
    BaseControlVideo_GetTargetRect pfnGetTargetRect;
    BaseControlVideo_GetVideoFormat pfnGetVideoFormat;
    BaseControlVideo_IsDefaultSourceRect pfnIsDefaultSourceRect;
    BaseControlVideo_IsDefaultTargetRect pfnIsDefaultTargetRect;
    BaseControlVideo_SetDefaultSourceRect pfnSetDefaultSourceRect;
    BaseControlVideo_SetDefaultTargetRect pfnSetDefaultTargetRect;
    BaseControlVideo_SetSourceRect pfnSetSourceRect;
    BaseControlVideo_SetTargetRect pfnSetTargetRect;
} BaseControlVideoFuncTable;

HRESULT basic_video_init(BaseControlVideo *video, struct strmbase_filter *filter,
        struct strmbase_pin *pin, const BaseControlVideoFuncTable *func_table) DECLSPEC_HIDDEN;
HRESULT WINAPI BaseControlVideo_Destroy(BaseControlVideo *pControlVideo) DECLSPEC_HIDDEN;

#endif /* __QUARTZ_PRIVATE_INCLUDED__ */
