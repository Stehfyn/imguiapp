#pragma once

/*

Index of this file:
// [SECTION] Header mess
// [SECTION] Forward declarations and basic types
// [SECTION] Compile-time helpers (ImGuiStatic<>, ImGuiType<>)
// [SECTION] Dear ImGui end-user API functions

*/

//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------

#include "imgui.h" 										    // IMGUI_API, ImGuiID, ImGuiStorage, ImBitArray, ImGuiTextIndex, ImChunkStream
#include "imgui_internal.h"               // ImStrncpy
#include "imapp_config.h"

// Keep VERSION and VERSION_NUM in sync.
#define IMGUI_APPLAYER_VERSION      "0.4.1"
#define IMGUI_APPLAYER_VERSION_NUM  401

#include <mutex>                          // std::call_once
#include <tuple>
#include <type_traits>
#include <string_view>
#include <array>                          // compile-time declaration spellings (ImGuiAppVecSpelling)

// Compile-time reflection (imguiapp_reflect.h, the applayer's port of qlibs/reflect):
// powers the live mirror's field introspection.
// windows.h's min/max macros (leaked by platform-backend TUs) break its std::min.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
//=============== compile-time reflection (folded from imguiapp_reflect.h) ===============
// ImGuiAppLayer static reflection. Port of qlibs/reflect v1.2.5 (https://github.com/qlibs/reflect,
// MIT -- license below) into the applayer, carrying the imguix patches to the member-count probe:
// two-strategy counting (positional probe capped at 64, braced past it so C-array members count as
// ONE member instead of brace-eliding element-wise) and a flat overflow pre-probe (one requires-
// expression instead of a 65-frame constexpr climb, which overflowed MSVC's dependency-context
// limit (C1202) when walking a transitive closure of large aggregates). Renames: namespace
// reflect -> ImAppReflect, REFLECT_* -> IMAPP_REFLECT_* (markers parsed from function signatures
// stay self-consistent under the uniform rename). The upstream self-test block is not carried;
// tests/imguiapp_headless_verify.cpp pins the bridge behavior.
// <!--
// The MIT License (MIT)
//
// Copyright (c) 2024 Kris Jusiak <kris@jusiak.net>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//


#include <array>
#include <string_view>
#include <source_location>
#include <type_traits>
#include <tuple>
#include <utility>
#include <limits>
#include <cstdint>

#ifndef IMAPP_REFLECT_ENUM_MIN
#define IMAPP_REFLECT_ENUM_MIN 0
#endif

#ifndef IMAPP_REFLECT_ENUM_MAX
#define IMAPP_REFLECT_ENUM_MAX 128
#endif

namespace {
template<bool Cond> struct IMAPP_REFLECT_FWD_LIKE { template<class T> using type = std::remove_reference_t<T>&&; };
template<> struct IMAPP_REFLECT_FWD_LIKE<true> { template<class T> using type = std::remove_reference_t<T>&; };
} // to speed up compilation times

#define IMAPP_REFLECT_FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define IMAPP_REFLECT_FWD_LIKE(T, ...) static_cast<typename ::IMAPP_REFLECT_FWD_LIKE<::std::is_lvalue_reference_v<T>>::template type<decltype(__VA_ARGS__)>>(__VA_ARGS__)
struct  IMAPP_REFLECT_STRUCT { void* MEMBER; enum class ENUM { VALUE }; }; // has to be in the global namespace

namespace ImAppReflect {
namespace detail {
template<class T> extern const T ext{};
struct any { template<class T> operator T() const noexcept; };
template<class T> struct any_except_base_of { template<class U> requires (not std::is_base_of_v<U, T>) operator U() const noexcept; };
template<auto...> struct auto_ { constexpr explicit(false) auto_(auto&&...) noexcept { } };
template<class T> struct ref { T& ref_; };
template<class T> ref(T&) -> ref<T>;

template<std::size_t N>
constexpr auto nth_pack_element_impl = []<auto... Ns>(std::index_sequence<Ns...>) -> decltype(auto) {
  return [](auto_<Ns>&&..., auto&& nth, auto&&...) -> decltype(auto) { return IMAPP_REFLECT_FWD(nth); };
}(std::make_index_sequence<N>{});

template<std::size_t N, class...Ts> requires (N < sizeof...(Ts))
[[nodiscard]] constexpr decltype(auto) nth_pack_element(Ts&&...args) noexcept {
  return nth_pack_element_impl<N>(IMAPP_REFLECT_FWD(args)...);
}

template<auto...  Vs> [[nodiscard]] constexpr auto function_name() noexcept -> std::string_view { return std::source_location::current().function_name(); }
template<class... Ts> [[nodiscard]] constexpr auto function_name() noexcept -> std::string_view { return std::source_location::current().function_name(); }

template<class T>
struct type_name_info {
  static constexpr auto name = function_name<int>();
  static constexpr auto begin = name.find("int");
  static constexpr auto end = name.substr(begin+std::size(std::string_view{"int"}));
};

template<class T> requires std::is_class_v<T>
struct type_name_info<T> {
  static constexpr auto name = function_name<IMAPP_REFLECT_STRUCT>();
  static constexpr auto begin = name.find("IMAPP_REFLECT_STRUCT");
  static constexpr auto end = name.substr(begin+std::size(std::string_view{"IMAPP_REFLECT_STRUCT"}));
};

template<class T> requires std::is_enum_v<T>
struct type_name_info<T> {
  static constexpr auto name = function_name<IMAPP_REFLECT_STRUCT::ENUM>();
  static constexpr auto begin = name.find("IMAPP_REFLECT_STRUCT::ENUM");
  static constexpr auto end = name.substr(begin+std::size(std::string_view{"IMAPP_REFLECT_STRUCT::ENUM"}));
};

struct enum_name_info {
  static constexpr auto name = function_name<IMAPP_REFLECT_STRUCT::ENUM::VALUE>();
  static constexpr auto begin = name.find("IMAPP_REFLECT_STRUCT::ENUM::VALUE");
  static constexpr auto end = std::size(name)-(name.find("IMAPP_REFLECT_STRUCT::ENUM::VALUE")+std::size(std::string_view{"IMAPP_REFLECT_STRUCT::ENUM::VALUE"}));
};

struct member_name_info {
  static constexpr auto name = function_name<ref{ext<IMAPP_REFLECT_STRUCT>.MEMBER}>();
  static constexpr auto begin = name[name.find("MEMBER")-1];
  static constexpr auto end = name.substr(name.find("MEMBER")+std::size(std::string_view{"MEMBER"}));
};
}  // namespace detail

template<class T, std::size_t Size>
struct fixed_string {
  constexpr explicit(false) fixed_string(const T* str) {
    for (decltype(Size) i{}; i < Size; ++i) { data[i] = str[i]; }
    data[Size] = T();
  }
  [[nodiscard]] constexpr auto operator<=>(const fixed_string&) const = default;
  [[nodiscard]] constexpr explicit(false) operator std::string_view() const { return {std::data(data), Size}; }
  [[nodiscard]] constexpr auto size() const { return Size; }
  T data[Size + 1u];
};
template<class T, std::size_t Capacity, std::size_t Size = Capacity-1> fixed_string(const T (&str)[Capacity]) -> fixed_string<T, Size>;

namespace detail {
// PATCHED (imguix): two-strategy member count. The upstream positional probe
// brace-elides into C-array members (a char[96] member absorbs 96 probe args), which
// miscounts against structured bindings and recurses past compiler limits on large
// buffers. Strategy 1 is the UNCHANGED upstream probe, capped at 64 positions --
// every type upstream supports keeps its exact semantics (converting-ctor members
// like optional stay unambiguous under positional copy-init). Past the cap, strategy
// 2 restarts with each probe argument wrapped in braces: a braced initializer maps to
// exactly ONE member (no brace elision), so arrays count as one member, matching
// structured bindings. Braced probing can misresolve converting-ctor class members;
// such members alongside >64-leaf arrays are unsupported (were unsupported upstream).
template<class T, std::size_t Bases = 0u, class... Ts> requires std::is_aggregate_v<T>
[[nodiscard]] constexpr auto size_braced() -> std::size_t {
  if constexpr (requires { T{{Ts{}}...}; } and not requires { T{{Ts{}}..., {detail::any{}}}; }) {
    return sizeof...(Ts) - Bases;
  } else if constexpr (Bases == sizeof...(Ts) and requires { T{{Ts{}}...}; } and not requires { T{{Ts{}}..., {detail::any_except_base_of<T>{}}}; }) {
    return size_braced<T, Bases + 1u, Ts..., detail::any>();
  } else {
    return size_braced<T, Bases, Ts..., detail::any>();
  }
}
// PATCHED (imguix), continued: one flat probe decides the strategy up front. The linear
// positional climb to the 64 cap costs 65 nested constexpr frames PER TYPE, which overflows
// MSVC's dependency-context limit (C1202) when reflection walks a transitive closure of
// large aggregates (each member type's count evaluates inside the enclosing walk's context).
// "Positional arity exceeds 64" is exactly "T is constructible from 65 positional anys"
// (arity is monotone in the argument count), so test it in one requires-expression instead
// of climbing. Types the climb would have resolved at <= 64 keep the identical upstream path.
template<class T, std::size_t... Ns> requires std::is_aggregate_v<T>
[[nodiscard]] constexpr auto positional_overflows(std::index_sequence<Ns...>) -> bool {
  return requires { T{((void)Ns, detail::any{})...}; };
}
template<class T, std::size_t Bases = 0u, class... Ts> requires std::is_aggregate_v<T>
[[nodiscard]] constexpr auto size() -> std::size_t {
  if constexpr (sizeof...(Ts) == 0u and Bases == 0u and positional_overflows<T>(std::make_index_sequence<65u>{})) {
    return size_braced<T>();
  } else if constexpr (sizeof...(Ts) > 64u) {
    return size_braced<T>();
  } else if constexpr (requires { T{Ts{}...}; } and not requires { T{Ts{}..., detail::any{}}; }) {
    return sizeof...(Ts) - Bases;
  } else if constexpr (Bases == sizeof...(Ts) and requires { T{Ts{}...}; } and not requires { T{Ts{}..., detail::any_except_base_of<T>{}}; }) {
    return size<T, Bases + 1u, Ts..., detail::any>();
  } else {
    return size<T, Bases, Ts..., detail::any>();
  }
}
} // namespace detail

template<class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto size() -> std::size_t {
  return detail::size<std::remove_cvref_t<T>>();
}

template<class T> requires std::is_aggregate_v<T>
[[nodiscard]] constexpr auto size(const T&) -> std::size_t {
  return detail::size<T>();
}

namespace detail {
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&&,   std::integral_constant<std::size_t, 0>) noexcept { return IMAPP_REFLECT_FWD(fn)(); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 1>) noexcept { auto&& [_1] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 2>) noexcept { auto&& [_1, _2] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 3>) noexcept { auto&& [_1, _2, _3] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 4>) noexcept { auto&& [_1, _2, _3, _4] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 5>) noexcept { auto&& [_1, _2, _3, _4, _5] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 6>) noexcept { auto&& [_1, _2, _3, _4, _5, _6] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 7>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 8>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 9>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 10>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 11>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 12>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 13>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 14>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 15>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 16>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 17>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 18>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 19>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 20>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 21>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 22>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 23>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 24>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 25>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 26>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 27>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 28>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 29>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 30>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 31>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 32>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 33>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 34>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 35>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 36>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 37>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 38>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 39>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 40>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 41>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 42>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 43>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 44>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 45>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 46>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 47>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 48>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 49>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 50>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 51>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 52>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 53>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 54>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 55>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 56>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 57>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56), IMAPP_REFLECT_FWD_LIKE(T, _57)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 58>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56), IMAPP_REFLECT_FWD_LIKE(T, _57), IMAPP_REFLECT_FWD_LIKE(T, _58)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 59>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56), IMAPP_REFLECT_FWD_LIKE(T, _57), IMAPP_REFLECT_FWD_LIKE(T, _58), IMAPP_REFLECT_FWD_LIKE(T, _59)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 60>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56), IMAPP_REFLECT_FWD_LIKE(T, _57), IMAPP_REFLECT_FWD_LIKE(T, _58), IMAPP_REFLECT_FWD_LIKE(T, _59), IMAPP_REFLECT_FWD_LIKE(T, _60)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 61>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56), IMAPP_REFLECT_FWD_LIKE(T, _57), IMAPP_REFLECT_FWD_LIKE(T, _58), IMAPP_REFLECT_FWD_LIKE(T, _59), IMAPP_REFLECT_FWD_LIKE(T, _60), IMAPP_REFLECT_FWD_LIKE(T, _61)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 62>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56), IMAPP_REFLECT_FWD_LIKE(T, _57), IMAPP_REFLECT_FWD_LIKE(T, _58), IMAPP_REFLECT_FWD_LIKE(T, _59), IMAPP_REFLECT_FWD_LIKE(T, _60), IMAPP_REFLECT_FWD_LIKE(T, _61), IMAPP_REFLECT_FWD_LIKE(T, _62)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 63>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56), IMAPP_REFLECT_FWD_LIKE(T, _57), IMAPP_REFLECT_FWD_LIKE(T, _58), IMAPP_REFLECT_FWD_LIKE(T, _59), IMAPP_REFLECT_FWD_LIKE(T, _60), IMAPP_REFLECT_FWD_LIKE(T, _61), IMAPP_REFLECT_FWD_LIKE(T, _62), IMAPP_REFLECT_FWD_LIKE(T, _63)); }
template<class Fn, class T> [[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, std::integral_constant<std::size_t, 64>) noexcept { auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64] = IMAPP_REFLECT_FWD(t); return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, _1), IMAPP_REFLECT_FWD_LIKE(T, _2), IMAPP_REFLECT_FWD_LIKE(T, _3), IMAPP_REFLECT_FWD_LIKE(T, _4), IMAPP_REFLECT_FWD_LIKE(T, _5), IMAPP_REFLECT_FWD_LIKE(T, _6), IMAPP_REFLECT_FWD_LIKE(T, _7), IMAPP_REFLECT_FWD_LIKE(T, _8), IMAPP_REFLECT_FWD_LIKE(T, _9), IMAPP_REFLECT_FWD_LIKE(T, _10), IMAPP_REFLECT_FWD_LIKE(T, _11), IMAPP_REFLECT_FWD_LIKE(T, _12), IMAPP_REFLECT_FWD_LIKE(T, _13), IMAPP_REFLECT_FWD_LIKE(T, _14), IMAPP_REFLECT_FWD_LIKE(T, _15), IMAPP_REFLECT_FWD_LIKE(T, _16), IMAPP_REFLECT_FWD_LIKE(T, _17), IMAPP_REFLECT_FWD_LIKE(T, _18), IMAPP_REFLECT_FWD_LIKE(T, _19), IMAPP_REFLECT_FWD_LIKE(T, _20), IMAPP_REFLECT_FWD_LIKE(T, _21), IMAPP_REFLECT_FWD_LIKE(T, _22), IMAPP_REFLECT_FWD_LIKE(T, _23), IMAPP_REFLECT_FWD_LIKE(T, _24), IMAPP_REFLECT_FWD_LIKE(T, _25), IMAPP_REFLECT_FWD_LIKE(T, _26), IMAPP_REFLECT_FWD_LIKE(T, _27), IMAPP_REFLECT_FWD_LIKE(T, _28), IMAPP_REFLECT_FWD_LIKE(T, _29), IMAPP_REFLECT_FWD_LIKE(T, _30), IMAPP_REFLECT_FWD_LIKE(T, _31), IMAPP_REFLECT_FWD_LIKE(T, _32), IMAPP_REFLECT_FWD_LIKE(T, _33), IMAPP_REFLECT_FWD_LIKE(T, _34), IMAPP_REFLECT_FWD_LIKE(T, _35), IMAPP_REFLECT_FWD_LIKE(T, _36), IMAPP_REFLECT_FWD_LIKE(T, _37), IMAPP_REFLECT_FWD_LIKE(T, _38), IMAPP_REFLECT_FWD_LIKE(T, _39), IMAPP_REFLECT_FWD_LIKE(T, _40), IMAPP_REFLECT_FWD_LIKE(T, _41), IMAPP_REFLECT_FWD_LIKE(T, _42), IMAPP_REFLECT_FWD_LIKE(T, _43), IMAPP_REFLECT_FWD_LIKE(T, _44), IMAPP_REFLECT_FWD_LIKE(T, _45), IMAPP_REFLECT_FWD_LIKE(T, _46), IMAPP_REFLECT_FWD_LIKE(T, _47), IMAPP_REFLECT_FWD_LIKE(T, _48), IMAPP_REFLECT_FWD_LIKE(T, _49), IMAPP_REFLECT_FWD_LIKE(T, _50), IMAPP_REFLECT_FWD_LIKE(T, _51), IMAPP_REFLECT_FWD_LIKE(T, _52), IMAPP_REFLECT_FWD_LIKE(T, _53), IMAPP_REFLECT_FWD_LIKE(T, _54), IMAPP_REFLECT_FWD_LIKE(T, _55), IMAPP_REFLECT_FWD_LIKE(T, _56), IMAPP_REFLECT_FWD_LIKE(T, _57), IMAPP_REFLECT_FWD_LIKE(T, _58), IMAPP_REFLECT_FWD_LIKE(T, _59), IMAPP_REFLECT_FWD_LIKE(T, _60), IMAPP_REFLECT_FWD_LIKE(T, _61), IMAPP_REFLECT_FWD_LIKE(T, _62), IMAPP_REFLECT_FWD_LIKE(T, _63), IMAPP_REFLECT_FWD_LIKE(T, _64)); }
} // namespace detail

