// Copyright 2018 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*!
* @file fixed_size_string.hpp
*
*/

#ifndef FASTRTPS_UTILS_FIXED_SIZE_BITMAP_HPP_
#define FASTRTPS_UTILS_FIXED_SIZE_BITMAP_HPP_

#include <array>
#include <string.h>

#if _MSC_VER
#include <intrin.h>
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS_PUBLIC
namespace eprosima {
namespace fastrtps {

template <class T>
struct DiffFunction
{
    constexpr auto operator () (T a, T b) const 
        -> decltype(a - b) 
    { 
        return a - b; 
    }
};

/**
 * Template class to hold a range of items using a custom bitmap.
 * @tparam T      Type of the elements in the range.
 *                Should have `>=` operator and `T + uint32_t` returning T. 
 * @tparam Diff   Functor calculating the difference of two T. 
 *                The result should be assignable to a uint32_t.
 * @tparam NBITS  Size in bits of the bitmap.
 *                Range of items will be [base, base + NBITS - 1].
 * @ingroup UTILITIES_MODULE
 */
template<class T, class Diff = DiffFunction<T>, uint32_t NBITS = 256>
class BitmapRange
{
    #define NITEMS ((NBITS + 31ul) / 32ul)
public:
    // Alias to improve readability.
    using bitmap_type = std::array<uint32_t, NITEMS>;

    /**
     * Default constructor.
     * Constructs an empty range with default base.
     */
    BitmapRange() noexcept : base_(), range_max_(base_ + (NBITS - 1)), bitmap_(), num_bits_(0u) {}

    /**
     * Base-specific constructor.
     * Constructs an empty range with specified base.
     * 
     * @param base   Specific base value for the created range.
     */
    explicit BitmapRange(T base) noexcept : base_(base), range_max_(base + (NBITS - 1)), bitmap_(), num_bits_(0u) {}

    // We don't need to define copy/move constructors/assignment operators as the default ones would be enough

    /**
     * Get base of the range.
     * @return a copy of the range base.
     */
    T base() const noexcept { return base_; }

    /**
     * Set a new base for the range.
     * This method resets the range and sets a new value for its base.
     *
     * @param base   New base value to set.
     */
    void base(T base) noexcept
    {
        base_ = base;
        range_max_ = base_ + (NBITS - 1);
        num_bits_ = 0;
        bitmap_.fill(0UL);
    }

    /**
     * Set a new base for the range, keeping old values where possible.
     * This method implements a sliding window mechanism for changing the base of the range.
     *
     * @param base   New base value to set.
     */
    void base_update(T base) noexcept
    {
        // Do nothing if base is not changing
        if (base == base_)
        {
            return;
        }

        Diff d_func;
        if (base > base_)
        {
            // Current content should move left
            uint32_t n_bits = d_func(base, base_);
            shift_map_left(n_bits);
        }
        else
        {
            // Current content should move right
            uint32_t n_bits = d_func(base_, base);
            shift_map_right(n_bits);
        }

        // Update base and range
        base_ = base;
        range_max_ = base_ + (NBITS - 1);
    }

    /**
     * Returns whether the range is empty (i.e. has all bits unset).
     * 
     * @return true if the range is empty, false otherwise.
     */
    bool empty() const noexcept { return num_bits_ == 0u; }

    /**
     * Returns the highest value set in the range.
     * 
     * @return the highest value set in the range. If the range is empty, the result is undetermined.
     */
    T max() const noexcept { return base_ + (num_bits_ - 1); }

    /**
     * Adds an element to the range.
     * Adds an element to the bitmap if it is in the allowed range.
     *
     * @param item   Value to be added.
     *
     * @return true if the item has been added (i.e. is in the allowed range), false otherwise.
     */
    bool add(const T& item) noexcept
    {
        // Check item is inside the allowed range.
        if ((item >= base_) && (range_max_ >= item))
        {
            // Calc distance from base to item, and set the corresponding bit.
            Diff d_func;
            uint32_t diff = d_func(item, base_);
            num_bits_ = std::max(diff + 1, num_bits_);
            uint32_t pos = diff >> 5;
            diff &= 31UL;
            bitmap_[pos] |= (1UL << (31UL - diff) );
            return true;
        }

        return false;
    }

