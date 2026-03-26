// system includes
#include <esp_common.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <gpio.h>
#include <rom/ets_sys.h>

#include <cstring>

// local includes
#include "teletype.h"

namespace {
constexpr const char TAG[] = "TTY";
}  // namespace

static bool tty_rx = false;
static Teletype* teletype_instance = nullptr;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    tty_rx = true;
}

Teletype::Teletype(uint8_t baudrate, uint8_t rx_pin, uint8_t tx_pin, uint8_t max_chars) {
    // Store instance for ISR
    teletype_instance = this;

    // initialize teletype values (baudrate etc)
    TTY_BAUDRATE = baudrate;
    DELAY_BIT = (1000 / TTY_BAUDRATE);  // in milliseconds

    DELAY_STOPBIT = DELAY_BIT * 1.5;
    TTY_MAX_CHARS_PAPER = max_chars;
    TTY_RX_PIN = rx_pin;
    TTY_TX_PIN = tx_pin;

    // Configure GPIO pins
    // Set RX pin as output
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
    GPIO_DIS_OUTPUT(TTY_RX_PIN);
    gpio_output_set(0, (1 << TTY_RX_PIN), (1 << TTY_RX_PIN), 0);

    // Set TX pin as input with pull-up
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
    GPIO_DIS_OUTPUT(TTY_TX_PIN);
    GPIO_REG_WRITE(GPIO_PIN_ADDR(GPIO_ID_PIN(TTY_TX_PIN)), GPIO_REG_READ(GPIO_PIN_ADDR(GPIO_ID_PIN(TTY_TX_PIN))) | GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE));

    // Set RX high initially
    gpio_output_set((1 << TTY_RX_PIN), 0, (1 << TTY_RX_PIN), 0);

    kb_mode = MODE_UNKNOWN;
    pr_mode = MODE_UNKNOWN;
    vTaskDelay(pdMS_TO_TICKS(DELAY_BIT * 5));
    shift_to_letters();
    vTaskDelay(pdMS_TO_TICKS(DELAY_BIT * 5));

    // initialize with CR + LF
    print_ascii_character_to_tty('\r');
    print_ascii_character_to_tty('\n');
    characters_on_paper = 0;

    // Set up GPIO interrupt for TX pin (rising edge)
    gpio_intr_handler_register(gpio_isr_handler, nullptr);
    gpio_pin_intr_state_set(GPIO_ID_PIN(TTY_TX_PIN), GPIO_PIN_INTR_POSEDGE);

    ESP_LOGI(TAG, "Teletype initialized on RX=%d TX=%d, baudrate=%d", TTY_RX_PIN, TTY_TX_PIN, TTY_BAUDRATE);
}

void Teletype::shift_to_numbers() {
    set_mode(MODE_NUMBER);
}

void Teletype::shift_to_letters() {
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
    ESP_LOGI(TAG, "pattern: %x", bits);
    bool tx_bit{false};

    // Disable interrupts while transmitting
    taskENTER_CRITICAL();

    // startbit
    gpio_output_set(0, (1 << TTY_RX_PIN), (1 << TTY_RX_PIN), 0);
    ets_delay_us(DELAY_BIT * 1000);

    for (int i = 0; i < NUMBER_OF_BITS; i++) {
        tx_bit = (bits & (1 << i));
        if (tx_bit) {
            gpio_output_set((1 << TTY_RX_PIN), 0, (1 << TTY_RX_PIN), 0);
        } else {
            gpio_output_set(0, (1 << TTY_RX_PIN), (1 << TTY_RX_PIN), 0);
        }
        ets_delay_us(DELAY_BIT * 1000);
    }

    // Stopbit
    gpio_output_set((1 << TTY_RX_PIN), 0, (1 << TTY_RX_PIN), 0);
    ets_delay_us(DELAY_STOPBIT * 1000);

    // Re-enable interrupts
    taskEXIT_CRITICAL();
}

// ISR routine
static void IRAM_ATTR rx_from_tty() {
    tty_rx = true;
}

uint8_t Teletype::read_rx_bits_tty() {
    if (!tty_rx) {
        return '\0';
    }

    // disable interrupt
    gpio_pin_intr_state_set(GPIO_ID_PIN(TTY_TX_PIN), GPIO_PIN_INTR_DISABLE);

    uint8_t result{0};

    // Wait till we are in the middle of Startbit
    ets_delay_us(DELAY_BIT * 500);
    if (GPIO_INPUT_GET(TTY_TX_PIN) == 1) {
        for (int i = 0; i < 5; i++) {
            ets_delay_us(DELAY_BIT * 1000);
            result += ((1 - GPIO_INPUT_GET(TTY_TX_PIN)) << i);
        }
        ets_delay_us(DELAY_BIT * 3000);
    } else {
        ESP_LOGE(TAG, "ERROR! Start bit not 0! False trigger?");
    }

    char ret = static_cast<char>(tolower(convert_baudot_char_to_ascii(result)));

    // re-enable the interrupt
    gpio_pin_intr_state_set(GPIO_ID_PIN(TTY_TX_PIN), GPIO_PIN_INTR_POSEDGE);
    tty_rx = false;                                      // Reset flag after processing
    return ret;
}

void Teletype::print_ascii_character_to_tty(char c) {
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
                ESP_LOGE(TAG, "ERROR: maximum characters on paper exceeded, printing newline");
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

    ESP_LOGD(TAG, "CHAR = '%c'", c);

    if (c == ASCII_ETX) {
        ESP_LOGD(TAG, "Char was ctrl + c: not printing this one to paper");
        found = true;
    }

    for (int i = 0; i < NUMBER_OF_BAUDOT_CHARS && !found; i++) {
        if (baudot_alphabet[i].mode_letter == c) {
            bd_char.bitcode = baudot_alphabet[i].bitcode;
            bd_char.mode = MODE_LETTER;
            found = true;
            ESP_LOGD(TAG, "LETTER %c", baudot_alphabet[i].mode_letter);
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
            ESP_LOGD(TAG, "NUMBER %c", baudot_alphabet[i].mode_number);
        }
        if (found) {
            bd_char.cc_action = baudot_alphabet[i].cc_action;
        }
    }

    if (!found) {
        ESP_LOGD(TAG, "letter not found in alphabet, printing nothing (0x00)");
        // bd_char = this->convert_ascii_character_to_baudot(' '); // TODO: avoid unneccesary mode change when printing space
        bd_char.mode = MODE_BOTH_POSSIBLE;
    }

    ESP_LOGD(TAG, "BITCODE = '%x'", bd_char.bitcode);
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
                            ESP_LOGE(TAG, "ERROR: state unknown, returning 0");
                    }
                }
            }
            break;
    }

    if (ret == ASCII_ETX) {
        ESP_LOGI(TAG, "CTRL + C pressed");
    }
    return ret;
}

void Teletype::print_all_characters() {
    print_baudot_char_t bd_char;

    for (auto& i : baudot_alphabet) {
        bd_char.bitcode = i.bitcode;
        bd_char.mode = MODE_LETTER;
        print_to_tty(bd_char);
        vTaskDelay(pdMS_TO_TICKS(DELAY_STOPBIT));
    }

    for (auto& i : baudot_alphabet) {
        bd_char.bitcode = i.bitcode;
        bd_char.mode = MODE_NUMBER;
        print_to_tty(bd_char);
        vTaskDelay(pdMS_TO_TICKS(DELAY_STOPBIT));
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