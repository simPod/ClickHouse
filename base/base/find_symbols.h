#pragma once

#include <cstdint>
#include <string>

#if defined(__SSE2__)
    #include <emmintrin.h>
#endif
#if defined(__SSE4_2__)
    #include <nmmintrin.h>
#endif


/** find_first_symbols<c1, c2, ...>(begin, end):
  *
  * Allow to search for next character from the set of 'symbols...' in a string.
  * It is similar to 'strpbrk', 'strcspn' (and 'strchr', 'memchr' in the case of one symbol and '\0'),
  * but with the following differences:
  * - works with any memory ranges, including containing zero bytes;
  * - doesn't require terminating zero byte: end of memory range is passed explicitly;
  * - if not found, returns pointer to end instead of nullptr;
  * - maximum number of symbols to search is 16.
  *
  * Uses SSE 2 in case of small number of symbols for search and SSE 4.2 in the case of large number of symbols,
  *  that have more than 2x performance advantage over trivial loop
  *  in the case of parsing tab-separated dump with (probably escaped) string fields.
  * In the case of parsing tab separated dump with short strings, there is no performance degradation over trivial loop.
  *
  * Note: the optimal threshold to choose between SSE 2 and SSE 4.2 may depend on CPU model.
  *
  * find_last_symbols_or_null<c1, c2, ...>(begin, end):
  *
  * Allow to search for the last matching character in a string.
  * If no such characters, returns nullptr.
  */

namespace detail
{
template <char ...chars> constexpr bool is_in(char x) { return ((x == chars) || ...); } // NOLINT(misc-redundant-expression)

#if defined(__SSE2__)
template <char s0>
inline __m128i mm_is_in(__m128i bytes)
{
    __m128i eq0 = _mm_cmpeq_epi8(bytes, _mm_set1_epi8(s0));
    return eq0;
}

template <char s0, char s1, char... tail>
inline __m128i mm_is_in(__m128i bytes)
{
    __m128i eq0 = _mm_cmpeq_epi8(bytes, _mm_set1_epi8(s0));
    __m128i eq = mm_is_in<s1, tail...>(bytes);
    return _mm_or_si128(eq0, eq);
}
#endif

template <bool positive>
constexpr bool maybe_negate(bool x) { return x == positive; }

template <bool positive>
constexpr uint16_t maybe_negate(uint16_t x)
{
    if constexpr (positive)
        return x;
    else
        return ~x;
}

enum class ReturnMode
{
    End,
    Nullptr,
};


template <bool positive, ReturnMode return_mode, char... symbols>
inline const char * find_first_symbols_sse2(const char * const begin, const char * const end)
{
    const char * pos = begin;

#if defined(__SSE2__)
    for (; pos + 15 < end; pos += 16)
    {
        __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pos));

        __m128i eq = mm_is_in<symbols...>(bytes);

        uint16_t bit_mask = maybe_negate<positive>(uint16_t(_mm_movemask_epi8(eq)));
        if (bit_mask)
            return pos + __builtin_ctz(bit_mask);
    }
#endif

    for (; pos < end; ++pos)
        if (maybe_negate<positive>(is_in<symbols...>(*pos)))
            return pos;

    return return_mode == ReturnMode::End ? end : nullptr;
}


template <bool positive, ReturnMode return_mode, char... symbols>
inline const char * find_last_symbols_sse2(const char * const begin, const char * const end)
{
    const char * pos = end;

#if defined(__SSE2__)
    for (; pos - 16 >= begin; pos -= 16)     /// Assuming the pointer cannot overflow. Assuming we can compare these pointers.
    {
        __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pos - 16));

        __m128i eq = mm_is_in<symbols...>(bytes);

        uint16_t bit_mask = maybe_negate<positive>(uint16_t(_mm_movemask_epi8(eq)));
        if (bit_mask)
            return pos - 1 - (__builtin_clz(bit_mask) - 16);    /// because __builtin_clz works with mask as uint32.
    }
#endif

    --pos;
    for (; pos >= begin; --pos)
        if (maybe_negate<positive>(is_in<symbols...>(*pos)))
            return pos;

    return return_mode == ReturnMode::End ? end : nullptr;
}


template <bool positive, ReturnMode return_mode, size_t num_chars,
    char c01,     char c02 = 0, char c03 = 0, char c04 = 0,
    char c05 = 0, char c06 = 0, char c07 = 0, char c08 = 0,
    char c09 = 0, char c10 = 0, char c11 = 0, char c12 = 0,
    char c13 = 0, char c14 = 0, char c15 = 0, char c16 = 0>
inline const char * find_first_symbols_sse42(const char * const begin, const char * const end)
{
    const char * pos = begin;

#if defined(__SSE4_2__)
    constexpr int mode = _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_LEAST_SIGNIFICANT;

    __m128i set = _mm_setr_epi8(c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, c11, c12, c13, c14, c15, c16);

    for (; pos + 15 < end; pos += 16)
    {
        __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pos));

        if constexpr (positive)
        {
            if (_mm_cmpestrc(set, num_chars, bytes, 16, mode))
                return pos + _mm_cmpestri(set, num_chars, bytes, 16, mode);
        }
        else
        {
            if (_mm_cmpestrc(set, num_chars, bytes, 16, mode | _SIDD_NEGATIVE_POLARITY))
                return pos + _mm_cmpestri(set, num_chars, bytes, 16, mode | _SIDD_NEGATIVE_POLARITY);
        }
    }
