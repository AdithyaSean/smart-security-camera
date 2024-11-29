#include "who_app.hpp"
#include "who_cam.hpp"
#include "who_lcd.hpp"

extern "C" void app_main(void)
{
    who::cam::Cam *cam = new who::cam::P4Cam(who::cam::VIDEO_PIX_FMT_RGB565, 5, V4L2_MEMORY_MMAP, false);
    who::lcd::LCD *lcd = new who::lcd::LCD();
    HumanFaceDetect *detect = new HumanFaceDetect();

    who::cam::WhoCam *who_cam = new who::cam::WhoCam(cam);
    who::lcd::WhoLCD *who_lcd = new who::lcd::WhoLCD(lcd, cam, 1);
    who::app::WhoHumanFaceDetect *who_detect = new who::app::WhoHumanFaceDetect(detect, cam);
    who_lcd->run();
    who_cam->run();
    who_detect->run();
}
