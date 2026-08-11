#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>

#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>
#include <toolbox/bit_buffer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dc34_hangman_protocol.h"

#define TAG "Dc34Hangman"

static const uint8_t DC34_HANGMAN_AID[] = {0xF0, 0x43, 0x42, 0x48, 0x4D, 0x4E, 0x01};

#define DC34_ALPHABET_COLS 13
#define DC34_ALPHABET_ROWS 2
#define DC34_ALPHABET_COUNT 26

typedef enum {
    Dc34CmdNewGame,
    Dc34CmdGuess,
    Dc34CmdExit,
} Dc34CmdType;

typedef struct {
    Dc34CmdType type;
    uint8_t p1;
    uint8_t p2;
} Dc34Cmd;

typedef struct {
    bool comm_ok; /* false = NFC transceive failed outright */
    bool fatal; /* true = session ended after this result */
    uint16_t sw;
    bool have_state;
    Dc34HangmanState state;
    bool have_guess_result;
    bool guess_correct;
} Dc34Result;

typedef struct {
    uint8_t word_mask[DC34_HANGMAN_WORD_MAX];
    uint8_t wrong_count;
    uint8_t guessed_mask[DC34_HANGMAN_GUESSED_MASK_LEN];
    Dc34HangmanStatus status;
    int cursor;
    bool connecting;
    bool busy;
    bool session_dead;
    char status_line[48];
} Dc34Model;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* board_view;
    FuriTimer* poll_timer;

    Nfc* nfc;
    NfcPoller* poller;
    FuriMessageQueue* cmd_queue;
    FuriMessageQueue* result_queue;
    volatile bool exit_requested;
} Dc34App;

/* ---------- status word -> human text ---------- */

static const char* dc34_sw_to_text(uint16_t sw) {
    switch(sw) {
    case DC34_HANGMAN_SW_SUCCESS:
        return "OK";
    case DC34_HANGMAN_SW_NO_ACTIVE_GAME:
        return "No active game";
    case DC34_HANGMAN_SW_GAME_OVER:
        return "Game already over";
    case DC34_HANGMAN_SW_INVALID_LETTER:
        return "Invalid letter";
    case DC34_HANGMAN_SW_LETTER_ALREADY_GUESSED:
        return "Already guessed";
    case DC34_HANGMAN_SW_CLA_NOT_SUPPORTED:
        return "CLA not supported";
    case DC34_HANGMAN_SW_INS_NOT_SUPPORTED:
        return "INS not supported";
    default:
        return "Card error";
    }
}

/* ---------- NFC worker: runs on the NfcPoller's internal thread ---------- */

static bool dc34_transceive_apdu(
    Iso14443_4aPoller* poller,
    BitBuffer* tx,
    BitBuffer* rx,
    uint8_t cla,
    uint8_t ins,
    uint8_t p1,
    uint8_t p2,
    const uint8_t* data,
    size_t data_len,
    uint16_t* out_sw,
    uint8_t* out_payload,
    size_t* out_payload_len) {
    uint8_t apdu[16];
    size_t apdu_len = 0;
    if(!dc34_build_apdu(apdu, &apdu_len, cla, ins, p1, p2, data, data_len)) return false;

    bit_buffer_reset(tx);
    bit_buffer_copy_bytes(tx, apdu, apdu_len);
    bit_buffer_reset(rx);

    if(iso14443_4a_poller_send_block(poller, tx, rx) != Iso14443_4aErrorNone) return false;

    size_t rx_len = bit_buffer_get_size_bytes(rx);
    if(rx_len < 2) return false;

    uint8_t raw[64];
    if(rx_len > sizeof(raw)) rx_len = sizeof(raw);
    bit_buffer_write_bytes(rx, raw, rx_len);

    *out_sw = ((uint16_t)raw[rx_len - 2] << 8) | raw[rx_len - 1];
    size_t payload_len = rx_len - 2;
    if(out_payload && out_payload_len) {
        if(payload_len > *out_payload_len) payload_len = *out_payload_len;
        memcpy(out_payload, raw, payload_len);
        *out_payload_len = payload_len;
    }
    return true;
}

static void dc34_push_result(Dc34App* app, Dc34Result* result) {
    furi_message_queue_put(app->result_queue, result, 100);
}

