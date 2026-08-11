#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../dc34_tictactoe_protocol.h"

int main(void) {
    uint8_t tic_response[] = {0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
    Dc34TicTacToeState tic_state;
    assert(dc34_parse_tictactoe_response(tic_response, sizeof(tic_response), &tic_state));
    assert(tic_state.board[0] == 0x02);
    assert(tic_state.board[4] == 0x01);
    assert(tic_state.status == DC34_TIC_TAC_TOE_STATUS_IN_PROGRESS);

    uint8_t tic_response_sw[] = {
        0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00};
    Dc34TicTacToeState tic_state_sw;
    assert(
        dc34_parse_tictactoe_response(tic_response_sw, sizeof(tic_response_sw), &tic_state_sw));
    assert(tic_state_sw.status == DC34_TIC_TAC_TOE_STATUS_IN_PROGRESS);

    uint8_t apdu[8] = {0};
    size_t apdu_len = 0;
    assert(dc34_build_apdu(apdu, &apdu_len, 0x80, 0x02, 0x00, 0x00, NULL, 0));
    assert(apdu_len == 4);
    assert(apdu[0] == 0x80);
    assert(apdu[1] == 0x02);
    assert(apdu[2] == 0x00);
    assert(apdu[3] == 0x00);

    uint8_t select_data[] = {0xF0, 0x43, 0x42, 0x54, 0x54, 0x54, 0x01};
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

    puts("dc34 tictactoe protocol tests passed");
    return 0;
}