template<class Fn, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr decltype(auto) visit(Fn&& fn, T&& t, auto...) noexcept {
  #if (__cpp_structured_bindings >= 202601L)
    auto&& [... ts] = IMAPP_REFLECT_FWD(t);
    return IMAPP_REFLECT_FWD(fn)(IMAPP_REFLECT_FWD_LIKE(T, ts)...);
  #else
    return detail::visit(IMAPP_REFLECT_FWD(fn), IMAPP_REFLECT_FWD(t), std::integral_constant<std::size_t, size<std::remove_cvref_t<T>>()>{});
  #endif
}

template<class T>
[[nodiscard]] constexpr auto type_name() noexcept -> std::string_view {
  using type_name_info = detail::type_name_info<std::remove_pointer_t<std::remove_cvref_t<T>>>;
  constexpr std::string_view function_name = detail::function_name<std::remove_pointer_t<std::remove_cvref_t<T>>>();
  constexpr std::string_view qualified_type_name = function_name.substr(type_name_info::begin, function_name.find(type_name_info::end)-type_name_info::begin);
  constexpr std::string_view tmp_type_name = qualified_type_name.substr(0, qualified_type_name.find_first_of("<", 1));
  constexpr std::string_view type_name = tmp_type_name.substr(tmp_type_name.find_last_of("::")+1);
  static_assert(std::size(type_name) > 0u);
  if (std::is_constant_evaluated()) {
    return type_name;
  } else {
    return [&] {
      static constexpr const auto name = fixed_string<std::remove_cvref_t<decltype(type_name[0])>, std::size(type_name)>{std::data(type_name)};
      return std::string_view{name};
    }();
  }
}

template<class T>
[[nodiscard]] constexpr auto type_name(T&&) noexcept -> std::string_view { return type_name<std::remove_cvref_t<T>>(); }

template<class E>
[[nodiscard]] constexpr auto to_underlying(const E e) noexcept {
  return static_cast<std::underlying_type_t<E>>(e);
}

namespace detail {
template<auto V> consteval const auto& data() { return V.data; }
template<class T, std::size_t Size>
struct static_vector {
  constexpr static_vector() = default;
  constexpr auto push_back(const T& value) { values_[size_++] = value; }
  [[nodiscard]] constexpr const auto& operator[](auto i) const { return values_[i]; }
  [[nodiscard]] constexpr auto begin() const { return &values_[0]; }
  [[nodiscard]] constexpr auto end() const { return &values_[0] + size_; }
  [[nodiscard]] constexpr auto size() const { return size_; }
  [[nodiscard]] constexpr auto empty() const { return not size_; }
  [[nodiscard]] constexpr auto capacity() const { return Size; }
  std::array<T, Size> values_{};
  std::size_t size_{};
};
template<class E, auto N> requires std::is_enum_v<E>
[[nodiscard]] constexpr auto enum_name() {
  constexpr auto fn_name = function_name<static_cast<E>(N)>();
  constexpr auto name = fn_name.substr(enum_name_info::begin, std::size(fn_name)-enum_name_info::end-enum_name_info::begin);
  constexpr auto enum_name = name.substr(name.find_last_of("::")+1);
  static_assert(std::size(enum_name) > 0u);
  return data<fixed_string<std::remove_cvref_t<decltype(enum_name[0])>, std::size(enum_name)>{std::data(enum_name)}>();
}
#if defined(__clang__)
#if (__clang_major__ > 15) and (__clang_major__ < 19)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wenum-constexpr-conversion"
#endif
template<class E, auto Min, auto Max> requires (std::is_enum_v<E> and Max > Min)
inline constexpr auto enum_cases = []<auto... Ns>(std::index_sequence<Ns...>) {
  static_vector<std::underlying_type_t<E>, sizeof...(Ns)> cases{};
  ([&] {
    if constexpr (requires { function_name<static_cast<E>(Ns+Min)>(); }) {
      const auto fn_name = function_name<static_cast<E>(Ns+Min)>();
      const auto name = fn_name.substr(enum_name_info::begin, std::size(fn_name)-enum_name_info::end-enum_name_info::begin);
      const auto enum_name = name.substr(name.find_last_of("::")+1);
      if (enum_name.find(")") == std::string_view::npos) {
        cases.push_back(std::underlying_type_t<E>(Ns+Min));
      }
    }
  }(), ...);
  return cases;
}(std::make_index_sequence<Max-Min+1/*inclusive*/>{});
#if (__clang_major__ > 15) and (__clang_major__ < 19)
  #pragma clang diagnostic pop
#endif
#else
template<class E, auto Min, auto Max> requires (std::is_enum_v<E> and Max > Min)
inline constexpr auto enum_cases = []<auto... Ns>(std::index_sequence<Ns...>) {
  const auto names = function_name<static_cast<E>(Ns+Min)...>();
  const auto begin = enum_name_info::begin;
  const auto end = std::size(names) - enum_name_info::end;
  static_vector<std::underlying_type_t<E>, sizeof...(Ns)> cases{};
  std::underlying_type_t<E> index{};
  auto valid = true;
  for (auto i = begin; i < end; ++i) {
    if (names[i] == '(' and names[i+1] != ')') {
      valid = false;
    } else if (names[i] == ' ') {
      valid = true;
    } else if (names[i] == ',' or i == end-1) {
      if (valid) { cases.push_back(index+Min); }
      ++index;
      valid = true;
    }
  }
  return cases;
}(std::make_index_sequence<Max-Min+1/*inclusive*/>{});
#endif
} // namespace detail

template<class E> requires std::is_enum_v<E> consteval auto enum_min(const E) -> int { return IMAPP_REFLECT_ENUM_MIN; }
template<class E, class T = std::underlying_type_t<E>> requires std::is_enum_v<E> consteval auto enum_max(const E) -> int {
  return std::numeric_limits<T>::max() < IMAPP_REFLECT_ENUM_MAX ? std::numeric_limits<T>::max() : IMAPP_REFLECT_ENUM_MAX;
}

template<class E, int Min = enum_min(E{}), int Max = enum_max(E{}), auto enum_cases = detail::enum_cases<E, Min, Max>>
inline constexpr auto enumerators = []<auto... Ns>(std::index_sequence<Ns...>) {
  return std::array{std::pair{enum_cases[Ns], detail::enum_name<E, enum_cases[Ns]>()}...};
}(std::make_index_sequence<enum_cases.size()>{});

template<class E, fixed_string unknown = "", auto Min = enum_min(E{}), auto Max = enum_max(E{})>
  requires (std::is_enum_v<E> and Max > Min)
[[nodiscard]] constexpr auto enum_name(const E e) noexcept -> std::string_view {
  if constexpr (constexpr auto cases = enumerators<E, Min, Max>; std::empty(cases)) {
    return unknown;
  } else {
    const auto switch_case = [&]<std::size_t I = 0u>(auto switch_case, const auto value) -> std::string_view {
      if constexpr (I == std::size(cases)) {
        return unknown;
      } else {
        switch (value) {
          default:             return switch_case.template operator()<I + 1u>(switch_case, value);
          case cases[I].first: return cases[I].second;
        }
      }
    };
    return switch_case(switch_case, to_underlying(e));
  }
}

/**
 * @code
 * static_assert(type_id_v<void> == type_id_v<void>);
 * static_assert(type_id_v<int>  != type_id_v<void>);
 * static_assert(type_id_v<int>  == type_id_v<int>);
 * static_assert(type_id_v<int>  != type_id_v<const int&>);
 * @endcode
 */
template<class...>
inline constexpr auto type_id_v = []{};

template<class T>
[[nodiscard]] constexpr auto type_id() -> std::size_t {
  std::size_t result{};
  for (const auto c : type_name<T>()) { (result ^= c) <<= 1; }
  return result;
}

template<class T>
[[nodiscard]] constexpr auto type_id(const T&) noexcept -> std::size_t { return type_id<T>(); }

