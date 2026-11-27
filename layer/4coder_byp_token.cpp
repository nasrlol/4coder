
function b32
byp_highlight_token(Token_Base_Kind kind){
 switch(kind){
 case TokenBaseKind_Keyword:
 //case TokenBaseKind_Preprocessor:
 case TokenBaseKind_Identifier:
 case byp_TokenKind_Primitive:
 case byp_TokenKind_ControlFlow:
 case byp_TokenKind_Struct:
 case byp_TokenKind_TypeKeyword:
 return true;
 }
 return false;
 }


 function ARGB_Color
 byp_get_token_color_cpp(Token token){
 Managed_ID color = defcolor_text_default;
 switch (token.kind){
 case TokenBaseKind_Preprocessor:{ color = defcolor_preproc; }break;
 case TokenBaseKind_Keyword:{ color = defcolor_keyword; }break;
 case TokenBaseKind_Comment:{ color = defcolor_comment; }break;
 case TokenBaseKind_LiteralString:{ color = defcolor_str_constant; }break;
 case TokenBaseKind_LiteralInteger:{ color = defcolor_int_constant; }break;
 case TokenBaseKind_LiteralFloat:{ color = defcolor_float_constant; }break;

 case TokenBaseKind_Operator:
 case TokenBaseKind_ScopeOpen:
 case TokenBaseKind_ScopeClose:
 case TokenBaseKind_ParentheticalOpen:
 case TokenBaseKind_ParentheticalClose:
 case TokenBaseKind_StatementClose:{ color = defcolor_non_text; } break;

 case byp_TokenKind_ControlFlow:{ color = defcolor_control; }break;
 case byp_TokenKind_Primitive:{ color = defcolor_primitive; }break;
 case byp_TokenKind_Struct:
 case byp_TokenKind_TypeKeyword:{ color = defcolor_struct; }break;
 }
 // specifics override generals
 switch (token.sub_kind){
 case TokenCppKind_LiteralTrue:
 case TokenCppKind_LiteralFalse:
 { color = defcolor_bool_constant; }break;

 case TokenCppKind_LiteralCharacter:
 case TokenCppKind_LiteralCharacterWide:
 case TokenCppKind_LiteralCharacterUTF8:
 case TokenCppKind_LiteralCharacterUTF16:
 case TokenCppKind_LiteralCharacterUTF32:
 { color = defcolor_char_constant; }break;

 case TokenCppKind_PPIncludeFile:
 { color = defcolor_include; }break;
 }
 return fcolor_resolve(fcolor_id(color));
 }

 function void
 byp_draw_cpp_token_colors(Application_Links *app, Text_Layout_ID text_layout_id, Token_Array *array){
 Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);
 i64 first_index = token_index_from_pos(array, visible_range.first);
 Token_Iterator_Array it = token_iterator_index(0, array, first_index);
 for(;;){
 Token *token = token_it_read(&it);
 if(token->pos >= visible_range.one_past_last){ break; }
 ARGB_Color argb = byp_get_token_color_cpp(*token);
 paint_text_color(app, text_layout_id, Ii64_size(token->pos, token->size), argb);
 if(!token_it_inc_all(&it)){ break; }
 }
 }

//~ NOTE(nasr): dim the dead branch of `#if 0` (up to its `#else` / `#elif` / `#endif`)
// The token array is scanned once per edit/relex (no API calls in the loop, just the array),
// the result is cached per buffer in a few slots, and drawing only checks the visible tokens.

#define BYP_IF0_SLOTS 8

struct Byp_If0_Cache{
 Buffer_ID buffer;
 Token *tokens;
 i64 token_count;
 History_Record_Index history;
 i32 count;
 i32 cap;
 Range_i64 *ranges; // heap, grows as needed (used to be a fixed 64 that silently stopped)
};
global Byp_If0_Cache byp_if0_cache[BYP_IF0_SLOTS];
global i32 byp_if0_cache_next;

function void
byp_if0_push(Byp_If0_Cache *slot, Range_i64 range){
 if(slot->count == slot->cap){
  Base_Allocator *allocator = get_base_allocator_system();
  i32 new_cap = (slot->cap == 0 ? 64 : slot->cap*2);
  Range_i64 *new_ranges = base_array(allocator, Range_i64, new_cap);
  if(slot->count > 0){ block_copy(new_ranges, slot->ranges, slot->count*sizeof(Range_i64)); }
  base_free(allocator, slot->ranges);
  slot->ranges = new_ranges;
  slot->cap = new_cap;
 }
 slot->ranges[slot->count++] = range;
}

