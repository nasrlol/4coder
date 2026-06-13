//~ NOTE(rjf): Cursor rendering
//
//

// Forward declarations and macros
#define MemorySet                 memset
#define MemoryCopy                memcpy
#define CalculateCStringLength    strlen
#define S8Lit(s)                  string_u8_litexpr(s)

// BitOffset - find the bit position (0-based from LSB) of a set bit
internal int BitOffset(u32 value) {
        int offset = 0;
        while ((value & 1) == 0) {
                value >>= 1;
                offset++;
        }
        return offset;
}

enum keybinding_mode
{
        KeyBindingMode_0,
        KeyBindingMode_1,
        KeyBindingMode_2,
        KeyBindingMode_3,
        KeyBindingMode_MAX
};

typedef u32 F4_SyntaxFlags;
enum
{
        F4_SyntaxFlag_Functions    = (1<<0),
        F4_SyntaxFlag_Macros       = (1<<1),
        F4_SyntaxFlag_Types        = (1<<2),
        F4_SyntaxFlag_Operators    = (1<<3),
        F4_SyntaxFlag_Constants    = (1<<4),
        F4_SyntaxFlag_Literals     = (1<<5),
        F4_SyntaxFlag_Preprocessor = (1<<6),
        F4_SyntaxFlag_Keywords     = (1<<7),
        F4_SyntaxFlag_HighlightAll = (1<<15),
};
#define F4_SyntaxFlag_All 0xffffffff

struct F4_SyntaxOptions
{
        String8 name;
        F4_SyntaxFlags flags;
};

global f32 f4_syntax_flag_transitions[32] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,};
global F4_SyntaxOptions f4_syntax_opts[] =
{
        { S8Lit("All"),            F4_SyntaxFlag_All },
        { S8Lit("None"),           0 },
        { S8Lit("Functions Only"), F4_SyntaxFlag_Functions },
        { S8Lit("Macros Only"),    F4_SyntaxFlag_Macros },
        { S8Lit("Function-Likes Only"), F4_SyntaxFlag_Functions | F4_SyntaxFlag_Macros },
        { S8Lit("Types Only"),     F4_SyntaxFlag_Types },
        { S8Lit("Externals Only"), F4_SyntaxFlag_Functions | F4_SyntaxFlag_Macros | F4_SyntaxFlag_Types | F4_SyntaxFlag_Constants },
};
global i32 f4_active_syntax_opt_idx = 0;

function b32
F4_ARGBIsValid(ARGB_Color color)
{
        return color != 0xFF990099;
}

internal void
F4_TickColors(Application_Links *app, Frame_Info frame_info)
{
        F4_SyntaxOptions opts = f4_syntax_opts[f4_active_syntax_opt_idx];
        for(int i = 0; i < sizeof(F4_SyntaxFlags)*8; i += 1)
        {
                f32 delta = ((f32)!!(opts.flags & (1<<i)) - f4_syntax_flag_transitions[i]) * frame_info.animation_dt * 8.f;
                f4_syntax_flag_transitions[i] += delta;
                if(fabsf(delta) > 0.001f)
                {
                        animate_in_n_milliseconds(app, 0);
                }
        }
}

CUSTOM_COMMAND_SIG(f4_switch_syntax_option)
CUSTOM_DOC("Switches the syntax highlighting mode.")
{
        f4_active_syntax_opt_idx = (f4_active_syntax_opt_idx + 1) % ArrayCount(f4_syntax_opts);
}

internal String8
F4_SyntaxOptionString(void)
{
        return f4_syntax_opts[f4_active_syntax_opt_idx].name;
}

typedef u32 ColorFlags;
enum
{
        ColorFlag_Macro = (1<<0),
        ColorFlag_PowerMode = (1<<1),
};

struct ColorCtx
{
        Token token;
        Buffer_ID buffer;
        ColorFlags flags;
        keybinding_mode mode;
};

internal ColorCtx
ColorCtx_Token(Token token, Buffer_ID buffer)
{
        ColorCtx ctx = {0};
        ctx.token = token;
        ctx.buffer = buffer;
        return ctx;
}

internal ColorCtx
ColorCtx_Cursor(ColorFlags flags, keybinding_mode mode)
{
        ColorCtx ctx = {0};
        ctx.flags = flags;
        ctx.mode = mode;
        return ctx;
}

static ARGB_Color
F4_ARGBFromID(Color_Table table, Managed_ID id, int subindex)
{
        ARGB_Color result = 0;
        FColor color = fcolor_id(id);
        if (color.a_byte == 0){
                if (color.id != 0){
                        result = finalize_color(table, color.id, subindex);
                }
        }
        else{
                result = color.argb;
        }
        return(result);
}

static ARGB_Color
F4_ARGBFromID(Color_Table table, Managed_ID id)
{
        return F4_ARGBFromID(table, id, 0);
}

