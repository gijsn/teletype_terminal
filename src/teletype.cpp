// system includes
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <gpio.h>
#include <rom/ets_sys.h>

#include <cstring>

// local includes
#include "semphr.h"
#include "stream_manager.h"
#include "teletype.h"
namespace {
constexpr const char TAG[] = "TTY";
}  // namespace

static TaskHandle_t tty_rx_task_handle = nullptr;
extern StreamManager stream_manager;
extern SemaphoreHandle_t cmd_mutex_stream;

// handle the incoming bits from the teletype, triggered by the GPIO interrupt
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    if (tty_rx_task_handle != nullptr) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(tty_rx_task_handle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken != pdFALSE) {
            portYIELD_FROM_ISR();
        }
    }
}

static void tty_rx_task(void* pvParameters) {
    Teletype* self = static_cast<Teletype*>(pvParameters);
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (self != nullptr) {
            self->read_rx_bits_tty();
        }
    }
}

Teletype::Teletype(uint8_t baudrate, uint8_t rx_pin, uint8_t tx_pin, uint8_t max_chars) {
    // Store instance for ISR

    // initialize teletype values (baudrate etc)
    TTY_BAUDRATE = baudrate;
    DELAY_BIT = (1000 / TTY_BAUDRATE);  // in milliseconds

    DELAY_STOPBIT = DELAY_BIT * 1.5;
    TTY_MAX_CHARS_PAPER = max_chars;
    TTY_RX_PIN = static_cast<gpio_num_t>(rx_pin);
    TTY_TX_PIN = static_cast<gpio_num_t>(tx_pin);

    // Configure GPIO pins
    // Set RX pin as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (uint32_t)(1ULL << TTY_TX_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(TTY_TX_PIN, 1);       // Set RX high initially
    io_conf = {
        .pin_bit_mask = (uint32_t)(1ULL << TTY_RX_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // Enable interrupt for RX pin
    };
    gpio_config(&io_conf);

    kb_mode = MODE_UNKNOWN;
    pr_mode = MODE_UNKNOWN;
    vTaskDelay(pdMS_TO_TICKS(DELAY_BIT * 5));
    shift_to_letters();
    vTaskDelay(pdMS_TO_TICKS(DELAY_BIT * 5));

    // initialize with CR + LF
    print_ascii_character_to_tty('\r');
    print_ascii_character_to_tty('\n');
    characters_on_paper = 0;

    if (xTaskCreate(tty_rx_task, "tty_rx", 4096, this, 5, &tty_rx_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TTY RX task");
        tty_rx_task_handle = nullptr;
    }

    // Set up GPIO interrupt for RX pin (falling edge)
    gpio_isr_handler_add(TTY_RX_PIN, gpio_isr_handler, nullptr);

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

void Teletype::set_baudrate(uint8_t baudrate) {
    TTY_BAUDRATE = baudrate;
    DELAY_BIT = (1000 / TTY_BAUDRATE);  // in milliseconds
    DELAY_STOPBIT = DELAY_BIT * 1.5;
}

void Teletype::set_rx_polarity(bool polarity) {
}

void Teletype::set_tx_polarity(bool polarity) {
}

void Teletype::tx_bits_to_tty(uint8_t bits) {
    ESP_LOGD(TAG, "Write: %x, delay %d", bits, DELAY_BIT);
    bool tx_bit{false};

    // Startbit, TODO: check polarity
    gpio_set_level(TTY_RX_PIN, 0);  // Start bit is LOW
    vTaskDelay(pdMS_TO_TICKS(DELAY_BIT));

    for (int i = 0; i < NUMBER_OF_BITS; i++) {
        tx_bit = (bits & (1 << i));
        gpio_set_level(TTY_TX_PIN, tx_bit);
        vTaskDelay(pdMS_TO_TICKS(DELAY_BIT));
    }

    // Stopbit
    gpio_set_level(TTY_RX_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(DELAY_STOPBIT));
    gpio_set_level(TTY_RX_PIN, 0);
    ESP_LOGD(TAG, "Done write: %x", bits);
}

uint8_t Teletype::read_rx_bits_tty() {
    // disable interrupt while sampling
    gpio_isr_handler_remove(TTY_RX_PIN);

    uint8_t result{0};
    /*
    Pattern:
    Startbit (1) | bit0 | bit1 | bit2 | bit3 | bit4 | Stopbit (1.5)
    ______        ______               _____________           ______
          |______|      |_____________|             |_________|
    */
    // Wait till we are in the middle of Startbit
    vTaskDelay(pdMS_TO_TICKS(DELAY_BIT * 500));
    if (gpio_get_level(TTY_RX_PIN) == 1) {
        for (int i = 0; i < 5; i++) {
            vTaskDelay(pdMS_TO_TICKS(DELAY_BIT * 1000));
            result += ((1 - gpio_get_level(TTY_TX_PIN)) << i);
        }
        vTaskDelay(pdMS_TO_TICKS(DELAY_BIT * 1500));
    } else {
        ESP_LOGE(TAG, "ERROR! Start bit not 0! False trigger?");
    }

    char ret = static_cast<char>(tolower(convert_baudot_char_to_ascii(result)));
    xSemaphoreTake(cmd_mutex_stream, portMAX_DELAY);
    stream_manager.publish(ret);
    xSemaphoreGive(cmd_mutex_stream);
    // re-enable the interrupt
    gpio_isr_handler_add(TTY_RX_PIN, gpio_isr_handler, nullptr);

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
                ESP_LOGW(TAG, "ERROR: maximum characters on paper exceeded, printing newline");
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