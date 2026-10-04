#include "private.h"
#include <errno.h>
#include <string.h>

/*
 * Fonts: what measuring text needs of a TrueType / OpenType file - the
 * Unicode map (cmap format 4 or 12), the advances (hmtx), the vertical
 * metrics (hhea) - read once per file and shared by its sizes.
 *
 * The numbers are the ones todmvf puts into the .dmvf it makes of the file
 * (stb_truetype's): the scale is the pixel size per unit of the em, the
 * ascent and descent are rounded up, every advance (with the tracking) is
 * rounded - computed in float as there, so they come out the same.
 */

#define TAG(a, b, c, d)     (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define SUPERSAMPLE         8               /* todmvf's LIBTODMVF_SUPERSAMPLE: its scale is computed through it */
#define MAX_TABLES          64u
#define MAX_TABLE_SIZE      (4u << 20)

/* The built-in font: Roboto's proportions (sans-N of dmview), estimated */
#define BUILTIN_ADVANCE     56              /* % of the size */
#define BUILTIN_ASCENT      93
#define BUILTIN_DESCENT     25

static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int32_t ifloor(double x)
{
    int32_t i = (int32_t)x;
    return ((double)i > x) ? i - 1 : i;
}

static int32_t iceil(double x)
{
    int32_t i = (int32_t)x;
    return ((double)i < x) ? i + 1 : i;
}

/* ---- Reading the file ---- */

static bool read_at(void* f, uint32_t offset, void* buffer, size_t size)
{
    return Dmod_FileSeek(f, (Dmod_FileOffset_t)offset, DMOD_SEEK_SET) == 0 && Dmod_FileRead(buffer, 1, size, f) == size;
}

/* A table of the font at `font` (an offset in the file) into memory */
static uint8_t* read_table(void* f, uint32_t font, uint32_t tag, uint32_t* size, int* status)
{
    uint8_t header[12];
    if (!read_at(f, font, header, sizeof(header)))
    {
        *status = -EBADMSG;
        return NULL;
    }
    uint32_t tables = rd16(header + 4);
    for (uint32_t i = 0; i < tables && i < MAX_TABLES; i++)
    {
        uint8_t record[16];
        if (!read_at(f, font + 12U + i * 16U, record, sizeof(record)))
            break;
        if (rd32(record) != tag)
            continue;
        uint32_t offset = rd32(record + 8), length = rd32(record + 12);
        if (length == 0 || length > MAX_TABLE_SIZE)
            break;
        uint8_t* table = Dmod_Malloc(length);
        if (table == NULL)
        {
            *status = -ENOMEM;
            return NULL;
        }
        if (!read_at(f, offset, table, length))
        {
            Dmod_Free(table);
            break;
        }
        *size = length;
        return table;
    }
    *status = -EBADMSG;
    return NULL;
}

/* The Unicode subtable of a cmap, moved to the start of its own buffer */
static bool pick_cmap(face_t* face, uint8_t* cmap, uint32_t size)
{
    uint32_t best = 0, best_rank = 0;
    if (size < 4)
        return false;
    uint32_t count = rd16(cmap + 2);
    for (uint32_t i = 0; i < count && 4U + i * 8U + 8U <= size; i++)
    {
        const uint8_t* r = cmap + 4 + i * 8;
        uint16_t platform = rd16(r), encoding = rd16(r + 2);
        uint32_t offset = rd32(r + 4);
        if (offset + 4U > size)
            continue;
        uint16_t format = rd16(cmap + offset);
        uint32_t rank = 0;
        if (format == 12 && (platform == 0 || (platform == 3 && encoding == 10)))
            rank = 3;
        else if (format == 4 && (platform == 0 || (platform == 3 && encoding == 1)))
            rank = 2;
        else if (format == 4 && platform == 3 && encoding == 0)     /* Symbol */
            rank = 1;
        if (rank > best_rank)
        {
            best_rank = rank;
            best = offset;
        }
    }
    if (best_rank == 0)
        return false;
    uint16_t format = rd16(cmap + best);
    uint32_t length = (format == 12) ? ((best + 16U <= size) ? rd32(cmap + best + 4) : 0) : ((best + 4U <= size) ? rd16(cmap + best + 2) : 0);
    if (length < 16U || best + length > size)
        length = size - best;          /* Some fonts get the length of format 4 wrong: up to the end */
    memmove(cmap, cmap + best, length);
    face->cmap = cmap;
    face->cmap_size = length;
    face->cmap_format = format;
    return true;
}

