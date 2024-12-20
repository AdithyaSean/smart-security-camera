#include "who_app.hpp"
#include "who_cam.hpp"

static const char *TAG = "who_app";

namespace who {
namespace app {
TaskHandle_t WhoHumanFaceRecognition::s_task_handle = nullptr;
SemaphoreHandle_t WhoHumanFaceRecognition::s_mutex = xSemaphoreCreateMutex();
lv_obj_t *WhoHumanFaceRecognition::s_label = nullptr;

void WhoDetect::task(void *args)
{
    WhoDetect *self = (WhoDetect *)args;
    who::lcd::result_t detect_result;
    while (true) {
        who::cam::cam_fb_t *fb = self->m_cam->cam_fb_peek();
        std::list<dl::detect::result_t> &detect_res = self->m_detect->run(who::cam::fb2img(fb));
        detect_result = {.res = {.detect_res = new std::list<dl::detect::result_t>(detect_res)},
                         .timestamp = fb->timestamp,
                         .type = self->m_type};
        xQueueSend(who::lcd::WhoLCD::s_queue_handle, &detect_result, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void WhoDetect::run()
{
    if (xTaskCreatePinnedToCore(task, "WhoDetect", 2560, this, 2, nullptr, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create detect task.\n");
    }
}

void WhoHumanFaceRecognition::event_handle_task(void *args)
{
    WhoHumanFaceRecognition *self = (WhoHumanFaceRecognition *)args;
    while (true) {
        uint32_t event;
        xTaskNotifyWait(0, 0xffffffff, &event, portMAX_DELAY);
        if (event & static_cast<uint32_t>(fr_event_t::RECOGNIZE)) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::RECOGNIZE;
            assert(self->m_status == fr_status_t::RECOGNIZE);
            xSemaphoreGive(s_mutex);
        }
        if (event & static_cast<uint32_t>(fr_event_t::ENROLL)) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::ENROLL;
            assert(self->m_status == fr_status_t::ENROLL);
            xSemaphoreGive(s_mutex);
        }
        if (event & static_cast<uint32_t>(fr_event_t::DELETE)) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::DELETE;
            assert(self->m_status == fr_status_t::DELETE);
            xSemaphoreGive(s_mutex);
        }
    }
}

void WhoHumanFaceRecognition::recognition_task(void *args)
{
    create_btns();
    create_label();
    WhoHumanFaceRecognition *self = (WhoHumanFaceRecognition *)args;
    fr_status_t status;
    who::lcd::result_t detect_result;
    detect_result.type = who::lcd::FACE_DETECT_RESULT_TYPE;
    who::lcd::result_t recognition_result;
    recognition_result.timestamp = {0, 0};
    recognition_result.type = who::lcd::FACE_RECOGNITION_RESULT_TYPE;

    // const TickType_t frame_interval = pdMS_TO_TICKS(200);
    // TickType_t last_wake_time = xTaskGetTickCount();
    std::vector<dl::recognition::result_t> recognition_res;
    while (true) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        status = self->m_status;
        xSemaphoreGive(s_mutex);
        switch (status) {
        case fr_status_t::RECOGNIZE: {
            who::cam::cam_fb_t *fb = self->m_cam->cam_fb_peek();
            dl::image::img_t img = who::cam::fb2img(fb);
            detect_result.timestamp = fb->timestamp;
            auto &detect_res = self->m_detect->run(img);
            detect_result.res.detect_res = new std::list<dl::detect::result_t>(detect_res);
            recognition_res = self->m_recognizer->recognize(img, detect_res);
            char *text = new char[64];
            recognition_result.res.recognition_res = text;
            if (recognition_res.empty()) {
                strcpy(text, "who?");
            } else {
                snprintf(text, 64, "id: %d, sim: %.2f", recognition_res[0].id, recognition_res[0].similarity);
            }
            xQueueSend(who::lcd::WhoLCD::s_queue_handle, &detect_result, portMAX_DELAY);
            xQueueSend(who::lcd::WhoLCD::s_queue_handle, &recognition_result, portMAX_DELAY);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::DETECT;
            xSemaphoreGive(s_mutex);
            break;
        }
        case fr_status_t::ENROLL: {
            who::cam::cam_fb_t *fb = self->m_cam->cam_fb_peek();
            dl::image::img_t img = who::cam::fb2img(fb);
            detect_result.timestamp = fb->timestamp;
            auto &detect_res = self->m_detect->run(img);
            detect_result.res.detect_res = new std::list<dl::detect::result_t>(detect_res);
            esp_err_t ret = self->m_recognizer->enroll(img, detect_res);
            char *text = new char[64];
            recognition_result.res.recognition_res = text;
            if (ret == ESP_FAIL) {
                strcpy(text, "Failed to enroll.");
            } else {
                snprintf(text, 64, "id: %d enrolled.", self->m_recognizer->get_num_feats());
            }
            xQueueSend(who::lcd::WhoLCD::s_queue_handle, &detect_result, portMAX_DELAY);
            xQueueSend(who::lcd::WhoLCD::s_queue_handle, &recognition_result, portMAX_DELAY);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::DETECT;
            xSemaphoreGive(s_mutex);
            break;
        }
        case fr_status_t::DELETE: {
            esp_err_t ret = self->m_recognizer->delete_last_feat();
            char *text = new char[64];
            recognition_result.res.recognition_res = text;
            if (ret == ESP_FAIL) {
                strcpy(text, "Failed to delete.");
            } else {
                snprintf(text, 64, "id: %d deleted.", self->m_recognizer->get_num_feats() + 1);
            }
            xQueueSend(who::lcd::WhoLCD::s_queue_handle, &recognition_result, portMAX_DELAY);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::DETECT;
            xSemaphoreGive(s_mutex);
            break;
        }
        case fr_status_t::DETECT: {
            who::cam::cam_fb_t *fb = self->m_cam->cam_fb_peek();
            detect_result.timestamp = fb->timestamp;
            std::list<dl::detect::result_t> &detect_res = self->m_detect->run(who::cam::fb2img(fb));
            detect_result.res.detect_res = new std::list<dl::detect::result_t>(detect_res);
            xQueueSend(who::lcd::WhoLCD::s_queue_handle, &detect_result, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(10));
            // vTaskDelayUntil(&last_wake_time, frame_interval);
            break;
        }
        }
    }
}

void WhoHumanFaceRecognition::lvgl_btn_event_handler(lv_event_t *e)
{
    fr_event_t fr_event = (fr_event_t)(reinterpret_cast<int>(lv_event_get_user_data(e)));
    btn_event_handler(fr_event);
}

void WhoHumanFaceRecognition::iot_btn_event_handler(void *button_handle, void *usr_data)
{
    fr_event_t fr_event = (fr_event_t)(reinterpret_cast<int>(usr_data));
    btn_event_handler(fr_event);
}

inline void WhoHumanFaceRecognition::btn_event_handler(fr_event_t fr_event)
{
    switch (fr_event) {
    case fr_event_t::RECOGNIZE:
        xTaskNotify(s_task_handle, (uint32_t)fr_event_t::RECOGNIZE, eSetBits);
        break;
    case fr_event_t::ENROLL:
        xTaskNotify(s_task_handle, (uint32_t)fr_event_t::ENROLL, eSetBits);
        break;
    case fr_event_t::DELETE:
        xTaskNotify(s_task_handle, (uint32_t)fr_event_t::DELETE, eSetBits);
        break;
    }
}

void WhoHumanFaceRecognition::create_btns()
{
#if CONFIG_IDF_TARGET_ESP32P4
    bsp_display_lock(0);
    lv_obj_t *btn_recognize = who::lcd::LCD::create_btn("recognize");
    lv_obj_t *btn_enroll = who::lcd::LCD::create_btn("enroll");
    lv_obj_t *btn_delete = who::lcd::LCD::create_btn("delete");
    lv_obj_add_event_cb(btn_recognize, lvgl_btn_event_handler, LV_EVENT_CLICKED, (void *)fr_event_t::RECOGNIZE);
    lv_obj_add_event_cb(btn_enroll, lvgl_btn_event_handler, LV_EVENT_CLICKED, (void *)fr_event_t::ENROLL);
    lv_obj_add_event_cb(btn_delete, lvgl_btn_event_handler, LV_EVENT_CLICKED, (void *)fr_event_t::DELETE);
    lv_obj_update_layout(btn_recognize);
    lv_obj_update_layout(btn_enroll);
    lv_obj_update_layout(btn_delete);
    int32_t w = lv_obj_get_width(btn_recognize);
    w = std::max(w, lv_obj_get_width(btn_enroll));
    w = std::max(w, lv_obj_get_width(btn_delete));
    int32_t h = lv_obj_get_height(btn_recognize);
    lv_obj_set_size(btn_recognize, w, h);
    lv_obj_set_size(btn_enroll, w, h);
    lv_obj_set_size(btn_delete, w, h);
    int32_t pad = h / 2;
    lv_obj_align(btn_recognize, LV_ALIGN_TOP_RIGHT, -pad, pad);
    lv_obj_align(btn_enroll, LV_ALIGN_TOP_RIGHT, -pad, pad + h + pad);
    lv_obj_align(btn_delete, LV_ALIGN_TOP_RIGHT, -pad, pad + 2 * (h + pad));
    bsp_display_unlock();
#elif CONFIG_IDF_TARGET_ESP32S3
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    // play  recognize
    ESP_ERROR_CHECK(
        iot_button_register_cb(btns[1], BUTTON_SINGLE_CLICK, iot_btn_event_handler, (void *)fr_event_t::RECOGNIZE));
    // up    enroll
    ESP_ERROR_CHECK(
        iot_button_register_cb(btns[3], BUTTON_SINGLE_CLICK, iot_btn_event_handler, (void *)fr_event_t::ENROLL));
    // down  delete
    ESP_ERROR_CHECK(
        iot_button_register_cb(btns[2], BUTTON_SINGLE_CLICK, iot_btn_event_handler, (void *)fr_event_t::DELETE));
#endif
}

void WhoHumanFaceRecognition::create_label()
{
    bsp_display_lock(0);
    s_label = who::lcd::LCD::create_label("");
    const lv_font_t *font = lv_obj_get_style_text_font(s_label, LV_PART_MAIN);
    lv_obj_align(s_label, LV_ALIGN_TOP_MID, 0, font->line_height);
    bsp_display_unlock();
}

void WhoHumanFaceRecognition::run()
{
    if (xTaskCreatePinnedToCore(event_handle_task, "WhoRecog_event", 2560, this, 2, &s_task_handle, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WhoRecog_event task.\n");
    }
    if (xTaskCreatePinnedToCore(recognition_task, "WhoRecog", 3584, this, 2, nullptr, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WhoRecog task.\n");
    }
}
} // namespace app
} // namespace who
