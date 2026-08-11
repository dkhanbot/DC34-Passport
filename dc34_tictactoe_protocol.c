#include "dc34_tictactoe_protocol.h"

#include <string.h>

bool dc34_parse_tictactoe_response(
    const uint8_t* response,
    size_t response_len,
    Dc34TicTacToeState* state) {
    size_t payload_len = response_len;
    if(state == NULL || response == NULL || response_len < 10) return false;

    if(payload_len >= 2 && response[payload_len - 2] == 0x90 &&
       response[payload_len - 1] == 0x00) {
        payload_len -= 2;
    }
    if(payload_len < 10) return false;

    memcpy(state->board, response, 9);
    state->status = (Dc34TicTacToeStatus)response[9];
    return true;
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
