// NOTE(nasr): Odin support.
//
// - nasr_lex_odin: a hand-written lexer (no lexer generator step). Tokens reuse the C++
//   sub-kinds (BraceOp, Eq, Comma, ...) so indentation, scope matching, vim motions and the
//   layer's token coloring work unchanged. Primitives / control flow use the byp kinds;
//   struct/union/enum/proc/... use byp_TokenKind_TypeKeyword (struct color, but no "};"
//   auto-insert like C's byp_TokenKind_Struct).
// - nasr_odin_index: runs after the generic (non-C++) code index parse and adds notes for Odin
//   declarations, which drive function/type/enum/global coloring:
//     name :: proc ...                 Function   (also proc groups, #force_inline proc)
//     Name :: #type proc ...           Type
//     Name :: struct|union|enum|...    Type       (enum members become Enum notes)
//     Name :: distinct T / f32 / []T   Type
//     NAME :: value                    Global     (constant)
//     name : T = v / name := v         Global     (file scope only)
//
// Dispatch (which lexer / indexer a buffer gets) is nasr_buffer_is_odin, used from the default
// hooks (full lex, incremental relex, code index tick).

struct Nasr_Odin_Keyword{
 char *str;
 Token_Base_Kind kind;
 i16 sub_kind;
};

global Nasr_Odin_Keyword nasr_odin_keywords[] = {
 // control flow
 {"if",          byp_TokenKind_ControlFlow, TokenCppKind_If},
 {"else",        byp_TokenKind_ControlFlow, TokenCppKind_Else},
 {"for",         byp_TokenKind_ControlFlow, TokenCppKind_For},
 {"switch",      byp_TokenKind_ControlFlow, TokenCppKind_Switch},
 {"case",        byp_TokenKind_ControlFlow, TokenCppKind_Case},
 {"break",       byp_TokenKind_ControlFlow, TokenCppKind_Break},
 {"continue",    byp_TokenKind_ControlFlow, TokenCppKind_Continue},
 {"return",      byp_TokenKind_ControlFlow, TokenCppKind_Return},
 {"do",          byp_TokenKind_ControlFlow, TokenCppKind_Do},
 {"fallthrough", byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},
 {"defer",       byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},
 {"when",        byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},
 {"in",          byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},
 {"not_in",      byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},
 {"or_else",     byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},
 {"or_return",   byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},
 {"or_break",    byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},
 {"or_continue", byp_TokenKind_ControlFlow, TokenCppKind_KeywordGeneric},

 // type constructors
 {"struct",      byp_TokenKind_TypeKeyword, TokenCppKind_Struct},
 {"union",       byp_TokenKind_TypeKeyword, TokenCppKind_Union},
 {"enum",        byp_TokenKind_TypeKeyword, TokenCppKind_Enum},
 {"proc",        byp_TokenKind_TypeKeyword, TokenCppKind_KeywordGeneric},
 {"bit_set",     byp_TokenKind_TypeKeyword, TokenCppKind_KeywordGeneric},
 {"bit_field",   byp_TokenKind_TypeKeyword, TokenCppKind_KeywordGeneric},
 {"map",         byp_TokenKind_TypeKeyword, TokenCppKind_KeywordGeneric},
 {"matrix",      byp_TokenKind_TypeKeyword, TokenCppKind_KeywordGeneric},
 {"dynamic",     byp_TokenKind_TypeKeyword, TokenCppKind_KeywordGeneric},
 {"distinct",    byp_TokenKind_TypeKeyword, TokenCppKind_KeywordGeneric},

 // other keywords
 {"package",     TokenBaseKind_Keyword, TokenCppKind_KeywordGeneric},
 {"import",      TokenBaseKind_Keyword, TokenCppKind_KeywordGeneric},
 {"foreign",     TokenBaseKind_Keyword, TokenCppKind_Extern},
 {"using",       TokenBaseKind_Keyword, TokenCppKind_Using},
 {"cast",        TokenBaseKind_Keyword, TokenCppKind_KeywordGeneric},
 {"transmute",   TokenBaseKind_Keyword, TokenCppKind_KeywordGeneric},
 {"auto_cast",   TokenBaseKind_Keyword, TokenCppKind_KeywordGeneric},
 {"context",     TokenBaseKind_Keyword, TokenCppKind_KeywordGeneric},
 {"where",       TokenBaseKind_Keyword, TokenCppKind_KeywordGeneric},
 {"asm",         TokenBaseKind_Keyword, TokenCppKind_Asm},
 {"true",        TokenBaseKind_Keyword, TokenCppKind_LiteralTrue},
 {"false",       TokenBaseKind_Keyword, TokenCppKind_LiteralFalse},
 {"nil",         TokenBaseKind_Keyword, TokenCppKind_Null},

 // primitive types
 {"bool",  byp_TokenKind_Primitive, TokenCppKind_Bool},
 {"b8",    byp_TokenKind_Primitive, TokenCppKind_Bool},
 {"b16",   byp_TokenKind_Primitive, TokenCppKind_Bool},
 {"b32",   byp_TokenKind_Primitive, TokenCppKind_Bool},
 {"b64",   byp_TokenKind_Primitive, TokenCppKind_Bool},
 {"int",   byp_TokenKind_Primitive, TokenCppKind_Int},
 {"uint",  byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i8",    byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i16",   byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i32",   byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i64",   byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i128",  byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u8",    byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u16",   byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u32",   byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u64",   byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u128",  byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i16le", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i32le", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i64le", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i128le",byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u16le", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u32le", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u64le", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u128le",byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i16be", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i32be", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i64be", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"i128be",byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u16be", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u32be", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u64be", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"u128be",byp_TokenKind_Primitive, TokenCppKind_Int},
 {"uintptr", byp_TokenKind_Primitive, TokenCppKind_Int},
 {"byte",  byp_TokenKind_Primitive, TokenCppKind_Char},
 {"rune",  byp_TokenKind_Primitive, TokenCppKind_Char},
 {"f16",   byp_TokenKind_Primitive, TokenCppKind_Float},
 {"f32",   byp_TokenKind_Primitive, TokenCppKind_Float},
 {"f64",   byp_TokenKind_Primitive, TokenCppKind_Double},
 {"f16le", byp_TokenKind_Primitive, TokenCppKind_Float},
 {"f32le", byp_TokenKind_Primitive, TokenCppKind_Float},
 {"f64le", byp_TokenKind_Primitive, TokenCppKind_Double},
 {"f16be", byp_TokenKind_Primitive, TokenCppKind_Float},
 {"f32be", byp_TokenKind_Primitive, TokenCppKind_Float},
 {"f64be", byp_TokenKind_Primitive, TokenCppKind_Double},
 {"complex32",     byp_TokenKind_Primitive, TokenCppKind_Float},
 {"complex64",     byp_TokenKind_Primitive, TokenCppKind_Float},
 {"complex128",    byp_TokenKind_Primitive, TokenCppKind_Double},
 {"quaternion64",  byp_TokenKind_Primitive, TokenCppKind_Float},
 {"quaternion128", byp_TokenKind_Primitive, TokenCppKind_Float},
 {"quaternion256", byp_TokenKind_Primitive, TokenCppKind_Double},
 {"string",  byp_TokenKind_Primitive, TokenCppKind_Char},
 {"cstring", byp_TokenKind_Primitive, TokenCppKind_Char},
 {"rawptr",  byp_TokenKind_Primitive, TokenCppKind_Void},
 {"typeid",  byp_TokenKind_Primitive, TokenCppKind_Void},
 {"any",     byp_TokenKind_Primitive, TokenCppKind_Void},
};

struct Nasr_Odin_Operator{
 char *str;
 Token_Base_Kind kind;
 i16 sub_kind;
};

// Longest first: the lexer takes the first match.
global Nasr_Odin_Operator nasr_odin_operators[] = {
 {"..<", TokenBaseKind_Operator, TokenCppKind_DotDotDot},
 {"..=", TokenBaseKind_Operator, TokenCppKind_DotDotDot},
 {"---", TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"<<=", TokenBaseKind_Operator, TokenCppKind_LeftLeftEq},
 {">>=", TokenBaseKind_Operator, TokenCppKind_RightRightEq},
 {"&&=", TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"||=", TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"%%=", TokenBaseKind_Operator, TokenCppKind_ModEq},
 {"&~=", TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"::",  TokenBaseKind_Operator, TokenCppKind_ColonColon},
 {"->",  TokenBaseKind_Operator, TokenCppKind_Arrow},
 {"..",  TokenBaseKind_Operator, TokenCppKind_DotDotDot},
 {"==",  TokenBaseKind_Operator, TokenCppKind_EqEq},
 {"!=",  TokenBaseKind_Operator, TokenCppKind_NotEq},
 {"<=",  TokenBaseKind_Operator, TokenCppKind_LessEq},
 {">=",  TokenBaseKind_Operator, TokenCppKind_GrtrEq},
 {"&&",  TokenBaseKind_Operator, TokenCppKind_AndAnd},
 {"||",  TokenBaseKind_Operator, TokenCppKind_OrOr},
 {"<<",  TokenBaseKind_Operator, TokenCppKind_LeftLeft},
 {">>",  TokenBaseKind_Operator, TokenCppKind_RightRight},
 {"+=",  TokenBaseKind_Operator, TokenCppKind_PlusEq},
 {"-=",  TokenBaseKind_Operator, TokenCppKind_MinusEq},
 {"*=",  TokenBaseKind_Operator, TokenCppKind_StarEq},
 {"/=",  TokenBaseKind_Operator, TokenCppKind_DivEq},
 {"%=",  TokenBaseKind_Operator, TokenCppKind_ModEq},
 {"&=",  TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"|=",  TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"~=",  TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"&~",  TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"%%",  TokenBaseKind_Operator, TokenCppKind_Mod},
 {"{",   TokenBaseKind_ScopeOpen,          TokenCppKind_BraceOp},
 {"}",   TokenBaseKind_ScopeClose,         TokenCppKind_BraceCl},
 {"(",   TokenBaseKind_ParentheticalOpen,  TokenCppKind_ParenOp},
 {")",   TokenBaseKind_ParentheticalClose, TokenCppKind_ParenCl},
 {"[",   TokenBaseKind_ParentheticalOpen,  TokenCppKind_BrackOp},
 {"]",   TokenBaseKind_ParentheticalClose, TokenCppKind_BrackCl},
 {";",   TokenBaseKind_StatementClose,     TokenCppKind_Semicolon},
 {",",   TokenBaseKind_StatementClose,     TokenCppKind_Comma},
 {":",   TokenBaseKind_Operator, TokenCppKind_Colon},
 {"=",   TokenBaseKind_Operator, TokenCppKind_Eq},
 {".",   TokenBaseKind_Operator, TokenCppKind_Dot},
 {"+",   TokenBaseKind_Operator, TokenCppKind_Plus},
 {"-",   TokenBaseKind_Operator, TokenCppKind_Minus},
 {"*",   TokenBaseKind_Operator, TokenCppKind_Star},
 {"/",   TokenBaseKind_Operator, TokenCppKind_Div},
 {"%",   TokenBaseKind_Operator, TokenCppKind_Mod},
 {"!",   TokenBaseKind_Operator, TokenCppKind_Not},
 {"~",   TokenBaseKind_Operator, TokenCppKind_Tilde},
 {"&",   TokenBaseKind_Operator, TokenCppKind_And},
 {"|",   TokenBaseKind_Operator, TokenCppKind_Or},
 {"^",   TokenBaseKind_Operator, TokenCppKind_Xor},
 {"<",   TokenBaseKind_Operator, TokenCppKind_Less},
 {">",   TokenBaseKind_Operator, TokenCppKind_Grtr},
 {"?",   TokenBaseKind_Operator, TokenCppKind_Ternary},
 {"$",   TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
 {"@",   TokenBaseKind_Operator, TokenCppKind_KeywordGeneric},
};

function b32
nasr_odin_is_ident_start(u8 c){
 return(character_is_alpha(c) || c >= 0x80);
}

function b32
nasr_odin_is_ident(u8 c){
 return(character_is_alpha(c) || ('0' <= c && c <= '9') || c >= 0x80);
}

function b32
nasr_odin_is_digit(u8 c){
 return('0' <= c && c <= '9');
}

function b32
nasr_odin_is_hex(u8 c){
 return(nasr_odin_is_digit(c) || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F'));
}

function Token_List
nasr_lex_odin(Arena *arena, String_Const_u8 text){
 Token_List list = {};
 u8 *s = text.str;
 i64 n = (i64)text.size;
 i64 i = 0;
#define NasrOdinAt(k) ((i + (k)) < n ? s[i + (k)] : 0)
 while (i < n){
  i64 start = i;
  u8 c = s[i];
  Token token = {};
  token.pos = start;
  token.kind = TokenBaseKind_LexError;
  token.sub_kind = TokenCppKind_LexError;

  if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'){
   for (; i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == '\f' || s[i] == '\v'); i += 1);
   token.kind = TokenBaseKind_Whitespace;
   token.sub_kind = TokenCppKind_Whitespace;
  }
  else if ((c == '/' && NasrOdinAt(1) == '/') || (start == 0 && c == '#' && NasrOdinAt(1) == '!')){
   for (; i < n && s[i] != '\n'; i += 1);
   token.kind = TokenBaseKind_Comment;
   token.sub_kind = TokenCppKind_LineComment;
  }
  else if (c == '/' && NasrOdinAt(1) == '*'){
   // Odin block comments nest.
   i += 2;
   i32 depth = 1;
   while (i < n && depth > 0){
    if (s[i] == '/' && NasrOdinAt(1) == '*'){ depth += 1; i += 2; }
    else if (s[i] == '*' && NasrOdinAt(1) == '/'){ depth -= 1; i += 2; }
    else{ i += 1; }
   }
   token.kind = TokenBaseKind_Comment;
   token.sub_kind = TokenCppKind_BlockComment;
  }
  else if (nasr_odin_is_ident_start(c)){
   for (; i < n && nasr_odin_is_ident(s[i]); i += 1);
   String_Const_u8 lexeme = SCu8(s + start, i - start);
   token.kind = TokenBaseKind_Identifier;
   token.sub_kind = TokenCppKind_Identifier;
   for (i32 k = 0; k < ArrayCount(nasr_odin_keywords); k += 1){
    if (string_match(lexeme, SCu8(nasr_odin_keywords[k].str))){
     token.kind = nasr_odin_keywords[k].kind;
     token.sub_kind = nasr_odin_keywords[k].sub_kind;
     break;
    }
   }
  }
  else if (nasr_odin_is_digit(c) || (c == '.' && nasr_odin_is_digit(NasrOdinAt(1)))){
   b32 is_float = false;
   if (c == '0' && (NasrOdinAt(1) == 'x' || NasrOdinAt(1) == 'X' || NasrOdinAt(1) == 'b' ||
                    NasrOdinAt(1) == 'o' || NasrOdinAt(1) == 'z' || NasrOdinAt(1) == 'h')){
    is_float = (NasrOdinAt(1) == 'h'); // 0h: hex float
    i += 2;
    for (; i < n && (nasr_odin_is_hex(s[i]) || s[i] == '_'); i += 1);
    token.sub_kind = is_float ? TokenCppKind_LiteralFloat64 : TokenCppKind_LiteralIntegerHex;
   }
   else{
    for (; i < n && (nasr_odin_is_digit(s[i]) || s[i] == '_'); i += 1);
    // "1..<5" is a range, not a float
    if (i < n && s[i] == '.' && NasrOdinAt(1) != '.'){
     is_float = true;
     i += 1;
     for (; i < n && (nasr_odin_is_digit(s[i]) || s[i] == '_'); i += 1);
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')){
     i64 k = i + 1;
     if (k < n && (s[k] == '+' || s[k] == '-')){ k += 1; }
     if (k < n && nasr_odin_is_digit(s[k])){
      is_float = true;
      i = k;
      for (; i < n && (nasr_odin_is_digit(s[i]) || s[i] == '_'); i += 1);
     }
    }
    token.sub_kind = is_float ? TokenCppKind_LiteralFloat64 : TokenCppKind_LiteralInteger;
   }
   // imaginary / quaternion suffix
   if (i < n && (s[i] == 'i' || s[i] == 'j' || s[i] == 'k') && !nasr_odin_is_ident(NasrOdinAt(1))){
    is_float = true;
    i += 1;
    token.sub_kind = TokenCppKind_LiteralFloat64;
   }
   token.kind = is_float ? TokenBaseKind_LiteralFloat : TokenBaseKind_LiteralInteger;
  }
  else if (c == '"' || c == '\''){
   i += 1;
   for (; i < n && s[i] != c && s[i] != '\n';){
    i += (s[i] == '\\' && i + 1 < n) ? 2 : 1;
   }
   if (i < n && s[i] == c){ i += 1; }
   token.kind = TokenBaseKind_LiteralString;
   token.sub_kind = (c == '"') ? TokenCppKind_LiteralString : TokenCppKind_LiteralCharacter;
  }
  else if (c == '`'){
   // raw string, may span lines
   i += 1;
   for (; i < n && s[i] != '`'; i += 1);
   if (i < n){ i += 1; }
   token.kind = TokenBaseKind_LiteralString;
   token.sub_kind = TokenCppKind_LiteralStringRaw;
  }
  else if (c == '#' && nasr_odin_is_ident_start(NasrOdinAt(1))){
   // directive: #load, #partial, #force_inline, #type, #assert, ...
   i += 1;
   for (; i < n && nasr_odin_is_ident(s[i]); i += 1);
   token.kind = TokenBaseKind_Preprocessor;
   token.sub_kind = TokenCppKind_KeywordGeneric;
  }
  else{
   for (i32 k = 0; k < ArrayCount(nasr_odin_operators); k += 1){
    String_Const_u8 op = SCu8(nasr_odin_operators[k].str);
    if ((i64)op.size <= n - i && block_match(s + i, op.str, op.size)){
     i += op.size;
     token.kind = nasr_odin_operators[k].kind;
     token.sub_kind = nasr_odin_operators[k].sub_kind;
     break;
    }
   }
   if (i == start){
    // unknown byte (or a stray '#'): one-byte error token
    i += 1;
   }
  }
#undef NasrOdinAt

  token.size = i - start;
  token_list_push(arena, &list, &token);
 }

 Token eof = {};
 eof.pos = n;
 eof.size = 0;
 eof.kind = TokenBaseKind_EOF;
 eof.sub_kind = TokenCppKind_EOF;
 token_list_push(arena, &list, &eof);
 return(list);
}

