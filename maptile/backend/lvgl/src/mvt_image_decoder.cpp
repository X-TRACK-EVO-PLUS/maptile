/*
 * MIT License
 * Copyright (c) 2024 - 2026 _VIFEXTech
 */
#include "maptile_lvgl/mvt_image_decoder.h"
#include "maptile/def_style.h"
#include "maptile/json_style.h"
#include "maptile/style.h"
#include "maptile/tile_parser.h"
#include "maptile/tile_renderer.h"
#include "maptile_plutovg/plutovg_gfx.h"
#include "maptile_plutovg/plutovg_text_gfx.h"

/* Need private header for lv_image_decoder_dsc_t struct definition */
#include "lvgl/src/draw/lv_image_decoder_private.h"
#include "lvgl/src/misc/cache/instance/lv_image_cache.h"

#include <cstdlib>
#include <cstring>
#include <vector>

using namespace MapTile;

static struct {
    lv_image_decoder_t* decoder;
    StyleBase* style;
    lv_font_t* font;
    plutovg_font_face_t* pvgFace;
    float fontSize;
    int tileSize;
} s_ctx;

static bool is_mvt_path(const void* src)
{
    if (!src)
        return false;
    const char* path = (const char*)src;
    const char* dot = strrchr(path, '.');
    return dot && strcmp(dot, ".mvt") == 0;
}

static bool parse_tile_coord(const char* path, int* z, int* x, int* y)
{
    const char* dot = strrchr(path, '.');
    if (!dot)
        return false;

    const char* p = dot;
    while (p > path && *(p - 1) != '/')
        p--;
    if (p <= path)
        return false;
    *y = atoi(p);

    p -= 2;
    while (p > path && *p != '/')
        p--;
    if (p <= path)
        return false;
    *x = atoi(p + 1);

    p--;
    while (p > path && *p != '/')
        p--;
    *z = atoi(p + 1);

    return true;
}

static bool read_file_lv_fs(const char* path, std::vector<uint8_t>& data)
{
    lv_fs_file_t f;
    lv_fs_res_t res = lv_fs_open(&f, path, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        LV_LOG_WARN("mvt: lv_fs_open failed: %d, path: %s", res, path);
        return false;
    }

    res = lv_fs_seek(&f, 0, LV_FS_SEEK_END);
    if (res != LV_FS_RES_OK) {
        LV_LOG_WARN("mvt: lv_fs_seek(END) failed: %d", res);
        lv_fs_close(&f);
        return false;
    }

    uint32_t size = 0;
    res = lv_fs_tell(&f, &size);
    if (res != LV_FS_RES_OK || size == 0) {
        LV_LOG_WARN("mvt: lv_fs_tell failed: %d, size: %u", res, size);
        lv_fs_close(&f);
        return false;
    }

    res = lv_fs_seek(&f, 0, LV_FS_SEEK_SET);
    if (res != LV_FS_RES_OK) {
        LV_LOG_WARN("mvt: lv_fs_seek(SET) failed: %d", res);
        lv_fs_close(&f);
        return false;
    }

    data.resize(size);
    uint32_t br = 0;
    res = lv_fs_read(&f, data.data(), size, &br);
    lv_fs_close(&f);

    if (res != LV_FS_RES_OK || br != size) {
        LV_LOG_WARN("mvt: lv_fs_read failed: %d, read %u/%u", res, br, size);
        return false;
    }

    return true;
}

static lv_result_t decoder_info(lv_image_decoder_t* decoder,
    lv_image_decoder_dsc_t* dsc, lv_image_header_t* header)
{
    (void)decoder;
    if (dsc->src_type != LV_IMAGE_SRC_FILE)
        return LV_RESULT_INVALID;
    if (!is_mvt_path(dsc->src))
        return LV_RESULT_INVALID;

    header->w = s_ctx.tileSize;
    header->h = s_ctx.tileSize;
    header->cf = LV_COLOR_FORMAT_ARGB8888;
    header->stride = s_ctx.tileSize * 4;
    return LV_RESULT_OK;
}

