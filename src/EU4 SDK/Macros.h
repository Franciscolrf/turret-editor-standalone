#pragma once

// ============================================================================
//  Macros.h - small glue shared by every EU4 SDK header (Basic/, Engine/,
//  Game/, SDK.h). Not part of the SDK dump itself, hand-written to match
//  what those headers expect: PAD() for unnamed padding fields, LOG() for
//  InitSDK()'s diagnostics (routed to this project's Logger), and the
//  BitOne/BitEight single-bit masks used by a couple of hand-decoded
//  bitfields in Engine/Engine.h.
// ============================================================================

#include "../Logger/Logger.h"

#define _EU4_CONCAT_INNER(a, b) a##b
#define _EU4_CONCAT(a, b) _EU4_CONCAT_INNER(a, b)

// Unnamed padding — unique member name per call site via __LINE__.
#define PAD(size) unsigned char _EU4_CONCAT(_pad_, __LINE__)[size]

// InitSDK()/signature-scan diagnostics -> Logger::Log (printf-style).
#define LOG(...) Logger::Log(__VA_ARGS__)

// Single-bit masks used by a few hand-decoded bitfields (AActor::IsPlayerMale,
// AActor::IsConscious) that read a raw byte instead of a real bitfield member.
static constexpr unsigned char BitOne   = 0x01;
static constexpr unsigned char BitTwo   = 0x02;
static constexpr unsigned char BitFour  = 0x04;
static constexpr unsigned char BitEight = 0x08;
