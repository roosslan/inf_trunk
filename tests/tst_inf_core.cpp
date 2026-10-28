// Тесты ядра: разбор, кодировки, анализ, проверка, правка и генерация INF.
// Собственный минимальный каркас: каждый тест — функция, проверки через CHECK.

#include "cabwriter.h"
#include "drvstore.h"
#include "infanalysis.h"
#include "infedit.h"
#include "inffile.h"
#include "infgenerator.h"
#include "infhelp.h"
#include "infknowledge.h"
#include "inflexer.h"
#include "infverif.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace inf;

namespace {

int g_failed = 0;
int g_checks = 0;
std::string g_current;

void fail(const char *file, int line, const std::string &what)
{
    ++g_failed;
    std::printf("FAIL  %s\n      %s(%d): %s\n", g_current.c_str(), file, line, what.c_str());
}

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(cond))                                                                                                   \
            fail(__FILE__, __LINE__, #cond);                                                                           \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                                           \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(cond))                                                                                                   \
            fail(__FILE__, __LINE__, std::string(#cond) + "\n" + toUtf8(msg));                                         \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!((a) == (b)))                                                                                             \
            fail(__FILE__, __LINE__, std::string(#a " == " #b));                                                       \
    } while (0)

struct TestCase {
    const char *name;
    std::function<void()> fn;
};

std::vector<TestCase> &tests()
{
    static std::vector<TestCase> t;
    return t;
}

struct Register {
    Register(const char *name, std::function<void()> fn) { tests().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                                                                                     \
    void name();                                                                                                       \
    Register reg_##name(#name, name);                                                                                  \
    void name()

const wchar_t *kSample = LR"([Version]
Signature   = "$WINDOWS NT$"   ; подпись
Class       = Sample
ClassGuid   = {78A1C341-4539-11d3-B88D-00C04FAD5171}
Provider    = %ProviderName%
CatalogFile = sample.cat
DriverVer   = 01/15/2026,1.2.3.4
PnpLockdown = 1

[DestinationDirs]
DefaultDestDir = 13

[SourceDisksNames]
1 = %DiskName%,,,""

[SourceDisksFiles]
sample.sys = 1,,

[Manufacturer]
%MfgName% = Standard,NTamd64.10.0...16299

[Standard.NTamd64.10.0...16299]
%Sample.DeviceDesc% = Sample_Device, Root\Sample, *PNP0C00

[Sample_Device.NT]
CopyFiles = Sample_Files, \
            Sample_More

[Sample_Files]
sample.sys

[Sample_More]
sample.sys

[Sample_Device.NT.Services]
AddService = Sample,0x00000002,Sample_Service_Inst

[Sample_Service_Inst]
DisplayName   = %Sample.SvcDesc%
ServiceType   = 1
StartType     = 3
ErrorControl  = 1
ServiceBinary = %13%\sample.sys

[Strings]
ProviderName      = "Provider"
MfgName           = "Manufacturer"
DiskName          = "Disk ""1"""
Sample.DeviceDesc = "Sample Device"
Sample.SvcDesc    = "Sample Service"
)";

int countSeverity(const std::vector<Diagnostic> &list, Severity s)
{
    int n = 0;
    for (const Diagnostic &d : list)
        if (d.severity == s)
            ++n;
    return n;
}

wstring dump(const std::vector<Diagnostic> &list)
{
    StringList out;
    for (const Diagnostic &d : list)
        out.push_back(severityName(d.severity) + L" " + d.code + L" line " + std::to_wstring(d.line + 1) + L": " + d.message);
    return join(out, L"\n");
}

bool hasCode(const std::vector<Diagnostic> &list, const wstring &code)
{
    for (const Diagnostic &d : list)
        if (d.code == code)
            return true;
    return false;
}

wstring sub(const wstring &s, const Token &t)
{
    return s.substr(size_t(t.start), size_t(t.length));
}

// ---------------------------------------------------------------- лексер

TEST(lexEntry)
{
    const wstring line = L"  Key = \"a,b\" , plain value ; comment";
    const LexResult r = lexLine(line, false);
    CHECK(r.error.empty());
    CHECK_EQ(r.tokens.size(), size_t(6));
    if (r.tokens.size() != 6)
        return;
    CHECK(r.tokens[0].kind == TokenKind::Key);
    CHECK(sub(line, r.tokens[0]) == L"Key");
    CHECK(r.tokens[1].kind == TokenKind::Equals);
    CHECK(r.tokens[2].kind == TokenKind::QuotedString);
    CHECK(r.tokens[3].kind == TokenKind::Comma);
    CHECK(sub(line, r.tokens[4]) == L"plain value");
    CHECK(r.tokens[5].kind == TokenKind::Comment);
}

TEST(lexNoKeyAfterComma)
{
    const LexResult r = lexLine(L"HKR,,Value,,a=b", false);
    for (const Token &t : r.tokens)
        CHECK(t.kind != TokenKind::Key && t.kind != TokenKind::Equals);
}

TEST(lexContinuation)
{
    const LexResult r = lexLine(L"CopyFiles = a, b, \\", false);
    CHECK(r.continues);
    CHECK(r.tokens.back().kind == TokenKind::Continuation);
    const LexResult c = lexLine(L"   c = d", true);
    CHECK(!c.continues);
    for (const Token &t : c.tokens)
        CHECK(t.kind != TokenKind::Key);
}

TEST(lexErrors)
{
    CHECK(!lexLine(L"[Version", false).error.empty());
    CHECK(!lexLine(L"[Version] junk", false).error.empty());
    CHECK(lexLine(L"key = \"open", false).unterminatedQuote);
    CHECK(lexLine(L"[Version] ; ok", false).error.empty());
}

TEST(lexContinuationBeforeComment)
{
    const LexResult r = lexLine(L"        0x05,   0x0c,       \\ ; Usage Page", true);
    CHECK(r.continues);
    CHECK(r.tokens.back().kind == TokenKind::Comment);
    const InfDocument doc = InfDocument::parse(L"[R]\nHKR,,Desc,0x00000001,\\\n    \\ ; header\n    0x05, 0x0c ; tail\n");
    const InfSection *s = doc.section(L"R");
    CHECK_EQ(s->entries.size(), size_t(1));
    CHECK_EQ(doc.lines()[size_t(s->entries[0])].values.size(), size_t(6));
}

TEST(quoting)
{
    CHECK(unquote(L"\"a\"\"b\"") == L"a\"b");
    CHECK(quote(L"a\"b") == L"\"a\"\"b\"");
    CHECK(quoteIfNeeded(L"plain") == L"plain");
    CHECK(quoteIfNeeded(L"a,b") == L"\"a,b\"");
}

// ---------------------------------------------------------------- строки и даты

TEST(stringUtils)
{
    CHECK(iequals(L"Версия", L"ВЕРСИЯ"));
    CHECK(lower(L"ABC Где") == L"abc где");
    uint32_t n = 0;
    CHECK(parseNumber(L"0x10", n) && n == 16);
    CHECK(parseNumber(L" 42 ", n) && n == 42);
    CHECK(!parseNumber(L"4x2", n));
    CHECK(Date::fromInf(L"1/5/2026").isValid());
    CHECK(!Date::fromInf(L"13/45/2026").isValid());
    CHECK(Date::fromInf(L"02/29/2024").isValid());
    CHECK(!Date::fromInf(L"02/29/2025").isValid());
    CHECK((Date{2025, 12, 31}.addDays(1).toInf() == L"01/01/2026"));
    CHECK(isGuid(newGuid()));
    CHECK(!isGuid(L"{1234}"));
}

// ---------------------------------------------------------------- парсер

TEST(parseSample)
{
    const InfDocument doc = InfDocument::parse(kSample);
    CHECK(doc.hasSection(L"version"));
    CHECK(doc.value(L"Version", L"signature") == L"$WINDOWS NT$");
    const InfLine *sig = doc.entry(L"Version", L"Signature");
    CHECK(sig && sig->comment == L"; подпись");

    const InfLine *cf = doc.entry(L"Sample_Device.NT", L"CopyFiles");
    CHECK(cf);
    if (!cf)
        return;
    CHECK_EQ(cf->values.size(), size_t(2));
    CHECK(cf->value(1) == L"Sample_More");
    CHECK_EQ(cf->lastLine - cf->firstLine, 1);
    CHECK_EQ(cf->values[1].line, cf->firstLine + 1);

    CHECK(doc.entry(L"Strings", L"DiskName")->value(0) == L"Disk \"1\"");

    const InfLine *sdn = doc.entry(L"SourceDisksNames", L"1");
    CHECK_EQ(sdn->values.size(), size_t(4));
    CHECK(sdn->value(1).empty());
    CHECK(sdn->values[3].quoted);
}

TEST(parseDuplicateSections)
{
    const InfDocument doc = InfDocument::parse(L"[A]\nx=1\n[B]\n[a]\ny=2\n");
    CHECK_EQ(doc.sections().size(), size_t(2));
    const InfSection *a = doc.section(L"A");
    CHECK_EQ(a->chunks.size(), size_t(2));
    CHECK_EQ(a->entries.size(), size_t(2));
    CHECK(doc.value(L"a", L"Y") == L"2");
}

TEST(parseOutside)
{
    const InfDocument doc = InfDocument::parse(L"; comment\nx = 1\n[A]\n");
    CHECK_EQ(doc.entriesOutsideSections().size(), size_t(1));
}

TEST(parseEmptyValues)
{
    const InfDocument doc = InfDocument::parse(L"[A]\nHKR,,Name,,\nk =\n");
    const InfLine &l = doc.lines()[size_t(doc.section(L"A")->entries[0])];
    CHECK_EQ(l.values.size(), size_t(5));
    CHECK(l.value(2) == L"Name");
    const InfLine *k = doc.entry(L"A", L"k");
    CHECK(k && k->values.empty());
}

// ---------------------------------------------------------------- файлы

TEST(encodings)
{
    struct Case {
        Encoding enc;
        LineEnding eol;
        bool ctrlz;
    };
    const Case cases[] = {
        {Encoding::Ansi, LineEnding::CRLF, false}, {Encoding::Ansi, LineEnding::LF, true},
        {Encoding::Utf8Bom, LineEnding::CRLF, false}, {Encoding::Utf8, LineEnding::CRLF, false},
        {Encoding::Utf16LE, LineEnding::CRLF, false}, {Encoding::Utf16BE, LineEnding::LF, false},
    };
    for (const Case &c : cases) {
        FileFormat f;
        f.encoding = c.enc;
        f.lineEnding = c.eol;
        f.hasBom = true;
        f.trailingCtrlZ = c.ctrlz;
        // для ANSI только ASCII: кодовая страница системы заранее неизвестна
        const wstring text = c.enc == Encoding::Ansi ? L"[Version]\nSignature=\"$Windows NT$\"\n" : wstring(kSample);
        const Bytes bytes = encode(text, f);
        const DecodedFile d = decode(bytes);
        CHECK(d.format.encoding == c.enc);
        CHECK(d.format.lineEnding == c.eol);
        CHECK(d.format.trailingCtrlZ == c.ctrlz);
        CHECK(d.text == text);
        CHECK(encode(d.text, d.format) == bytes);
    }
}

// Все INF из C:\Windows\INF: разбор без сбоев и сохранение байт в байт
TEST(systemInfRoundTrip)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"C:\\Windows\\INF\\*.inf", &fd);
    if (h == INVALID_HANDLE_VALUE) {
        std::printf("      пропущено: нет C:\\Windows\\INF\n");
        return;
    }
    int checked = 0, skipped = 0;
    do {
        const wstring path = wstring(L"C:\\Windows\\INF\\") + fd.cFileName;
        Bytes bytes;
        if (!readBytes(path, bytes))
            continue;
        const DecodedFile d = decode(bytes);
        const auto a = Analysis::run(d.text);
        CHECK(!a->doc.lines().empty());
        if (d.format.mixedLineEndings || d.lossy) {
            ++skipped;
            continue;
        }
        CHECK_MSG(encode(d.text, d.format) == bytes, L"Сохранение изменило файл " + path);
        ++checked;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::printf("      проверено файлов: %d, пропущено (смешанные концы строк): %d\n", checked, skipped);
    CHECK(checked > 0);
}

TEST(writeAndReadFile)
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const wstring path = wstring(tmp) + L"inf_editor_test.inf";
    FileFormat f;
    f.encoding = Encoding::Utf16LE;
    f.hasBom = true;
    wstring error;
    CHECK(writeFile(path, kSample, f, &error));
    DecodedFile d;
    CHECK(readFile(path, d, &error));
    CHECK(d.text == kSample);
    CHECK(d.format.encoding == Encoding::Utf16LE);
    DeleteFileW(path.c_str());
}

// ---------------------------------------------------------------- модель

TEST(model)
{
    const auto a = Analysis::run(kSample);
    const InfModel &m = a->model;
    CHECK_EQ(m.manufacturers().size(), size_t(1));
    CHECK(m.manufacturers().front().displayName == L"Manufacturer");
    CHECK_EQ(m.models().size(), size_t(1));
    const DeviceModel &dm = m.models().front();
    CHECK(dm.installSection == L"Sample_Device.NT");
    CHECK(dm.architecture == L"amd64");
    CHECK(dm.hardwareIds == StringList{L"Root\\Sample"});
    CHECK(dm.compatibleIds == StringList{L"*PNP0C00"});
    CHECK(m.role(L"Sample_Device.NT.Services") == SectionRole::InstallServices);
    CHECK(m.role(L"Sample_Service_Inst") == SectionRole::ServiceInstall);
    CHECK(m.role(L"Sample_More") == SectionRole::CopyFilesList);
    CHECK(m.resolve(L"%DiskName% 100%% %13%") == L"Disk \"1\" 100% %13%");
    CHECK_EQ(m.copiedFiles().size(), size_t(2));
    CHECK_EQ(m.stringUses(L"MfgName"), 1);
}

TEST(validSampleHasNoErrors)
{
    const auto a = Analysis::run(kSample);
    CHECK_MSG(countSeverity(a->diagnostics, Severity::Error) == 0, dump(a->diagnostics));
    CHECK_MSG(countSeverity(a->diagnostics, Severity::Warning) == 0, dump(a->diagnostics));
}

TEST(validatorFindsProblems)
{
    wstring t = kSample;
    t = replaceAll(t, L"%MfgName% = Standard,NTamd64.10.0...16299", L"%MfgName% = Standard,NTamd64.10.0...16299,NTarm64.10.0...16299");
    t = replaceAll(t, L"Sample.SvcDesc    = \"Sample Service\"\n", L"");
    t = replaceAll(t, L"sample.sys = 1,,", L"other.sys = 2,,");
    t = replaceAll(t, L"DriverVer   = 01/15/2026,1.2.3.4", L"DriverVer   = 13/45/2026,1.2.3");
    t = replaceAll(t, L"ClassGuid   = {78A1C341-4539-11d3-B88D-00C04FAD5171}", L"ClassGuid   = {4d36e972-e325-11ce-bfc1-08002be10318}");
    t += L"\n[Orphan]\nfoo = bar\n\n[Bad_AddReg]\nHKXX,,a\n";
    t = replaceAll(t, L"CopyFiles = Sample_Files", L"AddReg = Bad_AddReg\nCopyFiles = Sample_Files");
    const auto a = Analysis::run(t);
    const auto &d = a->diagnostics;
    for (const wchar_t *code : {L"R001", L"T001", L"F001", L"F002", L"V017", L"V009", L"R002", L"A001"})
        CHECK_MSG(hasCode(d, code), wstring(L"нет ") + code + L"\n" + dump(d));
}

TEST(undefinedStringPosition)
{
    const auto a = Analysis::run(L"[Version]\nSignature=\"$Windows NT$\"\nProvider = %Nope%\n");
    bool found = false;
    for (const Diagnostic &d : a->diagnostics) {
        if (d.code == L"T001") {
            found = true;
            CHECK_EQ(d.line, 2);
            CHECK_EQ(d.column, 11);
            CHECK_EQ(d.length, 6);
        }
    }
    CHECK(found);
}

// Ложные срабатывания, найденные на системных INF: числа через строки, модель
// без ID оборудования, секции .WMI, ссылки на секции включённого INF
TEST(systemInfPatterns)
{
    const wchar_t *text = LR"INF([Version]
Signature = "$WINDOWS NT$"
Class = System
ClassGuid = {4d36e97d-e325-11ce-bfc1-08002be10318}
Provider = %P%
LayoutFile = layout.inf
DriverVer = 06/21/2006,10.0.0.1

[Manufacturer]
%P% = Std,NTamd64

[Std.NTamd64]
%D% = Dev_Inst
%D% = Dev_Inst, ROOT\X

[Dev_Inst.NT]
Include = ks.inf
Needs = KS.Registration
AddReg = KS.Shared

[Dev_Inst.NT.Services]
AddService = x, %SPSVCINST_ASSOCSERVICE%, X_Svc

[Dev_Inst.NT.WMI]
WMIInterface = {12345678-1234-1234-1234-123456789012},1,WMISecurity

[WMISecurity]
Security = "O:BAG:BAD:(A;;GA;;;BA)"

[X_Svc]
ServiceType = %SERVICE_KERNEL_DRIVER%
StartType = %SERVICE_DEMAND_START%
ErrorControl = %SERVICE_ERROR_NORMAL%
ServiceBinary = %12%\x.sys

[Strings]
P = "P"
D = "D"
SPSVCINST_ASSOCSERVICE = 0x00000002
SERVICE_KERNEL_DRIVER = 1
SERVICE_DEMAND_START = 3
SERVICE_ERROR_NORMAL = 1
)INF";
    const auto a = Analysis::run(text);
    CHECK_MSG(countSeverity(a->diagnostics, Severity::Error) == 0, dump(a->diagnostics));
    CHECK_MSG(countSeverity(a->diagnostics, Severity::Warning) == 0, dump(a->diagnostics));
    CHECK(a->model.role(L"WMISecurity") == SectionRole::WmiSecurity);
}

TEST(help)
{
    const auto a = Analysis::run(kSample);
    const InfLine *st = a->doc.entry(L"Sample_Service_Inst", L"StartType");
    const int idx = int(st - a->doc.lines().data());
    CHECK(contains(describeEntry(*a, idx), L"SERVICE_DEMAND_START"));
    CHECK(contains(describeSection(*a, L"Sample_Service_Inst"), L"AddService"));
}

// ---------------------------------------------------------------- правка

TEST(editSetExisting)
{
    const wstring t = kSample;
    const InfDocument doc = InfDocument::parse(t);
    const wstring r = applyEdit(t, setValueEdit(doc, L"Version", L"signature", L"\"$Chicago$\""));
    CHECK(contains(r, L"Signature   = \"$Chicago$\"   ; подпись\n"));
    CHECK(split(r, L'\n').size() == split(t, L'\n').size());
}

TEST(editAppendAndCreate)
{
    const wstring t = kSample;
    const InfDocument doc = InfDocument::parse(t);
    wstring r = applyEdit(t, setValueEdit(doc, L"Version", L"ExtensionId", L"{x}"));
    CHECK(contains(r, L"PnpLockdown = 1\nExtensionId = {x}\n\n[DestinationDirs]"));

    r = applyEdit(t, setValueEdit(doc, L"ControlFlags", L"ExcludeFromSelect", L"*"));
    CHECK(contains(r, L"\n\n[ControlFlags]\nExcludeFromSelect = *\n"));

    const wstring tiny = L"[Strings]\na=\"b\"\n";
    r = applyEdit(tiny, setValueEdit(InfDocument::parse(tiny), L"Version", L"Signature", L"\"$Windows NT$\""));
    CHECK(r == L"[Version]\nSignature = \"$Windows NT$\"\n\n[Strings]\na=\"b\"\n");
}

TEST(editRemove)
{
    const wstring t = kSample;
    const InfDocument doc = InfDocument::parse(t);
    const wstring r = applyEdit(t, removeKeyEdit(doc, L"Sample_Device.NT", L"CopyFiles"));
    CHECK(!contains(r, L"CopyFiles"));
    CHECK_EQ(split(r, L'\n').size(), split(t, L'\n').size() - 2);
}

TEST(editRenameSection)
{
    const wstring t = kSample;
    const auto a = Analysis::run(t);
    const wstring r = applySpans(t, renameSectionEdits(a->doc, a->model, L"Sample_More", L"Extra_Files"));
    CHECK(contains(r, L"[Extra_Files]"));
    CHECK(contains(r, L"            Extra_Files"));
    CHECK(!contains(r, L"Sample_More"));
    const auto b = Analysis::run(r);
    CHECK_MSG(countSeverity(b->diagnostics, Severity::Error) == 0, dump(b->diagnostics));
}

TEST(editRemoveSection)
{
    const wstring t = L"[A]\nx=1\n\n[B]\ny=2\n\n[a]\nz=3\n";
    const InfDocument doc = InfDocument::parse(t);
    const wstring r = applyEdits(t, removeSectionEdits(doc, L"A"));
    CHECK(r == L"[B]\ny=2\n");
}

TEST(editReplaceSection)
{
    const wstring t = L"[A]\nx=1\n\n[B]\ny=2\n";
    const InfDocument doc = InfDocument::parse(t);
    CHECK(sectionText(doc, L"A") == (StringList{L"[A]", L"x=1"}));
    const wstring r = applyEdit(t, replaceSectionEdit(doc, L"A", {L"[A]", L"x=2", L"w=3"}));
    CHECK(r == L"[A]\nx=2\nw=3\n\n[B]\ny=2\n");
}

// ---------------------------------------------------------------- генератор

GeneratorParams sampleParams(DriverKind kind)
{
    GeneratorParams p;
    p.kind = kind;
    p.provider = L"Contoso";
    p.manufacturer = L"Contoso Ltd";
    p.className = kind == DriverKind::WinUsb ? L"" : L"System";
    p.date = Date{2026, 1, 15};
    p.architectures = {L"amd64", L"arm64"};
    p.devices = {{L"Contoso Device", L"USB\\VID_1234&PID_5678", {}},
                 {L"Contoso Device 2", L"USB\\VID_1234&PID_5679", {L"USB\\Class_FF"}}};
    if (kind == DriverKind::Umdf)
        p.binaryName = L"contoso.dll";
    return p;
}

TEST(generator)
{
    for (int k = 0; k < kDriverKindCount; ++k) {
        const wstring text = generateInf(sampleParams(DriverKind(k)));
        const auto a = Analysis::run(text);
        CHECK_MSG(countSeverity(a->diagnostics, Severity::Error) == 0, driverKindName(DriverKind(k)) + L"\n" + dump(a->diagnostics) + L"\n" + text);
        CHECK_MSG(countSeverity(a->diagnostics, Severity::Warning) == 0, driverKindName(DriverKind(k)) + L"\n" + dump(a->diagnostics) + L"\n" + text);
    }
}

// ---------------------------------------------------------------- InfVerif

TEST(infVerifParse)
{
    const wstring out = L"Validating test.inf\r\n"
                        L"ERROR(1233) in C:\\x\\test.inf, line 1: Missing directive CatalogFile required for digital signature.\r\n"
                        L"WARNING(2222) in C:\\x, y\\test.inf, line 17: Something.\r\n"
                        L"INF is NOT VALID\r\n";
    bool valid = true;
    const auto d = parseInfVerifOutput(out, &valid);
    CHECK(!valid);
    CHECK_EQ(d.size(), size_t(2));
    if (d.size() != 2)
        return;
    CHECK(d[0].code == L"1233");
    CHECK_EQ(d[0].line, 0);
    CHECK(d[1].severity == Severity::Warning);
    CHECK_EQ(d[1].line, 16);
}

// Сгенерированный INF проходит InfVerif /w (если WDK установлен)
TEST(generatedPassesInfVerif)
{
    const wstring tool = findInfVerif();
    if (tool.empty()) {
        std::printf("      пропущено: InfVerif не найден\n");
        return;
    }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    for (DriverKind k : {DriverKind::Kmdf, DriverKind::WinUsb, DriverKind::NullDriver}) {
        GeneratorParams p = sampleParams(k);
        p.devices = {{L"Contoso Device", k == DriverKind::Kmdf ? L"PCI\\VEN_1234&DEV_5678" : L"USB\\VID_1234&PID_5678", {}}};
        const wstring path = wstring(tmp) + L"mydriver.inf";
        CHECK(writeFile(path, generateInf(p), FileFormat()));
        const InfVerifResult r = runInfVerif(tool, L"/w", path);
        CHECK_MSG(r.started && r.valid && countSeverity(r.diagnostics, Severity::Error) == 0,
                  driverKindName(k) + L"\n" + r.output + r.error);
        DeleteFileW(path.c_str());
    }
}

// ---------------------------------------------------------------- Driver Store и экспорт в CAB

wstring tempDir(const wstring &name)
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const wstring dir = wstring(tmp) + L"inf_editor_test_" + name;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void removeTree(const wstring &dir)
{
    const wstring from = dir + L'\0';
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

void writeText(const wstring &path, const std::string &text)
{
    writeBytes(path, text);
}

// Распаковка CAB системной программой expand.exe
bool expandCab(const wstring &cab, const wstring &dir)
{
    wstring cmd = L"expand.exe -F:* \"" + cab + L"\" \"" + dir + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

TEST(flattenPaths)
{
    const wstring t = L"[SourceDisksNames.amd64]\n1 = %Disk%,,,\\amd64 ; диск\n2 = \"Disk 2\",tag.txt\n\n"
                      L"[SourceDisksFiles]\nfoo.sys = 1,drivers,1234\nbar.dll = 2\n";
    int changed = 0;
    const wstring r = flattenInfPaths(t, &changed);
    CHECK_EQ(changed, 2);
    CHECK(contains(r, L"1 = %Disk%,,,\"\" ; диск\n"));
    CHECK(contains(r, L"foo.sys = 1,,1234\n"));
    CHECK(contains(r, L"bar.dll = 2\n"));
}

TEST(cabWriter)
{
    const wstring dir = tempDir(L"cab");
    writeText(dir + L"\\a.txt", "first file\r\n");
    writeText(dir + L"\\b.bin", std::string(100000, 'x'));
    const wstring cab = dir + L"\\test.cab";
    wstring error;
    CHECK_MSG(writeCab(cab, {{dir + L"\\a.txt", L"a.txt"}, {dir + L"\\b.bin", L"b.bin"}}, &error), error);
    const wstring out = dir + L"\\out";
    CreateDirectoryW(out.c_str(), nullptr);
    CHECK(expandCab(cab, out));
    Bytes a, b;
    CHECK(readBytes(out + L"\\a.txt", a) && a == "first file\r\n");
    CHECK(readBytes(out + L"\\b.bin", b) && b == std::string(100000, 'x'));
    removeTree(dir);
}

const char *kPackageInf = "[Version]\r\nSignature=\"$WINDOWS NT$\"\r\nClass=System\r\n"
                          "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\nProvider=%P%\r\nCatalogFile=pkg.cat\r\n"
                          "DriverVer=01/15/2026,1.0.0.0\r\nPnpLockdown=1\r\n\r\n"
                          "[DestinationDirs]\r\nDefaultDestDir=13\r\n\r\n"
                          "[SourceDisksNames]\r\n1=%Disk%,,,\\bin\r\n\r\n"
                          "[SourceDisksFiles]\r\npkg.sys=1,amd64\r\npkg.dll=1\r\n\r\n"
                          "[Manufacturer]\r\n%P%=Std,NTamd64.10.0...16299\r\n\r\n"
                          "[Std.NTamd64.10.0...16299]\r\n%D%=Pkg_Inst,Root\\Pkg\r\n\r\n"
                          "[Pkg_Inst.NT]\r\nCopyFiles=Pkg_Files\r\n\r\n[Pkg_Files]\r\npkg.sys\r\npkg.dll\r\n\r\n"
                          "[Pkg_Inst.NT.Services]\r\nAddService=Pkg,0x00000002,Pkg_Svc\r\n\r\n"
                          "[Pkg_Svc]\r\nServiceType=1\r\nStartType=3\r\nErrorControl=1\r\nServiceBinary=%13%\\pkg.sys\r\n\r\n"
                          "[Strings]\r\nP=\"Contoso\"\r\nD=\"Pkg\"\r\nDisk=\"Disk\"\r\n";

TEST(exportFlatten)
{
    const wstring dir = tempDir(L"pkg");
    CreateDirectoryW((dir + L"\\bin").c_str(), nullptr);
    CreateDirectoryW((dir + L"\\bin\\amd64").c_str(), nullptr);
    writeText(dir + L"\\pkg.inf", kPackageInf);
    writeText(dir + L"\\pkg.cat", "catalog");
    writeText(dir + L"\\bin\\amd64\\pkg.sys", "driver");
    writeText(dir + L"\\bin\\pkg.dll", "library");

    const ExportPlan plan = planExport(dir + L"\\pkg.inf", true);
    CHECK_MSG(plan.errors.empty(), join(plan.errors, L"\n"));
    CHECK(plan.infChanged);
    CHECK_EQ(plan.files.size(), size_t(3));
    for (const ExportFile &f : plan.files)
        CHECK(!contains(f.nameInCab, L'\\'));

    const wstring cab = dir + L"\\pkg.cab";
    wstring error;
    CHECK_MSG(exportToCab(plan, cab, &error), error);
    const wstring out = dir + L"\\out";
    CreateDirectoryW(out.c_str(), nullptr);
    CHECK(expandCab(cab, out));
    for (const wchar_t *name : {L"pkg.inf", L"pkg.sys", L"pkg.dll", L"pkg.cat"})
        CHECK_MSG(GetFileAttributesW((out + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES, name);

    // распакованный INF берёт все файлы из корня: повторный план находит их без поиска
    DecodedFile d;
    CHECK(readFile(out + L"\\pkg.inf", d));
    CHECK(contains(d.text, L"1=%Disk%,,,\"\""));
    CHECK(contains(d.text, L"pkg.sys=1,"));
    const ExportPlan again = planExport(out + L"\\pkg.inf", true);
    CHECK(again.errors.empty() && again.warnings.empty() && !again.infChanged);

    // без изменения путей архив повторяет структуру пакета
    const ExportPlan keep = planExport(dir + L"\\pkg.inf", false);
    bool nested = false;
    for (const ExportFile &f : keep.files)
        nested = nested || f.nameInCab == L"bin\\amd64\\pkg.sys";
    CHECK(nested && !keep.infChanged);
    removeTree(dir);
}

TEST(exportCollision)
{
    const wstring dir = tempDir(L"collision");
    CreateDirectoryW((dir + L"\\x86").c_str(), nullptr);
    CreateDirectoryW((dir + L"\\amd64").c_str(), nullptr);
    writeText(dir + L"\\c.inf", "[Version]\r\nSignature=\"$WINDOWS NT$\"\r\n[SourceDisksNames]\r\n1=\"Disk\"\r\n"
                                "[SourceDisksFiles.x86]\r\nc.sys=1,x86\r\n[SourceDisksFiles.amd64]\r\nc.sys=1,amd64\r\n");
    writeText(dir + L"\\x86\\c.sys", "x86 driver");
    writeText(dir + L"\\amd64\\c.sys", "amd64 driver");
    const ExportPlan plan = planExport(dir + L"\\c.inf", true);
    CHECK_EQ(plan.errors.size(), size_t(1));
    removeTree(dir);
}

TEST(systemDevicesAndPackages)
{
    CHECK(isThirdPartyInf(L"oem12.inf") && isThirdPartyInf(L"C:\\Windows\\INF\\OEM3.INF"));
    CHECK(!isThirdPartyInf(L"usbport.inf") && !isThirdPartyInf(L"oemx.inf"));
    wstring error;
    const auto devices = enumerateDevices(&error);
    CHECK_MSG(!devices.empty(), error);
    const auto packages = enumerateDriverPackages(devices, true);
    std::printf("      устройств: %zu, сторонних пакетов: %zu\n", devices.size(), packages.size());
    for (const DriverPackage &p : packages)
        CHECK_MSG(GetFileAttributesW(p.storeInf.c_str()) != INVALID_FILE_ATTRIBUTES, p.publishedName);
    if (packages.empty())
        return;
    // экспорт настоящего стороннего пакета, используемого устройством
    const DriverPackage *used = &packages.front();
    for (const DriverPackage &p : packages)
        if (p.devices > 0) {
            used = &p;
            break;
        }
    const ExportPlan plan = planExport(used->storeInf, true);
    const wstring dir = tempDir(L"real");
    const wstring cab = dir + L"\\" + suggestedCabName(*used);
    if (plan.errors.empty()) {
        CHECK_MSG(exportToCab(plan, cab, &error), error);
        const wstring out = dir + L"\\out";
        CreateDirectoryW(out.c_str(), nullptr);
        CHECK(expandCab(cab, out));
        const ExportPlan again = planExport(out + L"\\" + plan.infName, true);
        CHECK_MSG(again.errors.empty() && !again.infChanged, used->publishedName + L": " + join(again.errors, L"; "));
        std::printf("      экспортирован %s (%s): файлов %zu\n", toUtf8(used->publishedName).c_str(),
                    toUtf8(used->originalName).c_str(), plan.files.size() + 1);
    } else {
        std::printf("      %s: %s\n", toUtf8(used->publishedName).c_str(), toUtf8(join(plan.errors, L"; ")).c_str());
    }
    removeTree(dir);
}

} // namespace

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const TestCase &t : tests()) {
        g_current = t.name;
        const int before = g_failed;
        t.fn();
        if (g_failed == before)
            std::printf("PASS  %s\n", t.name);
    }
    std::printf("\nПроверок: %d, неудачных: %d\n", g_checks, g_failed);
    CoUninitialize();
    return g_failed == 0 ? 0 : 1;
}
