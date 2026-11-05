#if !defined(FCODER_BYP_TOKEN_H)
#define FCODER_BYP_TOKEN_H

enum{
 byp_TokenKind_Primitive = 16,
 byp_TokenKind_ControlFlow = 17,
 byp_TokenKind_Struct = 18,
};

// NOTE(nasr): buffer language, see 4coder_byp_language.cpp
typedef i32 Byp_Language;
enum{
 BypLang_Unknown = 0, // not decided yet
 BypLang_Fundamental, // a file with an extension we don't know
 BypLang_Special,     // no file (*scratch*, *compilation*, ...)
 BypLang_Text,
 BypLang_C,
 BypLang_Cpp,
 BypLang_CHeader,
 BypLang_Rust,
 BypLang_Jai,
 BypLang_CSharp,
 BypLang_Java,
 BypLang_Glsl,
 BypLang_Hlsl,
 BypLang_Markdown,
 BypLang_4coder,
 BypLang_Shell,
 BypLang_Batch,
 BypLang_Python,
 BypLang_JavaScript,
 BypLang_TypeScript,
 BypLang_Json,
};

// NOTE(nasr): forward declared so the default hooks can use it
function Byp_Language byp_buffer_language(Application_Links *app, Buffer_ID buffer);

#endif
