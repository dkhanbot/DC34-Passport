#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DC34_HANGMAN_CLA 0x80
#define DC34_HANGMAN_INS_NEW_GAME 0x02
#define DC34_HANGMAN_INS_GUESS 0x04
#define DC34_HANGMAN_INS_GET_STATE 0x06
#define DC34_HANGMAN_INS_GET_STATS 0x08

#define DC34_HANGMAN_SW_SUCCESS 0x9000
#define DC34_HANGMAN_SW_NO_ACTIVE_GAME 0x6985
#define DC34_HANGMAN_SW_GAME_OVER 0x6986
#define DC34_HANGMAN_SW_INVALID_LETTER 0x6A80
#define DC34_HANGMAN_SW_LETTER_ALREADY_GUESSED 0x6A86
#define DC34_HANGMAN_SW_CLA_NOT_SUPPORTED 0x6E00
#define DC34_HANGMAN_SW_INS_NOT_SUPPORTED 0x6D00

#define DC34_HANGMAN_WORD_MAX 8
#define DC34_HANGMAN_GUESSED_MASK_LEN 4
#define DC34_HANGMAN_MAX_WRONG 6

typedef enum {
    Dc34HangmanStatusInProgress = 0x00,
    Dc34HangmanStatusWin = 0x01,
    Dc34HangmanStatusLose = 0x02,
    Dc34HangmanStatusNoGame = 0xFF,
} Dc34HangmanStatus;

typedef struct {
    uint8_t word_mask[DC34_HANGMAN_WORD_MAX]; /* ASCII letter, '_' (0x5F) hidden, 0x00 padding */
    uint8_t wrong_count;
    uint8_t guessed_mask[DC34_HANGMAN_GUESSED_MASK_LEN];
    Dc34HangmanStatus status;
} Dc34HangmanState;

/* Parses the 14-byte NEW_GAME / GET_STATE payload (SW optionally included). */
bool dc34_parse_hangman_state(
    const uint8_t* response,
    size_t response_len,
    Dc34HangmanState* state);

/* Parses the 15-byte GUESS payload (SW optionally included). */
bool dc34_parse_hangman_guess(
    const uint8_t* response,
    size_t response_len,
    bool* correct,
    Dc34HangmanState* state);

/* Parses the 2-byte GET_STATS payload (SW optionally included). */
bool dc34_parse_hangman_stats(
    const uint8_t* response,
    size_t response_len,
    uint16_t* game_count);

/* letter must be uppercase ASCII 'A'-'Z'. */
bool dc34_hangman_is_letter_guessed(const uint8_t guessed_mask[DC34_HANGMAN_GUESSED_MASK_LEN], char letter);

bool dc34_build_apdu(
    uint8_t* out,
    size_t* out_len,
    uint8_t cla,
    uint8_t ins,
    uint8_t p1,
    uint8_t p2,
    const uint8_t* data,
    size_t data_len);

#ifdef __cplusplus
}
#endif
