#pragma once
#include <cstdint>
#include "utils.h"

constexpr int CHAR_W = 51;
constexpr int CHAR_H = 45;

constexpr uint16_t _ = Color::TRANSPARENT;
constexpr uint16_t A = rgb565(247, 169, 195);
constexpr uint16_t B = rgb565(255, 255, 255);
constexpr uint16_t C = rgb565(41, 20, 40);
constexpr uint16_t D = rgb565(254, 239, 237);
constexpr uint16_t E = rgb565(89, 69, 84);
constexpr uint16_t F = rgb565(216, 133, 163);
constexpr uint16_t G = rgb565(132, 110, 165);
constexpr uint16_t H = rgb565(192, 175, 229);
constexpr uint16_t I = rgb565(64, 37, 58);
constexpr uint16_t J = rgb565(233, 154, 182);
constexpr uint16_t K = rgb565(110, 71, 92);
constexpr uint16_t L = rgb565(69, 49, 81);
constexpr uint16_t M = rgb565(113, 91, 140);

const uint16_t PROGMEM sprite_stand[CHAR_W * CHAR_H] = {
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, C, C, _, _, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, C, C, C, C, C, C, C, C, C, _, _, C, C, A, A, A, C, A, A, C, C, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, A, A, A, A, A, A, C, C, C, C, A, A, F, C, C, J, A, A, A, C, A, A, C, C, A, C, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, F, A, A, A, A, A, C, C, A, A, A, A, C, C, A, C, A, A, A, A, C, A, A, C, A, A, C, A, I, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, C, C, A, A, A, C, C, A, A, A, C, F, F, C, A, A, A, A, A, C, C, A, A, A, A, C, A, C, A, C, C, J, C, A, K, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, C, A, A, A, F, C, F, F, C, A, C, F, F, F, F, C, A, A, A, A, A, C, C, A, A, A, C, A, C, I, J, C, A, J, C, J, C, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, C, A, A, A, F, C, F, F, C, A, A, A, C, A, A, F, F, C, A, A, A, A, A, F, C, A, C, A, C, A, C, I, C, C, A, J, I, C, C, C, _, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, A, C, F, A, C, A, A, A, A, A, C, A, A, A, F, C, A, A, A, A, A, C, A, C, A, C, A, C, G, A, C, A, C, C, C, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, F, C, A, A, A, A, A, C, A, C, C, A, A, C, C, A, A, C, A, C, B, B, B, C, 
    _, _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, F, C, A, A, A, A, A, C, A, F, C, C, A, A, A, C, C, C, B, B, B, C, _, 
    _, _, _, _, _, _, _, C, A, A, A, C, A, A, A, A, A, A, A, A, A, A, A, A, C, A, A, A, A, C, A, A, A, A, A, C, A, A, A, F, C, C, C, A, A, C, C, C, C, _, _, 
    _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, C, A, A, A, F, C, A, A, A, A, A, C, A, A, A, A, A, A, A, C, G, L, D, B, C, _, 
    _, _, _, _, _, _, C, A, A, A, C, A, A, A, C, A, A, A, A, A, A, A, A, A, A, C, I, A, A, A, C, A, A, A, A, A, C, F, A, A, A, C, C, C, G, G, I, D, B, B, C, 
    _, _, _, _, _, C, A, A, A, F, C, A, A, A, C, A, A, A, A, A, A, A, A, C, A, C, I, A, A, A, F, C, A, A, A, A, A, C, C, C, C, C, G, C, C, C, C, C, B, C, _, 
    _, _, _, _, C, F, A, A, A, C, F, A, A, A, C, A, A, A, A, A, A, A, A, C, A, C, E, A, A, A, A, C, A, A, A, A, A, C, G, G, G, C, C, C, C, G, C, B, C, _, _, 
    _, _, _, _, C, A, A, A, A, C, F, A, A, C, E, A, A, A, A, A, A, A, A, C, A, E, D, E, A, A, A, C, A, A, A, A, A, C, C, G, C, C, G, C, M, G, G, C, B, C, _, 
    _, _, _, C, A, A, A, A, A, C, A, A, A, E, D, E, A, A, C, A, A, A, A, C, A, E, D, D, E, A, A, C, A, A, A, A, A, F, C, C, F, C, G, G, C, C, C, B, B, C, _, 
    _, _, C, A, A, A, A, A, A, C, A, A, K, D, D, E, A, A, C, A, A, A, A, C, A, E, D, D, D, E, A, F, C, A, A, A, A, A, C, F, F, C, G, C, C, B, B, B, B, C, _, 
    _, C, A, A, A, A, A, A, A, C, A, C, C, E, E, C, C, A, C, A, A, A, A, C, A, E, D, D, D, D, E, C, C, A, A, A, A, A, C, F, F, C, C, I, B, B, B, B, C, _, _, 
    C, C, C, C, C, A, A, A, A, C, A, C, C, G, B, G, C, C, A, C, A, A, C, C, C, D, E, E, E, E, D, D, C, A, A, A, A, A, C, F, A, F, C, B, B, B, B, C, C, C, _, 
    _, _, _, _, C, A, A, A, A, C, C, C, G, G, G, G, G, C, E, E, D, A, E, E, D, C, G, B, G, G, C, D, C, A, A, A, A, A, C, F, A, C, B, C, B, B, B, C, B, C, _, 
    _, _, _, _, C, A, A, A, A, C, E, C, G, G, G, G, G, E, D, D, D, E, D, D, E, G, G, G, G, G, G, C, C, A, A, A, A, A, C, F, A, C, B, C, B, B, C, C, B, C, _, 
    _, _, _, _, C, A, A, A, C, E, D, E, H, H, H, H, H, E, D, D, D, D, D, D, E, G, G, G, G, G, G, C, C, A, A, A, A, A, C, A, A, C, B, C, B, B, C, B, B, C, _, 
    _, _, _, _, C, A, A, C, E, D, D, D, E, H, H, H, E, D, D, D, D, D, D, D, E, H, H, H, H, H, H, C, C, A, A, A, A, A, C, A, C, B, B, B, C, B, C, B, B, B, C, 
    _, _, _, _, C, A, A, E, D, D, D, D, D, E, E, E, D, D, D, D, D, D, D, D, D, E, H, H, H, H, C, C, A, A, A, A, A, A, C, C, B, B, B, B, B, C, B, B, B, B, C, 
    _, _, _, _, _, C, A, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, E, E, E, B, C, A, A, A, A, A, F, C, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, C, A, E, D, D, D, D, D, D, D, D, D, E, D, D, E, D, D, E, D, D, D, D, D, D, E, C, A, A, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, C, A, A, E, D, D, D, D, D, D, D, D, D, E, E, D, E, E, D, D, D, D, D, D, D, E, A, A, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, _, C, A, A, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, A, C, C, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, _, C, A, A, A, E, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, E, C, C, B, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, C, A, A, A, A, E, E, E, D, D, D, D, D, D, D, D, D, D, E, E, B, B, B, B, B, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, A, C, C, E, E, E, E, E, E, E, E, E, E, C, B, B, B, B, B, B, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, _, C, A, C, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, _, _, I, C, _, C, C, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, C, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, C, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, C, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, C, C, C, C, B, B, B, B, B, B, B, B, B, C, C, C, _, _, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, C, _, _, _, C, B, B, B, B, B, B, C, C, _, _, _, _, _, _, C, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, C, _, _, _, C, B, B, B, B, B, B, C, _, _, _, _, _, _, _, _, C, B, B, B, C, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, _, _, _, _, _, C, B, B, B, B, B, C, _, _, _, _, _, _, _, _, _, C, C, C, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, C, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, 
};

