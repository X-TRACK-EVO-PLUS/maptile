/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_SVG_CANVAS_H
#define MAPTILE_SVG_CANVAS_H
#include "maptile/canvas.h"
#include <string>

namespace MapTile {

/* Writes the frame as an SVG document. Used for golden-image tests, for the
 * command-line tool, and to feed design tools with real map geometry. */
class SvgCanvas : public Canvas {
public:
    explicit SvgCanvas(std::string fontFamily = "sans-serif")
        : _font(std::move(fontFamily))
    {
    }

    void beginFrame(float width, float height, const Color& background) override;
    void fill(const PathBatch& rings, const Color& color) override;
    void stroke(const PathBatch& paths, const LineStyle& style) override;
    Vec2 measureText(const char* utf8, const TextStyle& style) override;
    void drawText(Vec2 center, const char* utf8, const TextStyle& style) override;
    void endFrame() override;

    const std::string& svg() const { return _svg; }
    bool save(const char* path) const;

private:
    void appendPath(const PathBatch& b);

    std::string _svg;
    std::string _font;
};

} // namespace MapTile

#endif // MAPTILE_SVG_CANVAS_H
