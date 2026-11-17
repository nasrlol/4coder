// NOTE(nasr): Region / multi-line editing built on the emacs-style mark.
// The "region" is every line from the cursor line to the mark line (inclusive), or for `im`/`am`
// the exact cursor<->mark span. See the summary at the bottom of each section for the keys.
//
//  operator + r + SPEC        apply the operator to SPEC on every line of the region
//    SPEC = i<obj> / a<obj>   text object (" ' ( [ { w W , ; _ - ...)
//           f<c> t<c> F<c> T<c>  find-char motions starting at the cursor's column
//           $  0  ^           to line end / line start / first non-blank, from the cursor's column
//           *                 every whole-word occurrence of the word under the cursor
//           n                 every occurrence of the last search pattern
//           A  I              (change only) insertion point at line end / first non-blank
//    y: pieces go into the register as lines (linewise), d: deleted (+ register),
//    c: pieces cleared and typing goes into all of them at once (Esc/Return finishes),
//    other operators (gU g~ > < =) run on each piece.
//  im / am                    text object: cursor<->mark charwise / the whole lines it spans
//  gp SPEC                    put register lines into SPEC on each line (1 line = every line)
//  gx i<obj>                  swap the object at the cursor with the same object at the mark
//  gs / gS SPEC               sort region lines / sort them by SPEC's text on each line
//  ga<c>                      align the first <c> on every region line
//  g Ctrl-A                   add 0,1,2,... (x count) to the first number on each region line
//  g@<reg>                    run vim macro <reg> on every region line (q/@ record/play macros)
//  zn / zN                    narrow: fold everything outside the region / widen again
// Every edit is a single undo step.

//~ Multi-insert (typing into several places at once)

struct Nasr_Vim_Multi_Insert{
 b32 active;
 Buffer_ID buffer;
 History_Group history;
 i64 *positions;    // insertion points, ascending
 i32 count;
 i32 cursor_index;  // which insertion point the view cursor follows
 i32 *typed;        // byte length of each text insert, for backspace
 i32 typed_count;
 i32 typed_cap;
};

global Nasr_Vim_Multi_Insert nasr_vim_multi;
global Arena nasr_vim_multi_arena;
global b32 nasr_vim_multi_arena_init;

CUSTOM_ID(attachment, buffer_nasr_narrowed);

//~ Helpers

function b32
nasr_vim_object_is_quote(u8 object){
 return object == '"' || object == '\'';
}

function Vim_Text_Object *
nasr_vim_find_text_object(u8 object){
 Vim_Text_Object *result = 0;
 foreach(i, ArrayCount(vim_text_object_vtable)){
  Vim_Text_Object *text_object = vim_text_object_vtable + i;
  if(text_object->character != 0 && text_object->character == object){
   result = text_object;
   break;
  }
 }
 return result;
}

struct Nasr_Vim_Region_Lines{
 i64 min;
 i64 max;
 i64 cursor_line;
 i64 cursor_pos;
 i64 mark_pos;
};

function Nasr_Vim_Region_Lines
nasr_vim_region_lines(Application_Links *app, View_ID view, Buffer_ID buffer){
 Nasr_Vim_Region_Lines result = {};
 result.cursor_pos = view_get_cursor_pos(app, view);
 result.mark_pos = view_get_mark_pos(app, view);
 result.cursor_line = get_line_number_from_pos(app, buffer, result.cursor_pos);
 i64 mark_line = get_line_number_from_pos(app, buffer, result.mark_pos);
 result.min = Min(result.cursor_line, mark_line);
 result.max = Max(result.cursor_line, mark_line);
 return result;
}

// Half-open range of a text object at `anchor` (any size). Leaves vim_state.params untouched.
function b32
nasr_vim_object_range(Application_Links *app, Buffer_ID buffer, i64 anchor,
                      u8 object, Vim_Clusivity clusivity, Range_i64 *out){
 b32 inner = (clusivity == VIM_Exclusive);
 Range_i64 range = {};
 b32 valid = false;

 Vim_Params saved = vim_state.params;
 vim_state.params.seek.character = object;

 Vim_Text_Object *text_object = nasr_vim_find_text_object(object);
 if(text_object){
  vim_state.params.clusivity = clusivity;
  Range_i64 r = text_object->func(app, buffer, anchor);
  if((r.min != 0 || r.max != 0) && r.min <= r.max){
   range = Ii64(r.min, r.max + 1);
   valid = true;
  }
 }
 else if(vim_character_can_bounce(object)){
  u8 c = buffer_get_char(app, buffer, anchor);
  u8 other = vim_corresponding_bounce(object);
  Range_i64 r = {};
  if(c == object || c == other){
   r = Ii64(vim_bounce_pair(app, buffer, anchor, c), anchor);
  }else{
   r = Ii64(vim_bounce_pair(app, buffer, anchor, object),
            vim_bounce_pair(app, buffer, anchor, other));
  }
  u8 open = buffer_get_char(app, buffer, r.min);
  u8 close = buffer_get_char(app, buffer, r.max);
  if(r.min < r.max && vim_character_can_bounce(open) &&
     vim_bounce_direction(open) == Scan_Forward && close == vim_corresponding_bounce(open)){
   range = inner ? Ii64(r.min + 1, r.max) : Ii64(r.min, r.max + 1);
   valid = true;
  }
 }
 else if(nasr_vim_object_is_quote(object)){
  vim_state.params.clusivity = VIM_Inclusive;
  Range_i64 r = vim_scan_object_quotes(app, buffer, anchor);
  if(r.min < r.max &&
     buffer_get_char(app, buffer, r.min) == object &&
     buffer_get_char(app, buffer, r.max) == object){
   range = inner ? Ii64(r.min + 1, r.max) : Ii64(r.min, r.max + 1);
   valid = true;
  }
 }

 vim_state.params = saved;
 if(valid){ *out = range; }
 return valid;
}

