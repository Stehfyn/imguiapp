#pragma once

// [SECTION] Compile-time type-identity layer (ImGuiStatic<>, ImGuiType<>).
//
// The ImGuiStatic layer: constexpr type name/id derivation from the compiler's function signature, plus
// GenerateLabel. A LEAF header (depends only on imgui + std); imguiapp.h includes it near the top. A
// consumer that needs only compile-time type identity (codegen, a reflect test) can include this alone
// without the whole runtime. Behavior-neutral extraction from imguiapp.h.

#include "imgui.h"                        // ImGuiID, ImFormatString
#include <mutex>                          // std::call_once
#include <string_view>
#include <type_traits>                    // std::remove_cvref_t (ImGuiType alias)

#ifndef ImFuncSig
#ifdef _MSC_VER
#define ImFuncSig __FUNCSIG__
#else
#define ImFuncSig __PRETTY_FUNCTION__
#endif
#endif

#ifndef IM_LABEL_SIZE
#define IM_LABEL_SIZE 256
#endif

#ifndef ImParseTypeStart
#ifdef _MSC_VER
#define ImParseTypeStart "::"
#define ImParseTypeStart2 " "
#else
#define ImParseTypeStart '='
#endif
#endif

#ifndef ImParseTypeStart2
#define ImParseTypeStart2 ImParseTypeStart
#endif

#ifndef ImParseTypeEnd
#ifdef _MSC_VER
#define ImParseTypeEnd ">"
#else
#define ImParseTypeEnd ']'
#endif
#endif

template <typename T>
struct ImGuiStatic
{
  inline static constexpr const char*      _FunctionSignature()                { return ImFuncSig; }
  inline static constexpr bool             _StartsWith(std::string_view sv, std::string_view prefix) { return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix; }
  inline static constexpr std::string_view _StripTypeKeyword(std::string_view sv)
  {
    return _StartsWith(sv, "struct ") ? sv.substr(7) :
           _StartsWith(sv, "class ")  ? sv.substr(6) :
           _StartsWith(sv, "enum ")   ? sv.substr(5) :
           _StartsWith(sv, "union ")  ? sv.substr(6) : sv;
  }
  inline static constexpr std::string_view _StripDisplayScope(std::string_view sv)
  {
    sv = _StripTypeKeyword(sv);
    size_t scope = sv.rfind("::");
    return _StripTypeKeyword(scope == std::string_view::npos ? sv : sv.substr(scope + 2));
  }
  inline static constexpr std::string_view _ParseType(std::string_view sv)
  {
    constexpr std::string_view clang_marker = "T = ";
    size_t start = sv.find(clang_marker);
    if (start != std::string_view::npos)
    {
      start += clang_marker.size();
      size_t end = sv.find(';', start);
      size_t bracket_end = sv.find(']', start);
      if (end == std::string_view::npos || (bracket_end != std::string_view::npos && bracket_end < end))
        end = bracket_end;
      if (end == std::string_view::npos)
        end = sv.size();
      return _StripDisplayScope(sv.substr(start, end - start));
    }

    constexpr std::string_view msvc_marker = "ImGuiStatic<";
    start = sv.find(msvc_marker);
    if (start != std::string_view::npos)
    {
      start += msvc_marker.size();
      size_t depth = 1;
      for (size_t i = start; i < sv.size(); ++i)
      {
        if (sv[i] == '<')
          ++depth;
        else if (sv[i] == '>' && --depth == 0)
          return _StripDisplayScope(sv.substr(start, i - start));
      }
    }

    size_t end = sv.rfind(ImParseTypeEnd);
    auto sv2 = sv.substr(0, end);
    start = (sv2.rfind(ImParseTypeStart) > sv2.rfind(ImParseTypeStart2)) ? sv2.rfind(ImParseTypeStart) : sv2.rfind(ImParseTypeStart2);
    start = start >= end ? 0 : start;
    return _StripDisplayScope((sv.size() > end) && (end >= (start + 2)) ? sv.substr(start + 2, end - (start + 1)) : sv);
  }
  inline static constexpr ImGuiID          _ConstantHash(std::string_view sv)  { return *sv.data() ? static_cast<ImGuiID>(*sv.data()) + 33 * _ConstantHash(sv.data() + 1) : 5381; }
  inline static           ImGuiID          GetRelativeID()                     { std::call_once(_Initialized, []() { Count = 1; }); return Count++; }
  static constexpr        std::string_view Name                                { _ParseType(_FunctionSignature()) };
  static constexpr        ImGuiID          ID                                  { _ConstantHash(Name) };
  inline static           int              Count;
  inline static           std::once_flag   _Initialized;
};

template <typename T>
using ImGuiType = ImGuiStatic<std::remove_cvref_t<std::remove_pointer_t<T>>>;

template <typename T>
inline static void GenerateLabel(char* label, size_t size) { std::string_view sv = ImGuiType<T>::Name; ImFormatString(label, size, "%.*s", (int)sv.size(), sv.data()); }
