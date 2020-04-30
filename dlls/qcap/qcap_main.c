/*
 * DirectShow capture
 *
 * Copyright (C) 2003 Dominik Strasser
 * Copyright (C) 2005 Rolf Kalbermatter
 * Copyright (C) 2019 Zebediah Figura
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
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#define COBJMACROS
#define NONAMELESSSTRUCT
#define NONAMELESSUNION
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winerror.h"
#include "objbase.h"
#include "uuids.h"
#include "strmif.h"
#include "rpcproxy.h"

#include "qcap_main.h"

#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(qcap);

static HINSTANCE qcap_instance;

static LONG objects_ref = 0;

struct class_factory
{
    IClassFactory IClassFactory_iface;
    HRESULT (*create_instance)(IUnknown *outer, IUnknown **out);
};

static inline struct class_factory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct class_factory, IClassFactory_iface);
}

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory))
    {
        *out = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }

    *out = NULL;
    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct class_factory *factory = impl_from_IClassFactory(iface);
    IUnknown *unk;
    HRESULT hr;

    TRACE("iface %p, outer %p, iid %s, out %p.\n", iface, outer, debugstr_guid(iid), out);

    if (outer && !IsEqualGUID(iid, &IID_IUnknown))
        return E_NOINTERFACE;

    *out = NULL;
    if (SUCCEEDED(hr = factory->create_instance(outer, &unk)))
    {
        hr = IUnknown_QueryInterface(unk, iid, out);
        IUnknown_Release(unk);
    }
    return hr;
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    TRACE("iface %p, lock %d.\n", iface, lock);

    if (lock)
        InterlockedIncrement(&objects_ref);
    else
        InterlockedDecrement(&objects_ref);
    return S_OK;
}

static const IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer,
};

static struct class_factory audio_record_cf = {{&class_factory_vtbl}, audio_record_create};
static struct class_factory avi_compressor_cf = {{&class_factory_vtbl}, avi_compressor_create};
static struct class_factory avi_mux_cf = {{&class_factory_vtbl}, avi_mux_create};
static struct class_factory capture_graph_cf = {{&class_factory_vtbl}, capture_graph_create};
static struct class_factory file_writer_cf = {{&class_factory_vtbl}, file_writer_create};
static struct class_factory smart_tee_cf = {{&class_factory_vtbl}, smart_tee_create};
static struct class_factory vfw_capture_cf = {{&class_factory_vtbl}, vfw_capture_create};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        qcap_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    struct class_factory *factory;

    TRACE("clsid %s, iid %s, out %p.\n", debugstr_guid(clsid), debugstr_guid(iid), out);

    if (IsEqualGUID(clsid, &CLSID_AudioRecord))
        factory = &audio_record_cf;
    else if (IsEqualGUID(clsid, &CLSID_AVICo))
        factory = &avi_compressor_cf;
    else if (IsEqualGUID(clsid, &CLSID_AviDest))
        factory = &avi_mux_cf;
    else if (IsEqualGUID(clsid, &CLSID_CaptureGraphBuilder))
        factory = &capture_graph_cf;
    else if (IsEqualGUID(clsid, &CLSID_CaptureGraphBuilder2))
        factory = &capture_graph_cf;
    else if (IsEqualGUID(clsid, &CLSID_FileWriter))
        factory = &file_writer_cf;
    else if (IsEqualGUID(clsid, &CLSID_SmartTee))
        factory = &smart_tee_cf;
    else if (IsEqualGUID(clsid, &CLSID_VfwCapture))
        factory = &vfw_capture_cf;
    else
    {
        FIXME("%s not implemented, returning CLASS_E_CLASSNOTAVAILABLE.\n", debugstr_guid(clsid));
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    return IClassFactory_QueryInterface(&factory->IClassFactory_iface, iid, out);
}

static const REGPINTYPES reg_avi_mux_sink_mt = {&MEDIATYPE_Stream, &MEDIASUBTYPE_Avi};

static const REGFILTERPINS2 reg_avi_mux_pins[1] =
{
    {
        .cInstances = 1,
        .nMediaTypes = 1,
        .lpMediaType = &reg_avi_mux_sink_mt,
    },
};

static const REGFILTER2 reg_avi_mux =
{
    .dwVersion = 2,
    .dwMerit = MERIT_DO_NOT_USE,
    .u.s2.cPins2 = 1,
    .u.s2.rgPins2 = reg_avi_mux_pins,
};

static const REGPINTYPES reg_video_mt = {&MEDIATYPE_Video, &GUID_NULL};

static const REGFILTERPINS2 reg_smart_tee_pins[3] =
{
    {
        .cInstances = 1,
        .nMediaTypes = 1,
        .lpMediaType = &reg_video_mt,
    },
    {
        .dwFlags = REG_PINFLAG_B_OUTPUT,
        .cInstances = 1,
        .nMediaTypes = 1,
        .lpMediaType = &reg_video_mt,
    },
    {
        .dwFlags = REG_PINFLAG_B_OUTPUT,
        .cInstances = 1,
        .nMediaTypes = 1,
        .lpMediaType = &reg_video_mt,
    },
};

static const REGFILTER2 reg_smart_tee =
{
    .dwVersion = 2,
    .dwMerit = MERIT_DO_NOT_USE,
    .u.s2.cPins2 = 3,
    .u.s2.rgPins2 = reg_smart_tee_pins,
};

static const REGPINTYPES reg_file_writer_sink_mt = {&GUID_NULL, &GUID_NULL};

static const REGFILTERPINS2 reg_file_writer_pins[1] =
{
    {
        .cInstances = 1,
        .nMediaTypes = 1,
        .lpMediaType = &reg_file_writer_sink_mt,
    },
};

static const REGFILTER2 reg_file_writer =
{
    .dwVersion = 2,
    .dwMerit = MERIT_DO_NOT_USE,
    .u.s2.cPins2 = 1,
    .u.s2.rgPins2 = reg_file_writer_pins,
};

/***********************************************************************
 *    DllRegisterServer (QCAP.@)
 */