// Where to look for the object on a line other than the cursor line.
function b32
nasr_vim_line_anchor(Application_Links *app, Buffer_ID buffer, i64 line, u8 object, i64 *anchor_out){
 i64 line_start = get_line_start_pos(app, buffer, line);
 i64 line_end = get_line_end_pos(app, buffer, line);
 if(line_end <= line_start){ return false; }

 if(nasr_vim_find_text_object(object) == 0){
  u8 opener = object;
  if(vim_character_can_bounce(object) && vim_bounce_direction(object) == Scan_Backward){
   opener = vim_corresponding_bounce(object);
  }
  if(nasr_vim_object_is_quote(object) || vim_character_can_bounce(object)){
   Scratch_Block scratch(app);
   String_Const_u8 text = push_buffer_range(app, scratch, buffer, Ii64(line_start, line_end));
   for(u64 i = 0; i < text.size; i += 1){
    if(text.str[i] == opener){
     *anchor_out = line_start + i;
     return true;
    }
   }
   return false;
  }
 }

 if(line_is_valid_and_blank(app, buffer, line)){ return false; }
 *anchor_out = get_pos_past_lead_whitespace_from_line_number(app, buffer, line);
 return true;
}

function b32
nasr_vim_is_word_char(u8 c){
 return character_is_alpha_numeric(c);
}

//~ Specs: what to pick on each line of the region

struct Nasr_Vim_Spec{
 u8 kind;     // 'i' 'a' 'f' 't' 'F' 'T' '$' '0' '^' '*' 'n' 'A' 'I'
 u8 arg;      // object or seek character
};

// Reads the SPEC keys from the user. Returns false (and nothing else) on Esc / unknown key.
function b32
nasr_vim_query_spec(Application_Links *app, Nasr_Vim_Spec *spec, b32 allow_insert_points){
 u8 kind = vim_query_user_key(app, string_u8_litexpr("-- REGION: i a f t F T $ 0 ^ * n A I --"));
 switch(kind){
  case 'i': case 'a':{
   u8 object = vim_query_user_key(app, string_u8_litexpr("-- REGION TEXT OBJECT --"));
   if(object == 0){ return false; }
   *spec = {kind, object};
   return true;
  }
  case 'f': case 't': case 'F': case 'T':{
   u8 c = vim_query_user_key(app, string_u8_litexpr("-- REGION SEEK CHAR --"));
   if(c == 0){ return false; }
   *spec = {kind, c};
   return true;
  }
  case '$': case '0': case '^': case '*': case 'n':{
   *spec = {kind, 0};
   return true;
  }
  case 'A': case 'I':{
   if(!allow_insert_points){ return false; }
   *spec = {kind, 0};
   return true;
  }
 }
 return false;
}

function b32
nasr_vim_spec_is_per_line(Nasr_Vim_Spec spec){
 return spec.kind != '*' && spec.kind != 'n';
}

// The range for a per-line spec on `line`. `column` is the cursor's byte column.
function b32
nasr_vim_spec_range_on_line(Application_Links *app, Buffer_ID buffer, Nasr_Vim_Region_Lines *region,
                            i64 line, i64 column, Nasr_Vim_Spec spec, Range_i64 *out){
 i64 line_start = get_line_start_pos(app, buffer, line);
 i64 line_end = get_line_end_pos(app, buffer, line);

 switch(spec.kind){
  case 'i': case 'a':{
   Vim_Clusivity clusivity = (spec.kind == 'i' ? VIM_Exclusive : VIM_Inclusive);
   i64 anchor = region->cursor_pos;
   if(line != region->cursor_line && !nasr_vim_line_anchor(app, buffer, line, spec.arg, &anchor)){
    return false;
   }
   Range_i64 range = {};
   if(nasr_vim_object_range(app, buffer, anchor, spec.arg, clusivity, &range) &&
      line_start <= range.min && range.max <= line_end){
    *out = range;
    return true;
   }
   return false;
  }

  case 'A':{
   if(line_is_valid_and_blank(app, buffer, line)){ return false; }
   *out = Ii64(line_end);
   return true;
  }
  case 'I':{
   if(line_is_valid_and_blank(app, buffer, line)){ return false; }
   *out = Ii64(get_pos_past_lead_whitespace_from_line_number(app, buffer, line));
   return true;
  }
 }

 // column based motions; lines shorter than the cursor column are skipped
 i64 anchor = line_start + column;
 if(anchor > line_end){ return false; }

 switch(spec.kind){
  case '$':{ *out = Ii64(anchor, line_end); return true; }
  case '0':{ *out = Ii64(line_start, anchor); return true; }
  case '^':{
   i64 first = get_pos_past_lead_whitespace_from_line_number(app, buffer, line);
   *out = Ii64(first, anchor);
   return true;
  }
  case 'f': case 't':{
   for(i64 p = anchor + 1; p < line_end; p += 1){
    if(buffer_get_char(app, buffer, p) == spec.arg){
     *out = Ii64(anchor, (spec.kind == 'f' ? p + 1 : p));
     return true;
    }
   }
   return false;
  }
  case 'F': case 'T':{
   for(i64 p = anchor - 1; p >= line_start; p -= 1){
    if(buffer_get_char(app, buffer, p) == spec.arg){
     *out = Ii64((spec.kind == 'F' ? p : p + 1), anchor);
     return true;
    }
   }
   return false;
  }
 }
 return false;
}

