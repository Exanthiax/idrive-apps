// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Exanthiax
#include "eta_strip.h"

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace woc {

/* FreeType 2's public types, as far as this file reads them (freetype.h,
 * ftimage.h, fttypes.h; LP64). VERIFIED: every offset read here equals
 * offsetof() of FreeType 2.8.1's own headers (Ubuntu 18.04
 * libfreetype6-dev). INFERRED: the same on the unit's libfreetype 2.9
 * (FreeType keeps these structs for soname 6). */
namespace ft {

typedef int Error;
typedef void *Library;

struct Generic {
    void *data;
    void (*finalizer)(void *);
};
struct BBox {
    long xMin, yMin, xMax, yMax;
};
struct Vector {
    long x, y;
};
struct GlyphMetrics {
    long width, height, horiBearingX, horiBearingY, horiAdvance, vertBearingX, vertBearingY, vertAdvance;
};
struct Bitmap {
    unsigned rows, width;
    int pitch;
    unsigned char *buffer;
    unsigned short num_grays;
    unsigned char pixel_mode, palette_mode;
    void *palette;
};
struct GlyphSlotRec {
    Library library;
    void *face;
    GlyphSlotRec *next;
    unsigned glyph_index;
    Generic generic;
    GlyphMetrics metrics;
    long linearHoriAdvance, linearVertAdvance;
    Vector advance;
    int format;
    Bitmap bitmap;
    int bitmap_left, bitmap_top;
    /* more follows */
};
struct SizeMetrics {
    unsigned short x_ppem, y_ppem;
    long x_scale, y_scale;
    long ascender, descender, height, max_advance;
};
struct SizeRec {
    void *face;
    Generic generic;
    SizeMetrics metrics;
    /* more follows */
};
struct FaceRec {
    long num_faces, face_index, face_flags, style_flags, num_glyphs;
    char *family_name, *style_name;
    int num_fixed_sizes;
    void *available_sizes;
    int num_charmaps;
    void *charmaps;
    Generic generic;
    BBox bbox;
    unsigned short units_per_EM;
    short ascender, descender, height, max_advance_width, max_advance_height, underline_position,
        underline_thickness;
    GlyphSlotRec *glyph;
    SizeRec *size;
    /* more follows */
};
typedef FaceRec *Face;

const int kLoadRender = 1 << 2;      /* FT_LOAD_RENDER */
const unsigned char kGray = 2;       /* FT_PIXEL_MODE_GRAY */

}  // namespace ft

struct EtaStrip::Ft {
    void *lib = nullptr;
    ft::Library library = nullptr;
    ft::Face face = nullptr;
    ft::Error (*init)(ft::Library *) = nullptr;
    ft::Error (*new_face)(ft::Library, const char *, long, ft::Face *) = nullptr;
    ft::Error (*pixel_sizes)(ft::Face, unsigned, unsigned) = nullptr;
    unsigned (*char_index)(ft::Face, unsigned long) = nullptr;
    ft::Error (*load_glyph)(ft::Face, unsigned, int32_t) = nullptr;
    ft::Error (*done_face)(ft::Face) = nullptr;
    ft::Error (*done)(ft::Library) = nullptr;
};

namespace {

template <typename F> bool sym(void *lib, const char *name, F *f)
{
    *f = (F)dlsym(lib, name);
    return *f != nullptr;
}

/* The pill's colour and opacity behind the text. */
const float kPillAlpha = 0.62f;

}  // namespace

std::vector<uint32_t> utf8_codes(const std::string &s)
{
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        uint32_t cp = n == 0 ? c : n == 1 ? c & 0x1f : n == 2 ? c & 0x0f : c & 0x07;
        bool ok = n >= 0 && i + (size_t)n < s.size();
        for (int k = 1; ok && k <= n; k++) {
            const unsigned char d = (unsigned char)s[i + k];
            ok = (d >> 6) == 2;
            cp = cp << 6 | (d & 0x3f);
        }
        if (!ok) {
            out.push_back(0xfffd);
            i++;
        } else {
            out.push_back(cp);
            i += (size_t)n + 1;
        }
    }
    return out;
}

EtaStrip::~EtaStrip()
{
    if (!ft_)
        return;
    if (ft_->face)
        ft_->done_face(ft_->face);
    if (ft_->library)
        ft_->done(ft_->library);
    delete ft_;
}