internal ARGB_Color
F4_GetColor(Application_Links *app, ColorCtx ctx)
{
        Color_Table table = active_color_table;
        ARGB_Color default_color = F4_ARGBFromID(table, defcolor_text_default);
        ARGB_Color color = default_color;
        f32 t = 1;

#define FillFromFlag(flag) do{ t = f4_syntax_flag_transitions[BitOffset(flag)]; }while(0)

        //~ NOTE(rjf): Token Color
        if(ctx.token.size != 0)
        {
                Scratch_Block scratch(app);

                switch(ctx.token.kind)
                {
                        case TokenBaseKind_Identifier:
                        {
                                FillFromFlag(F4_SyntaxFlag_Functions);
                                color = F4_ARGBFromID(table, defcolor_text_default);
                        }break;

                        case TokenBaseKind_Preprocessor:     { FillFromFlag(F4_SyntaxFlag_Preprocessor); color = F4_ARGBFromID(table, defcolor_preproc); } break;
                        case TokenBaseKind_Keyword:          { FillFromFlag(F4_SyntaxFlag_Keywords); color = F4_ARGBFromID(table, defcolor_keyword); } break;
                        case TokenBaseKind_Comment:          { color = F4_ARGBFromID(table, defcolor_comment); } break;
                        case TokenBaseKind_LiteralString:    { FillFromFlag(F4_SyntaxFlag_Literals); color = F4_ARGBFromID(table, defcolor_str_constant); } break;
                        case TokenBaseKind_LiteralInteger:   { FillFromFlag(F4_SyntaxFlag_Literals); color = F4_ARGBFromID(table, defcolor_int_constant); } break;
                        case TokenBaseKind_LiteralFloat:     { FillFromFlag(F4_SyntaxFlag_Literals); color = F4_ARGBFromID(table, defcolor_float_constant); } break;
                        case TokenBaseKind_Operator:         { FillFromFlag(F4_SyntaxFlag_Operators); color = F4_ARGBFromID(table, defcolor_text_default); } break;

                        case TokenBaseKind_ScopeOpen:
                        case TokenBaseKind_ScopeClose:
                        case TokenBaseKind_ParentheticalOpen:
                        case TokenBaseKind_ParentheticalClose:
                        case TokenBaseKind_StatementClose:
                        {
                                color = F4_ARGBFromID(table, defcolor_text_default);
                                break;
                        }

                        default:
                        {
                                switch(ctx.token.sub_kind)
                                {
                                        case TokenCppKind_LiteralTrue:
                                        case TokenCppKind_LiteralFalse:
                                        {
                                                color = F4_ARGBFromID(table, defcolor_bool_constant);
                                                FillFromFlag(F4_SyntaxFlag_Literals);
                                                break;
                                        }
                                        case TokenCppKind_LiteralCharacter:
                                        case TokenCppKind_LiteralCharacterWide:
                                        case TokenCppKind_LiteralCharacterUTF8:
                                        case TokenCppKind_LiteralCharacterUTF16:
                                        case TokenCppKind_LiteralCharacterUTF32:
                                        {
                                                color = F4_ARGBFromID(table, defcolor_char_constant);
                                                FillFromFlag(F4_SyntaxFlag_Literals);
                                                break;
                                        }
                                        case TokenCppKind_PPIncludeFile:
                                        {
                                                color = F4_ARGBFromID(table, defcolor_include);
                                                FillFromFlag(F4_SyntaxFlag_Literals);
                                                break;
                                        }
                                }
                        }break;

                }
        }

        //~ NOTE(rjf): Cursor Color
        else
        {
                color = F4_ARGBFromID(table, defcolor_cursor, ctx.mode);
        }

        return color_blend(default_color, t, color);
}

static void
F4_SyntaxHighlight(Application_Links *app, Text_Layout_ID text_layout_id, Token_Array *array)
{
        Color_Table table = active_color_table;
        Buffer_ID buffer = text_layout_get_buffer(app, text_layout_id);
        Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);
        i64 first_index = token_index_from_pos(array, visible_range.first);
        Token_Iterator_Array it = token_iterator_index(0, array, first_index);

        for(;;)
        {
                Token *token = token_it_read(&it);
                if(!token || token->pos >= visible_range.one_past_last)
                {
                        break;
                }
                ARGB_Color argb = F4_GetColor(app, ColorCtx_Token(*token, buffer));
                paint_text_color(app, text_layout_id, Ii64_size(token->pos, token->size), argb);

                if(!token_it_inc_all(&it))
                {
                        break;
                }
        }
}

static keybinding_mode GlobalKeybindingMode;
static Face_ID global_styled_title_face = 0;
static Face_ID global_styled_label_face = 0;
static Face_ID global_small_code_face = 0;
static Rect_f32 global_cursor_rect = {0};
static Rect_f32 global_last_cursor_rect = {0};
static Rect_f32 global_mark_rect = {0};
static Rect_f32 global_last_mark_rect = {0};
static b32 global_dark_mode = 1;
static b32 global_battery_saver = 0;
static View_ID global_compilation_view = 0;
static b32 global_compilation_view_expanded = 0;
global Arena permanent_arena = {};

global int global_cursor_count = 1;
global i64 global_cursor_positions[16] = {0};
global i64 global_mark_positions[16] = {0};
global int global_hide_region_boundary = 0;

enum Cursor_Type
{
        cursor_none,
        cursor_insert,
        cursor_open_range,
        cursor_close_range,
};

function void
C4_RenderCursorSymbolThingy(Application_Links *app, Rect_f32 rect,
                            f32 roundness, f32 thickness,
                            ARGB_Color color, Cursor_Type type)
{
        f32 line_height = rect.y1 - rect.y0;
        f32 bracket_width = 0.5f*line_height;

        if(type == cursor_open_range)
        {
                Rect_f32 start_top, start_side, start_bottom;

                Vec2_f32 start_p = {rect.x0, rect.y0};

                start_top.x0 = start_p.x + thickness;
                start_top.x1 = start_p.x + bracket_width;
                start_top.y0 = start_p.y;
                start_top.y1 = start_p.y + thickness;

                start_bottom.x0 = start_top.x0;
                start_bottom.x1 = start_top.x1;
                start_bottom.y1 = start_p.y + line_height;
                start_bottom.y0 = start_bottom.y1 - thickness;

                start_side.x0 = start_p.x;
                start_side.x1 = start_p.x + thickness;
                start_side.y0 = start_top.y0;
                start_side.y1 = start_bottom.y1;

                draw_rectangle(app, start_top, roundness, color);
                draw_rectangle(app, start_side, roundness, color);

                // draw_rectangle(app, start_bottom, start_color);
        }
        else if(type == cursor_close_range)
        {
                Rect_f32 end_top, end_side, end_bottom;

                Vec2_f32 end_p = {rect.x0, rect.y0};

                end_top.x0 = end_p.x;
                end_top.x1 = end_p.x - bracket_width;
                end_top.y0 = end_p.y;
                end_top.y1 = end_p.y + thickness;

                end_side.x1 = end_p.x;
                end_side.x0 = end_p.x + thickness;
                end_side.y0 = end_p.y;
                end_side.y1 = end_p.y + line_height;

                end_bottom.x0 = end_top.x0;
                end_bottom.x1 = end_top.x1;
                end_bottom.y1 = end_p.y + line_height;
                end_bottom.y0 = end_bottom.y1 - thickness;

                draw_rectangle(app, end_bottom, roundness, color);
                draw_rectangle(app, end_side, roundness, color);
        }
        else if(type == cursor_insert)
        {
                Rect_f32 side;
                side.x0 = rect.x0;
                side.x1 = rect.x0 + thickness;
                side.y0 = rect.y0;
                side.y1 = rect.y1;

                draw_rectangle(app, side, roundness, color);
        }
}