// Collects the spec's ranges over the region, ascending and non-overlapping.
// `lines_out` (optional) receives the line of each range.
function i32
nasr_vim_collect(Application_Links *app, Arena *arena, View_ID view, Buffer_ID buffer, Nasr_Vim_Spec spec,
                 Range_i64 **ranges_out, i64 **lines_out, i32 *cursor_index_out){
 Nasr_Vim_Region_Lines region = nasr_vim_region_lines(app, view, buffer);
 i64 column = region.cursor_pos - get_line_start_pos(app, buffer, region.cursor_line);
 i32 count = 0;
 i32 cursor_index = 0;
 Range_i64 *ranges = 0;
 i64 *lines = 0;

 if(nasr_vim_spec_is_per_line(spec)){
  i64 cap = region.max - region.min + 1;
  ranges = push_array(arena, Range_i64, cap);
  lines = push_array(arena, i64, cap);
  for(i64 line = region.min; line <= region.max; line += 1){
   Range_i64 range = {};
   if(nasr_vim_spec_range_on_line(app, buffer, &region, line, column, spec, &range)){
    if(line == region.cursor_line){ cursor_index = count; }
    lines[count] = line;
    ranges[count++] = range;
   }
  }
 }else{
  // occurrences of a string inside the region lines
  String_Const_u8 needle = {};
  b32 whole_word = false;
  if(spec.kind == '*'){
   i64 a = region.cursor_pos;
   i64 b = region.cursor_pos;
   if(!nasr_vim_is_word_char(buffer_get_char(app, buffer, a))){ return 0; }
   while(a > 0 && nasr_vim_is_word_char(buffer_get_char(app, buffer, a - 1))){ a -= 1; }
   while(nasr_vim_is_word_char(buffer_get_char(app, buffer, b))){ b += 1; }
   needle = push_buffer_range(app, arena, buffer, Ii64(a, b));
   whole_word = true;
  }else{
   needle = push_string_copy(arena, vim_registers.search.data.string);
  }
  if(needle.size == 0){ return 0; }

  Range_i64 span = Ii64(get_line_start_pos(app, buffer, region.min), get_line_end_pos(app, buffer, region.max));
  String_Const_u8 text = push_buffer_range(app, arena, buffer, span);
  i64 cap = i64(text.size/needle.size) + 1;
  ranges = push_array(arena, Range_i64, cap);
  lines = push_array(arena, i64, cap);
  b32 found_cursor = false;
  for(u64 i = 0; i + needle.size <= text.size;){
   if(string_match(SCu8(text.str + i, needle.size), needle) &&
      (!whole_word ||
       ((i == 0 || !nasr_vim_is_word_char(text.str[i - 1])) &&
        (i + needle.size == text.size || !nasr_vim_is_word_char(text.str[i + needle.size]))))){
    Range_i64 range = Ii64_size(span.min + i, needle.size);
    if(!found_cursor && range.min <= region.cursor_pos && region.cursor_pos <= range.max){
     cursor_index = count;
     found_cursor = true;
    }
    lines[count] = get_line_number_from_pos(app, buffer, range.min);
    ranges[count++] = range;
    i += needle.size;
   }else{
    i += 1;
   }
  }
 }

 *ranges_out = ranges;
 if(lines_out){ *lines_out = lines; }
 if(cursor_index_out){ *cursor_index_out = cursor_index; }
 return count;
}

//~ Registers

function Vim_Register *
nasr_vim_target_register(void){
 Vim_Register *reg = vim_state.params.selected_reg;
 if(reg == 0){ reg = &vim_registers.VIM_DEFAULT_REGISTER; }
 return reg;
}

function void
nasr_vim_multi_set_register(Application_Links *app, Buffer_ID buffer, Range_i64 *ranges, i32 count){
 Vim_Register *reg = vim_state.params.selected_reg;
 if(reg == 0 || count == 0){ return; }
 if(reg->flags & REGISTER_ReadOnly){
  Scratch_Block scratch(app);
  vim_set_bottom_text(push_stringf(scratch, "Register %c is Read Only", vim_get_register_char(reg)));
  return;
 }

 Scratch_Block scratch(app);
 List_String_Const_u8 list = {};
 for(i32 i = 0; i < count; i += 1){
  string_list_push(scratch, &list, push_buffer_range(app, scratch, buffer, ranges[i]));
  string_list_push(scratch, &list, string_u8_litexpr("\n"));
  buffer_post_fade(app, buffer, 0.667f, ranges[i], fcolor_resolve(fcolor_id(defcolor_paste)));
 }
 String_Const_u8 joined = string_list_flatten(scratch, list);

 if(!vim_register_copy(reg, joined)){ return; }
 reg->edit_type = EDIT_LineWise;

 if(vim_state.params.request == REQUEST_Yank){
  if(reg != &vim_registers.yank){ vim_register_copy(&vim_registers.yank, reg); }
 }else{
  vim_push_reg_cycle(app);
  vim_register_copy(vim_registers.cycle, reg);
 }
 if(reg == &vim_registers.system){ clipboard_post(0, reg->data.string); }
 vim_update_registers(app);
}