////////////////////////////////
// Code index

function b32
nasr_odin_tok_is(Generic_Parse_State *state, Token *token, char *str){
 return(token != 0 && string_match(string_substring(state->contents, Ii64(token)), SCu8(str)));
}

// Only whitespace between the start of the line and the token?
function b32
nasr_odin_first_on_line(Generic_Parse_State *state, Token *token){
 for (i64 p = token->pos - 1; p >= 0; p -= 1){
  u8 c = state->contents.str[p];
  if (c == '\n'){ return(true); }
  if (c != ' ' && c != '\t' && c != '\r'){ return(false); }
 }
 return(true);
}

function void
nasr_odin_index_enum_members(Code_Index_File *index, Generic_Parse_State *state, Token **t, i64 m, i64 k){
 // t[k] is "enum": skip the backing type up to "{"
 for (; k < m && t[k]->kind != TokenBaseKind_ScopeOpen; k += 1){
  if (t[k]->kind == TokenBaseKind_StatementClose || t[k]->kind == TokenBaseKind_ScopeClose){ return; }
 }
 if (k >= m){ return; }
 k += 1;
 b32 expect_member = true;
 i32 nest = 0;
 for (; k < m; k += 1){
  Token *token = t[k];
  if (token->kind == TokenBaseKind_ScopeOpen || token->kind == TokenBaseKind_ParentheticalOpen){ nest += 1; continue; }
  if (token->kind == TokenBaseKind_ParentheticalClose){ nest -= 1; continue; }
  if (token->kind == TokenBaseKind_ScopeClose){
   if (nest == 0){ break; }
   nest -= 1;
   continue;
  }
  if (nest > 0){ continue; }
  if (token->sub_kind == TokenCppKind_Comma){ expect_member = true; continue; }
  if (expect_member && token->kind == TokenBaseKind_Identifier){
   index_new_note(index, state, Ii64(token), CodeIndexNote_Enum, 0);
  }
  expect_member = false;
 }
}

