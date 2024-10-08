#ifndef GNURADIO_GRAPH_UTILS_HPP
#define GNURADIO_GRAPH_UTILS_HPP

#include <complex>
#include <cstdint>
#include <cxxabi.h>
#include <functional>
#include <iostream>
#include <map>
#include <new>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <typeinfo>
#include <unordered_map>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <vir/simd.h>
#pragma GCC diagnostic pop

#include "typelist.hpp"

#ifndef DISABLE_SIMD
#define DISABLE_SIMD 0
#endif

namespace gr {

using Size_t = std::uint32_t; // strict type definition in view of cross-platform/cross-compiler/cross-network portability similar to 'std::size_t' (N.B. which is not portable)

namespace meta {

struct null_type {};

template<typename... Ts>
struct print_types;

template<typename CharT, std::size_t SIZE>
struct fixed_string {
    constexpr static std::size_t N              = SIZE;
    CharT                        _data[N + 1UZ] = {};

    constexpr fixed_string() = default;

    constexpr explicit(false) fixed_string(const CharT (&str)[N + 1]) noexcept {
        if constexpr (N != 0) {
            for (std::size_t i = 0; i < N; ++i) {
                _data[i] = str[i];
            }
        }
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return N; }

    [[nodiscard]] constexpr bool empty() const noexcept { return N == 0; }

    [[nodiscard]] constexpr explicit(false) operator std::string_view() const noexcept { return {_data, N}; }

    [[nodiscard]] explicit operator std::string() const noexcept { return {_data, N}; }

    [[nodiscard]] explicit(false) operator const char*() const noexcept { return _data; }

    [[nodiscard]] constexpr bool operator==(const fixed_string& other) const noexcept { return std::string_view{_data, N} == std::string_view(other); }

    template<std::size_t N2>
    [[nodiscard]] friend constexpr bool operator==(const fixed_string&, const fixed_string<CharT, N2>&) {
        return false;
    }

    constexpr auto operator<=>(const fixed_string& other) const noexcept = default;

    friend constexpr auto operator<=>(const fixed_string& fs, std::string_view sv) noexcept { return std::string_view(fs) <=> sv; }

