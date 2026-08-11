# DC34 TicTacToe

A Flipper Zero external app that plays TicTacToe against a DC34 JavaCard badge over NFC (ISO14443-4A / APDU). The game itself is hosted on https://dc34.rfid.wtf

## Building

Requires [ufbt](https://github.com/flipperdevices/flipperzero-ufbt):

```
ufbt
```

## Running on a connected Flipper

```
ufbt launch
```

## Tests

The protocol codec (`dc34_tictactoe_protocol.c`) has a standalone host-side unit test with no Flipper SDK dependency:

```
gcc -Wall -Wextra -o /tmp/dc34_test tests/dc34_tictactoe_protocol_test.c dc34_tictactoe_protocol.c
/tmp/dc34_test
```

## How it works

On entering the app, it selects the TicTacToe applet AID over ISO14443-4A, auto-starts a new game (you play X, the card plays O), and lets you move the cursor and place moves with the D-pad and OK button. Press Back to leave the game.
