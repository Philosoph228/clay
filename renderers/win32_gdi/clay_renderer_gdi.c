#include <Windows.h>

// #define USE_INTRINSICS
// #define USE_FAST_SQRT

#if defined(USE_INTRINSICS)
#include <immintrin.h>
#endif

#include "../../clay.h"

HDC renderer_hdcMem = {0};
HBITMAP renderer_hbmMem = {0};
HANDLE renderer_hOld = {0};
bool gdi_fabulous = true;

#define RECTWIDTH(rc)   ((rc).right - (rc).left)
#define RECTHEIGHT(rc)  ((rc).bottom - (rc).top)

/*----------------------------------------------------------------------------+
 | Math stuff start                                                           |
 +----------------------------------------------------------------------------*/
#if defined(USE_INTRINSICS)
#define sqrtf_impl(x) intrin_sqrtf(x)
#elif defined(USE_FAST_SQRT)
#define sqrtf_impl(x) fast_sqrtf(x)
#else
#define sqrtf_impl(x) sqrtf(x)  // Fallback to std sqrtf
#endif

// Use intrinsics
#if defined(USE_INTRINSICS)
inline float intrin_sqrtf(const float f)
{
    __m128 temp = _mm_set_ss(f);
    temp = _mm_sqrt_ss(temp);
    return _mm_cvtss_f32(temp);
}
#endif  // defined(USE_INTRINSICS)

// Use fast inverse square root
#if defined(USE_FAST_SQRT)
float fast_inv_sqrtf(float number)
{
    const float threehalfs = 1.5f;

    float x2 = number * 0.5f;
    float y = number;

    // Evil bit-level hacking
    uint32_t i = *(uint32_t*)&y;
    i = 0x5f3759df - (i >> 1);  // Initial guess for Newton's method
    y = *(float*)&i;

    // One iteration of Newton's method
    y = y * (threehalfs - (x2 * y * y)); // y = y * (1.5 - 0.5 * x * y^2)

    return y;
}

// Fast square root approximation using the inverse square root
float fast_sqrtf(float number)
{
    if (number < 0.0f) return 0.0f; // Handle negative input
    return number * fast_inv_sqrtf(number);
}
#endif
/*----------------------------------------------------------------------------+
 | Math stuff end                                                             |
 +----------------------------------------------------------------------------*/

static inline Clay_Color ColorBlend(Clay_Color base, Clay_Color overlay, float factor)
{
    Clay_Color blended;

    // Normalize alpha values for multiplications
    float base_a = base.a / 255.0f;
    float overlay_a = overlay.a / 255.0f;

    overlay_a *= factor;

    float out_a = overlay_a + base_a * (1.0f - overlay_a);

    // Avoid division by zero and fully transparent cases
    if (out_a <= 0.0f)
    {
        return (Clay_Color) { .a = 0, .r = 0, .g = 0, .b = 0 };
    }

    blended.r = (overlay.r * overlay_a + base.r * base_a * (1.0f - overlay_a)) / out_a;
    blended.g = (overlay.g * overlay_a + base.g * base_a * (1.0f - overlay_a)) / out_a;
    blended.b = (overlay.b * overlay_a + base.b * base_a * (1.0f - overlay_a)) / out_a;
    blended.a = out_a * 255.0f; // Denormalize alpha back

    return blended;
}

