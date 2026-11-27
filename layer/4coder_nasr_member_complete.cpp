// NOTE(nasr): struct member completion, a standalone component.
//
// Given a cursor right after `expr.` or `expr->` (optionally with a partly typed field name),
// it resolves the struct type of `expr` heuristically and returns the matching field names.
// It is token based and only runs in C/C++ buffers. It shares no code with the struct
// position-context feature, so either can be refactored on its own.
//
// Resolution:
//  1. chain   walk back from the cursor over `a.b[i]->c.` collecting [a, b, c]
//  2. root    nearest earlier declaration `Type a` / `Type *a` in this buffer, else a global
//             from the code index
//  3. struct  the code index type note for `Type` -> its `{...}` body, following typedefs
//  4. fields  identifiers that end a declarator at the struct's own level (members of
//             anonymous struct/union are included); every chain step resolves the next
//             field's type the same way
//
// API
//  member_complete_query(app, arena, buffer, pos)   what can be completed at pos
//  nasr_complete                                    Tab: members when they resolve, else word_complete
//
// Cost: one backward token scan to find the declaration plus a walk over the code index
// notes per chain step. Callers that run every frame should cache on buffer/pos/history.

#define MEMBER_COMPLETE_MAX_CHAIN      8
#define MEMBER_COMPLETE_MAX_FIELDS     256
#define MEMBER_COMPLETE_MAX_NEEDLE     128
#define MEMBER_COMPLETE_SCAN_LIMIT     200000
#define MEMBER_COMPLETE_TYPEDEF_DEPTH  4

struct Member_Complete{
 b32 resolved;              // the expression before the cursor resolved to a struct
 Range_i64 needle;          // the partly typed field name, may be empty
 String_Const_u8 *matches;  // fields starting with the needle (case-insensitive), in order
 i32 match_count;
};

struct Member_Complete_Field{
 String_Const_u8 name;
 String_Const_u8 type;      // empty when the type isn't a plain identifier
};

struct Member_Complete_Struct{
 Member_Complete_Field *fields;
 i32 count;
};

//- Token helpers

function b32
member_complete_is_access(Token *token){
 return(token != 0 && (token->sub_kind == TokenCppKind_Dot || token->sub_kind == TokenCppKind_Arrow));
}

function b32
member_complete_is_identifier(Token *token){
 return(token != 0 && token->sub_kind == TokenCppKind_Identifier);
}

// qualifiers between a type and a declarator name: `Type const *name`
function b32
member_complete_is_declarator_noise(Token *token){
 return(token != 0 && (token->sub_kind == TokenCppKind_Star  ||
                       token->sub_kind == TokenCppKind_And   ||
                       token->sub_kind == TokenCppKind_Const ||
                       token->sub_kind == TokenCppKind_Volatile));
}

function b32
member_complete_token_matches(Application_Links *app, Buffer_ID buffer, Token *token, String_Const_u8 str){
 u8 space[MEMBER_COMPLETE_MAX_NEEDLE];
 if(token == 0 || token->size != (i64)str.size || str.size > sizeof(space)){ return false; }
 if(!buffer_read_range(app, buffer, Ii64_size(token->pos, token->size), space)){ return false; }
 return(string_match(SCu8(space, str.size), str));
}

//- 1. chain

// walks back from the access operator before `needle_min`; fills `chain` root first
function i32
member_complete_parse_chain(Application_Links *app, Arena *arena, Buffer_ID buffer, Token_Array *tokens,
                            i64 needle_min, String_Const_u8 *chain, i64 *root_pos){
 if(needle_min <= 0){ return 0; }
 Token_Iterator_Array it = token_iterator_pos(0, tokens, needle_min - 1);
 Token *token = token_it_read(&it);
 if(token != 0 && token->kind == TokenBaseKind_Whitespace){
  if(!token_it_dec_non_whitespace(&it)){ return 0; }
  token = token_it_read(&it);
 }

 String_Const_u8 reversed[MEMBER_COMPLETE_MAX_CHAIN];
 i32 count = 0;
 while(member_complete_is_access(token)){
  if(!token_it_dec_non_whitespace(&it)){ return 0; }
  token = token_it_read(&it);

  // skip index expressions: a[i][j].
  while(token != 0 && token->sub_kind == TokenCppKind_BrackCl){
   for(i32 nest = 0;;){
    if(token->sub_kind == TokenCppKind_BrackCl){ nest += 1; }
    if(token->sub_kind == TokenCppKind_BrackOp){ nest -= 1; }
    if(nest == 0){ break; }
    if(!token_it_dec_non_whitespace(&it)){ return 0; }
    token = token_it_read(&it);
    if(token == 0){ return 0; }
   }
   if(!token_it_dec_non_whitespace(&it)){ return 0; }
   token = token_it_read(&it);
  }

  // calls, casts and `this->` aren't resolvable here
  if(!member_complete_is_identifier(token) || count == MEMBER_COMPLETE_MAX_CHAIN){ return 0; }
  reversed[count++] = push_token_lexeme(app, arena, buffer, token);
  *root_pos = token->pos;

  if(!token_it_dec_non_whitespace(&it)){ break; }
  token = token_it_read(&it);
 }

 for(i32 i = 0; i < count; i += 1){ chain[i] = reversed[count - 1 - i]; }
 return(count);
}

