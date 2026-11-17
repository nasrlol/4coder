#if !defined(FCODER_BYP_TOKEN_H)
#define FCODER_BYP_TOKEN_H

enum{
 byp_TokenKind_Primitive = 16,
 byp_TokenKind_ControlFlow = 17,
 byp_TokenKind_Struct = 18,
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
};

// NOTE(nasr): forward declared so the default hooks can use it
function Nasr_Language nasr_buffer_language(Application_Links *app, Buffer_ID buffer);

#endif
