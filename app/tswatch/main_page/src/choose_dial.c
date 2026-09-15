
#include <string.h>
#include "choose_dial.h"
#include "../../utils/include/font_manager.h"

static lv_style_t circle; // 圆形的通用界面风格
static void click_event_cb(lv_event_t *e)
{
    LV_LOG_USER("click_event_cb in");
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *img = lv_event_get_current_target(e);
    lv_obj_t *parent = lv_obj_get_parent(img);
    const char *buf = lv_event_get_user_data(e);
    LV_LOG_USER("buf = %s", buf);

    switch (code)
    {
    case LV_EVENT_CLICKED:
        lv_obj_delete(parent);
        if (!strcmp(buf, "clock_dial"))
        {
            main_page("clock_dial");
        }
        if (!strcmp(buf, "digital_dial"))
        {
            main_page("digital_dial");
        }
        break;
    default:
        break;
    }
}

void choose_dial(const char *str)
{
    LV_LOG_USER("choose_dial in------");

    lv_style_init(&circle);
    lv_style_set_radius(&circle, 122); // 455的一半，保持圆形
    lv_obj_t *chooseDial = lv_obj_create(get_watch_scr());
    lv_obj_set_style_bg_color(chooseDial, lv_color_black(), 0);
    lv_obj_set_style_radius(chooseDial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(chooseDial, 0, 0);


    lv_obj_set_scrollbar_mode(chooseDial, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(chooseDial, LV_DIR_HOR);  // 允许水平滚动
    lv_obj_set_scroll_snap_x(chooseDial, LV_SCROLL_SNAP_CENTER);  // 设置滚动对齐
    lv_obj_set_flex_flow(chooseDial, LV_FLEX_FLOW_ROW);  // 水平排列

    lv_obj_set_flex_align(chooseDial, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 设置容器大小和布局
    lv_obj_set_size(chooseDial, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_pad_all(chooseDial, 0, 0);

    // 确保手势事件可以冒泡
    lv_obj_clear_flag(chooseDial, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(chooseDial, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *clockDial;
    lv_obj_t *digitalDial;
    lv_obj_t *add_screen;
    if (!strcmp(str, "clock_dial"))
    { // if user choose clock page
        clockDial = lv_obj_create(chooseDial);
        lv_obj_set_style_bg_color(clockDial, lv_color_black(), 0);
        lv_obj_set_scrollbar_mode(clockDial, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(clockDial, 250, 280);
        //lv_obj_center(clockDial);

        lv_obj_add_flag(clockDial, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(clockDial, click_event_cb, LV_EVENT_CLICKED, "clock_dial");
        // 允许手势传播
        lv_obj_clear_flag(clockDial, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t *clockImg = lv_image_create(clockDial);
        LV_IMAGE_DECLARE(clock_dial_choose);
        //lv_image_set_src(clockImg, &clock_dial_choose);
        //lv_image_set_src(clockImg, "/emmc/main_page/clock_dial_choose.png");
        lv_image_set_src(clockImg, "/emmc/clock_dial_choose.png");
        //lv_image_set_src(clockImg, "A:/emmc/main_page/clock_dial_choose.png");
        lv_obj_center(clockImg);

        lv_obj_t *clockLabel = lv_label_create(clockDial);
        lv_label_set_text(clockLabel, "ClockDial");
        lv_obj_set_style_text_color(clockLabel, lv_color_white(), 0);
        lv_obj_set_style_border_width(clockDial, 0, 0);
        lv_obj_align_to(clockLabel, clockImg, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

        digitalDial = lv_obj_create(chooseDial);
        lv_obj_set_style_bg_color(digitalDial, lv_color_black(), 0);
        lv_obj_set_scrollbar_mode(digitalDial, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(digitalDial, 250, 280);
        //lv_obj_align_to(digitalDial, clockDial, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
        lv_obj_add_flag(digitalDial, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(digitalDial, click_event_cb, LV_EVENT_CLICKED, "digital_dial");

        lv_obj_clear_flag(digitalDial, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *digitalImg = lv_image_create(digitalDial);
        LV_IMAGE_DECLARE(digital_dial_choose);
        //lv_image_set_src(digitalImg, &digital_dial_choose);
        //lv_image_set_src(digitalImg, "/emmc/main_page/digital_dial_choose.png");
        lv_image_set_src(digitalImg, "/emmc/digital_dial_choose.png");
        //lv_image_set_src(digitalImg, "A:/emmc/main_page/digital_dial_choose.png");
        lv_obj_center(digitalImg);

        lv_obj_t *digitalLabel = lv_label_create(digitalDial);
        lv_label_set_text(digitalLabel, "DigitalDial");
        lv_obj_set_style_text_color(digitalLabel, lv_color_white(), 0);
        lv_obj_set_style_border_width(digitalDial, 0, 0);
        lv_obj_align_to(digitalLabel, digitalImg, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

        add_screen = lv_obj_create(chooseDial);
        // lv_obj_add_style(add_screen, &style_face, 0);
        lv_obj_set_size(add_screen, 250, 280);
        lv_obj_set_scrollbar_mode(add_screen, LV_SCROLLBAR_MODE_OFF);
        lv_style_set_clip_corner(&circle, true);
        lv_obj_add_style(add_screen, &circle, 0);
        // set watch face bg color
        lv_obj_set_style_bg_color(add_screen, lv_color_black(), 0);

        lv_obj_clear_flag(add_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *add_btn = lv_btn_create(add_screen);
        lv_obj_set_size(add_btn, 209, 209); // 设置按钮大小
        lv_obj_center(add_btn);
        lv_obj_set_style_border_width(add_btn, 0, 0);

        // 设置按钮背景图片
        lv_obj_t *add_btn_img = lv_img_create(add_btn);
        LV_IMAGE_DECLARE(Add_Face); // 声明加号按钮图片
        //lv_img_set_src(add_btn_img, &Add_Face);
        lv_img_set_src(add_btn_img, "/emmc/Add_Face.png");
        lv_obj_center(add_btn_img);

        lv_obj_t *AddlLabel = lv_label_create(add_screen);
        lv_label_set_text(AddlLabel, "New");
        lv_obj_set_style_text_color(AddlLabel, lv_color_white(), 0);
        lv_obj_set_style_border_width(add_screen, 0, 0);
        lv_obj_align_to(AddlLabel, add_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

        // lv_obj_set_style_text_font(AddlLabel, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_font(AddlLabel, &lv_font_montserrat_14, LV_PART_MAIN);

        //lv_obj_align_to(add_screen, digitalDial, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    }
    if (!strcmp(str, "digital_dial"))
    {
        digitalDial = lv_obj_create(chooseDial);
        lv_obj_set_style_bg_color(digitalDial, lv_color_black(), 0);
        lv_obj_set_scrollbar_mode(digitalDial, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(digitalDial, 250, 280);
        //lv_obj_center(digitalDial);
        lv_obj_add_flag(digitalDial, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(digitalDial, click_event_cb, LV_EVENT_CLICKED, "digital_dial");

        // 允许手势传播
        lv_obj_clear_flag(digitalDial, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *digitalImg = lv_image_create(digitalDial);
        LV_IMAGE_DECLARE(digital_dial_choose);
        //lv_image_set_src(digitalImg, &digital_dial_choose);
        //lv_image_set_src(digitalImg, "/emmc/main_page/clock_dial_choose.png");
        lv_image_set_src(digitalImg, "/emmc/clock_dial_choose.png");
        //lv_image_set_src(digitalImg, "A:/emmc/main_page/clock_dial_choose.png");
        lv_obj_center(digitalImg);

        lv_obj_t *digitalLabel = lv_label_create(digitalDial);
        lv_label_set_text(digitalLabel, "DigitalDial");
        lv_obj_set_style_text_color(digitalLabel, lv_color_white(), 0);
        lv_obj_set_style_border_width(digitalDial, 0, 0);
        lv_obj_align_to(digitalLabel, digitalImg, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

        clockDial = lv_obj_create(chooseDial);
        lv_obj_set_style_bg_color(clockDial, lv_color_black(), 0);
        lv_obj_set_scrollbar_mode(clockDial, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(clockDial, 250, 280);
        //lv_obj_align_to(clockDial, digitalDial, LV_ALIGN_OUT_LEFT_MID, 0, 0);
        lv_obj_add_flag(clockDial, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(clockDial, click_event_cb, LV_EVENT_CLICKED, "clock_dial");

        // 允许手势传播
        lv_obj_clear_flag(clockDial, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *clockImg = lv_image_create(clockDial);
        LV_IMAGE_DECLARE(clock_dial_choose);
        //lv_image_set_src(clockImg, &clock_dial_choose);
        //lv_image_set_src(clockImg, "/emmc/main_page/clock_dial_choose.png");
        lv_image_set_src(clockImg, "/emmc/clock_dial_choose.png");
        //lv_image_set_src(clockImg, "A:/emmc/main_page/clock_dial_choose.png");
        lv_obj_center(clockImg);

        lv_obj_t *clockLabel = lv_label_create(clockDial);
        lv_label_set_text(clockLabel, "ClockDial");
        lv_obj_set_style_text_color(clockLabel, lv_color_white(), 0);
        lv_obj_set_style_border_width(clockDial, 0, 0);
        lv_obj_align_to(clockLabel, clockImg, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

        add_screen = lv_obj_create(chooseDial);
        // lv_obj_add_style(add_screen, &style_face, 0);
        lv_obj_set_size(add_screen, 250, 280);
        lv_obj_set_scrollbar_mode(add_screen, LV_SCROLLBAR_MODE_OFF);
        lv_style_set_clip_corner(&circle, true);
        lv_obj_add_style(add_screen, &circle, 0);
        lv_obj_set_style_border_width(add_screen, 0, 0);
        // set watch face bg color
        lv_obj_set_style_bg_color(add_screen, lv_color_black(), 0);

        // 允许手势传播
        lv_obj_clear_flag(add_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *add_btn = lv_btn_create(add_screen);
        lv_obj_set_size(add_btn, 209, 209); // 设置按钮大小
        lv_obj_center(add_btn);
        lv_obj_set_style_border_width(add_btn, 0, 0);

        // 设置按钮背景图片
        lv_obj_t *add_btn_img = lv_img_create(add_btn);
        LV_IMAGE_DECLARE(Add_Face); // 声明加号按钮图片
        //lv_img_set_src(add_btn_img, &Add_Face);
        lv_img_set_src(add_btn_img, "/emmc/Add_Face.png");
        lv_obj_center(add_btn_img);

        lv_obj_t *AddlLabel = lv_label_create(add_screen);
        lv_label_set_text(AddlLabel, "New");
        lv_obj_set_style_text_color(AddlLabel, lv_color_white(), 0);
        lv_obj_align_to(AddlLabel, add_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

        // lv_obj_set_style_text_font(AddlLabel, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_font(AddlLabel, &lv_font_montserrat_14, LV_PART_MAIN);

        //lv_obj_align_to(add_screen, digitalDial, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    }

    /*origin logic is simulate add_page*/
    // lv_obj_t* plusBtn = lv_button_create(chooseDial);
    // lv_obj_set_size(plusBtn, lv_obj_get_width(digitalDial), lv_obj_get_height(digitalDial));
    // lv_obj_set_style_bg_color(plusBtn, lv_color_black(), 0);
    // lv_obj_align_to(plusBtn, digitalDial, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    // lv_obj_t* plusLabel = lv_label_create(plusBtn);
    // lv_label_set_text(plusLabel, LV_SYMBOL_PLUS);
    // lv_obj_set_style_text_font(plusLabel, &lv_font_montserrat_32, 0);
    // lv_obj_center(plusLabel);

    lv_obj_update_snap(chooseDial, LV_ANIM_ON);
    // 添加调试日志
    LV_LOG_USER("choose_dial created with scroll capability");
}