//- 2. root declaration

// `it` sits on a name token; returns the declared type if this is a declaration of that name
function String_Const_u8
member_complete_declared_type(Application_Links *app, Arena *arena, Buffer_ID buffer, Token_Iterator_Array it){
 Token_Iterator_Array after = it;
 if(!token_it_inc_non_whitespace(&after)){ return {}; }
 Token *next = token_it_read(&after);
 if(next == 0){ return {}; }
 switch(next->sub_kind){
  case TokenCppKind_Semicolon: case TokenCppKind_Eq:      case TokenCppKind_Comma:
  case TokenCppKind_ParenCl:   case TokenCppKind_BrackOp: case TokenCppKind_Colon:
  case TokenCppKind_BraceOp: break;
  default: return {};
 }

 if(!token_it_dec_non_whitespace(&it)){ return {}; }
 Token *token = token_it_read(&it);
 while(member_complete_is_declarator_noise(token)){
  if(!token_it_dec_non_whitespace(&it)){ return {}; }
  token = token_it_read(&it);
 }
 if(!member_complete_is_identifier(token)){ return {}; }
 return(push_token_lexeme(app, arena, buffer, token));
}

function String_Const_u8
member_complete_find_root_type(Application_Links *app, Arena *arena, Buffer_ID buffer, Token_Array *tokens,
                               String_Const_u8 name, i64 root_pos){
 // nearest earlier declaration in this buffer (locals, parameters, file scope)
 Token_Iterator_Array it = token_iterator_pos(0, tokens, root_pos);
 for(i32 i = 0; i < MEMBER_COMPLETE_SCAN_LIMIT && token_it_dec_non_whitespace(&it); i += 1){
  Token *token = token_it_read(&it);
  if(member_complete_is_identifier(token) && member_complete_token_matches(app, buffer, token, name)){
   String_Const_u8 type = member_complete_declared_type(app, arena, buffer, it);
   if(type.size > 0){ return(type); }
  }
 }

 // globals declared in other buffers, or later in this one
 Buffer_ID found_buffer = 0;
 i64 found_pos = 0;
 code_index_lock();
 for(Buffer_ID b = get_buffer_next(app, 0, Access_Always); b != 0 && found_buffer == 0;
     b = get_buffer_next(app, b, Access_Always)){
  Code_Index_File *file = code_index_get_file(b);
  if(file == 0){ continue; }
  for(i32 i = 0; i < file->note_array.count; i += 1){
   Code_Index_Note *note = file->note_array.ptrs[i];
   if(note->note_kind == CodeIndexNote_Global && string_match(note->text, name)){
    found_buffer = b;
    found_pos = note->pos.min;
    break;
   }
  }
 }
 code_index_unlock();

 if(found_buffer != 0){
  Token_Array global_tokens = get_token_array_from_buffer(app, found_buffer);
  if(global_tokens.tokens != 0){
   Token_Iterator_Array git = token_iterator_pos(0, &global_tokens, found_pos);
   return(member_complete_declared_type(app, arena, found_buffer, git));
  }
 }
 return {};
}

//- 3. struct lookup

struct Member_Complete_Body{
 Buffer_ID buffer;
 Range_i64 range;            // from `{` through `}`
 String_Const_u8 alias;      // set instead of range for `typedef X Name;`
};

