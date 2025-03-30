#include <Windows.h>
#include "../../clay.h"

HDC renderer_hdcMem = {0};
HBITMAP renderer_hbmMem = {0};
HANDLE renderer_hOld = {0};

BOOL DrawTransparentRect(HDC hdc, RECT rc, HBRUSH brush, uint8_t opacity)
{
    HDC tempHdc = CreateCompatibleDC(hdc);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, opacity, 0 };
    RECT drawrc = { 0, 0, rc.right - rc.left, rc.bottom - rc.top };

    HBITMAP bmp;
    BITMAPINFO bmi;

    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = rc.right - rc.left;
    bmi.bmiHeader.biHeight = rc.bottom - rc.top;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32; // 32bpp bitmap
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = (rc.right - rc.left) * (rc.bottom - rc.top) * 4;

    bmp = CreateDIBSection(tempHdc, &bmi, DIB_RGB_COLORS, NULL, NULL, 0x0);
    SelectObject(tempHdc, bmp);

    FillRect(tempHdc, &drawrc, brush);
    BOOL rv = AlphaBlend(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, tempHdc, 0, 0, drawrc.right - drawrc.left, drawrc.bottom - drawrc.top, blend);

    DeleteObject(bmp);
    DeleteDC(tempHdc);

    return rv;
}

BOOL DrawTransparentRgn(HDC hdc, RECT rc, HRGN rgn, Clay_Color color, uint8_t opacity)
{
    HDC tempHdc = CreateCompatibleDC(hdc);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    RECT drawrc = { 0, 0, rc.right - rc.left, rc.bottom - rc.top };

    HBITMAP bmp;
    BITMAPINFO bmi;

    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = rc.right - rc.left;
    bmi.bmiHeader.biHeight = rc.bottom - rc.top;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32; // 32bpp bitmap
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = (rc.right - rc.left) * (rc.bottom - rc.top) * 4;

    void* pBits;
    bmp = CreateDIBSection(tempHdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0x0);

    COLORREF premultClr = RGB(
        color.r * color.a / 255, 
        color.g * color.a / 255,
        color.b * color.a / 255
    );
    HBRUSH brush = CreateSolidBrush(premultClr);

    // Set the bitmap to opaque white
    memset(pBits, 0xFF, bmi.bmiHeader.biSizeImage);

    SelectObject(tempHdc, bmp);
    SetBkMode(tempHdc, TRANSPARENT);

    OffsetRgn(rgn, -rc.left, -rc.top);
    FillRgn(tempHdc, rgn, brush);

    DWORD* pixel = (DWORD*)pBits;
    for (int i = 0; i < bmi.bmiHeader.biWidth * bmi.bmiHeader.biHeight; i++) {
        if ((pixel[i] & 0xFF000000) == 0) { // If alpha is 0 (GDI sets the alpha to 0 by default in the area it operates on)
            pixel[i] |= (opacity << 24); // Set the alpha channel
        }
        else {
            pixel[i] = 0; // Make the pixel transparent black.
        }
    }

    BOOL rv = AlphaBlend(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, tempHdc, 0, 0, drawrc.right - drawrc.left, drawrc.bottom - drawrc.top, blend);

    DeleteObject(bmp);
    DeleteObject(brush);
    DeleteDC(tempHdc);

    return rv;
}

void DrawBmp(HDC hdc, int x, int y, int width, int height, HBITMAP hBitmap) {
    HDC srcDC = CreateCompatibleDC(hdc);
    HDC destDC = CreateCompatibleDC(hdc);
    SelectObject(srcDC, hBitmap);

    BITMAP imgdata;
    GetObject(hBitmap, sizeof(BITMAP), &imgdata);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits;
    HBITMAP hScaledBitmap = CreateDIBSection(destDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    SelectObject(destDC, hScaledBitmap);

    // COLORONCOLOR is used here because it massively improves performance over HALFTONE, while simultaneously not destroying the alpha channel.
    // The drawback is that image quality is not as great, but at least I don't have to mess with more DIB sections.
    SetStretchBltMode(destDC, COLORONCOLOR);
    StretchBlt(destDC, 0, 0, width, height, srcDC, 0, 0, imgdata.bmWidth, imgdata.bmHeight, SRCCOPY);
    
    // Premultiply the alpha.
    // This would be better for perfomance if done by the developer during the full-scale bitmap's loading, rather than for every image on every render.
    uint8_t* pixel = (uint8_t*)pBits;
    for (int i = 0; i < width * height * 4; i += 4) {
        pixel[i] = pixel[i] * ((float)pixel[i + 3] / 255.f);
        pixel[i + 1] = pixel[i + 1] * ((float)pixel[i + 3] / 255.f);
        pixel[i + 2] = pixel[i + 2] * ((float)pixel[i + 3] / 255.f);
    }

    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, x, y, width, height, destDC, 0, 0, width, height, blend);

    DeleteObject(hScaledBitmap);
    DeleteDC(srcDC);
    DeleteDC(destDC);
}

