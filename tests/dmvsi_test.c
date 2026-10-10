#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmvsi.h"
#include <errno.h>
#include <string.h>

/*
 * dmvsi with the test converter dmvs_tsrc (tsrc/): "TSRC" files, a line per
 * shape. dmvsi_convert_file() loads the converter by the extension the
 * first time. The fonts are measured against what todmvf makes of them:
 * the widths below are the sums of the advances in its .dmvf files
 * (todmvf Inter-Regular.otf 16; ... 11 -t -0.55).
 */

#ifndef DMVSI_TEST_DIR
#define DMVSI_TEST_DIR      "."
#endif
#ifndef DMVSI_TEST_FONTS
#define DMVSI_TEST_FONTS    "fonts"
#endif
#define TEST_FILE(name)     DMVSI_TEST_DIR "/" name
#define INTER               DMVSI_TEST_FONTS "/Inter-Regular.otf"

static bool write_file(const char* path, const char* text)
{
    void* f = Dmod_FileOpen(path, "wb");
    if (f == NULL)
        return false;
    bool ok = Dmod_FileWrite(text, 1, strlen(text), f) == strlen(text);
    Dmod_FileClose(f);
    return ok;
}

static bool rect_is(const dmvsi_rect_t* r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return r->x == DMVSI_PX(x) && r->y == DMVSI_PX(y) && r->w == DMVSI_PX(w) && r->h == DMVSI_PX(h);
}

DMOD_TEST_STEP(dmvsi_converts_a_file_with_the_converter_of_its_extension)
{
    int status = -1;
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("a.TSRC"),
        "TSRC demo 272 480\n"
        "fill 0 0 272 480 0 FF2B5876\n"
        "group 10 20 100 50 0 128\n"
        "fill 16 24 20 20 4 80FFFFFF\n"
        "fill 40 60 30 8 0 FF000000\n"
        "end\n"
        "text 8 30 16 " INTER " Hello, dmod!\n"));

    dmvsi_doc_t doc = dmvsi_convert_file(TEST_FILE("a.TSRC"), NULL, &status);
    DMOD_TEST_EXPECT_TRUE(doc != NULL);
    if (doc == NULL)
        return;
    DMOD_TEST_EXPECT_EQ(status, 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(dmvsi_converter_name(doc), "dmvs_tsrc") == 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(dmvsi_view_name(doc), "demo") == 0);
    uint16_t w = 0, h = 0;
    DMOD_TEST_EXPECT_EQ(dmvsi_view_size(doc, &w, &h), 0);
    DMOD_TEST_EXPECT_EQ(w, 272);
    DMOD_TEST_EXPECT_EQ(h, 480);

    const dmvsi_node_t* root = dmvsi_root(doc);
    DMOD_TEST_EXPECT_TRUE(root != NULL && root->kind == DMVSI_NODE_GROUP);
    DMOD_TEST_EXPECT_TRUE(rect_is(&root->u.group.rect, 0, 0, 272, 480));
    const dmvsi_node_t* fill = root->first;
    DMOD_TEST_EXPECT_TRUE(fill != NULL && fill->kind == DMVSI_NODE_RECT);
    DMOD_TEST_EXPECT_EQ(fill->u.fill.paint.color, 0xFF2B5876u);

    /* A group without a clip is as large as what is in it and the rectangle it was given */
    const dmvsi_node_t* group = fill->next;
    DMOD_TEST_EXPECT_TRUE(group != NULL && group->kind == DMVSI_NODE_GROUP);
    DMOD_TEST_EXPECT_EQ(group->u.group.opacity, 128);
    DMOD_TEST_EXPECT_TRUE(rect_is(&group->u.group.rect, 10, 20, 100, 50));
    DMOD_TEST_EXPECT_TRUE(group->first != NULL && group->first->u.fill.radius == DMVSI_PX(4));

    /* The text, measured: Inter's ascent at 16 px is 16, its descent 4 */
    const dmvsi_node_t* text = group->next;
    DMOD_TEST_EXPECT_TRUE(text != NULL && text->kind == DMVSI_NODE_TEXT);
    DMOD_TEST_EXPECT_TRUE(strcmp(text->u.text.text, "Hello, dmod!") == 0);
    DMOD_TEST_EXPECT_TRUE(rect_is(&text->bounds, 8, 14, 98, 20));
    DMOD_TEST_EXPECT_TRUE(text->next == NULL);

    /* The characters of the font, increasing */
    dmvsi_font_t font = dmvsi_font_at(doc, 0);
    uint32_t c = 0;
    DMOD_TEST_EXPECT_TRUE(font != NULL && dmvsi_font_at(doc, 1) == NULL);
    DMOD_TEST_EXPECT_TRUE(dmvsi_font_char(font, 0, &c) && c == ' ');
    DMOD_TEST_EXPECT_TRUE(dmvsi_font_char(font, 1, &c) && c == '!');
    DMOD_TEST_EXPECT_TRUE(dmvsi_font_char(font, 8, &c) && c == 'o');
    DMOD_TEST_EXPECT_FALSE(dmvsi_font_char(font, 9, &c));
    dmvsi_free(doc);
}