function void
DoTheCursorInterpolation(Application_Links *app, Frame_Info frame_info,
                         Rect_f32 *rect, Rect_f32 *last_rect, Rect_f32 target)
{
        *last_rect = *rect;

        float x_change = target.x0 - rect->x0;
        float y_change = target.y0 - rect->y0;

        float cursor_size_x = (target.x1 - target.x0);
        float cursor_size_y = (target.y1 - target.y0) * (1 + fabsf(y_change) / 30.f);

        b32 should_animate_cursor = !global_battery_saver && !def_get_config_b32(vars_save_string_lit("f4_disable_cursor_trails"));
        if(should_animate_cursor)
        {
                if(fabs(x_change) > 1.f || fabs(y_change) > 1.f)
                {
                        animate_in_n_milliseconds(app, 0);
                }
        }
        else
        {
                *rect = *last_rect = target;
                cursor_size_y = target.y1 - target.y0;
        }

        if(should_animate_cursor)
        {
                rect->x0 += (x_change) * frame_info.animation_dt * 30.f;
                rect->y0 += (y_change) * frame_info.animation_dt * 30.f;
                rect->x1 = rect->x0 + cursor_size_x;
                rect->y1 = rect->y0 + cursor_size_y;
        }

        if(target.y0 > last_rect->y0)
        {
                if(rect->y0 < last_rect->y0)
                {
                        rect->y0 = last_rect->y0;
                }
        }
        else
        {
                if(rect->y1 > last_rect->y1)
                {
                        rect->y1 = last_rect->y1;
                }
        }

}

function void
F4_Cursor_RenderEmacsStyle(Application_Links *app, View_ID view_id, b32 is_active_view,
                           Buffer_ID buffer, Text_Layout_ID text_layout_id,
                           f32 roundness, f32 outline_thickness, Frame_Info frame_info)
{
        Rect_f32 view_rect = view_get_screen_rect(app, view_id);
        Rect_f32 clip = draw_set_clip(app, view_rect);
        Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);

        b32 has_highlight_range = draw_highlight_range(app, view_id, buffer, text_layout_id, roundness);

        ColorFlags flags = 0;
        flags |= !!global_keyboard_macro_is_recording * ColorFlag_Macro;
        ARGB_Color cursor_color = F4_GetColor(app, ColorCtx_Cursor(flags, GlobalKeybindingMode));
        ARGB_Color mark_color = cursor_color;
        ARGB_Color inactive_cursor_color = cursor_color;

        if(is_active_view == 0)
        {
                cursor_color = inactive_cursor_color;
                mark_color = inactive_cursor_color;
        }

        // TODO(rjf): REMOVE THIS
        {
                i64 cursor_pos = view_get_cursor_pos(app, view_id);
                i64 mark_pos = view_get_mark_pos(app, view_id);
                global_cursor_positions[0] = cursor_pos;
                global_mark_positions[0] = mark_pos;
        }

        if(!has_highlight_range)
        {

                for(int i = 0; i < 1/*global_cursor_count*/; ++i)
                {
                        i64 cursor_pos = global_cursor_positions[0];
                        i64 mark_pos = global_mark_positions[0];

                        Cursor_Type cursor_type = cursor_none;
                        Cursor_Type mark_type = cursor_none;
                        if(cursor_pos <= mark_pos)
                        {
                                cursor_type = cursor_open_range;
                                mark_type = cursor_close_range;
                        }
                        else
                        {
                                cursor_type = cursor_close_range;
                                mark_type = cursor_open_range;
                        }

                        if(global_hide_region_boundary)
                        {
                                cursor_type = cursor_insert;
                                mark_type = cursor_none;
                        }

                        Rect_f32 target_cursor = text_layout_character_on_screen(app, text_layout_id, cursor_pos);
                        Rect_f32 target_mark = text_layout_character_on_screen(app, text_layout_id, mark_pos);

                        // NOTE(rjf): Draw cursor.
                        {
                                if(is_active_view)
                                {

                                        if(cursor_pos < visible_range.start || cursor_pos > visible_range.end)
                                        {
                                                f32 width = target_cursor.x1 - target_cursor.x0;
                                                target_cursor.x0 = view_rect.x0;
                                                target_cursor.x1 = target_cursor.x0 + width;
                                        }

                                        DoTheCursorInterpolation(app, frame_info, &global_cursor_rect,
                                                                 &global_last_cursor_rect, target_cursor);


                                        if(mark_pos > visible_range.end)
                                        {
                                                target_mark.x0 = 0;
                                                target_mark.y0 = view_rect.y1;
                                                target_mark.y1 = view_rect.y1;
                                        }

                                        if(mark_pos < visible_range.start || mark_pos > visible_range.end)
                                        {
                                                f32 width = target_mark.x1 - target_mark.x0;
                                                target_mark.x0 = view_rect.x0;
                                                target_mark.x1 = target_mark.x0 + width;
                                        }

                                        DoTheCursorInterpolation(app, frame_info, &global_mark_rect, &global_last_mark_rect,
                                                                 target_mark);
                                }

                                // NOTE(rjf): Draw main cursor.
                                {
                                        C4_RenderCursorSymbolThingy(app, global_cursor_rect, roundness, outline_thickness, cursor_color, cursor_type);
                                        C4_RenderCursorSymbolThingy(app, target_cursor, roundness, outline_thickness, cursor_color, cursor_type);
                                }

                                // NOTE(rjf): GLOW IT UP
                                for(int glow = 0; glow < 20; ++glow)
                                {
                                        f32 alpha = 0.1f - (glow*0.015f);
                                        if(alpha > 0)
                                        {
                                                Rect_f32 glow_rect = target_cursor;
                                                glow_rect.x0 -= glow;
                                                glow_rect.y0 -= glow;
                                                glow_rect.x1 += glow;
                                                glow_rect.y1 += glow;
                                                C4_RenderCursorSymbolThingy(app, glow_rect, roundness + glow*0.7f, 2.f,
                                                                            fcolor_resolve(fcolor_change_alpha(fcolor_argb(cursor_color), alpha)), cursor_type);
                                        }
                                        else
                                        {
                                                break;
                                        }
                                }

                        }

                        // note(nasr): changed the parameter from .5 to .75
                        // paint_text_color_pos(app, text_layout_id, cursor_pos,
                        // fcolor_id(defcolor_at_cursor));
                        C4_RenderCursorSymbolThingy(app, global_mark_rect, roundness, outline_thickness,
                                                    fcolor_resolve(fcolor_change_alpha(fcolor_argb(mark_color), 0.75f)), mark_type);
                        C4_RenderCursorSymbolThingy(app, target_mark, roundness, outline_thickness,
                                                    fcolor_resolve(fcolor_change_alpha(fcolor_argb(mark_color), 0.75f)), mark_type);
                }
        }

        draw_set_clip(app, clip);
}