const uint16_t PROGMEM sprite_walk_a[CHAR_W * CHAR_H] = {
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, C, C, _, _, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, C, C, C, C, C, C, C, C, C, _, _, C, C, A, A, A, C, A, A, C, C, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, A, A, A, A, A, A, C, C, C, C, A, A, F, C, C, J, A, A, A, C, A, A, C, C, A, C, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, F, A, A, A, A, A, C, C, A, A, A, A, C, C, A, C, A, A, A, A, C, A, A, C, A, A, C, A, I, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, C, C, A, A, A, C, C, A, A, A, C, F, F, C, A, A, A, A, A, C, C, A, A, A, A, C, A, C, A, C, C, J, C, A, K, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, C, A, A, A, F, C, F, F, C, A, C, F, F, F, F, C, A, A, A, A, A, C, C, A, A, A, C, A, C, I, J, C, A, J, C, J, C, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, C, A, A, A, F, C, F, F, C, A, A, A, C, A, A, F, F, C, A, A, A, A, A, F, C, A, C, A, C, A, C, I, C, C, A, J, I, C, C, C, _, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, A, C, F, A, C, A, A, A, A, A, C, A, A, A, F, C, A, A, A, A, A, C, A, C, A, C, A, C, G, A, C, A, C, C, C, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, F, C, A, A, A, A, A, C, A, C, C, A, A, C, C, A, A, C, A, C, B, B, B, C, 
    _, _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, F, C, A, A, A, A, A, C, A, F, C, C, A, A, A, C, C, C, B, B, B, C, _, 
    _, _, _, _, _, _, _, C, A, A, A, C, A, A, A, A, A, A, A, A, A, A, A, A, C, A, A, A, A, C, A, A, A, A, A, C, A, A, A, F, C, C, C, A, A, C, C, C, C, _, _, 
    _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, C, A, A, A, F, C, A, A, A, A, A, C, A, A, A, A, A, A, A, C, G, L, D, B, C, _, 
    _, _, _, _, _, _, C, A, A, A, C, A, A, A, C, A, A, A, A, A, A, A, A, A, A, C, I, A, A, A, C, A, A, A, A, A, C, F, A, A, A, C, C, C, G, G, I, D, B, B, C, 
    _, _, _, _, _, C, A, A, A, F, C, A, A, A, C, A, A, A, A, A, A, A, A, C, A, C, I, A, A, A, F, C, A, A, A, A, A, C, C, C, C, C, G, C, C, C, C, C, B, C, _, 
    _, _, _, _, C, F, A, A, A, C, F, A, A, A, C, A, A, A, A, A, A, A, A, C, A, C, E, A, A, A, A, C, A, A, A, A, A, C, G, G, G, C, C, C, C, G, C, B, C, _, _, 
    _, _, _, _, C, A, A, A, A, C, F, A, A, C, E, A, A, A, A, A, A, A, A, C, A, E, D, E, A, A, A, C, A, A, A, A, A, C, C, G, C, C, G, C, M, G, G, C, B, C, _, 
    _, _, _, C, A, A, A, A, A, C, A, A, A, E, D, E, A, A, C, A, A, A, A, C, A, E, D, D, E, A, A, C, A, A, A, A, A, F, C, C, F, C, G, G, C, C, C, B, B, C, _, 
    _, _, C, A, A, A, A, A, A, C, A, A, K, D, D, E, A, A, C, A, A, A, A, C, A, E, D, D, D, E, A, F, C, A, A, A, A, A, C, F, F, C, G, C, C, B, B, B, B, C, _, 
    _, C, A, A, A, A, A, A, A, C, A, C, C, E, E, C, C, A, C, A, A, A, A, C, A, E, D, D, D, D, E, C, C, A, A, A, A, A, C, F, F, C, C, I, B, B, B, B, C, _, _, 
    C, C, C, C, C, A, A, A, A, C, A, C, C, G, B, G, C, C, A, C, A, A, C, C, C, D, E, E, E, E, D, D, C, A, A, A, A, A, C, F, A, F, C, B, B, B, B, C, C, C, _, 
    _, _, _, _, C, A, A, A, A, C, C, C, G, G, G, G, G, C, E, E, D, A, E, E, D, C, G, B, G, G, C, D, C, A, A, A, A, A, C, F, A, C, B, C, B, B, B, C, B, C, _, 
    _, _, _, _, C, A, A, A, A, C, E, C, G, G, G, G, G, E, D, D, D, E, D, D, E, G, G, G, G, G, G, C, C, A, A, A, A, A, C, F, A, C, B, C, B, B, C, C, B, C, _, 
    _, _, _, _, C, A, A, A, C, E, D, E, H, H, H, H, H, E, D, D, D, D, D, D, E, G, G, G, G, G, G, C, C, A, A, A, A, A, C, A, A, C, B, C, B, B, C, B, B, C, _, 
    _, _, _, _, C, A, A, C, E, D, D, D, E, H, H, H, E, D, D, D, D, D, D, D, E, H, H, H, H, H, H, C, C, A, A, A, A, A, C, A, C, B, B, B, C, B, C, B, B, B, C, 
    _, _, _, _, C, A, A, E, D, D, D, D, D, E, E, E, D, D, D, D, D, D, D, D, D, E, H, H, H, H, C, C, A, A, A, A, A, A, C, C, B, B, B, B, B, C, B, B, B, B, C, 
    _, _, _, _, _, C, A, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, E, E, E, B, C, A, A, A, A, A, F, C, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, C, A, E, D, D, D, D, D, D, D, D, D, E, D, D, E, D, D, E, D, D, D, D, D, D, E, C, A, A, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, C, A, A, E, D, D, D, D, D, D, D, D, D, E, E, D, E, E, D, D, D, D, D, D, D, E, A, A, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, _, C, A, A, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, A, C, C, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, _, C, A, A, A, E, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, E, C, C, B, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, C, A, A, A, A, E, E, E, D, D, D, D, D, D, D, D, D, D, E, E, B, B, B, B, B, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, A, C, C, E, E, E, E, E, E, E, E, E, E, C, B, B, B, B, B, B, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, _, C, A, C, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, _, _, I, C, _, C, C, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, C, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, C, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, C, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, C, C, C, C, B, B, B, B, B, B, B, B, B, C, C, C, _, _, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, C, _, _, _, C, B, B, B, B, B, B, C, C, _, _, _, _, _, _, C, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, B, B, B, B, C, _, C, B, B, B, B, B, B, C, _, _, _, _, _, _, _, _, _, C, C, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, _, _, _, C, B, B, B, B, B, C, _, _, _, _, _, _, _, _, _, _, _, C, C, C, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, C, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, 
};