DMOD_TEST_STEP(dmvsi_reports_files_nobody_converts)
{
    int status = 0;
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("b.tsrc"), "TSRX demo 10 10\n"));
    DMOD_TEST_EXPECT_TRUE(dmvsi_convert_file(TEST_FILE("b.tsrc"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -ENOTSUP);
    DMOD_TEST_EXPECT_TRUE(dmvsi_convert_file(TEST_FILE("missing.tsrc"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -ENOENT);

    /* A converter's error: a line it does not know */
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("c.tsrc"), "TSRC demo 10 10\nwhat 1 2\n"));
    DMOD_TEST_EXPECT_TRUE(dmvsi_convert_file(TEST_FILE("c.tsrc"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -EBADMSG);
}

DMOD_TEST_STEP(dmvsi_measures_text_as_todmvf_makes_its_fonts)
{
    int status = -1;
    dmvsi_doc_t doc = dmvsi_new();
    dmvsi_font_info_t info;
    DMOD_TEST_EXPECT_TRUE(doc != NULL);

    dmvsi_font_t r16 = dmvsi_font(doc, INTER, 16, 0, &status);
    DMOD_TEST_EXPECT_TRUE(r16 != NULL);
    DMOD_TEST_EXPECT_EQ(status, 0);
    DMOD_TEST_EXPECT_TRUE(dmvsi_font(doc, INTER, 16, 0, NULL) == r16);     /* the same */
    DMOD_TEST_EXPECT_EQ(dmvsi_font_info(r16, &info), 0);
    DMOD_TEST_EXPECT_EQ(info.ascent, 16);
    DMOD_TEST_EXPECT_EQ(info.descent, 4);
    DMOD_TEST_EXPECT_EQ(dmvsi_text_width(r16, "Hello, dmod!", 12), 98);
    const char* polish = "Zażółć gęślą jaźń";
    DMOD_TEST_EXPECT_EQ(dmvsi_text_width(r16, polish, strlen(polish)), 132);
    DMOD_TEST_EXPECT_TRUE(dmvsi_font_has(r16, 0x0119u));      /* ę */
    DMOD_TEST_EXPECT_FALSE(dmvsi_font_has(r16, 0xF1EBu));

    /* Letter spacing, -0.55 px */
    dmvsi_font_t r11 = dmvsi_font(doc, INTER, 11, -55, &status);
    DMOD_TEST_EXPECT_TRUE(r11 != NULL && r11 != r16);
    DMOD_TEST_EXPECT_EQ(dmvsi_font_info(r11, &info), 0);
    DMOD_TEST_EXPECT_EQ(info.ascent, 11);
    DMOD_TEST_EXPECT_EQ(info.descent, 3);
    DMOD_TEST_EXPECT_EQ(dmvsi_text_width(r11, "12:00", 5), 25);
    DMOD_TEST_EXPECT_EQ(dmvsi_text_width(r11, "dmodOS", 6), 42);

    /* Not a font, no file */
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("not-a-font.ttf"), "hello"));
    DMOD_TEST_EXPECT_TRUE(dmvsi_font(doc, TEST_FILE("not-a-font.ttf"), 12, 0, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -EBADMSG);
    DMOD_TEST_EXPECT_TRUE(dmvsi_font(doc, TEST_FILE("missing.ttf"), 12, 0, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -ENOENT);

    /* The built-in font */
    dmvsi_font_t builtin = dmvsi_font(doc, NULL, 16, 0, &status);
    DMOD_TEST_EXPECT_TRUE(builtin != NULL);
    DMOD_TEST_EXPECT_EQ(dmvsi_font_info(builtin, &info), 0);
    DMOD_TEST_EXPECT_TRUE(info.file == NULL && info.ascent > 0 && info.descent > 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_text_width(builtin, "ab", 2), 2 * 9);
    dmvsi_free(doc);
}

DMOD_TEST_STEP(dmvsi_checks_what_is_added)
{
    dmvsi_doc_t doc = dmvsi_new();
    dmvsi_fill_t fill;
    dmvsi_group_t group;
    memset(&fill, 0, sizeof(fill));
    memset(&group, 0, sizeof(group));
    DMOD_TEST_EXPECT_EQ(dmvsi_add_fill(doc, &fill), -EINVAL);             /* no view yet */
    DMOD_TEST_EXPECT_EQ(dmvsi_set_view(doc, "v", 0, 10), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmvsi_set_view(doc, "v", 10, 10), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_set_view(doc, "v", 10, 10), -EINVAL);       /* once */
    DMOD_TEST_EXPECT_EQ(dmvsi_end_group(doc), -EINVAL);                   /* not the root */

    fill.paint.kind = DMVSI_PAINT_LINEAR;
    fill.paint.count = 1;
    DMOD_TEST_EXPECT_EQ(dmvsi_add_fill(doc, &fill), -EINVAL);             /* one stop */
    fill.paint.count = 2;
    fill.paint.stops[0].position = DMVSI_PERCENT(60);
    fill.paint.stops[1].position = DMVSI_PERCENT(40);
    DMOD_TEST_EXPECT_EQ(dmvsi_add_fill(doc, &fill), -EINVAL);             /* decreasing */
    fill.paint.stops[1].position = DMVSI_PERCENT(60);
    DMOD_TEST_EXPECT_EQ(dmvsi_add_fill(doc, &fill), 0);

    group.flags = DMVSI_GROUP_CLIP;
    group.rect.w = DMVSI_PX(5);
    group.rect.h = DMVSI_PX(5);
    group.name = "panel";
    DMOD_TEST_EXPECT_EQ(dmvsi_begin_group(doc, &group), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_end_group(doc), 0);
    const dmvsi_node_t* g = dmvsi_root(doc)->first->next;
    DMOD_TEST_EXPECT_TRUE(g != NULL && strcmp(g->u.group.name, "panel") == 0);
    DMOD_TEST_EXPECT_TRUE(rect_is(&g->bounds, 0, 0, 5, 5));
    dmvsi_free(doc);
    dmvsi_free(NULL);
}

DMOD_TEST_STEP(dmvsi_describes_behaviour)
{
    dmvsi_doc_t doc = dmvsi_new();
    dmvsi_group_t group;
    memset(&group, 0, sizeof(group));
    group.opacity = 255;
    DMOD_TEST_EXPECT_EQ(dmvsi_set_view(doc, "v", 100, 100), 0);
    dmvsi_var_t y = dmvsi_add_var(doc, "window-y", DMVSI_PX(100));
    dmvsi_var_t y2 = dmvsi_add_var(doc, "window-y", 0);
    DMOD_TEST_EXPECT_TRUE(y == 1 && y2 == 2);
    const char* name = NULL;
    int32_t initial = 0;
    DMOD_TEST_EXPECT_TRUE(dmvsi_var_at(doc, 0, &name, &initial) && strcmp(name, "window_y") == 0 && initial == DMVSI_PX(100));
    DMOD_TEST_EXPECT_TRUE(dmvsi_var_at(doc, 1, &name, NULL) && strcmp(name, "window_y_2") == 0);
    DMOD_TEST_EXPECT_FALSE(dmvsi_var_at(doc, 2, &name, NULL));

    DMOD_TEST_EXPECT_EQ(dmvsi_bind(doc, DMVSI_BIND_Y, y), -EINVAL);       /* the root */
    DMOD_TEST_EXPECT_EQ(dmvsi_begin_group(doc, &group), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_bind(doc, DMVSI_BIND_Y, y), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_bind(doc, DMVSI_BIND_X, y), -EINVAL);       /* bound already */

    dmvsi_action_t open[3];
    memset(open, 0, sizeof(open));
    open[0].kind = DMVSI_ACT_IF_EQ;
    open[0].var = y2;
    open[1].kind = DMVSI_ACT_ANIMATE;
    open[1].var = y;
    open[1].duration = 300;
    open[2].kind = DMVSI_ACT_END;
    DMOD_TEST_EXPECT_EQ(dmvsi_add_handler(doc, open, 2), 0);              /* an IF without its END */
    dmvsi_handler_t h = dmvsi_add_handler(doc, open, 3);
    DMOD_TEST_EXPECT_EQ(h, 1);
    DMOD_TEST_EXPECT_EQ(dmvsi_on_click(doc, h), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_show_when(doc, y2, 1), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_show_when(doc, DMVSI_VAR_PRESSED, 0), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_show_when(doc, y2, 2), -EINVAL);           /* two at most */
    DMOD_TEST_EXPECT_EQ(dmvsi_end_group(doc), 0);

    const dmvsi_node_t* g = dmvsi_root(doc)->first;
    DMOD_TEST_EXPECT_TRUE(g != NULL && g->bind[DMVSI_BIND_Y] == y && g->bind[DMVSI_BIND_X] == 0 && g->click == h);
    DMOD_TEST_EXPECT_TRUE(g != NULL && g->show_var[0] == y2 && g->show_value[0] == 1 && g->show_var[1] == DMVSI_VAR_PRESSED);
    const dmvsi_action_t* actions = NULL;
    DMOD_TEST_EXPECT_EQ(dmvsi_handler_actions(doc, h, &actions), 3u);
    DMOD_TEST_EXPECT_TRUE(actions != NULL && actions[1].kind == DMVSI_ACT_ANIMATE && actions[1].duration == 300);
    dmvsi_free(doc);
}

/* A one-action handler: kind, var, operand var, value, text */
static dmvsi_handler_t one(dmvsi_doc_t doc, uint8_t kind, dmvsi_var_t var, dmvsi_var_t operand, int32_t value, const char* text)
{
    dmvsi_action_t a;
    memset(&a, 0, sizeof(a));
    a.kind = kind;
    a.var = var;
    a.operand = operand;
    a.value = value;
    a.text = text;
    return dmvsi_add_handler(doc, &a, 1);
}

DMOD_TEST_STEP(dmvsi_describes_code)
{
    dmvsi_doc_t doc = dmvsi_new();
    DMOD_TEST_EXPECT_EQ(dmvsi_set_view(doc, "v", 100, 100), 0);
    dmvsi_var_t n = dmvsi_add_var(doc, "speed", 0);
    dmvsi_var_t m = dmvsi_add_var(doc, "step", 2);
    dmvsi_var_t text = dmvsi_add_text_var(doc, "speed-text", 8, "0 km/h, too long");
    dmvsi_var_t unit = dmvsi_add_text_var(doc, "unit", 4, "km/h");
    DMOD_TEST_EXPECT_TRUE(n == 1 && m == 2 && text == 3 && unit == 4);
    DMOD_TEST_EXPECT_EQ(dmvsi_add_text_var(doc, "big", 2000, ""), 0);       /* 1024 bytes at most */
    dmvsi_var_info_t info;
    DMOD_TEST_EXPECT_EQ(dmvsi_var_info(doc, text, &info), 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(info.name, "speed_text") == 0 && info.kind == DMVSI_VAR_TEXT && info.size == 8);
    DMOD_TEST_EXPECT_TRUE(strcmp(info.text, "0 km/h, ") == 0);              /* cut to its size */
    DMOD_TEST_EXPECT_TRUE(dmvsi_var_info(doc, m, &info) == 0 && info.kind == DMVSI_VAR_INT && info.initial == 2);
    DMOD_TEST_EXPECT_EQ(dmvsi_var_info(doc, 9, &info), -EINVAL);

    /* What each action takes: integers, texts, both of one kind */
    DMOD_TEST_EXPECT_TRUE(one(doc, DMVSI_ACT_ADD, n, m, 0, NULL) != 0);                 /* speed += step */
    DMOD_TEST_EXPECT_TRUE(one(doc, DMVSI_ACT_SET, text, unit, 0, NULL) != 0);           /* text = unit */
    DMOD_TEST_EXPECT_TRUE(one(doc, DMVSI_ACT_APPEND, text, 0, 0, " km/h") != 0);
    DMOD_TEST_EXPECT_TRUE(one(doc, DMVSI_ACT_FORMAT, text, n, 0, "%d") != 0);           /* text = speed */
    DMOD_TEST_EXPECT_EQ(one(doc, DMVSI_ACT_ADD, n, text, 0, NULL), 0);                  /* a text into a number */
    DMOD_TEST_EXPECT_EQ(one(doc, DMVSI_ACT_APPEND, n, 0, 0, "x"), 0);                   /* text onto a number */
    DMOD_TEST_EXPECT_EQ(one(doc, DMVSI_ACT_SET, text, 0, 0, NULL), 0);                  /* no text */
    DMOD_TEST_EXPECT_EQ(one(doc, DMVSI_ACT_FORMAT, text, unit, 0, "%d"), 0);            /* formats a number */
    DMOD_TEST_EXPECT_EQ(one(doc, DMVSI_ACT_IF_EQ, text, 0, 0, "x"), 0);                 /* texts are not compared */
    DMOD_TEST_EXPECT_TRUE(one(doc, DMVSI_ACT_SET, m, DMVSI_VAR_TIME, 0, NULL) != 0);    /* step = the time */
    DMOD_TEST_EXPECT_EQ(one(doc, DMVSI_ACT_SET, text, DMVSI_VAR_TIME, 0, NULL), 0);     /* not a text */

    /* Blocks: IF ... ELSE ... END, LOOP with BREAK, nothing out of place */
    dmvsi_action_t loop[8];
    memset(loop, 0, sizeof(loop));
    loop[0].kind = DMVSI_ACT_LOOP;
    loop[1].kind = DMVSI_ACT_ADD;    loop[1].var = n;  loop[1].value = 2;
    loop[2].kind = DMVSI_ACT_IF_GE;  loop[2].var = n;  loop[2].value = 68;
    loop[3].kind = DMVSI_ACT_BREAK;
    loop[4].kind = DMVSI_ACT_ELSE;
    loop[5].kind = DMVSI_ACT_CONTINUE;
    loop[6].kind = DMVSI_ACT_END;
    loop[7].kind = DMVSI_ACT_END;
    dmvsi_handler_t counting = dmvsi_add_handler(doc, loop, 8);
    DMOD_TEST_EXPECT_TRUE(counting != 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_add_handler(doc, loop + 2, 6), 0);            /* BREAK out of no LOOP, END too many */
    loop[5].kind = DMVSI_ACT_ELSE;
    DMOD_TEST_EXPECT_EQ(dmvsi_add_handler(doc, loop, 8), 0);                /* two ELSEs */

    /* A handler called before it is made; not itself */
    dmvsi_handler_t later = dmvsi_new_handler(doc);
    dmvsi_action_t call[2];
    memset(call, 0, sizeof(call));
    call[0].kind = DMVSI_ACT_CALL;
    call[0].handler = later;
    call[1].kind = DMVSI_ACT_RETURN;
    dmvsi_handler_t caller = dmvsi_add_handler(doc, call, 2);
    DMOD_TEST_EXPECT_TRUE(caller != 0);
    const dmvsi_action_t* actions = NULL;
    DMOD_TEST_EXPECT_EQ(dmvsi_handler_actions(doc, later, &actions), 0u);   /* not made yet */
    DMOD_TEST_EXPECT_EQ(dmvsi_set_handler(doc, later, call, 1), -EINVAL);   /* calls itself */
    DMOD_TEST_EXPECT_EQ(dmvsi_set_handler(doc, later, loop + 1, 1), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_set_handler(doc, later, loop + 1, 1), -EINVAL);   /* once */
    DMOD_TEST_EXPECT_EQ(dmvsi_handler_actions(doc, later, &actions), 1u);
    call[0].handler = 99;
    DMOD_TEST_EXPECT_EQ(dmvsi_add_handler(doc, call, 1), 0);                /* no such handler */

    /* Its texts are copies */
    char format[4] = "%d";
    dmvsi_handler_t f = one(doc, DMVSI_ACT_FORMAT, text, n, 0, format);
    format[0] = 'x';
    DMOD_TEST_EXPECT_TRUE(dmvsi_handler_actions(doc, f, &actions) == 1u && strcmp(actions[0].text, "%d") == 0);

    /* Timers, the handler run when the view is shown */
    DMOD_TEST_EXPECT_EQ(dmvsi_add_timer(doc, 35, counting), 0);
    DMOD_TEST_EXPECT_EQ(dmvsi_add_timer(doc, 5, counting), -EINVAL);       /* 10 ms at least */
    DMOD_TEST_EXPECT_EQ(dmvsi_add_timer(doc, 100, 77), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmvsi_set_init(doc, caller), 0);
    uint16_t ms = 0;
    dmvsi_handler_t t = 0;
    DMOD_TEST_EXPECT_TRUE(dmvsi_timer_at(doc, 0, &ms, &t) && ms == 35 && t == counting);
    DMOD_TEST_EXPECT_FALSE(dmvsi_timer_at(doc, 1, &ms, &t));
    DMOD_TEST_EXPECT_EQ(dmvsi_init_handler(doc), caller);

    /* Text that shows a variable: its characters in the font, as wide as its room */
    dmvsi_font_t font = dmvsi_font(doc, NULL, 16, 0, NULL);
    dmvsi_text_t line;
    memset(&line, 0, sizeof(line));
    line.text = "0";
    line.length = 1;
    line.font = font;
    line.paint.color = 0xFFFFFFFFu;
    line.var = text;
    line.chars = "0123456789 km/h";
    line.width = DMVSI_PX(60);
    line.align = DMVSI_TEXT_CENTER;
    DMOD_TEST_EXPECT_EQ(dmvsi_add_text(doc, &line), 0);
    DMOD_TEST_EXPECT_TRUE(dmvsi_font_has(font, '9') && dmvsi_font_has(font, 'k'));
    const dmvsi_node_t* node = dmvsi_root(doc)->first;
    DMOD_TEST_EXPECT_TRUE(node != NULL && node->u.text.var == text && node->u.text.align == DMVSI_TEXT_CENTER &&
                          node->bounds.w == DMVSI_PX(60));
    line.var = n;
    DMOD_TEST_EXPECT_EQ(dmvsi_add_text(doc, &line), -EINVAL);               /* not a text variable */
    dmvsi_free(doc);
}