static float RoundedRectPixelCoverage(int x, int y, const Clay_CornerRadius radius, int width, int height) {
    // Check if the pixel is in one of the four rounded corners

    if (x < radius.topLeft && y < radius.topLeft) {
        // Top-left corner
        float dx = radius.topLeft - x - 1;
        float dy = radius.topLeft - y - 1;
        float distance = sqrtf_impl(dx * dx + dy * dy);
        if (distance > radius.topLeft)
            return 0.0f;
        if (distance <= radius.topLeft - 1)
            return 1.0f;
        return radius.topLeft - distance;
    }
    else if (x >= width - radius.topRight && y < radius.topRight) {
        // Top-right corner
        float dx = x - (width - radius.topRight);
        float dy = radius.topRight - y - 1;
        float distance = sqrtf_impl(dx * dx + dy * dy);
        if (distance > radius.topRight)
            return 0.0f;
        if (distance <= radius.topRight - 1)
            return 1.0f;
        return radius.topRight - distance;
    }
    else if (x < radius.bottomLeft && y >= height - radius.bottomLeft) {
        // Bottom-left corner
        float dx = radius.bottomLeft - x - 1;
        float dy = y - (height - radius.bottomLeft);
        float distance = sqrtf_impl(dx * dx + dy * dy);
        if (distance > radius.bottomLeft)
            return 0.0f;
        if (distance <= radius.bottomLeft - 1)
            return 1.0f;
        return radius.bottomLeft - distance;
    }
    else if (x >= width - radius.bottomRight && y >= height - radius.bottomRight) {
        // Bottom-right corner
        float dx = x - (width - radius.bottomRight);
        float dy = y - (height - radius.bottomRight);
        float distance = sqrtf_impl(dx * dx + dy * dy);
        if (distance > radius.bottomRight)
            return 0.0f;
        if (distance <= radius.bottomRight - 1)
            return 1.0f;
        return radius.bottomRight - distance;
    }
    else {
        // Not in a corner, full coverage
        return 1.0f;
    }
}

typedef struct {
    HDC hdcMem;
    HBITMAP hbmMem;
    HBITMAP hbmMemPrev;
    void* pBits;
    SIZE size;
} HDCSubstitute;

static void CreateHDCSubstitute(HDCSubstitute* phdcs, HDC hdcSrc, PRECT prc)
{
    if (prc == NULL)
        return;

    phdcs->size = (SIZE){ RECTWIDTH(*prc), RECTHEIGHT(*prc) };
    if (phdcs->size.cx <= 0 || phdcs->size.cy <= 0)
        return;

    phdcs->hdcMem = CreateCompatibleDC(hdcSrc);
    if (phdcs->hdcMem == NULL)
        return;

    // Create a 32-bit DIB section for the memory DC
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = phdcs->size.cx;
    bmi.bmiHeader.biHeight = -phdcs->size.cy;   // I think it's faster? Probably
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    phdcs->pBits = NULL;

    phdcs->hbmMem = CreateDIBSection(phdcs->hdcMem, &bmi, DIB_RGB_COLORS, &phdcs->pBits, NULL, 0);
    if (phdcs->hbmMem == NULL)
    {
        DeleteDC(phdcs->hdcMem);
        return;
    }

    // Select the DIB section into the memory DC
    phdcs->hbmMemPrev = SelectObject(phdcs->hdcMem, phdcs->hbmMem);

    // Copy the content of the target DC to the memory DC
    BitBlt(phdcs->hdcMem, 0, 0, phdcs->size.cx, phdcs->size.cy, hdcSrc, prc->left, prc->top, SRCCOPY);
}

static void DestroyHDCSubstitute(HDCSubstitute* phdcs)
{
    if (phdcs == NULL)
        return;

    // Clean up
    SelectObject(phdcs->hdcMem, phdcs->hbmMemPrev);
    DeleteObject(phdcs->hbmMem);
    DeleteDC(phdcs->hdcMem);

    ZeroMemory(phdcs, sizeof(HDCSubstitute));
}

