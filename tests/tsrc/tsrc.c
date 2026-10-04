#define DMOD_ENABLE_REGISTRATION    ON
#include "dmvsi.h"
#include <errno.h>
#include <string.h>

/*
 * "TSRC" files for the tests - one command per line, numbers in pixels:
 *
 *   TSRC name width height
 *   fill x y w h radius AARRGGBB
 *   group x y w h clip opacity          (clip 0: the group is as large as what is in it)
 *   end
 *   text x baseline size font text...    (the rest of the line)
 */

#define MAX_FILE    2048u

static const char* skip_spaces(const char* p)
{
    while (*p == ' ')
        p++;
    return p;
}

static const char* word(const char* p, char* out, size_t size)
{
    size_t n = 0;
    p = skip_spaces(p);
    while (*p != ' ' && *p != '\n' && *p != '\0')
    {
        if (n + 1U < size)
            out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    return p;
}

static const char* number(const char* p, int32_t* v)
{
    char w[16];
    p = word(p, w, sizeof(w));
    int32_t n = 0;
    for (const char* q = w; *q != '\0'; q++)
        n = n * 10 + (*q - '0');
    *v = n;
    return p;
}

static const char* hex(const char* p, uint32_t* v)
{
    char w[16];
    p = word(p, w, sizeof(w));
    uint32_t n = 0;
    for (const char* q = w; *q != '\0'; q++)
        n = (n << 4) | (uint32_t)((*q <= '9') ? *q - '0' : *q - 'A' + 10);
    *v = n;
    return p;
}

static int line(dmvsi_doc_t doc, const char* p)
{
    char cmd[16];
    int32_t x, y, w, h, r;
    p = word(p, cmd, sizeof(cmd));
    if (strcmp(cmd, "fill") == 0)
    {
        dmvsi_fill_t f;
        memset(&f, 0, sizeof(f));
        p = number(number(number(number(number(p, &x), &y), &w), &h), &r);
        (void)hex(p, &f.paint.color);
        f.rect.x = DMVSI_PX(x);
        f.rect.y = DMVSI_PX(y);
        f.rect.w = DMVSI_PX(w);
        f.rect.h = DMVSI_PX(h);
        f.radius = DMVSI_PX(r);
        return dmvsi_add_fill(doc, &f);
    }
    if (strcmp(cmd, "group") == 0)
    {
        dmvsi_group_t g;
        int32_t clip, opacity;
        memset(&g, 0, sizeof(g));
        (void)number(number(number(number(number(number(p, &x), &y), &w), &h), &clip), &opacity);
        g.rect.x = DMVSI_PX(x);
        g.rect.y = DMVSI_PX(y);
        g.rect.w = DMVSI_PX(w);
        g.rect.h = DMVSI_PX(h);
        g.flags = (clip != 0) ? DMVSI_GROUP_CLIP : 0;
        g.opacity = (uint8_t)opacity;
        return dmvsi_begin_group(doc, &g);
    }
    if (strcmp(cmd, "end") == 0)
        return dmvsi_end_group(doc);
    if (strcmp(cmd, "text") == 0)
    {
        char font_path[256];
        int32_t size;
        int status = 0;
        p = word(number(number(number(p, &x), &y), &size), font_path, sizeof(font_path));
        p = skip_spaces(p);
        const char* end = strchr(p, '\n');
        dmvsi_text_t t;
        memset(&t, 0, sizeof(t));
        t.font = dmvsi_font(doc, font_path, (uint16_t)size, 0, &status);
        if (t.font == NULL)
            return status;
        t.x = DMVSI_PX(x);
        t.baseline = DMVSI_PX(y);
        t.text = p;
        t.length = (end != NULL) ? (size_t)(end - p) : strlen(p);
        t.paint.color = 0xFFFFFFFFu;
        return dmvsi_add_text(doc, &t);
    }
    return -EBADMSG;
}

dmod_dmvsi_dif_api_declaration(1.0, dmvs_tsrc, bool, _probe, ( const char* path, const uint8_t* head, size_t size ))
{
    (void)path;
    return size >= 4 && memcmp(head, "TSRC", 4) == 0;
}

dmod_dmvsi_dif_api_declaration(1.0, dmvs_tsrc, int, _convert, ( const char* path, const dmvsi_options_t* options, dmvsi_doc_t doc ))
{
    static char text[MAX_FILE];
    void* f = Dmod_FileOpen(path, "rb");
    if (f == NULL)
        return -ENOENT;
    size_t size = Dmod_FileRead(text, 1, sizeof(text) - 1U, f);
    Dmod_FileClose(f);
    text[size] = '\0';

    char name[32];
    int32_t w, h;
    const char* p = word(text, name, sizeof(name));
    if (strcmp(name, "TSRC") != 0)
        return -EBADMSG;
    p = number(number(word(p, name, sizeof(name)), &w), &h);
    int status = dmvsi_set_view(doc, (options->name != NULL) ? options->name : name, (uint16_t)w, (uint16_t)h);
    while (status == 0 && (p = strchr(p, '\n')) != NULL && *++p != '\0')
        status = line(doc, p);
    return status;
}

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