internal b32
F4_Cursor_DrawHighlightRange(Application_Links *app, View_ID view_id,
                             Buffer_ID buffer, Text_Layout_ID text_layout_id,
                             f32 roundness)
{
        b32 has_highlight_range = false;
        Managed_Scope scope = view_get_managed_scope(app, view_id);
        Buffer_ID *highlight_buffer = scope_attachment(app, scope, view_highlight_buffer, Buffer_ID);
        if (*highlight_buffer != 0){
                if (*highlight_buffer != buffer){
                        view_disable_highlight_range(app, view_id);
                }
                else{
                        has_highlight_range = true;
                        Managed_Object *highlight = scope_attachment(app, scope, view_highlight_range, Managed_Object);
                        Marker marker_range[2];
                        if (managed_object_load_data(app, *highlight, 0, 2, marker_range)){
                                Range_i64 range = Ii64(marker_range[0].pos, marker_range[1].pos);
                                draw_character_block(app, text_layout_id, range, roundness,
                                                     fcolor_id(defcolor_highlight));
                        }
                }
        }
        return(has_highlight_range);
}

function void
F4_Cursor_RenderNotepadStyle(Application_Links *app, View_ID view_id, b32 is_active_view,
                             Buffer_ID buffer, Text_Layout_ID text_layout_id,
                             f32 roundness, f32 outline_thickness, Frame_Info frame_info)
{
        Rect_f32 view_rect = view_get_screen_rect(app, view_id);
        b32 has_highlight_range = draw_highlight_range(app, view_id, buffer, text_layout_id, roundness);
        if(!has_highlight_range)
        {
                i64 cursor_pos = view_get_cursor_pos(app, view_id);
                i64 mark_pos = view_get_mark_pos(app, view_id);

                if (cursor_pos != mark_pos)
                {
                        Range_i64 range = Ii64(cursor_pos, mark_pos);
                        draw_character_block(app, text_layout_id, range, roundness, fcolor_id(defcolor_highlight));
                }

                // NOTE(rjf): Draw cursor
                {
                        ARGB_Color cursor_color = F4_GetColor(app, ColorCtx_Cursor(0, GlobalKeybindingMode));
                        ARGB_Color ghost_color = fcolor_resolve(fcolor_change_alpha(fcolor_argb(cursor_color), 0.5f));
                        Rect_f32 rect = text_layout_character_on_screen(app, text_layout_id, cursor_pos);
                        rect.x1 = rect.x0 + outline_thickness;
                        if(rect.x0 < view_rect.x0)
                        {
                                rect.x0 = view_rect.x0;
                                rect.x1 = view_rect.x0 + outline_thickness;
                        }

                        if(is_active_view)
                        {
                                DoTheCursorInterpolation(app, frame_info, &global_cursor_rect, &global_last_cursor_rect, rect);
                        }
                        draw_rectangle(app, global_cursor_rect, roundness, ghost_color);
                        draw_rectangle(app, rect, roundness, cursor_color);
                }
        }
}

static void
F4_HighlightCursorMarkRange(Application_Links *app, View_ID view_id)
{
        Rect_f32 view_rect = view_get_screen_rect(app, view_id);
        Rect_f32 clip = draw_set_clip(app, view_rect);

        f32 lower_bound_y;
        f32 upper_bound_y;

        if(global_last_cursor_rect.y0 < global_last_mark_rect.y0)
        {
                lower_bound_y = global_last_cursor_rect.y0;
                upper_bound_y = global_last_mark_rect.y1;
        }
        else
        {
                lower_bound_y = global_last_mark_rect.y0;
                upper_bound_y = global_last_cursor_rect.y1;
        }

        draw_rectangle(app, Rf32(view_rect.x0, lower_bound_y, view_rect.x0 + 4, upper_bound_y), 3.f,
                       fcolor_resolve(fcolor_change_alpha(fcolor_id(defcolor_comment), 0.5f)));
        draw_set_clip(app, clip);
}


//~ NOTE(rjf): Mark Annotation

#if 0
function void
F4_RenderMarkAnnotation(Application_Links *app, Buffer_ID buffer, Text_Layout_ID text_layout_id,
                        View_ID view_id, b32 is_active_view)
{
        i64 pos = view_get_mark_pos(app, view_id);

        if(view_get_cursor_pos(app, view_id) > pos && is_active_view)
        {
                Scratch_Block scratch(app);
                ProfileScope(app, "[Fleury] Mark Annotation");

                Token_Array token_array = get_token_array_from_buffer(app, buffer);
                Face_ID face_id = global_small_code_face;
                Rect_f32 view_rect = view_get_screen_rect(app, view_id);

                Token *start_token = 0;

                if(token_array.tokens != 0)
                {
                        Token_Iterator_Array it = token_iterator_pos(0, &token_array, pos);

                        int max = 5;
                        int count = 0;

                        for(Token *token = 0; (token = token_it_read(&it)) != 0 && count < max;
                            token_it_inc_non_whitespace(&it), ++count)
                        {
                                if(token->kind == TokenBaseKind_Comment ||
                                   token->kind == TokenBaseKind_Identifier ||
                                   token->kind == TokenBaseKind_Keyword)
                                {
                                        start_token = token;
                                        break;
                                }
                        }
                }

                // NOTE(rjf): Draw.
                if(start_token)
                {
                        String_Const_u8 start_line = push_buffer_line(app, scratch, buffer,
                                                                      get_line_number_from_pos(app, buffer, start_token->pos));

                        u64 first_non_whitespace_offset = 0;
                        for(u64 c = 0; c < start_line.size; ++c)
                        {
                                if(start_line.str[c] <= 32)
                                {
                                        ++first_non_whitespace_offset;
                                }
                                else
                                {
                                        break;
                                }
                        }
                        start_line.str += first_non_whitespace_offset;
                        start_line.size -= first_non_whitespace_offset;

                        // NOTE(rjf): Special case to handle CRLF-newline files.
                        if(start_line.str[start_line.size - 1] == 13)
                        {
                                start_line.size -= 1;
                        }

                        Vec2_f32 draw_pos =
                        {
                                view_rect.x0 + 30,
                                global_cursor_rect.y0,
                        };

                        if(draw_pos.y < view_rect.y0)
                        {
                                draw_pos.y = view_rect.y0;
                        }

                        u32 color = finalize_color(defcolor_comment, 0);
                        color &= 0x00ffffff;
                        color |= 0x80000000;
                        draw_string_oriented(app, face_id, color, start_line, draw_pos, 0, V2f32(0.f, 1.f));
                }
        }
}
#endif