template<std::size_t N, class T> requires (std::is_aggregate_v<std::remove_cvref_t<T>> and N < size<T>())
[[nodiscard]] constexpr auto member_name() noexcept -> std::string_view {
  constexpr std::string_view function_name = detail::function_name<visit([](auto&&... args) { return detail::ref{detail::nth_pack_element<N>(IMAPP_REFLECT_FWD(args)...)}; }, detail::ext<std::remove_cvref_t<T>>)>();
  constexpr std::string_view tmp_member_name = function_name.substr(0, function_name.find(detail::member_name_info::end));
  constexpr std::string_view member_name = tmp_member_name.substr(tmp_member_name.find_last_of(detail::member_name_info::begin)+1);
  static_assert(std::size(member_name) > 0u);
  if (std::is_constant_evaluated()) {
    return member_name;
  } else {
    return [&] {
      static constexpr const auto name = fixed_string<std::remove_cvref_t<decltype(member_name[0])>, std::size(member_name)>{std::data(member_name)};
      return std::string_view{name};
    }();
  }
}

template<std::size_t N, class T> requires (std::is_aggregate_v<T> and N < size<T>())
[[nodiscard]] constexpr auto member_name(const T&) noexcept -> std::string_view {
  return member_name<N, T>();
}

template<std::size_t N, class T> requires (std::is_aggregate_v<std::remove_cvref_t<T>> and N < size<std::remove_cvref_t<T>>())
[[nodiscard]] constexpr decltype(auto) get(T&& t) noexcept {
  return visit([](auto&&... args) -> decltype(auto) { return detail::nth_pack_element<N>(IMAPP_REFLECT_FWD(args)...); }, IMAPP_REFLECT_FWD(t));
}

template<std::size_t N, class T> requires (std::is_aggregate_v<T> and N < size<T>())
using member_type = std::remove_cvref_t<decltype(ImAppReflect::get<N>(std::declval<T&&>()))>;

namespace detail {
template<class T, auto Name>
inline constexpr bool has_member_name_impl = []<auto... Ns>(std::index_sequence<Ns...>) {
  return ((Name == member_name<Ns, T>()) or ...);
}(std::make_index_sequence<size<T>()>{});

template<class T, fixed_string Name>
[[nodiscard]] consteval auto diagnose_member_name() {
  constexpr std::string_view prefix = "`";
  constexpr std::string_view type = type_name<T>();
  if constexpr (size<T>()) {
    constexpr std::string_view infix1 = "` has no data member named `";
    constexpr std::string_view incorrect = Name;
    constexpr std::string_view infix2 = "`. Did you mean `";
    constexpr std::string_view correct = [] {
      constexpr auto distance = [](std::string_view correct) {
        constexpr std::string_view incorrect = Name;
        std::array<std::size_t, incorrect.size() + 1> prev;
        std::array<std::size_t, incorrect.size() + 1> curr{};
        for (decltype(prev.size()) i{}; i < prev.size(); ++i) prev[i] = i;
        for (decltype(correct.size()) i{}; i < correct.size(); ++i) {
          curr[0] = i + 1;
          for (decltype(incorrect.size()) j{}; j < incorrect.size(); ++j) {
            const auto del = prev[j + 1] + 1;
            const auto ins = curr[j] + 1;
            const auto sub = prev[j] + (correct[i] != incorrect[j]);
            const auto min_del_ins = del < ins ? del : ins;
            curr[j + 1] = min_del_ins < sub ? min_del_ins : sub;
          }
          auto temp = curr;
          curr = prev;
          prev = temp;
        }
        return prev.back();
      };
      const auto member_names = []<auto... Ns>(std::index_sequence<Ns...>) {
        return std::array<std::string_view, sizeof...(Ns)>{member_name<Ns, T>()...};
      }(std::make_index_sequence<size<T>()>{});
      auto closest_name = member_names[0];
      auto min_distance = distance(closest_name);
      for (decltype(member_names.size()) n{1}; n < member_names.size(); ++n) {
        const auto nth_member_name = member_names[n];
        const auto nth_distance = distance(nth_member_name);
        if (nth_distance < min_distance) {
          closest_name = nth_member_name;
          min_distance = nth_distance;
        }
      }
      return closest_name;
    }();
    constexpr std::string_view suffix = "`?";
    char message[prefix.size() + type.size() + infix1.size() + incorrect.size() + infix2.size() + correct.size() + suffix.size() + 1];
    auto out = message;
    for (const auto c : prefix) *out++ = c;
    for (const auto c : type) *out++ = c;
    for (const auto c : infix1) *out++ = c;
    for (const auto c : incorrect) *out++ = c;
    for (const auto c : infix2) *out++ = c;
    for (const auto c : correct) *out++ = c;
    for (const auto c : suffix) *out++ = c;
    *out = '\0';
    return fixed_string{message};
  } else {
    constexpr std::string_view suffix = "` has no data members.";
    char message[prefix.size() + type.size() + suffix.size() + 1];
    auto out = message;
    for (const auto c : prefix) *out++ = c;
    for (const auto c : type) *out++ = c;
    for (const auto c : suffix) *out++ = c;
    *out = '\0';
    return fixed_string{message};
  }
}

template<auto Message>
struct print { static constexpr auto value = false; };
template<class TMessage>
concept diagnosis = TMessage::value;
template<class T, auto Name>
concept member_name_diagnosis = diagnosis<print<diagnose_member_name<T, Name>()>>;
} // namespace detail

template<class T, fixed_string Name>
concept has_member_name = detail::has_member_name_impl<std::remove_cvref_t<T>, Name> or detail::member_name_diagnosis<std::remove_cvref_t<T>, Name>;

template<fixed_string Name, class T>
  requires (std::is_aggregate_v<std::remove_cvref_t<T>> and has_member_name<std::remove_cvref_t<T>, Name>)
constexpr decltype(auto) get(T&& t) noexcept {
  return visit([](auto&&... args) -> decltype(auto) {
    constexpr auto index = []<auto... Ns>(std::index_sequence<Ns...>){
      return (((std::string_view{Name} == member_name<Ns, std::remove_cvref_t<T>>()) ? Ns : decltype(Ns){}) + ...);
    }(std::make_index_sequence<size<std::remove_cvref_t<T>>()>{});
    return detail::nth_pack_element<index>(IMAPP_REFLECT_FWD(args)...);
  }, IMAPP_REFLECT_FWD(t));
}

template<template<class...> class R, class T>
  requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto to(T&& t) noexcept {
   if constexpr (std::is_lvalue_reference_v<decltype(t)>) {
    return visit([](auto&&... args) { return R<decltype(IMAPP_REFLECT_FWD(args))...>{IMAPP_REFLECT_FWD(args)...}; }, t);
  } else {
    return visit([](auto&&... args) { return R{IMAPP_REFLECT_FWD(args)...}; }, t);
  }
}

template<fixed_string... Members, class TSrc, class TDst>
  requires (std::is_aggregate_v<TSrc> and std::is_aggregate_v<TDst>)
constexpr auto copy(const TSrc& src, TDst& dst) noexcept -> void {
  constexpr auto contains = []([[maybe_unused]] const auto name) {
    return sizeof...(Members) == 0u or ((name == std::string_view{Members}) or ...);
  };
  auto dst_view = to<std::tuple>(dst);
  [&]<auto... Ns>(std::index_sequence<Ns...>) {
    ([&] {
      if constexpr (contains(member_name<Ns, TDst>()) and requires { std::get<Ns>(dst_view) = get<fixed_string<std::remove_cvref_t<decltype(member_name<Ns, TDst>()[0])>, std::size(member_name<Ns, TDst>())>(std::data(member_name<Ns, TDst>()))>(src); }) {
        std::get<Ns>(dst_view) = get<fixed_string<std::remove_cvref_t<decltype(member_name<Ns, TDst>()[0])>, std::size(member_name<Ns, TDst>())>(std::data(member_name<Ns, TDst>()))>(src);
      }
    }(), ...);
  }(std::make_index_sequence<size<TDst>()>{});
}

template<class R, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto to(T&& t) noexcept {
  R r{};
  copy(IMAPP_REFLECT_FWD(t), r);
  return r;
}

template<std::size_t N, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto size_of() -> std::size_t {
  return sizeof(std::remove_cvref_t<decltype(get<N>(detail::ext<T>))>);
}

template<std::size_t N, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto size_of(T&&) -> std::size_t {
  return size_of<N, std::remove_cvref_t<T>>();
}

template<std::size_t N, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto align_of() -> std::size_t {
  return alignof(std::remove_cvref_t<decltype(get<N>(detail::ext<T>))>);
}

template<std::size_t N, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto align_of(T&&) -> std::size_t {
  return align_of<N, std::remove_cvref_t<T>>();
}

template<std::size_t N, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto offset_of() -> std::size_t {
  if constexpr (not N) {
    return {};
  } else {
    constexpr auto offset = offset_of<N-1, T>() + size_of<N-1, T>();
    constexpr auto alignment = std::min(alignof(T), align_of<N, T>());
    constexpr auto padding = offset % alignment == 0 ? 0 : alignment - (offset % alignment);
    return offset + padding;
  }
}

template<std::size_t N, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
[[nodiscard]] constexpr auto offset_of(T&&) -> std::size_t {
  return offset_of<N, std::remove_cvref_t<T>>();
}

template<class T, class Fn> requires std::is_aggregate_v<std::remove_cvref_t<T>>
constexpr auto for_each(Fn&& fn) -> void {
  [&]<auto... Ns>(std::index_sequence<Ns...>) {
    (IMAPP_REFLECT_FWD(fn)(std::integral_constant<decltype(Ns), Ns>{}), ...);
  }(std::make_index_sequence<ImAppReflect::size<std::remove_cvref_t<T>>()>{});
}

template<class Fn, class T> requires std::is_aggregate_v<std::remove_cvref_t<T>>
constexpr auto for_each(Fn&& fn, T&&) -> void {
  [&]<auto... Ns>(std::index_sequence<Ns...>) {
    (IMAPP_REFLECT_FWD(fn)(std::integral_constant<decltype(Ns), Ns>{}), ...);
  }(std::make_index_sequence<ImAppReflect::size<std::remove_cvref_t<T>>()>{});
}
} // namespace ImAppReflect
#undef IMAPP_REFLECT_FWD_LIKE
#undef IMAPP_REFLECT_FWD
#pragma pop_macro("max")
#pragma pop_macro("min")
#define IMGUIAPP_HAS_REFLECT 1

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 26495)          // [Static Analyzer] Variable 'XXX' is uninitialized. Always initialize a member variable (type.6).
#endif

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations and basic types
//-----------------------------------------------------------------------------

// Forward declarations: ImGuiStatic layer
template <typename T> struct ImGuiStatic;

// Forward declarations: ImGuiApp layer
struct ImGuiApp;
struct ImGuiAppBase;
struct ImGuiAppLayerBase;
struct ImGuiAppLayer;
struct ImGuiAppTaskLayer;
struct ImGuiAppCommandLayer;

// Forward declarations: ImGuiAppControl layer
struct ImGuiAppControlBase;
template <typename PersistDataT, typename TempDataT, typename... DataDependencies>                struct ImGuiInterfaceAdapterBase;
template <typename Base, typename PersistDataT, typename TempDataT, typename... DataDependencies> struct ImGuiInterfaceAdapter;
template <typename PersistDataT, typename TempDataT, typename... DataDependencies> struct ImGuiAppControl;

// Forward declarations: ImGuiAppDisplay layer
struct ImGuiAppWindowBase;

// Forward declarations: ImGuiAppSidebar layer
struct ImGuiAppSidebarBase;

// Forward declarations: state history + input log (time travel / record-replay)
struct ImGuiAppStateHistory;
struct ImGuiAppInputLog;

// Frame/app configuration (relocated from the switch-only imapp_config.h).
typedef int ImGuiAppFrameFlags;
enum ImGuiAppFrameFlags_
{
    ImGuiAppFrameFlags_None              = 0,
    ImGuiAppFrameFlags_NoClear           = 1 << 0,
    ImGuiAppFrameFlags_NoPresent         = 1 << 1,
    ImGuiAppFrameFlags_NoPlatformWindows = 1 << 2,
};

struct ImGuiAppFrameConfig
{
    ImVec4             ClearColor;
    ImGuiAppFrameFlags Flags;

    ImGuiAppFrameConfig()
        : ClearColor(0.0f, 0.0f, 0.0f, 1.0f)
        , Flags(ImGuiAppFrameFlags_None)
    {
    }
};

enum ImGuiAppStyle_
{
    ImGuiAppStyle_Dark    = 0,
    ImGuiAppStyle_Light   = 1,
    ImGuiAppStyle_Classic = 2,
};
typedef int ImGuiAppStyle;

struct ImGuiAppPlatform
{
    const char* Name;
    void*       NativeWindowHandle;
};

