#include <jni.h>
#include <map>
#include <string>
#include <fstream>
#include <vector>
#include <future>

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraDevice.h>
#include <media/NdkImageReader.h>
#include <android/native_window_jni.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "log.h"
#include "cam_utils.h"


const std::map<media_status_t, const char*> mediaStatusNames={
    {AMEDIA_OK                              , "OK"},
    {AMEDIACODEC_ERROR_INSUFFICIENT_RESOURCE, "CODEC_ERROR_INSUFFICIENT_RESOURCE"},
    {AMEDIACODEC_ERROR_RECLAIMED            , "CODEC_ERROR_RECLAIMED"},
    {AMEDIA_ERROR_BASE                      , "ERROR_BASE"},
    {AMEDIA_ERROR_UNKNOWN                   , "ERROR_UNKNOWN"},
    {AMEDIA_ERROR_MALFORMED                 , "ERROR_MALFORMED"},
    {AMEDIA_ERROR_UNSUPPORTED               , "ERROR_UNSUPPORTED"},
    {AMEDIA_ERROR_INVALID_OBJECT            , "ERROR_INVALID_OBJECT"},
    {AMEDIA_ERROR_INVALID_PARAMETER         , "ERROR_INVALID_PARAMETER"},
    {AMEDIA_ERROR_INVALID_OPERATION         , "ERROR_INVALID_OPERATION"},
    {AMEDIA_ERROR_END_OF_STREAM             , "ERROR_END_OF_STREAM"},
    {AMEDIA_ERROR_IO                        , "ERROR_IO"},
    {AMEDIA_ERROR_WOULD_BLOCK               , "ERROR_WOULD_BLOCK"},
    {AMEDIA_DRM_ERROR_BASE                  , "DRM_ERROR_BASE"},
    {AMEDIA_DRM_NOT_PROVISIONED             , "DRM_NOT_PROVISIONED"},
    {AMEDIA_DRM_RESOURCE_BUSY               , "DRM_RESOURCE_BUSY"},
    {AMEDIA_DRM_DEVICE_REVOKED              , "DRM_DEVICE_REVOKED"},
    {AMEDIA_DRM_SHORT_BUFFER                , "DRM_SHORT_BUFFER"},
    {AMEDIA_DRM_SESSION_NOT_OPENED          , "DRM_SESSION_NOT_OPENED"},
    {AMEDIA_DRM_TAMPER_DETECTED             , "DRM_TAMPER_DETECTED"},
    {AMEDIA_DRM_VERIFY_FAILED               , "DRM_VERIFY_FAILED"},
    {AMEDIA_DRM_NEED_KEY                    , "DRM_NEED_KEY"},
    {AMEDIA_DRM_LICENSE_EXPIRED             , "DRM_LICENSE_EXPIRED"},
    {AMEDIA_IMGREADER_ERROR_BASE            , "IMGREADER_ERROR_BASE"},
    {AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE   , "IMGREADER_NO_BUFFER_AVAILABLE"},
    {AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED   , "IMGREADER_MAX_IMAGES_ACQUIRED"},
    {AMEDIA_IMGREADER_CANNOT_LOCK_IMAGE     , "IMGREADER_CANNOT_LOCK_IMAGE"},
    {AMEDIA_IMGREADER_CANNOT_UNLOCK_IMAGE   , "IMGREADER_CANNOT_UNLOCK_IMAGE"},
    {AMEDIA_IMGREADER_IMAGE_NOT_LOCKED      , "IMGREADER_IMAGE_NOT_LOCKED"},
};

static ACameraManager* cameraManager = nullptr;
static ACameraDevice* cameraDevice = nullptr;
static ACaptureRequest* request = nullptr;
static ANativeWindow* imageWindow = nullptr;
static ACameraOutputTarget* imageTarget = nullptr;
static AImageReader* imageReader = nullptr;
static ACaptureSessionOutput* imageOutput = nullptr;
static ACaptureSessionOutput* output = nullptr;
static ACaptureSessionOutputContainer* outputs = nullptr;

static unsigned rawWidth, rawHeight;
static const auto desiredFormat=AIMAGE_FORMAT_RAW16;

