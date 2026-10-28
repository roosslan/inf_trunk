#include "stdafx.h"

#include "texteditor.h"

#include "dialogs.h"
#include "infanalysis.h"
#include "infknowledge.h"
#include "inflexer.h"
#include "infmodel.h"
#include "resource.h"

#include <algorithm>
#include <cstdlib>
#include <format>

using std::wstring;

namespace {

constexpr UINT_PTR kHighlightTimer = 1;
constexpr int kMaxHighlightChars = 600000;

// Цвета подсветки (как в Qt-версии редактора)
const COLORREF kSection = RGB(0x0b, 0x3d, 0x91);
const COLORREF kKey = RGB(0x7a, 0x3e, 0x00);
const COLORREF kDirective = RGB(0x8b, 0x1a, 0x1a);
const COLORREF kText = RGB(0, 0, 0);
const COLORREF kString = RGB(0x1a, 0x7f, 0x37);
const COLORREF kToken = RGB(0x82, 0x50, 0xdf);
const COLORREF kNumber = RGB(0x0a, 0x6b, 0x78);
const COLORREF kGuid = RGB(0x5a, 0x5a, 0x00);
const COLORREF kComment = RGB(0x6e, 0x77, 0x81);
const COLORREF kOperator = RGB(0x57, 0x60, 0x6a);
const COLORREF kError = RGB(0xc0, 0x00, 0x00);

bool isNumber(const wstring &v)
{
    uint32_t n = 0;
    return inf::parseNumber(v, n) || (v.size() > 1 && v[0] == L'-' && inf::parseUInt(v.substr(1), n));
}

class CTextEditorDlg : public CDialog {
public:
    CTextEditorDlg(CWnd *parent, const wstring &title, const wstring &text, int line)
        : CDialog(IDD_TEXT_EDITOR, parent), m_title(title), m_text(text), m_line(line)
    {
    }

    wstring result;

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    void OnCancel() override;
    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
    BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *res) override;

    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO *mmi);
    afx_msg void OnTimer(UINT_PTR id);
    afx_msg void OnDestroy();
    DECLARE_MESSAGE_MAP()

private:
    wstring editorText();
    void highlightLines(int first, int last);
    void highlightAll();
    void check();
    void findNext();
    void gotoLine(int line);
    void updateStatus();
    void layout(int cx, int cy);

    void setColor(int start, int length, COLORREF color, bool bold = false, bool italic = false);

    wstring m_title;
    wstring m_text;
    int m_line;
    CRichEditCtrl m_edit;
    CListBox m_problems;
    std::vector<int> m_problemLines;
    ITextDocument *m_tom = nullptr;
    int m_lineCount = 0;
    bool m_coloring = false;
    CSize m_minSize;
};

BEGIN_MESSAGE_MAP(CTextEditorDlg, CDialog)
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_TIMER()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CTextEditorDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetWindowText((L"Текст INF — " + m_title).c_str());
    SetIcon(static_cast<HICON>(LoadImageW(AfxGetResourceHandle(), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, LR_SHARED)), FALSE);

    CRect r;
    GetWindowRect(&r);
    m_minSize = r.Size();

    m_edit.SubclassDlgItem(IDC_RICHEDIT, this);
    m_problems.SubclassDlgItem(IDC_PROBLEMS, this);
    m_edit.SetTextMode(TM_PLAINTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE);
    m_edit.LimitText(64 * 1024 * 1024);
    m_edit.SetOptions(ECOOP_OR, ECO_SELECTIONBAR);

    // текст в шрифте диалога
    LOGFONTW lf{};
    GetFont()->GetLogFont(&lf);
    CClientDC screen(this);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET;
    cf.yHeight = MulDiv(std::abs(lf.lfHeight), 1440, screen.GetDeviceCaps(LOGPIXELSY));
    cf.bCharSet = DEFAULT_CHARSET;
    cf.crTextColor = kText;
    wcscpy_s(cf.szFaceName, lf.lfFaceName);
    m_edit.SendMessage(EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    // табуляция через каждые полдюйма
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_TABSTOPS;
    pf.cTabCount = MAX_TAB_STOPS;
    for (int i = 0; i < MAX_TAB_STOPS; ++i)
        pf.rgxTabs[i] = (i + 1) * 720;
    m_edit.SetParaFormat(pf);

    IRichEditOle *ole = m_edit.GetIRichEditOle();
    if (ole) {
        ole->QueryInterface(__uuidof(ITextDocument), reinterpret_cast<void **>(&m_tom));
        ole->Release();
    }

    m_edit.SetWindowText(inf::replaceAll(m_text, L"\n", L"\r\n").c_str());
    m_edit.EmptyUndoBuffer();
    m_edit.SetEventMask(ENM_CHANGE | ENM_SELCHANGE);
    m_lineCount = m_edit.GetLineCount();
    highlightAll();
    check();
    gotoLine(m_line < 0 ? 0 : m_line);
    m_edit.SetFocus();
    return FALSE;
}