static void face_free(face_t* face)
{
    Dmod_Free(face->hmtx);
    Dmod_Free(face->cmap);
    Dmod_Free(face->path);
    Dmod_Free(face);
}

static int face_load(face_t* face)
{
    int status = 0;
    void* f = Dmod_FileOpen(face->path, "rb");
    if (f == NULL)
        return -ENOENT;

    uint8_t tag[12];
    uint32_t font = 0;
    if (!read_at(f, 0, tag, sizeof(tag)))
        status = -EBADMSG;
    else if (rd32(tag) == TAG('t', 't', 'c', 'f'))
    {
        uint8_t first[4];                       /* A collection: its first font */
        font = read_at(f, 12, first, sizeof(first)) ? rd32(first) : 0;
    }

    uint32_t size = 0;
    uint8_t* head = (status == 0) ? read_table(f, font, TAG('h', 'e', 'a', 'd'), &size, &status) : NULL;
    if (head != NULL && size >= 54)
        face->units_per_em = rd16(head + 18);
    Dmod_Free(head);

    uint8_t* hhea = (status == 0) ? read_table(f, font, TAG('h', 'h', 'e', 'a'), &size, &status) : NULL;
    if (hhea != NULL && size >= 36)
    {
        face->ascender = (int16_t)rd16(hhea + 4);
        face->descender = (int16_t)rd16(hhea + 6);
        face->hmetrics = rd16(hhea + 34);
    }
    Dmod_Free(hhea);

    if (status == 0)
        face->hmtx = read_table(f, font, TAG('h', 'm', 't', 'x'), &face->hmtx_size, &status);
    uint8_t* cmap = (status == 0) ? read_table(f, font, TAG('c', 'm', 'a', 'p'), &size, &status) : NULL;
    if (cmap != NULL && !pick_cmap(face, cmap, size))
    {
        Dmod_Free(cmap);
        status = -EBADMSG;
    }
    Dmod_FileClose(f);

    if (status == 0 && (face->units_per_em == 0 || face->hmetrics == 0 || face->hmtx_size < 4U * face->hmetrics))
        status = -EBADMSG;
    return status;
}

static face_t* face_get(dmvsi_doc_t doc, const char* path, int* status)
{
    for (face_t* face = doc->faces; face != NULL; face = face->next)
    {
        if (strcmp(face->path, path) == 0)
            return face;
    }
    face_t* face = Dmod_Malloc(sizeof(*face));
    if (face == NULL)
    {
        *status = -ENOMEM;
        return NULL;
    }
    memset(face, 0, sizeof(*face));
    if ((face->path = copy_string(path, strlen(path))) == NULL)
        *status = -ENOMEM;
    else
        *status = face_load(face);
    if (*status != 0)
    {
        face_free(face);
        return NULL;
    }
    face->next = doc->faces;
    doc->faces = face;
    return face;
}

/* ---- Glyphs ---- */