static NfcCommand dc34_poller_callback(NfcGenericEvent event, void* context) {
    Dc34App* app = context;
    if(event.protocol != NfcProtocolIso14443_4a) return NfcCommandContinue;

    Iso14443_4aPollerEvent* iso_event = event.event_data;
    Dc34Result result = {0};

    if(iso_event->type == Iso14443_4aPollerEventTypeError) {
        /* Activation failed for this poll cycle (e.g. card not aligned yet). Keep
         * scanning instead of ending the session on the first missed tap. */
        return NfcCommandContinue;
    }

    Iso14443_4aPoller* poller = event.instance;
    BitBuffer* tx = bit_buffer_alloc(32);
    BitBuffer* rx = bit_buffer_alloc(64);
    uint16_t sw = 0;
    uint8_t payload[24];
    size_t payload_len;

    /* 1. Select the Hangman applet. */
    payload_len = sizeof(payload);
    if(!dc34_transceive_apdu(
           poller,
           tx,
           rx,
           0x00,
           0xA4,
           0x04,
           0x00,
           DC34_HANGMAN_AID,
           sizeof(DC34_HANGMAN_AID),
           &sw,
           payload,
           &payload_len)) {
        result.comm_ok = false;
        result.fatal = true;
        dc34_push_result(app, &result);
        goto cleanup_stop;
    }
    if(sw != DC34_HANGMAN_SW_SUCCESS) {
        result.comm_ok = true;
        result.fatal = true;
        result.sw = sw;
        dc34_push_result(app, &result);
        goto cleanup_stop;
    }

    /* 2. Auto-start a new game. */
    payload_len = sizeof(payload);
    if(!dc34_transceive_apdu(
           poller,
           tx,
           rx,
           DC34_HANGMAN_CLA,
           DC34_HANGMAN_INS_NEW_GAME,
           0x00,
           0x00,
           NULL,
           0,
           &sw,
           payload,
           &payload_len)) {
        result.comm_ok = false;
        result.fatal = true;
        dc34_push_result(app, &result);
        goto cleanup_stop;
    }
    result.comm_ok = true;
    result.fatal = false;
    result.sw = sw;
    if(sw == DC34_HANGMAN_SW_SUCCESS &&
       dc34_parse_hangman_state(payload, payload_len, &result.state)) {
        result.have_state = true;
    }
    dc34_push_result(app, &result);
    if(sw != DC34_HANGMAN_SW_SUCCESS) goto cleanup_stop;

    /* 3. Interactive loop: wait for UI commands until told to stop. */
    while(true) {
        if(app->exit_requested) break;

        Dc34Cmd cmd;
        if(furi_message_queue_get(app->cmd_queue, &cmd, 300) != FuriStatusOk) continue;

        if(cmd.type == Dc34CmdExit) break;

        uint8_t ins =
            (cmd.type == Dc34CmdNewGame) ? DC34_HANGMAN_INS_NEW_GAME : DC34_HANGMAN_INS_GUESS;

        memset(&result, 0, sizeof(result));
        payload_len = sizeof(payload);
        if(!dc34_transceive_apdu(
               poller,
               tx,
               rx,
               DC34_HANGMAN_CLA,
               ins,
               cmd.p1,
               cmd.p2,
               NULL,
               0,
               &sw,
               payload,
               &payload_len)) {
            result.comm_ok = false;
            result.fatal = true;
            dc34_push_result(app, &result);
            break;
        }

        result.comm_ok = true;
        result.fatal = false;
        result.sw = sw;
        if(sw == DC34_HANGMAN_SW_SUCCESS) {
            if(cmd.type == Dc34CmdNewGame) {
                if(dc34_parse_hangman_state(payload, payload_len, &result.state)) {
                    result.have_state = true;
                }
            } else {
                bool correct = false;
                if(dc34_parse_hangman_guess(payload, payload_len, &correct, &result.state)) {
                    result.have_state = true;
                    result.have_guess_result = true;
                    result.guess_correct = correct;
                }
            }
        }
        dc34_push_result(app, &result);
    }

cleanup_stop:
    bit_buffer_free(tx);
    bit_buffer_free(rx);
    return NfcCommandStop;
}

/* ---------- Board view: draw + input ---------- */

