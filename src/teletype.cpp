// system includes
#include <Arduino.h>

// local includes
#include "teletype.h"

namespace {
constexpr const char TAG[] = "TTY";
}  // namespace

static bool tty_rx = false;
static void tx_from_tty();

Teletype::Teletype(uint8_t baudrate, uint8_t rx_pin, uint8_t tx_pin, uint8_t max_chars) {
    // initialize teletype values (baudrate etc)
    TTY_BAUDRATE = baudrate;
    DELAY_BIT = (1000 / TTY_BAUDRATE);  // in milliseconds

    DELAY_STOPBIT = DELAY_BIT * 1.5;
    TTY_MAX_CHARS_PAPER = max_chars;
    TTY_RX_PIN = rx_pin;
    TTY_TX_PIN = tx_pin;

    // esp_log_level_set(TAG, ESP_LOG_WARN);
    pinMode(TTY_RX_PIN, OUTPUT);  // RX of the Teletype
    digitalWrite(TTY_RX_PIN, 1);

    kb_mode = MODE_UNKNOWN;
    pr_mode = MODE_UNKNOWN;
    delay(DELAY_BIT * 5);
    set_letter();
    delay(DELAY_BIT * 5);

    // initialize with CR + LF
    print_ascii_character('\r');
    print_ascii_character('\n');
    characters_on_paper = 0;
    attachInterrupt(digitalPinToInterrupt(TTY_TX_PIN), tx_from_tty, RISING);
}

void Teletype::set_number() {
    set_mode(MODE_NUMBER);
}

void Teletype::set_letter() {
    set_mode(MODE_LETTER);
}

void Teletype::set_mode(tty_mode_t mode) {
    switch (mode) {
        case MODE_LETTER:
            tx_bits_to_tty(SWITCH_LETTER);
            pr_mode = MODE_LETTER;
            break;
        case MODE_NUMBER:
            tx_bits_to_tty(SWITCH_NUMBER);
            pr_mode = MODE_NUMBER;
            break;
        default:;
    }
}

void Teletype::tx_bits_to_tty(uint8_t bits) {
    Serial.printf("pattern: %x\n", bits);
    bool tx_bit{false};

    // TODO: need to disable interrupts while writing a bit

    // startbit
    digitalWrite(TTY_RX_PIN, 0);
    delayMicroseconds(DELAY_BIT * 1000);

    for (int i = 0; i < NUMBER_OF_BITS; i++) {
        tx_bit = (bits & (1 << i));
        digitalWrite(TTY_RX_PIN, tx_bit);
        delayMicroseconds(DELAY_BIT * 1000);
    }

    // Stopbit
    digitalWrite(TTY_RX_PIN, 1);
    delayMicroseconds(DELAY_STOPBIT * 1000);
}

static void tx_from_tty() {
    tty_rx = true;
}

uint8_t Teletype::rx_bits_from_tty() {
    // disable interrupt
    if (!tty_rx) {
        return '\0';
    }

    detachInterrupt(digitalPinToInterrupt(TTY_TX_PIN));
    // TODO add char to buffer
    uint8_t result{0};
    // Serial.printf("Hello from the RX Bits");
    // Wait till we are in the middle of Startbit
    delayMicroseconds(DELAY_BIT * 500);
    if (digitalRead(TTY_TX_PIN) == 1) {
        for (int i = 0; i < 5; i++) {
            delayMicroseconds(DELAY_BIT * 1000);
            result += ((1 - digitalRead(TTY_TX_PIN)) << i);
        }
        delayMicroseconds(DELAY_BIT * 3000);
    } else {
        Serial.printf("ERROR! Start bit not 0! False trigger?\r\n");
    }
    char ret = static_cast<char>(tolower(convert_baudot_char_to_ascii(result)));
    // TODO: erro checking (need to decide how)                                                         // re-enable the interrupt

    attachInterrupt(digitalPinToInterrupt(TTY_TX_PIN), tx_from_tty, RISING);
    tty_rx = false;                                      // Reset flag after processing
    return ret;
}

void Teletype::print_ascii_character(char c) {
    print_to_tty(convert_ascii_character_to_baudot(c));  // TODO: error checking (need to decide how)
}