static uint32_t glyph_format4(const face_t* face, uint32_t c)
{
    const uint8_t* t = face->cmap;
    if (c > 0xFFFFu || face->cmap_size < 14)
        return 0;
    uint32_t segments = rd16(t + 6) / 2U;
    if (16U + segments * 8U > face->cmap_size)
        return 0;
    const uint8_t* ends = t + 14;
    const uint8_t* starts = ends + segments * 2U + 2U;
    const uint8_t* deltas = starts + segments * 2U;
    const uint8_t* ranges = deltas + segments * 2U;

    uint32_t lo = 0, hi = segments;
    while (lo < hi)                             /* The first segment ending at or after c */
    {
        uint32_t mid = (lo + hi) / 2U;
        if (rd16(ends + mid * 2U) < c)
            lo = mid + 1U;
        else
            hi = mid;
    }
    if (lo >= segments || rd16(starts + lo * 2U) > c)
        return 0;
    uint16_t delta = rd16(deltas + lo * 2U), range = rd16(ranges + lo * 2U);
    if (range == 0)
        return (uint16_t)(c + delta);
    uint32_t at = (uint32_t)(ranges + lo * 2U - t) + range + (c - rd16(starts + lo * 2U)) * 2U;
    if (at + 2U > face->cmap_size)
        return 0;
    uint16_t g = rd16(t + at);
    return (g == 0) ? 0 : (uint16_t)(g + delta);
}

static uint32_t glyph_format12(const face_t* face, uint32_t c)
{
    const uint8_t* t = face->cmap;
    if (face->cmap_size < 16)
        return 0;
    uint32_t groups = rd32(t + 12);
    if (16U + groups * 12U > face->cmap_size)
        return 0;
    uint32_t lo = 0, hi = groups;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2U;
        const uint8_t* g = t + 16 + mid * 12U;
        if (rd32(g + 4) < c)
            lo = mid + 1U;
        else
            hi = mid;
    }
    if (lo >= groups)
        return 0;
    const uint8_t* g = t + 16 + lo * 12U;
    return (rd32(g) <= c) ? rd32(g + 8) + (c - rd32(g)) : 0;
}

static uint32_t glyph_of(const face_t* face, uint32_t c)
{
    return (face->cmap_format == 12) ? glyph_format12(face, c) : glyph_format4(face, c);
}

static uint32_t advance_units(const face_t* face, uint32_t g)
{
    uint32_t i = (g < face->hmetrics) ? g : face->hmetrics - 1U;
    return rd16(face->hmtx + i * 4U);
}

/* The advance todmvf gives the glyph of `c`, 0 when the font has none */
static int32_t advance_of(dmvsi_font_t font, uint32_t c)
{
    if (font->face == NULL)
        return (c >= 0x20u && c < 0x7Fu) ? (int32_t)((font->size * BUILTIN_ADVANCE + 50U) / 100U) : 0;
    uint32_t g = glyph_of(font->face, c);
    if (g == 0)
        return 0;
    float big = font->scale * (float)SUPERSAMPLE;
    int32_t advance = ifloor((double)advance_units(font->face, g) * big / SUPERSAMPLE + (double)font->tracking / 100.0 + 0.5);
    return (advance < 0) ? 0 : advance;
}

/* ---- Fonts ---- */

dmvsi_font_t font_get(dmvsi_doc_t doc, const char* file, uint16_t size, int32_t tracking, int* status)
{
    *status = 0;
    dmvsi_font_t last = NULL;
    for (dmvsi_font_t f = doc->fonts; f != NULL; last = f, f = f->next)
    {
        bool same_file = (file == NULL) ? f->face == NULL : (f->face != NULL && strcmp(f->face->path, file) == 0);
        if (same_file && f->size == size && f->tracking == tracking)
            return f;
    }

    face_t* face = NULL;
    if (file != NULL && (face = face_get(doc, file, status)) == NULL)
        return NULL;
    dmvsi_font_t font = Dmod_Malloc(sizeof(*font));
    if (font == NULL)
    {
        *status = -ENOMEM;
        return NULL;
    }
    memset(font, 0, sizeof(*font));
    font->face = face;
    font->size = size;
    font->tracking = tracking;
    if (face != NULL)
    {
        font->scale = (float)size / (float)face->units_per_em;
        font->ascent = iceil((double)face->ascender * font->scale);
        font->descent = iceil(-(double)face->descender * font->scale);
    }
    else
    {
        font->ascent = (int32_t)((size * BUILTIN_ASCENT + 99U) / 100U);
        font->descent = (int32_t)((size * BUILTIN_DESCENT + 99U) / 100U);
    }
    if (last == NULL)
        doc->fonts = font;
    else
        last->next = font;             /* In the order they were made */
    return font;
}

