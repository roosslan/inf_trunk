#include "stdafx.h"

#include "about.h"

#include "guids.h"
#include "images.h"

namespace {

const COLORREF kMask = RGB(255, 0, 255);

HRESULT copyString(const wchar_t *s, LPOLESTR *out)
{
    if (!out)
        return E_POINTER;
    const size_t bytes = (wcslen(s) + 1) * sizeof(wchar_t);
    *out = static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));
    if (!*out)
        return E_OUTOFMEMORY;
    memcpy(*out, s, bytes);
    return S_OK;
}

} // namespace

CSnapinAbout::~CSnapinAbout()
{
    if (m_small)
        DeleteObject(m_small);
    if (m_large)
        DeleteObject(m_large);
}

STDMETHODIMP CSnapinAbout::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ISnapinAbout) {
        *ppv = static_cast<ISnapinAbout *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP CSnapinAbout::GetSnapinDescription(LPOLESTR *description)
{
    trace(L"About::GetSnapinDescription");
    return copyString(L"Редактор и Wizard INF-файлов драйверов Windows, включает проверку InfVerif из WDK", description);
}

STDMETHODIMP CSnapinAbout::GetProvider(LPOLESTR *name)
{
    return copyString(kSnapinProvider, name);
}

STDMETHODIMP CSnapinAbout::GetSnapinVersion(LPOLESTR *version)
{
    return copyString(APP_VERSION, version);
}

STDMETHODIMP CSnapinAbout::GetSnapinImage(HICON *icon)
{
    trace(L"About::GetSnapinImage");
    MANAGE_STATE();
    if (!icon)
        return E_POINTER;
    *icon = appIcon(true);
    return S_OK;
}

STDMETHODIMP CSnapinAbout::GetStaticFolderImage(HBITMAP *smallImage, HBITMAP *smallOpenImage, HBITMAP *largeImage, COLORREF *mask)
{
    trace(L"About::GetStaticFolderImage");
    MANAGE_STATE();
    if (!smallImage || !smallOpenImage || !largeImage || !mask)
        return E_POINTER;
    if (!m_small)
        m_small = iconBitmap(ImgApp, 16, kMask);
    if (!m_large)
        m_large = iconBitmap(ImgApp, 32, kMask);
    *smallImage = m_small;
    *smallOpenImage = m_small;
    *largeImage = m_large;
    *mask = kMask;
    return S_OK;
}