void CTextEditorDlg::OnDestroy()
{
    KillTimer(kHighlightTimer);
    if (m_tom) {
        m_tom->Release();
        m_tom = nullptr;
    }
    CDialog::OnDestroy();
}

wstring CTextEditorDlg::editorText()
{
    GETTEXTLENGTHEX len{GTL_DEFAULT | GTL_NUMCHARS, 1200};
    const int n = int(m_edit.SendMessage(EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&len), 0));
    wstring buf(size_t(n) + 1, L'\0');
    GETTEXTEX gt{};
    gt.cb = DWORD(buf.size() * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    m_edit.SendMessage(EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(buf.data()));
    buf.resize(wcslen(buf.c_str()));
    // RichEdit хранит переводы строк как '\r'
    for (wchar_t &c : buf)
        if (c == L'\r')
            c = L'\n';
    return buf;
}

void CTextEditorDlg::setColor(int start, int length, COLORREF color, bool bold, bool italic)
{
    if (length <= 0)
        return;
    m_edit.SetSel(start, start + length);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    cf.crTextColor = color;
    cf.dwEffects = (bold ? CFE_BOLD : 0) | (italic ? CFE_ITALIC : 0);
    m_edit.SendMessage(EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

void CTextEditorDlg::highlightLines(int first, int last)
{
    if (m_edit.GetTextLength() > kMaxHighlightChars)
        return;
    m_coloring = true;
    CHARRANGE sel;
    m_edit.GetSel(sel);
    const POINT scroll = [&] {
        POINT p{};
        m_edit.SendMessage(EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&p));
        return p;
    }();
    const DWORD mask = m_edit.GetEventMask();
    m_edit.SetEventMask(0);
    m_edit.SetRedraw(FALSE);
    // изменения цвета не должны попадать в историю отмены
    if (m_tom)
        m_tom->Undo(tomSuspend, nullptr);

    const int count = m_edit.GetLineCount();
    first = std::max(0, first);
    last = std::min(last, count - 1);

    // строка-продолжение: предыдущая строка закончилась символом '\'
    bool continued = false;
    if (first > 0) {
        const int prevStart = m_edit.LineIndex(first - 1);
        const int prevLen = m_edit.LineLength(prevStart);
        CString prev;
        if (prevLen > 0) {
            m_edit.GetLine(first - 1, prev.GetBuffer(prevLen + 1), prevLen + 1);
            prev.ReleaseBuffer(prevLen);
        }
        continued = inf::lexLine(wstring(prev.GetString()), false).continues;
    }

    for (int ln = first; ln <= last; ++ln) {
        const int start = m_edit.LineIndex(ln);
        const int len = m_edit.LineLength(start);
        wstring line;
        if (len > 0) {
            line.resize(size_t(len) + 1);
            *reinterpret_cast<WORD *>(line.data()) = WORD(len + 1);
            const int got = int(m_edit.SendMessage(EM_GETLINE, WPARAM(ln), reinterpret_cast<LPARAM>(line.data())));
            line.resize(size_t(std::min(got, len)));
        }
        setColor(start, len, kText);
        const inf::LexResult lex = inf::lexLine(line, continued);
        for (const inf::Token &t : lex.tokens) {
            const wstring part = line.substr(size_t(t.start), size_t(t.length));
            auto tokens = [&](COLORREF base) {
                setColor(start + t.start, t.length, base);
                for (const inf::StringToken &s : inf::InfModel::stringTokens(part))
                    setColor(start + t.start + s.start, s.length, kToken, true);
            };
            switch (t.kind) {
            case inf::TokenKind::SectionHeader: setColor(start + t.start, t.length, kSection, true); break;
            case inf::TokenKind::Key: tokens(inf::findDirective(part) ? kDirective : kKey); break;
            case inf::TokenKind::Equals:
            case inf::TokenKind::Comma:
            case inf::TokenKind::Continuation: setColor(start + t.start, t.length, kOperator); break;
            case inf::TokenKind::QuotedString: tokens(kString); break;
            case inf::TokenKind::Value: {
                const wstring v = inf::trim(part);
                if (isNumber(v))
                    setColor(start + t.start, t.length, kNumber);
                else if (inf::isGuid(v))
                    setColor(start + t.start, t.length, kGuid);
                else
                    tokens(kText);
                break;
            }
            case inf::TokenKind::Comment: setColor(start + t.start, t.length, kComment, false, true); break;
            case inf::TokenKind::Error: setColor(start + t.start, t.length, kError); break;
            }
        }
        continued = lex.continues;
    }

    if (m_tom)
        m_tom->Undo(tomResume, nullptr);
    m_edit.SetSel(sel);
    m_edit.SendMessage(EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    m_edit.SetRedraw(TRUE);
    m_edit.Invalidate();
    m_edit.SetEventMask(mask);
    m_coloring = false;
}

void CTextEditorDlg::highlightAll()
{
    highlightLines(0, m_edit.GetLineCount() - 1);
}

void CTextEditorDlg::check()
{
    const auto a = inf::Analysis::run(editorText());
    m_problems.ResetContent();
    m_problemLines.clear();
    int errors = 0, warnings = 0;
    for (const inf::Diagnostic &d : a->diagnostics) {
        // для фрагмента секции проверки целого файла (нет [Version] и т. п.) не показываются
        if (d.code == L"V001" || d.code == L"R002")
            continue;
        if (d.severity == inf::Severity::Error)
            ++errors;
        else if (d.severity == inf::Severity::Warning)
            ++warnings;
        m_problems.AddString(std::format(L"{}: строка {}: {} ({})", inf::severityName(d.severity), d.line + 1, d.message, d.code).c_str());
        m_problemLines.push_back(d.line);
    }
    setDlgText(this, IDC_STATUS, std::format(L"Ошибок: {}, предупреждений: {}", errors, warnings));
}

void CTextEditorDlg::findNext()
{
    const wstring what = dlgText(this, IDC_FIND);
    if (what.empty())
        return;
    CHARRANGE sel;
    m_edit.GetSel(sel);
    FINDTEXTEXW ft{};
    ft.chrg.cpMin = sel.cpMax;
    ft.chrg.cpMax = -1;
    ft.lpstrText = what.c_str();
    LONG pos = LONG(m_edit.SendMessage(EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&ft)));
    if (pos < 0) {
        ft.chrg.cpMin = 0;
        pos = LONG(m_edit.SendMessage(EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&ft)));
    }
    if (pos < 0) {
        MessageBeep(MB_ICONASTERISK);
        return;
    }
    m_edit.SetSel(ft.chrgText);
    m_edit.SetFocus();
}

