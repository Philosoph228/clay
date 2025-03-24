#include <Windows.h>
#include "../../clay.h"

HDC renderer_hdcMem = {0};
HBITMAP renderer_hbmMem = {0};
HANDLE renderer_hOld = {0};

void Clay_Win32_Render(HWND hwnd, Clay_RenderCommandArray renderCommands, LPCSTR* fonts)
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
			HFONT font = CreateFontA(textData->fontSize, 0, 0, 0, 0, false, false, false, ANSI_CHARSET, 0, 0, DEFAULT_QUALITY, DEFAULT_PITCH, fonts[textData->fontId]);
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

                FillRgn(renderer_hdcMem, roundedRectRgn, recColor);
                DeleteObject(roundedRectRgn);
            }
            else
            {
                FillRect(renderer_hdcMem, &r, recColor);
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

            r.left = boundingBox.x;
            r.top = boundingBox.y;
            r.right = boundingBox.x + boundingBox.width;
            r.bottom = boundingBox.y + boundingBox.height;

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
                MoveToEx(renderer_hdcMem, r.left - 0.5f * brd.width.left, r.top, NULL);
                LineTo(renderer_hdcMem, r.right + 0.5f * brd.width.right, r.top);

                SelectObject(renderer_hdcMem, leftPen);
                MoveToEx(renderer_hdcMem, r.left, r.top - 0.5f * brd.width.top, NULL);
                LineTo(renderer_hdcMem, r.left, r.bottom + 0.5f * brd.width.bottom);

                SelectObject(renderer_hdcMem, bottomPen);
                MoveToEx(renderer_hdcMem, r.left - 0.5f * brd.width.left, r.bottom, NULL);
                LineTo(renderer_hdcMem, r.right + 0.5f * brd.width.right, r.bottom);

                SelectObject(renderer_hdcMem, rightPen);
                MoveToEx(renderer_hdcMem, r.right, r.top + 0.5f * brd.width.top, NULL);
                LineTo(renderer_hdcMem, r.right, r.bottom + 0.5f * brd.width.bottom);
            }
            else
            {
                // TODO: Clean up this mess so it's readable

                RECT arcRC_TL = { r.left, r.top, r.left + 2 * brd.cornerRadius.topLeft, r.top + 2 * brd.cornerRadius.topLeft };
                RECT arcRC_TR = { r.right - 2 * brd.cornerRadius.topRight, r.top, r.right, r.top + 2 * brd.cornerRadius.topRight };
                RECT arcRC_BL = { r.left, r.bottom - 2 * brd.cornerRadius.bottomLeft, r.left + 2 * brd.cornerRadius.topLeft, r.bottom };
                RECT arcRC_BR = { r.right - 2 * brd.cornerRadius.bottomLeft, r.bottom - 2 * brd.cornerRadius.bottomLeft, r.right, r.bottom };

                SetArcDirection(renderer_hdcMem, AD_CLOCKWISE);
                Arc(renderer_hdcMem, arcRC_TL.left, arcRC_TL.top, arcRC_TL.right, arcRC_TL.bottom, 
                    arcRC_TL.left, (arcRC_TL.top + arcRC_TL.bottom) / 2, (arcRC_TL.left + arcRC_TL.right) / 2, arcRC_TL.top);

                MoveToEx(renderer_hdcMem, (arcRC_TL.left + arcRC_TL.right) / 2, r.top, NULL);
                LineTo(renderer_hdcMem, (arcRC_TR.left + arcRC_TR.right) / 2, r.top);

                SelectObject(renderer_hdcMem, rightPen);
                Arc(renderer_hdcMem, arcRC_TR.left, arcRC_TR.top, arcRC_TR.right, arcRC_TR.bottom,
                    (arcRC_TR.left + arcRC_TR.right) / 2, arcRC_TR.top, arcRC_TR.right, (arcRC_TR.top + arcRC_TR.bottom) / 2);

                MoveToEx(renderer_hdcMem, r.right, (arcRC_TR.top + arcRC_TR.bottom) / 2, NULL);
                LineTo(renderer_hdcMem, r.right, (arcRC_BR.top + arcRC_BR.bottom) / 2);

                SelectObject(renderer_hdcMem, leftPen);
                SetArcDirection(renderer_hdcMem, AD_COUNTERCLOCKWISE);
                Arc(renderer_hdcMem, arcRC_BL.left, arcRC_BL.top, arcRC_BL.right, arcRC_BL.bottom,
                    arcRC_BL.left, (arcRC_BL.top + arcRC_BL.bottom) / 2, (arcRC_BL.left + arcRC_BL.right) / 2, arcRC_BL.bottom);

                MoveToEx(renderer_hdcMem, r.left, (arcRC_TL.top + arcRC_TL.bottom) / 2, NULL);
                LineTo(renderer_hdcMem, r.left, (arcRC_BL.top + arcRC_BL.bottom) / 2);

                SelectObject(renderer_hdcMem, bottomPen);
                Arc(renderer_hdcMem, arcRC_BR.left, arcRC_BR.top, arcRC_BR.right, arcRC_BR.bottom,
                    (arcRC_BR.left + arcRC_BR.right) / 2, arcRC_BR.bottom, arcRC_BR.right, (arcRC_BR.top + arcRC_BR.bottom) / 2);

                MoveToEx(renderer_hdcMem, (arcRC_BL.left + arcRC_BL.right) / 2, r.bottom, NULL);
                LineTo(renderer_hdcMem, (arcRC_BR.left + arcRC_BR.right) / 2, r.bottom);
                
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
            // PLACEHOLDER: Loads a black rectangle instead of an image.
            RECT r = rc;

            r.left = boundingBox.x;
            r.top = boundingBox.y;
            r.right = boundingBox.x + boundingBox.width;
            r.bottom = boundingBox.y + boundingBox.height;

            HBRUSH recColor = CreateSolidBrush(RGB(0, 0, 0));

            FillRect(renderer_hdcMem, &r, recColor);

            DeleteObject(recColor);
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
    LPCSTR* fonts = userData;
    HFONT font = CreateFontA(config->fontSize, 0, 0, 0, 0, false, false, false, ANSI_CHARSET, 0, 0, DEFAULT_QUALITY, DEFAULT_PITCH, fonts[config->fontId]);
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