// classifies the type note at `name`: a body after it, a body before it, or a typedef alias
function Member_Complete_Body
member_complete_body_at_note(Application_Links *app, Arena *arena, Buffer_ID buffer, Token_Array *tokens, Range_i64 name){
 Member_Complete_Body result = {};
 Token_Iterator_Array start = token_iterator_pos(0, tokens, name.min);

 // `struct Name {` or `struct Name : Base {`
 Token_Iterator_Array it = start;
 for(i32 i = 0; i < 64 && token_it_inc_non_whitespace(&it); i += 1){
  Token *token = token_it_read(&it);
  if(token == 0 || token->sub_kind == TokenCppKind_Semicolon || token->sub_kind == TokenCppKind_ParenOp){ break; }
  if(token->sub_kind == TokenCppKind_BraceOp){
   i64 open = token->pos;
   for(i32 nest = 0;;){
    token = token_it_read(&it);
    if(token == 0){ return {}; }
    if(token->sub_kind == TokenCppKind_BraceOp){ nest += 1; }
    if(token->sub_kind == TokenCppKind_BraceCl){
     nest -= 1;
     if(nest == 0){
      result.buffer = buffer;
      result.range = Ii64(open, token->pos + token->size);
      return(result);
     }
    }
    if(!token_it_inc_non_whitespace(&it)){ return {}; }
   }
  }
  if(i == 0 && token->sub_kind != TokenCppKind_Colon){ break; }
 }

 // `typedef struct { ... } Name;` or `typedef X Name;`
 it = start;
 if(!token_it_dec_non_whitespace(&it)){ return {}; }
 Token *prev = token_it_read(&it);
 while(member_complete_is_declarator_noise(prev)){
  if(!token_it_dec_non_whitespace(&it)){ return {}; }
  prev = token_it_read(&it);
 }
 if(prev != 0 && prev->sub_kind == TokenCppKind_BraceCl){
  i64 close_end = prev->pos + prev->size;
  for(i32 nest = 0;;){
   if(prev->sub_kind == TokenCppKind_BraceCl){ nest += 1; }
   if(prev->sub_kind == TokenCppKind_BraceOp){
    nest -= 1;
    if(nest == 0){
     result.buffer = buffer;
     result.range = Ii64(prev->pos, close_end);
     return(result);
    }
   }
   if(!token_it_dec_non_whitespace(&it)){ return {}; }
   prev = token_it_read(&it);
   if(prev == 0){ return {}; }
  }
 }
 if(member_complete_is_identifier(prev)){
  result.alias = push_token_lexeme(app, arena, buffer, prev);
 }
 return(result);
}

function Member_Complete_Struct member_complete_struct_fields(Application_Links *app, Arena *arena, Buffer_ID buffer, Range_i64 body);

function b32
member_complete_resolve_struct(Application_Links *app, Arena *arena, String_Const_u8 type, Member_Complete_Struct *out){
 for(i32 depth = 0; depth < MEMBER_COMPLETE_TYPEDEF_DEPTH && type.size > 0; depth += 1){
  // collect candidate notes under the lock, inspect tokens after
  struct Candidate{ Buffer_ID buffer; Range_i64 name; };
  Candidate candidates[16];
  i32 candidate_count = 0;
  code_index_lock();
  for(Buffer_ID b = get_buffer_next(app, 0, Access_Always); b != 0 && candidate_count < ArrayCount(candidates);
      b = get_buffer_next(app, b, Access_Always)){
   Code_Index_File *file = code_index_get_file(b);
   if(file == 0){ continue; }
   for(i32 i = 0; i < file->note_array.count && candidate_count < ArrayCount(candidates); i += 1){
    Code_Index_Note *note = file->note_array.ptrs[i];
    if(note->note_kind == CodeIndexNote_Type && string_match(note->text, type)){
     candidates[candidate_count++] = {b, note->pos};
    }
   }
  }
  code_index_unlock();

  String_Const_u8 alias = {};
  for(i32 i = 0; i < candidate_count; i += 1){
   Token_Array tokens = get_token_array_from_buffer(app, candidates[i].buffer);
   if(tokens.tokens == 0){ continue; }
   Member_Complete_Body body = member_complete_body_at_note(app, arena, candidates[i].buffer, &tokens, candidates[i].name);
   if(body.buffer != 0){
    *out = member_complete_struct_fields(app, arena, body.buffer, body.range);
    return(true);
   }
   if(alias.size == 0 && body.alias.size > 0 && !string_match(body.alias, type)){ alias = body.alias; }
  }
  type = alias;
 }
 return(false);
}

//- 4. fields