function void
nasr_vim_multi_finish_command(void){
 if(vim_state.params.selected_reg){
  vim_state.params.selected_reg->flags &= (~REGISTER_Append);
 }
 vim_reset_state();
}

//~ Multi-insert

function void
nasr_vim_multi_insert_stop(Application_Links *app){
 if(!nasr_vim_multi.active){ return; }
 nasr_vim_multi.active = false;
 history_group_end(nasr_vim_multi.history);

 View_ID view = get_active_view(app, Access_ReadVisible);
 if(view_get_buffer(app, view, Access_ReadVisible) == nasr_vim_multi.buffer){
  i64 pos = nasr_vim_multi.positions[nasr_vim_multi.cursor_index];
  i64 line_start = get_line_start_pos_from_pos(app, nasr_vim_multi.buffer, pos);
  view_set_cursor_and_preferred_x(app, view, seek_pos(Max(line_start, pos - 1)));
 }
 vim_reset_state();
}

function void
nasr_vim_multi_sync_cursor(Application_Links *app){
 View_ID view = get_active_view(app, Access_ReadVisible);
 if(view_get_buffer(app, view, Access_ReadVisible) == nasr_vim_multi.buffer){
  view_set_cursor_and_preferred_x(app, view, seek_pos(nasr_vim_multi.positions[nasr_vim_multi.cursor_index]));
 }
}

function void
nasr_vim_multi_insert_text(Application_Links *app, String_Const_u8 text){
 if(text.size == 0){ return; }
 if(nasr_vim_multi.typed_count == nasr_vim_multi.typed_cap){ return; }
 for(i32 i = nasr_vim_multi.count - 1; i >= 0; i -= 1){
  buffer_replace_range(app, nasr_vim_multi.buffer, Ii64(nasr_vim_multi.positions[i]), text);
 }
 for(i32 i = 0; i < nasr_vim_multi.count; i += 1){
  nasr_vim_multi.positions[i] += i64(text.size)*(i + 1);
 }
 nasr_vim_multi.typed[nasr_vim_multi.typed_count++] = i32(text.size);
 nasr_vim_multi_sync_cursor(app);
}

function b32
nasr_vim_multi_backspace(Application_Links *app){
 if(nasr_vim_multi.typed_count == 0){ return false; }
 i64 size = nasr_vim_multi.typed[--nasr_vim_multi.typed_count];
 for(i32 i = nasr_vim_multi.count - 1; i >= 0; i -= 1){
  i64 pos = nasr_vim_multi.positions[i];
  buffer_replace_range(app, nasr_vim_multi.buffer, Ii64(pos - size, pos), string_u8_empty);
 }
 for(i32 i = 0; i < nasr_vim_multi.count; i += 1){
  nasr_vim_multi.positions[i] -= size*(i + 1);
 }
 nasr_vim_multi_sync_cursor(app);
 return true;
}

// Called first thing from vim_handle_input while a multi-line change is being typed.
function b32
nasr_vim_handle_multi_insert(Application_Links *app, Input_Event *event){
 View_ID view = get_active_view(app, Access_ReadVisible);
 if(view_get_buffer(app, view, Access_ReadVisible) != nasr_vim_multi.buffer){
  nasr_vim_multi_insert_stop(app);
  return false;
 }

 if(event->kind == InputEventKind_KeyStroke){
  Key_Code code = event->key.code;
  if(code == KeyCode_Escape || code == KeyCode_Return){
   nasr_vim_multi_insert_stop(app);
   return true;
  }
  if(code == KeyCode_Backspace){
   if(has_modifier(event, KeyCode_Control)){
    // delete back to the previous word boundary, but never past what was typed
    b32 clearing_whitespace = true;
    while(nasr_vim_multi.typed_count > 0){
     u8 c = buffer_get_char(app, nasr_vim_multi.buffer, nasr_vim_multi.positions[0] - 1);
     if(!clearing_whitespace && character_is_whitespace(c)){ break; }
     if(clearing_whitespace && !character_is_whitespace(c)){ clearing_whitespace = false; }
     nasr_vim_multi_backspace(app);
    }
   }else{
    nasr_vim_multi_backspace(app);
   }
   return true;
  }
  // Plain keys must stay "unhandled" so their text event still arrives; ctrl chords are
  // swallowed. Either way the keystroke itself does nothing (same trick as visual insert).
  b32 result = has_modifier(event, KeyCode_Control);
  event->kind = InputEventKind_None;
  return result;
 }

 if(event->kind == InputEventKind_TextInsert){
  String_Const_u8 text = event->text.string;
  if(text.size > 0 && text.str[0] != '\n' && text.str[0] != '\r'){
   nasr_vim_multi_insert_text(app, text);
  }
  return true;
 }

 return false;
}

