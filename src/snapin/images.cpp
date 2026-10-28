#include "stdafx.h"

#include "images.h"

#include "resource.h"
#include "snapinapp.h"

#include <algorithm>
#include <vector>

namespace {

// Цвет-маска: такие пиксели в ленте изображений MMC считаются прозрачными
const COLORREF kMaskColor = RGB(255, 0, 255);

HICON stockIcon(SHSTOCKICONID id, bool large)
{
    SHSTOCKICONINFO info{};
    info.cbSize = sizeof(info);
    if (SUCCEEDED(SHGetStockIconInfo(id, SHGSI_ICON | (large ? SHGSI_LARGEICON : SHGSI_SMALLICON), &info)))
        return info.hIcon;
    return nullptr;
}

// Значок, связанный в системе с типом файла .inf
HICON infFileIcon(bool large)
{
    SHFILEINFOW info{};
    if (SHGetFileInfoW(L".inf", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | (large ? SHGFI_LARGEICON : SHGFI_SMALLICON)))
        return info.hIcon;
    return stockIcon(SIID_DOCASSOC, large);
}

// Значки создаются один раз для каждого размера и живут до выгрузки DLL
HICON icon(Image image, bool large)
{
    static HICON cache[2][ImgCount] = {};
    HICON &slot = cache[large ? 1 : 0][image];
    if (slot)
        return slot;
    HICON h = nullptr;
    switch (image) {
    case ImgApp: h = appIcon(large); break;
    case ImgFolder: h = stockIcon(SIID_FOLDER, large); break;
    case ImgFolderOpen: h = stockIcon(SIID_FOLDEROPEN, large); break;
    case ImgFile: h = infFileIcon(large); break;
    case ImgSection: h = stockIcon(SIID_STACK, large); break;
    case ImgError: h = stockIcon(SIID_ERROR, large); break;
    case ImgWarning: h = stockIcon(SIID_WARNING, large); break;
    case ImgInfo: h = stockIcon(SIID_INFO, large); break;
    case ImgDevice: h = stockIcon(SIID_DRIVEFIXED, large); break;
    case ImgString: h = stockIcon(SIID_RENAME, large); break;
    case ImgCopyFile: h = stockIcon(SIID_DOCNOASSOC, large); break;
    case ImgManufacturer: h = stockIcon(SIID_SERVER, large); break;
    case ImgEntry: h = stockIcon(SIID_KEY, large); break;
    case ImgSettings: h = stockIcon(SIID_SOFTWARE, true); break;  // у малого варианта тёмный фон
    case ImgCount: break;
    }
    if (!h)
        h = stockIcon(SIID_DOCNOASSOC, large);
    slot = h;
    return slot;
}

// Пиксели значка в формате BGRA (сверху вниз) с учётом альфа-канала или маски
std::vector<RGBQUAD> iconPixels(HICON h, int size)
{
    std::vector<RGBQUAD> px(size_t(size) * size);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    // значок рисуется в 32-битный буфер дважды: на чёрном и на белом фоне;
    // по разнице восстанавливается альфа каждого пикселя (работает и для старых значков с маской)
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    void *bitsBlack = nullptr, *bitsWhite = nullptr;
    HBITMAP black = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bitsBlack, nullptr, 0);
    HBITMAP white = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bitsWhite, nullptr, 0);
    RECT r{0, 0, size, size};
    HGDIOBJ old = SelectObject(dc, black);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    DrawIconEx(dc, 0, 0, h, size, size, 0, nullptr, DI_NORMAL);
    SelectObject(dc, white);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    DrawIconEx(dc, 0, 0, h, size, size, 0, nullptr, DI_NORMAL);
    SelectObject(dc, old);
    GdiFlush();

    const auto *b = static_cast<const RGBQUAD *>(bitsBlack);
    const auto *w = static_cast<const RGBQUAD *>(bitsWhite);
    for (size_t i = 0; i < px.size(); ++i) {
        // на чёрном: c = a*C; на белом: c = a*C + (1-a)*255  =>  a = 1 - (белый - чёрный)/255
        const int diff = ((w[i].rgbRed - b[i].rgbRed) + (w[i].rgbGreen - b[i].rgbGreen) + (w[i].rgbBlue - b[i].rgbBlue)) / 3;
        const int alpha = 255 - std::clamp(diff, 0, 255);
        px[i] = b[i];  // цвет, уже умноженный на альфу
        px[i].rgbReserved = BYTE(alpha);
    }
    DeleteObject(black);
    DeleteObject(white);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return px;
}