static void dc34_draw_wrapped(
    Canvas* canvas,
    int x,
    int y,
    int line_height,
    int max_width,
    int max_lines,
    const char* text) {
    char line[32] = {0};
    size_t line_len = 0;
    int lines_drawn = 0;
    const char* word_start = text;

    while(*word_start != '\0' && lines_drawn < max_lines) {
        const char* word_end = word_start;
        while(*word_end != '\0' && *word_end != ' ') word_end++;
        size_t word_len = (size_t)(word_end - word_start);
        if(word_len >= sizeof(line)) word_len = sizeof(line) - 1;

        char candidate[32];
        size_t candidate_len = line_len;
        memcpy(candidate, line, candidate_len);
        if(candidate_len > 0 && candidate_len + 1 < sizeof(candidate)) {
            candidate[candidate_len++] = ' ';
        }
        if(candidate_len + word_len >= sizeof(candidate)) {
            word_len = sizeof(candidate) - candidate_len - 1;
        }
        memcpy(candidate + candidate_len, word_start, word_len);
        candidate_len += word_len;
        candidate[candidate_len] = '\0';

        if(line_len > 0 && canvas_string_width(canvas, candidate) > max_width) {
            canvas_draw_str(canvas, x, y + lines_drawn * line_height, line);
            lines_drawn++;
            line_len = 0;
            continue; /* retry the same word on a fresh line */
        }

        memcpy(line, candidate, candidate_len + 1);
        line_len = candidate_len;
        word_start = (*word_end == ' ') ? word_end + 1 : word_end;
    }

    if(line_len > 0 && lines_drawn < max_lines) {
        canvas_draw_str(canvas, x, y + lines_drawn * line_height, line);
    }
}

static void dc34_format_word_mask(const uint8_t* word_mask, char* out, size_t out_size) {
    size_t pos = 0;
    for(size_t i = 0; i < DC34_HANGMAN_WORD_MAX && pos + 2 < out_size; i++) {
        if(word_mask[i] == 0x00) break;
        if(pos > 0) out[pos++] = ' ';
        out[pos++] = (char)word_mask[i];
    }
    out[pos] = '\0';
}

static void dc34_draw_gallows_figure(Canvas* canvas, uint8_t wrong_count) {
    canvas_draw_line(canvas, 2, 62, 20, 62); /* base */
    canvas_draw_line(canvas, 10, 62, 10, 38); /* pole */
    canvas_draw_line(canvas, 10, 38, 30, 38); /* top beam */
    canvas_draw_line(canvas, 30, 38, 30, 43); /* rope */

    if(wrong_count >= 1) canvas_draw_circle(canvas, 30, 47, 4); /* head */
    if(wrong_count >= 2) canvas_draw_line(canvas, 30, 51, 30, 58); /* body */
    if(wrong_count >= 3) canvas_draw_line(canvas, 30, 53, 25, 57); /* left arm */
    if(wrong_count >= 4) canvas_draw_line(canvas, 30, 53, 35, 57); /* right arm */
    if(wrong_count >= 5) canvas_draw_line(canvas, 30, 58, 26, 62); /* left leg */
    if(wrong_count >= 6) canvas_draw_line(canvas, 30, 58, 34, 62); /* right leg */
}

static void dc34_board_draw_callback(Canvas* canvas, void* ctx) {
    Dc34Model* model = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    if(model->connecting) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 8, 24, "Hangman");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 8, 40, "Present card to");
        canvas_draw_str(canvas, 8, 52, "back of Flipper...");
        return;
    }

    char word_buf[24];
    dc34_format_word_mask(model->word_mask, word_buf, sizeof(word_buf));
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 9, word_buf);
    canvas_set_font(canvas, FontSecondary);

    const int grid_x = 2;
    const int grid_y = 12;
    const int cell_w = 9;
    const int cell_h = 12;
    for(int i = 0; i < DC34_ALPHABET_COUNT; i++) {
        int row = i / DC34_ALPHABET_COLS;
        int col = i % DC34_ALPHABET_COLS;
        int x = grid_x + col * cell_w;
        int y = grid_y + row * cell_h;
        char letter[2] = {(char)('A' + i), '\0'};
        bool guessed = dc34_hangman_is_letter_guessed(model->guessed_mask, letter[0]);

        if(guessed) {
            canvas_draw_box(canvas, x, y, cell_w - 1, cell_h - 1);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, x + 2, y + cell_h - 3, letter);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, x + 2, y + cell_h - 3, letter);
        }

        if(i == model->cursor && model->status == Dc34HangmanStatusInProgress &&
           !model->busy && !model->session_dead) {
            canvas_draw_frame(canvas, x, y, cell_w, cell_h);
        }
    }

    dc34_draw_gallows_figure(canvas, model->wrong_count);

    char wrong_buf[16];
    snprintf(wrong_buf, sizeof(wrong_buf), "Wrong: %u/6", model->wrong_count);
    canvas_draw_str(canvas, 38, 45, wrong_buf);

    if(model->status_line[0] != '\0') {
        dc34_draw_wrapped(canvas, 38, 56, 10, 128 - 38 - 2, 2, model->status_line);
    }
}