function void
nasr_vim_multi_insert_start(Application_Links *app, Buffer_ID buffer, History_Group history,
                            Range_i64 *points, i32 count, i32 cursor_index){
 if(!nasr_vim_multi_arena_init){
  nasr_vim_multi_arena = make_arena_system();
  nasr_vim_multi_arena_init = true;
 }
 linalloc_clear(&nasr_vim_multi_arena);

 nasr_vim_multi.buffer = buffer;
 nasr_vim_multi.history = history;
 nasr_vim_multi.count = count;
 nasr_vim_multi.cursor_index = cursor_index;
 nasr_vim_multi.positions = push_array(&nasr_vim_multi_arena, i64, count);
 for(i32 i = 0; i < count; i += 1){ nasr_vim_multi.positions[i] = points[i].min; }
 nasr_vim_multi.typed_cap = 4096;
 nasr_vim_multi.typed = push_array(&nasr_vim_multi_arena, i32, nasr_vim_multi.typed_cap);
 nasr_vim_multi.typed_count = 0;
 nasr_vim_multi.active = true;
 vim_state.mode = VIM_Insert;
 nasr_vim_multi_sync_cursor(app);
}

//~ operator + r + SPEC

function void
nasr_vim_multiline_object(Application_Links *app){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 Vim_Request_Type request = vim_state.params.request;

 Nasr_Vim_Spec spec = {};
 if(!nasr_vim_query_spec(app, &spec, request == REQUEST_Change)){ nasr_vim_multi_finish_command(); return; }

 Scratch_Block scratch(app);
 Range_i64 *ranges = 0;
 i32 cursor_index = 0;
 i32 count = nasr_vim_collect(app, scratch, view, buffer, spec, &ranges, 0, &cursor_index);

 // empty pieces only make sense as insertion points for a change
 if(request != REQUEST_Change){
  i32 kept = 0;
  i32 new_cursor_index = 0;
  for(i32 i = 0; i < count; i += 1){
   if(range_size(ranges[i]) == 0){ continue; }
   if(i <= cursor_index){ new_cursor_index = kept; }
   ranges[kept++] = ranges[i];
  }
  count = kept;
  cursor_index = Min(new_cursor_index, Max(0, count - 1));
 }

 if(count == 0 || request == REQUEST_None){ nasr_vim_multi_finish_command(); return; }

 vim_push_jump(app, view);
 History_Group history = history_group_begin(app, buffer);

 switch(request){
  case REQUEST_Yank:{
   nasr_vim_multi_set_register(app, buffer, ranges, count);
   view_set_cursor_and_preferred_x(app, view, seek_pos(ranges[cursor_index].min));
  }break;

  case REQUEST_Delete:
  case REQUEST_Change:{
   b32 any_text = false;
   for(i32 i = 0; i < count; i += 1){ any_text |= (range_size(ranges[i]) > 0); }
   if(any_text){ nasr_vim_multi_set_register(app, buffer, ranges, count); }
   for(i32 i = count - 1; i >= 0; i -= 1){
    buffer_replace_range(app, buffer, ranges[i], string_u8_empty);
   }
   // where each piece starts once everything above it has been removed
   i64 removed = 0;
   for(i32 i = 0; i < count; i += 1){
    i64 size = range_size(ranges[i]);
    ranges[i].min -= removed;
    ranges[i].max = ranges[i].min;
    removed += size;
   }
   view_set_cursor_and_preferred_x(app, view, seek_pos(ranges[cursor_index].min));
  }break;

  default:{
   // any other operator (gU, g~, >, =, ...) just runs on each piece
   for(i32 i = count - 1; i >= 0; i -= 1){
    vim_request_vtable[request](app, view, buffer, ranges[i]);
   }
  }break;
 }

 if(request != REQUEST_Change){
  history_group_end(history);
  nasr_vim_multi_finish_command();
  return;
 }

 nasr_vim_multi_finish_command();
 nasr_vim_multi_insert_start(app, buffer, history, ranges, count, cursor_index);
}

VIM_COMMAND_SIG(nasr_vim_modal_r){
 if(vim_state.mode == VIM_Normal && vim_state.params.request != REQUEST_None){
  nasr_vim_multiline_object(app);
 }else{
  vim_replace_next_char(app);
 }
}

//~ im / am : the cursor<->mark region as a text object

// im: cursor..mark exactly like an emacs region (mark side exclusive); am: the whole lines.
VIM_TEXT_OBJECT_SIG(nasr_vim_object_mark){
 View_ID view = get_active_view(app, Access_ReadVisible);
 i64 mark_pos = view_get_mark_pos(app, view);
 Range_i64 range = {};
 if(vim_state.params.clusivity == VIM_Exclusive){
  Range_i64 r = Ii64(cursor_pos, mark_pos);
  if(r.max > r.min){ range = Ii64(r.min, r.max - 1); }
 }else{
  Range_i64 lines = Ii64(get_line_number_from_pos(app, buffer, cursor_pos),
                         get_line_number_from_pos(app, buffer, mark_pos));
  range = Ii64(get_line_start_pos(app, buffer, lines.min), get_line_end_pos(app, buffer, lines.max));
  vim_state.params.edit_type = EDIT_LineWise;
 }
 return range;
}

//~ gp : put register lines into each line's SPEC