    friend constexpr auto operator<=>(const fixed_string& fs, const std::string& str) noexcept { return std::string(fs) <=> str; }
};

template<typename CharT, std::size_t N>
fixed_string(const CharT (&str)[N]) -> fixed_string<CharT, N - 1>;

template<typename T>
struct is_fixed_string : std::false_type {};

template<typename CharT, std::size_t N>
struct is_fixed_string<gr::meta::fixed_string<CharT, N>> : std::true_type {};

template<typename T>
concept FixedString = is_fixed_string<T>::value;

template<typename CharT, std::size_t N1, std::size_t N2>
constexpr fixed_string<CharT, N1 + N2> operator+(const fixed_string<CharT, N1>& lhs, const fixed_string<CharT, N2>& rhs) noexcept {
    meta::fixed_string<CharT, N1 + N2> result{};
    for (std::size_t i = 0; i < N1; ++i) {
        result._data[i] = lhs._data[i];
    }
    for (std::size_t i = 0; i < N2; ++i) {
        result._data[N1 + i] = rhs._data[i];
    }
    result._data[N1 + N2] = '\0';
    return result;
}

namespace detail {
constexpr int log10(int n) noexcept {
    if (n < 10) {
        return 0;
    }
    return 1 + log10(n / 10);
}

constexpr int pow10(int n) noexcept {
    if (n == 0) {
        return 1;
    }
    return 10 * pow10(n - 1);
}

template<int N, std::size_t... Idx>
constexpr fixed_string<char, sizeof...(Idx)> make_fixed_string_impl(std::index_sequence<Idx...>) {
    constexpr auto numDigits = sizeof...(Idx);
    return {{('0' + (N / pow10(numDigits - Idx - 1) % 10))..., 0}};
}
} // namespace detail

template<int N>
constexpr auto make_fixed_string() noexcept {
    if constexpr (N == 0) {
        return fixed_string{"0"};
    } else {
        constexpr std::size_t digits = 1U + static_cast<std::size_t>(detail::log10(N));
        return detail::make_fixed_string_impl<N>(std::make_index_sequence<digits>());
    }
}

static_assert(fixed_string("0") == make_fixed_string<0>());
static_assert(fixed_string("1") == make_fixed_string<1>());
static_assert(fixed_string("2") == make_fixed_string<2>());
static_assert(fixed_string("123") == make_fixed_string<123>());
static_assert((fixed_string("out") + make_fixed_string<123>()) == fixed_string("out123"));

template<typename T>
[[nodiscard]] std::string type_name() noexcept {
    std::string type_name = typeid(T).name();
    int         status;
    char*       demangled_name = abi::__cxa_demangle(type_name.c_str(), nullptr, nullptr, &status);
    if (status == 0) {
        std::string ret(demangled_name);
        free(demangled_name);
        return ret;
    } else {
        free(demangled_name);
        return typeid(T).name();
    }
}

template<fixed_string val>
struct message_type {};

template<class... T>
constexpr bool always_false = false;

constexpr std::size_t invalid_index              = -1UZ;
constexpr std::size_t default_message_port_index = -2UZ;

#if HAVE_SOURCE_LOCATION
[[gnu::always_inline]] inline void precondition(bool cond, const std::source_location loc = std::source_location::current()) {
    struct handle {
        [[noreturn]] static void failure(std::source_location const& loc) {
            std::clog << "failed precondition in " << loc.file_name() << ':' << loc.line() << ':' << loc.column() << ": `" << loc.function_name() << "`\n";
            __builtin_trap();
        }
    };

    if (not cond) [[unlikely]] {
        handle::failure(loc);
    }
}
#else
[[gnu::always_inline]] inline void precondition(bool cond) {
    struct handle {
        [[noreturn]] static void failure() {
            std::clog << "failed precondition\n";
            __builtin_trap();
        }
    };

    if (not cond) [[unlikely]] {
        handle::failure();
    }
}
#endif

/**
 * T is tuple-like if it implements std::tuple_size, std::tuple_element, and std::get.
 * Tuples with size 0 are excluded.
 */
template<typename T>
concept tuple_like = (std::tuple_size<T>::value > 0) && requires(T tup) {
    { std::get<0>(tup) } -> std::same_as<typename std::tuple_element_t<0, T>&>;
};

template<template<typename...> class Template, typename Class>
struct is_instantiation : std::false_type {};

template<template<typename...> class Template, typename... Args>
struct is_instantiation<Template, Template<Args...>> : std::true_type {};
template<typename Class, template<typename...> class Template>
concept is_instantiation_of = is_instantiation<Template, Class>::value;

template<typename T>
concept map_type = is_instantiation_of<T, std::map> || is_instantiation_of<T, std::unordered_map>;

template<typename T>
concept vector_type = is_instantiation_of<std::remove_cv_t<T>, std::vector>;

template<typename T>
struct is_std_array_type : std::false_type {};

template<typename T, std::size_t N>
struct is_std_array_type<std::array<T, N>> : std::true_type {};

template<typename T>
concept array_type = is_std_array_type<std::remove_cv_t<T>>::value;

template<typename T, typename V = void>
concept array_or_vector_type = (vector_type<T> || array_type<T>) && (std::same_as<V, void> || std::same_as<typename T::value_type, V>);

namespace stdx = vir::stdx;

template<typename V, typename T = void>
concept any_simd = stdx::is_simd_v<V> && (std::same_as<T, void> || std::same_as<T, typename V::value_type>);

template<typename V, typename T>
concept t_or_simd = std::same_as<V, T> || any_simd<V, T>;

template<typename T>
concept complex_like = std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>;

template<fixed_string Name, typename PortList>
consteval std::size_t indexForName() {
    auto helper = []<std::size_t... Ids>(std::index_sequence<Ids...>) {
        auto static_name_for_index = []<std::size_t Id> {
            using Port = typename PortList::template at<Id>;
            if constexpr (requires(Port port) {
                              { port.static_name() };
                          }) {
                return Port::Name;
            } else {
                return Port::value_type::Name;
            }
        };

        constexpr int n_matches = ((static_name_for_index.template operator()<Ids>() == Name) + ...);
        static_assert(n_matches <= 1, "Multiple ports with that name were found. The name must be unique. You can "
                                      "still use a port index instead.");
        static_assert(n_matches == 1, "No port with the given name exists.");
        std::size_t result = meta::invalid_index;
        ((static_name_for_index.template operator()<Ids>() == Name ? (result = Ids) : 0), ...);
        return result;
    };
    return helper(std::make_index_sequence<PortList::size>());
}

// template<template<typename...> typename Type, typename... Items>
// using find_type = decltype(std::tuple_cat(std::declval<std::conditional_t<is_instantiation_of<Items, Type>, std::tuple<Items>, std::tuple<>>>()...));

template<template<typename> typename Pred, typename... Items>
struct find_type;

template<template<typename> typename Pred>
struct find_type<Pred> {
    using type = std::tuple<>;
};

template<template<typename> typename Pred, typename First, typename... Rest>
struct find_type<Pred, First, Rest...> {
    using type = decltype(std::tuple_cat(std::conditional_t<Pred<First>::value, std::tuple<First>, std::tuple<>>(), typename find_type<Pred, Rest...>::type()));
};

template<template<typename> typename Pred, typename... Items>
using find_type_t = typename find_type<Pred, Items...>::type;

template<typename Tuple, typename Default = void>
struct get_first_or_default;

template<typename First, typename... Rest, typename Default>
struct get_first_or_default<std::tuple<First, Rest...>, Default> {
    using type = First;
};

template<typename Default>
struct get_first_or_default<std::tuple<>, Default> {
    using type = Default;
};

template<typename Tuple, typename Default = void>
using get_first_or_default_t = typename get_first_or_default<Tuple, Default>::type;

template<typename... Lambdas>
struct overloaded : Lambdas... {
    using Lambdas::operator()...;
};

template<typename... Lambdas>
overloaded(Lambdas...) -> overloaded<Lambdas...>;

namespace detail {
template<template<typename...> typename Mapper, template<typename...> typename Wrapper, typename... Args>
Wrapper<Mapper<Args>...>* type_transform_impl(Wrapper<Args...>*);

template<template<typename...> typename Mapper, typename T>
Mapper<T>* type_transform_impl(T*);

template<template<typename...> typename Mapper>
void* type_transform_impl(void*);
} // namespace detail

template<template<typename...> typename Mapper, typename T>
using type_transform = std::remove_pointer_t<decltype(detail::type_transform_impl<Mapper>(static_cast<T*>(nullptr)))>;

template<typename Arg, typename... Args>
auto safe_min(Arg&& arg, Args&&... args) {
    if constexpr (sizeof...(Args) == 0) {
        return arg;
    } else {
        return std::min(std::forward<Arg>(arg), std::forward<Args>(args)...);
    }
}

template<typename Arg, typename... Args>
auto safe_pair_min(Arg&& arg, Args&&... args) {
    if constexpr (sizeof...(Args) == 0) {
        return arg;
    } else {
        return std::make_pair(std::min(std::forward<Arg>(arg).first, std::forward<Args>(args).first...), std::min(std::forward<Arg>(arg).second, std::forward<Args>(args).second...));
    }
}

template<typename Function, typename Tuple, typename... Tuples>
auto tuple_for_each(Function&& function, Tuple&& tuple, Tuples&&... tuples) {
    static_assert(((std::tuple_size_v<std::remove_cvref_t<Tuple>> == std::tuple_size_v<std::remove_cvref_t<Tuples>>) && ...));
    return [&]<std::size_t... Idx>(std::index_sequence<Idx...>) { (([&function, &tuple, &tuples...](auto I) { function(std::get<I>(tuple), std::get<I>(tuples)...); }(std::integral_constant<std::size_t, Idx>{}), ...)); }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>());
}

template<typename Function, typename Tuple, typename... Tuples>
void tuple_for_each_enumerate(Function&& function, Tuple&& tuple, Tuples&&... tuples) {
    static_assert(((std::tuple_size_v<std::remove_cvref_t<Tuple>> == std::tuple_size_v<std::remove_cvref_t<Tuples>>) && ...));
    [&]<std::size_t... Idx>(std::index_sequence<Idx...>) { ([&function](auto I, auto&& t0, auto&&... ts) { function(I, std::get<I>(t0), std::get<I>(ts)...); }(std::integral_constant<std::size_t, Idx>{}, tuple, tuples...), ...); }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>());
}