static bool dc34_board_input_callback(InputEvent* event, void* ctx) {
    Dc34App* app = ctx;
    if(event->type != InputTypeShort) return false;

    bool consumed = false;

    with_view_model(
        app->board_view,
        Dc34Model * model,
        {
            if(model->connecting || model->busy || model->session_dead) {
                /* No interaction while connecting, mid-request, or after a fatal error. */
            } else if(model->status != Dc34HangmanStatusInProgress) {
                if(event->key == InputKeyOk) {
                    Dc34Cmd cmd;
                    cmd.type = Dc34CmdNewGame;
                    cmd.p1 = 0x00;
                    cmd.p2 = 0x00;
                    if(furi_message_queue_put(app->cmd_queue, &cmd, 0) == FuriStatusOk) {
                        model->busy = true;
                        snprintf(
                            model->status_line, sizeof(model->status_line), "New game...");
                    }
                    consumed = true;
                }
            } else {
                int row = model->cursor / DC34_ALPHABET_COLS;
                int col = model->cursor % DC34_ALPHABET_COLS;
                switch(event->key) {
                case InputKeyUp:
                    if(row > 0) model->cursor -= DC34_ALPHABET_COLS;
                    consumed = true;
                    break;
                case InputKeyDown:
                    if(row == 0 && model->cursor + DC34_ALPHABET_COLS < DC34_ALPHABET_COUNT) {
                        model->cursor += DC34_ALPHABET_COLS;
                    }
                    consumed = true;
                    break;
                case InputKeyLeft:
                    if(col > 0) model->cursor -= 1;
                    consumed = true;
                    break;
                case InputKeyRight:
                    if(col < DC34_ALPHABET_COLS - 1 && model->cursor + 1 < DC34_ALPHABET_COUNT) {
                        model->cursor += 1;
                    }
                    consumed = true;
                    break;
                case InputKeyOk: {
                    char letter = (char)('A' + model->cursor);
                    if(dc34_hangman_is_letter_guessed(model->guessed_mask, letter)) {
                        snprintf(
                            model->status_line,
                            sizeof(model->status_line),
                            "Already guessed");
                    } else {
                        Dc34Cmd cmd;
                        cmd.type = Dc34CmdGuess;
                        cmd.p1 = (uint8_t)letter;
                        cmd.p2 = 0x00;
                        if(furi_message_queue_put(app->cmd_queue, &cmd, 0) == FuriStatusOk) {
                            model->busy = true;
                            snprintf(
                                model->status_line,
                                sizeof(model->status_line),
                                "Guessing '%c'...",
                                letter);
                        }
                    }
                    consumed = true;
                    break;
                }
                default:
                    break;
                }
            }
        },
        true);

    return consumed;
}

/* ---------- Poll timer: bridges worker results into the view model ---------- */

