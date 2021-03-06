#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include <gde.h>
#ifdef DEPG0290B1
#include <depg0290b1.h>
#else
#include <gdeh029a1.h>
#endif
#include <pictures.h>

#include "badge_pins.h"
#include "badge_i2c.h"
#include "badge_portexp.h"
#include "badge_touch.h"
#include "badge_leds.h"
#include "badge_eink.h"

#include "imgv2_sha.h"
#include "imgv2_menu.h"
#include "imgv2_nick.h"
#include "imgv2_weather.h"
#include "imgv2_test.h"

esp_err_t event_handler(void *ctx, system_event_t *event) { return ESP_OK; }

uint32_t
get_buttons(void)
{
	uint32_t bits = 0;
#ifdef CONFIG_SHA_BADGE_V1
	bits |= gpio_get_level(PIN_NUM_BUTTON_A)     <<  0; // A
	bits |= gpio_get_level(PIN_NUM_BUTTON_B)     <<  1; // B
	bits |= gpio_get_level(PIN_NUM_BUTTON_MID)   <<  2; // MID
	bits |= gpio_get_level(PIN_NUM_BUTTON_UP)    <<  3; // UP
	bits |= gpio_get_level(PIN_NUM_BUTTON_DOWN)  <<  4; // DOWN
	bits |= gpio_get_level(PIN_NUM_BUTTON_LEFT)  <<  5; // LEFT
	bits |= gpio_get_level(PIN_NUM_BUTTON_RIGHT) <<  6; // RIGHT
#else // CONFIG_SHA_BADGE_V2
	bits |= gpio_get_level(PIN_NUM_BUTTON_FLASH) <<  7; // FLASH
#endif // CONFIG_SHA_BADGE_V1
	return bits;
}

#include "event_queue.h"

uint32_t buttons_state = 0;

void gpio_intr_buttons(void *arg) {
  // read status to get interrupt status for GPIO 0-31
  uint32_t gpio_intr_status_lo = READ_PERI_REG(GPIO_STATUS_REG);
  // read status to get interrupt status for GPIO 32-39
  uint32_t gpio_intr_status_hi = READ_PERI_REG(GPIO_STATUS1_REG);
  // clear intr for GPIO 0-31
  SET_PERI_REG_MASK(GPIO_STATUS_W1TC_REG, gpio_intr_status_lo);
  // clear intr for GPIO 32-39
  SET_PERI_REG_MASK(GPIO_STATUS1_W1TC_REG, gpio_intr_status_hi);

  uint32_t buttons_new = get_buttons();
  uint32_t buttons_down = (~buttons_new) & buttons_state;
  buttons_state = buttons_new;

  if (buttons_down != 0)
    xQueueSendFromISR(evt_queue, &buttons_down, NULL);

  if (buttons_down & (1 << 0))
    ets_printf("Button A\n");
  if (buttons_down & (1 << 1))
    ets_printf("Button B\n");
  if (buttons_down & (1 << 2))
    ets_printf("Button MID\n");
  if (buttons_down & (1 << 3))
    ets_printf("Button UP\n");
  if (buttons_down & (1 << 4))
    ets_printf("Button DOWN\n");
  if (buttons_down & (1 << 5))
    ets_printf("Button LEFT\n");
  if (buttons_down & (1 << 6))
    ets_printf("Button RIGHT\n");
  if (buttons_down & (1 << 7))
    ets_printf("Button FLASH\n");
}

#ifdef CONFIG_SHA_BADGE_V2
void
touch_event_handler(int event)
{
	// convert into button queue event
	if (((event >> 16) & 0x0f) == 0x0) { // button down event
		static const int conv[12] =
		{ -1, -1, 5, 3, 6, 4, -1, 0, 2, 1, -1, 0 };
		if (((event >> 8) & 0xff) < 12) {
			int id = conv[(event >> 8) & 0xff];
			if (id != -1)
			{
				uint32_t buttons_down = 1<<id;
				xQueueSend(evt_queue, &buttons_down, 0);
			}
		}
	}
}
#endif // CONFIG_SHA_BADGE_V2

