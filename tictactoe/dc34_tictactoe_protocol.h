#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DC34_TICTACTOE_CLA 0x80
#define DC34_TICTACTOE_INS_NEW_GAME 0x02
#define DC34_TICTACTOE_INS_PLAY_MOVE 0x04
#define DC34_TICTACTOE_INS_GET_BOARD 0x06
#define DC34_TICTACTOE_INS_GET_STATS 0x08

#define DC34_SW_SUCCESS 0x9000
#define DC34_SW_NO_ACTIVE_GAME 0x6985
#define DC34_SW_GAME_OVER 0x6986
#define DC34_SW_CELL_OCCUPIED 0x6A80
#define DC34_SW_INVALID_POSITION 0x6A86
#define DC34_SW_CLA_NOT_SUPPORTED 0x6E00
#define DC34_SW_INS_NOT_SUPPORTED 0x6D00

typedef enum {
    DC34_TIC_TAC_TOE_STATUS_IN_PROGRESS = 0x00,
    DC34_TIC_TAC_TOE_STATUS_X_WINS = 0x01,
    DC34_TIC_TAC_TOE_STATUS_O_WINS = 0x02,
    DC34_TIC_TAC_TOE_STATUS_DRAW = 0x03,
} Dc34TicTacToeStatus;

typedef struct {
    uint8_t board[9];
    Dc34TicTacToeStatus status;
} Dc34TicTacToeState;

bool dc34_parse_tictactoe_response(
    const uint8_t* response,
    size_t response_len,
    Dc34TicTacToeState* state);
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