// Лента значков одного размера: прозрачные пиксели — цвет-маска,
// полупрозрачные смешиваются с цветом фона окна
HBITMAP makeStrip(int size, int count, const Image *images, COLORREF mask, COLORREF background)
{
    const int width = size * count;
    std::vector<RGBQUAD> strip(size_t(width) * size);
    const BYTE bgR = GetRValue(background), bgG = GetGValue(background), bgB = GetBValue(background);
    for (int n = 0; n < count; ++n) {
        std::vector<RGBQUAD> px = iconPixels(icon(images[n], size > 16), size);
        // у некоторых системных значков нет изображения нужного размера: берётся другой размер
        if (std::all_of(px.begin(), px.end(), [](const RGBQUAD &q) { return q.rgbReserved < 8; }))
            px = iconPixels(icon(images[n], size <= 16), size);
        if (std::all_of(px.begin(), px.end(), [](const RGBQUAD &q) { return q.rgbReserved < 8; }))
            px = iconPixels(stockIcon(SIID_DOCNOASSOC, size > 16), size);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const RGBQUAD &s = px[size_t(y) * size + x];
                RGBQUAD &d = strip[size_t(y) * width + size_t(n) * size + x];
                const int a = s.rgbReserved;
                if (a < 8) {
                    d = RGBQUAD{GetBValue(mask), GetGValue(mask), GetRValue(mask), 0};
                    continue;
                }
                // цвет уже умножен на альфу: результат = C*a + фон*(1-a)
                d.rgbRed = BYTE(std::min(255, s.rgbRed + bgR * (255 - a) / 255));
                d.rgbGreen = BYTE(std::min(255, s.rgbGreen + bgG * (255 - a) / 255));
                d.rgbBlue = BYTE(std::min(255, s.rgbBlue + bgB * (255 - a) / 255));
                d.rgbReserved = 0;
                // совпадение с маской случайно сделало бы пиксель прозрачным
                if (d.rgbRed == GetRValue(mask) && d.rgbGreen == GetGValue(mask) && d.rgbBlue == GetBValue(mask))
                    d.rgbGreen = BYTE(d.rgbGreen ^ 1);
            }
        }
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    // обычный (зависимый от устройства) рисунок без альфа-канала: MMC применяет цвет-маску
    HDC screen = GetDC(nullptr);
    HBITMAP bmp = CreateDIBitmap(screen, &bi.bmiHeader, CBM_INIT, strip.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    return bmp;
}

} // namespace

HICON appIcon(bool large)
{
    const int size = GetSystemMetrics(large ? SM_CXICON : SM_CXSMICON);
    return static_cast<HICON>(LoadImageW(AfxGetResourceHandle(), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, LR_SHARED));
}

void loadImages(IImageList *list)
{
    if (!list)
        return;
    Image all[ImgCount];
    for (int i = 0; i < ImgCount; ++i)
        all[i] = Image(i);
    const COLORREF bg = GetSysColor(COLOR_WINDOW);
    HBITMAP small16 = makeStrip(16, ImgCount, all, kMaskColor, bg);
    HBITMAP large32 = makeStrip(32, ImgCount, all, kMaskColor, bg);
    if (traceEnabled()) {
        // при включённой трассировке ленты значков сохраняются для просмотра
        CImage img;
        img.Attach(small16);
        img.Save((traceDir() + L"inf_snapin_images16.png").c_str(), Gdiplus::ImageFormatPNG);
        img.Detach();
        img.Attach(large32);
        img.Save((traceDir() + L"inf_snapin_images32.png").c_str(), Gdiplus::ImageFormatPNG);
        img.Detach();
    }
    // MMC копирует изображения, поэтому рисунки можно сразу удалить
    list->ImageListSetStrip(reinterpret_cast<LONG_PTR *>(small16), reinterpret_cast<LONG_PTR *>(large32), 0, kMaskColor);
    DeleteObject(small16);
    DeleteObject(large32);
}

HBITMAP iconBitmap(Image image, int size, COLORREF mask)
{
    return makeStrip(size, 1, &image, mask, GetSysColor(COLOR_WINDOW));
}