function void
nasr_odin_index(Code_Index_File *index, Generic_Parse_State *state){
 Token *tokens = state->it.tokens;
 i64 count = state->it.count;
 if (tokens == 0 || count == 0){ return; }

 // significant tokens only
 Token **t = push_array(state->arena, Token*, count);
 i64 m = 0;
 for (i64 k = 0; k < count; k += 1){
  Token *token = tokens + k;
  if (token->kind == TokenBaseKind_Whitespace || token->kind == TokenBaseKind_Comment ||
      token->kind == TokenBaseKind_EOF){
   continue;
  }
  t[m++] = token;
 }

 i32 depth = 0;       // { }
 i32 paren_depth = 0; // ( ) [ ]: multi-line proc parameters aren't declarations
 for (i64 k = 0; k < m; k += 1){
  Token *token = t[k];
  if (token->kind == TokenBaseKind_ScopeOpen){ depth += 1; continue; }
  if (token->kind == TokenBaseKind_ScopeClose){ depth -= (depth > 0); continue; }
  if (token->kind == TokenBaseKind_ParentheticalOpen){ paren_depth += 1; continue; }
  if (token->kind == TokenBaseKind_ParentheticalClose){ paren_depth -= (paren_depth > 0); continue; }
  if (token->kind != TokenBaseKind_Identifier || k + 1 >= m || paren_depth > 0){ continue; }

  // Declarations start a statement. Odin has no ';' at line ends, so "first on its line"
  // counts, as does following an attribute: @(private) foo :: ..., @private foo :: ...
  Token *prev = (k > 0) ? t[k - 1] : 0;
  b32 at_start = (prev == 0 || nasr_odin_first_on_line(state, token) ||
                  prev->kind == TokenBaseKind_StatementClose ||
                  prev->kind == TokenBaseKind_ScopeOpen ||
                  prev->kind == TokenBaseKind_ScopeClose);
  if (!at_start && prev->kind == TokenBaseKind_Identifier && k >= 2 && nasr_odin_tok_is(state, t[k - 2], "@")){
   at_start = true;
  }
  if (!at_start && prev->sub_kind == TokenCppKind_ParenCl){
   i32 level = 0;
   for (i64 j = k - 1; j >= 0; j -= 1){
    if (t[j]->kind == TokenBaseKind_ParentheticalClose){ level += 1; }
    else if (t[j]->kind == TokenBaseKind_ParentheticalOpen){
     level -= 1;
     if (level == 0){
      at_start = (j > 0 && nasr_odin_tok_is(state, t[j - 1], "@"));
      break;
     }
    }
   }
  }
  if (!at_start){ continue; }

  Token *next = t[k + 1];
  if (next->sub_kind == TokenCppKind_ColonColon){
   // name :: [#directive ...] rhs
   i64 r = k + 2;
   b32 is_type_directive = false;
   for (; r < m && t[r]->kind == TokenBaseKind_Preprocessor; r += 1){
    if (nasr_odin_tok_is(state, t[r], "#type")){ is_type_directive = true; }
   }
   if (r >= m){ continue; }
   Token *rhs = t[r];
   Code_Index_Note_Kind kind = CodeIndexNote_Global;
   if (nasr_odin_tok_is(state, rhs, "proc")){
    kind = is_type_directive ? CodeIndexNote_Type : CodeIndexNote_Function;
   }
   else if (rhs->kind == byp_TokenKind_TypeKeyword || rhs->kind == byp_TokenKind_Primitive ||
            is_type_directive ||
            rhs->sub_kind == TokenCppKind_BrackOp || rhs->sub_kind == TokenCppKind_Xor){
    kind = CodeIndexNote_Type;
   }
   index_new_note(index, state, Ii64(token), kind, 0);
   if (rhs->sub_kind == TokenCppKind_Enum){
    nasr_odin_index_enum_members(index, state, t, m, r);
   }
   k += 1;
  }
  else if (next->sub_kind == TokenCppKind_Colon && depth == 0){
   // name : T : v (constant), name : T = v, name := v, name : T (file-scope variables)
   index_new_note(index, state, Ii64(token), CodeIndexNote_Global, 0);
   k += 1;
  }
 }

 index->note_array = code_index_note_ptr_array_from_list(state->arena, &index->note_list);
}

////////////////////////////////
// Dispatch

function b32
nasr_buffer_is_odin(Application_Links *app, Buffer_ID buffer){
 return(nasr_buffer_language(app, buffer) == NasrLang_Odin);
}