template<typename Function, typename Tuple, typename... Tuples>
auto tuple_transform(Function&& function, Tuple&& tuple, Tuples&&... tuples) {
    static_assert(((std::tuple_size_v<std::remove_cvref_t<Tuple>> == std::tuple_size_v<std::remove_cvref_t<Tuples>>) && ...));
    return [&]<std::size_t... Idx>(std::index_sequence<Idx...>) { return std::make_tuple([&function, &tuple, &tuples...](auto I) { return function(std::get<I>(tuple), std::get<I>(tuples)...); }(std::integral_constant<std::size_t, Idx>{})...); }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>());
}

template<typename Function, typename Tuple, typename... Tuples>
auto tuple_transform_enumerated(Function&& function, Tuple&& tuple, Tuples&&... tuples) {
    static_assert(((std::tuple_size_v<std::remove_cvref_t<Tuple>> == std::tuple_size_v<std::remove_cvref_t<Tuples>>) && ...));
    return [&]<std::size_t... Idx>(std::index_sequence<Idx...>) { return std::make_tuple([&function, &tuple, &tuples...](auto I) { return function(I, std::get<I>(tuple), std::get<I>(tuples)...); }(std::integral_constant<std::size_t, Idx>{})...); }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>());
}

static_assert(std::is_same_v<std::vector<int>, type_transform<std::vector, int>>);
static_assert(std::is_same_v<std::tuple<std::vector<int>, std::vector<float>>, type_transform<std::vector, std::tuple<int, float>>>);
static_assert(std::is_same_v<void, type_transform<std::vector, void>>);