function void
nasr_draw_rect(Application_Links *app, Rect_f32 rect, f32 roundness, ARGB_Color color)
{
        draw_rectangle(app, rect, roundness, color);
}



function void
vim_draw_visual_mode(Application_Links *app, View_ID view, Buffer_ID buffer, Face_ID face_id, Text_Layout_ID text_layout_id){
        Range_i64 range = get_view_range(app, view);

        ARGB_Color text_color = fcolor_resolve(fcolor_id(defcolor_at_highlight));
        switch(vim_state.params.edit_type){
                case EDIT_Block:{
                        Rect_f32 block_rect = vim_get_abs_block_rect(app, view, buffer, text_layout_id, range);
                        nasr_draw_rect(app, block_rect, 0.f, fcolor_resolve(fcolor_id(defcolor_highlight)));

                        i64 line_min = get_line_number_from_pos(app, buffer, range.min);
                        i64 line_max = get_line_number_from_pos(app, buffer, range.max);
                        f32 line_advance = rect_height(block_rect)/f32(Max(1, line_max-line_min));
                        f32 wid = rect_width(block_rect);

                        Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);
                        Range_i64 test_range = range_intersect(visible_range, range);
                        i64 test_line_min = get_line_number_from_pos(app, buffer, test_range.min);
                        i64 test_line_max = get_line_number_from_pos(app, buffer, test_range.max);

                        ARGB_Color helper_color = fcolor_resolve(fcolor_id(defcolor_mark));
                        for(i64 i=test_line_min; i<=test_line_max; i++){
                                if(line_is_valid_and_blank(app, buffer, i) && i != line_min && i != line_max){ continue; }
                                Vec2_f32 min_point = block_rect.p0 + V2f32(0, line_advance*(i-line_min));
                                Vec2_f32 max_point = min_point + V2f32(wid,0);
                                i64 min_pos = view_pos_from_xy(app, view, min_point);
                                i64 max_pos = view_pos_from_xy(app, view, max_point);
                                paint_text_color(app, text_layout_id, Ii64(min_pos, max_pos), text_color);

                                if(!vim_show_block_helper || min_pos == max_pos){ continue; }
                                Rect_f32 min_rect = text_layout_character_on_screen(app, text_layout_id, min_pos);
                                Rect_f32 max_rect = text_layout_character_on_screen(app, text_layout_id, max_pos-1);
                                draw_rectangle(app, rect_split_top_bottom_neg(min_rect, 3.f).b, 3.0f, helper_color);
                                draw_rectangle(app, rect_split_top_bottom(max_rect,     3.f).a, 3.0f, helper_color);
                                draw_rectangle(app, rect_split_left_right(min_rect,     3.f).a, 3.0f, helper_color);
                                draw_rectangle(app, rect_split_left_right_neg(max_rect, 3.f).b, 3.0f, helper_color);
                        }
                } break;

                case EDIT_LineWise:{
                        range.min = get_line_side_pos_from_pos(app, buffer, range.min, Side_Min);
                        range.max = get_line_side_pos_from_pos(app, buffer, range.max, Side_Max) + 1; // highlight newlines

                        if(vim_do_full_line){
                                Range_i64 line_range = Ii64(get_line_number_from_pos(app, buffer, range.min),
                                                            get_line_number_from_pos(app, buffer, range.max)-1);
                                draw_line_highlight(app, text_layout_id, line_range, fcolor_id(defcolor_highlight));
                                paint_text_color(app, text_layout_id, range, text_color);
                                break;
                        }

                        // TODO(BYP): There may still be weird edge cases here
                        /// Virtual Whitespace handling
                        Managed_Scope scope = buffer_get_managed_scope(app, buffer);
                        Command_Map_ID *map_id_ptr = scope_attachment(app, scope, buffer_map_id, Command_Map_ID);
                        b32 is_code = *map_id_ptr == i64(vars_save_string_lit("keys_code"));
                        b32 virtual_enabled = def_get_config_b32(vars_save_string_lit("enable_virtual_whitespace"));
                        if((virtual_enabled && is_code) && character_is_whitespace(buffer_get_char(app, buffer, range.min))){
                                //range.min = get_pos_past_lead_whitespace(app, buffer, range.min);
                                i64 line_end = get_line_end_pos(app, buffer, get_line_number_from_pos(app, buffer, range.min));
                                line_end -= (line_end > 0 && buffer_get_char(app, buffer, line_end) == '\n' && buffer_get_char(app, buffer, line_end-1) == '\r');
                                i64 non_ws = buffer_seek_character_class_change_1_0(app, buffer, &character_predicate_whitespace, Scan_Forward, range.min);
                                range.min = Min(line_end, non_ws);
                        }
                        // Fall through
                }

                case EDIT_CharWise:{
                        if(vim_state.params.edit_type != EDIT_LineWise){ range.max++; }
                        draw_character_block(app, text_layout_id, range, 0.f, fcolor_id(defcolor_highlight));
                        paint_text_color(app, text_layout_id, range, text_color);
                } break;
        }
}