void CTextEditorDlg::gotoLine(int line)
{
    const int count = m_edit.GetLineCount();
    line = std::clamp(line, 0, std::max(0, count - 1));
    const int pos = m_edit.LineIndex(line);
    m_edit.SetSel(pos, pos);
    // строка выводится примерно в верхнюю треть окна
    const int first = m_edit.GetFirstVisibleLine();
    m_edit.LineScroll(line - first - 5);
    m_edit.SetFocus();
    updateStatus();
}

void CTextEditorDlg::updateStatus()
{
    CHARRANGE sel;
    m_edit.GetSel(sel);
    const int line = m_edit.LineFromChar(sel.cpMax);
    const int col = sel.cpMax - m_edit.LineIndex(line);
    CString status;
    GetDlgItemText(IDC_STATUS, status);
    const int sep = status.Find(L"  |  ");
    if (sep >= 0)
        status = status.Left(sep);
    status += std::format(L"  |  Стр {}, стлб {}", line + 1, col + 1).c_str();
    SetDlgItemText(IDC_STATUS, status);
}

BOOL CTextEditorDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
    const int id = LOWORD(wParam);
    const int code = HIWORD(wParam);
    if (id == IDC_RICHEDIT && code == EN_CHANGE && !m_coloring) {
        // текущая строка подсвечивается сразу, весь текст — после паузы при многострочных изменениях
        const int count = m_edit.GetLineCount();
        CHARRANGE sel;
        m_edit.GetSel(sel);
        const int line = m_edit.LineFromChar(sel.cpMax);
        if (count != m_lineCount) {
            m_lineCount = count;
            highlightLines(line - 1, line + 1);
            SetTimer(kHighlightTimer, 500, nullptr);
        } else {
            highlightLines(line, line + 1);
        }
        return TRUE;
    }
    if (id == IDC_FIND_NEXT && code == BN_CLICKED) {
        findNext();
        return TRUE;
    }
    if (id == IDC_CHECK && code == BN_CLICKED) {
        check();
        return TRUE;
    }
    if (id == IDC_PROBLEMS && code == LBN_DBLCLK) {
        const int i = m_problems.GetCurSel();
        if (i >= 0 && i < int(m_problemLines.size()))
            gotoLine(m_problemLines[size_t(i)]);
        return TRUE;
    }
    return CDialog::OnCommand(wParam, lParam);
}

