#include <stddef.h>

#ifdef NATIVE_TEST

#include "serial_input.h"

void serial_request_wifi_password(const char *ssid) { (void)ssid; }
void serial_request_bt_pair_code(const char *device_name) { (void)device_name; }
void serial_request_bt_pair_code_with_addr(const char *device_name, const char *device_addr) {
    (void)device_name; (void)device_addr;
}
void serial_cancel(void) {}
serial_state_t serial_poll(void) { return SERIAL_STATE_IDLE; }
const char* serial_get_input(void) { return NULL; }
const char* serial_get_target_name(void) { return NULL; }
const char* serial_get_target_addr(void) { return NULL; }

#else

#include "serial_input.h"
#include <Arduino.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define INPUT_BUFFER_SIZE   65   /* 64 chars + null terminator */
#define PASSWORD_MAX_LEN    64
#define PAIR_CODE_MAX_LEN   16
#define TIMEOUT_MS          30000 /* 30 seconds */

// ---------------------------------------------------------------------------
// Module state (file-scoped, immutable from the outside)
// ---------------------------------------------------------------------------

static serial_state_t state          = SERIAL_STATE_IDLE;
static char           input_buffer[INPUT_BUFFER_SIZE];
static size_t         input_len      = 0;
static char           target_name[64]; // SSID or BT device name
static char           target_addr[18]; // BT MAC address (for pairing)
static uint32_t       wait_start_ms  = 0;
static bool           input_consumed = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void clear_buffer(void)
{
    input_buffer[0] = '\0';
    input_len       = 0;
    input_consumed  = false;
}

static size_t current_max_len(void)
{
    return (state == SERIAL_STATE_WAITING_PASSWORD) ? PASSWORD_MAX_LEN
                                                    : PAIR_CODE_MAX_LEN;
}

static void enter_waiting_state(serial_state_t waiting_state,
                                const char *name)
{
    strlcpy(target_name, name, sizeof(target_name));
    clear_buffer();
    state         = waiting_state;
    wait_start_ms = millis();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void serial_request_wifi_password(const char *ssid)
{
    Serial.println();
    Serial.print("PASSWORD for ");
    Serial.print(ssid);
    Serial.print(": ");
    Serial.flush();

    enter_waiting_state(SERIAL_STATE_WAITING_PASSWORD, ssid);
}

void serial_request_bt_pair_code(const char *device_name)
{
    Serial.print("PAIR CODE for ");
    Serial.print(device_name);
    Serial.print(": ");
    Serial.flush();

    target_addr[0] = '\0';
    enter_waiting_state(SERIAL_STATE_WAITING_PAIR_CODE, device_name);
}

void serial_request_bt_pair_code_with_addr(const char *device_name, const char *device_addr)
{
    Serial.print("PAIR CODE for ");
    Serial.print(device_name);
    Serial.print(" [");
    Serial.print(device_addr);
    Serial.print("]: ");
    Serial.flush();

    strlcpy(target_addr, device_addr ? device_addr : "", sizeof(target_addr));
    enter_waiting_state(SERIAL_STATE_WAITING_PAIR_CODE, device_name);
}

void serial_cancel(void)
{
    state = SERIAL_STATE_CANCELLED;
    clear_buffer();
}

serial_state_t serial_poll(void)
{
    // --- Auto-transition back to IDLE after input was consumed ---
    if (input_consumed &&
        (state == SERIAL_STATE_PASSWORD_RECEIVED ||
         state == SERIAL_STATE_PAIR_CODE_RECEIVED))
    {
        state = SERIAL_STATE_IDLE;
        return state;
    }

    // --- Only process input in waiting states ---
    if (state != SERIAL_STATE_WAITING_PASSWORD &&
        state != SERIAL_STATE_WAITING_PAIR_CODE)
    {
        return state;
    }

    // --- Timeout check ---
    if ((millis() - wait_start_ms) >= TIMEOUT_MS)
    {
        Serial.println("\n[TIMEOUT]");
        state = SERIAL_STATE_CANCELLED;
        clear_buffer();
        return state;
    }

    // --- Read available bytes one at a time (non-blocking) ---
    while (Serial.available() > 0)
    {
        int raw = Serial.read();
        if (raw < 0) {
            break;
        }

        char c = (char)raw;

        // Enter / Return -> accept input
        if (c == '\n' || c == '\r')
        {
            input_buffer[input_len] = '\0';
            Serial.println(); // echo newline

            state = (state == SERIAL_STATE_WAITING_PASSWORD)
                        ? SERIAL_STATE_PASSWORD_RECEIVED
                        : SERIAL_STATE_PAIR_CODE_RECEIVED;
            return state;
        }

        // Backspace (BS or DEL)
        if (c == '\b' || c == 0x7F)
        {
            if (input_len > 0)
            {
                input_len--;
                input_buffer[input_len] = '\0';
                // Echo backspace sequence: move cursor back, overwrite, move back again
                Serial.print("\b \b");
            }
            continue;
        }

        // Ignore other control characters
        if (c < 0x20) {
            continue;
        }

        // Append printable character if room
        if (input_len < current_max_len())
        {
            input_buffer[input_len++] = c;
            input_buffer[input_len]   = '\0';
            Serial.print('*'); // mask password / code on screen
        }
    }

    return state;
}

const char *serial_get_input(void)
{
    if (state == SERIAL_STATE_PASSWORD_RECEIVED ||
        state == SERIAL_STATE_PAIR_CODE_RECEIVED)
    {
        input_consumed = true;
        return input_buffer;
    }
    return NULL;
}

const char *serial_get_target_name(void)
{
    return target_name;
}

const char *serial_get_target_addr(void)
{
    return target_addr[0] ? target_addr : NULL;
}

#endif /* NATIVE_TEST */
