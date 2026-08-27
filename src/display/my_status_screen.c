#include <zephyr/sys/util.h>

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#error "Custom status screen must be built ONLY for the CENTRAL half"
#endif

#include <lvgl.h>
#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#include <zmk/display/widgets/layer_status.h>
#include <zmk/display/widgets/output_status.h>

static struct zmk_widget_output_status w_output;
static struct zmk_widget_layer_status w_layer;

/* battery labels */
static lv_obj_t *lbl_batt_l = NULL;
static lv_obj_t *lbl_batt_r = NULL;

/* local + right battery state */
static volatile uint8_t batt_left = 0;
static volatile bool batt_left_valid = false;

static volatile uint8_t batt_right = 0;
static volatile bool batt_right_valid = false;
static volatile int64_t batt_right_last_ms = 0;

static int64_t screen_started_ms = 0;
#define RIGHT_BATT_STARTUP_GRACE_MS 30000 /* 30s */
#define RIGHT_BATT_TIMEOUT_MS 300000      /* 5min */

/* fonts */
extern const lv_font_t lv_font_montserrat_16;
extern const lv_font_t lv_font_unscii_8;

/* style helper */
static void flat(lv_obj_t *o) {
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
}

static void no_layout(lv_obj_t *o) {
  lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(o, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(o, 0, LV_PART_MAIN);
}

static void label_flat(lv_obj_t *o) {
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(o, 0, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(o, 0, LV_PART_MAIN);
}

static void set_font(lv_obj_t *o, const lv_font_t *f) {
  if (!o)
    return;
  lv_obj_set_style_text_font(o, f, LV_PART_MAIN);
}

/* "L:--%" / "R:100%" */
static void set_batt_text(lv_obj_t *lbl, char prefix, bool valid, uint8_t soc) {
  if (!lbl)
    return;

  if (!valid) {
    char tmp[6];
    tmp[0] = prefix;
    tmp[1] = ':';
    tmp[2] = '-';
    tmp[3] = '-';
    tmp[4] = '%';
    tmp[5] = '\0';
    lv_label_set_text(lbl, tmp);
    return;
  }

  char buf[7];
  buf[0] = prefix;
  buf[1] = ':';

  if (soc >= 100) {
    buf[2] = '1';
    buf[3] = '0';
    buf[4] = '0';
    buf[5] = '%';
    buf[6] = '\0';
  } else {
    buf[2] = '0' + (soc / 10);
    buf[3] = '0' + (soc % 10);
    buf[4] = '%';
    buf[5] = '\0';
  }

  lv_label_set_text(lbl, buf);
}

static void refresh_labels_now(void) {
  /* left */
  set_batt_text(lbl_batt_l, 'L', batt_left_valid, batt_left);

  /* right with timeout */
  int64_t now = k_uptime_get();
  bool in_grace = (screen_started_ms != 0) &&
                  ((now - screen_started_ms) < RIGHT_BATT_STARTUP_GRACE_MS);
  bool r_valid = batt_right_valid && (in_grace || (now - batt_right_last_ms) <
                                                      RIGHT_BATT_TIMEOUT_MS);
  set_batt_text(lbl_batt_r, 'R', r_valid, batt_right);
}

/* ---- UI update: event-driven (no periodic polling) ---- */

static void batt_ui_work_fn(struct k_work *work);
K_WORK_DEFINE(batt_ui_work, batt_ui_work_fn);

static void batt_ui_work_fn(struct k_work *work) {
  ARG_UNUSED(work);
  refresh_labels_now();
}

/* right-side timeout: fires once after last right update */
static void right_timeout_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(right_timeout_work, right_timeout_work_fn);

static void right_timeout_work_fn(struct k_work *work) {
  ARG_UNUSED(work);

  /* If no right update for timeout window, mark invalid and refresh UI */
  int64_t now = k_uptime_get();
  if (!batt_right_valid ||
      (now - batt_right_last_ms) >= RIGHT_BATT_TIMEOUT_MS) {
    k_work_submit(&batt_ui_work);
  }
}

lv_obj_t *zmk_display_status_screen(void) {
  lv_obj_t *root = lv_scr_act();
  lv_obj_clean(root);

  lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);

  /* TOP */
  lv_obj_t *top = lv_obj_create(root);
  lv_obj_set_size(top, LV_HOR_RES, 16);
  lv_obj_align(top, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
  flat(top);
  no_layout(top);

  /* output widget (icons -> 16) */
  zmk_widget_output_status_init(&w_output, top);
  lv_obj_t *o = zmk_widget_output_status_obj(&w_output);
  if (o) {
    lv_obj_align(o, LV_ALIGN_LEFT_MID, 0, 0);
    set_font(o, &lv_font_montserrat_16);
  }

  /* battery area on the right (2 lines) */
  lv_obj_t *bats_txt = lv_obj_create(top);
  lv_obj_set_size(bats_txt, 52, 16);
  lv_obj_align(bats_txt, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_clear_flag(bats_txt, LV_OBJ_FLAG_SCROLLABLE);
  flat(bats_txt);
  no_layout(bats_txt);

  lbl_batt_l = lv_label_create(bats_txt);
  if (lbl_batt_l) {
    label_flat(lbl_batt_l);
    set_font(lbl_batt_l, &lv_font_unscii_8);
    lv_label_set_long_mode(lbl_batt_l, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_batt_l, 52);
    lv_obj_set_height(lbl_batt_l, 8);
    lv_obj_set_pos(lbl_batt_l, 0, 0);
    lv_label_set_text(lbl_batt_l, "L:--%");
  }

  lbl_batt_r = lv_label_create(bats_txt);
  if (lbl_batt_r) {
    label_flat(lbl_batt_r);
    set_font(lbl_batt_r, &lv_font_unscii_8);
    lv_label_set_long_mode(lbl_batt_r, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_batt_r, 52);
    lv_obj_set_height(lbl_batt_r, 8);
    lv_obj_set_pos(lbl_batt_r, 0, 8);
    lv_label_set_text(lbl_batt_r, "R:--%");
  }

  /* BOTTOM */
  lv_obj_t *bottom = lv_obj_create(root);
  lv_obj_set_size(bottom, LV_HOR_RES, 16);
  lv_obj_align(bottom, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
  flat(bottom);

  zmk_widget_layer_status_init(&w_layer, bottom);
  lv_obj_t *ly = zmk_widget_layer_status_obj(&w_layer);
  if (ly) {
    lv_obj_align(ly, LV_ALIGN_LEFT_MID, 0, 0);
    set_font(ly, &lv_font_montserrat_16);
  }

  /* initial paint (no polling) */
  screen_started_ms = k_uptime_get();
  refresh_labels_now();

  /* if we already have right data, arm timeout once */
  if (batt_right_valid) {
    k_work_reschedule(&right_timeout_work, K_MSEC(RIGHT_BATT_TIMEOUT_MS));
  }

  return root;
}

/* local battery event */
static int batt_left_listener(const zmk_event_t *eh) {
  const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
  if (!ev)
    return ZMK_EV_EVENT_BUBBLE;

  batt_left = ev->state_of_charge;
  batt_left_valid = true;

  k_work_submit(&batt_ui_work);
  return ZMK_EV_EVENT_BUBBLE;
}

/* right battery event */
static int batt_right_listener(const zmk_event_t *eh) {
  const struct zmk_peripheral_battery_state_changed *ev =
      as_zmk_peripheral_battery_state_changed(eh);

  if (!ev)
    return ZMK_EV_EVENT_BUBBLE;

  batt_right = ev->state_of_charge;
  batt_right_valid = true;
  batt_right_last_ms = k_uptime_get();

  /* update UI once */
  k_work_submit(&batt_ui_work);

  /* arm/refresh timeout */
  k_work_reschedule(&right_timeout_work, K_MSEC(RIGHT_BATT_TIMEOUT_MS));

  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(batt_l, batt_left_listener);
ZMK_SUBSCRIPTION(batt_l, zmk_battery_state_changed);

ZMK_LISTENER(batt_r, batt_right_listener);
ZMK_SUBSCRIPTION(batt_r, zmk_peripheral_battery_state_changed);
