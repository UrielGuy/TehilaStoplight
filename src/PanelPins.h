#pragma once
// ESP32-S3-DevKitC-1 in the Seengreat / WatangTech "RGB Matrix Adapter Board (E)"
// rev 2.x, S3 socket. Verified on the bench 2026-09-19 (the maker's table has
// the three colour pins rotated relative to what the panel shows).
namespace PanelPins {
constexpr int r1_pin  = 8;
constexpr int g1_pin  = 17;
constexpr int b1_pin  = 18;
constexpr int r2_pin  = 1;
constexpr int g2_pin  = 15;
constexpr int b2_pin  = 16;
constexpr int a_pin   = 7;
constexpr int b_pin   = 48;
constexpr int c_pin   = 6;
constexpr int d_pin   = 47;
constexpr int e_pin   = 2;
constexpr int lat_pin = 21;
constexpr int oe_pin  = 4;
constexpr int clk_pin = 5;
}  // namespace PanelPins