function Member_Complete_Struct
member_complete_struct_fields(Application_Links *app, Arena *arena, Buffer_ID buffer, Range_i64 body){
 Member_Complete_Struct result = {};
 Token_Array tokens = get_token_array_from_buffer(app, buffer);
 if(tokens.tokens == 0){ return(result); }

 // the tokens strictly inside the braces, without whitespace and comments
 i64 first = token_index_from_pos(&tokens, body.min);
 i64 last  = token_index_from_pos(&tokens, body.max - 1);
 Scratch_Block scratch(app, arena);
 Token **v = push_array(scratch, Token*, Max(1, last - first));
 i32 n = 0;
 for(i64 i = first + 1; i < last; i += 1){
  Token *token = tokens.tokens + i;
  if(token->kind == TokenBaseKind_Whitespace || token->kind == TokenBaseKind_Comment){ continue; }
  v[n++] = token;
 }

 result.fields = push_array(arena, Member_Complete_Field, MEMBER_COMPLETE_MAX_FIELDS);
 String_Const_u8 last_type = {};
 i32 paren = 0;
 for(i32 i = 0; i < n && result.count < MEMBER_COMPLETE_MAX_FIELDS; i += 1){
  Token *token = v[i];
  switch(token->sub_kind){
   case TokenCppKind_BraceOp:{
    // anonymous struct/union members are reachable directly; skip any other block
    // (method bodies, nested named types, enums, initializers)
    b32 anonymous = (i > 0 && (v[i-1]->sub_kind == TokenCppKind_Struct || v[i-1]->sub_kind == TokenCppKind_Union));
    if(!anonymous){
     for(i32 nest = 0; i < n; i += 1){
      if(v[i]->sub_kind == TokenCppKind_BraceOp){ nest += 1; }
      if(v[i]->sub_kind == TokenCppKind_BraceCl){ nest -= 1; if(nest == 0){ break; } }
     }
    }
   }break;
   case TokenCppKind_ParenOp:   { paren += 1; }break;
   case TokenCppKind_ParenCl:   { paren -= 1; }break;
   case TokenCppKind_Semicolon: { last_type = {}; }break;
   case TokenCppKind_Eq:{
    // default initializer (`Foo x = bar;`, `int a = f(1, 2), b;`): skip to the `,` / `;`
    // that ends it, so identifiers inside aren't taken as fields
    if(paren != 0){ break; }
    for(i32 nest = 0; i + 1 < n; i += 1){
     i16 k = v[i+1]->sub_kind;
     if(nest == 0 && (k == TokenCppKind_Comma || k == TokenCppKind_Semicolon)){ break; }
     if(k == TokenCppKind_ParenOp || k == TokenCppKind_BraceOp || k == TokenCppKind_BrackOp){ nest += 1; }
     if(k == TokenCppKind_ParenCl || k == TokenCppKind_BraceCl || k == TokenCppKind_BrackCl){
      if(nest == 0){ break; }
      nest -= 1;
     }
    }
   }break;
   case TokenCppKind_Identifier:{
    if(paren != 0 || i + 1 >= n){ break; }
    switch(v[i+1]->sub_kind){
     case TokenCppKind_Semicolon: case TokenCppKind_Comma: case TokenCppKind_BrackOp:
     case TokenCppKind_Eq:        case TokenCppKind_Colon: break;
     default: continue;
    }
    i32 j = i - 1;
    while(j >= 0 && member_complete_is_declarator_noise(v[j])){ j -= 1; }
    String_Const_u8 type = {};
    if(j >= 0 && member_complete_is_identifier(v[j])){
     type = push_token_lexeme(app, arena, buffer, v[j]);
     last_type = type;
    }
    else if(j >= 0 && v[j]->sub_kind == TokenCppKind_Comma){
     type = last_type;
    }
    else{
     last_type = {};
    }
    Member_Complete_Field *field = result.fields + result.count++;
    field->name = push_token_lexeme(app, arena, buffer, token);
    field->type = type;
   }break;
  }
 }
 return(result);
}

//- Query