struct menu_item {
  const char *title;
  void (*handler)(void);
};

#include "demo_text1.h"
#include "demo_text2.h"
#include "demo_greyscale1.h"
#include "demo_greyscale2.h"
#include "demo_greyscale_img1.h"
#include "demo_greyscale_img2.h"
#include "demo_greyscale_img3.h"
#include "demo_greyscale_img4.h"
#include "demo_partial_update.h"
#include "demo_dot1.h"
#include "demo_test_adc.h"
#include "demo_leds.h"

const struct menu_item demoMenu[] = {
    {"text demo 1", &demoText1},
    {"text demo 2", &demoText2},
    {"greyscale 1", &demoGreyscale1},
    {"greyscale 2", &demoGreyscale2},
    {"greyscale image 1", &demoGreyscaleImg1},
    {"greyscale image 2", &demoGreyscaleImg2},
    {"greyscale image 3", &demoGreyscaleImg3},
    {"greyscale image 4", &demoGreyscaleImg4},
    {"partial update test", &demoPartialUpdate},
    {"dot 1", &demoDot1},
    {"ADC test", &demoTestAdc},
#ifdef CONFIG_SHA_BADGE_V2
    {"LEDs demo", &demo_leds},
#endif // CONFIG_SHA_BADGE_V2
    {"tetris?", NULL},
    {"something else", NULL},
    {"test, test, test", NULL},
    {"another item..", NULL},
    {"dot 2", NULL},
    {"dot 3", NULL},
    {NULL, NULL},
};

#define MENU_UPDATE_CYCLES 8
void displayMenu(const char *menu_title, const struct menu_item *itemlist) {
  int num_items = 0;
  while (itemlist[num_items].title != NULL)
    num_items++;

  int scroll_pos = 0;
  int item_pos = 0;
  int num_draw = 0;
  while (1) {
    TickType_t xTicksToWait = portMAX_DELAY;

    /* draw menu */
    if (num_draw < 2) {
      // init buffer
      drawText(14, 0, DISP_SIZE_Y, menu_title,
               FONT_16PX | FONT_INVERT | FONT_FULL_WIDTH | FONT_UNDERLINE_2);
      int i;
      for (i = 0; i < 7; i++) {
        int pos = scroll_pos + i;
        drawText(12 - 2 * i, 0, DISP_SIZE_Y,
                 (pos < num_items) ? itemlist[pos].title : "",
                 FONT_16PX | FONT_FULL_WIDTH |
                     ((pos == item_pos) ? 0 : FONT_INVERT));
      }
    }
    if (num_draw == 0) {
      // init LUT
      static const uint8_t lut[30] = {
          0x99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
          0,    0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      };
      gdeWriteCommandStream(0x32, lut, 30);
      // init timings
      gdeWriteCommand_p1(0x3a, 0x02); // 2 dummy lines per gate
      gdeWriteCommand_p1(0x3b, 0x00); // 30us per line
      //			gdeWriteCommand_p1(0x3a, 0x1a); // 26 dummy
      //lines per gate
      //			gdeWriteCommand_p1(0x3b, 0x08); // 62us per line
    }
    if (num_draw < MENU_UPDATE_CYCLES) {
      updateDisplay();
      gdeBusyWait();
      num_draw++;
      if (num_draw < MENU_UPDATE_CYCLES)
        xTicksToWait = 0;
    }

    /* handle input */
    uint32_t buttons_down;
    if (xQueueReceive(evt_queue, &buttons_down, xTicksToWait)) {
      if (buttons_down & (1 << 1)) {
        ets_printf("Button B handling\n");
        return;
      }
      if (buttons_down & (1 << 2)) {
        ets_printf("Selected '%s'\n", itemlist[item_pos].title);
        if (itemlist[item_pos].handler != NULL)
          itemlist[item_pos].handler();
        num_draw = 0;
        ets_printf("Button MID handled\n");
        continue;
      }
      if (buttons_down & (1 << 3)) {
        if (item_pos > 0) {
          item_pos--;
          if (scroll_pos > item_pos)
            scroll_pos = item_pos;
          num_draw = 0;
        }
        ets_printf("Button UP handled\n");
      }
      if (buttons_down & (1 << 4)) {
        if (item_pos + 1 < num_items) {
          item_pos++;
          if (scroll_pos + 6 < item_pos)
            scroll_pos = item_pos - 6;
          num_draw = 0;
        }
        ets_printf("Button DOWN handled\n");
      }
    }
  }
}

