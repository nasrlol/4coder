
// Vim window/panel commands: directional focus, Ctrl-W prefix, resize/equalize, rotate/exchange, new split.

enum Vim_Panel_Dir{
 Vim_Panel_Dir_Left,
 Vim_Panel_Dir_Down,
 Vim_Panel_Dir_Up,
 Vim_Panel_Dir_Right,
};

struct Vim_View_Snapshot{
 Buffer_ID buffer;
 Buffer_Scroll scroll;
 i64 cursor_pos;
 i64 mark_pos;
};

function Rect_f32
vim_panel_get_rect(Application_Links *app, Panel_ID panel){
 if(panel == 0){ return Rf32(0.f, 0.f, 0.f, 0.f); }
 if(panel_is_leaf(app, panel)){
  View_ID view = panel_get_view(app, panel, Access_Always);
  return view_get_screen_rect(app, view);
 }
 Rect_f32 r0 = vim_panel_get_rect(app, panel_get_child(app, panel, Side_Min));
 Rect_f32 r1 = vim_panel_get_rect(app, panel_get_child(app, panel, Side_Max));
 return rect_union(r0, r1);
}

function b32
vim_panel_has_primary(Application_Links *app, Panel_ID panel){
 if(panel == 0){ return false; }
 if(panel_is_leaf(app, panel)){
  View_ID v = panel_get_view(app, panel, Access_Always);
  return (v != 0 && !view_get_is_passive(app, v));
 }
 return (vim_panel_has_primary(app, panel_get_child(app, panel, Side_Min)) ||
         vim_panel_has_primary(app, panel_get_child(app, panel, Side_Max)));
}

function b32
vim_panel_is_primary_split(Application_Links *app, Panel_ID panel){
 if(panel == 0 || !panel_is_split(app, panel)){ return false; }
 return (vim_panel_has_primary(app, panel_get_child(app, panel, Side_Min)) &&
         vim_panel_has_primary(app, panel_get_child(app, panel, Side_Max)));
}

function Panel_ID
vim_primary_panel_root(Application_Links *app){
 Panel_ID p = panel_get_root(app);
 while(p != 0 && panel_is_split(app, p)){
  Panel_ID c0 = panel_get_child(app, p, Side_Min);
  Panel_ID c1 = panel_get_child(app, p, Side_Max);
  b32 p0 = vim_panel_has_primary(app, c0);
  b32 p1 = vim_panel_has_primary(app, c1);
  if(p0 && p1){ break; }
  if(p0){ p = c0; }
  else if(p1){ p = c1; }
  else{ break; }
 }
 return p;
}

// 4coder's API does not expose a split's Dimension directly; infer it from child rects.
// A vertical split (Dimension_X) shares y0/y1 across children; horizontal (Dimension_Y) shares x0/x1.
function Dimension
vim_panel_split_dim(Application_Links *app, Panel_ID panel){
 Rect_f32 r0 = vim_panel_get_rect(app, panel_get_child(app, panel, Side_Min));
 Rect_f32 r1 = vim_panel_get_rect(app, panel_get_child(app, panel, Side_Max));
 b32 same_y = (near_zero(r0.y0 - r1.y0, 1.f) && near_zero(r0.y1 - r1.y1, 1.f));
 b32 same_x = (near_zero(r0.x0 - r1.x0, 1.f) && near_zero(r0.x1 - r1.x1, 1.f));
 if(same_y && !same_x){ return Dimension_X; }
 if(same_x && !same_y){ return Dimension_Y; }
 return (near_zero(r0.y1 - r1.y0, 1.f) ? Dimension_Y : Dimension_X);
}

function void
vim_collect_primary_views(Application_Links *app, Panel_ID panel, View_ID *out, i32 *count, i32 cap){
 if(panel == 0 || *count >= cap){ return; }
 if(panel_is_leaf(app, panel)){
  View_ID v = panel_get_view(app, panel, Access_Always);
  if(v != 0 && !view_get_is_passive(app, v)){
   out[(*count)++] = v;
  }
  return;
 }
 vim_collect_primary_views(app, panel_get_child(app, panel, Side_Min), out, count, cap);
 vim_collect_primary_views(app, panel_get_child(app, panel, Side_Max), out, count, cap);
}

function void
vim_switch_active_view(Application_Links *app, View_ID target){
 if(target == 0 || !view_exists(app, target) || view_get_is_passive(app, target)){ return; }
 View_ID cur = get_active_view(app, Access_Always);
 if(cur != 0 && cur != target && !view_get_is_passive(app, cur)){
  vim_prev_active_view = cur;
 }
 vim_cur_active_view = target;
 view_set_active(app, target);
}

function void
vim_track_active_view(Application_Links *app){
 View_ID active_now = get_active_view(app, Access_Always);
 if(active_now != 0 && !view_get_is_passive(app, active_now) && active_now != vim_cur_active_view){
  if(vim_cur_active_view != 0 && view_exists(app, vim_cur_active_view) && !view_get_is_passive(app, vim_cur_active_view)){
   vim_prev_active_view = vim_cur_active_view;
  }
  vim_cur_active_view = active_now;
 }
}

//- Directional panel selection

function Vec2_f32
vim_view_cursor_screen_point(Application_Links *app, View_ID view, Rect_f32 view_rect){
 Vec2_f32 center = rect_center(view_rect);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
 if(buffer == 0){ return center; }
 Buffer_Scroll scroll = view_get_buffer_scroll(app, view);
 i64 pos = view_get_cursor_pos(app, view);
 Vec2_f32 rel = view_relative_xy_of_pos(app, view, scroll.position.line_number, pos);
 Rect_f32 region = view_get_buffer_region(app, view);
 Vec2_f32 pt = region.p0 + rel - scroll.position.pixel_shift;
 pt.x = clamp(view_rect.x0, pt.x, view_rect.x1);
 pt.y = clamp(view_rect.y0, pt.y, view_rect.y1);
 return pt;
}

function View_ID
vim_find_panel_in_dir(Rect_f32 cur, Vec2_f32 ref_pt, View_ID active,
                      View_ID *views, Rect_f32 *rects, i32 count, Vim_Panel_Dir dir){
 i32 axis = (dir == Vim_Panel_Dir_Left || dir == Vim_Panel_Dir_Right) ? 0 : 1;
 i32 perp = 1 - axis;
 f32 sign = (dir == Vim_Panel_Dir_Right || dir == Vim_Panel_Dir_Down) ? 1.f : -1.f;

 f32 cur_min_a = cur.p0.v[axis];
 f32 cur_max_a = cur.p1.v[axis];
 f32 cur_min_b = cur.p0.v[perp];
 f32 cur_max_b = cur.p1.v[perp];
 f32 ref_b = ref_pt.v[perp];

 View_ID best = 0;
 f32 best_gap = 1e30f;
 f32 best_perp = 1e30f;
 f32 best_center = 1e30f;
 f32 best_min_b = 1e30f;

 for(i32 i = 0; i < count; i += 1){
  if(views[i] == active){ continue; }
  Rect_f32 r = rects[i];
  f32 r_min_a = r.p0.v[axis];
  f32 r_max_a = r.p1.v[axis];
  f32 r_min_b = r.p0.v[perp];
  f32 r_max_b = r.p1.v[perp];

  f32 gap = 0.f;
  if(sign > 0.f){
   if(r_min_a < cur_max_a - 2.f || r_max_a <= cur_max_a + 1.f){ continue; }
   gap = Max(0.f, r_min_a - cur_max_a);
  }else{
   if(r_max_a > cur_min_a + 2.f || r_min_a >= cur_min_a - 1.f){ continue; }
   gap = Max(0.f, cur_min_a - r_max_a);
  }

  f32 overlap = Min(cur_max_b, r_max_b) - Max(cur_min_b, r_min_b);
  if(overlap <= 1.f){ continue; }

  f32 perp_dist = (ref_b < r_min_b) ? (r_min_b - ref_b) :
                  ((ref_b > r_max_b) ? (ref_b - r_max_b) : 0.f);
  f32 center_dist = abs_f32(ref_b - 0.5f*(r_min_b + r_max_b));

  b32 better = false;
  if(best == 0){
   better = true;
  }else if(gap < best_gap - 2.f){
   better = true;
  }else if(abs_f32(gap - best_gap) <= 2.f){
   if(perp_dist < best_perp - 1.f){
    better = true;
   }else if(abs_f32(perp_dist - best_perp) <= 1.f){
    if(center_dist < best_center - 1.f){
     better = true;
    }else if(abs_f32(center_dist - best_center) <= 1.f && r_min_b < best_min_b){
     better = true;
    }
   }
  }

  if(better){
   best = views[i];
   best_gap = gap;
   best_perp = perp_dist;
   best_center = center_dist;
   best_min_b = r_min_b;
  }
 }
 return best;
}

function void
vim_panel_move_dir(Application_Links *app, Vim_Panel_Dir dir){
 i32 steps = vim_consume_number();
 for(i32 s = 0; s < steps; s += 1){
  View_ID active = get_active_view(app, Access_Always);
  if(active == 0){ break; }
  View_ID views[32];
  Rect_f32 rects[32];
  i32 count = 0;
  vim_collect_primary_views(app, vim_primary_panel_root(app), views, &count, ArrayCount(views));
  if(count <= 1){ break; }
  for(i32 i = 0; i < count; i += 1){
   rects[i] = view_get_screen_rect(app, views[i]);
  }
  Rect_f32 cur = view_get_screen_rect(app, active);
  Vec2_f32 ref_pt = vim_view_cursor_screen_point(app, active, cur);
  View_ID target = vim_find_panel_in_dir(cur, ref_pt, active, views, rects, count, dir);
  if(target == 0){ break; }
  vim_switch_active_view(app, target);
 }
}

//- Equalize and resize

function i32
vim_panel_axis_weight(Application_Links *app, Panel_ID panel, Dimension dim){
 if(panel == 0 || !vim_panel_has_primary(app, panel)){ return 0; }
 if(panel_is_leaf(app, panel)){ return 1; }
 Panel_ID c0 = panel_get_child(app, panel, Side_Min);
 Panel_ID c1 = panel_get_child(app, panel, Side_Max);
 if(!vim_panel_is_primary_split(app, panel)){
  return vim_panel_axis_weight(app, vim_panel_has_primary(app, c0) ? c0 : c1, dim);
 }
 i32 w0 = vim_panel_axis_weight(app, c0, dim);
 i32 w1 = vim_panel_axis_weight(app, c1, dim);
 if(vim_panel_split_dim(app, panel) == dim){
  return w0 + w1;
 }
 return Max(w0, w1);
}

function void
vim_panel_equalize_subtree(Application_Links *app, Panel_ID panel){
 if(panel == 0 || panel_is_leaf(app, panel)){ return; }
 Panel_ID c0 = panel_get_child(app, panel, Side_Min);
 Panel_ID c1 = panel_get_child(app, panel, Side_Max);
 if(vim_panel_is_primary_split(app, panel)){
  Dimension dim = vim_panel_split_dim(app, panel);
  i32 w0 = vim_panel_axis_weight(app, c0, dim);
  i32 w1 = vim_panel_axis_weight(app, c1, dim);
  if(w0 > 0 && w1 > 0){
   f32 ratio = (f32)w0 / (f32)(w0 + w1);
   panel_set_split(app, panel, PanelSplitKind_Ratio_Min, ratio);
  }
 }
 vim_panel_equalize_subtree(app, c0);
 vim_panel_equalize_subtree(app, c1);
}

function f32
vim_panel_min_px(Application_Links *app, Dimension dim){
 Face_Metrics m = get_face_metrics(app, get_face_id(app, 0));
 return (dim == Dimension_Y) ? Max(40.f, 2.f*m.line_height) : Max(60.f, 6.f*m.normal_advance);
}

function void
vim_panel_resize_px(Application_Links *app, Dimension dim, f32 delta_px){
 View_ID active = get_active_view(app, Access_Always);
 if(active == 0){ return; }
 Panel_ID child = view_get_panel(app, active);
 Panel_ID parent = panel_get_parent(app, child);
 f32 min_px = vim_panel_min_px(app, dim);

 while(parent != 0){
  if(vim_panel_is_primary_split(app, parent) && vim_panel_split_dim(app, parent) == dim){
   Panel_ID c_min = panel_get_child(app, parent, Side_Min);
   b32 is_min = (c_min == child);
   Rect_f32 r_parent = vim_panel_get_rect(app, parent);
   Rect_f32 r_min = vim_panel_get_rect(app, c_min);
   f32 total_px = rect_dim(r_parent).v[dim];
   if(total_px > 1.f){
    f32 cur_min_px = rect_dim(r_min).v[dim];
    f32 cur_ratio = cur_min_px / total_px;
    f32 min_ratio = clamp(0.04f, min_px / total_px, 0.25f);
    f32 new_min_px = cur_min_px + (is_min ? delta_px : -delta_px);
    f32 new_ratio = clamp(min_ratio, new_min_px / total_px, 1.f - min_ratio);
    if(abs_f32(new_ratio - cur_ratio) > 1e-4f){
     panel_set_split(app, parent, PanelSplitKind_Ratio_Min, new_ratio);
     return;
    }
   }
  }
  child = parent;
  parent = panel_get_parent(app, parent);
 }
}

function void
vim_panel_resize_step(Application_Links *app, Dimension dim, f32 sign){
 Face_Metrics m = get_face_metrics(app, get_face_id(app, 0));
 i32 raw_count = vim_state.number;
 vim_consume_number();
 f32 cell_px = (dim == Dimension_Y) ? m.line_height : m.normal_advance;
 f32 units = (raw_count > 0) ? (f32)raw_count : ((dim == Dimension_Y) ? 4.f : 8.f);
 vim_panel_resize_px(app, dim, sign * units * cell_px);
}

function void
vim_panel_maximize_dim(Application_Links *app, Dimension dim){
 View_ID active = get_active_view(app, Access_Always);
 if(active == 0){ return; }
 i32 raw_count = vim_state.number;
 vim_consume_number();

 if(raw_count > 0){
  Face_Metrics m = get_face_metrics(app, get_face_id(app, 0));
  f32 cell_px = (dim == Dimension_Y) ? m.line_height : m.normal_advance;
  Rect_f32 cur = view_get_screen_rect(app, active);
  f32 cur_px = rect_dim(cur).v[dim];
  vim_panel_resize_px(app, dim, (f32)raw_count * cell_px - cur_px);
  return;
 }

 Panel_ID ancestors[32];
 b32 ancestor_is_min[32];
 i32 count = 0;

 Panel_ID child = view_get_panel(app, active);
 Panel_ID parent = panel_get_parent(app, child);
 while(parent != 0 && count < ArrayCount(ancestors)){
  if(vim_panel_is_primary_split(app, parent) && vim_panel_split_dim(app, parent) == dim){
   ancestors[count] = parent;
   ancestor_is_min[count] = (panel_get_child(app, parent, Side_Min) == child);
   count += 1;
  }
  child = parent;
  parent = panel_get_parent(app, parent);
 }

 // Apply top-down so inner splits see their expanded parent dimensions.
 f32 min_px = vim_panel_min_px(app, dim);
 for(i32 i = count - 1; i >= 0; i -= 1){
  Rect_f32 r_parent = vim_panel_get_rect(app, ancestors[i]);
  f32 total_px = rect_dim(r_parent).v[dim];
  if(total_px > 1.f){
   f32 min_ratio = clamp(0.04f, min_px / total_px, 0.25f);
   f32 ratio = ancestor_is_min[i] ? (1.f - min_ratio) : min_ratio;
   panel_set_split(app, ancestors[i], PanelSplitKind_Ratio_Min, ratio);
  }
 }
}

//- Buffer snapshot / exchange / rotate

function Vim_View_Snapshot
vim_view_take_snapshot(Application_Links *app, View_ID view){
 Vim_View_Snapshot s = {};
 s.buffer = view_get_buffer(app, view, Access_Always);
 s.scroll = view_get_buffer_scroll(app, view);
 s.cursor_pos = view_get_cursor_pos(app, view);
 s.mark_pos = view_get_mark_pos(app, view);
 return s;
}

function void
vim_view_apply_snapshot(Application_Links *app, View_ID view, Vim_View_Snapshot s){
 if(s.buffer == 0){ return; }
 view_set_buffer(app, view, s.buffer, 0);
 view_set_buffer_scroll(app, view, s.scroll, SetBufferScroll_NoCursorChange);
 view_set_mark(app, view, seek_pos(s.mark_pos));
 view_set_cursor_and_preferred_x(app, view, seek_pos(s.cursor_pos));
}

function void
vim_panel_rotate(Application_Links *app, i32 dir){
 i32 steps = vim_consume_number();
 View_ID views[32];
 Vim_View_Snapshot snaps[32];
 i32 count = 0;
 vim_collect_primary_views(app, vim_primary_panel_root(app), views, &count, ArrayCount(views));
 if(count < 2){ return; }

 View_ID active = get_active_view(app, Access_Always);
 i32 active_idx = 0;
 for(i32 i = 0; i < count; i += 1){
  snaps[i] = vim_view_take_snapshot(app, views[i]);
  if(views[i] == active){ active_idx = i; }
 }

 i32 shift = (dir > 0) ? (steps % count) : (count - (steps % count)) % count;
 for(i32 i = 0; i < count; i += 1){
  vim_view_apply_snapshot(app, views[(i + shift) % count], snaps[i]);
 }
 vim_switch_active_view(app, views[(active_idx + shift) % count]);
}

//- Commands

VIM_COMMAND_SIG(vim_submode_w){
 vim_state.sub_mode = SUB_W;
 vim_state.chord_resolved = false;
}

VIM_COMMAND_SIG(vim_submode_w_digit){
 vim_digit(app);
}

VIM_COMMAND_SIG(vim_submode_w_0){
 if(vim_state.number > 0){ vim_digit(app); }
}

VIM_COMMAND_SIG(vim_panel_left){  vim_panel_move_dir(app, Vim_Panel_Dir_Left); }
VIM_COMMAND_SIG(vim_panel_down){  vim_panel_move_dir(app, Vim_Panel_Dir_Down); }
VIM_COMMAND_SIG(vim_panel_up){    vim_panel_move_dir(app, Vim_Panel_Dir_Up); }
VIM_COMMAND_SIG(vim_panel_right){ vim_panel_move_dir(app, Vim_Panel_Dir_Right); }

VIM_COMMAND_SIG(vim_panel_next){
 i32 raw_count = vim_state.number;
 vim_consume_number();
 View_ID views[32];
 i32 count = 0;
 vim_collect_primary_views(app, vim_primary_panel_root(app), views, &count, ArrayCount(views));
 if(count < 2){ return; }
 if(raw_count > 0){
  i32 idx = clamp(0, raw_count - 1, count - 1);
  vim_switch_active_view(app, views[idx]);
  return;
 }
 View_ID active = get_active_view(app, Access_Always);
 i32 cur = 0;
 for(i32 i = 0; i < count; i += 1){
  if(views[i] == active){ cur = i; break; }
 }
 vim_switch_active_view(app, views[(cur + 1) % count]);
}

VIM_COMMAND_SIG(vim_panel_prev_cycle){
 i32 raw_count = vim_state.number;
 vim_consume_number();
 View_ID views[32];
 i32 count = 0;
 vim_collect_primary_views(app, vim_primary_panel_root(app), views, &count, ArrayCount(views));
 if(count < 2){ return; }
 if(raw_count > 0){
  i32 idx = clamp(0, raw_count - 1, count - 1);
  vim_switch_active_view(app, views[idx]);
  return;
 }
 View_ID active = get_active_view(app, Access_Always);
 i32 cur = 0;
 for(i32 i = 0; i < count; i += 1){
  if(views[i] == active){ cur = i; break; }
 }
 vim_switch_active_view(app, views[(cur + count - 1) % count]);
}

VIM_COMMAND_SIG(vim_panel_last){
 vim_consume_number();
 if(vim_prev_active_view != 0 && view_exists(app, vim_prev_active_view) && !view_get_is_passive(app, vim_prev_active_view)){
  vim_switch_active_view(app, vim_prev_active_view);
 }
}

VIM_COMMAND_SIG(vim_panel_new){
 vim_consume_number();
 View_ID view = get_active_view(app, Access_Always);
 View_ID new_view = open_view(app, view, ViewSplit_Bottom);
 if(new_view != 0){
  new_view_settings(app, new_view);
  Buffer_ID buf = create_buffer(app, string_u8_litexpr("*new*"),
                                BufferCreate_NeverAttachToFile|BufferCreate_AlwaysNew);
  if(buf != 0){
   view_set_buffer(app, new_view, buf, 0);
  }
  vim_switch_active_view(app, new_view);
 }
}

VIM_COMMAND_SIG(vim_close_other_panels){
 vim_consume_number();
 View_ID keep = get_active_view(app, Access_Always);
 for(;;){
  View_ID v = get_next_view_looped_primary_panels(app, keep, Access_Always);
  if(v == 0 || v == keep){ break; }
  view_close(app, v);
 }
}

VIM_COMMAND_SIG(vim_panel_equalize){
 vim_consume_number();
 vim_panel_equalize_subtree(app, vim_primary_panel_root(app));
}

VIM_COMMAND_SIG(vim_panel_grow_height){   vim_panel_resize_step(app, Dimension_Y, +1.f); }
VIM_COMMAND_SIG(vim_panel_shrink_height){ vim_panel_resize_step(app, Dimension_Y, -1.f); }
VIM_COMMAND_SIG(vim_panel_grow_width){    vim_panel_resize_step(app, Dimension_X, +1.f); }
VIM_COMMAND_SIG(vim_panel_shrink_width){  vim_panel_resize_step(app, Dimension_X, -1.f); }
VIM_COMMAND_SIG(vim_panel_max_height){    vim_panel_maximize_dim(app, Dimension_Y); }
VIM_COMMAND_SIG(vim_panel_max_width){     vim_panel_maximize_dim(app, Dimension_X); }

VIM_COMMAND_SIG(vim_panel_rotate_down){ vim_panel_rotate(app, +1); }
VIM_COMMAND_SIG(vim_panel_rotate_up){   vim_panel_rotate(app, -1); }

VIM_COMMAND_SIG(vim_panel_exchange){
 i32 raw_count = vim_state.number;
 vim_consume_number();
 View_ID views[32];
 i32 count = 0;
 vim_collect_primary_views(app, vim_primary_panel_root(app), views, &count, ArrayCount(views));
 if(count < 2){ return; }

 View_ID active = get_active_view(app, Access_Always);
 i32 cur = -1;
 for(i32 i = 0; i < count; i += 1){
  if(views[i] == active){ cur = i; break; }
 }
 if(cur < 0){ return; }

 i32 target = (raw_count > 0) ? clamp(0, raw_count - 1, count - 1)
                              : ((cur + 1 < count) ? (cur + 1) : (cur - 1));
 if(target == cur){ return; }

 Vim_View_Snapshot s_cur = vim_view_take_snapshot(app, views[cur]);
 Vim_View_Snapshot s_tgt = vim_view_take_snapshot(app, views[target]);
 vim_view_apply_snapshot(app, views[cur], s_tgt);
 vim_view_apply_snapshot(app, views[target], s_cur);
 vim_switch_active_view(app, views[target]);
}
