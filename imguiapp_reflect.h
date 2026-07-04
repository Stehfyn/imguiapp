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

#pragma once

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