VIM_COMMAND_SIG(nasr_vim_put_lines){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 Vim_Register *reg = nasr_vim_target_register();

 Nasr_Vim_Spec spec = {};
 if(!nasr_vim_query_spec(app, &spec, true)){ nasr_vim_multi_finish_command(); return; }

 Scratch_Block scratch(app);
 String_Const_u8 data = push_string_copy(scratch, reg->data.string);
 if(data.size > 0 && data.str[data.size - 1] == '\n'){ data.size -= 1; }
 List_String_Const_u8 parts = string_split(scratch, data, (u8 *)"\n", 1);
 i32 part_count = i32(parts.node_count);
 if(data.size == 0){ part_count = 0; }
 String_Const_u8 *values = push_array(scratch, String_Const_u8, Max(1, part_count));
 {
  i32 i = 0;
  for(Node_String_Const_u8 *node = parts.first; node != 0 && i < part_count; node = node->next){
   values[i++] = node->string;
  }
 }

 Range_i64 *ranges = 0;
 i32 count = nasr_vim_collect(app, scratch, view, buffer, spec, &ranges, 0, 0);
 if(part_count == 0 || count == 0){ nasr_vim_multi_finish_command(); return; }

 // one line in the register goes everywhere, otherwise line k goes to piece k
 i32 used = (part_count == 1 ? count : Min(count, part_count));
 vim_push_jump(app, view);
 History_Group history = history_group_begin(app, buffer);
 for(i32 i = used - 1; i >= 0; i -= 1){
  String_Const_u8 value = (part_count == 1 ? values[0] : values[i]);
  buffer_replace_range(app, buffer, ranges[i], value);
  buffer_post_fade(app, buffer, 0.667f, Ii64_size(ranges[i].min, value.size), fcolor_resolve(fcolor_id(defcolor_paste)));
 }
 history_group_end(history);
 view_set_cursor_and_preferred_x(app, view, seek_pos(ranges[0].min));
 nasr_vim_multi_finish_command();
}

//~ gx : swap the object at the cursor with the one at the mark

VIM_COMMAND_SIG(nasr_vim_swap_objects){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);

 u8 kind = vim_query_user_key(app, string_u8_litexpr("-- SWAP (i/a) --"));
 if(kind != 'i' && kind != 'a'){ vim_reset_state(); return; }
 u8 object = vim_query_user_key(app, string_u8_litexpr("-- SWAP TEXT OBJECT --"));
 if(object == 0){ vim_reset_state(); return; }
 Vim_Clusivity clusivity = (kind == 'i' ? VIM_Exclusive : VIM_Inclusive);

 i64 cursor_pos = view_get_cursor_pos(app, view);
 i64 mark_pos = view_get_mark_pos(app, view);
 Range_i64 at_cursor = {};
 Range_i64 at_mark = {};
 if(!nasr_vim_object_range(app, buffer, cursor_pos, object, clusivity, &at_cursor) ||
    !nasr_vim_object_range(app, buffer, mark_pos, object, clusivity, &at_mark)){
  vim_set_bottom_text(string_u8_litexpr("swap: object not found at cursor or mark"));
  vim_reset_state();
  return;
 }
 if(range_overlap(at_cursor, at_mark) || at_cursor.min == at_mark.min){
  vim_set_bottom_text(string_u8_litexpr("swap: the two objects overlap"));
  vim_reset_state();
  return;
 }

 Scratch_Block scratch(app);
 String_Const_u8 cursor_text = push_buffer_range(app, scratch, buffer, at_cursor);
 String_Const_u8 mark_text = push_buffer_range(app, scratch, buffer, at_mark);

 History_Group history = history_group_begin(app, buffer);
 i64 new_cursor = 0;
 i64 new_mark = 0;
 if(at_cursor.min < at_mark.min){
  buffer_replace_range(app, buffer, at_mark, cursor_text);
  buffer_replace_range(app, buffer, at_cursor, mark_text);
  new_cursor = at_cursor.min;
  new_mark = at_mark.min + (i64(mark_text.size) - i64(cursor_text.size));
 }else{
  buffer_replace_range(app, buffer, at_cursor, mark_text);
  buffer_replace_range(app, buffer, at_mark, cursor_text);
  new_mark = at_mark.min;
  new_cursor = at_cursor.min + (i64(cursor_text.size) - i64(mark_text.size));
 }
 history_group_end(history);

 // cursor and mark stay on "their" places, so gx again swaps back
 view_set_cursor_and_preferred_x(app, view, seek_pos(new_cursor));
 view_set_mark(app, view, seek_pos(new_mark));
 vim_reset_state();
}

//~ gs / gS : sort the region lines

struct Nasr_Vim_Sort_Item{
 String_Const_u8 line;
 String_Const_u8 key;
};

function void
nasr_vim_sort_items(Nasr_Vim_Sort_Item *items, Nasr_Vim_Sort_Item *temp, i64 count){
 // stable merge sort
 if(count < 2){ return; }
 i64 half = count/2;
 nasr_vim_sort_items(items, temp, half);
 nasr_vim_sort_items(items + half, temp, count - half);
 i64 a = 0, b = half, k = 0;
 while(a < half && b < count){
  if(string_compare(items[b].key, items[a].key) < 0){ temp[k++] = items[b++]; }
  else{ temp[k++] = items[a++]; }
 }
 while(a < half){ temp[k++] = items[a++]; }
 while(b < count){ temp[k++] = items[b++]; }
 block_copy(items, temp, count*sizeof(*items));
}

