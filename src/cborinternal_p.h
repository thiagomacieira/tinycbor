/****************************************************************************
**
** Copyright (C) 2018 Intel Corporation
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
** THE SOFTWARE.
**
****************************************************************************/

#ifndef CBORINTERNAL_P_H
#define CBORINTERNAL_P_H

#include "compilersupport_p.h"

#ifndef CBOR_NO_FLOATING_POINT
#  include <float.h>
#  include <math.h>
#else
#  ifndef CBOR_NO_HALF_FLOAT_TYPE
#    define CBOR_NO_HALF_FLOAT_TYPE     1
#  endif
#endif

#ifndef CBOR_NO_HALF_FLOAT_TYPE
#  ifdef __F16C__
#    include <immintrin.h>
static inline unsigned short encode_half(double val)
{
    return _cvtss_sh((float)val, 3);
}
static inline double decode_half(unsigned short half)
{
    return _cvtsh_ss(half);
}
#  else
/* software implementation of float-to-fp16 conversions */
static inline unsigned short encode_half(double val)
{
    uint64_t v;
    int sign, exp, mant;
    memcpy(&v, &val, sizeof(v));
    sign = v >> 63 << 15;
    exp = (v >> 52) & 0x7ff;
    mant = v << 12 >> 12 >> (53-11);    /* keep only the 11 most significant bits of the mantissa */
    exp -= 1023;
    if (exp == 1024) {
        /* infinity or NaN */
        exp = 16;
        mant >>= 1;
    } else if (exp >= 16) {
        /* overflow, as largest number */
        exp = 15;
        mant = 1023;
    } else if (exp >= -14) {
        /* regular normal */
    } else if (exp >= -24) {
        /* subnormal */
        mant |= 1024;
        mant >>= -(exp + 14);
        exp = -15;
    } else {
        /* underflow, make zero */
        return 0;
    }

    /* safe cast here as bit operations above guarantee not to overflow */
    return (unsigned short)(sign | ((exp + 15) << 10) | mant);
}

/* this function was copied & adapted from RFC 7049 Appendix D */
static inline double decode_half(unsigned short half)
{
    int exp = (half >> 10) & 0x1f;
    int mant = half & 0x3ff;
    double val;
    if (exp == 0) val = ldexp(mant, -24);
    else if (exp != 31) val = ldexp(mant + 1024, exp - 25);
    else val = mant == 0 ? INFINITY : NAN;
    return half & 0x8000 ? -val : val;
}
#  endif
#endif /* CBOR_NO_HALF_FLOAT_TYPE */

#ifndef CBOR_INTERNAL_API
#  define CBOR_INTERNAL_API
#endif

#ifndef CBOR_PARSER_MAX_RECURSIONS
#  define CBOR_PARSER_MAX_RECURSIONS 1024
#endif

/*
 * CBOR Major types
 * Encoded in the high 3 bits of the descriptor byte
 * See http://tools.ietf.org/html/rfc7049#section-2.1
 */
typedef enum CborMajorTypes {
    UnsignedIntegerType = 0U,
    NegativeIntegerType = 1U,
    ByteStringType = 2U,
    TextStringType = 3U,
    ArrayType = 4U,
    MapType = 5U,           /* a.k.a. object */
    TagType = 6U,
    SimpleTypesType = 7U
} CborMajorTypes;

/*
 * CBOR simple and floating point types
 * Encoded in the low 8 bits of the descriptor byte when the
 * Major Type is 7.
 */
typedef enum CborSimpleTypes {
    FalseValue              = 20,
    TrueValue               = 21,
    NullValue               = 22,
    UndefinedValue          = 23,
    SimpleTypeInNextByte    = 24,   /* not really a simple type */
    HalfPrecisionFloat      = 25,   /* ditto */
    SinglePrecisionFloat    = 26,   /* ditto */
    DoublePrecisionFloat    = 27,   /* ditto */
    Break                   = 31
} CborSimpleTypes;

enum {
    SmallValueBitLength     = 5U,
    SmallValueMask          = (1U << SmallValueBitLength) - 1,      /* 31 */
    Value8Bit               = 24U,
    Value16Bit              = 25U,
    Value32Bit              = 26U,
    Value64Bit              = 27U,
    IndefiniteLength        = 31U,

    MajorTypeShift          = SmallValueBitLength,
    MajorTypeMask           = (int) (~0U << MajorTypeShift),

    BreakByte               = (unsigned)Break | (SimpleTypesType << MajorTypeShift)
};

CBOR_INTERNAL_API CborError CBOR_INTERNAL_API_CC _cbor_value_prepare_string_iteration(CborValue *it);

static inline void copy_current_position(CborValue *dst, const CborValue *src)
{
    dst->ptr = src->ptr;
}

static inline bool can_read_bytes(const CborValue *it, size_t n)
{
    /* Convert the pointer subtraction to size_t since end >= ptr
     * (this prevents issues with (ptrdiff_t)n becoming negative).
     */
    return (size_t)(it->parser->end - it->ptr) >= n;
}

static inline void advance_bytes(CborValue *it, size_t n)
{
    it->ptr += n;
}

static inline void *read_bytes_unchecked(const CborValue *it, void *dst, size_t offset, size_t n)
{
    return memcpy(dst, it->ptr + offset, n);
}

#ifdef __GNUC__
__attribute__((warn_unused_result))
#endif
static inline void *read_bytes(const CborValue *it, void *dst, size_t offset, size_t n)
{
    if (can_read_bytes(it, offset + n))
        return read_bytes_unchecked(it, dst, offset, n);
    return NULL;
}

static inline uint16_t read_uint8(const CborValue *it, size_t offset)
{
    uint8_t result;
    read_bytes_unchecked(it, &result, offset, sizeof(result));
    return result;
}

static inline uint16_t read_uint16(const CborValue *it, size_t offset)
{
    uint16_t result;
    read_bytes_unchecked(it, &result, offset, sizeof(result));
    return cbor_ntohs(result);
}

static inline uint32_t read_uint32(const CborValue *it, size_t offset)
{
    uint32_t result;
    read_bytes_unchecked(it, &result, offset, sizeof(result));
    return cbor_ntohl(result);
}

static inline uint64_t read_uint64(const CborValue *it, size_t offset)
{
    uint64_t result;
    read_bytes_unchecked(it, &result, offset, sizeof(result));
    return cbor_ntohll(result);
}

static inline CborError extract_number_checked(const CborValue *it, uint64_t *value, size_t *bytesUsed)
{
    uint8_t descriptor;
    size_t bytesNeeded = 0;

    /* We've already verified that there's at least one byte to be read */
    read_bytes_unchecked(it, &descriptor, 0, 1);
    descriptor &= SmallValueMask;
    if (descriptor < Value8Bit) {
        *value = descriptor;
    } else if (unlikely(descriptor > Value64Bit)) {
        return CborErrorIllegalNumber;
    } else {
        bytesNeeded = (size_t)(1 << (descriptor - Value8Bit));
        if (!can_read_bytes(it, 1 + bytesNeeded))
            return CborErrorUnexpectedEOF;
        if (descriptor <= Value16Bit) {
            if (descriptor == Value16Bit)
                *value = read_uint16(it, 1);
            else
                *value = read_uint8(it, 1);
        } else {
            if (descriptor == Value32Bit)
                *value = read_uint32(it, 1);
            else
                *value = read_uint64(it, 1);
        }
    }

    if (bytesUsed)
        *bytesUsed = bytesNeeded;
    return CborNoError;
}

#endif /* CBORINTERNAL_P_H */
