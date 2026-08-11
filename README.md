# DC34 Games

Flipper Zero external apps that play games against a DC34 Passport over NFC (ISO14443-4A / APDU). Info on the games on the passport can be found at https://dc34.rfid.wtf/games

## TicTacToe

Play TicTacToe against the badge — you play X, the card plays O. Source lives at the repo root.

### Building

Requires [ufbt](https://github.com/flipperdevices/flipperzero-ufbt):

```
ufbt
```

### Running on a connected Flipper

```
ufbt launch
```

### Tests

The protocol codec (`dc34_tictactoe_protocol.c`) has a standalone host-side unit test with no Flipper SDK dependency:

```
gcc -Wall -Wextra -o /tmp/dc34_test tests/dc34_tictactoe_protocol_test.c dc34_tictactoe_protocol.c
/tmp/dc34_test
```

### How it works

On entering the app, it selects the TicTacToe applet AID over ISO14443-4A, auto-starts a new game (you play X, the card plays O), and lets you move the cursor and place moves with the D-pad and OK button. Press Back to leave the game.

## Hangman

Guess a hidden word one letter at a time against the badge; six wrong guesses and the gallows figure is complete. Source lives in [hangman/](hangman/).

### Building

```
cd hangman
UFBT_APP_DIR="$(pwd)" ufbt
```

### Running on a connected Flipper

```
cd hangman
UFBT_APP_DIR="$(pwd)" ufbt launch
```

### Tests

```
gcc -Wall -Wextra -o /tmp/dc34_hangman_test hangman/tests/dc34_hangman_protocol_test.c hangman/dc34_hangman_protocol.c
/tmp/dc34_hangman_test
```

### How it works

On entering the app, it selects the Hangman applet AID and auto-starts a new game. Move the cursor over the on-screen alphabet with the D-pad and press OK to guess a letter; guessed letters are marked. Press Back to leave the game.