typedef int ImGuiAppHeadlessMode;
enum ImGuiAppHeadlessMode_
{
    ImGuiAppHeadlessMode_None = 0,   // normal windowed app
    ImGuiAppHeadlessMode_Null,       // no GPU, no pixels (test engine only; backend CaptureFrame stays null)
    ImGuiAppHeadlessMode_Offscreen,  // GPU renders to an offscreen target; no OS window, CaptureFrame works
};

struct ImGuiAppConfig
{
    ImGuiAppPlatform     Platform;
    ImGuiConfigFlags     ConfigFlags;
    ImGuiAppStyle        Style;
    ImVec4               ClearColor;
    float                FontScale;
    float                DpiScale;
    ImGuiAppHeadlessMode Headless;
    bool                 PersistSettings;
    const char*          WindowTitle;
    int                  WindowWidth;
    int                  WindowHeight;

    ImGuiAppConfig()
    {
        Platform.Name               = nullptr;
        Platform.NativeWindowHandle = nullptr;
        ConfigFlags                 = 0;
        Style                       = ImGuiAppStyle_Dark;
        ClearColor                  = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        FontScale                   = 1.0f;
        DpiScale                    = 1.0f;
        Headless                    = ImGuiAppHeadlessMode_None;
        PersistSettings             = true;
        WindowTitle                 = nullptr;
        WindowWidth                 = 0;
        WindowHeight                = 0;
    }
};

enum ImGuiAppCommand
{
  ImGuiAppCommand_None,
  ImGuiAppCommand_Shutdown,
  ImGuiAppCommand_COUNT,
};

enum ImGuiAppCommandPrivate
{
  ImGuiAppCommandPrivate_ = ImGuiAppCommand_COUNT,
};

// Frame identity: one id per frame, taken at the top of OnDrawFrame. The correlation key across
// video frames, sidecar records, WAL lines, and test logs (docs/av-design.md).
struct ImGuiAppFrameID
{
  ImU64  FrameIndex; // monotonic from run start (not ImGui's frame count: survives context recreation)
  ImU64  Tsc;        // __rdtsc / platform equivalent at frame begin
  double TimeSec;    // QPC seconds since run start

  ImGuiAppFrameID() { FrameIndex = 0; Tsc = 0; TimeSec = 0.0; }
};

// Advisory frame pacer. Backend run loops call AppPacerWait once per iteration, before OnDrawFrame;
// Off returns immediately. The pacer decides what time the app SIMULATES; video timing is separate
// (imapp_av.h ImGuiAppAVTimingMode) -- honest-realtime video takes PTS from FrameID.TimeSec.
typedef int ImGuiAppPacerMode;
enum ImGuiAppPacerMode_
{
  ImGuiAppPacerMode_Off = 0,     // free-run; vsync/present mode governs
  ImGuiAppPacerMode_Target,      // pace wall clock to TargetHz (sleep + spin hybrid)
  ImGuiAppPacerMode_Fixed,       // Target pacing AND io.DeltaTime forced to exactly 1/TargetHz (determinism: replay, tests)
};

struct ImGuiAppPacer
{
  ImGuiAppPacerMode Mode;
  float             TargetHz;        // <= 0 with Mode_Target = pace to primary monitor refresh
  float             SleepSlackMs;    // spin the last N ms (OS sleep granularity guard)
                                     // read-only telemetry
  double            LastFrameMs;
  double            LastWaitMs;
  ImU64             MissedDeadlines; // frames that arrived after their deadline

  ImGuiAppPacer() { Mode = ImGuiAppPacerMode_Off; TargetHz = 0.0f; SleepSlackMs = 2.0f; LastFrameMs = 0.0; LastWaitMs = 0.0; MissedDeadlines = 0; }
};

// Write-ahead logger. Each record is appended and flushed BEFORE the operation it names executes, so after
// a crash the file tail names the in-flight operation. Attach to ImGuiApp::WAL; null = silent.
typedef int ImGuiAppWALLevel;
enum ImGuiAppWALLevel_
{
  ImGuiAppWALLevel_Off = 0,
  ImGuiAppWALLevel_Lifecycle,   // composition changes, storage, command dispatch
  ImGuiAppWALLevel_Frame,       // + per-frame per-layer phase begins (crash hunts; large files)
};

struct ImGuiAppWAL
{
  void*                  File;    // FILE*; typed void* to keep <cstdio> out of this header
  int                    Seq;     // monotonic record number
  ImGuiAppWALLevel       Level;
  const ImGuiAppFrameID* FrameID; // optional (point at ImGuiApp::FrameID): prefixes records "[tick:N tsc:T]"
  char                   Path[256];

  ImGuiAppWAL() { File = nullptr; Seq = 0; Level = ImGuiAppWALLevel_Off; FrameID = nullptr; Path[0] = 0; }
};

struct ImGuiAppAVFrame;   // imguiapp_av.h: one captured frame (pixels + FrameID + per-frame blob)
struct ImGuiAppRecorder;  // imguiapp_av.h: active recording; OnEncodeFrame pumps it

// Platform backend interface. The core app layer depends only on this vtable. Exactly one backend
// translation unit is linked per build; it defines ImGuiApp_GetPlatformBackend().
struct ImGuiAppPlatformBackend
{
  bool (*InitPlatform)(ImGuiApp* app, ImGuiAppConfig& config);
  void (*ShutdownPlatform)(ImGuiApp* app);
  int  (*RunLoop)(ImGuiApp* app);
  // Optional (null = backend cannot capture; recording fails with a clear error). Readback in the
  // encode phase (after render, before present). Encode-every-frame contract: the FIRST call of a
  // take returns the current frame (synchronously if the pipeline is unprimed); steady state may
  // return frame N-1's pixels PROVIDED out_frame->FrameID carries the id recorded at copy time (a
  // zeroed id gets the pumping frame's stamped by the recorder); a call with no new frame rendered
  // since the last one returns the freshest unreturned copy if already GPU-complete (never block),
  // else false -- callers drain the tail by re-calling after the GPU settles. Never return the
  // same FrameIndex twice. Pixels stay valid until the next CaptureFrame call.
  bool (*CaptureFrame)(ImGuiApp* app, ImGuiAppAVFrame* out_frame);
};

IMGUI_API const ImGuiAppPlatformBackend* ImGuiApp_GetPlatformBackend();

//-----------------------------------------------------------------------------
// [SECTION] Compile-time helpers (ImGuiStatic<>, ImGuiType<>)
//-----------------------------------------------------------------------------

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


struct ImGuiInterface { char Label[IM_LABEL_SIZE] = {}; ImGuiInterface() = default; virtual ~ImGuiInterface() = default; };

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

// State-delta event helpers over (this frame, last frame): rising = started, falling = ended, changed = either.
inline static bool ImAppRising (bool now, bool last) { return now && !last; }
inline static bool ImAppFalling(bool now, bool last) { return !now && last; }
inline static bool ImAppChanged(bool now, bool last) { return now ^ last; }
template <typename T>
inline static bool ImAppChanged(const T& now, const T& last) { return !(now == last); }

// splitmix64, no global state. Keep the seed in PersistData (seed in OnInitialize, step only through the
// seed) so snapshots and replay reproduce it; hidden effect sources (rand(), clocks) break replay.
inline static ImU64 ImAppRandom(ImU64* seed)
{
  ImU64 z = (*seed += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
inline static float ImAppRandomFloat(ImU64* seed, float mn, float mx)   // uniform in [mn, mx)
{
  return mn + (mx - mn) * (float)(ImAppRandom(seed) >> 40) / (float)(1ull << 24);
}
inline static int ImAppRandomInt(ImU64* seed, int mn, int mx)           // uniform in [mn, mx]
{
  return mx <= mn ? mn : mn + (int)(ImAppRandom(seed) % (ImU64)(mx - mn + 1));
}

// Best-effort symbolized backtrace of the caller as "  #N name (file:line)" lines; returns characters
// written. skip_frames drops innermost frames (0 = caller). Win32 only; other platforms write "".
IMGUI_API int ImStackTrace(char* out, int out_size, int skip_frames = 0);

// IM_ASSERT sink (wired via IMGUI_USER_CONFIG): logs expr/file/line + ImStackTrace to the SetAppAssertWAL
// sink and stderr, flushes, then __debugbreak()s under a debugger or exits with code 3 -- never the CRT popup.
IMGUI_API void ImGuiAppAssertFail(const char* expr, const char* file, int line);

// One authorable style-var override: Value.x for float vars, both lanes for ImVec2 vars; Active is
// runtime-toggleable.
// Aggregate (default member initializers, no ctors) so the build-time reflection walk sees
// its members. Float-valued vars brace-init as { var, ImVec2(v, 0.0f) }.
struct ImGuiAppStyleModDesc
{
  ImGuiStyleVar Var    = 0;
  ImVec2        Value  = ImVec2(0.0f, 0.0f);
  bool          Active = true;
};

// One authorable PushStyleColor override. Value is packed IM_COL32 RGBA.
// Aggregate for the same reason as ImGuiAppStyleModDesc.
struct ImGuiAppColorModDesc
{
  ImGuiCol Col    = 0;
  ImU32    Value  = 0;
  bool     Active = true;
};

// Routes one of a control's data dependencies to a specific producer instance at push time.
// TypeID names WHICH dependency of the pack is routed; Instance names the producer.
struct ImGuiAppDataBinding
{
  ImGuiID TypeID;           // ImGuiType<Dep>::ID of the dependency being routed
  ImGuiID Instance;         // producer's instance id (0 = the type singleton)
  bool    Optional = false; // absent producer resolves to null instead of asserting; the consumer
                            // handles null (and is rebound live when the producer is pushed/popped)
};

// Storage key for a control's instance data in ImGuiApp::Data: instance 0 keeps the bare
// data type id (the type singleton), any other instance qualifies it.
inline ImGuiID ImGuiAppInstanceKey(ImGuiID type_id, ImGuiID instance)
{
  if (instance == 0)
    return type_id;
  return (ImGuiID)ImHashData(&instance, sizeof(instance), type_id);
}

//-----------------------------------------------------------------------------
// [SECTION] Dear ImGui end-user API functions
//-----------------------------------------------------------------------------

namespace ImGui
{
  IMGUI_API void InitializeApp(ImGuiApp* app, const ImGuiAppConfig* config = nullptr);
  IMGUI_API void ShutdownApp(ImGuiApp* app);
  IMGUI_API void UpdateApp(ImGuiApp* app);                    // dt = GetIO().DeltaTime
  IMGUI_API void UpdateApp(ImGuiApp* app, float dt);          // explicit dt (replay injects here)
  IMGUI_API void RenderApp(const ImGuiApp* app);
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, void (*destroy)(void*));
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, void (*destroy)(void*));   // size > 0 => snapshottable
  IMGUI_API void RegisterAppStorage(ImGuiApp* app, ImGuiID id, void* ptr, int size, int temp_offset, int temp_size, void (*destroy)(void*));   // + input (TempData) byte range
  IMGUI_API void UnregisterAppStorage(ImGuiApp* app, ImGuiID id);   // destroys + removes one entry
  IMGUI_API void ClearAppStorage(ImGuiApp* app);

  // Snapshot appends snapshottable state to the ring (layout rebuilt + history cleared on composition
  // change); Restore copies snapshot `index` (0 = oldest) into the live app. False = nothing
  // snapshottable / invalid index or composition.
  IMGUI_API bool AppStateSnapshot(ImGuiApp* app, ImGuiAppStateHistory* h);
  IMGUI_API bool AppStateRestore(ImGuiApp* app, ImGuiAppStateHistory* h, int index);
  IMGUI_API void AppStateHistoryClear(ImGuiAppStateHistory* h);

  // AppInputRecord appends this frame's inputs (every control's TempData + dt) and resulting state hash;
  // call once per frame AFTER RenderApp. AppInputReplay re-runs the recorded frames through UpdateApp (no
  // rendering) -- restore the starting state first. out_first_divergence (if non-null) receives the first
  // frame whose state hash differs from the recording; -1 = deterministic reproduction.
  IMGUI_API bool AppInputRecord(ImGuiApp* app, ImGuiAppInputLog* log, float dt);
  IMGUI_API bool AppInputReplay(ImGuiApp* app, const ImGuiAppInputLog* log, int* out_first_divergence);
  IMGUI_API void AppInputLogClear(ImGuiAppInputLog* log);

  // Hash of the Persist + LastTemp prefix of every snapshottable instance -- the same
  // per-frame fingerprint AppInputRecord stores. 0 when nothing snapshottable exists.
  IMGUI_API ImGuiID AppStateHash(const ImGuiApp* app);

  // Fingerprint of the snapshottable slot LAYOUT (id + size + temp range per entry, in
  // StorageEntries order) -- what state hashes and snapshots depend on. The take's Identity
  // record carries this; F64's reconstruction identity gate requires the reconstruction app's
  // to equal the recorded one. 0 when nothing snapshottable exists.
  IMGUI_API ImU32 AppStateSchemaHash(const ImGuiApp* app);

  // Advisory frame pacing. Backend run loops call this once per iteration before OnDrawFrame; Off
  // returns immediately (the call is unconditional in the loops). Sleeps until deadline - SleepSlackMs,
  // spins the rest on QPC; Fixed mode also forces io.DeltaTime to exactly 1/TargetHz.
  IMGUI_API void AppPacerWait(ImGuiApp* app);

  // The rate the pacer actually paces at: TargetHz when positive, else the primary
  // monitor's refresh rate (the same resolution AppPacerWait performs). Callers that
  // need the frame rate (e.g. an encode config) read it here instead of guessing.
  IMGUI_API float AppPacerResolveHz(const ImGuiApp* app);

  // Consulted by the backend's per-viewport present hook (Renderer_SwapBuffers /
  // Platform_RenderWindow). True = present this frame; false = skip (contents unchanged
  // on that monitor until its next deadline). Main viewport never skips; Off pacer never skips.
  IMGUI_API bool AppPacerViewportShouldPresent(ImGuiApp* app, ImGuiViewport* viewport);

  // AppWALWrite appends one record and flushes to disk BEFORE returning; records below the WAL's level
  // are dropped. All three are null-safe on wal.
  IMGUI_API bool AppWALOpen(ImGuiAppWAL* wal, const char* path, ImGuiAppWALLevel level);
  IMGUI_API void AppWALClose(ImGuiAppWAL* wal);
  IMGUI_API void AppWALWrite(ImGuiAppWAL* wal, ImGuiAppWALLevel level, const char* fmt, ...) IM_FMTARGS(3);

  // WAL sink for IM_ASSERT failures routed to ImGuiAppAssertFail.
  IMGUI_API void SetAppAssertWAL(ImGuiAppWAL* wal);

  // Identity of the app's composition (layers, windows/sidebars, controls, in order). Changes exactly
  // when something is pushed or popped; mirrors poll it and reconcile only on change.
  IMGUI_API ImGuiID GetAppCompositionID(const ImGuiApp* app);

  // Controls sorted by the resolved dependency wiring: every producer before its consumers,
  // composition order among independents. Rebuilt when the composition changes. ONLY the Task
  // layer's OnUpdate pass iterates this -- update is the pass where producers write what
  // consumers read same-frame. Command collection and rendering stay composition order.
  IMGUI_API const ImVector<ImGuiAppControlBase*>* AppRebuildUpdateOrder(ImGuiApp* app);

  template <typename T>
  inline void PushAppSidebar(ImGuiApp* app, ImGuiViewport* vp, ImGuiDir dir, float size = 0.0f, ImGuiWindowFlags flags = 0);
  inline void PopAppSidebar(ImGuiApp* app);

  template <typename T>
  IMGUI_API inline void PushAppLayer(ImGuiApp* app);
  IMGUI_API inline void PopAppLayer(ImGuiApp* app);

  // instance: client-chosen discriminator; 0 = the type singleton (bare type-id key), any other
  // value keys a distinct instance of the same control data type. binds routes individual
  // dependencies to specific producer instances; an unrouted dependency resolves to the pushing
  // control's own instance id, then to the singleton (producer must be pushed first either way).
  template <typename T>
  IMGUI_API inline void PushAppControl(ImGuiApp* app, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);
  IMGUI_API inline void PopAppControl(ImGuiApp* app);

  template <typename T>
  IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);

  template <typename T>
  IMGUI_API inline void PushSidebarControl(ImGuiApp* app, ImGuiAppSidebarBase* sidebar, ImGuiID instance = 0, const ImGuiAppDataBinding* binds = nullptr, int binds_count = 0);

  // host: the PROCESS's real app, offered as the "Host app" live-mirror perspective
  // (strictly read-only there: time scrub is disabled for the host -- restoring its
  // state from inside its own render would mutate mid-frame).
  IMGUI_API void ShowAppLayerDemo(bool* p_open = nullptr, ImGuiApp* host = nullptr);

  // App-time transport (F29): number of state snapshots the running composer has recorded of its
  // snapshottable controls (0 = none). Drives the toolbar scrubber; exposed for the headless scrub test.
  IMGUI_API int AppComposerAppTimeFrames(ImGuiApp* host);

  // App-time transport source (F63): ImGuiAppTransportSource_ (LiveRing vs FileRun). Exposed for tests.
  IMGUI_API int AppComposerTransportSource(ImGuiApp* host);

  // Tick shown by the FILE-mode transport (F63): the decoded frame's tick at the current scrub index
  // (0 when no recorded run is open). Exposed for the scrub-to-tick acceptance.
  IMGUI_API ImU64 AppComposerFileRunShownTick(ImGuiApp* host);

  // Composer outliner column width (F32): >0 shown, 0 hidden. Exposed for the status-bar zone test.
  IMGUI_API float AppComposerOutlinerWidth(ImGuiApp* host);

  // Composer layout-preset visibilities (F36): bitmask tree(1)|insp(2)|code(4)|live(8). Exposed for the
  // preset-switch test; a Compose/Review/Observe pick sets a fixed combination.
  IMGUI_API int AppComposerLayoutFlags(ImGuiApp* host);

  // Push every Active (in-range) entry; returns the number pushed -- pop with PopStyleVar/PopStyleColor(count).
  IMGUI_API int PushAppStyleMods(const ImGuiAppStyleModDesc* mods, int count);
  IMGUI_API int PushAppColorMods(const ImGuiAppColorModDesc* mods, int count);
}