function Member_Complete
member_complete_query(Application_Links *app, Arena *arena, Buffer_ID buffer, i64 pos){
 Member_Complete result = {};
 Nasr_Language lang = nasr_buffer_language(app, buffer);
 if(lang != NasrLang_C && lang != NasrLang_Cpp && lang != NasrLang_CHeader){ return(result); }
 Token_Array tokens = get_token_array_from_buffer(app, buffer);
 if(tokens.tokens == 0){ return(result); }

 Range_i64 needle = Ii64(pos);
 for(i32 i = 0; i < MEMBER_COMPLETE_MAX_NEEDLE && needle.min > 0; i += 1){
  u8 c = buffer_get_char(app, buffer, needle.min - 1);
  if(!(character_is_alpha_numeric(c) || c == '_')){ break; }
  needle.min -= 1;
 }
 // NOTE(nasr): the replace range also covers the rest of the identifier after the cursor
 // (`a.fo|o`), matching still only uses what's before the cursor
 i64 buffer_size = buffer_get_size(app, buffer);
 for(i32 i = 0; i < MEMBER_COMPLETE_MAX_NEEDLE && needle.max < buffer_size; i += 1){
  u8 c = buffer_get_char(app, buffer, needle.max);
  if(!(character_is_alpha_numeric(c) || c == '_')){ break; }
  needle.max += 1;
 }

 Scratch_Block scratch(app, arena);
 String_Const_u8 chain[MEMBER_COMPLETE_MAX_CHAIN];
 i64 root_pos = 0;
 i32 chain_count = member_complete_parse_chain(app, scratch, buffer, &tokens, needle.min, chain, &root_pos);
 if(chain_count == 0){ return(result); }

 String_Const_u8 type = member_complete_find_root_type(app, scratch, buffer, &tokens, chain[0], root_pos);
 Member_Complete_Struct s = {};
 if(!member_complete_resolve_struct(app, scratch, type, &s)){ return(result); }
 for(i32 k = 1; k < chain_count; k += 1){
  String_Const_u8 field_type = {};
  for(i32 i = 0; i < s.count; i += 1){
   if(string_match(s.fields[i].name, chain[k])){ field_type = s.fields[i].type; break; }
  }
  if(!member_complete_resolve_struct(app, scratch, field_type, &s)){ return(result); }
 }

 result.resolved = true;
 result.needle = needle;
 String_Const_u8 typed = push_buffer_range(app, scratch, buffer, Ii64(needle.min, pos));
 result.matches = push_array(arena, String_Const_u8, Max(1, s.count));
 // NOTE(nasr): two passes: exact-case prefix matches first, then the case-insensitive rest
 for(i32 pass = 0; pass < 2; pass += 1){
  for(i32 i = 0; i < s.count; i += 1){
   String_Const_u8 name = s.fields[i].name;
   if(name.size < typed.size){ continue; }
   String_Const_u8 prefix = string_prefix(name, typed.size);
   b32 exact = string_match(prefix, typed);
   if(pass == 0 ? exact : (!exact && string_match_insensitive(prefix, typed))){
    result.matches[result.match_count++] = push_string_copy(arena, name);
   }
  }
 }
 return(result);
}

//- Tab command

CUSTOM_COMMAND_SIG(nasr_complete)
CUSTOM_DOC("Completes struct members after `.` or `->` when the type resolves, otherwise falls back to word_complete. Repeat to cycle.")
{
 View_ID view = get_active_view(app, Access_ReadWriteVisible);
 Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
 if(buffer == 0){ return; }

 struct Cycle{
  b32 active;
  Buffer_ID buffer;
  Range_i64 range;            // the text currently standing in for the completion
  String_Const_u8 typed;      // what was typed before cycling started
  String_Const_u8 *matches;
  i32 count;
  i32 index;
 };
 local_persist Arena arena = make_arena_system();
 local_persist Cycle cycle = {};

 Managed_Scope scope = view_get_managed_scope(app, view);
 Rewrite_Type *rewrite = scope_attachment(app, scope, view_rewrite_loc, Rewrite_Type);
 i64 pos = view_get_cursor_pos(app, view);
 b32 continuing = (cycle.active && rewrite != 0 && *rewrite == Rewrite_WordComplete &&
                   cycle.buffer == buffer && cycle.range.max == pos);
 if(!continuing){
  cycle = {};
  linalloc_clear(&arena);
  Member_Complete mc = member_complete_query(app, &arena, buffer, pos);
  if(!mc.resolved){
   word_complete(app);
   return;
  }
  if(mc.match_count == 0){ return; }
  cycle.active = true;
  cycle.buffer = buffer;
  cycle.range = mc.needle;
  cycle.typed = push_buffer_range(app, &arena, buffer, mc.needle);
  cycle.matches = mc.matches;
  cycle.count = mc.match_count;
  cycle.index = -1;
 }

 // cycle through the matches, then back to what was typed
 cycle.index = (cycle.index + 1) % (cycle.count + 1);
 String_Const_u8 str = (cycle.index == cycle.count ? cycle.typed : cycle.matches[cycle.index]);
 buffer_replace_range(app, buffer, cycle.range, str);
 cycle.range.max = cycle.range.min + str.size;
 view_set_cursor_and_preferred_x(app, view, seek_pos(cycle.range.max));
 set_next_rewrite(app, view, Rewrite_WordComplete);
}
