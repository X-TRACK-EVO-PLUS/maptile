/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "json.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace MapTile {

namespace {

    struct Parser {
        const char* p;
        const char* end;
        std::string err;
        int depth = 0;

        bool fail(const char* what)
        {
            if (err.empty()) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "%s at byte offset %d", what, (int)(p - start));
                err = buf;
            }
            return false;
        }
        const char* start = nullptr;

        void ws()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                p++;
        }
        bool lit(const char* word)
        {
            size_t n = std::strlen(word);
            if ((size_t)(end - p) >= n && std::memcmp(p, word, n) == 0) {
                p += n;
                return true;
            }
            return false;
        }
        static void utf8(std::string& out, unsigned cp)
        {
            if (cp < 0x80) {
                out += (char)cp;
            } else if (cp < 0x800) {
                out += (char)(0xc0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3f));
            } else if (cp < 0x10000) {
                out += (char)(0xe0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3f));
                out += (char)(0x80 | (cp & 0x3f));
            } else {
                out += (char)(0xf0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3f));
                out += (char)(0x80 | ((cp >> 6) & 0x3f));
                out += (char)(0x80 | (cp & 0x3f));
            }
        }
        bool hex4(unsigned& v)
        {
            if (end - p < 4)
                return false;
            v = 0;
            for (int i = 0; i < 4; i++) {
                char c = *p++;
                v <<= 4;
                if (c >= '0' && c <= '9')
                    v |= c - '0';
                else if (c >= 'a' && c <= 'f')
                    v |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    v |= c - 'A' + 10;
                else
                    return false;
            }
            return true;
        }
        bool string(std::string& out)
        {
            if (p >= end || *p != '"')
                return fail("expected string");
            p++;
            while (p < end && *p != '"') {
                if (*p != '\\') {
                    out += *p++;
                    continue;
                }
                p++;
                if (p >= end)
                    break;
                char c = *p++;
                switch (c) {
                case 'n':
                    out += '\n';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp))
                        return fail("bad \\u escape");
                    if (cp >= 0xd800 && cp < 0xdc00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned lo;
                        if (!hex4(lo))
                            return fail("bad \\u escape");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                    }
                    utf8(out, cp);
                    break;
                }
                default:
                    out += c; /* " \ / */
                }
            }
            if (p >= end)
                return fail("unterminated string");
            p++;
            return true;
        }
        bool value(Json& v)
        {
            ws();
            if (p >= end)
                return fail("unexpected end");
            if (++depth > 64)
                return fail("nesting too deep");
            bool ok = false;
            char c = *p;
            if (c == '{') {
                p++;
                v.type = Json::OBJ;
                ws();
                if (p < end && *p == '}') {
                    p++;
                    ok = true;
                } else {
                    for (;;) {
                        ws();
                        std::string key;
                        if (!string(key))
                            break;
                        ws();
                        if (p >= end || *p++ != ':') {
                            fail("expected ':'");
                            break;
                        }
                        v.o.emplace_back(std::move(key), Json());
                        if (!value(v.o.back().second))
                            break;
                        ws();
                        if (p < end && *p == ',') {
                            p++;
                            continue;
                        }
                        if (p < end && *p == '}') {
                            p++;
                            ok = true;
                        } else {
                            fail("expected ',' or '}'");
                        }
                        break;
                    }
                }
            } else if (c == '[') {
                p++;
                v.type = Json::ARR;
                ws();
                if (p < end && *p == ']') {
                    p++;
                    ok = true;
                } else {
                    for (;;) {
                        v.a.emplace_back();
                        if (!value(v.a.back()))
                            break;
                        ws();
                        if (p < end && *p == ',') {
                            p++;
                            continue;
                        }
                        if (p < end && *p == ']') {
                            p++;
                            ok = true;
                        } else {
                            fail("expected ',' or ']'");
                        }
                        break;
                    }
                }
            } else if (c == '"') {
                v.type = Json::STR;
                ok = string(v.s);
            } else if (lit("true")) {
                v.type = Json::BOOL;
                v.b = true;
                ok = true;
            } else if (lit("false")) {
                v.type = Json::BOOL;
                ok = true;
            } else if (lit("null")) {
                ok = true;
            } else {
                char* e = nullptr;
                v.n = std::strtod(p, &e);
                if (e && e != p) {
                    v.type = Json::NUM;
                    p = e;
                    ok = true;
                } else {
                    fail("unexpected character");
                }
            }
            depth--;
            return ok;
        }
    };

} // namespace

bool parseJson(const char* text, size_t size, Json& out, std::string& error)
{
    Parser ps;
    ps.p = ps.start = text;
    ps.end = text + size;
    if (!ps.value(out)) {
        error = ps.err;
        return false;
    }
    ps.ws();
    if (ps.p != ps.end) {
        error = "trailing data";
        return false;
    }
    return true;
}

} // namespace MapTile
