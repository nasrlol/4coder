#if !defined(FCODER_BYP_TOKEN_H)
#define FCODER_BYP_TOKEN_H

enum{
 byp_TokenKind_Primitive = 16,
 byp_TokenKind_ControlFlow = 17,
 byp_TokenKind_Struct = 18,
 // NOTE(nasr): Odin struct/union/enum/proc/...: struct color, but no "};" auto-insert
 byp_TokenKind_TypeKeyword = 19,
};

// NOTE(nasr): buffer language, see 4coder_byp_language.cpp
typedef i32 Nasr_Language;
enum{
 NasrLang_Unknown = 0, // not decided yet
 NasrLang_Fundamental, // a file with an extension we don't know
 NasrLang_Special,     // no file (*scratch*, *compilation*, ...)
 NasrLang_Text,
 NasrLang_C,
 NasrLang_Cpp,
 NasrLang_CHeader,
 NasrLang_Rust,
 NasrLang_Jai,
 NasrLang_CSharp,
 NasrLang_Java,
 NasrLang_Glsl,
 NasrLang_Hlsl,
 NasrLang_Markdown,
 NasrLang_4coder,
 NasrLang_Shell,
 NasrLang_Batch,
 NasrLang_Python,
 NasrLang_JavaScript,
 NasrLang_TypeScript,
 NasrLang_Json,
 NasrLang_Odin,
};

// NOTE(nasr): forward declared so the default hooks can use it
function Nasr_Language nasr_buffer_language(Application_Links *app, Buffer_ID buffer);

// NOTE(nasr): Odin, see 4coder_nasr_odin.cpp (used by the default lex/index hooks)
function b32 nasr_buffer_is_odin(Application_Links *app, Buffer_ID buffer);
function Token_List nasr_lex_odin(Arena *arena, String_Const_u8 text);
function void nasr_odin_index(Code_Index_File *index, Generic_Parse_State *state);

#endif
