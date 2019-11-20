#include <jni.h>
#include <map>
#include <string>
#include <fstream>
#include <vector>
#include <future>
#include <sstream>
#include <iomanip>

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraDevice.h>
#include <media/NdkImageReader.h>
#include <android/native_window_jni.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "log.h"


const std::map<int32_t,const char*> formatNames={
    {AIMAGE_FORMAT_RGBA_8888        , "RGBA_8888"},
    {AIMAGE_FORMAT_RGBX_8888        , "RGBX_8888"},
    {AIMAGE_FORMAT_RGB_888          , "RGB_888"},
    {AIMAGE_FORMAT_RGB_565          , "RGB_565"},
    {AIMAGE_FORMAT_RGBA_FP16        , "RGBA_FP16"},
    {AIMAGE_FORMAT_YUV_420_888      , "YUV_420_888"},
    {AIMAGE_FORMAT_JPEG             , "JPEG"},
    {AIMAGE_FORMAT_RAW16            , "RAW16"},
    {AIMAGE_FORMAT_RAW_PRIVATE      , "RAW_PRIVATE"},
    {AIMAGE_FORMAT_RAW10            , "RAW10"},
    {AIMAGE_FORMAT_RAW12            , "RAW12"},
    {AIMAGE_FORMAT_DEPTH16          , "DEPTH16"},
    {AIMAGE_FORMAT_DEPTH_POINT_CLOUD, "DEPTH_POINT_CLOUD"},
    {AIMAGE_FORMAT_PRIVATE          , "PRIVATE"},
    {AIMAGE_FORMAT_Y8               , "Y8"},
    {AIMAGE_FORMAT_HEIC             , "HEIC"},
    {AIMAGE_FORMAT_DEPTH_JPEG       , "DEPTH_JPEG"},
};

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

const auto desiredRawFormat=AIMAGE_FORMAT_RAW16;
unsigned imageWidthRaw, imageHeightRaw;
const auto desiredCookedFormat=AIMAGE_FORMAT_JPEG;
unsigned imageWidthCooked, imageHeightCooked;

void getCamProps(ACameraManager *cameraManager, const char *id,
                 const AIMAGE_FORMATS formatToFindA, unsigned& bestWidthA, unsigned& bestHeightA,
                 const AIMAGE_FORMATS formatToFindB, unsigned& bestWidthB, unsigned& bestHeightB)
{
    ACameraMetadata *metadataObj;
    ACameraManager_getCameraCharacteristics(cameraManager, id, &metadataObj);

    ACameraMetadata_const_entry entry = {0};
    ACameraMetadata_getConstEntry(metadataObj, ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE, &entry);

    const long long minExposure = entry.data.i64[0];
    const long long maxExposure = entry.data.i64[1];
    LOGD("camProps: minExposure=%lld, maxExposure=%lld", minExposure, maxExposure);

    // sensitivity
    ACameraMetadata_getConstEntry(metadataObj, ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE, &entry);

    const int minSensitivity = entry.data.i32[0];
    const int maxSensitivity = entry.data.i32[1];

    LOGD("camProps: minSensitivity=%d, maxSensitivity=%d", minSensitivity, maxSensitivity);

    ACameraMetadata_getConstEntry(metadataObj, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);

    bestWidthA=bestHeightA=bestWidthB=bestHeightB=0;
    for (int i = 0; i < entry.count; i += 4)
    {
        // We are only interested in output streams, so skip input stream
        const bool input = entry.data.i32[i + 3];
        if (input)
            continue;

        const auto format = entry.data.i32[i + 0];
        const int width = entry.data.i32[i + 1];
        const int height = entry.data.i32[i + 2];
        const auto it=formatNames.find(format);
        const auto formatName = it==formatNames.end() ? std::to_string(format) : it->second;
        LOGD("camProps: format: %s, maxWidth=%d, maxHeight=%d", formatName.c_str(), width, height);
        if(format==formatToFindA && uint64_t(width)*height > uint64_t(bestWidthA)*bestHeightA)
        {
            bestWidthA=width;
            bestHeightA=height;
        }
        else if(format==formatToFindB && uint64_t(width)*height > uint64_t(bestWidthB)*bestHeightB)
        {
            bestWidthB=width;
            bestHeightB=height;
        }
    }

    // cam facing
    ACameraMetadata_getConstEntry(metadataObj, ACAMERA_SENSOR_ORIENTATION, &entry);

    const int orientation = entry.data.i32[0];
    LOGD("camProps: orientation: %d", orientation);
}