#ifdef __cpp_lib_hardware_interference_size
static inline constexpr const std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
static inline constexpr const std::size_t kCacheLine = 64;
#endif

namespace detail {

template<typename T>
concept HasValueType = requires { typename T::value_type; };

template<typename T, typename = void>
struct fundamental_base_value_type {
    using type = T;
};

template<HasValueType T>
struct fundamental_base_value_type<T> {
    using type = typename fundamental_base_value_type<typename T::value_type>::type;
};

} // namespace detail

template<typename T>
using fundamental_base_value_type_t = typename detail::fundamental_base_value_type<T>::type;

static_assert(std::is_same_v<fundamental_base_value_type_t<int>, int>);
static_assert(std::is_same_v<fundamental_base_value_type_t<std::vector<float>>, float>);
static_assert(std::is_same_v<fundamental_base_value_type_t<std::vector<std::complex<double>>>, double>);

template<typename T>
concept string_like = std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_convertible_v<T, std::string_view>;

namespace detail {
template<typename T>
struct is_const_member_function : std::false_type {};

template<typename T, typename TReturn, typename... Args>
struct is_const_member_function<TReturn (T::*)(Args...) const> : std::true_type {};

template<typename T, typename TReturn, typename... Args>
struct is_const_member_function<TReturn (T::*)(Args...) const noexcept> : std::true_type {};

template<typename T>
struct is_noexcept_member_function : std::false_type {};

template<typename T, typename TReturn, typename... Args>
struct is_noexcept_member_function<TReturn (T::*)(Args...) noexcept> : std::true_type {};

template<typename T, typename TReturn, typename... Args>
struct is_noexcept_member_function<TReturn (T::*)(Args...) const noexcept> : std::true_type {};
} // namespace detail

template<typename T>
concept IsConstMemberFunction = std::is_member_function_pointer_v<T> && detail::is_const_member_function<T>::value;

template<typename T>
concept IsNoexceptMemberFunction = std::is_member_function_pointer_v<T> && detail::is_noexcept_member_function<T>::value;

} // namespace meta

#if HAVE_SOURCE_LOCATION
inline auto this_source_location(std::source_location l = std::source_location::current()) { return fmt::format("{}:{},{}", l.file_name(), l.line(), l.column()); }
#else
inline auto this_source_location() { return "not yet implemented"; }
#endif // HAVE_SOURCE_LOCATION

} // namespace gr

#endif // include guard