BOOL CTextEditorDlg::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *res)
{
    const auto *hdr = reinterpret_cast<NMHDR *>(lParam);
    if (hdr->idFrom == IDC_RICHEDIT && hdr->code == EN_SELCHANGE && !m_coloring) {
        updateStatus();
        return TRUE;
    }
    return CDialog::OnNotify(wParam, lParam, res);
}

void CTextEditorDlg::OnTimer(UINT_PTR id)
{
    if (id == kHighlightTimer) {
        KillTimer(kHighlightTimer);
        highlightAll();
        return;
    }
    CDialog::OnTimer(id);
}

void CTextEditorDlg::OnOK()
{
    // Enter в поле поиска ищет, а не закрывает окно
    if (GetFocus() == GetDlgItem(IDC_FIND)) {
        findNext();
        return;
    }
    result = editorText();
    // завершающий перевод строки сохраняется таким же, как в исходном тексте
    const bool hadNewline = !m_text.empty() && m_text.back() == L'\n';
    while (!result.empty() && result.back() == L'\n' && !hadNewline)
        result.pop_back();
    if (hadNewline && (result.empty() || result.back() != L'\n'))
        result += L'\n';
    CDialog::OnOK();
}

void CTextEditorDlg::OnCancel()
{
    if (m_edit.CanUndo() && editorText() != m_text
        && MessageBoxW(L"Отменить сделанные изменения текста?", m_title.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    CDialog::OnCancel();
}

void CTextEditorDlg::layout(int cx, int cy)
{
    if (!m_edit.GetSafeHwnd())
        return;
    CRect unit(0, 0, 4, 8);
    MapDialogRect(&unit);
    const int m = unit.Width() * 7 / 4;          // 7 DLU
    const int bh = unit.Height() * 15 / 8;       // 15 DLU
    const int listH = unit.Height() * 44 / 8;
    const int rowH = bh;

    const int okW = unit.Width() * 50 / 4;
    GetDlgItem(IDCANCEL)->SetWindowPos(nullptr, cx - m - okW, cy - m - bh, okW, bh, SWP_NOZORDER);
    GetDlgItem(IDOK)->SetWindowPos(nullptr, cx - 2 * m - 2 * okW + m / 2, cy - m - bh, okW, bh, SWP_NOZORDER);
    const int listTop = cy - m - bh - m / 2 - listH;
    m_problems.SetWindowPos(nullptr, m, listTop, cx - 2 * m, listH, SWP_NOZORDER);
    const int rowTop = listTop - m / 2 - rowH;
    auto place = [&](int id, int x, int w) {
        if (CWnd *w2 = GetDlgItem(id))
            w2->SetWindowPos(nullptr, x, rowTop + (id == IDC_STATUS || id == -1 ? 3 : 0), w, rowH, SWP_NOZORDER);
    };
    const int labelW = unit.Width() * 26 / 4;
    const int findW = unit.Width() * 120 / 4;
    const int btnW = unit.Width() * 55 / 4;
    int x = m;
    if (CWnd *label = GetDlgItem(IDC_FIND)->GetWindow(GW_HWNDPREV))
        label->SetWindowPos(nullptr, x, rowTop + 3, labelW, rowH, SWP_NOZORDER);
    x += labelW + 2;
    place(IDC_FIND, x, findW);
    x += findW + m / 2;
    place(IDC_FIND_NEXT, x, btnW);
    x += btnW + m / 2;
    place(IDC_CHECK, x, btnW);
    x += btnW + m;
    place(IDC_STATUS, x, std::max(10, cx - x - m));
    m_edit.SetWindowPos(nullptr, m, m, cx - 2 * m, rowTop - m - m / 2, SWP_NOZORDER);
}

void CTextEditorDlg::OnSize(UINT type, int cx, int cy)
{
    CDialog::OnSize(type, cx, cy);
    if (type != SIZE_MINIMIZED)
        layout(cx, cy);
}

void CTextEditorDlg::OnGetMinMaxInfo(MINMAXINFO *mmi)
{
    if (m_minSize.cx > 0) {
        mmi->ptMinTrackSize.x = m_minSize.cx * 2 / 3;
        mmi->ptMinTrackSize.y = m_minSize.cy * 2 / 3;
    }
    CDialog::OnGetMinMaxInfo(mmi);
}

} // namespace

bool runTextEditor(CWnd *parent, const wstring &title, wstring &text, int line)
{
    CTextEditorDlg dlg(parent, title, text, line);
    if (dlg.DoModal() != IDOK || dlg.result == text)
        return false;
    text = dlg.result;
    return true;
}