HRESULT WINAPI DllRegisterServer(void)
{
    static const WCHAR avi_muxW[] = {'A','V','I',' ','M','u','x',0};
    static const WCHAR file_writerW[] = {'F','i','l','e',' ','w','r','i','t','e','r',0};
    static const WCHAR smart_teeW[] = {'S','m','a','r','t',' ','T','e','e',0};
    IFilterMapper2 *mapper;
    HRESULT hr;

    if (FAILED(hr = __wine_register_resources( qcap_instance )))
        return hr;

    if (FAILED(hr = CoCreateInstance(&CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
            &IID_IFilterMapper2, (void **)&mapper)))
        return hr;

    IFilterMapper2_RegisterFilter(mapper, &CLSID_AviDest, avi_muxW,
            NULL, NULL, NULL, &reg_avi_mux);
    IFilterMapper2_RegisterFilter(mapper, &CLSID_FileWriter, file_writerW,
            NULL, NULL, NULL, &reg_file_writer);
    IFilterMapper2_RegisterFilter(mapper, &CLSID_SmartTee, smart_teeW,
            NULL, NULL, NULL, &reg_smart_tee);

    IFilterMapper2_Release(mapper);
    return S_OK;
}

/***********************************************************************
 *    DllUnregisterServer (QCAP.@)
 */
HRESULT WINAPI DllUnregisterServer(void)
{
    IFilterMapper2 *mapper;
    HRESULT hr;

    if (FAILED(hr = __wine_unregister_resources( qcap_instance )))
        return hr;

    if (FAILED(hr = CoCreateInstance(&CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
            &IID_IFilterMapper2, (void **)&mapper)))
        return hr;

    IFilterMapper2_UnregisterFilter(mapper, NULL, NULL, &CLSID_AviDest);
    IFilterMapper2_UnregisterFilter(mapper, NULL, NULL, &CLSID_FileWriter);
    IFilterMapper2_UnregisterFilter(mapper, NULL, NULL, &CLSID_SmartTee);

    IFilterMapper2_Release(mapper);
    return S_OK;
}

/***********************************************************************
 *    DllCanUnloadNow (QCAP.@)
 */
HRESULT WINAPI DllCanUnloadNow(void)
{
    TRACE(".\n");

    return objects_ref ? S_FALSE : S_OK;
}

DWORD ObjectRefCount(BOOL increment)
{
    if (increment)
        return InterlockedIncrement(&objects_ref);
    return InterlockedDecrement(&objects_ref);
}
