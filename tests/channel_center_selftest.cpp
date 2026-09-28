/* Headless guard for the primary -> RF center channel map (src/ChannelCenter.h): the whole 5 GHz table for 20, 40
 * and 80 MHz, the 40 MHz primary offset per 5 GHz channel, and 2.4 GHz 40 MHz with an explicit offset. 802.11 rule:
 * 5 GHz 40 MHz channels are fixed pairs; the lower channel is HT40+ (primary lower), the upper HT40- (primary
 * upper). Pure math; no hardware. */
#include "ChannelCenter.h"

#include <cstdio>
#include <initializer_list>

using namespace devourer;

static int g_fail = 0;

static void check(const char *what, int ch, long got, long want) {
  if (got != want) {
    std::printf("FAIL %s ch%d: got %ld want %ld\n", what, ch, got, want);
    ++g_fail;
  }
}

struct Pair { int lower, upper, center40; };
struct Block { int first, last, center80; };

static const Pair kPairs[] = {
    {36, 40, 38},    {44, 48, 46},    {52, 56, 54},    {60, 64, 62},
    {100, 104, 102}, {108, 112, 110}, {116, 120, 118}, {124, 128, 126},
    {132, 136, 134}, {140, 144, 142}, {149, 153, 151}, {157, 161, 159},
    {165, 169, 167}, {173, 177, 175},
};
static const Block kBlocks[] = {
    {36, 48, 42}, {52, 64, 58}, {100, 112, 106}, {116, 128, 122},
    {132, 144, 138}, {149, 161, 155}, {165, 177, 171},
};

int main() {
  for (const Pair &p : kPairs) {
    /* 20 MHz: center == primary */
    check("20", p.lower, center_channel(p.lower, CHANNEL_WIDTH_20, kPrimeDontCare), p.lower);
    check("20", p.upper, center_channel(p.upper, CHANNEL_WIDTH_20, kPrimeDontCare), p.upper);
    /* 40 MHz: the pair's center, whatever offset the caller passes (the pair fixes it in 5 GHz) */
    for (uint8_t off : {kPrimeDontCare, kPrimeLower, kPrimeUpper}) {
      check("40", p.lower, center_channel(p.lower, CHANNEL_WIDTH_40, off), p.center40);
      check("40", p.upper, center_channel(p.upper, CHANNEL_WIDTH_40, off), p.center40);
    }
    /* the primary offset the pair implies */
    check("offset", p.lower, ht40_offset(p.lower), kPrimeLower);
    check("offset", p.upper, ht40_offset(p.upper), kPrimeUpper);
  }
  for (const Block &b : kBlocks)
    for (int ch = b.first; ch <= b.last; ch += 4)
      check("80", ch, center_channel(ch, CHANNEL_WIDTH_80, kPrimeDontCare), b.center80);

  /* 2.4 GHz 40 MHz: HT40+ (primary lower) center = ch + 2, HT40- (primary upper) center = ch - 2 */
  for (int ch = 1; ch <= 9; ++ch)
    check("2.4 HT40+", ch, center_channel(ch, CHANNEL_WIDTH_40, kPrimeLower), ch + 2);
  for (int ch = 5; ch <= 13; ++ch)
    check("2.4 HT40-", ch, center_channel(ch, CHANNEL_WIDTH_40, kPrimeUpper), ch - 2);
  /* the legacy default for an unset offset stays: 4 -> 6, 8 -> 6 */
  check("2.4 dont-care", 4, center_channel(4, CHANNEL_WIDTH_40, kPrimeDontCare), 6);
  check("2.4 dont-care", 8, center_channel(8, CHANNEL_WIDTH_40, kPrimeDontCare), 6);
  /* 2.4 GHz has no fixed pairs: no offset can be derived from the channel */
  check("offset 2.4", 6, ht40_offset(6), kPrimeDontCare);

  /* TX DATA_SC: a 20 MHz frame in a 40 MHz channel goes on the primary; anything else is unchanged (0) */
  check("sc 20-in-40 lower", 0, tx_data_sc(CHANNEL_WIDTH_40, CHANNEL_WIDTH_20, kPrimeLower), 2);
  check("sc 20-in-40 upper", 0, tx_data_sc(CHANNEL_WIDTH_40, CHANNEL_WIDTH_20, kPrimeUpper), 1);
  check("sc 20-in-40 no primary", 0, tx_data_sc(CHANNEL_WIDTH_40, CHANNEL_WIDTH_20, kPrimeDontCare), 0);
  check("sc 40-in-40", 0, tx_data_sc(CHANNEL_WIDTH_40, CHANNEL_WIDTH_40, kPrimeLower), 0);
  check("sc 20-in-20", 0, tx_data_sc(CHANNEL_WIDTH_20, CHANNEL_WIDTH_20, kPrimeDontCare), 0);

  /* O82b (2026-09-28): the air unit runs `iw set channel 157 80MHz` + wfb_tx -B 40, so its 40 MHz frames sit on
   * 157/161 (center 159) and its RX primary is 157. The Quest at 40 MHz on 157 must tune center 159 with primary
   * lower (HT40+), and send its 20 MHz uplink on the lower sub-channel = 157. */
  {
    const uint8_t off = ht40_offset(157);
    check("O82b offset", 157, off, kPrimeLower);
    check("O82b center", 157, center_channel(157, CHANNEL_WIDTH_40, off), 159);
    check("O82b uplink sc", 157, tx_data_sc(CHANNEL_WIDTH_40, CHANNEL_WIDTH_20, off), 2);
    check("O82b air 80 center", 157, center_channel(157, CHANNEL_WIDTH_80, kPrimeDontCare), 155);
  }

  /* an unsupported width is reported, not guessed */
  check("160", 50, center_channel(50, CHANNEL_WIDTH_160, kPrimeDontCare), -1);

  if (g_fail) {
    std::printf("channel_center: %d failure(s)\n", g_fail);
    return 1;
  }
  std::printf("channel_center: all checks passed\n");
  return 0;
}