void Clay_Win32_Render(HWND hwnd, Clay_RenderCommandArray renderCommands, LOGFONT* fonts)
{
    bool is_clipping = false;
    HRGN clipping_region = {0};

    PAINTSTRUCT ps;
    HDC hdc;
    RECT rc; // Top left of our window

    GetWindowRect(hwnd, &rc);

    hdc = BeginPaint(hwnd, &ps);

    int win_width = rc.right - rc.left,
        win_height = rc.bottom - rc.top;

    // Create an off-screen DC for double-buffering
    renderer_hdcMem = CreateCompatibleDC(hdc);
    renderer_hbmMem = CreateCompatibleBitmap(hdc, win_width, win_height);

    renderer_hOld = SelectObject(renderer_hdcMem, renderer_hbmMem);

    // draw

    for (int j = 0; j < renderCommands.length; j++)
    {
        Clay_RenderCommand *renderCommand = Clay_RenderCommandArray_Get(&renderCommands, j);
        Clay_BoundingBox boundingBox = renderCommand->boundingBox;

        switch (renderCommand->commandType)
        {
        case CLAY_RENDER_COMMAND_TYPE_TEXT:
        {
            Clay_TextRenderData* textData = &renderCommand->renderData.text;
            Clay_Color c = textData->textColor;
            SetTextColor(renderer_hdcMem, RGB(c.r, c.g, c.b));
            SetBkMode(renderer_hdcMem, TRANSPARENT);
            fonts[textData->fontId].lfHeight = textData->fontSize;
            HFONT font = CreateFontIndirectA(&fonts[textData->fontId]);
			SelectObject(renderer_hdcMem, font);

            RECT r = rc;
            r.left = boundingBox.x;
            r.top = boundingBox.y;
            r.right = boundingBox.x + boundingBox.width + r.right;
            r.bottom = boundingBox.y + boundingBox.height + r.bottom;

            DrawTextA(renderer_hdcMem, renderCommand->renderData.text.stringContents.chars,
                      renderCommand->renderData.text.stringContents.length,
                      &r, DT_TOP | DT_LEFT);
			DeleteObject(font);
			
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
        {
            Clay_RectangleRenderData rrd = renderCommand->renderData.rectangle;
            RECT r = rc;

            r.left = boundingBox.x;
            r.top = boundingBox.y;
            r.right = boundingBox.x + boundingBox.width;
            r.bottom = boundingBox.y + boundingBox.height;

            HBRUSH recColor = CreateSolidBrush(RGB(rrd.backgroundColor.r, rrd.backgroundColor.g, rrd.backgroundColor.b));

            if (rrd.cornerRadius.topLeft > 0)
            {
                HRGN roundedRectRgn = CreateRoundRectRgn(
                    r.left, r.top, r.right + 1, r.bottom + 1,
                    rrd.cornerRadius.topLeft * 2, rrd.cornerRadius.topLeft * 2);

                if (rrd.backgroundColor.a == 255.f) {
                    FillRgn(renderer_hdcMem, roundedRectRgn, recColor);
                }
                else {
                    DrawTransparentRgn(renderer_hdcMem, r, roundedRectRgn, rrd.backgroundColor, rrd.backgroundColor.a);
                }
                DeleteObject(roundedRectRgn);
            }
            else
            {
                if (rrd.backgroundColor.a == 255.f) {
                    FillRect(renderer_hdcMem, &r, recColor);
                }
                else {
                    DrawTransparentRect(renderer_hdcMem, r, recColor, rrd.backgroundColor.a);
                }
            }

            DeleteObject(recColor);
            break;
        }

        // The renderer should begin clipping all future draw commands, only rendering content that falls within the provided boundingBox.
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
        {
            is_clipping = true;

            clipping_region = CreateRectRgn(boundingBox.x,
                                            boundingBox.y,
                                            boundingBox.x + boundingBox.width,
                                            boundingBox.y + boundingBox.height);

            SelectClipRgn(renderer_hdcMem, clipping_region);
            break;
        }

        // The renderer should finish any previously active clipping, and begin rendering elements in full again.
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
        {
            SelectClipRgn(renderer_hdcMem, NULL);

            if (clipping_region)
            {
                DeleteObject(clipping_region);
            }

            is_clipping = false;
            clipping_region = NULL;

            break;
        }

        // The renderer should draw a colored border inset into the bounding box.
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
        {
            Clay_BorderRenderData brd = renderCommand->renderData.border;
            RECT r = rc;

            r.left = boundingBox.x + brd.width.left / 2;
            r.top = boundingBox.y + brd.width.top / 2;
            r.right = boundingBox.x + boundingBox.width - brd.width.right / 2;
            r.bottom = boundingBox.y + boundingBox.height - brd.width.bottom / 2;

            LOGBRUSH penAttributes = {
                BS_SOLID,
                RGB(brd.color.r, brd.color.g, brd.color.b),
                (ULONG_PTR)0
            };
            
            HPEN topPen = ExtCreatePen(PS_SOLID | PS_ENDCAP_FLAT | PS_GEOMETRIC, brd.width.top, &penAttributes, 0, NULL);
            HPEN leftPen = ExtCreatePen(PS_SOLID | PS_ENDCAP_FLAT | PS_GEOMETRIC, brd.width.left, &penAttributes, 0, NULL);
            HPEN bottomPen = ExtCreatePen(PS_SOLID | PS_ENDCAP_FLAT | PS_GEOMETRIC, brd.width.bottom, &penAttributes, 0, NULL);
            HPEN rightPen = ExtCreatePen(PS_SOLID | PS_ENDCAP_FLAT | PS_GEOMETRIC, brd.width.right, &penAttributes, 0, NULL);

            HPEN oldPen = SelectObject(renderer_hdcMem, topPen);

            if (brd.cornerRadius.topLeft == 0)
            {
                if (brd.width.top) {
                    MoveToEx(renderer_hdcMem, r.left - 0.5f * brd.width.left, r.top, NULL);
                    LineTo(renderer_hdcMem, r.right + 0.5f * brd.width.right, r.top);
                }

                if (brd.width.left) {
                    SelectObject(renderer_hdcMem, leftPen);
                    MoveToEx(renderer_hdcMem, r.left, r.top - 0.5f * brd.width.top, NULL);
                    LineTo(renderer_hdcMem, r.left, r.bottom + 0.5f * brd.width.bottom);
                }

                if (brd.width.bottom) {
                    SelectObject(renderer_hdcMem, bottomPen);
                    MoveToEx(renderer_hdcMem, r.left - 0.5f * brd.width.left, r.bottom, NULL);
                    LineTo(renderer_hdcMem, r.right + 0.5f * brd.width.right, r.bottom);
                }

                if (brd.width.right) {
                    SelectObject(renderer_hdcMem, rightPen);
                    MoveToEx(renderer_hdcMem, r.right, r.top + 0.5f * brd.width.top, NULL);
                    LineTo(renderer_hdcMem, r.right, r.bottom + 0.5f * brd.width.bottom);
                }
            }
            else
            {
                // TODO: Clean up this mess so it's readable

                RECT arcRC_TL = { 
                    r.left, 
                    r.top, 
                    r.left + 2 * brd.cornerRadius.topLeft - brd.width.top / 2,
                    r.top + 2 * brd.cornerRadius.topLeft - brd.width.left / 2
                };
                RECT arcRC_TR = { 
                    r.right - 2 * brd.cornerRadius.topRight + brd.width.top / 2, 
                    r.top, 
                    r.right, 
                    r.top + 2 * brd.cornerRadius.topRight - brd.width.right / 2
                };
                RECT arcRC_BL = { 
                    r.left, 
                    r.bottom - 2 * brd.cornerRadius.bottomLeft + brd.width.left / 2, 
                    r.left + 2 * brd.cornerRadius.topLeft - brd.width.bottom / 2, 
                    r.bottom 
                };
                RECT arcRC_BR = { 
                    r.right - 2 * brd.cornerRadius.bottomLeft + brd.width.bottom / 2, 
                    r.bottom - 2 * brd.cornerRadius.bottomLeft + brd.width.right / 2, 
                    r.right, 
                    r.bottom 
                };

                SetArcDirection(renderer_hdcMem, AD_CLOCKWISE);

                if (brd.width.top) {
                    Arc(renderer_hdcMem, arcRC_TL.left, arcRC_TL.top, arcRC_TL.right, arcRC_TL.bottom,
                        arcRC_TL.left, (arcRC_TL.top + arcRC_TL.bottom) / 2, (arcRC_TL.left + arcRC_TL.right) / 2, arcRC_TL.top);

                    MoveToEx(renderer_hdcMem, (arcRC_TL.left + arcRC_TL.right) / 2, r.top, NULL);
                    LineTo(renderer_hdcMem, (arcRC_TR.left + arcRC_TR.right) / 2, r.top);
                }

                if (brd.width.right) {
                    SelectObject(renderer_hdcMem, rightPen);
                    Arc(renderer_hdcMem, arcRC_TR.left, arcRC_TR.top, arcRC_TR.right, arcRC_TR.bottom,
                        (arcRC_TR.left + arcRC_TR.right) / 2, arcRC_TR.top, arcRC_TR.right, (arcRC_TR.top + arcRC_TR.bottom) / 2);

                    MoveToEx(renderer_hdcMem, r.right, (arcRC_TR.top + arcRC_TR.bottom) / 2, NULL);
                    LineTo(renderer_hdcMem, r.right, (arcRC_BR.top + arcRC_BR.bottom) / 2);
                }
                
                SetArcDirection(renderer_hdcMem, AD_COUNTERCLOCKWISE);

                if (brd.width.left) {
                    SelectObject(renderer_hdcMem, leftPen);
                    Arc(renderer_hdcMem, arcRC_BL.left, arcRC_BL.top, arcRC_BL.right, arcRC_BL.bottom,
                        arcRC_BL.left, (arcRC_BL.top + arcRC_BL.bottom) / 2, (arcRC_BL.left + arcRC_BL.right) / 2, arcRC_BL.bottom);

                    MoveToEx(renderer_hdcMem, r.left, (arcRC_TL.top + arcRC_TL.bottom) / 2, NULL);
                    LineTo(renderer_hdcMem, r.left, (arcRC_BL.top + arcRC_BL.bottom) / 2);
                }

                if (brd.width.bottom) {
                    SelectObject(renderer_hdcMem, bottomPen);
                    Arc(renderer_hdcMem, arcRC_BR.left, arcRC_BR.top, arcRC_BR.right, arcRC_BR.bottom,
                        (arcRC_BR.left + arcRC_BR.right) / 2, arcRC_BR.bottom, arcRC_BR.right, (arcRC_BR.top + arcRC_BR.bottom) / 2);

                    MoveToEx(renderer_hdcMem, (arcRC_BL.left + arcRC_BL.right) / 2, r.bottom, NULL);
                    LineTo(renderer_hdcMem, (arcRC_BR.left + arcRC_BR.right) / 2, r.bottom);
                }
                
            }

            SelectObject(renderer_hdcMem, oldPen);
            DeleteObject(topPen);
            DeleteObject(leftPen);
            DeleteObject(bottomPen);
            DeleteObject(rightPen);

            break;
        }

        case CLAY_RENDER_COMMAND_TYPE_IMAGE:
        {
            Clay_ImageRenderData ird = renderCommand->renderData.image;
            HBITMAP img = *(HBITMAP*)ird.imageData;

            DrawBmp(renderer_hdcMem, boundingBox.x, boundingBox.y, boundingBox.width, boundingBox.height, img);

            break;
        }

        default:
            printf("Unhandled render command %d\r\n", renderCommand->commandType);
            break;
        }
    }

    BitBlt(hdc, 0, 0, win_width, win_height, renderer_hdcMem, 0, 0, SRCCOPY);

    // Free-up the off-screen DC
    SelectObject(renderer_hdcMem, renderer_hOld);
    DeleteObject(renderer_hbmMem);
    DeleteDC(renderer_hdcMem);

    EndPaint(hwnd, &ps);
}

/*
    Hacks due to the Windows API not making sense to use.... may measure too large, but never too small
*/

#ifndef WIN32_FONT_HEIGHT
#define WIN32_FONT_HEIGHT (16)
#endif

#ifndef WIN32_FONT_WIDTH
#define WIN32_FONT_WIDTH (8)
#endif

static inline Clay_Dimensions Clay_Win32_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData)
{
    Clay_Dimensions textSize = {0};

    HDC fontDC = CreateCompatibleDC(NULL);
    LOGFONT* fonts = userData;
    fonts[config->fontId].lfHeight = config->fontSize;
    HFONT font = CreateFontIndirectA(&fonts[config->fontId]);
    SelectObject(fontDC, font);

    float maxTextWidth = 0.0f;
    float lineTextWidth = 0;
    float textHeight = config->fontSize;
    ABC abcWidth;

    for (int i = 0; i < text.length; ++i)
    {
        if (text.chars[i] == '\n')
        {
            maxTextWidth = fmax(maxTextWidth, lineTextWidth);
            lineTextWidth = 0;
            continue;
        }

        if (!GetCharABCWidthsA(fontDC, text.chars[i], text.chars[i], &abcWidth))
            lineTextWidth += WIN32_FONT_WIDTH;
        else
            lineTextWidth += abcWidth.abcA + abcWidth.abcB + abcWidth.abcC;
    }

    maxTextWidth = fmax(maxTextWidth, lineTextWidth);

    textSize.width = maxTextWidth;
    textSize.height = textHeight;

    DeleteObject(font);
    DeleteDC(fontDC);
    return textSize;
}