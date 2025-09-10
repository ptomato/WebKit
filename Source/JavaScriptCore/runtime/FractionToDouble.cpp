// Copyright (C) 2023-2025 Mozilla Foundation
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "config.h"
#include "FractionToDouble.h"

#include "MathCommon.h"

namespace JSC {

// The following is adapted from
// https://github.com/mozilla-firefox/firefox/blob/main/js/src/builtin/temporal/Temporal.cpp

// Return the quotient and remainder of the division.
static inline std::pair<Int128, Int128> divremInt128(const Int128& numerator,
    const UInt128& denominator)
{
    return { numerator / denominator, numerator % denominator };
}

// Return the real number value of the fraction |numerator / denominator|.
//
// As an optimization we multiply the remainder by 16 when computing the number
// of digits after the decimal point, i.e. we compute four instead of one bit of
// the fractional digits. The denominator is therefore required to not exceed
// 2**(N - log2(16)), where N is the number of non-sign bits in the mantissa.
static double fractionToDoubleSlow(const Int128& numerator, const Int128& denominator)
{
    ASSERT(denominator > 0, "expected positive denominator");
    ASSERT(denominator <= (static_cast<Int128>(1) << (std::numeric_limits<Int128>::digits - 4)),
        "denominator too large");

    auto [quot, rem] = divremInt128(absInt128(numerator), static_cast<UInt128>(denominator));

    // Simple case when no remainder is present.
    if (!rem) {
        double sign = numerator < 0 ? -1 : 1;
        return sign * static_cast<double>(quot);
    }

    // Significand including the implicit one of IEEE-754 floating point numbers.
    constexpr uint32_t significandWidthWithImplicitOne = 53;

    // Number of leading zeros for a correctly adjusted significand.
    constexpr uint32_t significandLeadingZeros = 64 - significandWidthWithImplicitOne;

    // Exponent bias for an integral significand. (`Double::kExponentBias` is the
    // bias for the binary fraction `1.xyz * 2**exp`. For an integral significand
    // the significand width has to be added to the bias.)
    constexpr int32_t exponentBias = ((1U << 10) - 1) + (significandWidthWithImplicitOne - 1);

    // Significand, possibly unnormalized.
    uint64_t significand = 0;

    // Significand ignored msd bits.
    uint32_t ignoredBits = 0;

    // Read quotient, from most to least significant digit. Stop when the
    // significand got too large for double precision.
    int32_t shift = std::numeric_limits<UInt128>::digits;
    for (; shift && !ignoredBits; shift -= 4) {
        uint64_t digit = static_cast<uint64_t>(quot >> (shift - 4)) & 0xf;

        significand = significand * 16 + digit;
        ignoredBits = significand >> significandWidthWithImplicitOne;
    }

    // Read remainder, from most to least significant digit. Stop when the
    // remainder is zero or the significand got too large.
    int32_t fractionDigit = 0;
    for (; rem && !ignoredBits; fractionDigit++) {
        auto [digit, next] = divremInt128(rem * 16, static_cast<UInt128>(denominator));
        rem = next;

        significand = significand * 16 + static_cast<uint64_t>(digit);
        ignoredBits = significand >> significandWidthWithImplicitOne;
    }

    // Unbiased exponent. (`shift` remaining bits in the quotient, minus the
    // fractional digits.)
    int32_t exponent = shift - (fractionDigit * 4);

    // Significand got too large and some bits are now ignored. Adjust the
    // significand and exponent.
    if (ignoredBits) {
        //        significand
        //  ___________|__________
        // /                      |
        // [xxx················yyy|
        //  \_/                \_/
        //   |                  |
        // ignoredBits       extraBits
        //
        // `ignoredBits` have to be shifted back into the 53 bits of the significand
        // and `extraBits` has to be checked if the result has to be rounded up.

        // Number of ignored/extra bits in the significand.
        uint32_t extraBitsCount = 32 - std::countl_zero(ignoredBits);
        ASSERT(extraBitsCount > 0);

        // Extra bits in the significand.
        uint32_t extraBits = uint32_t(significand) & ((1 << extraBitsCount) - 1);

        // Move the ignored bits into the proper significand position and adjust the
        // exponent to reflect the now moved out extra bits.
        significand >>= extraBitsCount;
        exponent += extraBitsCount;

        ASSERT(!(significand >> significandWidthWithImplicitOne),
            "no excess bits in the significand");

        // When the most significant digit in the extra bits is set, we may need to
        // round the result.
        uint32_t msdExtraBit = extraBits >> (extraBitsCount - 1);
        if (msdExtraBit) {
            // Extra bits, excluding the most significant digit.
            uint32_t extraBitExcludingMsdMask = (1 << (extraBitsCount - 1)) - 1;

            // Unprocessed bits in the quotient.
            auto bitsBelowExtraBits = quot & ((1 << shift) - 1);

            // Round up if the extra bit's msd is set and either the significand is
            // odd or any other bits below the extra bit's msd are non-zero.
            //
            // Bits below the extra bit's msd are:
            // 1. The remaining bits of the extra bits.
            // 2. Any bits below the extra bits.
            // 3. Any rest of the remainder.
            bool shouldRoundUp = (significand & 1)
                || (extraBits & extraBitExcludingMsdMask)
                || bitsBelowExtraBits
                || rem;
            if (shouldRoundUp) {
                // Add one to the significand bits.
                significand += 1;

                // If they overflow, the exponent must also be increased.
                if ((significand >> significandWidthWithImplicitOne)) {
                    exponent++;
                    significand >>= 1;
                }
            }
        }
    }

    ASSERT(significand > 0, "significand is non-zero");
    ASSERT(!(significand >> significandWidthWithImplicitOne),
        "no excess bits in the significand");

    // Move the significand into the correct position and adjust the exponent
    // accordingly.
    uint32_t significandZeros = std::countl_zero(significand);
    if (significandZeros < significandLeadingZeros) {
        uint32_t shift = significandLeadingZeros - significandZeros;
        significand >>= shift;
        exponent += shift;
    } else if (significandZeros > significandLeadingZeros) {
        uint32_t shift = significandZeros - significandLeadingZeros;
        significand <<= shift;
        exponent -= shift;
    }

    // Combine the individual bits of the double value and return it.
    constexpr uint64_t signBitShift = 63;
    constexpr uint64_t exponentBitShift = static_cast<uint64_t>(significandWidthWithImplicitOne - 1);

    uint64_t signBit = static_cast<uint64_t>(numerator < static_cast<Int128>(0) ? 1 : 0)
        << signBitShift;
    uint64_t exponentBits = static_cast<uint64_t>(exponent + exponentBias)
        << exponentBitShift;
    uint64_t significandBits = significand & ((static_cast<uint64_t>(1) << exponentBitShift) - 1);
    return std::bit_cast<double>(signBit | exponentBits | significandBits);
}

double fractionToDouble(const Int128& numerator, const Int128& denominator)
{
    ASSERT(denominator > 0);

    if (!numerator)
        return 0;

    // When both values can be represented as doubles, use double division to
    // compute the exact result. The result is exact, because double division is
    // guaranteed to return the exact result.
    if (isSafeInteger(static_cast<double>(numerator))
        && isSafeInteger(static_cast<double>(denominator))) [[likely]]
        return static_cast<double>(numerator) / static_cast<double>(denominator);

    // Otherwise call into fractionToDoubleSlow() to compute the exact result.
    return fractionToDoubleSlow(numerator, denominator);
}

} // namespace JSC

