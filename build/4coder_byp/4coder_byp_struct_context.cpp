
function b32
byp_token_is(Application_Links *app, Buffer_ID buffer, Token *token, String_Const_u8 str){
 Scratch_Block scratch(app);
 return string_match(push_token_lexeme(app, scratch, buffer, token), str);
 }

 function b32
 byp_struct_find_body(Application_Links *app, Buffer_ID buffer, Range_i64 name, Range_i64 *out_range){
 Token_Array tokens = get_token_array_from_buffer(app, buffer);
 if(tokens.tokens == 0){ return false; }

 Token_Iterator_Array it = token_iterator_pos(0, &tokens, name.min);
 i64 open_pos = -1;
 for(;token_it_inc_non_whitespace(&it);){
 Token *token = token_it_read(&it);
 if(token == 0){ break; }
 if(token->kind == TokenBaseKind_ScopeOpen){ open_pos = token->pos; break; }
 if(token->kind == TokenBaseKind_StatementClose ||
 token->kind == TokenBaseKind_ParentheticalOpen ||
 token->kind == TokenBaseKind_ScopeClose){ break; }
 }

 if(open_pos >= 0){
 i32 nest = 0;
 for(;;){
 Token *token = token_it_read(&it);
 if(token == 0){ return false; }
 if(token->kind == TokenBaseKind_ScopeOpen){ nest += 1; }
 if(token->kind == TokenBaseKind_ScopeClose){
 nest -= 1;
 if(nest == 0){
 *out_range = Ii64(name.min, token->pos + token->size);
 return true;
 }
 }
 if(!token_it_inc_non_whitespace(&it)){ return false; }
 }
 }

 it = token_iterator_pos(0, &tokens, name.min);
 if(!token_it_dec_non_whitespace(&it)){ return false; }
 Token *close = token_it_read(&it);
 if(close == 0 || close->kind != TokenBaseKind_ScopeClose){ return false; }
 i32 nest = 0;
 for(;;){
 Token *token = token_it_read(&it);
 if(token == 0){ return false; }
 if(token->kind == TokenBaseKind_ScopeClose){ nest += 1; }
 if(token->kind == TokenBaseKind_ScopeOpen){
 nest -= 1;
 if(nest == 0){
 *out_range = Ii64(token->pos, name.max);
 return true;
 }
 }
 if(!token_it_dec_non_whitespace(&it)){ return false; }
 }
 }

 function b32
 byp_code_index_find_struct(Application_Links *app, String_Const_u8 query, Buffer_ID *out_buffer, Range_i64 *out_range){
 Buffer_ID found_buffer = 0;
 Range_i64 found_range = {};
 b32 found = false;

 code_index_lock();
 for(Buffer_ID b = get_buffer_next(app, 0, Access_Always);
 b != 0 && !found;
 b = get_buffer_next(app, b, Access_Always))
 {
 Code_Index_File *file = code_index_get_file(b);
 if(file == 0){ continue; }
 foreach(i, file->note_array.count){
 Code_Index_Note *note = file->note_array.ptrs[i];
 if(note->note_kind == CodeIndexNote_Type && string_match(note->text, query)){
 Range_i64 body = {};
 if(byp_struct_find_body(app, b, note->pos, &body)){
 found_buffer = b;
 found_range = body;
 found = true;
 break;
 }
 }
 }
 }
 code_index_unlock();

 *out_buffer = found_buffer;
 *out_range = found_range;
 return found;
 }

 function b32
 byp_struct_context_find(Application_Links *app, Buffer_ID buffer, Token_Array *tokens, i64 pos,
 String_Const_u8 *out_type, String_Const_u8 *out_designator, i32 *out_field_idx, Arena *arena)
 {
 Token_Iterator_Array it = token_iterator_pos(0, tokens, pos);
 i32 scope_nest = 0;
 i32 paren_nest = 0;
 i32 field_idx = 0;
 String_Const_u8 designator = {};
 b32 found_open = false;

 for(i32 i = 0; i < 4096; i += 1){
 Token *token = token_it_read(&it);
 if(token == 0){ return false; }

 if(token->kind == TokenBaseKind_ScopeOpen){
 if(scope_nest == 0 && paren_nest == 0){ found_open = true; break; }
 scope_nest -= 1;
 }
 else if(token->kind == TokenBaseKind_ScopeClose && i > 0){
 scope_nest += 1;
 }
 else if(token->kind == TokenBaseKind_ParentheticalOpen){
 if(paren_nest == 0 && scope_nest == 0){ return false; }
 paren_nest -= 1;
 }
 else if(token->kind == TokenBaseKind_ParentheticalClose && i > 0){
 paren_nest += 1;
 }
 else if(scope_nest == 0 && paren_nest == 0 && i > 0 && token->kind == TokenBaseKind_StatementClose){
 if(buffer_get_char(app, buffer, token->pos) != ','){ return false; }
 field_idx += 1;
 }
 else if(scope_nest == 0 && paren_nest == 0 && designator.size == 0 &&
 token->kind == TokenBaseKind_Identifier)
 {
 Token_Iterator_Array prev = it;
 if(token_it_dec_non_whitespace(&prev)){
 Token *dot = token_it_read(&prev);
 if(dot && byp_token_is(app, buffer, dot, S8Lit("."))){
 Token_Iterator_Array before_dot = prev;
 if(token_it_dec_non_whitespace(&before_dot)){
 Token *b = token_it_read(&before_dot);
 if(b && (b->kind == TokenBaseKind_ScopeOpen || b->kind == TokenBaseKind_StatementClose)){
 designator = push_token_lexeme(app, arena, buffer, token);
 }
 }
 }
 }
 }

 if(!token_it_dec_non_whitespace(&it)){ return false; }
 }
 if(!found_open){ return false; }

 if(!token_it_dec_non_whitespace(&it)){ return false; }
 Token *a = token_it_read(&it);
 if(a == 0){ return false; }

 Token *type = 0;
 if(a->kind == TokenBaseKind_Identifier){
 type = a;
 if(token_it_dec_non_whitespace(&it)){
 Token *b = token_it_read(&it);
 if(b && b->kind == TokenBaseKind_Identifier){
 type = b;
 }
 else if(b && b->kind == TokenBaseKind_Keyword){
 String_Const_u8 kw = push_token_lexeme(app, arena, buffer, b);
 if(string_match(kw, S8Lit("struct")) || string_match(kw, S8Lit("union")) ||
 string_match(kw, S8Lit("class")) || string_match(kw, S8Lit("enum")) ||
 string_match(kw, S8Lit("namespace")) || string_match(kw, S8Lit("public")) ||
 string_match(kw, S8Lit("private")) || string_match(kw, S8Lit("protected"))){
 return false;
 }
 }
 else if(b && byp_token_is(app, buffer, b, S8Lit(":"))){
 return false;
 }
 }
 }
 else if(a->kind == TokenBaseKind_ParentheticalClose){
 if(!token_it_dec_non_whitespace(&it)){ return false; }
 Token *b = token_it_read(&it);
 if(b == 0 || b->kind != TokenBaseKind_Identifier){ return false; }
 if(!token_it_dec_non_whitespace(&it)){ return false; }
 Token *c = token_it_read(&it);
 if(c == 0 || c->kind != TokenBaseKind_ParentheticalOpen){ return false; }
 type = b;
 }
 else if(byp_token_is(app, buffer, a, S8Lit("="))){
 if(!token_it_dec_non_whitespace(&it)){ return false; }
 Token *var = token_it_read(&it);
 if(var == 0 || var->kind != TokenBaseKind_Identifier){ return false; }
 if(!token_it_dec_non_whitespace(&it)){ return false; }
 Token *b = token_it_read(&it);
 if(b == 0 || b->kind != TokenBaseKind_Identifier){ return false; }
 type = b;
 }
 if(type == 0){ return false; }

 *out_type = push_token_lexeme(app, arena, buffer, type);
 *out_field_idx = (designator.size > 0) ? -1 : field_idx;
 *out_designator = designator;
 return true;
 }

 function Vec2_f32
 byp_draw_struct_tokens(Application_Links *app, Face_ID face, String_Const_u8 text, Token_Array tokens,
 b32 do_render, i32 highlight_field, String_Const_u8 highlight_name,
 Vec2_f32 origin)
 {
 Scratch_Block scratch(app);
 Face_Metrics metrics = get_face_metrics(app, face);
 f32 indent = 4.f*metrics.space_advance;
 f32 space = get_string_advance(app, face, string_u8_litexpr(" "));

 i32 named_field = -1;
 if(highlight_name.size > 0){
 i32 depth = 0, parens = 0, field = 0;
 Token_Iterator_Array it = token_iterator_pos(0, &tokens, 0);
 for(;;){
 Token *token = token_it_read(&it);
 if(token == 0){ break; }
 if(token->kind == TokenBaseKind_ScopeOpen){ depth += 1; }
 else if(token->kind == TokenBaseKind_ScopeClose){ depth -= 1; }
 else if(token->kind == TokenBaseKind_ParentheticalOpen){ parens += 1; }
 else if(token->kind == TokenBaseKind_ParentheticalClose){ parens -= 1; }
 else if(depth == 1 && parens == 0 && token->kind == TokenBaseKind_StatementClose){ field += 1; }
 else if(depth == 1 && parens == 0 && token->kind == TokenBaseKind_Identifier &&
 string_match(string_substring(text, Ii64(token)), highlight_name)){
 named_field = field;
 break;
 }
 if(!token_it_inc_all(&it)){ break; }
 }
 highlight_field = named_field;
 }

 Vec2_f32 p = origin;
 Vec2_f32 extent = {};
 i32 depth = 0, parens = 0, field = 0;
 b32 line_start = false;
 Token_Iterator_Array it = token_iterator_pos(0, &tokens, 0);
 for(;;){
 Token *token = token_it_read(&it);
 if(token == 0){ break; }

 if(token->kind == TokenBaseKind_Whitespace){
 String_Const_u8 ws = string_substring(text, Ii64(token));
 if(string_find_first(ws, '\n') < ws.size){
 p.x = origin.x;
 p.y += metrics.line_height;
 line_start = true;
 }
 else if(!line_start){
 p.x += space;
 }
 }
 else{
 if(token->kind == TokenBaseKind_ScopeClose){ depth -= 1; }
 if(line_start){
 p.x = origin.x + depth*indent;
 line_start = false;
 }

 ARGB_Color color = finalize_color(defcolor_text_default, 0);
 b32 highlight = (depth == 1 && parens == 0 && field == highlight_field &&
 (token->kind == TokenBaseKind_Identifier ||
 token->kind == TokenBaseKind_Operator ||
 token->kind == TokenBaseKind_Keyword));
 if(highlight){ color = finalize_color(defcolor_preproc, 0); }

 String_Const_u8 str = string_substring(text, Ii64(token));
 f32 advance = get_string_advance(app, face, str);
 if(do_render){
 draw_string(app, face, str, p, color);
 if(highlight){
 draw_rectangle(app, Rf32(p.x, p.y + metrics.line_height,
 p.x + advance, p.y + metrics.line_height + 2.f), 1.f, color);
 }
 }
 p.x += advance;
 extent.x = Max(extent.x, p.x - origin.x);

 if(token->kind == TokenBaseKind_ScopeOpen){ depth += 1; }
 else if(token->kind == TokenBaseKind_ParentheticalOpen){ parens += 1; }
 else if(token->kind == TokenBaseKind_ParentheticalClose){ parens -= 1; }
 else if(depth == 1 && parens == 0 && token->kind == TokenBaseKind_StatementClose){ field += 1; }
 }

 if(!token_it_inc_all(&it)){ break; }
 }
 extent.y = p.y - origin.y + metrics.line_height;
 return extent;
 }

 function void
 byp_draw_struct_context(Application_Links *app, Buffer_ID buffer, Token_Array *tokens, i64 pos,
 Face_ID face, f32 padding, Rect_f32 view_rect, Vec2_f32 *tooltip_position)
 {
 Scratch_Block scratch(app);
 String_Const_u8 type_name = {};
 String_Const_u8 designator = {};
 i32 field_idx = 0;
 if(!byp_struct_context_find(app, buffer, tokens, pos, &type_name, &designator, &field_idx, scratch)){ return; }

 Buffer_ID def_buffer = 0;
 Range_i64 def_range = {};
 if(!byp_code_index_find_struct(app, type_name, &def_buffer, &def_range)){ return; }

 String_Const_u8 def_string = push_buffer_range(app, scratch, def_buffer, def_range);
 Token_Array def_tokens = token_array_from_text(app, scratch, def_string);

 Vec2_f32 size = byp_draw_struct_tokens(app, face, def_string, def_tokens, false, field_idx, designator, V2f32(0, 0));

 Rect_f32 draw_rect = Rf32(tooltip_position->x, tooltip_position->y,
 tooltip_position->x + size.x + 2*padding,
 tooltip_position->y + size.y + 2*padding);
 if(draw_rect.x1 > view_rect.x1){
 f32 width = rect_width(draw_rect);
 draw_rect.x0 = (f32)(int)(view_rect.x1 - width);
 draw_rect.x1 = view_rect.x1;
 }
 if(draw_rect.y1 > view_rect.y1){
 f32 height = rect_height(draw_rect);
 draw_rect.y0 = (f32)(int)(view_rect.y1 - height);
 draw_rect.y1 = view_rect.y1;
 }

 draw_rectangle_fcolor(app, draw_rect, 3.f, fcolor_id(defcolor_back));
 draw_rectangle_outline_fcolor(app, draw_rect, 3.f, 2.f, fcolor_id(defcolor_ghost_character));

 byp_draw_struct_tokens(app, face, def_string, def_tokens, true, field_idx, designator,
 V2f32(draw_rect.x0 + padding, draw_rect.y0 + padding));

 tooltip_position->y += draw_rect.y1 - draw_rect.y0;
}
