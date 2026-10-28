// Сведения об оснастке для окна «Добавить или удалить оснастки»
#pragma once

#include "snapinapp.h"

class CSnapinAbout : public ISnapinAbout, public ComRefCounted {
public:
    ~CSnapinAbout() override;

    IMPLEMENT_REFCOUNT()
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override;

    STDMETHOD(GetSnapinDescription)(LPOLESTR *description) override;
    STDMETHOD(GetProvider)(LPOLESTR *name) override;
    STDMETHOD(GetSnapinVersion)(LPOLESTR *version) override;
    STDMETHOD(GetSnapinImage)(HICON *icon) override;
    STDMETHOD(GetStaticFolderImage)(HBITMAP *smallImage, HBITMAP *smallOpenImage, HBITMAP *largeImage, COLORREF *mask) override;

private:
    HBITMAP m_small = nullptr;
    HBITMAP m_large = nullptr;
};
