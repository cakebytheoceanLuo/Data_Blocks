// ---------------------------------------------------------------------------------------------------
// IMLAB
// ---------------------------------------------------------------------------------------------------
#ifndef INCLUDE_IMLAB_UTIL_COUNT_BYTE1_H_
#define INCLUDE_IMLAB_UTIL_COUNT_BYTE1_H_
// ---------------------------------------------------------------------------------------------------
#include <stdint.h>
#include <assert.h>
#include <immintrin.h>
#include <smmintrin.h>
#include <cstdint>
#include <functional>
#include <sstream>
#include <iostream>
#include <bitset>
#include <vector>
#include <numeric> // std::iota
#include "imlab/util/Count.h"
#include "imlab/SIMD/SIMD_Types.h"
// ---------------------------------------------------------------------------------------------------
namespace imlab {
// ---------------------------------------------------------------------------------------------------
using SIMD::Greater8u;
using SIMD::LesserOrEqual8u;
using SIMD::GreaterOrEqual8u;
using SIMD::Lesser8u;
using SIMD::SIMD_WIDTH_BYTE1;
using SIMD::SIMD_WIDTH_BYTE1_LOG2;
using SIMD::calculate_match_counter;
using SIMD::SIMD_ITERATION_BYTE1;
using SIMD::SIMD_WIDTH_BYTE1_LOG2_REDUCE;
// ---------------------------------------------------------------------------------------------------
namespace util {
// ---------------------------------------------------------------------------------------------------
// Encoding: Byte1------------------------------------------------------------------------------
template <>
uint32_t first_count<EncodingType::Byte1, CompareType::EQUAL>(const Predicate &p,
                                                              const uint32_t dataLength,
                                                              uint8_t *column_base_pointer,
                                                              std::array<uint32_t, TUPLES_PER_DATABLOCK> &index_vector,
                                                              const SMA* sma_ptr,
                                                              const uint32_t index_offset,
                                                              const uint32_t col_count_start_index) {
  const auto[min, max] = sma_ptr->getSMA_min_max();
  const uint64_t value = p.val;
  const uint8_t differ = value - min;

  uint32_t* writer = &index_vector[col_count_start_index];

  /// IF Not 32 aligned, THEN process then using scalar
  uint32_t numSimdIterations = 0;
  uint8_t pre_scalar_counter = 0;
  if ((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0) {
    numSimdIterations = (dataLength >> SIMD_WIDTH_BYTE1_LOG2);  /// equivalent as / 32
  } else {
    while (pre_scalar_counter < dataLength && (uintptr_t)reinterpret_cast<void*>(column_base_pointer + pre_scalar_counter) % 32 != 0) {
      const uint8_t value = *(column_base_pointer + pre_scalar_counter);
      if (value == differ) {
        *writer = (pre_scalar_counter + index_offset);
        writer++;
      }
      pre_scalar_counter++;
    }
    numSimdIterations = (dataLength - pre_scalar_counter) >> (SIMD_WIDTH_BYTE1_LOG2);  /// equivalent as / 32
  }

  /// Case for first scan => full loop all data needed
  #ifdef SIMD_
  const __m256i comparisonValueVec = _mm256_set1_epi8(differ);
  assert((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0);  /// get datablock->data 32 bytes align for AVX

  __m256i* column_base_pointer_m256 = reinterpret_cast<__m256i*>(column_base_pointer + pre_scalar_counter);

  for (uint32_t i = 0; i != numSimdIterations; ++i) {
    /// Load and compare 32 values
    __m256i attributeVec = _mm256_load_si256(column_base_pointer_m256 + i);  /// __m256i attributeVec=_mm256_load_si256(reinterpret_cast<__m256i*>(&column[scanPos]));
    __m256i selMask = _mm256_cmpeq_epi8(attributeVec, comparisonValueVec);  /// compare for equal  const uint8_t differ = value - min;
    uint32_t bitMask = _mm256_movemask_epi8(selMask);

    /// Lookup match positions and update positions vector
    const uint32_t scanPos = (i << SIMD_WIDTH_BYTE1_LOG2);  /// uint32_t scanPos = from + (i * simdWidth);
    simd_iterations<SIMD_ITERATION_BYTE1>(bitMask, scanPos + index_offset, writer);
  }

  /// Rest in scalar code, scan the rest
  const uint32_t scalars_start_index = (numSimdIterations << SIMD_WIDTH_BYTE1_LOG2) + pre_scalar_counter;
  for (uint32_t scalar_i = scalars_start_index; scalar_i < dataLength; scalar_i++) {
    const uint8_t value = *(column_base_pointer + scalar_i);
    if (value == differ) {
      *writer = scalar_i;
      writer++;
    }
  }

  return (writer - &index_vector[col_count_start_index]);
#endif

  #ifdef SCALAR
  size_t match_counter = 0;
  for (size_t in = 0 ; in < dataLength; in++) {
    if (*(column_base_pointer + in) == differ) {
      index_vector[match_counter] = in;
      match_counter++;
    }
  }
  return match_counter;
  #endif

  #ifdef DEBUG_COUNT
    for (size_t in = 0 ; in < dataLength; in++) {
      if (in == 0) { std::cout << "count() Byte1 EQUAL for val: " << p.val << std::endl; }
      printf("ptr has value %lu, ", *(basePointer + in) + min);
      printf("ptr has in pointer %p\n", basePointer + in);
    }
  #endif
}

template <>
uint32_t non_first_count<EncodingType::Byte1, CompareType::EQUAL>(const Predicate &p,
                                                                  const uint32_t num_index,
                                                                  uint8_t *column_base_pointer,
                                                                  std::array<uint32_t, TUPLES_PER_DATABLOCK> &index_vector,
                                                                  const SMA *sma_ptr,
                                                                  const uint32_t index_offset,
                                                                  const uint32_t col_count_start_index,
                                                                  const uint32_t reader_offset) {
  const auto[min, max] = sma_ptr->getSMA_min_max();
  const int* ptr_32      = reinterpret_cast<int*>(column_base_pointer);  /// should be uint8_t*, but the _mm256_i32gather_epi32 only allows int*
  const uint8_t* ptr_8 = reinterpret_cast<uint8_t*>(column_base_pointer);

  const uint64_t value = p.val;
  const uint8_t differ = value - min;

  uint32_t* writer = &index_vector[col_count_start_index];

  /// IF Not 32 aligned, THEN process then using scalar
  uint32_t numSimdIterations = 0;
  uint8_t pre_scalar_counter = 0;
  if ((uintptr_t)reinterpret_cast<void*>(&index_vector[reader_offset]) % 32 == 0) {
      numSimdIterations = num_index >> (SIMD_WIDTH_BYTE1_LOG2_REDUCE);  /// Each time 8 uint32_t = 256 bit
  } else {
      while (pre_scalar_counter < num_index
             && (uintptr_t)reinterpret_cast<void*>(&index_vector[reader_offset + pre_scalar_counter]) % 32 != 0) {
          const uint32_t index = index_vector[reader_offset + pre_scalar_counter] - index_offset;
          const uint8_t value = *(ptr_8 + index);
          if (value == differ) {
              *writer = (index + index_offset);
              writer++;
          }
          pre_scalar_counter++;
      }
      numSimdIterations = (num_index - pre_scalar_counter) >> (SIMD_WIDTH_BYTE1_LOG2_REDUCE);  /// Each time 8 uint32_t = 256 bit
  }

  const __m256i* index_vector_ptr_mm256 = reinterpret_cast<__m256i*>(&index_vector[reader_offset + pre_scalar_counter]);

  /// Case for non first scan
#ifdef SIMD_
  const __m256i comparisonValueVec = _mm256_set1_epi8(differ);
  assert((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0);  /// get datablock->data 32 bytes align for AVX

  /// Offset of index, in order to get the data in this page
  const __m256i index_offset_256 = _mm256_set1_epi32(index_offset);

  for (uint32_t i = 0; i != numSimdIterations; i++) {
    /// Load 8 Indexes from Index Vector, subtract the offset to get real index in the page
    __m256i indexes = *(index_vector_ptr_mm256 + i) /* reader */;  /// x/8d &indexes
    __m256i real_indexes = _mm256_sub_epi32(indexes, index_offset_256);

    /// Load 8 Int at the 8 indexes from Datablock
    __m256i gather = _mm256_i32gather_epi32(ptr_32, real_indexes, 1);     /// x/8u &gather

    __m256i selMask = _mm256_cmpeq_epi8(gather, comparisonValueVec);  /// compare for Greater8u
    const uint32_t bitMask = _mm256_movemask_epi8(selMask);

    /// Pick only the first bit from every 4 bits
    uint8_t bitMask_u8 = _pext_u32(bitMask, 0x11111111);

    /// Lookup match positions and update positions vector
    auto& matchEntry = NON_FirstScan_MatchTable[bitMask_u8];
    auto dest = _mm256_permutevar8x32_epi32(indexes, matchEntry.reg256);  /// x/8d &dest
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(writer), dest);
    writer += __builtin_popcount(bitMask_u8);
  }

  /// Rest in scalar code, scan the rest
   const uint32_t scalars_start_index = (numSimdIterations << SIMD_WIDTH_BYTE1_LOG2_REDUCE) + reader_offset + pre_scalar_counter;
   for (uint32_t scalar_i = scalars_start_index; scalar_i < num_index + reader_offset; scalar_i++) {
    const uint32_t index = index_vector[scalar_i] - index_offset;
    const uint8_t value = *(ptr_8 + index);
    if (value == differ) {
      *writer = (index + index_offset);
      writer++;
    }
  }
  return (writer - &index_vector[col_count_start_index]);
#endif
}

template <>
uint32_t first_count<EncodingType::Byte1, CompareType::GREATER_THAN>(const Predicate &p,
                                                               const uint32_t dataLength,
                                                               uint8_t *column_base_pointer,
                                                               std::array<uint32_t, TUPLES_PER_DATABLOCK> &index_vector,
                                                               const SMA* sma_ptr,
                                                               const uint32_t index_offset,
                                                               const uint32_t col_count_start_index) {
  const auto[min, max] = sma_ptr->getSMA_min_max();
  const uint64_t value = p.val;
  const uint8_t differ = value - min;

  uint32_t* writer = &index_vector[col_count_start_index];

  /// IF Not 32 aligned, THEN process then using scalar
  uint32_t numSimdIterations = 0;
  uint8_t pre_scalar_counter = 0;
  if ((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0) {
    numSimdIterations = (dataLength >> SIMD_WIDTH_BYTE1_LOG2);  /// equivalent as / 32
  } else {
    while (pre_scalar_counter < dataLength && (uintptr_t)reinterpret_cast<void*>(column_base_pointer + pre_scalar_counter) % 32 != 0) {
      const uint8_t value = *(column_base_pointer + pre_scalar_counter);
      if (value > differ){
        *writer = (pre_scalar_counter + index_offset);
        writer++;
      }
      pre_scalar_counter++;
    }
    numSimdIterations = (dataLength - pre_scalar_counter) >> (SIMD_WIDTH_BYTE1_LOG2);  /// equivalent as / 32
  }

  /// Case for first scan => full loop all data needed
  #ifdef SIMD_
  const __m256i comparisonValueVec = _mm256_set1_epi8(differ);
  assert((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0);  /// get datablock->data 32 bytes align for AXV

  __m256i* column_base_pointer_m256 = reinterpret_cast<__m256i*>(column_base_pointer + pre_scalar_counter);

  for (uint32_t i = 0; i != numSimdIterations; i++) {
    /// Load and compare 32 values
    __m256i attributeVec = _mm256_load_si256(column_base_pointer_m256 + i);  /// __m256i attributeVec=_mm256_load_si256(reinterpret_cast<__m256i*>(&column[scanPos]));
    __m256i selMask = Greater8u(attributeVec, comparisonValueVec);  /// compare for great than
    uint32_t bitMask = _mm256_movemask_epi8(selMask);

    /// Lookup match positions and update positions vector
    const uint32_t scanPos = (i << SIMD_WIDTH_BYTE1_LOG2);  /// uint32_t scanPos = from + (i * simdWidth);
    simd_iterations<SIMD_ITERATION_BYTE1>(bitMask, scanPos + index_offset, writer);
  }

  /// Rest in scalar code, scan the rest
  const uint32_t scalars_start_index = (numSimdIterations << SIMD_WIDTH_BYTE1_LOG2) + pre_scalar_counter;
  for (uint32_t scalar_i = scalars_start_index; scalar_i < dataLength; scalar_i++) {
    const uint16_t value = *(column_base_pointer + scalar_i);
    if (value > differ) {
      *writer = scalar_i;
      writer++;
    }
  }

  return (writer - &index_vector[col_count_start_index]);
#endif

  #ifdef SCALAR
  size_t match_counter = 0;
  for (size_t in = 0 ; in < dataLength; in++) {
    if (*(column_base_pointer + in) > differ) {
      index_vector[match_counter] = in;
      match_counter++;
    }
  }
  return match_counter;
#endif
  #ifdef DEBUG_COUNT
    for (size_t in = 0 ; in < dataLength; in++) {
      if (in == 0) { std::cout << "count() Byte1 GREATER_THAN for p.val: " << p.val << std::endl; }
      // printf("ptr has value %lu, ", *(basePointer + in) + min);
      // printf("ptr has in pointer %p\n", basePointer + in);
    }
  #endif
}

template <>
uint32_t non_first_count<EncodingType::Byte1, CompareType::GREATER_THAN>(const Predicate &p,
                                                                         const uint32_t num_index,
                                                                         uint8_t *column_base_pointer,
                                                                         std::array<uint32_t, TUPLES_PER_DATABLOCK> &index_vector,
                                                                         const SMA *sma_ptr,
                                                                         const uint32_t index_offset,
                                                                         const uint32_t col_count_start_index,
                                                                         const uint32_t reader_offset) {
  const auto[min, max] = sma_ptr->getSMA_min_max();
  const int* ptr_32      = reinterpret_cast<int*>(column_base_pointer);  /// should be uint8_t*, but the _mm256_i32gather_epi32 only allows int*
  const uint8_t* ptr_8 = reinterpret_cast<uint8_t*>(column_base_pointer);

  const uint64_t value = p.val;
  const uint8_t differ = value - min;

  uint32_t* writer = &index_vector[col_count_start_index];

  /// IF Not 32 aligned, THEN process then using scalar
  uint32_t numSimdIterations = 0;
  uint8_t pre_scalar_counter = 0;
  if ((uintptr_t)reinterpret_cast<void*>(&index_vector[reader_offset]) % 32 == 0) {
      numSimdIterations = num_index >> (SIMD_WIDTH_BYTE1_LOG2_REDUCE);  /// Each time 8 uint32_t = 256 bit
  } else {
      while (pre_scalar_counter < num_index
             && (uintptr_t)reinterpret_cast<void*>(&index_vector[reader_offset + pre_scalar_counter]) % 32 != 0) {
          const uint32_t index = index_vector[reader_offset + pre_scalar_counter] - index_offset;
          const uint8_t value = *(ptr_8 + index);
          if (value > differ) {
              *writer = (index + index_offset);
              writer++;
          }
          pre_scalar_counter++;
      }
      numSimdIterations = (num_index - pre_scalar_counter) >> (SIMD_WIDTH_BYTE1_LOG2_REDUCE);  /// Each time 8 uint32_t = 256 bit
  }

  const __m256i* index_vector_ptr_mm256 = reinterpret_cast<__m256i*>(&index_vector[reader_offset + pre_scalar_counter]);

/// Case for non first scan
#ifdef SIMD_
  const __m256i comparisonValueVec = _mm256_set1_epi8(differ);
  assert((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0);  /// get datablock->data 32 bytes align for AVX

  /// Offset of index, in order to get the data in this page
  const __m256i index_offset_256 = _mm256_set1_epi32(index_offset);

  for (uint32_t i = 0; i != numSimdIterations; i++) {
      /// Load 8 Indexes from Index Vector
      __m256i indexes = *(index_vector_ptr_mm256 + i) /* reader */;  /// x/8d &indexes
      __m256i real_indexes = _mm256_sub_epi32(indexes, index_offset_256);

      /// Load 8 Int at the 8 indexes from Datablock
      __m256i gather = _mm256_i32gather_epi32(ptr_32, real_indexes, 1);     /// x/8u &gather

      __m256i selMask = Greater8u(gather, comparisonValueVec);  /// compare for Greater8u
      const uint32_t bitMask = _mm256_movemask_epi8(selMask);

      /// Pick only the first bit from every 4 bits
      uint8_t bitMask_u8 = _pext_u32(bitMask, 0x11111111);

      /// Lookup match positions and update positions vector
      auto& matchEntry = NON_FirstScan_MatchTable[bitMask_u8];
      auto dest = _mm256_permutevar8x32_epi32(indexes, matchEntry.reg256);  /// x/8d &dest
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(writer), dest);
      writer += __builtin_popcount(bitMask_u8);
  }

  /// Rest in scalar code, scan the rest
  const uint32_t scalars_start_index = (numSimdIterations << SIMD_WIDTH_BYTE1_LOG2_REDUCE) + reader_offset + pre_scalar_counter;
  for (uint32_t scalar_i = scalars_start_index; scalar_i < num_index + reader_offset; scalar_i++) {
    const uint32_t index = index_vector[scalar_i] - index_offset;
    const uint8_t value = *(ptr_8 + index);
    if (value > differ) {
        *writer = (index + index_offset);
        writer++;
    }
    }
    return (writer - &index_vector[col_count_start_index]);
#endif
}

template <>
uint32_t first_count<EncodingType::Byte1, CompareType::LESS_THAN>(const Predicate &p,
                                                            const uint32_t dataLength,
                                                            uint8_t *column_base_pointer,
                                                            std::array<uint32_t, TUPLES_PER_DATABLOCK> &index_vector,
                                                            const SMA* sma_ptr,
                                                            const uint32_t index_offset,
                                                            const uint32_t col_count_start_index) {
  const auto[min, max] = sma_ptr->getSMA_min_max();
  const uint64_t value = p.val;
  const uint8_t differ = value - min;

  uint32_t* writer = &index_vector[col_count_start_index];

  /// IF Not 32 aligned, THEN process then using scalar
  uint32_t numSimdIterations = 0;
  uint8_t pre_scalar_counter = 0;
  if ((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0) {
    numSimdIterations = (dataLength >> SIMD_WIDTH_BYTE1_LOG2);  /// equivalent as / 32
  } else {
    while (pre_scalar_counter < dataLength && (uintptr_t)reinterpret_cast<void*>(column_base_pointer + pre_scalar_counter) % 32 != 0) {
      const uint8_t value = *(column_base_pointer + pre_scalar_counter);
      if (value < differ) {
        *writer = (pre_scalar_counter + index_offset);
        writer++;
      }
      pre_scalar_counter++;
    }
    numSimdIterations = (dataLength - pre_scalar_counter) >> (SIMD_WIDTH_BYTE1_LOG2);  /// equivalent as / 32
  }

  /// Case for first scan => full loop all data needed
#ifdef SIMD_
  const __m256i comparisonValueVec = _mm256_set1_epi8(differ);
  assert((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0);  /// get datablock->data 32 bytes align for AXV

  __m256i* column_base_pointer_m256 = reinterpret_cast<__m256i*>(column_base_pointer + pre_scalar_counter);

  for (uint32_t i = 0; i != numSimdIterations; i++) {
    /// Load and compare 32 values
    __m256i attributeVec = _mm256_load_si256(column_base_pointer_m256 + i);  /// __m256i attributeVec=_mm256_load_si256(reinterpret_cast<__m256i*>(&column[scanPos]));
    __m256i selMask = Lesser8u(attributeVec, comparisonValueVec);  /// compare for less than
    uint32_t bitMask = _mm256_movemask_epi8(selMask);

    /// Lookup match positions and update positions vector
    const uint32_t scanPos = (i << SIMD_WIDTH_BYTE1_LOG2);  /// uint32_t scanPos = from + (i * simdWidth);
    simd_iterations<SIMD_ITERATION_BYTE1>(bitMask, scanPos + index_offset, writer);
  }

  /// Rest in scalar code, scan the rest
  const uint32_t scalars_start_index = (numSimdIterations << SIMD_WIDTH_BYTE1_LOG2) + pre_scalar_counter;
  for (uint32_t scalar_i = scalars_start_index; scalar_i < dataLength; scalar_i++) {
    const uint16_t value = *(column_base_pointer + scalar_i);
    if (value < differ) {
      *writer = scalar_i;
      writer++;
    }
  }

  return (writer - &index_vector[col_count_start_index]);
#endif


#ifdef SCALAR
  size_t match_counter = 0;
    for (size_t in = 0 ; in < dataLength; in++) {
      if (*(column_base_pointer + in) < differ) {
        index_vector[match_counter] = in;
        match_counter++;
      }
    }
    return match_counter;
#endif

  #ifdef DEBUG_COUNT
    for (size_t in = 0 ; in < dataLength; in++) {
      if (in == 0) { std::cout << "count() Byte1 LESS_THAN for val: " << value << std::endl; }
      // printf("ptr has value %lu, ", *(basePointer + in) + min);
      // printf("ptr has in pointer %p\n", basePointer + in);
    }
  #endif
}

template <>
uint32_t non_first_count<EncodingType::Byte1, CompareType::LESS_THAN>(const Predicate &p,
                                                                      const uint32_t num_index,
                                                                      uint8_t *column_base_pointer,
                                                                      std::array<uint32_t, TUPLES_PER_DATABLOCK> &index_vector,
                                                                      const SMA *sma_ptr,
                                                                      const uint32_t index_offset,
                                                                      const uint32_t col_count_start_index,
                                                                      const uint32_t reader_offset) {
  const auto[min, max] = sma_ptr->getSMA_min_max();
  const int* ptr_32      = reinterpret_cast<int*>(column_base_pointer);  /// should be uint8_t*, but the _mm256_i32gather_epi32 only allows int*
  const uint8_t* ptr_8 = reinterpret_cast<uint8_t*>(column_base_pointer);

  const uint64_t value = p.val;
  const uint8_t differ = value - min;

  uint32_t* writer = &index_vector[col_count_start_index];

  /// IF Not 32 aligned, THEN process then using scalar
  uint32_t numSimdIterations = 0;
  uint8_t pre_scalar_counter = 0;
  if ((uintptr_t)reinterpret_cast<void*>(&index_vector[reader_offset]) % 32 == 0) {
      numSimdIterations = num_index >> (SIMD_WIDTH_BYTE1_LOG2_REDUCE);  /// Each time 8 uint32_t = 256 bit
  } else {
      while (pre_scalar_counter < num_index
             && (uintptr_t)reinterpret_cast<void*>(&index_vector[reader_offset + pre_scalar_counter]) % 32 != 0) {
          const uint32_t index = index_vector[reader_offset + pre_scalar_counter] - index_offset;
          const uint8_t value = *(ptr_8 + index);
          if (value < differ) {
              *writer = (index + index_offset);
              writer++;
          }
          pre_scalar_counter++;
      }
      numSimdIterations = (num_index - pre_scalar_counter) >> (SIMD_WIDTH_BYTE1_LOG2_REDUCE);  /// Each time 8 uint32_t = 256 bit
  }

  const __m256i* index_vector_ptr_mm256 = reinterpret_cast<__m256i*>(&index_vector[reader_offset + pre_scalar_counter]);

  /// Case for non first scan
#ifdef SIMD_
  const __m256i comparisonValueVec = _mm256_set1_epi8(differ);
  assert((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0);  /// get datablock->data 32 bytes align for AVX

  /// Offset of index, in order to get the data in this page
  const __m256i index_offset_256 = _mm256_set1_epi32(index_offset);

  for (uint32_t i = 0; i != numSimdIterations; i++) {
    /// Load 8 Indexes from Index Vector
    __m256i indexes = *(index_vector_ptr_mm256 + i) /* reader */;  /// x/8d &indexes
    __m256i real_indexes = _mm256_sub_epi32(indexes, index_offset_256);

      /// Load 8 Int at the 8 indexes from Datablock
    __m256i gather = _mm256_i32gather_epi32(ptr_32, real_indexes, 1);     /// x/8u &gather

    __m256i selMask = Lesser8u(gather, comparisonValueVec);  /// compare for Greater8u
    const uint32_t bitMask = _mm256_movemask_epi8(selMask);

    /// Pick only the first bit from every 4 bits
    uint8_t bitMask_u8 = _pext_u32(bitMask, 0x11111111);

    /// Lookup match positions and update positions vector
    auto& matchEntry = NON_FirstScan_MatchTable[bitMask_u8];
    auto dest = _mm256_permutevar8x32_epi32(indexes, matchEntry.reg256);  /// x/8d &dest
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(writer), dest);
    writer += __builtin_popcount(bitMask_u8);
  }

  /// Rest in scalar code, scan the rest
  const uint32_t scalars_start_index = (numSimdIterations << SIMD_WIDTH_BYTE1_LOG2_REDUCE) + reader_offset + pre_scalar_counter;
  for (uint32_t scalar_i = scalars_start_index; scalar_i < num_index + reader_offset; scalar_i++) {
    const uint32_t index = index_vector[scalar_i] - index_offset;
    const uint8_t value = *(ptr_8 + index);
    if (value < differ) {
      *writer = (index + index_offset);
      writer++;
    }
  }
  return (writer - &index_vector[col_count_start_index]);
#endif
}

template <>
uint32_t first_count<EncodingType::Byte1, CompareType::BETWEEN>(const Predicate &p,
                                                          const uint32_t dataLength,
                                                          uint8_t *column_base_pointer,
                                                          std::array<uint32_t, TUPLES_PER_DATABLOCK> &index_vector,
                                                          const SMA* sma_ptr,
                                                          const uint32_t index_offset,
                                                          const uint32_t col_count_start_index) {
  const auto[min, max] = sma_ptr->getSMA_min_max();
  const uint64_t left_value = p.val;
  const uint64_t right_value = p.right_value;
  const uint8_t differ_left = (left_value < min)? 0 : (left_value - min);
  const uint8_t differ_right = p.right_value - min;

  uint32_t* writer = &index_vector[col_count_start_index];

  /// IF Not 32 aligned, THEN process then using scalar
  uint32_t numSimdIterations = 0;
  uint8_t pre_scalar_counter = 0;
  if ((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0) {
    numSimdIterations = (dataLength >> SIMD_WIDTH_BYTE1_LOG2);  /// equivalent as / 32
  } else {
    while (pre_scalar_counter < dataLength && (uintptr_t)reinterpret_cast<void*>(column_base_pointer + pre_scalar_counter) % 32 != 0) {
      const uint8_t value = *(column_base_pointer + pre_scalar_counter);
      if (differ_left <= value && value <= differ_right) {
        *writer = (pre_scalar_counter + index_offset);
        writer++;
      }
      pre_scalar_counter++;
    }
    numSimdIterations = (dataLength - pre_scalar_counter) >> (SIMD_WIDTH_BYTE1_LOG2);  /// equivalent as / 32
  }

  #ifdef SIMD_
  const __m256i comparisonValueVec_left = _mm256_set1_epi8(differ_left);
  const __m256i comparisonValueVec_right = _mm256_set1_epi8(differ_right);
  assert((uintptr_t)reinterpret_cast<void*>(column_base_pointer) % 32 == 0);  /// get datablock->data 32 bytes align for AXV

  __m256i* column_base_pointer_m256 = reinterpret_cast<__m256i*>(column_base_pointer + pre_scalar_counter);

  for (uint32_t i = 0; i != numSimdIterations; i++) {
    /// Load and compare 32 values
    __m256i attributeVec = _mm256_load_si256(column_base_pointer_m256 + i);  /// __m256i attributeVec=_mm256_load_si256(reinterpret_cast<__m256i*>(&column[scanPos]));
    __m256i selMask_left = GreaterOrEqual8u(attributeVec, comparisonValueVec_left);  /// compare for great than
    uint32_t bitMask_left = _mm256_movemask_epi8(selMask_left);
    __m256i selMask_right = LesserOrEqual8u(attributeVec, comparisonValueVec_right);  /// compare for great than
    uint32_t bitMask = _mm256_movemask_epi8(selMask_right) & bitMask_left;

    /// Lookup match positions and update positions vector
    const uint32_t scanPos = (i << SIMD_WIDTH_BYTE1_LOG2);  /// uint32_t scanPos = from + (i * simdWidth);
    simd_iterations<SIMD_ITERATION_BYTE1>(bitMask, scanPos + index_offset, writer);
  }

  /// Rest in scalar code, scan the rest
  const uint32_t scalars_start_index = (numSimdIterations << SIMD_WIDTH_BYTE1_LOG2) + pre_scalar_counter;
  for (uint32_t scalar_i = scalars_start_index; scalar_i < dataLength; scalar_i++) {
    const uint16_t value = *(column_base_pointer + scalar_i);
    if (differ_left <= value  && value <= differ_right) {
      *writer = scalar_i;
      writer++;
    }
  }

  return (writer - &index_vector[col_count_start_index]);
  #endif

  #ifdef SCALAR
  size_t match_counter = 0;
    for (size_t in = 0 ; in < dataLength; in++) {
      uint8_t value = *(column_base_pointer + in);
      if (differ_left <= value  && value <= differ_right) {
        index_vector[match_counter] = in;
        match_counter++;
      }
    }
    return match_counter;
  #endif

  #ifdef DEBUG_COUNT
    for (size_t in = 0 ; in < dataLength; in++) {
      if (in == 0) { std::cout << "count() Byte1 BETWEEN for val: " << left_value  << "  " << right_value
          << " in min:" << min << "  max: " << max << std::endl; }
      // printf("ptr has value %lu, ", *(basePointer + in) + min);
      // printf("ptr has in pointer %p\n", basePointer + in);
    }
  #endif
}

template <>
uint32_t non_first_count<EncodingType::Byte1, CompareType::BETWEEN>(const Predicate &p,
                                                                    const uint32_t num_index,
                                                                    uint8_t *column_base_pointer,
                                                                    std::array<uint32_t, TUPLES_PER_DATABLOCK> &index_vector,
                                                                    const SMA *sma_ptr,
                                                                    const uint32_t index_offset,
                                                                    const uint32_t col_count_start_index,
                                                                    const uint32_t reader_offset) {
  const auto [min, max] = sma_ptr->getSMA_min_max();
  const int *ptr_32 = reinterpret_cast<int *>(column_base_pointer); /// should be uint8_t*, but the _mm256_i32gather_epi32 only allows int*

  const uint64_t left_value = p.val;
  const uint64_t right_value = p.right_value;
  const uint8_t differ_left = (left_value < min) ? 0 : (left_value - min);
  const uint8_t differ_right = right_value - min;

  uint32_t* writer = &index_vector[col_count_start_index];

  /// IF Not 32 aligned, THEN process then using scalar
  uint32_t numSimdIterations = 0;
  uint8_t pre_scalar_counter = 0;
  if ((uintptr_t)reinterpret_cast<void*>(&index_vector[reader_offset]) % 32 == 0) {
      numSimdIterations = num_index >> (SIMD_WIDTH_BYTE1_LOG2_REDUCE);  /// Each time 8 uint32_t = 256 bit
  } else {
      while (pre_scalar_counter < num_index
             && (uintptr_t)reinterpret_cast<void*>(&index_vector[reader_offset + pre_scalar_counter]) % 32 != 0) {
          const uint32_t index = index_vector[reader_offset + pre_scalar_counter] - index_offset;
          const uint8_t value = *(column_base_pointer + index);
          if (differ_left <= value && value <= differ_right) {
              *writer = (index + index_offset);
              writer++;
          }
          pre_scalar_counter++;
      }
      numSimdIterations = (num_index - pre_scalar_counter) >> (SIMD_WIDTH_BYTE1_LOG2_REDUCE);  /// Each time 8 uint32_t = 256 bit
  }

  const __m256i* index_vector_ptr_mm256 = reinterpret_cast<__m256i*>(&index_vector[reader_offset + pre_scalar_counter]);

  /// Case for non first scan
#ifdef SIMD_
  const __m256i comparisonValueVec_left = _mm256_set1_epi16(differ_left);
  const __m256i comparisonValueVec_right = _mm256_set1_epi16(differ_right);
  assert((uintptr_t) reinterpret_cast<void *>(column_base_pointer) % 32 == 0); /// get datablock->data 32 bytes align for AVX

  /// Offset of index, in order to get the data in this page
  const __m256i index_offset_256 = _mm256_set1_epi32(index_offset);

  for (uint32_t i = 0; i != numSimdIterations; i++) {
    /// Load 8 Indexes from Index Vector
    __m256i indexes = *(index_vector_ptr_mm256 + i) /* reader */; /// x/8d &indexes
    __m256i real_indexes = _mm256_sub_epi32(indexes, index_offset_256);

    /// Load 8 Int at the 8 indexes from Datablock
    __m256i gather = _mm256_i32gather_epi32(ptr_32, real_indexes, 1); /// x/8u &gather

    /// compare 8 values
    __m256i selMask_left = GreaterOrEqual8u(gather, comparisonValueVec_left); /// compare for great than
    uint32_t bitMask_left = _mm256_movemask_epi8(selMask_left);

    __m256i selMask_right = LesserOrEqual8u(gather, comparisonValueVec_right); /// compare for less than
    uint32_t bitMask = _mm256_movemask_epi8(selMask_right) & bitMask_left;

    /// Pick only the first bit from every 4 bits
    uint8_t bitMask_u8 = _pext_u32(bitMask, 0x11111111);

    /// Lookup match positions and update positions vector
    auto& matchEntry = NON_FirstScan_MatchTable[bitMask_u8];
    auto dest = _mm256_permutevar8x32_epi32(indexes, matchEntry.reg256);  /// x/8d &dest
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(writer), dest);
    writer += __builtin_popcount(bitMask_u8);
  }

  /// Rest in scalar code, scan the rest
  const uint32_t scalars_start_index = (numSimdIterations << SIMD_WIDTH_BYTE1_LOG2_REDUCE) + reader_offset + pre_scalar_counter;
  for (uint32_t scalar_i = scalars_start_index; scalar_i < num_index + reader_offset; scalar_i++) {
    const uint32_t index = index_vector[scalar_i] - index_offset;
    const uint8_t value = *(column_base_pointer + index);
    if (differ_left <= value && value <= differ_right) {
      *writer = (index + index_offset);
      writer++;
    }
  }
  return (writer - &index_vector[col_count_start_index]);
#endif
}
// ---------------------------------------------------------------------------------------------------
}  // namespace util
// ---------------------------------------------------------------------------------------------------
}  // namespace imlab
// ---------------------------------------------------------------------------------------------------
#endif  // INCLUDE_IMLAB_UTIL_COUNT_BYTE1_H_
// ---------------------------------------------------------------------------------------------------