bool EtaStrip::load(const std::string &lib, const std::string &font, unsigned px, std::string *why)
{
    if (ft_)
        return true;
    Ft f;
    f.lib = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!f.lib) {
        const char *e = dlerror();
        *why = std::string("cannot load ") + lib + ": " + (e ? e : "unknown error");
        return false;
    }
    if (!sym(f.lib, "FT_Init_FreeType", &f.init) || !sym(f.lib, "FT_New_Face", &f.new_face) ||
        !sym(f.lib, "FT_Set_Pixel_Sizes", &f.pixel_sizes) || !sym(f.lib, "FT_Get_Char_Index", &f.char_index) ||
        !sym(f.lib, "FT_Load_Glyph", &f.load_glyph) || !sym(f.lib, "FT_Done_Face", &f.done_face) ||
        !sym(f.lib, "FT_Done_FreeType", &f.done)) {
        *why = lib + " lacks a FreeType function";
        return false;
    }
    if (f.init(&f.library)) {
        *why = "FreeType did not start";
        return false;
    }
    if (f.new_face(f.library, font.c_str(), 0, &f.face) || !f.face) {
        *why = "cannot read the font " + font;
        f.done(f.library);
        return false;
    }
    if (f.pixel_sizes(f.face, 0, px)) {
        *why = "the font " + font + " has no " + std::to_string(px) + " px size";
        f.done_face(f.face);
        f.done(f.library);
        return false;
    }
    ft_ = new Ft(f);
    return true;
}

bool EtaStrip::draw(const std::string &text, std::vector<uint8_t> *rgba)
{
    if (!ft_)
        return false;
    const int W = (int)kWidth, H = (int)kHeight;
    /* coverage of the text and of the pill, 0-1 */
    std::vector<float> ink((size_t)W * H, 0.0f), pill((size_t)W * H, 0.0f);
    rgba->assign((size_t)W * H * 4, 0);
    if (text.empty())
        return true;

    /* glyphs and the text's width */
    std::vector<unsigned> glyphs;
    for (uint32_t cp : utf8_codes(text)) {
        unsigned g = ft_->char_index(ft_->face, cp);
        if (!g && cp == 0xb7)   /* a font without the middle dot: a hyphen */
            g = ft_->char_index(ft_->face, '-');
        glyphs.push_back(g);
    }
    long width = 0;
    for (unsigned g : glyphs)
        if (!ft_->load_glyph(ft_->face, g, 0))
            width += ft_->face->glyph->advance.x >> 6;

    const ft::SizeMetrics &m = ft_->face->size->metrics;
    const long asc = m.ascender >> 6, desc = m.descender >> 6;   /* desc below the baseline: negative */
    const int baseline = (int)((H + asc + desc) / 2);
    const int pad = H / 2;
    const int pw = std::min<int>(W, (int)width + 2 * pad);
    const int px0 = (W - pw) / 2;

    /* the pill: a capsule, H high, edges anti-aliased */
    const float r = H / 2.0f;
    for (int y = 0; y < H; y++)
        for (int x = px0; x < px0 + pw; x++) {
            const float cx = std::min(std::max(x + 0.5f, px0 + r), px0 + pw - r), cy = r;
            const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            pill[(size_t)y * W + x] = std::min(1.0f, std::max(0.0f, r - d + 0.5f));
        }

    /* the text, centred in the pill */
    long pen = px0 + (pw - width) / 2;
    for (unsigned g : glyphs) {
        if (ft_->load_glyph(ft_->face, g, ft::kLoadRender))
            continue;
        const ft::GlyphSlotRec *s = ft_->face->glyph;
        const ft::Bitmap &b = s->bitmap;
        if (b.pixel_mode == ft::kGray && b.buffer) {
            for (unsigned by = 0; by < b.rows; by++) {
                const int y = baseline - s->bitmap_top + (int)by;
                if (y < 0 || y >= H)
                    continue;
                const unsigned char *row = b.buffer + (long)by * b.pitch;
                for (unsigned bx = 0; bx < b.width; bx++) {
                    const long x = pen + s->bitmap_left + (long)bx;
                    if (x < 0 || x >= W)
                        continue;
                    float &i = ink[(size_t)y * W + x];
                    i = std::max(i, row[bx] / 255.0f);
                }
            }
        }
        pen += s->advance.x >> 6;
    }

    /* white text over the dark pill, straight alpha */
    for (size_t i = 0; i < ink.size(); i++) {
        const float c = ink[i], bg = pill[i] * kPillAlpha;
        const float a = c + bg * (1 - c);
        if (a <= 0)
            continue;
        const float white = c / a;   /* the pill is black */
        uint8_t *o = &(*rgba)[i * 4];
        o[0] = o[1] = o[2] = (uint8_t)std::lround(white * 255);
        o[3] = (uint8_t)std::lround(a * 255);
    }
    return true;
}

}  // namespace woc
