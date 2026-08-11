#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../dc34_hangman_protocol.h"

int main(void) {
    /* NEW_GAME: 6-letter word, all hidden */
    uint8_t new_game_response[] = {
        0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x00, 0x00, /* word mask */
        0x00, /* wrong count */
        0x00, 0x00, 0x00, 0x00, /* guessed mask */
        0x00, /* status */
        0x90, 0x00};
    Dc34HangmanState state;
    assert(dc34_parse_hangman_state(new_game_response, sizeof(new_game_response), &state));
    assert(state.word_mask[0] == 0x5F);
    assert(state.word_mask[6] == 0x00);
    assert(state.wrong_count == 0);
    assert(state.status == Dc34HangmanStatusInProgress);

    /* GUESS 'C' - correct, revealed at position 0 */
    uint8_t guess_correct_response[] = {
        0x01, /* correct */
        0x43, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x00, 0x00, /* word mask */
        0x00, /* wrong count */
        0x04, 0x00, 0x00, 0x00, /* guessed mask (bit 2 = 'C') */
        0x00, /* status */
        0x90, 0x00};
    bool correct = false;
    Dc34HangmanState guess_state;
    assert(dc34_parse_hangman_guess(
        guess_correct_response, sizeof(guess_correct_response), &correct, &guess_state));
    assert(correct == true);
    assert(guess_state.word_mask[0] == 'C');
    assert(guess_state.word_mask[1] == 0x5F);
    assert(guess_state.wrong_count == 0);
    assert(dc34_hangman_is_letter_guessed(guess_state.guessed_mask, 'C'));
    assert(!dc34_hangman_is_letter_guessed(guess_state.guessed_mask, 'A'));

    /* GUESS 'Z' - wrong */
    uint8_t guess_wrong_response[] = {
        0x00, /* wrong */
        0x43, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x00, 0x00,
        0x01, /* wrong count */
        0x04, 0x00, 0x00, 0x02, /* guessed mask ('C' and 'Z') */
        0x00,
        0x90, 0x00};
    assert(dc34_parse_hangman_guess(
        guess_wrong_response, sizeof(guess_wrong_response), &correct, &guess_state));
    assert(correct == false);
    assert(guess_state.wrong_count == 1);
    assert(dc34_hangman_is_letter_guessed(guess_state.guessed_mask, 'Z'));

    /* Win: word CRYPTO fully revealed */
    uint8_t win_response[] = {
        0x01,
        'C', 'R', 'Y', 'P', 'T', 'O', 0x00, 0x00,
        0x01,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01, /* win */
        0x90, 0x00};
    assert(dc34_parse_hangman_guess(win_response, sizeof(win_response), &correct, &guess_state));
    assert(guess_state.status == Dc34HangmanStatusWin);
    assert(memcmp(guess_state.word_mask, "CRYPTO\x00\x00", 8) == 0);

    /* GET_STATS */
    uint8_t stats_response[] = {0x00, 0x03, 0x90, 0x00};
    uint16_t game_count = 0;
    assert(dc34_parse_hangman_stats(stats_response, sizeof(stats_response), &game_count));
    assert(game_count == 3);

    /* APDU builder */
    uint8_t apdu[8] = {0};
    size_t apdu_len = 0;
    assert(dc34_build_apdu(apdu, &apdu_len, 0x80, 0x04, 'C', 0x00, NULL, 0));
    assert(apdu_len == 4);
    assert(apdu[0] == 0x80);
    assert(apdu[1] == 0x04);
    assert(apdu[2] == 'C');
    assert(apdu[3] == 0x00);

    uint8_t select_data[] = {0xF0, 0x43, 0x42, 0x48, 0x4D, 0x4E, 0x01};
    uint8_t select_apdu[16] = {0};
    size_t select_apdu_len = 0;
    assert(dc34_build_apdu(
        select_apdu,
        &select_apdu_len,
        0x00,
        0xA4,
        0x04,
        0x00,
        select_data,
        sizeof(select_data)));
    assert(select_apdu_len == 5 + sizeof(select_data));
    assert(select_apdu[4] == sizeof(select_data));
    assert(memcmp(select_apdu + 5, select_data, sizeof(select_data)) == 0);

    puts("dc34 hangman protocol tests passed");
    return 0;
}
