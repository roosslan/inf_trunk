#include "stdafx.h"

#include "dataobject.h"

#include "guids.h"

namespace {

CLIPFORMAT format(const wchar_t *name)
{
    return CLIPFORMAT(RegisterClipboardFormatW(name));
}

CLIPFORMAT cfNodeType() { static CLIPFORMAT f = format(CCF_NODETYPE); return f; }
CLIPFORMAT cfSzNodeType() { static CLIPFORMAT f = format(CCF_SZNODETYPE); return f; }
CLIPFORMAT cfDisplayName() { static CLIPFORMAT f = format(CCF_DISPLAY_NAME); return f; }
CLIPFORMAT cfSnapinClsid() { static CLIPFORMAT f = format(CCF_SNAPIN_CLASSID); return f; }

} // namespace

CDataObject::CDataObject(DATA_OBJECT_TYPES type, Node *node, std::shared_ptr<ResultRow> row)
    : m_type(type), m_node(node), m_row(std::move(row))
{
}

HRESULT CDataObject::create(DATA_OBJECT_TYPES type, Node *node, std::shared_ptr<ResultRow> row, IDataObject **out)
{
    if (!out)
        return E_POINTER;
    auto *obj = new (std::nothrow) CDataObject(type, node, std::move(row));
    if (!obj)
        return E_OUTOFMEMORY;
    *out = obj;
    return S_OK;
}

STDMETHODIMP CDataObject::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
        *ppv = static_cast<IDataObject *>(this);
    } else if (riid == IID_IInfDataObject) {
        // внутренний запрос: указатель на сам объект
        *ppv = this;
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

HRESULT CDataObject::writeToStream(STGMEDIUM *medium, const void *data, ULONG size)
{
    if (!medium || medium->tymed != TYMED_HGLOBAL || !medium->hGlobal)
        return DV_E_TYMED;
    IStream *stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(medium->hGlobal, FALSE, &stream);
    if (FAILED(hr))
        return hr;
    ULONG written = 0;
    hr = stream->Write(data, size, &written);
    stream->Release();
    return hr;
}

STDMETHODIMP CDataObject::GetDataHere(FORMATETC *fmt, STGMEDIUM *medium)
{
    trace(L"DO::GetDataHere cf=%u", fmt ? unsigned(fmt->cfFormat) : 0u);
    MANAGE_STATE();
    if (!fmt || !medium)
        return E_POINTER;
    const GUID &nodeType = m_row ? NODETYPE_Item : (m_node ? m_node->nodeType() : NODETYPE_Root);
    if (fmt->cfFormat == cfNodeType())
        return writeToStream(medium, &nodeType, sizeof(GUID));
    if (fmt->cfFormat == cfSzNodeType()) {
        wchar_t buf[64];
        const int n = StringFromGUID2(nodeType, buf, 64);
        return writeToStream(medium, buf, ULONG(n * sizeof(wchar_t)));
    }
    if (fmt->cfFormat == cfDisplayName()) {
        const std::wstring name = m_node ? m_node->displayName() : std::wstring(kSnapinName);
        return writeToStream(medium, name.c_str(), ULONG((name.size() + 1) * sizeof(wchar_t)));
    }
    if (fmt->cfFormat == cfSnapinClsid())
        return writeToStream(medium, &CLSID_InfSnapin, sizeof(CLSID));
    return DV_E_FORMATETC;
}

STDMETHODIMP CDataObject::GetData(FORMATETC *fmt, STGMEDIUM *)
{
    trace(L"DO::GetData cf=%u", fmt ? unsigned(fmt->cfFormat) : 0u);
    return E_NOTIMPL;
}

STDMETHODIMP CDataObject::QueryGetData(FORMATETC *fmt)
{
    if (!fmt)
        return E_POINTER;
    const CLIPFORMAT f = fmt->cfFormat;
    return f == cfNodeType() || f == cfSzNodeType() || f == cfDisplayName() || f == cfSnapinClsid() ? S_OK : DV_E_FORMATETC;
}

STDMETHODIMP CDataObject::GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out)
{
    if (out)
        out->ptd = nullptr;
    return E_NOTIMPL;
}

CDataObject *extractDataObject(IDataObject *obj)
{
    // специальные объекты MMC (DOBJ_CUSTOMOCX и т. п.) — небольшие числа, а не указатели
    if (!obj || IS_SPECIAL_DATAOBJECT(obj))
        return nullptr;
    CDataObject *own = nullptr;
    if (FAILED(obj->QueryInterface(IID_IInfDataObject, reinterpret_cast<void **>(&own))) || !own)
        return nullptr;
    own->Release();
    return own;
}
