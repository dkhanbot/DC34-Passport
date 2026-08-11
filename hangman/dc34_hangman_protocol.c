#include "dc34_hangman_protocol.h"

#include <string.h>

static size_t dc34_strip_sw(const uint8_t* response, size_t response_len) {
    size_t payload_len = response_len;
    if(payload_len >= 2 && response[payload_len - 2] == 0x90 &&
       response[payload_len - 1] == 0x00) {
        payload_len -= 2;
    }
    return payload_len;
}

bool dc34_parse_hangman_state(
    const uint8_t* response,
    size_t response_len,
    Dc34HangmanState* state) {
    if(state == NULL || response == NULL || response_len < 14) return false;
    if(dc34_strip_sw(response, response_len) < 14) return false;

    memcpy(state->word_mask, response, DC34_HANGMAN_WORD_MAX);
    state->wrong_count = response[8];
    memcpy(state->guessed_mask, response + 9, DC34_HANGMAN_GUESSED_MASK_LEN);
    state->status = (Dc34HangmanStatus)response[13];
    return true;
}

bool dc34_parse_hangman_guess(
    const uint8_t* response,
    size_t response_len,
    bool* correct,
    Dc34HangmanState* state) {
    if(correct == NULL || state == NULL || response == NULL || response_len < 15) return false;
    if(dc34_strip_sw(response, response_len) < 15) return false;

    *correct = response[0] != 0;
    memcpy(state->word_mask, response + 1, DC34_HANGMAN_WORD_MAX);
    state->wrong_count = response[9];
    memcpy(state->guessed_mask, response + 10, DC34_HANGMAN_GUESSED_MASK_LEN);
    state->status = (Dc34HangmanStatus)response[14];
    return true;
}

bool dc34_parse_hangman_stats(
    const uint8_t* response,
    size_t response_len,
    uint16_t* game_count) {
    if(game_count == NULL || response == NULL || response_len < 2) return false;
    if(dc34_strip_sw(response, response_len) < 2) return false;

    *game_count = ((uint16_t)response[0] << 8) | response[1];
    return true;
}

bool dc34_hangman_is_letter_guessed(
    const uint8_t guessed_mask[DC34_HANGMAN_GUESSED_MASK_LEN],
    char letter) {
    if(letter < 'A' || letter > 'Z') return false;
    uint8_t idx = (uint8_t)(letter - 'A');
    return (guessed_mask[idx / 8] >> (idx % 8)) & 1;
}

bool dc34_build_apdu(
    uint8_t* out,
    size_t* out_len,
    uint8_t cla,
    uint8_t ins,
    uint8_t p1,
    uint8_t p2,
    const uint8_t* data,
    size_t data_len) {
    if(out == NULL || out_len == NULL || data_len > 255) return false;

    out[0] = cla;
    out[1] = ins;
    out[2] = p1;
    out[3] = p2;

    if(data_len > 0) {
        if(data == NULL) return false;
        out[4] = (uint8_t)data_len;
        memcpy(out + 5, data, data_len);
        *out_len = 5 + data_len;
    } else {
        *out_len = 4;
    }
    return true;
}
