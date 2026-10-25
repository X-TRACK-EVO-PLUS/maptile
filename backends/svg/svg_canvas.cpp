/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "svg_canvas.h"
#include <cstdio>

namespace MapTile {

namespace {

    std::string rgb(const Color& c)
    {
        char buf[16];
        std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
        return buf;
    }

    std::string opacity(const Color& c)
    {
        if (c.a == 255)
            return std::string();
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3f", c.a / 255.0);
        return std::string(" opacity=\"") + buf + "\"";
    }

    std::string num(float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f", v);
        std::string s = buf;
        if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0)
            s.erase(s.size() - 2);
        return s;
    }

    std::string escape(const char* t)
    {
        std::string out;
        for (; *t; t++) {
            switch (*t) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            default:
                out += *t;
            }
        }
        return out;
    }

    /* CJK text is about one em wide per character, Latin about half */
    float textWidth(const char* t, float size)
    {
        float w = 0;
        for (const unsigned char* p = (const unsigned char*)t; *p; p++) {
            if ((*p & 0xc0) == 0x80)
                continue;
            w += *p >= 0xe0 ? size : size * 0.56f;
        }
        return w;
    }

} // namespace

void SvgCanvas::beginFrame(float width, float height, const Color& background)
{
    _svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + num(width) + "\" height=\"" + num(height)
        + "\" viewBox=\"0 0 " + num(width) + " " + num(height) + "\">\n<rect width=\"100%\" height=\"100%\" fill=\""
        + rgb(background) + "\"/>\n";
}

void SvgCanvas::appendPath(const PathBatch& b)
{
    _svg += " d=\"";
    uint32_t i = 0;
    for (size_t s = 0; s < b.ends.size(); s++) {
        for (bool first = true; i < b.ends[s]; i++, first = false) {
            _svg += first ? 'M' : 'L';
            _svg += num(b.pts[i].x);
            _svg += ' ';
            _svg += num(b.pts[i].y);
        }
        if (b.closed[s])
            _svg += 'Z';
    }
    _svg += '"';
}

void SvgCanvas::fill(const PathBatch& rings, const Color& color)
{
    _svg += "<path";
    appendPath(rings);
    _svg += " fill=\"" + rgb(color) + "\" fill-rule=\"nonzero\"" + opacity(color) + "/>\n";
}

void SvgCanvas::stroke(const PathBatch& paths, const LineStyle& s)
{
    _svg += "<path";
    appendPath(paths);
    _svg += " fill=\"none\" stroke=\"" + rgb(s.color) + "\" stroke-width=\"" + num(s.width) + "\"";
    _svg += s.cap == Cap::BUTT ? " stroke-linecap=\"butt\"" : s.cap == Cap::SQUARE ? " stroke-linecap=\"square\""
                                                                                   : " stroke-linecap=\"round\"";
    _svg += s.join == Join::MITER ? " stroke-linejoin=\"miter\"" : s.join == Join::BEVEL ? " stroke-linejoin=\"bevel\""
                                                                                         : " stroke-linejoin=\"round\"";
    if (s.dash && s.dashCount > 0) {
        _svg += " stroke-dasharray=\"";
        for (int i = 0; i < s.dashCount; i++)
            _svg += (i ? " " : "") + num(s.dash[i]);
        _svg += "\"";
    }
    _svg += opacity(s.color) + "/>\n";
}

Vec2 SvgCanvas::measureText(const char* utf8, const TextStyle& style)
{
    return { textWidth(utf8, style.size), style.size * 1.2f };
}

void SvgCanvas::drawText(Vec2 c, const char* utf8, const TextStyle& st)
{
    _svg += "<text x=\"" + num(c.x) + "\" y=\"" + num(c.y + st.size * 0.35f) + "\" font-size=\"" + num(st.size)
        + "\" font-weight=\"" + std::to_string(st.weight) + "\" font-family=\"" + _font
        + "\" text-anchor=\"middle\" fill=\"" + rgb(st.color) + "\"";
    if (st.halo.a)
        _svg += " paint-order=\"stroke\" stroke=\"" + rgb(st.halo) + "\" stroke-width=\"" + num(st.size * 0.25f)
            + "\" stroke-linejoin=\"round\"";
    _svg += ">" + escape(utf8) + "</text>\n";
}

void SvgCanvas::endFrame() { _svg += "</svg>\n"; }

bool SvgCanvas::save(const char* path) const
{
    FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(_svg.data(), 1, _svg.size(), f) == _svg.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace MapTile