struct ImGuiAppLayerBase : ImGuiInterface
{
  virtual void OnAttach(ImGuiApp*)        const = 0;
  virtual void OnDetach(ImGuiApp*)        const = 0;
  virtual void OnUpdate(ImGuiApp*, float) const = 0;
  virtual void OnRender(const ImGuiApp*)  const = 0;
};

struct ImGuiAppItemBase : ImGuiInterface
{
  // Authored style/color overrides applied around the item's submission. OnStylePush latches the pushed
  // counts and OnStylePop pops those counts, so toggling Active mid-frame cannot unbalance the stacks.
  ImVector<ImGuiAppStyleModDesc> StyleMods;
  ImVector<ImGuiAppColorModDesc> ColorMods;
  mutable int                    _StylePushCount = 0;
  mutable int                    _ColorPushCount = 0;

  virtual void OnInitialize(ImGuiApp*)                         const = 0;
  virtual void OnShutdown(ImGuiApp*)                           const = 0;
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const = 0;
  virtual void OnUpdate(const ImGuiApp* app, float dt)         const = 0;
  virtual void OnRender(const ImGuiApp*)                       const = 0;
  virtual void OnStylePush(const ImGuiApp*)                    const;
  virtual void OnStylePop(const ImGuiApp*)                     const;
};

struct ImGuiAppWindowBase : ImGuiAppItemBase
{
	bool                           Open     = true;
	ImGuiWindow*                   Window   = nullptr;
	ImGuiViewport*                 Viewport = nullptr;
	ImGuiWindowFlags               Flags    = ImGuiWindowFlags_None;
	ImVector<ImGuiAppControlBase*> Controls;

  // Optional first-use placement (applied with ImGuiCond_FirstUseEver, so saved .ini wins).
  bool   HasInitialPlacement = false;
  ImVec2 InitialPos          = ImVec2(0.0f, 0.0f);
  ImVec2 InitialSize         = ImVec2(0.0f, 0.0f);
};

struct ImGuiAppSidebarBase : ImGuiAppWindowBase
{
  ImGuiDir DockDir = ImGuiDir_None;
  float    Size    = 0.0f;
};

// One reflected member of a control's Persist/Temp aggregate (live-mirror introspection).
// Name/TypeName point at static storage (reflect materializes null-terminated fixed_strings).
typedef int ImGuiAppLiveFieldKind;
enum ImGuiAppLiveFieldKind_
{
  ImGuiAppLiveFieldKind_Opaque = 0,   // unmapped type: Size bytes, TypeName from reflect
  ImGuiAppLiveFieldKind_Bool,
  ImGuiAppLiveFieldKind_S32,
  ImGuiAppLiveFieldKind_U32,
  ImGuiAppLiveFieldKind_F32,
  ImGuiAppLiveFieldKind_F64,
  ImGuiAppLiveFieldKind_Vec2,
  ImGuiAppLiveFieldKind_Vec4,
  ImGuiAppLiveFieldKind_CharArray,    // char[Size]
};

struct ImGuiAppLiveFieldDesc
{
  const char*           Name;
  const char*           TypeName;
  const char*           ElemTypeName; // ImVector element type (schema fields); null otherwise
  int                   Offset;       // within the Persist (or Temp) struct
  int                   Size;
  ImGuiAppLiveFieldKind Kind;
  bool                  Exact;        // TypeName is the member's declared C++ spelling (emit verbatim)
};

struct ImGuiAppControlBase : ImGuiAppItemBase
{
  // Re-expose the compile-time-erased data identity for live mirrors. Defaults inert; ImGuiAppControl<>
  // overrides from its pack.
  virtual ImGuiID GetControlDataID()                              const { return 0; }
  virtual int     GetControlDependencyIDs(ImGuiID* out, int cap)  const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
  virtual void    GetControlDataTypeName(char* out, int out_size) const { if (out && out_size > 0) out[0] = 0; }
  virtual void    GetControlTempDataTypeName(char* out, int out_size) const { if (out && out_size > 0) out[0] = 0; }
  // Reflected members of PersistDataT (temp_data false) or TempDataT (true). out null = count only.
  virtual int     GetControlFields(ImGuiAppLiveFieldDesc* out, int cap, bool temp_data) const { IM_UNUSED(out); IM_UNUSED(cap); IM_UNUSED(temp_data); return 0; }
  // Live instance memory of the RUNNING control (read-only by contract). False before initialization.
  virtual bool    GetControlLiveData(const void** out_persist, const void** out_temp) const { IM_UNUSED(out_persist); IM_UNUSED(out_temp); return false; }
  // False = outside the reflectable contract (not trivially copyable): opaque, exactly like snapshots.
  virtual bool    IsControlDataReflectable(bool temp_data) const { IM_UNUSED(temp_data); return false; }
  // Rebind the cached dependency pointers from their resolved keys. AppRebuildUpdateOrder calls
  // this after any push/pop, so a re-pushed producer's fresh instance data is picked up (and a
  // popped producer with live consumers asserts instead of dangling).
  virtual void    RefreshControlDependencyData(const ImGuiApp* app) { IM_UNUSED(app); }
  // Declared dependency TYPE ids (the compile-time pack, before resolution): what CAN be wired.
  // GetControlDependencyIDs returns where each slot is wired NOW (resolved storage keys).
  virtual int     GetControlDependencyTypeIDs(ImGuiID* out, int cap) const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
  // Per-slot Optional flags, same slot order as the id queries (mirror draws soft wires dimmed).
  virtual int     GetControlDependencyOptional(bool* out, int cap) const { IM_UNUSED(out); IM_UNUSED(cap); return 0; }
  // Re-route one declared dependency at runtime (Composer edge rewiring, no pop/re-push): the
  // slot whose type matches bind->TypeID re-resolves to that producer instance and the app's
  // update order rebuilds. False when TypeID is not in this control's pack.
  virtual bool    SetControlDependencyBinding(ImGuiApp* app, const ImGuiAppDataBinding* bind) { IM_UNUSED(app); IM_UNUSED(bind); return false; }
};

// Snapshot contract: aggregate + trivially copyable = byte-snapshottable. Owning containers
// are outside it. This governs SNAPSHOTS only; field enumeration is the weaker
// ImGuiAppFieldsVisible contract below. `using ImGuiAppOpaque = void;` opts out of both.
template <typename T>
inline constexpr bool ImGuiAppDataReflectable = std::is_aggregate_v<T>
                                             && std::is_trivially_copyable_v<T>
                                             && !requires { typename T::ImGuiAppOpaque; };

//-----------------------------------------------------------------------------
// Type schema registry: per-type field manifests BUILT AUTOMATICALLY AT COMPILE TIME from
// the reflection walk (imguiapp_reflect.h) and materialized into a runtime registry anyone can read (live
// mirrors, codegen, inspectors). No hand-authored manifests: instantiating a control (or
// reaching a type through another type's members) registers its manifest transitively.
// Snapshot reflectability (trivially-copyable byte copies) is separate and stricter.
//-----------------------------------------------------------------------------

struct ImGuiAppTypeSchema
{
  const char*                  TypeName; // display name (scope-stripped, matches ImGuiType<T>::Name)
  const ImGuiAppLiveFieldDesc* Fields;   // declaration order
  int                          Count;
  int                          Size;     // sizeof(T)
};

IMGUI_API void                        ImGuiAppRegisterTypeSchema(const ImGuiAppTypeSchema* schema);
IMGUI_API const ImGuiAppTypeSchema*   ImGuiAppFindTypeSchema(const char* type_name);

