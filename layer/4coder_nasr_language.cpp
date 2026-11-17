// NOTE(nasr): Per-buffer language. Decided once from the file extension (lazily, on first
// query) and cached in the buffer's managed scope; used by the file bar's "(Lang)" segment.

struct Nasr_Language_Info{
 Nasr_Language lang;
 char *name;
 char *extensions; // '.'-separated, like treat_as_code
};

global Nasr_Language_Info nasr_language_table[] = {
 {NasrLang_C,           "C",           ".c"},
 {NasrLang_Cpp,         "C++",         ".cpp.cc.cxx.hpp.hh.hxx.inl"},
 {NasrLang_CHeader,     "C/C++",       ".h"},
 {NasrLang_Rust,        "Rust",        ".rs"},
 {NasrLang_Jai,         "Jai",         ".jai"},
 {NasrLang_CSharp,      "C#",          ".cs"},
 {NasrLang_Java,        "Java",        ".java"},
 {NasrLang_Glsl,        "GLSL",        ".glsl.vert.frag.geom.comp"},
 {NasrLang_Hlsl,        "HLSL",        ".hlsl"},
 {NasrLang_Markdown,    "Markdown",    ".md"},
 {NasrLang_4coder,      "4coder",      ".4coder"},
 {NasrLang_Shell,       "Shell",       ".sh.bash.zsh.fish"},
 {NasrLang_Batch,       "Batch",       ".bat.cmd"},
 {NasrLang_Python,      "Python",      ".py"},
 {NasrLang_JavaScript,  "JavaScript",  ".js"},
 {NasrLang_TypeScript,  "TypeScript",  ".ts"},
 {NasrLang_Json,        "JSON",        ".json"},
 {NasrLang_Text,        "Text",        ".txt"},
};

function Nasr_Language
nasr_language_from_file_name(String_Const_u8 file_name){
 Nasr_Language result = NasrLang_Fundamental;
 if (file_name.size == 0){
  return(NasrLang_Special);
 }
 String_Const_u8 ext = string_file_extension(file_name);
 if (ext.size == 0){
  return(result);
 }
 for (i32 i = 0; i < ArrayCount(nasr_language_table); i += 1){
  String_Const_u8 list = SCu8(nasr_language_table[i].extensions);
  // walk ".a.b.c"
  for (u64 at = 0; at < list.size;){
   u64 start = at + 1;
   u64 end = start;
   for (; end < list.size && list.str[end] != '.'; end += 1);
   if (string_match(ext, SCu8(list.str + start, end - start))){
    return(nasr_language_table[i].lang);
   }
   at = end;
  }
 }
 return(result);
}

function String_Const_u8
nasr_language_name(Nasr_Language lang){
 String_Const_u8 result = string_u8_litexpr("Fundamental");
 if (lang == NasrLang_Special){
  result = string_u8_litexpr("Special");
 }
 for (i32 i = 0; i < ArrayCount(nasr_language_table); i += 1){
  if (nasr_language_table[i].lang == lang){
   result = SCu8(nasr_language_table[i].name);
   break;
  }
 }
 return(result);
}

function Nasr_Language
nasr_buffer_language(Application_Links *app, Buffer_ID buffer){
 Nasr_Language result = NasrLang_Unknown;
 Managed_Scope scope = buffer_get_managed_scope(app, buffer);
 Nasr_Language *ptr = scope_attachment(app, scope, buffer_nasr_language, Nasr_Language);
 if (ptr != 0){
  if (*ptr == NasrLang_Unknown){
   Scratch_Block scratch(app);
   *ptr = nasr_language_from_file_name(push_buffer_file_name(app, scratch, buffer));
  }
  result = *ptr;
 }
 return(result);
}