const uint16_t PROGMEM sprite_walk_b[CHAR_W * CHAR_H] = {
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, C, C, _, _, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, C, C, C, C, C, C, C, C, C, _, _, C, C, A, A, A, C, A, A, C, C, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, A, A, A, A, A, A, C, C, C, C, A, A, F, C, C, J, A, A, A, C, A, A, C, C, A, C, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, F, A, A, A, A, A, C, C, A, A, A, A, C, C, A, C, A, A, A, A, C, A, A, C, A, A, C, A, I, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, C, C, A, A, A, C, C, A, A, A, C, F, F, C, A, A, A, A, A, C, C, A, A, A, A, C, A, C, A, C, C, J, C, A, K, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, C, A, A, A, F, C, F, F, C, A, C, F, F, F, F, C, A, A, A, A, A, C, C, A, A, A, C, A, C, I, J, C, A, J, C, J, C, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, C, A, A, A, F, C, F, F, C, A, A, A, C, A, A, F, F, C, A, A, A, A, A, F, C, A, C, A, C, A, C, I, C, C, A, J, I, C, C, C, _, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, A, C, F, A, C, A, A, A, A, A, C, A, A, A, F, C, A, A, A, A, A, C, A, C, A, C, A, C, G, A, C, A, C, C, C, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, F, C, A, A, A, A, A, C, A, C, C, A, A, C, C, A, A, C, A, C, B, B, B, C, 
    _, _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, F, C, A, A, A, A, A, C, A, F, C, C, A, A, A, C, C, C, B, B, B, C, _, 
    _, _, _, _, _, _, _, C, A, A, A, C, A, A, A, A, A, A, A, A, A, A, A, A, C, A, A, A, A, C, A, A, A, A, A, C, A, A, A, F, C, C, C, A, A, C, C, C, C, _, _, 
    _, _, _, _, _, _, C, A, A, A, C, F, A, A, A, A, A, A, A, A, A, A, A, A, A, C, A, A, A, F, C, A, A, A, A, A, C, A, A, A, A, A, A, A, C, G, L, D, B, C, _, 
    _, _, _, _, _, _, C, A, A, A, C, A, A, A, C, A, A, A, A, A, A, A, A, A, A, C, I, A, A, A, C, A, A, A, A, A, C, F, A, A, A, C, C, C, G, G, I, D, B, B, C, 
    _, _, _, _, _, C, A, A, A, F, C, A, A, A, C, A, A, A, A, A, A, A, A, C, A, C, I, A, A, A, F, C, A, A, A, A, A, C, C, C, C, C, G, C, C, C, C, C, B, C, _, 
    _, _, _, _, C, F, A, A, A, C, F, A, A, A, C, A, A, A, A, A, A, A, A, C, A, C, E, A, A, A, A, C, A, A, A, A, A, C, G, G, G, C, C, C, C, G, C, B, C, _, _, 
    _, _, _, _, C, A, A, A, A, C, F, A, A, C, E, A, A, A, A, A, A, A, A, C, A, E, D, E, A, A, A, C, A, A, A, A, A, C, C, G, C, C, G, C, M, G, G, C, B, C, _, 
    _, _, _, C, A, A, A, A, A, C, A, A, A, E, D, E, A, A, C, A, A, A, A, C, A, E, D, D, E, A, A, C, A, A, A, A, A, F, C, C, F, C, G, G, C, C, C, B, B, C, _, 
    _, _, C, A, A, A, A, A, A, C, A, A, K, D, D, E, A, A, C, A, A, A, A, C, A, E, D, D, D, E, A, F, C, A, A, A, A, A, C, F, F, C, G, C, C, B, B, B, B, C, _, 
    _, C, A, A, A, A, A, A, A, C, A, C, C, E, E, C, C, A, C, A, A, A, A, C, A, E, D, D, D, D, E, C, C, A, A, A, A, A, C, F, F, C, C, I, B, B, B, B, C, _, _, 
    C, C, C, C, C, A, A, A, A, C, A, C, C, G, B, G, C, C, A, C, A, A, C, C, C, D, E, E, E, E, D, D, C, A, A, A, A, A, C, F, A, F, C, B, B, B, B, C, C, C, _, 
    _, _, _, _, C, A, A, A, A, C, C, C, G, G, G, G, G, C, E, E, D, A, E, E, D, C, G, B, G, G, C, D, C, A, A, A, A, A, C, F, A, C, B, C, B, B, B, C, B, C, _, 
    _, _, _, _, C, A, A, A, A, C, E, C, G, G, G, G, G, E, D, D, D, E, D, D, E, G, G, G, G, G, G, C, C, A, A, A, A, A, C, F, A, C, B, C, B, B, C, C, B, C, _, 
    _, _, _, _, C, A, A, A, C, E, D, E, H, H, H, H, H, E, D, D, D, D, D, D, E, G, G, G, G, G, G, C, C, A, A, A, A, A, C, A, A, C, B, C, B, B, C, B, B, C, _, 
    _, _, _, _, C, A, A, C, E, D, D, D, E, H, H, H, E, D, D, D, D, D, D, D, E, H, H, H, H, H, H, C, C, A, A, A, A, A, C, A, C, B, B, B, C, B, C, B, B, B, C, 
    _, _, _, _, C, A, A, E, D, D, D, D, D, E, E, E, D, D, D, D, D, D, D, D, D, E, H, H, H, H, C, C, A, A, A, A, A, A, C, C, B, B, B, B, B, C, B, B, B, B, C, 
    _, _, _, _, _, C, A, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, E, E, E, B, C, A, A, A, A, A, F, C, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, C, A, E, D, D, D, D, D, D, D, D, D, E, D, D, E, D, D, E, D, D, D, D, D, D, E, C, A, A, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, C, A, A, E, D, D, D, D, D, D, D, D, D, E, E, D, E, E, D, D, D, D, D, D, D, E, A, A, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, _, C, A, A, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, A, C, C, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, 
    _, _, _, _, _, _, C, A, A, A, E, E, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, E, E, C, C, B, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, C, A, A, A, A, E, E, E, D, D, D, D, D, D, D, D, D, D, E, E, B, B, B, B, B, C, A, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, A, A, C, C, E, E, E, E, E, E, E, E, E, E, C, B, B, B, B, B, B, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, C, A, A, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, _, C, A, C, C, A, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, A, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, 
    _, _, _, _, _, _, _, _, _, _, I, C, _, C, C, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, C, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, C, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, C, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, C, C, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, B, C, C, C, C, B, B, B, B, B, B, B, B, B, C, C, C, _, _, C, B, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, B, C, _, _, _, C, B, B, B, B, B, B, C, C, _, _, _, _, _, _, C, B, B, B, B, B, C, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, B, C, _, _, _, _, _, C, B, B, B, B, B, B, C, _, _, _, _, _, _, C, B, B, B, C, C, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, C, _, _, _, _, _, _, _, C, B, B, B, B, B, C, _, _, _, _, _, _, _, C, C, C, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, B, B, B, C, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, 
    _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, C, C, C, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, 
};

const uint16_t* const walk_frames[] = { sprite_walk_a, sprite_stand, sprite_walk_b, sprite_stand };
constexpr int WALK_FRAME_COUNT = 4;