static void __Clay_Win32_FillRoundRect(HDC hdc, PRECT prc, Clay_Color color, Clay_CornerRadius radius)
{
    HDCSubstitute substitute = { 0 };
    CreateHDCSubstitute(&substitute, hdc, prc);

    bool has_corner_radius = radius.topLeft || radius.topRight || radius.bottomLeft || radius.bottomRight;

    if (has_corner_radius)
    {
        // Limit the corner radius to the minimum of half the width and half the height
        float max_radius = (float)fmin(substitute.size.cx / 2.0f, substitute.size.cy / 2.0f);
        if (radius.topLeft > max_radius)        radius.topLeft = max_radius;
        if (radius.topRight > max_radius)       radius.topRight = max_radius;
        if (radius.bottomLeft > max_radius)     radius.bottomLeft = max_radius;
        if (radius.bottomRight > max_radius)    radius.bottomRight = max_radius;
    }

    // Iterate over each pixel in the DIB section
    uint32_t* pixels = (uint32_t*)substitute.pBits;
    for (int y = 0; y < substitute.size.cy; ++y)
    {
        for (int x = 0; x < substitute.size.cx; ++x)
        {
            float coverage = 1.0f;
            if (has_corner_radius)
                coverage = RoundedRectPixelCoverage(x, y, radius, substitute.size.cx, substitute.size.cy);

            if (coverage > 0.0f)
            {
                uint32_t pixel = pixels[y * substitute.size.cx + x];
                Clay_Color dst_color = {
                    .r = (float)((pixel >> 16) & 0xFF), // Red
                    .g = (float)((pixel >> 8) & 0xFF),  // Green
                    .b = (float)(pixel & 0xFF),         // Blue
                    .a = 255.0f                         // Fully opaque
                };
                Clay_Color blended = ColorBlend(dst_color, color, coverage);

                pixels[y * substitute.size.cx + x] =
                    ((uint32_t)(blended.b) << 0) |
                    ((uint32_t)(blended.g) << 8) |
                    ((uint32_t)(blended.r) << 16);
            }
        }
    }

    // Copy the blended content back to the target DC
    BitBlt(hdc, prc->left, prc->top, substitute.size.cx, substitute.size.cy, substitute.hdcMem, 0, 0, SRCCOPY);
    DestroyHDCSubstitute(&substitute);
}

#define ALPHABLEND_NEAREST  0x0
#define ALPHABLEND_BILINEAR   0x1
#define ALPHABLEND_BICUBIC  0x2

#define FONT_RESOURCE_HFONT     0x01
#define FONT_RESOURCE_STRING    0x02
#define FONT_RESOURCE_LOGFONT   0x03

DWORD g_alphaBlendStretchMode;
DWORD g_fontResourceMode;

int Clay_Win32_SetFontResourceMode(DWORD dwMode)
{
    g_fontResourceMode = dwMode;
}

int Clay_Win32_SetAlphaBlendStretchMode(DWORD dwMode)
{
    g_alphaBlendStretchMode = dwMode;
}

static int InternalAlphaBlend(
    HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest,
    HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc,
    BLENDFUNCTION ftn)
{
    RECT rcDest;
    rcDest.left = xoriginDest;
    rcDest.top = yoriginDest;
    rcDest.right = xoriginDest + wDest;
    rcDest.bottom = yoriginDest + hDest;
    HDCSubstitute sub = { 0 };
    CreateHDCSubstitute(&sub, hdcDest, &rcDest);

    RECT rcSrc;
    rcSrc.left = xoriginSrc;
    rcSrc.top = yoriginSrc;
    rcSrc.right = xoriginSrc + wSrc;
    rcSrc.bottom = yoriginSrc + hSrc;
    HDCSubstitute subOverlay = { 0 };
    CreateHDCSubstitute(&subOverlay, hdcSrc, &rcSrc);

    // Calculate scaling factors
    float xScale = (float)wSrc / (float)wDest;
    float yScale = (float)hSrc / (float)hDest;

    COLORREF* destPixels = (COLORREF*)sub.pBits;
    COLORREF* srcPixels = (COLORREF*)subOverlay.pBits;
    for (int y = 0; y < sub.size.cy; ++y)
    {
        for (int x = 0; x < sub.size.cx; ++x)
        {
            // Nearest neighboor coordinates in source image
            int srcX = (int)(x * xScale);
            int srcY = (int)(y * yScale);

            // Clamp to source dimensions
            srcX = min(max(srcX, 0), wSrc - 1);
            srcY = min(max(srcY, 0), hSrc - 1);

            COLORREF srcPixel = srcPixels[y * subOverlay.size.cx + srcX];
            COLORREF destPixel = destPixels[y * sub.size.cx + x];

            Clay_Color dst_color = {
                .r = (float)((destPixel >> 16) & 0xFF), // Red
                .g = (float)((destPixel >> 8) & 0xFF),  // Green
                .b = (float)(destPixel & 0xFF),         // Blue
                .a = 255.0f                             // Fully opaque
            };

            Clay_Color src_color = {
                .r = (float)((srcPixel >> 16) & 0xFF),
                .g = (float)((srcPixel >> 8) & 0xFF),
                .b = (float)(srcPixel & 0xFF),
                .a = (float)((srcPixel >> 24) & 0xFF)   // Alpha channel
            };

            Clay_Color blended = ColorBlend(dst_color, src_color, ftn.SourceConstantAlpha / 255.0f);

            destPixels[y * sub.size.cx + x] =
                ((uint32_t)(blended.b) << 0) |
                ((uint32_t)(blended.g) << 8) |
                ((uint32_t)(blended.r) << 16);
        }
    }

    DestroyHDCSubstitute(&sub);
    DestroyHDCSubstitute(&subOverlay);

    return 0;
}

