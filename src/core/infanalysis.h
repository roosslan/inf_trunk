#pragma once

// Результат полного разбора текста: синтаксис, семантика и диагностика.

#include "infdocument.h"
#include "infmodel.h"
#include "infvalidator.h"

#include <memory>

namespace inf {

struct Analysis {
    InfDocument doc;
    InfModel model;
    std::vector<Diagnostic> diagnostics;

    static std::shared_ptr<const Analysis> run(const wstring &text)
    {
        auto a = std::make_shared<Analysis>();
        a->doc = InfDocument::parse(text);
        a->model = InfModel::analyze(a->doc);
        a->diagnostics = validate(a->doc, a->model);
        return a;
    }
};

} // namespace inf
