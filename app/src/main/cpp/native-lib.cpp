#include <jni.h>
#include <string>
#include <fstream>

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraDevice.h>
#include <media/NdkImageReader.h>

#include <android/log.h>

#define S(x) #x
#define S_(x) S(x)
#define S__LINE__ S_(__LINE__)

#define LOGI(...) (__android_log_print(ANDROID_LOG_SILENT, "LOGI: " __FILE__ " | " S__LINE__, __VA_ARGS__))
#define LOGD(...) (__android_log_print(ANDROID_LOG_DEBUG, "LOGD: " __FILE__ " | " S__LINE__, __VA_ARGS__))
#define LOGW(...) (__android_log_print(ANDROID_LOG_WARN, "LOGW: " __FILE__ " | " S__LINE__, __VA_ARGS__))
#define LOGE(...) (__android_log_print(ANDROID_LOG_ERROR, "LOGE: " __FILE__ " | " S__LINE__, __VA_ARGS__))

#define CHECK_MEDIA_CALL(call, HANDLER_STATEMENT) \
    if(const media_status_t status=call; status!=AMEDIA_OK) \
    { \
        LOGE("Call %s failed. Error code %d", #call, int(status)); \
        HANDLER_STATEMENT; \
    }
#define CHECK_CAM_CALL(call, HANDLER_STATEMENT) \
    if(const camera_status_t status=call; status!=ACAMERA_OK) \
    { \
        LOGE("Call %s failed. Error code %d", #call, int(status)); \
        HANDLER_STATEMENT; \
    }

ACameraManager* cameraManager;
ACameraDevice* cameraDevice;
ACaptureRequest* captureRequest;
ANativeWindow* rawImageWindow;
ANativeWindow* jpegImageWindow;
ACameraOutputTarget* rawImageTarget;
ACameraOutputTarget* jpegImageTarget;
AImageReader* rawImageReader;
AImageReader* jpegImageReader;
ACaptureSessionOutput* rawImageOutput;
ACaptureSessionOutput* jpegImageOutput;
ACaptureSessionOutputContainer* outputs;
ACameraCaptureSession* captureSession;

int numberOfTimesCaptured=0;
ACameraCaptureSession_captureCallbacks captureCallbacks
{
    .onCaptureCompleted = [](void*, ACameraCaptureSession*, ACaptureRequest*,
                             const ACameraMetadata* metadata)
    {
        LOGD("Capture completed");

        if(numberOfTimesCaptured++) return;
        // Retry because first frame will most likely be blurred
        CHECK_CAM_CALL(ACameraCaptureSession_capture(captureSession, &captureCallbacks,
                                                     1, &captureRequest, nullptr),);
    },
    .onCaptureFailed = [](auto, auto, auto, auto)
    {
        LOGE("***************** Capture failed! **********************");
    },
};

AImageReader* createImageReader(AIMAGE_FORMATS desiredFormat,
                                unsigned width, unsigned height)
{
    AImageReader* reader = nullptr;
    CHECK_MEDIA_CALL(AImageReader_new(width, height, desiredFormat, 1, &reader),
                     return nullptr);

    static const auto imageCallback=[](void* context, AImageReader* reader)
    {
        const int format=reinterpret_cast<uintptr_t>(context);
        LOGD("Format %d: imageCallback()", format);

        AImage *image = nullptr;
        CHECK_MEDIA_CALL(AImageReader_acquireNextImage(reader, &image), return);

        const bool isRaw = format==AIMAGE_FORMAT_RAW16;
        const auto filename=std::string("/data/data/eu.sisik.cam/IMG_TEST")+
                                (isRaw?".raw":".jpg");

        uint8_t *data = nullptr;
        int len = 0;
        AImage_getPlaneData(image, 0, &data, &len);
        int32_t width, height;
        AImage_getWidth(image, &width);
        AImage_getHeight(image, &height);

        LOGD("Format %d: Plane data len: %d", format, len);
        std::ofstream file(filename);
        if(isRaw)
        {
            file.write(reinterpret_cast<const char*>(&width ), sizeof width);
            file.write(reinterpret_cast<const char*>(&height), sizeof height);
        }
        file.write(reinterpret_cast<const char*>(data), len);

        AImage_delete(image);
        LOGD("Format %d: returning from imageCallback", format);
    };
    AImageReader_ImageListener listener
    {
        .context=reinterpret_cast<void*>(desiredFormat), 
        .onImageAvailable = imageCallback
    };
    AImageReader_setImageListener(reader, &listener);

    return reader;
}

static void initCam()
{
    cameraManager = ACameraManager_create();

    ACameraDevice_stateCallbacks cameraDeviceCallbacks = {
        .onError=[](void*, ACameraDevice*, int error) { LOGD("error %d", error); }};
    ACameraManager_openCamera(cameraManager, "0", &cameraDeviceCallbacks,
                              &cameraDevice);

    ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_STILL_CAPTURE,
                                       &captureRequest);

    ACaptureSessionOutputContainer_create(&outputs);

    rawImageReader = createImageReader(AIMAGE_FORMAT_RAW16, 4144, 3106);
    AImageReader_getWindow(rawImageReader, &rawImageWindow);
    ANativeWindow_acquire(rawImageWindow);
    CHECK_CAM_CALL(ACameraOutputTarget_create(rawImageWindow, &rawImageTarget),);
    CHECK_CAM_CALL(ACaptureRequest_addTarget(captureRequest, rawImageTarget),);
    CHECK_CAM_CALL(ACaptureSessionOutput_create(rawImageWindow, &rawImageOutput),);
    CHECK_CAM_CALL(ACaptureSessionOutputContainer_add(outputs, rawImageOutput),);

    jpegImageReader = createImageReader(AIMAGE_FORMAT_JPEG, 4128, 2322);
    AImageReader_getWindow(jpegImageReader, &jpegImageWindow);
    ANativeWindow_acquire(jpegImageWindow);
    CHECK_CAM_CALL(ACameraOutputTarget_create(jpegImageWindow, &jpegImageTarget),);
    CHECK_CAM_CALL(ACaptureRequest_addTarget(captureRequest, jpegImageTarget),);
    CHECK_CAM_CALL(ACaptureSessionOutput_create(jpegImageWindow, &jpegImageOutput),);
    CHECK_CAM_CALL(ACaptureSessionOutputContainer_add(outputs, jpegImageOutput),);

    static const ACameraCaptureSession_stateCallbacks sessionStateCallbacks{};
    CHECK_CAM_CALL(ACameraDevice_createCaptureSession(cameraDevice, outputs,
                                                      &sessionStateCallbacks,
                                                      &captureSession),);

    CHECK_CAM_CALL(ACameraCaptureSession_capture(captureSession, &captureCallbacks,
                                                 1, &captureRequest, nullptr),);
}

static void exitCam()
{
    numberOfTimesCaptured = 0;
    if(!cameraManager) return;

    ACaptureSessionOutputContainer_free(outputs);
    ACaptureSessionOutput_free(rawImageOutput);
    ACaptureSessionOutput_free(jpegImageOutput);

    ACameraDevice_close(cameraDevice);
    ACameraManager_delete(cameraManager);
    cameraManager = nullptr;

    AImageReader_delete(rawImageReader);
    AImageReader_delete(jpegImageReader);

    ACaptureRequest_free(captureRequest);

    ACameraOutputTarget_free(rawImageTarget);
    ACameraOutputTarget_free(jpegImageTarget);

}

extern "C"
{
JNIEXPORT void JNICALL
Java_eu_sisik_cam_MainActivity_initCam(JNIEnv *env, jobject)
{
    initCam();
}

JNIEXPORT void JNICALL
Java_eu_sisik_cam_MainActivity_exitCam(JNIEnv *env, jobject)
{
    exitCam();
}
}
