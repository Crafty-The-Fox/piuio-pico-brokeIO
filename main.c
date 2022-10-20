/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

/* This example demonstrates WebUSB as web serial with browser with WebUSB support (e.g Chrome).
 * After enumerated successfully, browser will pop-up notification
 * with URL to landing page, click on it to test
 *  - Click "Connect" and select device, When connected the on-board LED will litted up.
 *  - Any charters received from either webusb/Serial will be echo back to webusb and Serial
 *
 * Note:
 * - The WebUSB landing page notification is currently disabled in Chrome
 * on Windows due to Chromium issue 656702 (https://crbug.com/656702). You have to
 * go to landing page (below) to test
 *
 * - On Windows 7 and prior: You need to use Zadig tool to manually bind the
 * WebUSB interface with the WinUSB driver for Chrome to access. From windows 8 and 10, this
 * is done automatically by firmware.
 *
 * - On Linux/macOS, udev permission may need to be updated by
 *   - copying '/examples/device/99-tinyusb.rules' file to /etc/udev/rules.d/ then
 *   - run 'sudo udevadm control --reload-rules && sudo udevadm trigger'
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// PIN CONFIGURATION
//--------------------------------------------------------------------+

// Set pins for the switches/LEDs
// Order of DL, UL, C, UR, DR for each player, then test and service switches
const uint8_t pinSwitch[12] = {19, 21, 10, 6, 8, 17, 27, 2, 0, 4, 15, 14};
const uint8_t pinLED[10] = {18, 20, 11, 7, 9, 16, 26, 3, 1, 5};
const uint8_t pinNEO = 22;


//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED     = 1000,
  BLINK_SUSPENDED   = 2500,

  BLINK_ALWAYS_ON   = UINT32_MAX,
  BLINK_ALWAYS_OFF  = 0
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// PIUIO input and output data
uint8_t inputData[8];
uint8_t lampData[8];

//------------- prototypes -------------//
void led_blinking_task(void);
void cdc_task(void);
void webserial_task(void);

/*------------- MAIN -------------*/
int main(void)
{
  board_init();

  // Set up GPIO pins: Inputs first, then outputs
  for (int i = 0; i < 12; i++) {
    gpio_init(pinSwitch[i]);
    gpio_set_dir(pinSwitch[i], false);
    gpio_pull_up(pinSwitch[i]);
  }

  for (int i = 0; i < 10; i++) {
    gpio_init(pinLED[i]);
    gpio_set_dir(pinLED[i], true);
  }

  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);


  // Main loop
  while (1)
  {
    tud_task(); // tinyusb device task
    piuio_task();
    led_blinking_task();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

//--------------------------------------------------------------------+
// WebUSB use vendor class
//--------------------------------------------------------------------+

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  // nothing to with DATA & ACK stage
  if (stage != CONTROL_STAGE_SETUP) return true;

  // Request 0xAE = IO Time
  if (request->bRequest == 0xAE) {
    switch (request->bmRequestType_bit.type)
    {
      case 0x40: // Received output data
        // I have a feeling we may need to receive the data from another state (I.E. remove the stage setup check, above)
        return true;

      case 0xC0: // Requesting input data
        return tud_control_xfer(rhport, request, (void*)(uintptr_t) inputData, 8);

      default: break;
    }
  }

  // stall unknown request
  return false;
}

// Renamed from webusb_task, do we actually need this?
void piuio_task(void)
{
    // Read our switch inputs into the game-ready inputData array
    // P1
    bool input = gpio_get(pinSwitch[0]); if (input) { inputData[0] = tu_bit_set(inputData[0], 3); } else { inputData[0] = tu_bit_clear(inputData[0], 3); }
    input = gpio_get(pinSwitch[1]); if (input) { inputData[0] = tu_bit_set(inputData[0], 0); } else { inputData[0] = tu_bit_clear(inputData[0], 0); }
    input = gpio_get(pinSwitch[2]); if (input) { inputData[0] = tu_bit_set(inputData[0], 2); } else { inputData[0] = tu_bit_clear(inputData[0], 2); }
    input = gpio_get(pinSwitch[3]); if (input) { inputData[0] = tu_bit_set(inputData[0], 1); } else { inputData[0] = tu_bit_clear(inputData[0], 1); }
    input = gpio_get(pinSwitch[4]); if (input) { inputData[0] = tu_bit_set(inputData[0], 4); } else { inputData[0] = tu_bit_clear(inputData[0], 4); }

    // P2
    input = gpio_get(pinSwitch[5]); if (input) { inputData[2] = tu_bit_set(inputData[2], 3); } else { inputData[2] = tu_bit_clear(inputData[2], 3); }
    input = gpio_get(pinSwitch[6]); if (input) { inputData[2] = tu_bit_set(inputData[2], 0); } else { inputData[2] = tu_bit_clear(inputData[2], 0); }
    input = gpio_get(pinSwitch[7]); if (input) { inputData[2] = tu_bit_set(inputData[2], 2); } else { inputData[2] = tu_bit_clear(inputData[2], 2); }
    input = gpio_get(pinSwitch[8]); if (input) { inputData[2] = tu_bit_set(inputData[2], 1); } else { inputData[2] = tu_bit_clear(inputData[2], 1); }
    input = gpio_get(pinSwitch[9]); if (input) { inputData[2] = tu_bit_set(inputData[2], 4); } else { inputData[2] = tu_bit_clear(inputData[2], 4); }

    // Test/Service
    input = gpio_get(pinSwitch[10]); if (input) { inputData[1] = tu_bit_set(inputData[1], 1); } else { inputData[1] = tu_bit_clear(inputData[1], 1); }
    input = gpio_get(pinSwitch[11]); if (input) { inputData[1] = tu_bit_set(inputData[1], 2); } else { inputData[1] = tu_bit_clear(inputData[1], 2); }

    // Check if we've received... data..?
    if ( tud_vendor_available() )
    {
      uint8_t buf[64];
      uint32_t count = tud_vendor_read(buf, sizeof(buf));

      // echo back to both web serial and cdc
      //echo_all(buf, count);
    }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
