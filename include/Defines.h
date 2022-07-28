//
// Created by vigi99 on 27/09/20.
//

#pragma once

#ifdef _WIN32
#    ifdef LIBRARY_EXPORTS
#        define LIBRARY_API __declspec(dllexport)
#    else
#        define LIBRARY_API __declspec(dllimport)
#    endif
#endif

#ifdef UNICODE_SUPPORT
using xchar = wchar_t;
#	define isxspace std::iswspace
#	define to_xstring std::to_wstring
#	define XL(x) L##x
#	define xcout std::wcout
#	define xcerr std::wcerr
#   define to_xlower ::towlower
#   define to_xupper ::towupper
#   define is_xupper std::iswupper
#   define is_xpunct std::iswpunct
#else
using xchar = char;
#	define isxspace std::isspace
#	define to_xstring std::to_string
#	define XL(x) x
#	define xcout std::cout
#	define xcerr std::cout
#   define to_xlower ::tolower
#   define to_xupper ::toupper
#   define is_xupper std::isupper
#   define is_xpunct std::ispunct
#endif

using xstring = std::basic_string<xchar>;
using xstring_view = std::basic_string_view<xchar>;
using xifstream = std::basic_ifstream<xchar>;
using xstringstream = std::basic_stringstream<xchar>;
using xregex = std::basic_regex<xchar>;
using xsmatch = std::match_results<xchar const*>;