static lv_result_t decoder_open(lv_image_decoder_t* decoder,
    lv_image_decoder_dsc_t* dsc)
{
    (void)decoder;
    if (dsc->src_type != LV_IMAGE_SRC_FILE)
        return LV_RESULT_INVALID;
    if (!is_mvt_path(dsc->src))
        return LV_RESULT_INVALID;

    const char* path = (const char*)dsc->src;
    int z, x, y;
    if (!parse_tile_coord(path, &z, &x, &y))
        return LV_RESULT_INVALID;

    std::vector<uint8_t> fileData;
    if (!read_file_lv_fs(path, fileData))
        return LV_RESULT_INVALID;

    vector_tile::Tile tile;
    if (!tile.ParseFromArray(fileData.data(), (int)fileData.size()))
        return LV_RESULT_INVALID;

    const char** layerSeq = DefStyle::layerSequence();
    const char** labelSeq = DefStyle::labelSequence();
    auto* jsonStyle = dynamic_cast<JsonStyle*>(s_ctx.style);
    if (jsonStyle) {
        layerSeq = jsonStyle->layerSequence();
        labelSeq = jsonStyle->labelSequence();
    }

    auto parsed = TileParser::parse(tile, *s_ctx.style, (float)z, layerSeq, labelSeq);

    int ts = s_ctx.tileSize;
    lv_draw_buf_t* buf = lv_draw_buf_create(ts, ts, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    if (!buf)
        return LV_RESULT_INVALID;

    lv_draw_buf_clear(buf, NULL);

    /* Render using PlutoVG directly into the buffer - no LVGL draw pipeline involved */
    plutovg_surface_t* surface = plutovg_surface_create_for_data(
        buf->data, ts, ts, ts * 4);
    if (!surface) {
        lv_draw_buf_destroy(buf);
        return LV_RESULT_INVALID;
    }

    plutovg_canvas_t* pvgCanvas = plutovg_canvas_create(surface);
    if (!pvgCanvas) {
        plutovg_surface_destroy(surface);
        lv_draw_buf_destroy(buf);
        return LV_RESULT_INVALID;
    }

    PlutoVGGfx gfx(pvgCanvas);
    PlutoVGTextGfx textGfx(pvgCanvas, s_ctx.pvgFace, s_ctx.fontSize);
    TileRenderer renderer(gfx, textGfx, *s_ctx.style, (float)z);
    renderer.setTargetSize(ts);
    renderer.render(parsed);

    plutovg_canvas_destroy(pvgCanvas);
    plutovg_surface_destroy(surface);

    /* PlutoVG renders in BGRA, LVGL expects ARGB - swap R and B channels */
    uint8_t* px = buf->data;
    uint32_t total = ts * ts;
    for (uint32_t i = 0; i < total; i++) {
        uint8_t tmp = px[0];
        px[0] = px[2];
        px[2] = tmp;
        px += 4;
    }

    dsc->decoded = buf;

    /* Add to LVGL image cache so subsequent requests hit cache */
    if (lv_image_cache_is_enabled()) {
        lv_image_cache_data_t search_key;
        search_key.src_type = dsc->src_type;
        search_key.src = dsc->src;
        search_key.slot.size = buf->data_size;

        lv_cache_entry_t* entry = lv_image_decoder_add_to_cache(decoder, &search_key, buf, NULL);
        if (entry) {
            dsc->cache_entry = entry;
        }
    }

    return LV_RESULT_OK;
}

static void decoder_close(lv_image_decoder_t* decoder,
    lv_image_decoder_dsc_t* dsc)
{
    (void)decoder;

    /* If cached, the cache owns the buffer and frees it on eviction.
     * Only free here if not cached. */
    if (!dsc->cache_entry && dsc->decoded) {
        lv_draw_buf_destroy((lv_draw_buf_t*)dsc->decoded);
        dsc->decoded = NULL;
    }
}

void MvtImageDecoder::install(StyleBase& style, lv_font_t* font, int tileSize)
{
    s_ctx.style = &style;
    s_ctx.font = font;
    s_ctx.pvgFace = nullptr;
    s_ctx.fontSize = 12.0f;
    s_ctx.tileSize = tileSize;

    lv_image_decoder_t* dec = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(dec, decoder_info);
    lv_image_decoder_set_open_cb(dec, decoder_open);
    lv_image_decoder_set_close_cb(dec, decoder_close);
    s_ctx.decoder = dec;
}

void MvtImageDecoder::setStyle(StyleBase& style)
{
    s_ctx.style = &style;
    lv_image_cache_drop(NULL);
}

void MvtImageDecoder::setFont(lv_font_t* font)
{
    s_ctx.font = font;
    lv_image_cache_drop(NULL);
}

void MvtImageDecoder::setPlutoVGFont(plutovg_font_face_t* face, float fontSize)
{
    s_ctx.pvgFace = face;
    s_ctx.fontSize = fontSize;
    lv_image_cache_drop(NULL);
}

void MvtImageDecoder::uninstall()
{
    if (s_ctx.decoder) {
        lv_image_decoder_delete(s_ctx.decoder);
        s_ctx.decoder = nullptr;
    }
}
