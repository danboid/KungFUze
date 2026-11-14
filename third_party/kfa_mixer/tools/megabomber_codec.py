#!/usr/bin/env python3
"""Uzebox dconvert-compatible Precedent Scaling ADPCM helpers.

The 2-bit path matches dconvert transform 6 and Tower of Rubble's decoder.
The 4-bit path implements the intended transform-7 four-decision code layout
(bits 3..0) and matching decoder.
"""
from __future__ import annotations

from typing import Iterable


def _adjust(accum: int, delta: int, up: bool) -> int:
    return (accum + delta if up else accum - delta) & 0xFF


def encode_precedent(samples: bytes | bytearray | Iterable[int], bits: int) -> bytes:
    data = bytes(samples)
    if bits not in (2, 4):
        raise ValueError("bits must be 2 or 4")
    if not data:
        return b""

    symbols_per_byte = 8 // bits
    pad = (-len(data)) % symbols_per_byte
    if pad:
        data += data[-1:] * pad

    accum = 0x80
    slope = 2 if bits == 2 else 7
    up_count = down_count = neutral_count = 0
    out = bytearray()
    mask_weights = (1, 2) if bits == 2 else (1, 2, 4, 8)

    for base in range(0, len(data), symbols_per_byte):
        packed = 0
        for symbol_index in range(symbols_per_byte):
            target = data[base + symbol_index]
            code = 0
            for decision, divisor in enumerate(mask_weights):
                delta = slope // divisor
                up = accum < target
                if up:
                    code |= 1 << (bits - 1 - decision)
                    up_count += 1
                    down_count = 0
                else:
                    down_count += 1
                    up_count = 0
                accum = _adjust(accum, delta, up)

            neutral_count += 1
            if bits == 2:
                if neutral_count == 4:
                    up_count = down_count = neutral_count = 0
                    if slope > 2:
                        slope -= 1
                if up_count == 2 or down_count == 2:
                    up_count = down_count = neutral_count = 0
                    if slope < 16:
                        slope += 1
            else:
                if neutral_count > 2:
                    up_count = down_count = neutral_count = 0
                    if slope > 4:
                        slope -= 1
                if up_count > 3 or down_count > 3:
                    up_count = down_count = neutral_count = 0
                    if slope < 24:
                        slope += 2

            packed |= code << (symbol_index * bits)
        out.append(packed)
    return bytes(out)


def decode_precedent(packed: bytes | bytearray, bits: int, sample_count: int) -> bytes:
    if bits not in (2, 4):
        raise ValueError("bits must be 2 or 4")
    if sample_count < 0:
        raise ValueError("sample_count must be non-negative")

    accum = 0x80
    slope = 2 if bits == 2 else 7
    up_count = down_count = neutral_count = 0
    mask_weights = (1, 2) if bits == 2 else (1, 2, 4, 8)
    symbol_mask = (1 << bits) - 1
    result = bytearray()

    for value in packed:
        byte = value
        for _ in range(8 // bits):
            if len(result) >= sample_count:
                return bytes(result)
            code = byte & symbol_mask
            byte >>= bits
            for decision, divisor in enumerate(mask_weights):
                delta = slope // divisor
                up = bool(code & (1 << (bits - 1 - decision)))
                if up:
                    up_count += 1
                    down_count = 0
                else:
                    down_count += 1
                    up_count = 0
                accum = _adjust(accum, delta, up)

            neutral_count += 1
            if bits == 2:
                if neutral_count == 4:
                    up_count = down_count = neutral_count = 0
                    if slope > 2:
                        slope -= 1
                if up_count == 2 or down_count == 2:
                    up_count = down_count = neutral_count = 0
                    if slope < 16:
                        slope += 1
            else:
                if neutral_count > 2:
                    up_count = down_count = neutral_count = 0
                    if slope > 4:
                        slope -= 1
                if up_count > 3 or down_count > 3:
                    up_count = down_count = neutral_count = 0
                    if slope < 24:
                        slope += 2
            result.append(accum)
    return bytes(result)

# Compact 16-state 4-bit adaptive codec used by MegaBomber's experimental
# scanline mixer.  The decoder is deliberately LUT-friendly: each state/code
# pair produces one signed 8-bit delta and one next-state byte, so the complete
# AVR decode table is only 16*16*2 = 512 bytes.  Samples use the same unsigned
# 8-bit modulo accumulator as the existing precedent-scaling path.
ADPCM4_FAST16_STEPS = (1, 2, 3, 4, 6, 9, 13, 18, 25, 35, 49, 68, 88, 106, 120, 127)
ADPCM4_FAST16_INDEX_ADJUST = (-1, -1, -1, 0, 1, 2, 3, 4)


def adpcm4_fast16_transition(state: int, code: int) -> tuple[int, int]:
    """Return (next_state, signed_delta) for one 4-bit symbol."""
    if not 0 <= state < 16 or not 0 <= code < 16:
        raise ValueError("state/code outside fast16 range")
    step = ADPCM4_FAST16_STEPS[state]
    mag = code & 7
    # IMA-like midpoint quantizer, capped so the LUT delta always fits in s8.
    delta = ((2 * mag + 1) * step + 4) // 8
    if delta < 1:
        delta = 1
    elif delta > 127:
        delta = 127
    if code & 8:
        delta = -delta
    next_state = state + ADPCM4_FAST16_INDEX_ADJUST[mag]
    if next_state < 0:
        next_state = 0
    elif next_state > 15:
        next_state = 15
    return next_state, delta


def encode_adpcm4_fast16(samples: bytes | bytearray | Iterable[int]) -> bytes:
    """Greedy encoder for the compact 16-state 4-bit adaptive decoder."""
    data = bytes(samples)
    if not data:
        return b""
    if len(data) & 1:
        data += data[-1:]

    # Precompute decoded candidate and next state for every state/code pair.
    delta = [[0] * 16 for _ in range(16)]
    nxt = [[0] * 16 for _ in range(16)]
    for st in range(16):
        for code in range(16):
            ns, d = adpcm4_fast16_transition(st, code)
            nxt[st][code] = ns
            delta[st][code] = d

    accum = 0x80
    state = 0
    out = bytearray()
    for base in range(0, len(data), 2):
        packed = 0
        for symbol in range(2):
            target = data[base + symbol]
            best_code = 0
            best_sample = (accum + delta[state][0]) & 0xFF
            best_error = abs(best_sample - target)
            for code in range(1, 16):
                sample = (accum + delta[state][code]) & 0xFF
                err = abs(sample - target)
                if err < best_error:
                    best_error = err
                    best_code = code
                    best_sample = sample
            accum = best_sample
            state = nxt[state][best_code]
            packed |= best_code << (symbol * 4)
        out.append(packed)
    return bytes(out)


def decode_adpcm4_fast16(packed: bytes | bytearray, sample_count: int) -> bytes:
    if sample_count < 0:
        raise ValueError("sample_count must be non-negative")
    accum = 0x80
    state = 0
    out = bytearray()
    for value in packed:
        byte = value
        for _ in range(2):
            if len(out) >= sample_count:
                return bytes(out)
            code = byte & 0x0F
            byte >>= 4
            state, delta = adpcm4_fast16_transition(state, code)
            accum = (accum + delta) & 0xFF
            out.append(accum)
    return bytes(out)