int font_info(dmvsi_font_t font, dmvsi_font_info_t* info)
{
    info->ascent = font->ascent;
    info->descent = font->descent;
    info->size = font->size;
    info->tracking = font->tracking;
    info->file = (font->face != NULL) ? font->face->path : NULL;
    return 0;
}

bool font_has(dmvsi_font_t font, uint32_t c)
{
    return (font->face == NULL) ? (c >= 0x20u && c < 0x7Fu) : glyph_of(font->face, c) != 0;
}

uint32_t utf8_next(const char** p, const char* end)
{
    const uint8_t* s = (const uint8_t*)*p;
    const uint8_t* e = (const uint8_t*)end;
    uint32_t c = *s++;
    uint32_t extra = 0;
    if (c >= 0xF0u && c < 0xF8u)
    {
        c &= 0x07u;
        extra = 3;
    }
    else if (c >= 0xE0u)
    {
        c &= 0x0Fu;
        extra = 2;
    }
    else if (c >= 0xC0u)
    {
        c &= 0x1Fu;
        extra = 1;
    }
    else if (c >= 0x80u)
    {
        *p = (const char*)s;
        return 0xFFFDu;
    }
    for (; extra > 0; extra--)
    {
        if (s >= e || (*s & 0xC0u) != 0x80u)
        {
            *p = (const char*)s;
            return 0xFFFDu;
        }
        c = (c << 6) | (*s++ & 0x3Fu);
    }
    *p = (const char*)s;
    return c;
}

int32_t text_width(dmvsi_font_t font, const char* text, size_t length)
{
    int32_t width = 0;
    const char* end = text + length;
    for (const char* p = text; p < end; )
        width += advance_of(font, utf8_next(&p, end));
    return width;
}

/* `c` into the font's increasing set of characters */
static int add_char(dmvsi_font_t font, uint32_t c)
{
    uint32_t lo = 0, hi = font->char_count;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2U;
        if (font->chars[mid] < c)
            lo = mid + 1U;
        else
            hi = mid;
    }
    if (lo < font->char_count && font->chars[lo] == c)
        return 0;
    if (font->char_count == font->char_capacity)
    {
        uint32_t capacity = (font->char_capacity == 0) ? 64U : font->char_capacity * 2U;
        uint32_t* chars = Dmod_Malloc(capacity * sizeof(uint32_t));
        if (chars == NULL)
            return -ENOMEM;
        if (font->char_count != 0)
            memcpy(chars, font->chars, font->char_count * sizeof(uint32_t));
        Dmod_Free(font->chars);
        font->chars = chars;
        font->char_capacity = capacity;
    }
    memmove(font->chars + lo + 1, font->chars + lo, (font->char_count - lo) * sizeof(uint32_t));
    font->chars[lo] = c;
    font->char_count++;
    return 0;
}

int font_add_chars(dmvsi_font_t font, const char* text, size_t length)
{
    const char* end = text + length;
    for (const char* p = text; p < end; )
    {
        int status = add_char(font, utf8_next(&p, end));
        if (status != 0)
            return status;
    }
    return 0;
}

bool font_char(dmvsi_font_t font, uint32_t index, uint32_t* c)
{
    if (index >= font->char_count)
        return false;
    *c = font->chars[index];
    return true;
}

void fonts_free(dmvsi_doc_t doc)
{
    while (doc->fonts != NULL)
    {
        dmvsi_font_t f = doc->fonts;
        doc->fonts = f->next;
        Dmod_Free(f->chars);
        Dmod_Free(f);
    }
    while (doc->faces != NULL)
    {
        face_t* face = doc->faces;
        doc->faces = face->next;
        face_free(face);
    }
}
