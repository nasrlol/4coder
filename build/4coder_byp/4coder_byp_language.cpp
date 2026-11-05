// NOTE(nasr): Per-buffer language. Decided once from the file extension (lazily, on first
// query) and cached in the buffer's managed scope; used by the file bar's "(Lang)" segment.

struct Byp_Language_Info{
 Byp_Language lang;
 char *name;
 char *extensions; // '.'-separated, like treat_as_code
};

global Byp_Language_Info byp_language_table[] = {
 {BypLang_C,           "C",           ".c"},
 {BypLang_Cpp,         "C++",         ".cpp.cc.cxx.hpp.hh.hxx.inl"},
 {BypLang_CHeader,     "C/C++",       ".h"},
 {BypLang_Rust,        "Rust",        ".rs"},
 {BypLang_Jai,         "Jai",         ".jai"},
 {BypLang_CSharp,      "C#",          ".cs"},
 {BypLang_Java,        "Java",        ".java"},
 {BypLang_Glsl,        "GLSL",        ".glsl.vert.frag.geom.comp"},
 {BypLang_Hlsl,        "HLSL",        ".hlsl"},
 {BypLang_Markdown,    "Markdown",    ".md"},
 {BypLang_4coder,      "4coder",      ".4coder"},
 {BypLang_Shell,       "Shell",       ".sh.bash.zsh.fish"},
 {BypLang_Batch,       "Batch",       ".bat.cmd"},
 {BypLang_Python,      "Python",      ".py"},
 {BypLang_JavaScript,  "JavaScript",  ".js"},
 {BypLang_TypeScript,  "TypeScript",  ".ts"},
 {BypLang_Json,        "JSON",        ".json"},
 {BypLang_Text,        "Text",        ".txt"},
};

function Byp_Language
byp_language_from_file_name(String_Const_u8 file_name){
 Byp_Language result = BypLang_Fundamental;
 if (file_name.size == 0){
  return(BypLang_Special);
 }
 String_Const_u8 ext = string_file_extension(file_name);
 if (ext.size == 0){
  return(result);
 }
 for (i32 i = 0; i < ArrayCount(byp_language_table); i += 1){
  String_Const_u8 list = SCu8(byp_language_table[i].extensions);
  // walk ".a.b.c"
  for (u64 at = 0; at < list.size;){
   u64 start = at + 1;
   u64 end = start;
   for (; end < list.size && list.str[end] != '.'; end += 1);
   if (string_match(ext, SCu8(list.str + start, end - start))){
    return(byp_language_table[i].lang);
   }
   at = end;
  }
 }
 return(result);
}

function String_Const_u8
byp_language_name(Byp_Language lang){
 String_Const_u8 result = string_u8_litexpr("Fundamental");
 if (lang == BypLang_Special){
  result = string_u8_litexpr("Special");
 }
 for (i32 i = 0; i < ArrayCount(byp_language_table); i += 1){
  if (byp_language_table[i].lang == lang){
   result = SCu8(byp_language_table[i].name);
   break;
  }
 }
 return(result);
}

function Byp_Language
byp_buffer_language(Application_Links *app, Buffer_ID buffer){
 Byp_Language result = BypLang_Unknown;
 Managed_Scope scope = buffer_get_managed_scope(app, buffer);
 Byp_Language *ptr = scope_attachment(app, scope, buffer_byp_language, Byp_Language);
 if (ptr != 0){
  if (*ptr == BypLang_Unknown){
   Scratch_Block scratch(app);
   *ptr = byp_language_from_file_name(push_buffer_file_name(app, scratch, buffer));
  }
  result = *ptr;
 }
 return(result);
}