#endif

    for (; pos < end; ++pos)
        if (   (num_chars == 1 && maybe_negate<positive>(is_in<c01>(*pos)))
            || (num_chars == 2 && maybe_negate<positive>(is_in<c01, c02>(*pos)))
            || (num_chars == 3 && maybe_negate<positive>(is_in<c01, c02, c03>(*pos)))
            || (num_chars == 4 && maybe_negate<positive>(is_in<c01, c02, c03, c04>(*pos)))
            || (num_chars == 5 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05>(*pos)))
            || (num_chars == 6 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06>(*pos)))
            || (num_chars == 7 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07>(*pos)))
            || (num_chars == 8 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08>(*pos)))
            || (num_chars == 9 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08, c09>(*pos)))
            || (num_chars == 10 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08, c09, c10>(*pos)))
            || (num_chars == 11 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, c11>(*pos)))
            || (num_chars == 12 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, c11, c12>(*pos)))
            || (num_chars == 13 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, c11, c12, c13>(*pos)))
            || (num_chars == 14 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, c11, c12, c13, c14>(*pos)))
            || (num_chars == 15 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, c11, c12, c13, c14, c15>(*pos)))
            || (num_chars == 16 && maybe_negate<positive>(is_in<c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, c11, c12, c13, c14, c15, c16>(*pos))))
            return pos;
    return return_mode == ReturnMode::End ? end : nullptr;
}


/// NOTE No SSE 4.2 implementation for find_last_symbols_or_null. Not worth to do.

template <bool positive, ReturnMode return_mode, char... symbols>
inline const char * find_first_symbols_dispatch(const char * begin, const char * end)
    requires(0 <= sizeof...(symbols) && sizeof...(symbols) <= 16)
{
#if defined(__SSE4_2__)
    if (sizeof...(symbols) >= 5)
        return find_first_symbols_sse42<positive, return_mode, sizeof...(symbols), symbols...>(begin, end);
    else
#endif
        return find_first_symbols_sse2<positive, return_mode, symbols...>(begin, end);
}

}


template <char... symbols>
inline const char * find_first_symbols(const char * begin, const char * end)
{
    return detail::find_first_symbols_dispatch<true, detail::ReturnMode::End, symbols...>(begin, end);
}

/// Returning non const result for non const arguments.
/// It is convenient when you are using this function to iterate through non-const buffer.
template <char... symbols>
inline char * find_first_symbols(char * begin, char * end)
{
    return const_cast<char *>(detail::find_first_symbols_dispatch<true, detail::ReturnMode::End, symbols...>(begin, end));
}

template <char... symbols>
inline const char * find_first_not_symbols(const char * begin, const char * end)
{
    return detail::find_first_symbols_dispatch<false, detail::ReturnMode::End, symbols...>(begin, end);
}

template <char... symbols>
inline char * find_first_not_symbols(char * begin, char * end)
{
    return const_cast<char *>(detail::find_first_symbols_dispatch<false, detail::ReturnMode::End, symbols...>(begin, end));
}

template <char... symbols>
inline const char * find_first_symbols_or_null(const char * begin, const char * end)
{
    return detail::find_first_symbols_dispatch<true, detail::ReturnMode::Nullptr, symbols...>(begin, end);
}

template <char... symbols>
inline char * find_first_symbols_or_null(char * begin, char * end)
{
    return const_cast<char *>(detail::find_first_symbols_dispatch<true, detail::ReturnMode::Nullptr, symbols...>(begin, end));
}

template <char... symbols>
inline const char * find_first_not_symbols_or_null(const char * begin, const char * end)
{
    return detail::find_first_symbols_dispatch<false, detail::ReturnMode::Nullptr, symbols...>(begin, end);
}

template <char... symbols>
inline char * find_first_not_symbols_or_null(char * begin, char * end)
{
    return const_cast<char *>(detail::find_first_symbols_dispatch<false, detail::ReturnMode::Nullptr, symbols...>(begin, end));
}


template <char... symbols>
inline const char * find_last_symbols_or_null(const char * begin, const char * end)
{
    return detail::find_last_symbols_sse2<true, detail::ReturnMode::Nullptr, symbols...>(begin, end);
}

template <char... symbols>
inline char * find_last_symbols_or_null(char * begin, char * end)
{
    return const_cast<char *>(detail::find_last_symbols_sse2<true, detail::ReturnMode::Nullptr, symbols...>(begin, end));
}

template <char... symbols>
inline const char * find_last_not_symbols_or_null(const char * begin, const char * end)
{
    return detail::find_last_symbols_sse2<false, detail::ReturnMode::Nullptr, symbols...>(begin, end);
}

template <char... symbols>
inline char * find_last_not_symbols_or_null(char * begin, char * end)
{
    return const_cast<char *>(detail::find_last_symbols_sse2<false, detail::ReturnMode::Nullptr, symbols...>(begin, end));
}


/// Slightly resembles boost::split. The drawback of boost::split is that it fires a false positive in clang static analyzer.
/// See https://github.com/boostorg/algorithm/issues/63
/// And https://bugs.llvm.org/show_bug.cgi?id=41141
template <char... symbols, typename To>
inline void splitInto(To & to, const std::string & what, bool token_compress = false)
{
    const char * pos = what.data();
    const char * end = pos + what.size();
    while (pos < end)
    {
        const char * delimiter_or_end = find_first_symbols<symbols...>(pos, end);

        if (!token_compress || pos < delimiter_or_end)
            to.emplace_back(pos, delimiter_or_end - pos);

        if (delimiter_or_end < end)
            pos = delimiter_or_end + 1;
        else
            pos = delimiter_or_end;
    }
}