#ifdef IMGUIAPP_HAS_REFLECT

// Field-enumeration contract: the reflection walk handles any aggregate (the patched probe counts array
// members correctly), bounded by the 64-binding visit chain. The tag opts out explicitly.
template <typename T>
consteval bool ImGuiAppFieldsVisibleFn()
{
  if constexpr (!std::is_aggregate_v<T> || requires { typename T::ImGuiAppOpaque; })
    return false;
  else
    return ImAppReflect::size<T>() <= 64u;
}
template <typename T>
inline constexpr bool ImGuiAppFieldsVisible = ImGuiAppFieldsVisibleFn<T>();

// Display name in static null-terminated storage. Uses ImGuiType's signature parser (not
// reflect's, which cannot parse function-local types), so registry keys match
// GetControlDataTypeName by construction.
template <typename T>
inline const char* ImGuiAppTypeDisplayName()
{
  static char name[IM_LABEL_SIZE] = "";
  if (name[0] == 0)
    GenerateLabel<std::remove_cvref_t<T>>(name, IM_ARRAYSIZE(name));
  return name;
}

template <typename M> struct ImGuiAppVecElemOf { using Type = void; };
template <typename E> struct ImGuiAppVecElemOf<ImVector<E>> { using Type = E; };

// Compile-time declaration spellings for members reflect's scope/template-stripping
// type_name cannot spell: "ImVector<Elem>" and "Pointee*".
template <typename E>
struct ImGuiAppVecSpelling
{
  static constexpr bool PtrElem = std::is_pointer_v<E>;
  static constexpr std::string_view Elem = ImGuiType<E>::Name;   // ImGuiType strips the pointer; re-spell it
  static constexpr auto Make()
  {
    std::array<char, 9 + Elem.size() + 3> a{};
    std::size_t k = 0;
    for (char c : std::string_view{"ImVector<"})
      a[k++] = c;
    for (char c : Elem)
      a[k++] = c;
    if (PtrElem)
      a[k++] = '*';
    a[k++] = '>';
    return a;
  }
  static constexpr std::array<char, 9 + Elem.size() + 3> Value = Make();
};

template <typename P>
struct ImGuiAppPtrSpelling
{
  static constexpr std::string_view Pointee = ImGuiType<P>::Name;
  static constexpr auto Make()
  {
    std::array<char, Pointee.size() + 2> a{};
    std::size_t k = 0;
    for (char c : Pointee)
      a[k++] = c;
    a[k++] = '*';
    return a;
  }
  static constexpr std::array<char, Pointee.size() + 2> Value = Make();
};

template <typename T>
inline int ImGuiAppReflectFields(ImGuiAppLiveFieldDesc* out, int cap);

// Transitive automatic registration: materializes T's manifest into the runtime registry
// and, through the field walk, the manifest of every visible aggregate T reaches (members
// and ImVector elements). Reentrancy-safe: the entry registers before its fields fill.
template <typename T>
inline void ImGuiAppEnsureTypeRegistered()
{
  if constexpr (ImGuiAppFieldsVisible<T>)
  {
    constexpr int n = (int)ImAppReflect::size<T>();
    const char* type_name = ImGuiAppTypeDisplayName<T>();
    if (ImGuiAppFindTypeSchema(type_name) != nullptr)
      return;
    static ImGuiAppLiveFieldDesc fields[n > 0 ? n : 1];
    static ImGuiAppTypeSchema schema;
    schema.TypeName = type_name;
    schema.Fields = fields;
    schema.Count = 0;
    schema.Size = (int)sizeof(T);
    ImGuiAppRegisterTypeSchema(&schema);
    schema.Count = ImGuiAppReflectFields<T>(fields, n > 0 ? n : 1);
  }
}


// Aggregate walk shared by every ImGuiAppControl<> instantiation. Types outside the
// visibility contract yield zero fields rather than failing to compile.
template <typename T>
inline int ImGuiAppReflectFields(ImGuiAppLiveFieldDesc* out, int cap)
{
  if constexpr (ImGuiAppFieldsVisible<T>)
  {
    constexpr int n = (int)ImAppReflect::size<T>();
    if (out == nullptr || cap <= 0)
      return n;
    int written = 0;
    ImAppReflect::for_each<T>([&](auto I)
    {
      constexpr auto i = decltype(I)::value;
      using M = std::remove_cvref_t<decltype(ImAppReflect::get<i>(std::declval<T&>()))>;
      using E = typename ImGuiAppVecElemOf<M>::Type;
      if (written >= cap)
        return;
      ImGuiAppLiveFieldDesc* d = &out[written++];
      d->Name = ImAppReflect::member_name<i, T>().data();
      d->ElemTypeName = nullptr;
      d->Exact = true;
      d->Offset = (int)ImAppReflect::offset_of<i, T>();
      d->Size = (int)sizeof(M);
      if constexpr (std::is_same_v<M, bool>)                                        { d->Kind = ImGuiAppLiveFieldKind_Bool;      d->TypeName = "bool"; }
      else if constexpr (std::is_same_v<M, float>)                                  { d->Kind = ImGuiAppLiveFieldKind_F32;       d->TypeName = "float"; }
      else if constexpr (std::is_same_v<M, double>)                                 { d->Kind = ImGuiAppLiveFieldKind_F64;       d->TypeName = "double"; }
      else if constexpr (std::is_same_v<M, ImVec2>)                                 { d->Kind = ImGuiAppLiveFieldKind_Vec2;      d->TypeName = "ImVec2"; }
      else if constexpr (std::is_same_v<M, ImVec4>)                                 { d->Kind = ImGuiAppLiveFieldKind_Vec4;      d->TypeName = "ImVec4"; }
      else if constexpr (std::is_array_v<M> && std::is_same_v<std::remove_extent_t<M>, char>) { d->Kind = ImGuiAppLiveFieldKind_CharArray; d->TypeName = "char"; }
      else if constexpr (std::is_enum_v<M> && sizeof(M) == 4)                       { d->Kind = ImGuiAppLiveFieldKind_S32;       d->TypeName = "int"; }
      else if constexpr (std::is_integral_v<M> && std::is_signed_v<M> && sizeof(M) == 4)   { d->Kind = ImGuiAppLiveFieldKind_S32; d->TypeName = "int"; }
      else if constexpr (std::is_integral_v<M> && std::is_unsigned_v<M> && sizeof(M) == 4) { d->Kind = ImGuiAppLiveFieldKind_U32; d->TypeName = "unsigned int"; }
      else if constexpr (!std::is_same_v<E, void>)
      {
        // ImVector member: exact spelling; a non-pointer element is registered so codegen can
        // recurse into it (a pointer element's pointee is not owned -- never mirrored).
        d->Kind = ImGuiAppLiveFieldKind_Opaque;
        d->TypeName = ImGuiAppVecSpelling<E>::Value.data();
        if constexpr (!std::is_pointer_v<E>)
        {
          d->ElemTypeName = ImGuiAppTypeDisplayName<E>();
          ImGuiAppEnsureTypeRegistered<E>();
        }
      }
      else if constexpr (std::is_pointer_v<M>)
      {
        d->Kind = ImGuiAppLiveFieldKind_Opaque;
        d->TypeName = ImGuiAppPtrSpelling<std::remove_cv_t<std::remove_pointer_t<M>>>::Value.data();
      }
      else if constexpr (ImGuiAppFieldsVisible<M>)
      {
        // Nested visible aggregate: registered transitively, mirrored by codegen.
        d->Kind = ImGuiAppLiveFieldKind_Opaque;
        d->TypeName = ImGuiAppTypeDisplayName<M>();
        ImGuiAppEnsureTypeRegistered<M>();
      }
      else
      {
        // Leaf outside every contract: bytes, honestly labelled.
        d->Kind = ImGuiAppLiveFieldKind_Opaque;
        d->TypeName = ImGuiAppTypeDisplayName<M>();
        d->Exact = false;
      }
    });
    return written;
  }
  else
  {
    IM_UNUSED(out);
    IM_UNUSED(cap);
    return 0;
  }
}
#endif

template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiInterfaceAdapterBase : ImGuiInterface
{
  virtual void OnInitialize(ImGuiApp*, PersistDataT*, const DataDependencies*...)                                                const = 0;
  virtual void OnShutdown(ImGuiApp*, PersistDataT*, const DataDependencies*...)                                                  const = 0;
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*, const PersistDataT*, const TempDataT*, const DataDependencies*...) const = 0;
  virtual void OnUpdate(float, PersistDataT*, const TempDataT*, const TempDataT*, const DataDependencies*...)                    const = 0;
  virtual void OnRender(const PersistDataT*, TempDataT*, const DataDependencies*...)                                             const = 0;
};

struct ImGuiAppLayer : ImGuiAppLayerBase
{
  virtual void OnAttach(ImGuiApp*)        const override {}
  virtual void OnDetach(ImGuiApp*)        const override {}
  virtual void OnUpdate(ImGuiApp*, float) const override {}
  virtual void OnRender(const ImGuiApp*)  const override {}
};

struct ImGuiAppTaskLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppCommandLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppStatusLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppLayoutLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppDisplayLayer : ImGuiAppLayer
{
  virtual void OnAttach(ImGuiApp*)        const override final;
  virtual void OnDetach(ImGuiApp*)        const override final;
  virtual void OnUpdate(ImGuiApp*, float) const override final;
  virtual void OnRender(const ImGuiApp*)  const override final;
};

struct ImGuiAppBase : ImGuiInterface
{
  virtual void OnExecuteCommand(ImGuiAppCommand cmd) = 0;
  bool ShutdownPending = false;
};

struct ImGuiAppStorageEntry
{
  ImGuiID ID         = 0;
  void*   Ptr        = nullptr;
  int     Size       = 0; // byte size when the data is snapshottable (trivially copyable); 0 = opaque
  int     TempOffset = 0; // byte range of the TempData member inside the instance data -- the
  int     TempSize   = 0; // control's per-frame INPUT; [0, TempOffset) is Persist + LastTemp (state)
  void (*Destroy)(void*) = nullptr;
};

// Ring of MaxFrames byte snapshots of every snapshottable storage entry (opaque entries skipped). Layout
// is keyed to GetAppCompositionID and rebuilt (history cleared) when the composition changes.
struct ImGuiAppStateHistory
{
  ImGuiID           CompositionID = 0;   // layout is valid for exactly this composition
  int               FrameSize     = 0;   // bytes per snapshot (sum of slot sizes)
  int               MaxFrames     = 600; // ring capacity (default 600 ~ 10s at 60fps)
  int               Count         = 0;   // valid snapshots
  int               Head          = 0;   // ring write index
  ImVector<ImGuiID> SlotIds;             // storage entry per slot, in StorageEntries order
  ImVector<int>     SlotSizes;
  ImVector<char>    Frames;              // MaxFrames * FrameSize bytes
};

// Input log: per frame, every control's TempData + dt, plus a hash of the resulting state
// (Persist+LastTemp prefix of every instance) so replay can pinpoint the first divergent frame.
struct ImGuiAppInputLog
{
  ImGuiID           CompositionID; // layout is valid for exactly this composition
  int               FrameSize;     // bytes per frame: sum of temp sizes + sizeof(float) dt
  int               Count;         // recorded frames
  ImVector<ImGuiID> SlotIds;       // storage entry per slot, in StorageEntries order
  ImVector<int>     SlotOffsets;   // TempData offset within each instance
  ImVector<int>     SlotSizes;     // TempData size
  ImVector<char>    Frames;        // Count * FrameSize bytes, appended (caller clears between takes)
  ImVector<ImGuiID> StateHashes;   // per-frame post-update state hash (replay divergence reference)

  ImGuiAppInputLog() { CompositionID = 0; FrameSize = 0; Count = 0; }
};

struct ImGuiApp : ImGuiAppBase
{
  ImGuiStorage                   Data;
  ImVector<ImGuiAppStorageEntry> StorageEntries;
  ImVector<ImGuiAppLayerBase*>   Layers;
  ImVector<ImGuiAppWindowBase*>  Windows;
  ImVector<ImGuiAppSidebarBase*> Sidebars;
  ImVector<ImGuiAppControlBase*> Controls;
  ImVector<ImGuiAppControlBase*> UpdateOrder;         // dependency-sorted OnUpdate iteration (AppRebuildUpdateOrder)
  int                            CompositionRevision; // bumped by every storage register/unregister; unlike the composition HASH, pop+repush of the same type still advances it
  int                            UpdateOrderRevision; // revision UpdateOrder and the cached dependency bindings were built at
  ImGuiAppPlatform               Platform;
  ImVec4                         ClearColor;
  void*                          PlatformData;
  ImGuiAppWAL*                   WAL;      // optional write-ahead logger (caller-owned); null = silent
  ImGuiAppRecorder*              Recorder; // active recording (AppRecordBegin registers, AppRecordEnd clears); null = none
  ImGuiAppFrameID                FrameID;  // updated at the top of OnDrawFrame; correlation key for WAL/video/sidecar
  ImGuiAppPacer                  Pacer;    // advisory; consulted by the backend run loop via AppPacerWait
  bool                           Initialized;

