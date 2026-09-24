#pragma once

#include <cstddef>
#include <cstdint>

// Cabecalho wire 7.48 — cópia Linux.
// Tick usa uint32_t para preservar 12 bytes em LP64 (Linux x86_64), onde
// unsigned long tem 8 bytes. O layout on-wire permanece idêntico ao Win32.

typedef struct
{
    unsigned short Size;
    unsigned char KeyWord;
    unsigned char CheckSum;
    unsigned short Type;
    unsigned short ID;
    std::uint32_t Tick;
} MSG_STANDARD;

static_assert(sizeof(unsigned short) == 2 && sizeof(std::uint32_t) == 4,
    "Wire header requires fixed 16/32-bit integer widths");
static_assert(sizeof(MSG_STANDARD) == 12, "Wire header size changed");
static_assert(offsetof(MSG_STANDARD, Size) == 0, "Size offset changed");
static_assert(offsetof(MSG_STANDARD, KeyWord) == 2, "KeyWord offset changed");
static_assert(offsetof(MSG_STANDARD, CheckSum) == 3, "CheckSum offset changed");
static_assert(offsetof(MSG_STANDARD, Type) == 4, "Type offset changed");
static_assert(offsetof(MSG_STANDARD, ID) == 6, "ID offset changed");
static_assert(offsetof(MSG_STANDARD, Tick) == 8, "Tick offset changed");