#if defined(USE_MSIMG32)
#define AlphaBlend_impl AlphaBlend
#else
#define AlphaBlend_impl InternalAlphaBlend
#endif  // defined(USE_MSIMG32)

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
    BOOL rv = AlphaBlend_impl(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, tempHdc, 0, 0, drawrc.right - drawrc.left, drawrc.bottom - drawrc.top, blend);

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

    BOOL rv = AlphaBlend_impl(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, tempHdc, 0, 0, drawrc.right - drawrc.left, drawrc.bottom - drawrc.top, blend);

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
    AlphaBlend_impl(hdc, x, y, width, height, destDC, 0, 0, width, height, blend);

    DeleteObject(hScaledBitmap);
    DeleteDC(srcDC);
    DeleteDC(destDC);
}

void Clay_Win32_Render(HWND hwnd, Clay_RenderCommandArray renderCommands, void* fonts)
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
            HFONT hFont;
            switch (g_fontResourceMode)
            {
            case FONT_RESOURCE_STRING:
            {
                const char** font_paths = fonts;
                hFont = CreateFontA(textData->fontSize, 0, 0, 0, 0, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, font_paths[textData->fontId]);


                SelectObject(renderer_hdcMem, hFont);
                break;
            }
            case FONT_RESOURCE_HFONT: {
                const HFONT* font_handles = fonts;
                hFont = font_handles[textData->fontId];
                break;
            }
            case FONT_RESOURCE_LOGFONT: {
                LOGFONT* logfonts = fonts;
                logfonts[textData->fontId].lfHeight = textData->fontSize;
                hFont = CreateFontIndirectA(&logfonts[textData->fontId]);
                break;
            }
            }

            HFONT hPrevFont = SelectObject(renderer_hdcMem, hFont);

            RECT r = rc;
            r.left = boundingBox.x;
            r.top = boundingBox.y;
            r.right = boundingBox.x + boundingBox.width + r.right;
            r.bottom = boundingBox.y + boundingBox.height + r.bottom;

            // Actually draw text
            DrawTextA(renderer_hdcMem, renderCommand->renderData.text.stringContents.chars,
                      renderCommand->renderData.text.stringContents.length,
                      &r, DT_TOP | DT_LEFT);

            SelectObject(renderer_hdcMem, hPrevFont);

            if (g_fontResourceMode == FONT_RESOURCE_STRING || g_fontResourceMode == FONT_RESOURCE_LOGFONT)
            {
                DeleteObject(hFont);
                hFont = NULL;
            }

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

            bool translucid = rrd.backgroundColor.a > 0.0f && rrd.backgroundColor.a < 255.0f;
            bool has_rounded_corners = rrd.cornerRadius.topLeft > 0.0f
                || rrd.cornerRadius.topRight > 0.0f
                || rrd.cornerRadius.bottomLeft > 0.0f
                || rrd.cornerRadius.bottomRight > 0.0f;

            if (gdi_fabulous && (translucid || has_rounded_corners))
            {
                __Clay_Win32_FillRoundRect(renderer_hdcMem, &r, rrd.backgroundColor, rrd.cornerRadius);
            }
            else
            {
                HBRUSH recColor = CreateSolidBrush(RGB(rrd.backgroundColor.r, rrd.backgroundColor.g, rrd.backgroundColor.b));

                if (has_rounded_corners)
                {
                    HRGN roundedRectRgn = CreateRoundRectRgn(
                        r.left, r.top, r.right + 1, r.bottom + 1,
                        rrd.cornerRadius.topLeft * 2, rrd.cornerRadius.topLeft * 2);

                    if (translucid)
                    {
                        DrawTransparentRgn(renderer_hdcMem, r, roundedRectRgn, rrd.backgroundColor, rrd.backgroundColor.a);
                    }
                    else
                    {
                        FillRgn(renderer_hdcMem, roundedRectRgn, recColor);
                    }

                    DeleteObject(roundedRectRgn);
                }
                else
                {
                    if (translucid)
                    {
                        DrawTransparentRect(renderer_hdcMem, r, recColor, rrd.backgroundColor.a);
                    }
                    else {
                        FillRect(renderer_hdcMem, &r, recColor);
                    }
                }

                DeleteObject(recColor);
            }

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