// `#if 0` exactly: the condition is a lone `0` (a trailing comment is fine, `#if 0 || X` isn't)
function b32
byp_token_is_if0(Application_Links *app, Buffer_ID buffer, Token_Array *array, i64 if_index){
 i64 j = if_index + 1;
 while(j < array->count && array->tokens[j].kind == TokenBaseKind_Whitespace){ j += 1; }
 if(j >= array->count){ return false; }
 Token *zero = array->tokens + j;
 if(zero->kind != TokenBaseKind_LiteralInteger || zero->size != 1 ||
    buffer_get_char(app, buffer, zero->pos) != '0'){ return false; }
 i64 k = j + 1;
 while(k < array->count && array->tokens[k].kind == TokenBaseKind_Whitespace){ k += 1; }
 if(k >= array->count){ return true; }
 Token *next = array->tokens + k;
 return(next->kind == TokenBaseKind_Comment || !HasFlag(next->flags, TokenBaseFlag_PreprocessorBody));
}

function Byp_If0_Cache*
byp_if0_ranges(Application_Links *app, Buffer_ID buffer, Token_Array *array){
 History_Record_Index history = buffer_history_get_current_state_index(app, buffer);
 Byp_If0_Cache *slot = 0;
 for(i32 i = 0; i < BYP_IF0_SLOTS; i += 1){
  Byp_If0_Cache *c = byp_if0_cache + i;
  if(c->buffer == buffer){
   if(c->tokens == array->tokens && c->token_count == array->count && c->history == history){ return c; }
   slot = c;
   break;
  }
 }
 if(slot == 0){
  slot = byp_if0_cache + byp_if0_cache_next;
  byp_if0_cache_next = (byp_if0_cache_next + 1) % BYP_IF0_SLOTS;
 }
 slot->buffer = buffer;
 slot->tokens = array->tokens;
 slot->token_count = array->count;
 slot->history = history;
 slot->count = 0;

 // depth > 0 while inside a dead branch; nested #if/#ifdef inside it only count up/down
 i32 depth = 0;
 i64 start = 0;
 for(i64 i = 0; i < array->count; i += 1){
  Token *token = array->tokens + i;
  if(token->kind != TokenBaseKind_Preprocessor){ continue; }
  switch(token->sub_kind){
   case TokenCppKind_PPIf: case TokenCppKind_PPIfDef: case TokenCppKind_PPIfNDef:{
    if(depth > 0){ depth += 1; }
    else if(token->sub_kind == TokenCppKind_PPIf && byp_token_is_if0(app, buffer, array, i)){
     depth = 1;
     start = token->pos + token->size;
    }
   }break;
   case TokenCppKind_PPElse: case TokenCppKind_PPElIf:{
    if(depth == 1){ byp_if0_push(slot, Ii64(start, token->pos)); depth = 0; }
   }break;
   case TokenCppKind_PPEndIf:{
    if(depth == 1){ byp_if0_push(slot, Ii64(start, token->pos)); depth = 0; }
    else if(depth > 1){ depth -= 1; }
   }break;
  }
 }
 // unterminated `#if 0`: dead to the end of the buffer
 if(depth > 0 && array->count > 0){
  Token *last = array->tokens + array->count - 1;
  byp_if0_push(slot, Ii64(start, last->pos + last->size));
 }
 return slot;
}

 function void byp_draw_token_colors(Application_Links *app, View_ID view, Buffer_ID buffer, Text_Layout_ID text_layout_id){
 Token_Array token_array = get_token_array_from_buffer(app, buffer);
 Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);

 Scratch_Block scratch(app);
 byp_draw_cpp_token_colors(app, text_layout_id, &token_array);

 i64 cursor_index = token_index_from_pos(&token_array, view_get_cursor_pos(app, view));
 Token_Iterator_Array it = token_iterator_index(0, token_array.tokens, token_array.count, cursor_index);
 Token *cursor_token = token_it_read(&it);
 b32 do_cursor_tok_highlight = byp_highlight_token(cursor_token->kind);

 String_Const_u8 token_string = {};
 Rect_f32 cursor_tok_rect = {};
 Vec2_f32 tok_rect_dim = {};

 if(do_cursor_tok_highlight){
 token_string = push_token_lexeme(app, scratch, buffer, cursor_token);
 cursor_tok_rect = text_layout_character_on_screen(app, text_layout_id, cursor_token->pos);
 tok_rect_dim = V2f32(cursor_token->size*rect_width(cursor_tok_rect), 2.f);
 cursor_tok_rect = Rf32_xy_wh(V2f32(cursor_tok_rect.x0, cursor_tok_rect.y1 - 2.f), tok_rect_dim);
 }

 ARGB_Color cl_func   = fcolor_resolve(fcolor_id(defcolor_function));
 ARGB_Color cl_type   = fcolor_resolve(fcolor_id(defcolor_type));
 ARGB_Color cl_macro  = fcolor_resolve(fcolor_id(defcolor_macro));
 ARGB_Color cl_enum   = fcolor_resolve(fcolor_id(defcolor_enum));
 ARGB_Color cl_global = fcolor_resolve(fcolor_id(defcolor_global));
 ARGB_Color cl_back   = fcolor_resolve(fcolor_id(defcolor_back));
 ARGB_Color cursor_tok_color = byp_get_token_color_cpp(*cursor_token);

 if(cursor_token->kind == TokenBaseKind_Identifier){
 String_Const_u8 lexeme = push_token_lexeme(app, scratch, buffer, cursor_token);
 Code_Index_Note *note = code_index_note_from_string(lexeme);

 if(note != 0){
 switch(note->note_kind){
 case CodeIndexNote_Function: cursor_tok_color = cl_func;   break;
 case CodeIndexNote_Type:     cursor_tok_color = cl_type;   break;
 case CodeIndexNote_Macro:    cursor_tok_color = cl_macro;  break;
 case CodeIndexNote_Enum:     cursor_tok_color = cl_enum;   break;
 case CodeIndexNote_Global:   cursor_tok_color = cl_global; break;
 }
 }
 }

 // NOTE(nasr): comment keywords (NOTE/TODO/...) are painted by byp_draw_comments, which runs
 // right after this with a superset of the keywords; doing NOTE/TODO here too scanned every
 // visible comment twice per frame

 // dead `#if 0` branches (C family only: other lexers reuse the sub_kind numbers)
 Byp_If0_Cache *if0 = 0;
 i32 if0_index = 0;
 {
 Nasr_Language lang = nasr_buffer_language(app, buffer);
 if((lang == NasrLang_C || lang == NasrLang_Cpp || lang == NasrLang_CHeader) && token_array.tokens != 0){
 if0 = byp_if0_ranges(app, buffer, &token_array);
 if(if0->count == 0){ if0 = 0; }
 }
 }

 it = token_iterator_pos(0, &token_array, Max(0, visible_range.first-1));
 for(;;){
 Token *token = token_it_read(&it);
 if(token->pos > visible_range.max){ break; }
 ARGB_Color argb = byp_get_token_color_cpp(*token);
 String_Const_u8 lexeme = push_token_lexeme(app, scratch, buffer, token);
 Code_Index_Note *note = code_index_note_from_string(lexeme);

 if(do_cursor_tok_highlight && string_match(lexeme, token_string)){
 Rect_f32 cur_tok_rect = text_layout_character_on_screen(app, text_layout_id, token->pos);
 cur_tok_rect = Rf32_xy_wh(V2f32(cur_tok_rect.x0, cur_tok_rect.y1 - 2.f), tok_rect_dim);
 draw_rectangle(app, cur_tok_rect, 5.f, argb_color_blend(cursor_tok_color, 0.7f, cl_back));
 }

 if(note != 0){
 switch(note->note_kind){
 case CodeIndexNote_Function: argb = cl_func;   break;
 case CodeIndexNote_Type:     argb = cl_type;   break;
 case CodeIndexNote_Macro:    argb = cl_macro;  break;
 case CodeIndexNote_Enum:     argb = cl_enum;   break;
 case CodeIndexNote_Global:   argb = cl_global; break;
 }
 }

 // ranges are sorted and tokens come in order, so the index only moves forward
 if(if0 != 0){
 while(if0_index < if0->count && if0->ranges[if0_index].max <= token->pos){ if0_index += 1; }
 if(if0_index < if0->count && if0->ranges[if0_index].min <= token->pos){
 argb = argb_color_blend(argb, 0.6f, cl_back);
 }
 }

 paint_text_color(app, text_layout_id, Ii64(token), argb);
 if(!token_it_inc_non_whitespace(&it)){ break; }
 }
 if(do_cursor_tok_highlight){ draw_rectangle(app, cursor_tok_rect, 5.f, cursor_tok_color); }

}