function void
vim_draw_filebar(Application_Links *app, View_ID view_id, Buffer_ID buffer, Frame_Info frame_info, Face_ID face_id, Rect_f32 bar){
        Scratch_Block scratch(app);
        String_Const_u8 unique_name = push_buffer_unique_name(app, scratch, buffer);

        nasr_draw_rect(app, bar, 0.f, fcolor_resolve(fcolor_id(defcolor_bar)));

        f32 char_wid = get_face_metrics(app, face_id).normal_advance;
        Rect_f32 title_rect = bar;
        // NOTE(nasr): padding for the title rect
        // just like byp mentioned in the 4coder server
        title_rect.x1 = bar.x0 + char_wid*unique_name.size + 0.5;
        nasr_draw_rect(app, title_rect, 0.f, fcolor_resolve(fcolor_id(defcolor_vim_filebar_pop)));

        Rect_f32 triangle_rect = title_rect;
        f32 radius_fudge = 0.f;
        triangle_rect.x0 = title_rect.x1 - radius_fudge*char_wid;
        triangle_rect.x1 = title_rect.x1 + radius_fudge*char_wid;
        nasr_draw_rect(app, triangle_rect, radius_fudge*char_wid, fcolor_resolve(fcolor_id(defcolor_vim_filebar_pop)));

        FColor base_color = fcolor_id(defcolor_base);
        FColor pop2_color = fcolor_id(defcolor_pop2);

        i64 cursor_position = view_get_cursor_pos(app, view_id);
        Buffer_Cursor cursor = view_compute_cursor(app, view_id, seek_pos(cursor_position));

        u8 space[5];
        String_u8 str = Su8(space, 0, 4);

        Managed_Scope scope = buffer_get_managed_scope(app, buffer);
        Line_Ending_Kind *eol_kind = scope_attachment(app, scope, buffer_eol_setting, Line_Ending_Kind);
        switch(*eol_kind){
                case LineEndingKind_Binary:{ string_append(&str, string_u8_litexpr("bin"));  } break;
                case LineEndingKind_LF:    { string_append(&str, string_u8_litexpr("lf"));   } break;
                case LineEndingKind_CRLF:  { string_append(&str, string_u8_litexpr("crlf")); } break;
        }


        Vec2_f32 p = V2f32(title_rect.x1 + 4.5f*char_wid, bar.y0 + 3.f);
        p = draw_string(app, face_id, str.string, p, base_color);

        str = Su8(space, 0, 5);
        Dirty_State dirty = buffer_get_dirty_state(app, buffer);
        if(dirty != 0){
                string_append(&str, string_u8_litexpr(" ["));
                if(HasFlag(dirty, DirtyState_UnsavedChanges))
                        string_append(&str, string_u8_litexpr("+"));
                if(HasFlag(dirty, DirtyState_UnloadedChanges))
                        string_append(&str, string_u8_litexpr("!"));
                string_append(&str, string_u8_litexpr("]"));
                draw_string(app, face_id, str.string, p, pop2_color);
        }

        p.x = Max(p.x + 5.f*char_wid, bar.x1 - char_wid*15.f);
        draw_string(app, face_id, push_stringf(scratch, "%d,%d", cursor.line, cursor.col), p, base_color);

        p.x = bar.x0 + 2.f;
        draw_string(app, face_id, unique_name, p, base_color);

        p.x = bar.x1 - char_wid*3.5f;
        i64 buffer_size = buffer_get_size(app, buffer);
        String_Const_u8 PosText;
        if(cursor_position == 0){
                PosText = string_u8_litexpr("Top");
        }else if(cursor_position ==  buffer_size){
                PosText = string_u8_litexpr("Bot");
        }else{
                PosText = push_stringf(scratch, "%d%%", i64(100.f*cursor_position/(buffer_size)));
        }
        draw_string(app, face_id, PosText, p, base_color);
}

function void
vim_draw_search_highlight(Application_Links *app, View_ID view, Buffer_ID buffer, Text_Layout_ID text_layout_id, f32 roundness){
        String_u8 *pattern = &vim_registers.search.data;
        if(pattern->size == 0){ return; }
        Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);
        i64 buffer_size = buffer_get_size(app, buffer);
        i64 cur_pos = visible_range.min;
        while(cur_pos < visible_range.max){
                i64 new_pos = 0;
                seek_string_forward(app, buffer, cur_pos, 0, pattern->string, &new_pos);
                if(new_pos == 0 || new_pos == buffer_size){ break; }
                else{
                        cur_pos = new_pos;
                        Rect_f32 rect = text_layout_character_on_screen(app, text_layout_id, cur_pos);
                        rect.x1 = rect.x0 + pattern->size*(rect_width(rect));
                        nasr_draw_rect(app, rect, roundness, fcolor_resolve(fcolor_id(defcolor_highlight)));
                }
        }
}

function void
vim_draw_cursor(Application_Links *app, View_ID view, b32 is_active_view, Buffer_ID buffer, Text_Layout_ID text_layout_id, f32 roundness, f32 thickness, Frame_Info frame_info = {}){

        if(is_active_view && vim_state.mode == VIM_Visual_Insert){
                if(vim_show_cursor(app)){
                        Range_i64 range = get_view_range(app, view);
                        Rect_f32 block_rect = vim_get_abs_block_rect(app, view, buffer, text_layout_id, range);

                        // TODO(BYP): Could use vim_show_block_helper to have nicer, more precise cursors like in vim_draw_visual_mode
                        if(vim_visual_insert_after){
                                block_rect.x0 = block_rect.x1 - 2.f;
                        }else{
                                block_rect.x1 = block_rect.x0 + 2.f;
                        }
                        nasr_draw_rect(app, block_rect, 1.f, fcolor_resolve(fcolor_id(defcolor_cursor)));
                }
                return;
        }

        F4_Cursor_RenderEmacsStyle(app, view, is_active_view, buffer, text_layout_id, roundness, thickness, frame_info);
}

function void
vim_draw_after_text(Application_Links *app, View_ID view, b32 is_active_view, Buffer_ID buffer, Text_Layout_ID text_layout_id, f32 cursor_roundness, f32 mark_thickness, Frame_Info frame_info = {}){
        if(is_active_view && vim_is_selecting_register && vim_state.mode == VIM_Insert){
                i64 cursor_pos = view_get_cursor_pos(app, view);
                Rect_f32 cursor_rect = text_layout_character_on_screen(app, text_layout_id, cursor_pos);
                nasr_draw_rect(app, cursor_rect, 0.f, fcolor_resolve(fcolor_id(defcolor_back)));
                if(!def_get_config_b32(vars_save_string_lit("highlight_line_at_cursor"))){
                        nasr_draw_rect(app, cursor_rect, 0.f, fcolor_resolve(fcolor_id(defcolor_highlight_cursor_line)));
                }
                vim_draw_cursor(app, view, is_active_view, buffer, text_layout_id, cursor_roundness, mark_thickness, frame_info);
                draw_string(app, get_face_id(app, 0), string_u8_litexpr("\""), cursor_rect.p0, fcolor_id(defcolor_text_default));
        }
}

