// Объект данных, через который MMC идентифицирует узлы и элементы оснастки
#pragma once

#include "nodes.h"
#include "snapinapp.h"

#include <memory>

class CDataObject : public IDataObject, public ComRefCounted {
public:
    CDataObject(DATA_OBJECT_TYPES type, Node *node, std::shared_ptr<ResultRow> row);

    IMPLEMENT_REFCOUNT()
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override;

    // IDataObject
    STDMETHOD(GetData)(FORMATETC *fmt, STGMEDIUM *medium) override;
    STDMETHOD(GetDataHere)(FORMATETC *fmt, STGMEDIUM *medium) override;
    STDMETHOD(QueryGetData)(FORMATETC *fmt) override;
    STDMETHOD(GetCanonicalFormatEtc)(FORMATETC *, FORMATETC *out) override;
    STDMETHOD(SetData)(FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
    STDMETHOD(EnumFormatEtc)(DWORD, IEnumFORMATETC **) override { return E_NOTIMPL; }
    STDMETHOD(DAdvise)(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHOD(DUnadvise)(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHOD(EnumDAdvise)(IEnumSTATDATA **) override { return OLE_E_ADVISENOTSUPPORTED; }

    DATA_OBJECT_TYPES type() const { return m_type; }
    Node *node() const { return m_node; }
    const std::shared_ptr<ResultRow> &row() const { return m_row; }

    static HRESULT create(DATA_OBJECT_TYPES type, Node *node, std::shared_ptr<ResultRow> row, IDataObject **out);

private:
    HRESULT writeToStream(STGMEDIUM *medium, const void *data, ULONG size);

    DATA_OBJECT_TYPES m_type;
    Node *m_node;
    std::shared_ptr<ResultRow> m_row;
};

// Собственный объект данных оснастки или nullptr для чужих и специальных объектов
CDataObject *extractDataObject(IDataObject *obj);