static void dc34_poll_timer_callback(void* ctx) {
    Dc34App* app = ctx;
    Dc34Result result;

    while(furi_message_queue_get(app->result_queue, &result, 0) == FuriStatusOk) {
        with_view_model(
            app->board_view,
            Dc34Model * model,
            {
                model->connecting = false;
                model->busy = false;

                if(!result.comm_ok) {
                    model->session_dead = true;
                    snprintf(
                        model->status_line,
                        sizeof(model->status_line),
                        "Card lost. Press Back.");
                } else if(result.sw != DC34_HANGMAN_SW_SUCCESS) {
                    snprintf(
                        model->status_line,
                        sizeof(model->status_line),
                        "%s",
                        dc34_sw_to_text(result.sw));
                    if(result.fatal) model->session_dead = true;
                } else {
                    if(result.have_state) {
                        memcpy(model->word_mask, result.state.word_mask, DC34_HANGMAN_WORD_MAX);
                        model->wrong_count = result.state.wrong_count;
                        memcpy(
                            model->guessed_mask,
                            result.state.guessed_mask,
                            DC34_HANGMAN_GUESSED_MASK_LEN);
                        model->status = result.state.status;
                    }
                    switch(model->status) {
                    case Dc34HangmanStatusWin:
                        snprintf(
                            model->status_line,
                            sizeof(model->status_line),
                            "You win! Ok=rematch");
                        break;
                    case Dc34HangmanStatusLose:
                        snprintf(
                            model->status_line,
                            sizeof(model->status_line),
                            "You lose. Ok=rematch");
                        break;
                    default:
                        if(result.have_guess_result) {
                            snprintf(
                                model->status_line,
                                sizeof(model->status_line),
                                result.guess_correct ? "Correct!" : "Wrong!");
                        } else {
                            snprintf(
                                model->status_line, sizeof(model->status_line), "Guess a letter");
                        }
                        break;
                    }
                }
            },
            true);
    }
}

/* ---------- Session lifecycle ---------- */

static void dc34_session_start(Dc34App* app) {
    app->exit_requested = false;
    app->nfc = nfc_alloc();
    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolIso14443_4a);
    nfc_poller_start(app->poller, dc34_poller_callback, app);
    furi_timer_start(app->poll_timer, 100);
}

static void dc34_session_stop(Dc34App* app) {
    app->exit_requested = true;
    if(app->poller) {
        Dc34Cmd cmd = {.type = Dc34CmdExit, .p1 = 0, .p2 = 0};
        furi_message_queue_put(app->cmd_queue, &cmd, 0);
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
        app->poller = NULL;
    }
    if(app->nfc) {
        nfc_free(app->nfc);
        app->nfc = NULL;
    }
    furi_message_queue_reset(app->cmd_queue);
    furi_message_queue_reset(app->result_queue);
    furi_timer_stop(app->poll_timer);
}

static void dc34_board_enter_callback(void* ctx) {
    Dc34App* app = ctx;
    with_view_model(
        app->board_view,
        Dc34Model * model,
        {
            memset(model, 0, sizeof(Dc34Model));
            model->connecting = true;
        },
        true);
    dc34_session_start(app);
}

static void dc34_board_exit_callback(void* ctx) {
    Dc34App* app = ctx;
    dc34_session_stop(app);
}

static bool dc34_navigation_callback(void* ctx) {
    Dc34App* app = ctx;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

/* ---------- App lifecycle ---------- */

static Dc34App* dc34_app_alloc(void) {
    Dc34App* app = malloc(sizeof(Dc34App));
    memset(app, 0, sizeof(Dc34App));

    app->cmd_queue = furi_message_queue_alloc(1, sizeof(Dc34Cmd));
    app->result_queue = furi_message_queue_alloc(4, sizeof(Dc34Result));

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    app->board_view = view_alloc();

    view_allocate_model(app->board_view, ViewModelTypeLocking, sizeof(Dc34Model));
    view_set_context(app->board_view, app);
    view_set_draw_callback(app->board_view, dc34_board_draw_callback);
    view_set_input_callback(app->board_view, dc34_board_input_callback);
    view_set_enter_callback(app->board_view, dc34_board_enter_callback);
    view_set_exit_callback(app->board_view, dc34_board_exit_callback);

    app->poll_timer =
        furi_timer_alloc(dc34_poll_timer_callback, FuriTimerTypePeriodic, app);

    view_dispatcher_add_view(app->view_dispatcher, 0, app->board_view);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, dc34_navigation_callback);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, 0);

    return app;
}

static void dc34_app_free(Dc34App* app) {
    furi_timer_free(app->poll_timer);

    view_dispatcher_remove_view(app->view_dispatcher, 0);
    view_free(app->board_view);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);

    furi_message_queue_free(app->cmd_queue);
    furi_message_queue_free(app->result_queue);

    free(app);
}

int32_t dc34_hangman_app(void* p) {
    UNUSED(p);
    Dc34App* app = dc34_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_run(app->view_dispatcher);

    dc34_app_free(app);
    return 0;
}