function void
nasr_vim_sort_region(Application_Links *app, Nasr_Vim_Spec *spec){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 Nasr_Vim_Region_Lines region = nasr_vim_region_lines(app, view, buffer);
 i64 count = region.max - region.min + 1;
 if(count < 2){ vim_reset_state(); return; }

 Scratch_Block scratch(app);
 Nasr_Vim_Sort_Item *items = push_array_zero(scratch, Nasr_Vim_Sort_Item, count);
 Nasr_Vim_Sort_Item *temp = push_array_zero(scratch, Nasr_Vim_Sort_Item, count);
 for(i64 i = 0; i < count; i += 1){
  i64 line = region.min + i;
  items[i].line = push_buffer_range(app, scratch, buffer,
                                    Ii64(get_line_start_pos(app, buffer, line), get_line_end_pos(app, buffer, line)));
  items[i].key = items[i].line;
 }

 if(spec){
  if(!nasr_vim_spec_is_per_line(*spec)){ vim_reset_state(); return; }
  Range_i64 *ranges = 0;
  i64 *lines = 0;
  i32 found = nasr_vim_collect(app, scratch, view, buffer, *spec, &ranges, &lines, 0);
  for(i64 i = 0; i < count; i += 1){ items[i].key = string_u8_empty; }
  for(i32 i = 0; i < found; i += 1){
   items[lines[i] - region.min].key = push_buffer_range(app, scratch, buffer, ranges[i]);
  }
 }

 nasr_vim_sort_items(items, temp, count);

 List_String_Const_u8 list = {};
 for(i64 i = 0; i < count; i += 1){
  if(i > 0){ string_list_push(scratch, &list, string_u8_litexpr("\n")); }
  string_list_push(scratch, &list, items[i].line);
 }
 String_Const_u8 sorted = string_list_flatten(scratch, list);
 Range_i64 span = Ii64(get_line_start_pos(app, buffer, region.min), get_line_end_pos(app, buffer, region.max));
 buffer_replace_range(app, buffer, span, sorted);
 vim_reset_state();
}

VIM_COMMAND_SIG(nasr_vim_sort_lines){
 nasr_vim_sort_region(app, 0);
}

VIM_COMMAND_SIG(nasr_vim_sort_lines_by){
 Nasr_Vim_Spec spec = {};
 if(!nasr_vim_query_spec(app, &spec, false)){ vim_reset_state(); return; }
 nasr_vim_sort_region(app, &spec);
}

//~ ga : align the first <c> on every region line

VIM_COMMAND_SIG(nasr_vim_align_char){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 u8 c = vim_query_user_key(app, string_u8_litexpr("-- ALIGN ON CHAR --"));
 if(c == 0){ vim_reset_state(); return; }

 Nasr_Vim_Region_Lines region = nasr_vim_region_lines(app, view, buffer);
 i64 count = region.max - region.min + 1;
 Scratch_Block scratch(app);
 i64 *columns = push_array(scratch, i64, count);
 i64 target = -1;
 for(i64 i = 0; i < count; i += 1){
  i64 line = region.min + i;
  i64 start = get_line_start_pos(app, buffer, line);
  String_Const_u8 text = push_buffer_range(app, scratch, buffer, Ii64(start, get_line_end_pos(app, buffer, line)));
  columns[i] = -1;
  for(u64 k = 0; k < text.size; k += 1){
   if(text.str[k] == c){ columns[i] = i64(k); break; }
  }
  target = Max(target, columns[i]);
 }
 if(target < 0){ vim_reset_state(); return; }

 History_Group history = history_group_begin(app, buffer);
 for(i64 i = count - 1; i >= 0; i -= 1){
  if(columns[i] < 0 || columns[i] == target){ continue; }
  i64 pad = target - columns[i];
  String_Const_u8 spaces = string_const_u8_push(scratch, pad);
  block_fill_u8(spaces.str, pad, ' ');
  i64 pos = get_line_start_pos(app, buffer, region.min + i) + columns[i];
  buffer_replace_range(app, buffer, Ii64(pos), spaces);
 }
 history_group_end(history);
 vim_reset_state();
}

//~ g Ctrl-A : number the region lines

VIM_COMMAND_SIG(nasr_vim_number_lines){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 i64 step = vim_consume_number();
 Nasr_Vim_Region_Lines region = nasr_vim_region_lines(app, view, buffer);

 Scratch_Block scratch(app);
 History_Group history = history_group_begin(app, buffer);
 // count how many numbered lines come before each one, so editing bottom-up keeps positions valid
 i64 total = 0;
 for(i64 line = region.min; line <= region.max; line += 1){
  i64 start = get_line_start_pos(app, buffer, line);
  String_Const_u8 text = push_buffer_range(app, scratch, buffer, Ii64(start, get_line_end_pos(app, buffer, line)));
  for(u64 k = 0; k < text.size; k += 1){
   if(character_is_base10(text.str[k])){ total += 1; break; }
  }
 }
 i64 index = total;
 for(i64 line = region.max; line >= region.min; line -= 1){
  i64 start = get_line_start_pos(app, buffer, line);
  String_Const_u8 text = push_buffer_range(app, scratch, buffer, Ii64(start, get_line_end_pos(app, buffer, line)));
  u64 k = 0;
  while(k < text.size && !character_is_base10(text.str[k])){ k += 1; }
  if(k == text.size){ continue; }
  index -= 1;
  u64 begin = k;
  b32 negative = (begin > 0 && text.str[begin - 1] == '-');
  u64 end = k;
  u64 value = 0;
  while(end < text.size && character_is_base10(text.str[end])){
   value = value*10 + (text.str[end] - '0');
   end += 1;
  }
  i64 number = (negative ? -i64(value) : i64(value)) + index*step;
  u64 width = end - begin;
  b32 zero_padded = (width > 1 && text.str[begin] == '0');
  if(negative){ begin -= 1; }
  String_Const_u8 replacement = (zero_padded ?
                                 push_stringf(scratch, "%0*lld", i32(width + (number < 0)), number) :
                                 push_stringf(scratch, "%lld", number));
  buffer_replace_range(app, buffer, Ii64(start + begin, start + end), replacement);
 }
 history_group_end(history);
 vim_reset_state();
}