void Teletype::print_to_tty(print_baudot_char_t bd_char) {
    if (pr_mode != MODE_BOTH_POSSIBLE && pr_mode != bd_char.mode) {
        set_mode(bd_char.mode);
    }
    tx_bits_to_tty(bd_char.bitcode);
    switch (bd_char.cc_action) {
        case INCREMENT_CHAR_COUNT:
            characters_on_paper++;
            if (characters_on_paper > TTY_MAX_CHARS_PAPER) {
                Serial.printf("ERROR: maximum characters on paper exceeded, printing newline\r\n");
                // this->decide_on_crnl("\r\n");
                tx_bits_to_tty(convert_ascii_character_to_baudot('\r').bitcode);
                tx_bits_to_tty(convert_ascii_character_to_baudot('\n').bitcode);
                characters_on_paper = 0;
            }
            break;
        case RESET_CHAR_COUNT:
            characters_on_paper = 0;
            break;
        default:
            break;
    }
}
/*
void Teletype::decide_on_crnl(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), toupper);
    Serial.printf("%s", str.c_str());

    std::list<print_baudot_char_t> bd_char_list;

    for (auto it = str.begin(); it != str.end(); it++) {
        char c = *it;
        // TODO: decide if we REALLY want to change the contents of the string THAT MUCH
        if (c == '\n') {
            // change \n to CR + LF
            bd_char_list.push_back(convert_ascii_character_to_baudot('\r'));
            bd_char_list.push_back(convert_ascii_character_to_baudot('\n'));
            if (*(it + 1) == '\r')  // if string contains \n\r the \r needs to be discarded -> swap to \r\n
                it++;
        } else if (c == '\r' && *(it + 1) != '\n') {
            // change \r to CR + LF
            bd_char_list.push_back(convert_ascii_character_to_baudot('\r'));
            bd_char_list.push_back(convert_ascii_character_to_baudot('\n'));
        } else {
            // normal character or direct
            bd_char_list.push_back(convert_ascii_character_to_baudot(c));
        }
    }

    for (auto it = bd_char_list.begin(); it != bd_char_list.end(); it++) {
        print_to_tty(*it);
        Serial.printf("%x %d %c", it->bitcode, it->mode, convert_baudot_char_to_ascii(it->bitcode));
    }
}
*/

print_baudot_char_t Teletype::convert_ascii_character_to_baudot(char c) {
    c = static_cast<char>(toupper(c));

    print_baudot_char_t bd_char;
    bd_char.bitcode = 0b0;
    bd_char.mode = MODE_UNKNOWN;

    bool found{false};

    Serial.printf("CHAR = '%c'", c);

    if (c == ASCII_ETX) {
        Serial.printf("Char was ctrl + c: not printing this one to paper\r\n");
        found = true;
    }

    for (int i = 0; i < NUMBER_OF_BAUDOT_CHARS && !found; i++) {
        if (baudot_alphabet[i].mode_letter == c) {
            bd_char.bitcode = baudot_alphabet[i].bitcode;
            bd_char.mode = MODE_LETTER;
            found = true;
            Serial.printf("LETTER %c", baudot_alphabet[i].mode_letter);
        }

        if (baudot_alphabet[i].mode_number == c) {
            bd_char.bitcode = baudot_alphabet[i].bitcode;
            // if character is printable in both modes select the "both_modes" flag
            // --> space, newline, carriage return, NUL
            if (bd_char.mode != MODE_UNKNOWN) {
                bd_char.mode = MODE_BOTH_POSSIBLE;
            } else {
                bd_char.mode = MODE_NUMBER;
            }
            found = true;
            Serial.printf("NUMBER %c", baudot_alphabet[i].mode_number);
        }
        if (found) {
            bd_char.cc_action = baudot_alphabet[i].cc_action;
        }
    }

    if (!found) {
        Serial.printf("letter not found in alphabet, printing nothing (0x00)\r\n");
        // bd_char = this->convert_ascii_character_to_baudot(' '); // TODO: avoid unneccesary mode change when printing space
        bd_char.mode = MODE_BOTH_POSSIBLE;
    }

    Serial.printf("BITCODE = '%x'", bd_char.bitcode);
    return bd_char;
}

char Teletype::convert_baudot_char_to_ascii(uint8_t bits) {
    bool found{false};

    char ret{0};
    // TODO: Remove this hack if we ever loopback locally
    switch (bits) {
        case SWITCH_LETTER:
            kb_mode = MODE_LETTER;
            break;
        case SWITCH_NUMBER:
            kb_mode = MODE_NUMBER;
            break;
        default:
            for (int i = 0; i < NUMBER_OF_BAUDOT_CHARS && !found; i++)  // found is unused
            {
                if (baudot_alphabet[i].bitcode == bits) {
                    switch (kb_mode) {
                        case MODE_LETTER:
                            ret = baudot_alphabet[i].mode_letter;
                            found = true;
                            break;
                        case MODE_NUMBER:
                            ret = baudot_alphabet[i].mode_number;
                            found = true;
                            break;
                        default:
                            Serial.printf("ERROR: state unknown, returning 0");
                    }
                }
            }
            break;
    }

    if (ret == ASCII_ETX) {
        Serial.printf("CTRL + C pressed");
    }
    return ret;
}

void Teletype::print_all_characters() {
    print_baudot_char_t bd_char;

    for (auto& i : baudot_alphabet) {
        bd_char.bitcode = i.bitcode;
        bd_char.mode = MODE_LETTER;
        print_to_tty(bd_char);
        delay(DELAY_STOPBIT);
    }

    for (auto& i : baudot_alphabet) {
        bd_char.bitcode = i.bitcode;
        bd_char.mode = MODE_NUMBER;
        print_to_tty(bd_char);
        delay(DELAY_STOPBIT);
    }
}

uint8_t Teletype::get_TTY_BAUDRATE() {
    return TTY_BAUDRATE;
}

uint8_t Teletype::get_TTY_MAX_CHARS_PAPER() {
    return TTY_MAX_CHARS_PAPER;
}

uint8_t Teletype::get_TTY_RX_PIN() {
    return TTY_RX_PIN;
}

uint8_t Teletype::get_TTY_TX_PIN() {
    return TTY_TX_PIN;
}