bool use_text_textent = true;

static inline Clay_Dimensions Clay_Win32_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData)
{
    Clay_Dimensions textSize = {0};

    if (userData != NULL)
    {
        HFONT hFont = NULL;

        if (g_fontResourceMode == FONT_RESOURCE_STRING)
        {
            PCSTR* fonts = userData;
            hFont = CreateFontA(config->fontSize, 0, 0, 0, 0, false, false, false, ANSI_CHARSET, 0, 0, DEFAULT_QUALITY, DEFAULT_PITCH, fonts[config->fontId]);
        }
        else if (g_fontResourceMode == FONT_RESOURCE_HFONT)
        {
            HFONT* font_handles = userData;
            hFont = font_handles[config->fontId];
        }
        else if (g_fontResourceMode == FONT_RESOURCE_LOGFONT)
        {
            HDC fontDC = CreateCompatibleDC(NULL);
            LOGFONT* logfonts = userData;
            logfonts[config->fontId].lfHeight = config->fontSize;
            hFont = CreateFontIndirectA(&logfonts[config->fontId]);
        }

        if (hFont != NULL)
        {
            HDC fontDC = CreateCompatibleDC(NULL);
            HFONT holdFont = SelectObject(fontDC, hFont);

            if (use_text_textent)
            {
                SIZE size;
                GetTextExtentPoint32(fontDC, text.chars, text.length, &size);


                textSize.width = size.cx;
                textSize.height = size.cy;
            }
            else {
                // Something broken, need REVIEW
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
            }

            SelectObject(fontDC, holdFont);
            DeleteDC(fontDC);
        }
    }

    // Fallback for system bitmap font
    float maxTextWidth = 0.0f;
    float lineTextWidth = 0;
    float textHeight = WIN32_FONT_HEIGHT;

    for (int i = 0; i < text.length; ++i)
    {
        if (text.chars[i] == '\n')
        {
            maxTextWidth = fmax(maxTextWidth, lineTextWidth);
            lineTextWidth = 0;
            continue;
        }

        lineTextWidth += WIN32_FONT_WIDTH;
    }

    // Cap again
    maxTextWidth = fmax(maxTextWidth, lineTextWidth);

    textSize.width = maxTextWidth;
    textSize.height = textHeight;

    return textSize;
}

HFONT Clay_Win32_SimpleCreateFont(const char* filePath, const char* family, int height, int weight)
{
    // Add the font resource to the application instance
    int fontAdded = AddFontResourceEx(filePath, FR_PRIVATE, NULL);
    if (fontAdded == 0) {
        return NULL;
    }

    int fontHeight = height;

    // If negative, treat height as Pt rather than pixels
    if (height < 0) {
        // Get the screen DPI
        HDC hScreenDC = GetDC(HWND_DESKTOP);
        int iScreenDPI = GetDeviceCaps(hScreenDC, LOGPIXELSY);
        ReleaseDC(HWND_DESKTOP, hScreenDC);

        // Convert font height from points to pixels
        fontHeight = MulDiv(height, iScreenDPI, 72);
    }

    // Create the font using the calculated height and the font name
    HFONT hFont = CreateFont(
        fontHeight,             // Height 
        0,                      // Width (0 means default width)
        0,                      // Escapement angle
        0,                      // Orientation angle
        weight,                 // Font weight
        FALSE,                  // Italic
        FALSE,                  // Underline
        FALSE,                  // Strikeout
        ANSI_CHARSET,           // Character set
        OUT_DEFAULT_PRECIS,     // Output precision
        CLIP_DEFAULT_PRECIS,    // Clipping precision
        DEFAULT_QUALITY,        // Font quality
        DEFAULT_PITCH,          // Pitch and family
        family                  // Font name
    );

    return hFont;
}