//~ g@ : run a vim macro on every region line

global i64 nasr_vim_macro_lines[4096];
global i32 nasr_vim_macro_line_count;
global i32 nasr_vim_macro_line_index;
global Buffer_ID nasr_vim_macro_buffer;

// Queued between macro replays as a virtual event; moves to the next line (bottom-up).
function void
nasr_vim_macro_line_step(Application_Links *app){
 if(nasr_vim_macro_line_index >= nasr_vim_macro_line_count){ return; }
 View_ID view = get_active_view(app, Access_ReadVisible);
 if(view_get_buffer(app, view, Access_ReadVisible) != nasr_vim_macro_buffer){
  nasr_vim_macro_line_index = nasr_vim_macro_line_count;
  return;
 }
 if(vim_state.mode != VIM_Normal){ vim_normal_mode(app); }
 i64 line = nasr_vim_macro_lines[nasr_vim_macro_line_index++];
 view_set_cursor_and_preferred_x(app, view, seek_line_col(line, 1));
}

VIM_COMMAND_SIG(nasr_vim_macro_lines_run){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 u8 c = vim_query_user_key(app, string_u8_litexpr("-- MACRO FOR EVERY LINE --"));
 if(c == '@'){ c = vim_state.prev_macro; }
 if(!(in_range('a', c, 'z'+1) || in_range('A', c, 'Z'+1))){ vim_reset_state(); return; }
 Range_i64 macro_range = vim_macros[vim_macro_index(c)];
 if(macro_range.min == 0 || macro_range.max == 0){ vim_reset_state(); return; }
 vim_state.prev_macro = c;

 Nasr_Vim_Region_Lines region = nasr_vim_region_lines(app, view, buffer);
 i64 count = region.max - region.min + 1;
 if(count > i64(ArrayCount(nasr_vim_macro_lines))){
  vim_set_bottom_text(string_u8_litexpr("g@: too many lines"));
  vim_reset_state();
  return;
 }

 // bottom-up, so whatever the macro does to a line can't shift the lines still to come
 nasr_vim_macro_buffer = buffer;
 nasr_vim_macro_line_count = i32(count);
 nasr_vim_macro_line_index = 0;
 for(i64 i = 0; i < count; i += 1){ nasr_vim_macro_lines[i] = region.max - i; }

 Scratch_Block scratch(app);
 String_Const_u8 macro = push_buffer_range(app, scratch, get_keyboard_log_buffer(app), macro_range);
 vim_reset_state();
 for(i64 i = 0; i < count; i += 1){
  Input_Event step = {};
  step.kind = InputEventKind_CustomFunction;
  step.custom_func = nasr_vim_macro_line_step;
  enqueue_virtual_event(app, &step);
  keyboard_macro_play(app, macro);
 }
}

//~ zn / zN : narrow to the region with folds, widen again

VIM_COMMAND_SIG(nasr_vim_narrow){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 Managed_Scope scope = buffer_get_managed_scope(app, buffer);
 b32 *narrowed = scope_attachment(app, scope, buffer_nasr_narrowed, b32);
 if(narrowed == 0 || *narrowed){ vim_reset_state(); return; }

 Nasr_Vim_Region_Lines region = nasr_vim_region_lines(app, view, buffer);
 i64 line_count = buffer_get_line_count(app, buffer);
 i64 size = buffer_get_size(app, buffer);
 // bit 1: folded above the region, bit 2: folded below it
 b32 sides = 0;
 if(region.min > 1){
  fold_push(app, buffer, Ii64(0, get_line_end_pos(app, buffer, region.min - 1)));
  sides |= bit_1;
 }
 if(region.max < line_count){
  fold_push(app, buffer, Ii64(get_line_start_pos(app, buffer, region.max + 1), Max(0, size - 1)));
  sides |= bit_2;
 }
 *narrowed = sides;
 vim_reset_state();
}

VIM_COMMAND_SIG(nasr_vim_widen){
 View_ID view = get_active_view(app, Access_ReadVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 Managed_Scope scope = buffer_get_managed_scope(app, buffer);
 b32 *narrowed = scope_attachment(app, scope, buffer_nasr_narrowed, b32);
 Fold_List *fold_list = scope_attachment(app, scope, buffer_folds, Fold_List);
 if(narrowed == 0 || !*narrowed || fold_list == 0){ vim_reset_state(); return; }

 // the narrowing folds are the outermost ones touching the start / end of the buffer
 i64 size = buffer_get_size(app, buffer);
 i64 edges[2] = {0, Max(0, size - 1)};
 for(i32 e = 0; e < 2; e += 1){
  if(!(*narrowed & (e == 0 ? bit_1 : bit_2))){ continue; }
  for(i32 i = 0; i < fold_list->fold_count; i += 1){
   if(range_contains(fold_list->folds[i].range, edges[e]) ||
      (e == 1 && fold_list->folds[i].range.max >= edges[e])){
    fold_pop_inner(app, buffer, fold_list, i);
    break;
   }
  }
 }
 *narrowed = false;
 vim_reset_state();
}
