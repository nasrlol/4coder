
function Rect_f32
vim_get_rel_block_rect(Application_Links *app, View_ID view, Buffer_ID buffer, Range_i64 range, i64 line){
 Vec2_f32 p0 = view_relative_xy_of_pos(app, view, line, range.min);
 Vec2_f32 p1 = view_relative_xy_of_pos(app, view, line, range.max);
 return Rf32(Min(p0.x, p1.x), Min(p0.y, p1.y), Max(p0.x, p1.x), Max(p0.y, p1.y));
 }

 function Rect_f32
 vim_get_abs_block_rect(Application_Links *app, View_ID view, Buffer_ID buffer, Text_Layout_ID text_layout_id, Range_i64 range){
 Range_i64 visible_range = text_layout_get_visible_range(app, text_layout_id);

 i64 rel_pos = range.min;
 if(!range_contains(visible_range, rel_pos)){ rel_pos = range.max; }
 if(!range_contains(visible_range, rel_pos)){ rel_pos = scan(app, boundary_alpha_numeric, buffer, Scan_Forward, visible_range.min); }
 if(!range_contains(visible_range, rel_pos)){ return Rect_f32{}; }   // This is nearly impossible to hit... nearly

 i64 line = get_line_number_from_pos(app, buffer, rel_pos);
 Vec2_f32 rel_p = view_relative_xy_of_pos(app, view, line, rel_pos);
 Rect_f32 rel_rect = text_layout_character_on_screen(app, text_layout_id, rel_pos);
 Vec2_f32 offset0 = rel_p - rel_rect.p0;
 Vec2_f32 offset1 = rel_p - rel_rect.p1;
 Rect_f32 min_rect, max_rect;

 if(range_contains(visible_range, range.min)){
 min_rect = text_layout_character_on_screen(app, text_layout_id, range.min);
 }else{
 Vec2_f32 min_p = view_relative_xy_of_pos(app, view, line, range.min);
 min_rect = Rf32(min_p - offset0, min_p - offset1);
 }

 if(range_contains(visible_range, range.max)){
 max_rect = text_layout_character_on_screen(app, text_layout_id, range.max);
 }else{
 Vec2_f32 max_p = view_relative_xy_of_pos(app, view, line, range.max);
 max_rect = Rf32(max_p - offset0, max_p - offset1);
 }

 return rect_union(min_rect, max_rect);
 }

 // NOTE(nasr): the part of `line` covered by the block. An x past the end of a line resolves to
 // that line's newline (it has a layout item), so a blank line, or one that ends before the
 // block, used to yield "\n" and add an extra row to the register -> clamp to the line end
 function Range_i64
 vim_block_line_range(Application_Links *app, View_ID view, Buffer_ID buffer, i64 line_min,
                      Rect_f32 block_rect, f32 line_advance, f32 wid, i64 line){
 Vec2_f32 min_point = block_rect.p0 + V2f32(0, line_advance*(line-line_min));
 Vec2_f32 max_point = min_point + V2f32(wid,0);
 i64 min_pos = view_pos_at_relative_xy(app, view, line_min, min_point);
 i64 max_pos = view_pos_at_relative_xy(app, view, line_min, max_point)+1;
 i64 line_end = get_line_end_pos(app, buffer, line);
 if(min_pos >= line_end){ return Ii64(line_end); }
 return Ii64(min_pos, Min(max_pos, line_end));
 }

 function void
 vim_block_copy(Application_Links *app, View_ID view, Buffer_ID buffer, Range_i64 range, Vim_Register *reg){
 if(reg->flags & REGISTER_ReadOnly){
 vim_state.chord_resolved = bitmask_2;
 Scratch_Block scratch(app);
 vim_set_bottom_text(push_stringf(scratch, "Register %c is Read Only", vim_get_register_char(reg)));
 return;
 }
 reg->edit_type = EDIT_Block;
 reg->flags |= (REGISTER_Set|REGISTER_Updated);
 print_message(app, string_u8_litexpr("Block copy\n"));
 i64 line_min = get_line_number_from_pos(app, buffer, range.min);
 i64 line_max = get_line_number_from_pos(app, buffer, range.max);
 Rect_f32 block_rect = vim_get_rel_block_rect(app, view, buffer, range, line_min);

 f32 line_advance = rect_height(block_rect)/f32(Max(1, line_max-line_min));
 f32 wid = rect_width(block_rect);

 u64 size = 0;
 for(i64 i=line_max; i>=line_min; i--){
 Range_i64 line_range = vim_block_line_range(app, view, buffer, line_min, block_rect, line_advance, wid, i);
 size += range_size(line_range) + 1;
 }

 b32 valid = true;
 if(size >= reg->data.cap){ valid = vim_realloc_string(&reg->data, size); }
 if(!valid){ return; }
 Scratch_Block scratch(app);

 reg->data.size = 0;
 for(i64 i=line_max; i>=line_min; i--){
 Range_i64 line_range = vim_block_line_range(app, view, buffer, line_min, block_rect, line_advance, wid, i);
 if(range_size(line_range) > 0){
 buffer_read_range(app, buffer, line_range, reg->data.str + reg->data.size);
 buffer_post_fade(app, buffer, 0.667f, line_range, fcolor_resolve(fcolor_id(defcolor_paste)));
 }
 reg->data.size += range_size(line_range) + 1;
 reg->data.str[reg->data.size-1] = '\n';
 }
 }

 function void
 vim_block_paste(Application_Links *app, View_ID view, Buffer_ID buffer, Vim_Register *reg){
 i64 cursor_pos = view_get_cursor_pos(app, view);

 History_Group group = history_group_begin(app, buffer);

 // NOTE(nasr): one register row per '\n'; rows are stored bottom-up (row 0 = last line)
 u8 *data = reg->data.str;
 u64 data_size = reg->data.size;
 i64 row_count = 0;
 u64 row_width = 0;
 {
 u64 cur = 0;
 foreach(i, data_size){
 if(data[i] == '\n'){ row_count += 1; row_width = Max(row_width, cur); cur = 0; }
 else{ cur += 1; }
 }
 }

 i64 line_min = get_line_number_from_pos(app, buffer, cursor_pos);
 i64 line_max = line_min + row_count - 1;

 Scratch_Block scratch(app);

 // block taller than what's left of the buffer -> add lines at the end
 i64 line_count = buffer_get_line_count(app, buffer);
 if(row_count > 0 && line_max > line_count){
 i64 extra = line_max - line_count;
 u8 *newlines = push_array(scratch, u8, extra);
 for(i64 i = 0; i < extra; i += 1){ newlines[i] = '\n'; }
 buffer_replace_range(app, buffer, Ii64(buffer_get_size(app, buffer)), SCu8(newlines, extra));
 }

 f32 x = view_relative_xy_of_pos(app, view, line_min, cursor_pos).x;
 f32 space = get_face_metrics(app, get_face_id(app, buffer)).space_advance;
 if(space < 1.f){ space = 1.f; }
 ARGB_Color argb = fcolor_resolve(fcolor_id(defcolor_paste));

 u64 row_start = 0;
 for(i64 i=line_max; i>=line_min && row_start < data_size; i--){
 u64 row_end = row_start;
 while(row_end < data_size && data[row_end] != '\n'){ row_end += 1; }
 String_Const_u8 row = SCu8(data + row_start, row_end - row_start);
 row_start = row_end + 1;

 // an x past the end of the line resolves to its newline, so short and blank lines land at
 // the line end; pad those out to the block column with spaces
 i64 line_start = get_line_start_pos(app, buffer, i);
 i64 line_end = get_line_end_pos(app, buffer, i);
 f32 y = view_relative_xy_of_pos(app, view, line_min, line_start).y;
 i64 pos = view_pos_at_relative_xy(app, view, line_min, V2f32(x, y));
 pos = Max(line_start, Min(pos, line_end));

 u64 pad_before = 0;
 u64 pad_after = 0;
 if(pos == line_end){
 if(row.size == 0){ continue; }
 f32 end_x = view_relative_xy_of_pos(app, view, line_min, line_end).x;
 if(x > end_x){ pad_before = u64((x - end_x)/space + 0.5f); }
 }else{
 // text continues to the right: keep it aligned by padding short rows to the block width
 pad_after = row_width - row.size;
 }

 String_Const_u8 text = row;
 if(pad_before > 0 || pad_after > 0){
 u64 size = pad_before + row.size + pad_after;
 u8 *str = push_array(scratch, u8, size);
 for(u64 j = 0; j < pad_before; j += 1){ str[j] = ' '; }
 block_copy(str + pad_before, row.str, row.size);
 for(u64 j = pad_before + row.size; j < size; j += 1){ str[j] = ' '; }
 text = SCu8(str, size);
 }

 buffer_replace_range(app, buffer, Ii64(pos), text);
 buffer_post_fade(app, buffer, 0.667f, Ii64_size(pos, text.size), argb);
 }

 history_group_end(group);


 vim_default_register();
 }

 function void
 vim_block_edit(Application_Links *app, View_ID view, Buffer_ID buffer, Range_i64 range){
 Vim_Params *params = &vim_state.params;
 if(params->request == REQUEST_Yank ||
 params->request == REQUEST_Change ||
 params->request == REQUEST_Delete)
 {
 if(vim_state.params.selected_reg){
 vim_block_copy(app, view, buffer, range, vim_state.params.selected_reg);
 vim_update_registers(app);
 vim_state.params.selected_reg = 0;
 }
 if(params->request == REQUEST_Yank){ return; }
 }

 History_Group group = history_group_begin(app, buffer);

 i64 line_min = get_line_number_from_pos(app, buffer, range.min);
 i64 line_max = get_line_number_from_pos(app, buffer, range.max);
 Rect_f32 block_rect = vim_get_rel_block_rect(app, view, buffer, range, line_min);

 f32 line_advance = rect_height(block_rect)/f32(Max(1, line_max-line_min));
 f32 wid = rect_width(block_rect);
 block_rect = rect_inner(block_rect, -0.1f);

 for(i64 i=line_max; i>=line_min; i--){
 if(line_is_valid_and_blank(app, buffer, i)){ continue; }
 Vec2_f32 min_point = block_rect.p0 + V2f32(0, line_advance*(i-line_min));
 Vec2_f32 max_point = min_point + V2f32(wid,0);
 i64 min_pos = view_pos_at_relative_xy(app, view, line_min, min_point);
 i64 max_pos = view_pos_at_relative_xy(app, view, line_min, max_point);

 Vec2_f32 min_p = view_relative_xy_of_pos(app, view, line_min, min_pos);
 Vec2_f32 max_p = view_relative_xy_of_pos(app, view, line_min, max_pos);
 if(!rect_contains_point(block_rect, min_p) || !rect_contains_point(block_rect, max_p)){ continue; }

 Range_i64 line_range = Ii64(min_pos, max_pos+1);

 vim_request_vtable[params->request](app, view, buffer, line_range);
 }

 history_group_end(group);
 }

 function void
 vim_visual_insert_char(Application_Links *app, View_ID view, Buffer_ID buffer, u8 character){
 History_Group group = history_group_begin(app, buffer);

 Range_i64 range = get_view_range(app, view);
 i64 line_min = get_line_number_from_pos(app, buffer, range.min);
 i64 line_max = get_line_number_from_pos(app, buffer, range.max);
 Rect_f32 block_rect = vim_get_rel_block_rect(app, view, buffer, range, line_min);

 f32 line_advance = rect_height(block_rect)/f32(Max(1, line_max-line_min));
 f32 wid = rect_width(block_rect);
 f32 x_off = vim_visual_insert_after*wid;

 for(i64 i=line_max; i>=line_min; i--){
 if(line_is_valid_and_blank(app, buffer, i) && i != line_min && i != line_max){ continue; }
 Vec2_f32 point = block_rect.p0 + V2f32(x_off, line_advance*(i-line_min));
 i64 pos = view_pos_at_relative_xy(app, view, line_min, point);
 buffer_replace_range(app, buffer, Ii64(pos + vim_visual_insert_after), SCu8(&character,1));
 }

 i64 cursor_pos = view_get_cursor_pos(app, view);
 i64 mark_pos = view_get_mark_pos(app, view);

 Vec2_f32 top_point = block_rect.p0 + V2f32(x_off, 0.f);
 Vec2_f32 bot_point = block_rect.p0 + V2f32(x_off, rect_height(block_rect));
 i64 top_pos = view_pos_at_relative_xy(app, view, line_min, top_point);
 i64 bot_pos = view_pos_at_relative_xy(app, view, line_min, bot_point);

 if(cursor_pos == top_pos || cursor_pos == bot_pos){
 view_set_cursor(app, view, seek_pos(cursor_pos+1));
 }
 if(mark_pos == top_pos || mark_pos == bot_pos){
 view_set_mark(app, view, seek_pos(mark_pos+1));
 }

 history_group_end(group);
}