void
app_main(void) {
	nvs_flash_init();

	// install isr-service, so we can register interrupt-handlers per
	// gpio pin.
	gpio_install_isr_service(0);

	/* configure buttons input */
	evt_queue = xQueueCreate(10, sizeof(uint32_t));
#ifdef CONFIG_SHA_BADGE_V1
	gpio_isr_handler_add(PIN_NUM_BUTTON_A    , gpio_intr_buttons, NULL);
	gpio_isr_handler_add(PIN_NUM_BUTTON_B    , gpio_intr_buttons, NULL);
	gpio_isr_handler_add(PIN_NUM_BUTTON_MID  , gpio_intr_buttons, NULL);
	gpio_isr_handler_add(PIN_NUM_BUTTON_UP   , gpio_intr_buttons, NULL);
	gpio_isr_handler_add(PIN_NUM_BUTTON_DOWN , gpio_intr_buttons, NULL);
	gpio_isr_handler_add(PIN_NUM_BUTTON_LEFT , gpio_intr_buttons, NULL);
	gpio_isr_handler_add(PIN_NUM_BUTTON_RIGHT, gpio_intr_buttons, NULL);
#else
	gpio_isr_handler_add(PIN_NUM_BUTTON_FLASH, gpio_intr_buttons, NULL);
#endif // CONFIG_SHA_BADGE_V1

	// configure button-listener
	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_INTR_ANYEDGE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask =
#ifdef CONFIG_SHA_BADGE_V1
		(1LL << PIN_NUM_BUTTON_A) |
		(1LL << PIN_NUM_BUTTON_B) |
		(1LL << PIN_NUM_BUTTON_MID) |
		(1LL << PIN_NUM_BUTTON_UP) |
		(1LL << PIN_NUM_BUTTON_DOWN) |
		(1LL << PIN_NUM_BUTTON_LEFT) |
		(1LL << PIN_NUM_BUTTON_RIGHT) |
#else
		(1LL << PIN_NUM_BUTTON_FLASH) |
#endif // CONFIG_SHA_BADGE_V1
		0LL;
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 1;
	gpio_config(&io_conf);

#ifdef CONFIG_SHA_BADGE_V2
  //badge_i2c_init();
  //badge_portexp_init();
  //badge_touch_init();
  //badge_touch_set_event_handler(touch_event_handler);
  //badge_leds_init();
#endif // CONFIG_SHA_BADGE_V2

  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

#ifdef CONFIG_WIFI_USE
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  wifi_config_t sta_config = {
      .sta = {.ssid = CONFIG_WIFI_SSID, .password = CONFIG_WIFI_PASSWORD, .bssid_set = false}};
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());
#endif // CONFIG_WIFI_USE

  gdeInit();
  initDisplay(LUT_DEFAULT); // configure slow LUT

  int picture_id = 0;
	ets_printf("Drawimage begin\n");
  drawImage(pictures[picture_id]);
	ets_printf("Drawimage gedaan");
  updateDisplay();
  gdeBusyWait();

  //int selected_lut = LUT_PART;
  //writeLUT(selected_lut); // configure fast LUT

  while (1) {

        drawImage(pictures[picture_id]);
        updateDisplay();
        gdeBusyWait();


        if (picture_id + 1 < NUM_PICTURES) {
          picture_id++;
        } else {
					picture_id=0;
				}
				ets_delay_us(5000000);
      }
}