std::string getBackFacingCamId(ACameraManager *cameraManager)
{
    ACameraIdList *cameraIds = nullptr;
    ACameraManager_getCameraIdList(cameraManager, &cameraIds);

    std::string backId;

    LOGD("Found %d cameras", cameraIds->numCameras);

    for (int i = 0; i < cameraIds->numCameras; ++i)
    {
        const char *id = cameraIds->cameraIds[i];

        ACameraMetadata *metadataObj;
        ACameraManager_getCameraCharacteristics(cameraManager, id, &metadataObj);

        ACameraMetadata_const_entry lensInfo = {0};
        ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_FACING, &lensInfo);

        auto facing = static_cast<acamera_metadata_enum_android_lens_facing_t>(
                lensInfo.data.u8[0]);

        // Found a back-facing camera?
        if (facing == ACAMERA_LENS_FACING_BACK)
        {
            backId = id;
            LOGD("Back-facing id is \"%s\"", backId.c_str());
            break;
        }
    }

    ACameraManager_deleteCameraIdList(cameraIds);

    return backId;
}
static ACameraManager* cameraManager = nullptr;
static ACameraDevice* cameraDevice = nullptr;
static ACaptureRequest* request = nullptr;
static ANativeWindow* rawImageWindow = nullptr;
static ANativeWindow* jpegImageWindow = nullptr;
static ACameraOutputTarget* rawImageTarget = nullptr;
static ACameraOutputTarget* jpegImageTarget = nullptr;
static AImageReader* rawImageReader = nullptr;
static AImageReader* jpegImageReader = nullptr;
static ACaptureSessionOutput* rawImageOutput = nullptr;
static ACaptureSessionOutput* jpegImageOutput = nullptr;
static ACaptureSessionOutputContainer* outputs = nullptr;

std::string getFormattedTimeNow()
{
    const auto t=std::time(nullptr);
    struct tm tm_;
    const auto tm=localtime_r(&t, &tm_);
    std::ostringstream str;
    str << std::put_time(tm, "%Y%m%d_%H%M%S");
    return str.str();
}