AImageReader* createRAWReader()
{
    AImageReader* reader = nullptr;
    if(AImageReader_new(rawWidth, rawHeight, desiredFormat, 1, &reader) != AMEDIA_OK)
    {
        LOGD("Failed to create image reader");
        return nullptr;
    }

    static const auto imageCallback=[](void*, AImageReader* reader)
        {
            static std::future<bool> processorFinish;
            using namespace std::chrono_literals;
            if(processorFinish.valid() && processorFinish.wait_for(0ms) != std::future_status::ready)
            {
                LOGD("Skipping an image due to still running processor of previous frame");
                return;
            }

            AImage *image = nullptr;
            const auto status = AImageReader_acquireNextImage(reader, &image);
            LOGD("imageCallback()");
            if (status != AMEDIA_OK)
            {
                const auto it = mediaStatusNames.find(status);
                const auto name = it==mediaStatusNames.end() ? std::to_string(status) : it->second;
                LOGD("*********** AImageReader_acquireNextImage failed with code %s *********", name.c_str());
                return;
            }

            processorFinish=std::async(std::launch::async, [image]{
                    uint8_t *data = nullptr;
                    int len = 0;
                    AImage_getPlaneData(image, 0, &data, &len);

                    LOGD("Plane data len: %d", len);
                    static int counter;
                    ++counter;
                    const auto filename="/data/data/eu.sisik.cam/test"+std::to_string(counter%5)+".raw";
                    std::ofstream file(filename);
                    bool success=false;
                    if(file)
                    {
                        file.write(reinterpret_cast<const char*>(&rawWidth), sizeof rawWidth);
                        file.write(reinterpret_cast<const char*>(&rawHeight), sizeof rawHeight);
                        file.write(reinterpret_cast<const char*>(data), len);
                        if(!file.flush())
                        {
                            LOGD("Failed to write \"%s\"", filename.c_str());
                        }
                        else
                        {
                            LOGD("File \"%s\" successfully written", filename.c_str());
                            success=true;
                        }
                    }
                    else
                    {
                        LOGD("Failed to open \"%s\"", filename.c_str());
                    }

                    AImage_delete(image);
                    return success;
                });
        };
    AImageReader_ImageListener listener{ .onImageAvailable = imageCallback };
    AImageReader_setImageListener(reader, &listener);

    return reader;
}

static void initCam()
{
    cameraManager = ACameraManager_create();

    const auto id = getBackFacingCamId(cameraManager);
    ACameraDevice_stateCallbacks cameraDeviceCallbacks = {.onError=[](void*, ACameraDevice*, int error)
                                                                   { LOGD("error %d", error); }};
    ACameraManager_openCamera(cameraManager, id.c_str(), &cameraDeviceCallbacks, &cameraDevice);

    getCamProps(cameraManager, id.c_str(), desiredFormat, rawWidth, rawHeight);
    if(!rawWidth || !rawHeight)
    {
        LOGD("RAW16 doesn't appear to be supported, giving up");
        return;
    }

    ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_PREVIEW, &request);
    ACaptureSessionOutputContainer_create(&outputs);

    imageReader = createRAWReader();
    AImageReader_getWindow(imageReader, &imageWindow);
    ANativeWindow_acquire(imageWindow);
    ACameraOutputTarget_create(imageWindow, &imageTarget);
    ACaptureRequest_addTarget(request, imageTarget);
    ACaptureSessionOutput_create(imageWindow, &imageOutput);
    ACaptureSessionOutputContainer_add(outputs, imageOutput);

    ACameraCaptureSession* captureSession = nullptr;
    static const ACameraCaptureSession_stateCallbacks sessionStateCallbacks{};
    ACameraDevice_createCaptureSession(cameraDevice, outputs, &sessionStateCallbacks, &captureSession);

    static ACameraCaptureSession_captureCallbacks captureCallbacks
    {
        .onCaptureCompleted = [](void*, ACameraCaptureSession*, ACaptureRequest*, const ACameraMetadata*)
                              { LOGD("Capture completed"); },
        .onCaptureFailed = [](void*, ACameraCaptureSession*, ACaptureRequest*, ACameraCaptureFailure*)
                           { LOGE("***************** Capture failed! **********************"); },
    };
    ACameraCaptureSession_setRepeatingRequest(captureSession, &captureCallbacks, 1, &request, nullptr);
}

static void exitCam()
{
    if(!cameraManager) return;

    ACaptureSessionOutputContainer_free(outputs);
    ACaptureSessionOutput_free(output);

    ACameraDevice_close(cameraDevice);
    ACameraManager_delete(cameraManager);
    cameraManager = nullptr;

    AImageReader_delete(imageReader);
    imageReader = nullptr;

    ACaptureRequest_free(request);
}

/**
 * JNI stuff
 */

extern "C" {

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
} // Extern C