  ImGuiApp() : CompositionRevision(0), UpdateOrderRevision(-1), PlatformData(nullptr), WAL(nullptr), Recorder(nullptr), Initialized(false) {}
  virtual ~ImGuiApp();
  int                         Run(int argc, char** argv);
  bool                        Initialize(const ImGuiAppConfig* config);
  bool                        IsInitialized() const { return Initialized; }
  virtual void                Shutdown();
  static void                 DrawFrame(ImGuiApp* app);
  virtual bool                OnInitialize(int argc, char** argv) { return true; }
  virtual void                OnLayout() {}
  // One frame = the four phases in order: draw (frame id, NewFrame, app layers/widgets),
  // render (draw data -> GPU, platform windows), encode (recorder pump reads the frame
  // just rendered), present. Backend run loops call Frame(); override a phase to extend it.
  void                        Frame();
  virtual void                OnDrawFrame();
  virtual void                OnRenderFrame();
  virtual void                OnEncodeFrame();
  virtual void                OnPresentFrame();
  virtual void                OnExecuteCommand(ImGuiAppCommand cmd) override;
  virtual bool                OnInitializePlatform(ImGuiAppConfig& config);
  virtual void                OnShutdownPlatform();
};

template <typename Base, typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiInterfaceAdapter : Base, ImGuiInterfaceAdapterBase<PersistDataT, TempDataT, DataDependencies...>
{
    // Created, registered in ImGuiApp::Data, and bound here by PushAppControl<>() before OnInitialize.
    struct InstanceData
    {
      PersistDataT PersistData;
      TempDataT    LastTempData;
      TempDataT    TempData;
    } *_InstanceData = nullptr;

    // Instance identity + dependency routing, resolved once by PushAppControl<>() before OnInitialize.
    ImGuiID                          _InstanceID = 0;
    std::tuple<DataDependencies*...> _Dependencies = {};
    ImGuiID                          _DependencyKeys[sizeof...(DataDependencies) > 0 ? sizeof...(DataDependencies) : 1] = {};
    bool                             _DependencyOptional[sizeof...(DataDependencies) > 0 ? sizeof...(DataDependencies) : 1] = {};

    // Resolution order: explicit binding -> this control's own instance id -> the type singleton.
    // Asserts when the resolved producer was not pushed before this control, unless the binding
    // marks the dependency Optional (then null, rebound live as the producer comes and goes).
    template <typename Dep>
    inline Dep* ResolveDependency(const ImGuiApp* app, const ImGuiAppDataBinding* binds, int binds_count, int slot)
    {
      const ImGuiID type_id = ImGuiType<Dep>::ID;
      for (int i = 0; i < binds_count; i++)
        if (binds[i].TypeID == type_id)
        {
          _DependencyKeys[slot] = ImGuiAppInstanceKey(type_id, binds[i].Instance);
          _DependencyOptional[slot] = binds[i].Optional;
          Dep* data = static_cast<Dep*>(app->Data.GetVoidPtr(_DependencyKeys[slot]));
          IM_ASSERT(data != nullptr || binds[i].Optional);
          return data;
        }
      if (_InstanceID != 0)
      {
        const ImGuiID own_key = ImGuiAppInstanceKey(type_id, _InstanceID);
        Dep* data = static_cast<Dep*>(app->Data.GetVoidPtr(own_key));
        if (data != nullptr)
        {
          _DependencyKeys[slot] = own_key;
          return data;
        }
      }
      _DependencyKeys[slot] = type_id;
      Dep* data = static_cast<Dep*>(app->Data.GetVoidPtr(type_id));
      IM_ASSERT(data != nullptr);
      return data;
    }

    inline void ResolveDependencies(const ImGuiApp* app, const ImGuiAppDataBinding* binds, int binds_count)
    {
      int slot = 0;
      IM_UNUSED(slot);
      _Dependencies = { ResolveDependency<DataDependencies>(app, binds, binds_count, slot++)... };
    }

    // Re-fetch each cached dependency pointer by its resolved key. Asserts when a producer was
    // popped while this consumer is still alive -- except Optional dependencies, which go null.
    template <typename Dep>
    inline Dep* LookupDependency(const ImGuiApp* app, int slot) const
    {
      Dep* data = static_cast<Dep*>(app->Data.GetVoidPtr(_DependencyKeys[slot]));
      IM_ASSERT(data != nullptr || _DependencyOptional[slot]);
      return data;
    }

    inline void RebindDependencies(const ImGuiApp* app)
    {
      int slot = 0;
      IM_UNUSED(slot);
      _Dependencies = { LookupDependency<DataDependencies>(app, slot++)... };
    }

    inline std::tuple<DataDependencies*...> GetAllDependencyData(const ImGuiApp* app) const { IM_UNUSED(app); return _Dependencies; }

    virtual void OnInitialize(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnInitialize(ImGuiApp* app) const override final
    {
      IM_ASSERT(_InstanceData != nullptr);
      std::apply([=, this](DataDependencies*... dependencies) { OnInitialize(app, &_InstanceData->PersistData, dependencies...); }, GetAllDependencyData(app));
    }

    virtual void OnShutdown(ImGuiApp*, PersistDataT*, const DataDependencies*...) const override {}
    virtual void OnShutdown(ImGuiApp* app) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnShutdown(app, &_InstanceData->PersistData, dependencies...); }, GetAllDependencyData(app));
    }

    virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*, const PersistDataT*, const TempDataT*, const DataDependencies*...) const override {}
    virtual void OnGetCommand(const ImGuiApp* app, ImGuiAppCommand* cmd) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnGetCommand(app, cmd, &_InstanceData->PersistData, &_InstanceData->TempData, dependencies...); }, GetAllDependencyData(app));
    }

    virtual void OnUpdate(float, PersistDataT*, const TempDataT*, const TempDataT*, const DataDependencies*...) const override {}
    virtual void OnUpdate(const ImGuiApp* app, float dt) const override final
    {
      std::apply([=, this](DataDependencies*... dependencies) { OnUpdate(dt, &_InstanceData->PersistData, &_InstanceData->TempData, &_InstanceData->LastTempData, dependencies...); }, GetAllDependencyData(app));
      _InstanceData->LastTempData = _InstanceData->TempData;
    }

    virtual void OnRender(const PersistDataT*, TempDataT*, const DataDependencies*...) const override {}
    virtual void OnRender(const ImGuiApp* app) const override final
    {
      _InstanceData->TempData = {};
      std::apply([=, this](DataDependencies*... dependencies) { OnRender(&_InstanceData->PersistData, &_InstanceData->TempData, dependencies...); }, GetAllDependencyData(app));
    }

};

template <typename PersistDataT, typename TempDataT, typename... DataDependencies>
struct ImGuiAppControl : ImGuiInterfaceAdapter<ImGuiAppControlBase, PersistDataT, TempDataT, DataDependencies...>
{
  using ControlDataType = PersistDataT;
  using ControlInstanceDataType = ImGuiInterfaceAdapter<ImGuiAppControlBase, PersistDataT, TempDataT, DataDependencies...>::InstanceData;

  // Constructing any control materializes its data types' manifests (and everything they
  // reach) into the runtime schema registry.
  ImGuiAppControl()
  {
#ifdef IMGUIAPP_HAS_REFLECT
    ImGuiAppEnsureTypeRegistered<PersistDataT>();
    ImGuiAppEnsureTypeRegistered<TempDataT>();
#endif
  }

  // Instance-qualified storage keys -- the same keys app->Data uses. Dependency ids are the
  // RESOLVED producer keys (push-time routing), so mirrors draw the actual wiring.
  virtual ImGuiID GetControlDataID() const override final { return ImGuiAppInstanceKey(ImGuiType<PersistDataT>::ID, this->_InstanceID); }

  virtual int GetControlDependencyIDs(ImGuiID* out, int cap) const override final
  {
    const int count = (int)(sizeof...(DataDependencies));
    if (out == nullptr || cap <= 0)
      return count;
    const int n = count < cap ? count : cap;
    for (int i = 0; i < n; i++)
      out[i] = this->_DependencyKeys[i];
    return n;
  }

  virtual void GetControlDataTypeName(char* out, int out_size) const override final { GenerateLabel<PersistDataT>(out, (size_t)out_size); }
  virtual void GetControlTempDataTypeName(char* out, int out_size) const override final { GenerateLabel<TempDataT>(out, (size_t)out_size); }

  virtual int GetControlFields(ImGuiAppLiveFieldDesc* out, int cap, bool temp_data) const override final
  {
#ifdef IMGUIAPP_HAS_REFLECT
    return temp_data ? ImGuiAppReflectFields<TempDataT>(out, cap) : ImGuiAppReflectFields<PersistDataT>(out, cap);
#else
    IM_UNUSED(out); IM_UNUSED(cap); IM_UNUSED(temp_data);
    return 0;
#endif
  }

  virtual bool IsControlDataReflectable(bool temp_data) const override final
  {
    return temp_data ? ImGuiAppDataReflectable<TempDataT> : ImGuiAppDataReflectable<PersistDataT>;
  }

  virtual void RefreshControlDependencyData(const ImGuiApp* app) override final
  {
    this->RebindDependencies(app);
  }

  virtual int GetControlDependencyTypeIDs(ImGuiID* out, int cap) const override final
  {
    const int count = (int)(sizeof...(DataDependencies));
    if (out == nullptr || cap <= 0)
      return count;
    const ImGuiID ids[] = { (ImGuiID)0, ImGuiType<DataDependencies>::ID... }; // leading 0 -> never zero-size
    const int n = count < cap ? count : cap;
    for (int i = 0; i < n; i++)
      out[i] = ids[i + 1];
    return n;
  }

  virtual int GetControlDependencyOptional(bool* out, int cap) const override final
  {
    const int count = (int)(sizeof...(DataDependencies));
    if (out == nullptr || cap <= 0)
      return count;
    const int n = count < cap ? count : cap;
    for (int i = 0; i < n; i++)
      out[i] = this->_DependencyOptional[i];
    return n;
  }

  virtual bool SetControlDependencyBinding(ImGuiApp* app, const ImGuiAppDataBinding* bind) override final
  {
    if (app == nullptr || bind == nullptr)
      return false;
    constexpr int count = (int)(sizeof...(DataDependencies));
    const ImGuiID ids[] = { (ImGuiID)0, ImGuiType<DataDependencies>::ID... };
    for (int slot = 0; slot < count; slot++)
    {
      if (ids[slot + 1] != bind->TypeID)
        continue;
      this->_DependencyKeys[slot] = ImGuiAppInstanceKey(bind->TypeID, bind->Instance);
      this->_DependencyOptional[slot] = bind->Optional;
      this->RebindDependencies(app);
      app->CompositionRevision++;   // rewiring changes the dependency DAG: update order must rebuild
      return true;
    }
    return false;
  }

  virtual bool GetControlLiveData(const void** out_persist, const void** out_temp) const override final
  {
    if (this->_InstanceData == nullptr)
      return false;
    if (out_persist != nullptr)
      *out_persist = &this->_InstanceData->PersistData;
    if (out_temp != nullptr)
      *out_temp = &this->_InstanceData->TempData;
    return true;
  }
};

template <typename T>
struct ImGuiAppWindow : ImGuiAppWindowBase
{
  ImGuiAppWindow() { GenerateLabel<T>(this->Label, sizeof(this->Label)); }   // bare class name; PushAppWindow suffixes only real duplicates

  virtual void OnInitialize(ImGuiApp*)                         const override {};
  virtual void OnShutdown(ImGuiApp*)                           const override {};
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override {};
  virtual void OnUpdate(const ImGuiApp* app, float dt)         const override {};
  virtual void OnRender(const ImGuiApp*)                       const override {};
};

template <typename T>
struct ImGuiAppSidebar : ImGuiAppSidebarBase
{
  ImGuiAppSidebar() { GenerateLabel<T>(this->Label, sizeof(this->Label)); }   // bare class name; PushAppSidebar suffixes only real duplicates

  virtual void OnInitialize(ImGuiApp*)                         const override {};
  virtual void OnShutdown(ImGuiApp*)                           const override {};
  virtual void OnGetCommand(const ImGuiApp*, ImGuiAppCommand*) const override {};
  virtual void OnUpdate(const ImGuiApp* app, float dt)         const override {};
  virtual void OnRender(const ImGuiApp*)                       const override {};
};

namespace ImGui
{
  template <typename T>
  inline void DestroyAppStorageValue(void* ptr)
  {
      IM_DELETE(static_cast<T*>(ptr));
  }