AImageReader* createImageReader(const AIMAGE_FORMATS desiredFormat, const unsigned width, const unsigned height)
{
    AImageReader* reader = nullptr;
    if(AImageReader_new(width, height, desiredFormat, 1, &reader) != AMEDIA_OK)
    {
        LOGD("Failed to create image reader");
        return nullptr;
    }
    else
        LOGD("Created image reader with dimensions %d×%d", width, height);

    static std::map<AIMAGE_FORMATS, std::future<bool>> futures;
    static const auto imageCallback=[](void* context, AImageReader* reader)
        {
            const auto format=static_cast<AIMAGE_FORMATS>(reinterpret_cast<uintptr_t>(context));
            using namespace std::chrono_literals;
            auto& future=futures[format];
            if(future.valid() && future.wait_for(0ms) != std::future_status::ready)
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

            const bool isRaw = format==AIMAGE_FORMAT_RAW16 ||
                               format==AIMAGE_FORMAT_RAW12 ||
                               format==AIMAGE_FORMAT_RAW10;
            const auto filename="/data/data/eu.sisik.cam/IMG_"+getFormattedTimeNow()+(isRaw?".raw":".jpg");
            future=std::async(std::launch::async, [image,format,filename,isRaw]{
                    uint8_t *data = nullptr;
                    int len = 0;
                    AImage_getPlaneData(image, 0, &data, &len);
                    int32_t width, height;
                    AImage_getWidth(image, &width);
                    AImage_getHeight(image, &height);

                    LOGD("Plane data len: %d", len);
                    std::ofstream file(filename);
                    bool success=false;
                    if(file)
                    {
                        if(isRaw)
                        {
                            file.write(reinterpret_cast<const char*>(&width ), sizeof width);
                            file.write(reinterpret_cast<const char*>(&height), sizeof height);
                        }
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
    AImageReader_ImageListener listener{ .context=reinterpret_cast<void*>(desiredFormat), 
                                         .onImageAvailable = imageCallback };
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

    getCamProps(cameraManager, id.c_str(), desiredRawFormat, imageWidthRaw, imageHeightRaw,
                                           desiredCookedFormat, imageWidthCooked, imageHeightCooked);
    if(!imageWidthRaw || !imageHeightRaw)
    {
        LOGD("Desired raw format doesn't appear to be supported, giving up");
        return;
    }
    if(!imageWidthRaw || !imageHeightRaw)
    {
        LOGD("Desired cooked format doesn't appear to be supported, giving up");
        return;
    }

    ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_PREVIEW, &request);
    ACaptureSessionOutputContainer_create(&outputs);

    rawImageReader = createImageReader(desiredRawFormat, imageWidthRaw, imageHeightRaw);
    AImageReader_getWindow(rawImageReader, &rawImageWindow);
    ANativeWindow_acquire(rawImageWindow);
    ACameraOutputTarget_create(rawImageWindow, &rawImageTarget);
    ACaptureRequest_addTarget(request, rawImageTarget);
    ACaptureSessionOutput_create(rawImageWindow, &rawImageOutput);
    ACaptureSessionOutputContainer_add(outputs, rawImageOutput);

    jpegImageReader = createImageReader(desiredCookedFormat, imageWidthCooked, imageHeightCooked);
    AImageReader_getWindow(jpegImageReader, &jpegImageWindow);
    ANativeWindow_acquire(jpegImageWindow);
    ACameraOutputTarget_create(jpegImageWindow, &jpegImageTarget);
    ACaptureRequest_addTarget(request, jpegImageTarget);
    ACaptureSessionOutput_create(jpegImageWindow, &jpegImageOutput);
    ACaptureSessionOutputContainer_add(outputs, jpegImageOutput);

    ACameraCaptureSession* captureSession = nullptr;
    static const ACameraCaptureSession_stateCallbacks sessionStateCallbacks{};
    ACameraDevice_createCaptureSession(cameraDevice, outputs, &sessionStateCallbacks, &captureSession);

    static ACameraCaptureSession_captureCallbacks captureCallbacks
    {
        .onCaptureCompleted = [](void*, ACameraCaptureSession*, ACaptureRequest*, const ACameraMetadata* metadata)
        {
            LOGD("Capture completed");
            ACameraMetadata_const_entry entry{};
            if(const auto res=ACameraMetadata_getConstEntry(metadata, ACAMERA_COLOR_CORRECTION_TRANSFORM, &entry); res==ACAMERA_OK)
            {
                LOGD("Color correction matrix:\n%g %g %g\n%g %g %g\n%g %g %g"
  , double(entry.data.i32[0 ])/entry.data.i32[1 ], double(entry.data.i32[2 ])/entry.data.i32[3 ], double(entry.data.i32[4 ])/entry.data.i32[5 ]
  , double(entry.data.i32[6 ])/entry.data.i32[7 ], double(entry.data.i32[8 ])/entry.data.i32[9 ], double(entry.data.i32[10])/entry.data.i32[11]
  , double(entry.data.i32[12])/entry.data.i32[13], double(entry.data.i32[14])/entry.data.i32[15], double(entry.data.i32[16])/entry.data.i32[17]
                    );
            }
            else
                LOGE("Failed to color correction transform: error %d", int(res));

        },
        .onCaptureFailed = [](void*, ACameraCaptureSession*, ACaptureRequest*, ACameraCaptureFailure*)
                           { LOGE("***************** Capture failed! **********************"); },
    };
    ACameraCaptureSession_setRepeatingRequest(captureSession, &captureCallbacks, 1, &request, nullptr);
}

static void exitCam()
{
    if(!cameraManager) return;

    ACaptureSessionOutputContainer_free(outputs);
    ACaptureSessionOutput_free(rawImageOutput);
    ACaptureSessionOutput_free(jpegImageOutput);

    ACameraDevice_close(cameraDevice);
    ACameraManager_delete(cameraManager);
    cameraManager = nullptr;

    AImageReader_delete(rawImageReader);
    rawImageReader = nullptr;
    AImageReader_delete(jpegImageReader);
    jpegImageReader = nullptr;

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