    /**
     * Gets the current value of the bitmap.
     * This method is designed to be used when performing serialization of a bitmap range.
     * 
     * @param num_bits         Upon return, it will contain the number of significant bits in the bitmap.
     * @param bitmap           Upon return, it will contain the current value of the bitmap.
     * @param num_longs_used   Upon return, it will contain the number of valid elements on the returned bitmap.
     */
    void bitmap_get(uint32_t& num_bits, bitmap_type& bitmap, uint32_t& num_longs_used) const noexcept
    {
        num_bits = num_bits_;
        num_longs_used = (num_bits_ + 31UL) / 32UL;
        bitmap = bitmap_;
    }

    /**
     * Sets the current value of the bitmap.
     * This method is designed to be used when performing deserialization of a bitmap range.
     * 
     * @param num_bits   Number of significant bits in the input bitmap.
     * @param bitmap     Points to the begining of a uint32_t array holding the input bitmap.
     */
    void bitmap_set(uint32_t num_bits, const uint32_t* bitmap) noexcept
    {
        num_bits_ = std::min(num_bits, NBITS);
        uint32_t num_bytes = ( (num_bits_ + 31UL) / 32UL) * sizeof(uint32_t);
        bitmap_.fill(0UL);
        memcpy(bitmap_.data(), bitmap, num_bytes);
    }

    /**
     * Apply a function on every item on the range.
     *
     * @param f   Function to apply on each item.
     */
    template<class UnaryFunc>
    void for_each(UnaryFunc f) const
    {
        T item = base_;

        // Traverse through the significant items on the bitmap
        uint32_t n_longs = (num_bits_ + 31UL) / 32UL;
        for (uint32_t i = 0; i < n_longs; i++)
        {
            // Traverse through the bits set on the item, msb first.
            // Loop will stop when there are no bits set.
            uint32_t bits = bitmap_[i];
            while(bits)
            {
                // We use an intrinsic to find the index of the highest bit set.
                // Most modern CPUs have an instruction to count the leading zeroes of a word.
                // The number of leading zeroes will give us the index we need.
#if _MSC_VER
                unsigned long bit;
                _BitScanReverse(&bit, bits);
                uint32_t offset = 31UL - bit;
#else
                uint32_t offset = __builtin_clz(bits);
                uint32_t bit = 31UL - offset;
#endif

                // Call the function for the corresponding item
                f(item + offset);

                // Clear the most significant bit
                bits &= ~(1UL << bit);
            }

            // There are 32 items on each bitmap item.
            item = item + 32UL;
        }
    }

protected:
    T base_;               ///< Holds base value of the range.
    T range_max_;          ///< Holds maximum allowed value of the range.
    bitmap_type bitmap_;   ///< Holds the bitmap values.
    uint32_t num_bits_;    ///< Holds the highest bit set in the bitmap.

private:

    void shift_map_left(uint32_t n_bits)
    {
        if (n_bits >= num_bits_)
        {
            // Shifting more than most significant. Clear whole bitmap.
            num_bits_ = 0;
            bitmap_.fill(0UL);
        }
        else
        {
            // Significant bit will move left by n_bits
            num_bits_ -= n_bits;

            // Div and mod by 32
            uint32_t n_items = n_bits >> 5;
            n_bits &= 31UL;
            if (n_bits == 0)
            {
                // Shifting a multiple of 32 bits, just move the bitmap integers
                std::copy(bitmap_.begin() + n_items, bitmap_.end(), bitmap_.begin());
                std::fill_n(bitmap_.rbegin(), n_items, 0);
            }
            else
            {
                // Example. Shifting 44 bits. Should shift one complete word and 12 bits.
                // Need to iterate forward and take 12 bits from next word (shifting it 20 bits).
                // aaaaaaaa bbbbbbbb cccccccc dddddddd
                // bbbbbccc bbbbbbbb cccccccc dddddddd
                // bbbbbccc cccccddd ddddd000 dddddddd
                // bbbbbccc cccccddd ddddd000 00000000
                uint32_t overflow_bits = 32UL - n_bits;
                size_t last_index = NITEMS - 1u;
                for (size_t i = 0, n = n_items; n < last_index; i++, n++)
                {
                    bitmap_[i] = (bitmap_[n] << n_bits) | (bitmap_[n + 1] >> overflow_bits);
                }
                // Last one does not have next word
                bitmap_[last_index - n_items] = bitmap_[last_index] << n_bits;
                // Last n_items will become 0
                std::fill_n(bitmap_.rbegin(), n_items, 0);
            }
        }
    }

    void shift_map_right(uint32_t n_bits)
    {
        if (n_bits >= NBITS)
        {
            // Shifting more than total bitmap size. Clear whole bitmap.
            num_bits_ = 0;
            bitmap_.fill(0UL);
        }
        else
        {
            // Detect if highest bit will be dropped and take note, as we will need
            // to find new maximum bit in that case
            uint32_t new_num_bits = num_bits_ + n_bits;
            bool find_new_max = new_num_bits > NBITS;

            // Div and mod by 32
            uint32_t n_items = n_bits >> 5;
            n_bits &= 31UL;
            if (n_bits == 0)
            {
                // Shifting a multiple of 32 bits, just move the bitmap integers
                std::copy(bitmap_.rbegin() + n_items, bitmap_.rend(), bitmap_.rbegin());
                std::fill_n(bitmap_.begin(), n_items, 0);
            }
            else
            {
                // Example. Shifting 44 bits. Should shift one complete word and 12 bits.
                // Need to iterate backwards and take 12 bits from previous word (shifting it 20 bits).
                // aaaaaaaa bbbbbbbb cccccccc dddddddd
                // aaaaaaaa bbbbbbbb cccccccc bbbccccc
                // aaaaaaaa bbbbbbbb aaabbbbb bbbccccc
                // aaaaaaaa 000aaaaa aaabbbbb bbbccccc
                // 00000000 000aaaaa aaabbbbb bbbccccc
                uint32_t overflow_bits = 32UL - n_bits;
                size_t last_index = NITEMS - 1u;
                for (size_t i = last_index, n = last_index - n_items; n > 0; i--, n--)
                {
                    bitmap_[i] = (bitmap_[n] >> n_bits) | (bitmap_[n - 1] << overflow_bits);
                }
                // First item does not have previous word
                bitmap_[n_items] = bitmap_[0] >> n_bits;
                // First n_items will become 0
                std::fill_n(bitmap_.begin(), n_items, 0);
            }

            if (find_new_max)
            {
                new_num_bits = 0;
                for (uint32_t i = NITEMS; i > n_items;)
                {
                    --i;
                    uint32_t bits = bitmap_[i];
                    if (bits != 0)
                    {
                        bits = (bits & ~(bits - 1));
#if _MSC_VER
                        unsigned long bit;
                        _BitScanReverse(&bit, bits);
                        uint32_t offset = 32UL - bit;
#else
                        uint32_t offset = __builtin_clz(bits) + 1;
#endif
                        new_num_bits = (i << 5UL) + offset;
                        break;
                    }
                }
            }
            num_bits_ = new_num_bits;
        }
    }
}; 
    
}   // namespace fastrtps
}   // namespace eprosima

#endif   // DOXYGEN_SHOULD_SKIP_THIS_PUBLIC

#endif   // FASTRTPS_UTILS_FIXED_SIZE_BITMAP_HPP_