#ifndef CHANNEL_CENTER_H
#define CHANNEL_CENTER_H

#include <cstdint>

#include "SelectedChannel.h"

/* Primary channel -> RF center channel for a bandwidth, and the 40 MHz primary offset. Generation-neutral,
 * header-only, pure (headless guard: tests/channel_center_selftest.cpp).
 *
 * Offsets use the vendor HAL values (HAL_PRIME_CHNL_OFFSET_* in jaguar1/RadioManagementModule.h, checked there with
 * static_asserts): the offset names where the PRIMARY 20 MHz sits inside the 40 MHz channel. */

namespace devourer {

constexpr uint8_t kPrimeDontCare = 0;
constexpr uint8_t kPrimeLower = 1; /* primary is the lower 20 MHz: HT40+ (secondary above) */
constexpr uint8_t kPrimeUpper = 2; /* primary is the upper 20 MHz: HT40- (secondary below) */

/* 80 MHz center for a 5 GHz channel inside a VHT80 block; the channel itself if it is in none. */
static inline int center_80(int ch) {
  if (ch >= 36 && ch <= 48) return 42;
  if (ch >= 52 && ch <= 64) return 58;
  if (ch >= 100 && ch <= 112) return 106;
  if (ch >= 116 && ch <= 128) return 122;
  if (ch >= 132 && ch <= 144) return 138;
  if (ch >= 149 && ch <= 161) return 155;
  if (ch >= 165 && ch <= 177) return 171;
  if (ch <= 14) return 7;
  return ch;
}

/* 5 GHz 40 MHz channels are fixed pairs (lower = HT40+, upper = HT40-); this is the lower channel of each. */
static inline int ht40_pair_lower(int ch) {
  static const int kLower[] = {36, 44, 52, 60, 100, 108, 116, 124, 132, 140, 149, 157, 165, 173};
  for (int lower : kLower)
    if (ch == lower || ch == lower + 4) return lower;
  return 0;
}

/* The 40 MHz primary offset a channel implies: fixed by the pair in 5 GHz; none in 2.4 GHz (no fixed pairs). */
static inline uint8_t ht40_offset(int ch) {
  const int lower = ht40_pair_lower(ch);
  if (lower == 0) return kPrimeDontCare;
  return ch == lower ? kPrimeLower : kPrimeUpper;
}

/* 40 MHz center. 5 GHz: the pair's center, whatever offset is passed (the pair fixes it; an earlier map sent
 * 153 -> 155 and 161 -> 163, the 80 MHz center and nothing). 2.4 GHz: primary +- 2 by the offset; with no offset, the
 * lower half of the band is taken as HT40+ and the upper as HT40- (the earlier map's 4 -> 6 and 8 -> 6). */
static inline int center_40(int ch, uint8_t offset) {
  if (ch > 14) {
    const int lower = ht40_pair_lower(ch);
    return lower ? lower + 2 : ch;
  }
  if (offset == kPrimeLower) return ch + 2;
  if (offset == kPrimeUpper) return ch - 2;
  return ch <= 7 ? ch + 2 : ch - 2;
}

/* TX descriptor DATA_SC for one frame: which sub-channel a narrower frame goes out on. Only a 20 MHz frame in a
 * 40 MHz channel is decided here: on the primary, the same values phy_GetSecondaryChnl_8812 writes to REG_DATA_SC at
 * 40 MHz (VHT_DATA_SC_20_UPPER_OF_80MHZ = 1 for primary upper, _LOWER_ = 2 for primary lower). Everything else stays
 * 0, as before (the full channel, or no primary known). */
static inline uint8_t tx_data_sc(ChannelWidth_t channel_bw, ChannelWidth_t frame_bw, uint8_t prime40) {
  if (channel_bw != CHANNEL_WIDTH_40 || frame_bw != CHANNEL_WIDTH_20) return 0;
  if (prime40 == kPrimeUpper) return 1;
  if (prime40 == kPrimeLower) return 2;
  return 0;
}

/* The RF center channel for a primary channel and width; -1 for a width this does not handle. */
static inline int center_channel(int ch, ChannelWidth_t bw, uint8_t offset) {
  switch (bw) {
    case CHANNEL_WIDTH_80: return center_80(ch);
    case CHANNEL_WIDTH_40: return center_40(ch, offset);
    case CHANNEL_WIDTH_20:
    case CHANNEL_WIDTH_5:
    case CHANNEL_WIDTH_10: return ch; /* narrowband re-clocks the 20 MHz baseband; RF stays a 20 MHz tune */
    default: return -1;
  }
}

} /* namespace devourer */

#endif /* CHANNEL_CENTER_H */
