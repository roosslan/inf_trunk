// Значки узлов и элементов оснастки
#pragma once

enum Image {
    ImgApp,
    ImgFolder,
    ImgFolderOpen,
    ImgFile,
    ImgSection,
    ImgError,
    ImgWarning,
    ImgInfo,
    ImgDevice,
    ImgString,
    ImgCopyFile,
    ImgManufacturer,
    ImgEntry,
    ImgSettings,
    ImgCount
};

// Заполнить список изображений MMC (области дерева или результатов)
void loadImages(IImageList *list);

// Значок приложения (для ISnapinAbout)
HICON appIcon(bool large);

// Растровое изображение значка с прозрачным цветом-маской (для GetStaticFolderImage)
HBITMAP iconBitmap(Image image, int size, COLORREF mask);