function Rect_f32
vim_draw_query_bars(Application_Links *app, Rect_f32 region, View_ID view_id, Face_ID face_id){
        Face_Metrics face_metrics = get_face_metrics(app, face_id);
        f32 line_height = face_metrics.line_height;

        Query_Bar *space[32];
        Query_Bar_Ptr_Array query_bars = {};
        query_bars.ptrs = space;
        if(get_active_query_bars(app, view_id, ArrayCount(space), &query_bars)){
                foreach(i,query_bars.count){
                        Rect_f32_Pair pair = layout_query_bar_on_bot(region, line_height, 1);
                        nasr_draw_rect(app, pair.max, 0.f, fcolor_resolve(fcolor_id(defcolor_back)));
                        draw_query_bar(app, query_bars.ptrs[i], face_id, pair.max);
                        region = pair.min;
                }
        }
        return region;
}

function Rect_f32_Pair
vim_line_number_margin(Application_Links *app, Buffer_ID buffer, Rect_f32 rect, f32 digit_advance){
        i64 line_count = buffer_get_line_count(app, buffer);
        i64 digit_count = digit_count_from_integer(line_count, 10) + i64(vim_relative_numbers != 0);

        f32 margin_width = (f32)digit_count*digit_advance + 6.f;
        Rect_f32_Pair pair = rect_split_left_right(rect, margin_width);
        pair.a = rect_split_left_right(pair.a, 6.f).b;
        pair.b = rect_split_left_right(pair.b, 4.f).b;
        return pair;
}


function void
vim_draw_rel_line_number_margin(Application_Links *app, View_ID view, Buffer_ID buffer, Face_ID face, Text_Layout_ID text_layout_id, Rect_f32 margin){
        Rect_f32 prev_clip = draw_set_clip(app, margin);
        nasr_draw_rect(app, margin, 0.f, fcolor_resolve(fcolor_id(defcolor_line_numbers_back)));

        const i64 cur_line = get_line_number_from_pos(app, buffer, view_get_cursor_pos(app, view));
        const i64 line_count = buffer_get_line_count(app, buffer);

        i64 cur_line_digit_count = digit_count_from_integer(cur_line, 10);
        i64 bot_line_digit_count = digit_count_from_integer(line_count, 10);
        i64 digit_count = Max(cur_line_digit_count+1, bot_line_digit_count);

        Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);
        Buffer_Cursor cursor = view_compute_cursor(app, view, seek_pos(visible_range.first));
        Buffer_Cursor cursor_end = view_compute_cursor(app, view, seek_pos(visible_range.end));
        const i64 first_line_num = cursor.line;
        const i64 one_past_last = cursor_end.line;

        Scratch_Block scratch(app);
        u8 *digit_buffer = push_array(scratch, u8, digit_count);
        String_Const_u8 digit_string = SCu8(digit_buffer, digit_count);
        foreach(i, digit_count){ digit_buffer[i] = ' '; }

        u8 *small_digit = digit_buffer + (digit_count-1) - 1;
        u8 *ptr = small_digit;
        if(cur_line == 0){ *ptr = '0'; }
        else{
                for(u64 X=cur_line; X>0; X/=10){
                        *ptr-- = '0' + (X%10);
                }
        }
        small_digit++;

        Range_f32 line_y = text_layout_line_on_screen(app, text_layout_id, cur_line);
        Vec2_f32 p = V2f32(margin.x0, line_y.min);

        // NOTE(BYP): This assumes background is darker than font color
        FColor text_color = fcolor_id(defcolor_line_numbers_text);
        FColor contrast_color = fcolor_blend(text_color, 0.4f, f_white);

        draw_string(app, face, digit_string, p, fcolor_resolve(contrast_color));

        i32 rel_num = 1;
        foreach(i, digit_count-1){ digit_buffer[i] = ' '; }
        digit_buffer[digit_count-1] = '1';

        for(;;){
                i64 bot_line = cur_line+rel_num;
                if(bot_line > one_past_last){ break; }

                for(;;){
                        line_y = text_layout_line_on_screen(app, text_layout_id, cur_line+rel_num);
                        if(line_y.min != line_y.max){ break; }
                        rel_num++;
                }
                p = V2f32(margin.x0, line_y.min);
                draw_string(app, face, digit_string, p, fcolor_resolve(text_color));

                rel_num++;
                ptr = small_digit;
                while(ptr >= digit_buffer){
                        if(*ptr == ' '){ *ptr   = '0'; }
                        if(*ptr == '9'){ *ptr-- = '0'; }
                        else{ (*ptr)++; break; }
                }
        }

        rel_num = 1;
        foreach(i, digit_count-1){ digit_buffer[i] = ' '; }
        digit_buffer[digit_count-1] = '1';

        for(;;){
                i64 top_line = cur_line-rel_num;
                if(top_line < first_line_num){ break; }

                for(;;){
                        line_y = text_layout_line_on_screen(app, text_layout_id, cur_line-rel_num);
                        if(line_y.min != line_y.max){ break; }
                        rel_num++;
                }
                p = V2f32(margin.x0, line_y.min);
                draw_string(app, face, digit_string, p, fcolor_resolve(text_color));

                rel_num++;
                ptr = small_digit;
                while(ptr >= digit_buffer){
                        if(*ptr == ' '){ *ptr   = '0'; }
                        if(*ptr == '9'){ *ptr-- = '0'; }
                        else{ (*ptr)++; break; }
                }
        }
        draw_set_clip(app, prev_clip);
}

function void
vim_draw_line_number_margin(Application_Links *app, View_ID view, Buffer_ID buffer, Face_ID face, Text_Layout_ID text_layout_id, Rect_f32 margin){

        Scratch_Block scratch(app);
        Rect_f32 prev_clip = draw_set_clip(app, margin);
        nasr_draw_rect(app, margin, 0.f, fcolor_resolve(fcolor_id(defcolor_line_numbers_back)));

        Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);
        i64 line_count = buffer_get_line_count(app, buffer);
        i64 digit_count = digit_count_from_integer(line_count, 10);

        u8 *digit_buffer = push_array(scratch, u8, digit_count);
        String_Const_u8 digit_string = SCu8(digit_buffer, digit_count);
        foreach(i, digit_count){ digit_buffer[i] = ' '; }

        i64 cur_line = view_compute_cursor(app, view, seek_pos(visible_range.min)).line;
        i64 end_line = view_compute_cursor(app, view, seek_pos(visible_range.max)).line+1;

        u8 *small_digit = digit_buffer + (digit_count-1) - 1;
        u8 *ptr = small_digit;
        if(cur_line == 0){ *ptr = '0'; }
        else{
                for(u64 X=cur_line; X>0; X/=10){
                        *ptr-- = '0' + (X%10);
                }
        }

        Range_f32 line_y = text_layout_line_on_screen(app, text_layout_id, cur_line);
        Vec2_f32 p = V2f32(margin.x0, line_y.min);

        // NOTE(BYP): This assumes background is darker than font color
        FColor text_color = fcolor_id(defcolor_line_numbers_text);
        FColor contrast_color = fcolor_blend(text_color, 0.4f, f_white);

        draw_string(app, face, digit_string, p, fcolor_resolve(contrast_color));

        for(;;){
                if(cur_line > end_line){ break; }

                line_y = text_layout_line_on_screen(app, text_layout_id, cur_line);
                if(line_y.min != line_y.max){
                        p = V2f32(margin.x0, line_y.min);
                        draw_string(app, face, digit_string, p, fcolor_resolve(text_color));
                }

                cur_line++;
                ptr = small_digit;
                while(ptr >= digit_buffer){
                        if(*ptr == ' '){ *ptr   = '0'; }
                        if(*ptr == '9'){ *ptr-- = '0'; }
                        else{ (*ptr)++; break; }
                }
        }

        draw_set_clip(app, prev_clip);
}