  // Visit every pushed control in update order: app-level, then sidebar-hosted, then window-hosted.
  // visitor(control, host) with host == nullptr for app-level. The single shared enumeration of "all controls".
  template <typename Visitor>
  inline void ForEachAppControl(ImGuiApp* app, Visitor visitor)
  {
      IM_ASSERT(app);
      for (int i = 0; i < app->Controls.Size; i++)
        visitor(app->Controls.Data[i], (ImGuiAppWindowBase*)nullptr);
      for (int s = 0; s < app->Sidebars.Size; s++)
        for (int i = 0; i < app->Sidebars.Data[s]->Controls.Size; i++)
          visitor(app->Sidebars.Data[s]->Controls.Data[i], (ImGuiAppWindowBase*)app->Sidebars.Data[s]);
      for (int w = 0; w < app->Windows.Size; w++)
        for (int i = 0; i < app->Windows.Data[w]->Controls.Size; i++)
          visitor(app->Windows.Data[w]->Controls.Data[i], app->Windows.Data[w]);
  }

  template <typename Visitor>
  inline void ForEachAppControl(const ImGuiApp* app, Visitor visitor)
  {
      IM_ASSERT(app);
      for (int i = 0; i < app->Controls.Size; i++)
        visitor((const ImGuiAppControlBase*)app->Controls.Data[i], (const ImGuiAppWindowBase*)nullptr);
      for (int s = 0; s < app->Sidebars.Size; s++)
        for (int i = 0; i < app->Sidebars.Data[s]->Controls.Size; i++)
          visitor((const ImGuiAppControlBase*)app->Sidebars.Data[s]->Controls.Data[i], (const ImGuiAppWindowBase*)app->Sidebars.Data[s]);
      for (int w = 0; w < app->Windows.Size; w++)
        for (int i = 0; i < app->Windows.Data[w]->Controls.Size; i++)
          visitor((const ImGuiAppControlBase*)app->Windows.Data[w]->Controls.Data[i], (const ImGuiAppWindowBase*)app->Windows.Data[w]);
  }

  inline void ShutdownAppControls(ImGuiApp* app, ImVector<ImGuiAppControlBase*>& controls)
  {
      IM_ASSERT(app);

      while (!controls.empty())
      {
        ImGuiAppControlBase* control = controls.back();
        controls.pop_back();
        if (app->WAL != nullptr)
        {
          char dt[IM_LABEL_SIZE];
          control->GetControlDataTypeName(dt, IM_ARRAYSIZE(dt));
          AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "shutdown control <%s>", dt);
        }
        control->OnShutdown(app);
        const ImGuiID data_id = control->GetControlDataID();   // read before delete
        IM_DELETE(control);
        if (data_id != 0)
          UnregisterAppStorage(app, data_id);
      }
  }

  template <typename T>
  inline void PushAppLayer(ImGuiApp* app)
  {
      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push layer %s", name);

      app->Layers.push_back(IM_NEW(T)());
      if (app->Layers.back()->Label[0] == 0)   // default Label to the type name
        ImStrncpy(app->Layers.back()->Label, name, IM_ARRAYSIZE(app->Layers.back()->Label));
      app->Layers.back()->OnAttach(app);
  }

  inline void PopAppLayer(ImGuiApp* app)
  {
      IM_ASSERT(app);

      if (app->Layers.empty())
        return;

      ImGuiAppLayerBase* layer = app->Layers.back();
      app->Layers.pop_back();
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop layer %s", layer->Label);
      layer->OnDetach(app);
      IM_DELETE(layer);
  }

  // The sole instance of a window type keeps its bare class name; a second live instance of the
  // same type gets "##N" so imgui window ids stay distinct.
  inline void AppDeduplicateItemLabel(char* label, int label_size, const ImVector<ImGuiAppWindowBase*>* windows, const ImVector<ImGuiAppSidebarBase*>* sidebars)
  {
    char base[IM_LABEL_SIZE];
    ImStrncpy(base, label, IM_ARRAYSIZE(base));
    for (int suffix = 2; ; suffix++)
    {
      bool taken = false;
      if (windows != nullptr)
        for (int i = 0; i < windows->Size && !taken; i++)
          taken = strcmp(windows->Data[i]->Label, label) == 0;
      if (sidebars != nullptr)
        for (int i = 0; i < sidebars->Size && !taken; i++)
          taken = strcmp(sidebars->Data[i]->Label, label) == 0;
      if (!taken)
        return;
      ImFormatString(label, (size_t)label_size, "%s##%d", base, suffix);
    }
  }

  template <typename T>
  inline void PushAppWindow(ImGuiApp* app)
  {
    IM_ASSERT(app);
    char name[IM_LABEL_SIZE];
    GenerateLabel<T>(name, sizeof(name));
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push window %s", name);
    T* window = IM_NEW(T)();
    IM_ASSERT(window);
    AppDeduplicateItemLabel(window->Label, IM_ARRAYSIZE(window->Label), &app->Windows, &app->Sidebars);
    app->Windows.push_back(window);
    app->Windows.back()->OnInitialize(app);
  }

  inline void PopAppWindow(ImGuiApp* app)
  {
    IM_ASSERT(app);

    if (app->Windows.empty())
      return;

    ImGuiAppWindowBase* window = app->Windows.back();
    app->Windows.pop_back();
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop window '%s'", window->Label);
    ShutdownAppControls(app, window->Controls);
    window->OnShutdown(app);
    IM_DELETE(window);
  }

  template <typename T>
  inline void PushAppSidebar(ImGuiApp* app, ImGuiViewport* vp, ImGuiDir dir, float size, ImGuiWindowFlags flags)
  {
    IM_ASSERT(app);
    char name[IM_LABEL_SIZE];
    GenerateLabel<T>(name, sizeof(name));
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push sidebar %s", name);
    T* sidebar = IM_NEW(T)();
    IM_ASSERT(sidebar);
    AppDeduplicateItemLabel(sidebar->Label, IM_ARRAYSIZE(sidebar->Label), &app->Windows, &app->Sidebars);
    sidebar->Viewport = vp;
    sidebar->DockDir = dir;
    sidebar->Size = size;
    sidebar->Flags = flags;
    app->Sidebars.push_back(sidebar);
    app->Sidebars.back()->OnInitialize(app);
  }

  inline void PopAppSidebar(ImGuiApp* app)
  {
    IM_ASSERT(app);

    if (app->Sidebars.empty())
      return;
    ImGuiAppSidebarBase* sidebar = app->Sidebars.back();
    app->Sidebars.pop_back();
    AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop sidebar '%s'", sidebar->Label);
    ShutdownAppControls(app, sidebar->Controls);
    sidebar->OnShutdown(app);
    IM_DELETE(sidebar);
  }

  template <typename T>
  inline void PushAppControl(ImGuiApp* app, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
  {
      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s (instance %u)", name, (unsigned)instance);

      // Instance data is keyed by the instance-qualified data type id so dependents can resolve it.
      ImGuiID id = ImGuiAppInstanceKey(ImGuiType<typename T::ControlDataType>::ID, instance);

      // One instance per (control data type, instance id).
      typename T::ControlInstanceDataType* instance_data = static_cast<typename T::ControlInstanceDataType*>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      T* control = IM_NEW(T)();
      IM_ASSERT(control);
      ImGuiAppControlBase* control_base = control;
      ImStrncpy(control_base->Label, name, sizeof(control_base->Label));

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size + TempData byte range (snapshot/replay);
      // heap-owning data registers opaque.
      {
        const bool snapshottable = std::is_trivially_copyable_v<typename T::ControlInstanceDataType>;
        RegisterAppStorage(app, id, instance_data,
            snapshottable ? (int)sizeof(typename T::ControlInstanceDataType) : 0,
            snapshottable ? (int)((char*)&instance_data->TempData - (char*)instance_data) : 0,
            snapshottable ? (int)sizeof(instance_data->TempData) : 0,
            DestroyAppStorageValue<typename T::ControlInstanceDataType>);
      }
      control->_InstanceID = instance;
      control->_InstanceData = instance_data;
      control->ResolveDependencies(app, binds, binds_count);
      app->Controls.push_back(control);
      app->Controls.back()->OnInitialize(app);
  }

  inline void PopAppControl(ImGuiApp* app)
  {
      IM_ASSERT(app);

      if (app->Controls.empty())
        return;

      ImGuiAppControlBase* control = app->Controls.back();
      app->Controls.pop_back();
      if (app->WAL != nullptr)
      {
        char dt[IM_LABEL_SIZE];
        control->GetControlDataTypeName(dt, IM_ARRAYSIZE(dt));
        AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "pop control <%s>", dt);
      }
      control->OnShutdown(app);
      const ImGuiID data_id = control->GetControlDataID();   // read before delete; pop frees what push registered
      IM_DELETE(control);
      if (data_id != 0)
        UnregisterAppStorage(app, data_id);
  }

  // Host a control inside a window: instance data registers in app->Data as usual, but the control joins
  // window->Controls and renders between the host window's Begin/End (no Begin of its own).
  template <typename T>
  IMGUI_API inline void PushWindowControl(ImGuiApp* app, ImGuiAppWindowBase* window, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
  {
      IM_ASSERT(app && window);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s (instance %u) into window '%s'", name, (unsigned)instance, window->Label);

      // Instance data is keyed by the instance-qualified data type id so dependents can resolve it.
      ImGuiID id = ImGuiAppInstanceKey(ImGuiType<typename T::ControlDataType>::ID, instance);

      // One instance per (control data type, instance id).
      typename T::ControlInstanceDataType* instance_data = static_cast<typename T::ControlInstanceDataType*>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      T* control = IM_NEW(T)();
      IM_ASSERT(control);
      ImGuiAppControlBase* control_base = control;
      ImStrncpy(control_base->Label, name, sizeof(control_base->Label));

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size + TempData byte range (snapshot/replay);
      // heap-owning data registers opaque.
      {
        const bool snapshottable = std::is_trivially_copyable_v<typename T::ControlInstanceDataType>;
        RegisterAppStorage(app, id, instance_data,
            snapshottable ? (int)sizeof(typename T::ControlInstanceDataType) : 0,
            snapshottable ? (int)((char*)&instance_data->TempData - (char*)instance_data) : 0,
            snapshottable ? (int)sizeof(instance_data->TempData) : 0,
            DestroyAppStorageValue<typename T::ControlInstanceDataType>);
      }
      control->_InstanceID = instance;
      control->_InstanceData = instance_data;
      control->ResolveDependencies(app, binds, binds_count);
      window->Controls.push_back(control);
      window->Controls.back()->OnInitialize(app);
  }

  template <typename T>
  IMGUI_API inline void PushSidebarControl(ImGuiApp* app, ImGuiAppSidebarBase* sidebar, ImGuiID instance, const ImGuiAppDataBinding* binds, int binds_count)
  {
      ImGuiID id;
      T* control;
      typename T::ControlInstanceDataType* instance_data;

      IM_ASSERT(app);

      char name[IM_LABEL_SIZE];
      GenerateLabel<T>(name, sizeof(name));
      AppWALWrite(app->WAL, ImGuiAppWALLevel_Lifecycle, "push control %s (instance %u) into sidebar '%s'", name, (unsigned)instance, sidebar ? sidebar->Label : "(null)");

      // Instance data is keyed by the instance-qualified data type id so dependents can resolve it.
      id = ImGuiAppInstanceKey(ImGuiType<typename T::ControlDataType>::ID, instance);

      // One instance per (control data type, instance id).
      instance_data = static_cast<decltype(instance_data)>(app->Data.GetVoidPtr(id));
      IM_ASSERT(nullptr == instance_data);

      control = IM_NEW(T)();
      IM_ASSERT(control);
      ImGuiAppControlBase* control_base = control;
      ImStrncpy(control_base->Label, name, sizeof(control_base->Label));

      instance_data = IM_NEW(typename T::ControlInstanceDataType)();
      IM_ASSERT(instance_data);

      app->Data.SetVoidPtr(id, instance_data);
      // Trivially-copyable instance data registers its size + TempData byte range (snapshot/replay);
      // heap-owning data registers opaque.
      {
        const bool snapshottable = std::is_trivially_copyable_v<typename T::ControlInstanceDataType>;
        RegisterAppStorage(app, id, instance_data,
            snapshottable ? (int)sizeof(typename T::ControlInstanceDataType) : 0,
            snapshottable ? (int)((char*)&instance_data->TempData - (char*)instance_data) : 0,
            snapshottable ? (int)sizeof(instance_data->TempData) : 0,
            DestroyAppStorageValue<typename T::ControlInstanceDataType>);
      }
      control->_InstanceID = instance;
      control->_InstanceData = instance_data;
      control->ResolveDependencies(app, binds, binds_count);
      sidebar->Controls.push_back(control);
      sidebar->Controls.back()->OnInitialize(app);
  }
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif
