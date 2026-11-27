
global Color_Table target_color_table = {};
global Color_Table cached_color_table = {};

function b32
byp_is_theme(String_Const_u8 name){
 String_Const_u8 target_prefix = string_u8_litexpr("theme-");
 String_Const_u8 target_suffix = string_u8_litexpr(".4coder");
 String_Const_u8 actual_prefix = string_prefix(name, target_prefix.size);
 String_Const_u8 actual_suffix = string_postfix(name, target_suffix.size);
 return string_match(actual_prefix, target_prefix) && string_match(actual_suffix, target_suffix);
 }

 // NOTE(nasr): slots per color id in the fade tables (target/cached). Themes rarely use more than
 // a few (back_cycle etc.); byp_copy_color_table clamps to this, extra entries are dropped.
 #define BYP_COLOR_ARRAY_CAP 14

 function Color_Table
 byp_init_color_table(Application_Links *app){
 Color_Table result = make_color_table(app, &global_permanent_arena);
 foreach(i, result.count){
 result.arrays[i].vals = push_array(&global_permanent_arena, u32, BYP_COLOR_ARRAY_CAP);
 result.arrays[i].count = BYP_COLOR_ARRAY_CAP;
 }
 return result;
 }

 // dst must come from byp_init_color_table (BYP_COLOR_ARRAY_CAP slots per array)
 function void
 byp_copy_color_table(Color_Table *dst, Color_Table table){
 i32 table_count = Min(table.count, dst->count);
 foreach(i, table_count){
 Color_Array *nxt_array = &dst->arrays[i];
 Color_Array *array = &table.arrays[i];
 nxt_array->count = Min(array->count, BYP_COLOR_ARRAY_CAP);
 foreach(j, nxt_array->count){
 nxt_array->vals[j] = array->vals[j];
 }
 }
 }

 // NOTE(nasr): one fade step per 8-bit channel, done on the integers. The old float lerp went
 // through pack_color, which truncates, so a channel that had to get brighter lost every
 // sub-1/255 step and never arrived (the fade, and the 60fps redraw it requests, ran forever).
 // Every step now moves at least 1, so it always lands exactly on the target.
 function ARGB_Color
 byp_color_step(ARGB_Color cur, ARGB_Color nxt, f32 t, b32 *still_animating){
 ARGB_Color result = 0;
 for(u32 shift = 0; shift < 32; shift += 8){
 i32 c = (cur >> shift) & 0xFF;
 i32 n = (nxt >> shift) & 0xFF;
 if(c != n){
 i32 step = (i32)((n - c)*t + (n > c ? 0.5f : -0.5f));
 if(step == 0){ step = (n > c ? 1 : -1); }
 c += step;
 if((step > 0 && c > n) || (step < 0 && c < n)){ c = n; }
 if(c != n){ *still_animating = true; }
 }
 result |= ((u32)c << shift);
 }
 return result;
 }

 function void
 byp_tick_colors(Application_Links *app, Frame_Info frame_info){
 // If another function sets active, correct this to not write into a loaded theme
 if(active_color_table.arrays != cached_color_table.arrays){
 if(active_color_table.arrays && active_color_table.arrays->vals && active_color_table.arrays->count > 0){
 byp_copy_color_table(&target_color_table, active_color_table);
 active_color_table = cached_color_table;
 }
 }
 b32 needs_animate = false;
 f32 t = clamp(0.f, frame_info.animation_dt*10.f, 1.f);
 foreach(i, active_color_table.count){
 Color_Array *nxt_array = &target_color_table.arrays[i];
 Color_Array *cur_array = &active_color_table.arrays[i];
 foreach(j, cur_array->count){
 if(cur_array->vals[j] != nxt_array->vals[j]){
 cur_array->vals[j] = byp_color_step(cur_array->vals[j], nxt_array->vals[j], t, &needs_animate);
 }
 }
 }
 if(needs_animate){ animate_in_n_milliseconds(app, 0); }
 }

 function void
 byp_theme_navigate(Application_Links *app, View_ID view, Lister *lister, i32 index_delta){
 lister__navigate__default(app, view, lister, index_delta);
 Color_Table *result = (Color_Table *)lister_get_user_data(lister, lister->raw_item_index);
 if(result){ byp_copy_color_table(&target_color_table, *result); }
 }


 CUSTOM_UI_COMMAND_SIG(theme_lister)
 CUSTOM_DOC("Opens an interactive list of all registered themes.")
 {
 Color_Table_List *color_table_list = &global_theme_list;

 Scratch_Block scratch(app);
 Lister_Block lister(app, scratch);
 vim_lister_set_default_handlers(lister);
 vim_reset_bottom_text();
 string_append(&vim_bot_text, string_u8_litexpr("Theme:"));

 lister_add_item(lister, string_u8_litexpr("4coder"), string_u8_litexpr(""),
 (void*)&default_color_table, 0);
 for(Color_Table_Node *node = color_table_list->first; node; node = node->next){
 lister_add_item(lister, node->name, string_u8_litexpr(""), (void*)&node->table, 0);
 }

 // NOTE(nasr): navigating previews themes live (byp_theme_navigate). Snapshot the current one so
 // Esc can put it back; opening no longer jumps to the first theme (it didn't even match the
 // highlighted "4coder" entry).
 local_persist Color_Table before = {};
 if(before.arrays == 0){ before = byp_init_color_table(app); }
 byp_copy_color_table(&before, target_color_table);

 lister.lister.current->handlers.navigate = byp_theme_navigate;
 Lister_Result l_result = vim_run_lister(app, lister);

 Color_Table *result = 0;
 if(!l_result.canceled){ result = (Color_Table*)l_result.user_data; }

 byp_copy_color_table(&target_color_table, (result != 0 ? *result : before));
 // the lister leaves "Theme:<name>" in the bottom bar on Return
 vim_reset_bottom_text();
}