function void
vim_draw_whole_screen(Application_Links *app, Frame_Info frame_info){
        Rect_f32 region = global_get_screen_rectangle(app);
        Vec2_f32 center = rect_center(region);
        draw_set_clip(app, region);

        Face_ID face_id = get_face_id(app, 0);
        Face_Metrics metrics = get_face_metrics(app, face_id);
        f32 line_height = metrics.line_height;
        f32 char_wid = metrics.normal_advance;

        // NOTE(BYP): Drawing the cursor for view transitions
        if(vim_cur_cursor_pos != vim_nxt_cursor_pos){
                Vec2_f32 cursor_dim = V2f32(9.f, (vim_state.mode == VIM_Insert ? 4.f : 18.f));
                Rect_f32 cursor_rect = Rf32_xy_wh(vim_cur_cursor_pos - cursor_dim, cursor_dim);
                u64 cursor_roundness_100 = def_get_config_u64(app, vars_save_string_lit("cursor_roundness"));
                f32 roundness = char_wid*cursor_roundness_100*0.01f;
                nasr_draw_rect(app, cursor_rect, roundness, fcolor_resolve(fcolor_id(defcolor_cursor, default_cursor_sub_id())));
        }

        ARGB_Color back_color = fcolor_resolve(fcolor_id(defcolor_back));

        // NOTE(BYP): Drawing the back of the filebar lister
        Rect_f32 back_rect = vim_get_bottom_rect(app);
        if(vim_cur_filebar_offset > vim_nxt_filebar_offset){
                draw_rectangle(app, back_rect, 0.f, back_color);
                nasr_draw_rect(app, rect_split_top_bottom_neg(back_rect, 4.f).b, 0.f, fcolor_resolve(get_item_margin_color(UIHighlight_Active)));
        }

        draw_rectangle(app, rect_split_top_bottom_neg(region, 2.f*line_height).b, 0.f, back_color);

        Vec2_f32 bot_left = {region.x0 + 4.f, region.y1 - 1.5f*line_height};
        String_Const_u8 bot_string = vim_get_bot_string();

        if(vim_use_bottom_cursor){
                Vec2_f32 p = draw_string(app, face_id, vim_bot_text.string, bot_left, finalize_color(defcolor_text_default, 0));
                if(vim_show_cursor(app)){
                        if(vim_bot_text.size){ p.x -= 0.37f*(p.x - bot_left.x)/vim_bot_text.size; }
                        draw_string(app, face_id, string_u8_litexpr("|"), p, finalize_color(defcolor_text_default, 0));
                }
        }else{
                draw_string(app, face_id, bot_string, bot_left, finalize_color(defcolor_text_default, 0));
        }

        if(vim_lister_view_id == 0){
                /// NOTE(BYP): This is kinda a hacky pseudo-view
                if(vim_show_buffer_peek && rect_height(back_rect) > 0.f){
                        Vim_Buffer_Peek_Entry *entry = vim_buffer_peek_list + vim_buffer_peek_index;
                        Buffer_Identifier buffer_iden = entry->buffer_id;
                        Buffer_ID buffer = buffer_identifier_to_id(app, buffer_iden);
                        vim_set_bottom_text(SCu8((u8 *)buffer_iden.name, buffer_iden.name_len));

                        f32 ratio_diff = entry->nxt_ratio - entry->cur_ratio;
                        entry->cur_ratio += ratio_diff*frame_info.animation_dt*20.f;

                        Buffer_Point buffer_point = {};
                        i64 line_count = buffer_get_line_count(app, buffer);
                        //buffer_point.line_number = i64(entry->cur_ratio*(line_count+1) - rect_height(back_rect)/line_height);
                        buffer_point.pixel_shift.y = line_height*entry->cur_ratio*(line_count+1) - rect_height(back_rect);

                        FColor peek_back_color = fcolor_id(defcolor_back);
                        nasr_draw_rect(app, back_rect, 0.f, fcolor_resolve(peek_back_color));
                        Rect_f32_Pair pair = rect_split_top_bottom_neg(back_rect, 4.f);
                        nasr_draw_rect(app, pair.b, 0.f, fcolor_resolve(get_item_margin_color(UIHighlight_Active)));
                        back_rect = rect_split_left_right(pair.a, 4.f).b;
                        Rect_f32 prev_clip = draw_set_clip(app, back_rect);

                        Text_Layout_ID text_layout_id = text_layout_create(app, buffer, back_rect, buffer_point);
                        Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);

                        paint_text_color_fcolor(app, text_layout_id, visible_range, fcolor_id(defcolor_text_default));
                        paint_fade_ranges(app, text_layout_id, buffer);
                        draw_text_layout_default(app, text_layout_id);

                        text_layout_free(app, text_layout_id);
                        draw_set_clip(app, prev_clip);
                }
        }

        Vec2_f32 bot_right = {region.x1 - 4.f - char_wid*vim_keystroke_text.size, bot_left.y};
        FColor chord_color = fcolor_id(defcolor_vim_chord_unresolved);
        if(vim_state.chord_resolved){
                chord_color = (vim_state.chord_resolved & bit_2 ?  fcolor_id(defcolor_vim_chord_error) : fcolor_id(defcolor_vim_chord_text));
        }
        draw_string(app, face_id, vim_keystroke_text.string, bot_right, chord_color);
}

