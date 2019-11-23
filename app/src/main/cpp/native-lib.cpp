#include <jni.h>
#include <map>
#include <cmath>
#include <string>
#include <fstream>
#include <vector>
#include <future>
#include <sstream>
#include <iomanip>

#include <sys/time.h>
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

const std::map<camera_status_t, const char*> cameraStatusNames={
    {ACAMERA_OK                          , "OK"},
    {ACAMERA_ERROR_UNKNOWN               , "ERROR_UNKNOWN"},
    {ACAMERA_ERROR_INVALID_PARAMETER     , "ERROR_INVALID_PARAMETER"},
    {ACAMERA_ERROR_CAMERA_DISCONNECTED   , "ERROR_CAMERA_DISCONNECTED"},
    {ACAMERA_ERROR_NOT_ENOUGH_MEMORY     , "ERROR_NOT_ENOUGH_MEMORY"},
    {ACAMERA_ERROR_METADATA_NOT_FOUND    , "ERROR_METADATA_NOT_FOUND"},
    {ACAMERA_ERROR_CAMERA_DEVICE         , "ERROR_CAMERA_DEVICE"},
    {ACAMERA_ERROR_CAMERA_SERVICE        , "ERROR_CAMERA_SERVICE"},
    {ACAMERA_ERROR_SESSION_CLOSED        , "ERROR_SESSION_CLOSED"},
    {ACAMERA_ERROR_INVALID_OPERATION     , "ERROR_INVALID_OPERATION"},
    {ACAMERA_ERROR_STREAM_CONFIGURE_FAIL , "ERROR_STREAM_CONFIGURE_FAIL"},
    {ACAMERA_ERROR_CAMERA_IN_USE         , "ERROR_CAMERA_IN_USE"},
    {ACAMERA_ERROR_MAX_CAMERA_IN_USE     , "ERROR_MAX_CAMERA_IN_USE"},
    {ACAMERA_ERROR_CAMERA_DISABLED       , "ERROR_CAMERA_DISABLED"},
    {ACAMERA_ERROR_PERMISSION_DENIED     , "ERROR_PERMISSION_DENIED"},
    {ACAMERA_ERROR_UNSUPPORTED_OPERATION , "ERROR_UNSUPPORTED_OPERATION"},
};

template<typename T, typename ErrorType>
std::string errorName(T const& names, ErrorType error)
{
    const auto it=names.find(error);
    if(it!=names.end())
        return it->second;
    return std::to_string(int(error));
}
#define CHECK_MEDIA_CALL(call, HANDLER_STATEMENT) \
    if(const media_status_t status=call; status!=AMEDIA_OK) \
    { \
        LOGE("Call %s failed. Error code %s", #call, errorName(mediaStatusNames,status).c_str()); \
        HANDLER_STATEMENT; \
    }
#define CHECK_CAM_CALL(call, HANDLER_STATEMENT) \
    if(const camera_status_t status=call; status!=ACAMERA_OK) \
    { \
        LOGE("Call %s failed. Error code %s", #call, errorName(cameraStatusNames,status).c_str()); \
        HANDLER_STATEMENT; \
    }

std::string cameraPropsString;
const auto desiredRawFormat=AIMAGE_FORMAT_RAW16;
const auto desiredCookedFormat=AIMAGE_FORMAT_JPEG;

void printRationalMatrix(std::ostream& os, ACameraMetadata_const_entry const& entry)
{
    const auto*const m=entry.data.i32;
    os << double(m[0 ])/m[1 ] << ','
       << double(m[2 ])/m[3 ] << ','
       << double(m[4 ])/m[5 ] << '\n'

       << double(m[6 ])/m[7 ] << ','
       << double(m[8 ])/m[9 ] << ','
       << double(m[10])/m[11] << '\n'

       << double(m[12])/m[13] << ','
       << double(m[14])/m[15] << ','
       << double(m[16])/m[17] << "\n";
}

void printIlluminant(std::ostream& os, ACameraMetadata_const_entry const& entry)
{
    const unsigned illum=entry.data.u8[0];
    switch(illum)
    {
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_DAYLIGHT              : os << "DAYLIGHT"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_FLUORESCENT           : os << "FLUORESCENT"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_TUNGSTEN              : os << "TUNGSTEN"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_FLASH                 : os << "FLASH"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_FINE_WEATHER          : os << "FINE_WEATHER"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_CLOUDY_WEATHER        : os << "CLOUDY_WEATHER"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_SHADE                 : os << "SHADE"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_DAYLIGHT_FLUORESCENT  : os << "DAYLIGHT_FLUORESCENT"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_DAY_WHITE_FLUORESCENT : os << "DAY_WHITE_FLUORESCENT"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_COOL_WHITE_FLUORESCENT: os << "COOL_WHITE_FLUORESCENT"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_WHITE_FLUORESCENT     : os << "WHITE_FLUORESCENT"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_STANDARD_A            : os << "STANDARD_A"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_STANDARD_B            : os << "STANDARD_B"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_STANDARD_C            : os << "STANDARD_C"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_D55                   : os << "D55"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_D65                   : os << "D65"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_D75                   : os << "D75"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_D50                   : os << "D50"; break;
    case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_ISO_STUDIO_TUNGSTEN   : os << "ISO_STUDIO_TUNGSTEN"; break;
    default: os << illum;
    }
}

void getCamProps(std::ostream& file, ACameraManager *cameraManager, const char *id,
                 const AIMAGE_FORMATS formatToFindA, unsigned& bestWidthA, unsigned& bestHeightA,
                 const AIMAGE_FORMATS formatToFindB, unsigned& bestWidthB, unsigned& bestHeightB)
{
    ACameraMetadata *metadata;
    ACameraManager_getCameraCharacteristics(cameraManager, id, &metadata);

    ACameraMetadata_const_entry entry{};
    file << "Camera " << id << " metadata:\n";

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE, &entry),)
    else file << "Exposure range: " << entry.data.i32[0] << " - " << entry.data.i32[1] << "\n";

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE, &entry),)
    else file << "Sensitivity range: " << entry.data.i32[0] << " - " << entry.data.i32[1] << "\n";

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_BLACK_LEVEL_PATTERN, &entry),)
    else file << "Black level pattern: " << entry.data.i32[0] << ", " << entry.data.i32[1]
                                 << ", " << entry.data.i32[2] << ", " << entry.data.i32[3] << "\n";

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_REFERENCE_ILLUMINANT1, &entry),)
    else
    {
        file << "Sensor reference illuminant 1: ";
        printIlluminant(file, entry);
        file << '\n';
    }

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_REFERENCE_ILLUMINANT2, &entry),)
    else
    {
        file << "Sensor reference illuminant 2: ";
        printIlluminant(file, entry);
        file << '\n';
    }

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_CALIBRATION_TRANSFORM1, &entry),)
    else
    {
        file << "Sensor calibration transform 1:\n";
        printRationalMatrix(file, entry);
        file << "\n";
    }

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_CALIBRATION_TRANSFORM2, &entry),)
    else
    {
        file << "Sensor calibration transform 2:\n";
        printRationalMatrix(file, entry);
        file << "\n";
    }

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_COLOR_TRANSFORM1, &entry),)
    else
    {
        file << "Sensor color transform 1:\n";
        printRationalMatrix(file, entry);
        file << "\n";
    }

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_COLOR_TRANSFORM2, &entry),)
    else
    {
        file << "Sensor color transform 2:\n";
        printRationalMatrix(file, entry);
        file << "\n";
    }

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_FORWARD_MATRIX1, &entry),)
    else
    {
        file << "Sensor forward matrix 1:\n";
        printRationalMatrix(file, entry);
        file << "\n";
    }

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_FORWARD_MATRIX2, &entry),)
    else
    {
        file << "Sensor forward matrix 2:\n";
        printRationalMatrix(file, entry);
        file << "\n";
    }

    bestWidthA=bestHeightA=bestWidthB=bestHeightB=0;
    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry),)
    else
    {
        for (int i = 0; i < entry.count; i += 4)
        {
            // We are only interested in output streams, so skip input stream
            const bool isInput = entry.data.i32[i + 3];
            if (isInput) continue;

            const auto format = entry.data.i32[i + 0];
            const int width = entry.data.i32[i + 1];
            const int height = entry.data.i32[i + 2];
            const auto it=formatNames.find(format);
            const auto formatName = it==formatNames.end() ? std::to_string(format) : it->second;
            file << "Format: " << formatName << ", width=" << width << ", height=" << height << "\n";
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
    }

    CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_ORIENTATION, &entry),)
    else
    {
        const int orientation = entry.data.i32[0];
        file << "Orientation: " << orientation << "\n";
    }
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
static ACaptureRequest* captureRequest = nullptr;
static ANativeWindow* rawImageWindow = nullptr;
static ANativeWindow* jpegImageWindow = nullptr;
static ACameraOutputTarget* rawImageTarget = nullptr;
static ACameraOutputTarget* jpegImageTarget = nullptr;
static AImageReader* rawImageReader = nullptr;
static AImageReader* jpegImageReader = nullptr;
static ACaptureSessionOutput* rawImageOutput = nullptr;
static ACaptureSessionOutput* jpegImageOutput = nullptr;
static ACaptureSessionOutputContainer* outputs = nullptr;
static ACameraCaptureSession* captureSession = nullptr;

#define FILE_PATH_PREFIX "/data/data/eu.sisik.cam/"
std::string getFormattedTimeNow()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    auto ms=tv.tv_usec/1000.;
    if(ms>=1000)
    {
        ms-=1000;
        ++tv.tv_sec;
    }
    struct tm tm_;
    const auto tm=localtime_r(&tv.tv_sec, &tm_);
    std::ostringstream str;
    str << std::put_time(tm, "%Y%m%d_%H%M%S_");
    str << std::setw(3) << std::setfill('0') << std::lrint(ms);
    return str.str();
}

static int numberOfTimesCaptured=0;
static ACameraCaptureSession_captureCallbacks captureCallbacks
{
    .onCaptureCompleted = [](void*, ACameraCaptureSession*, ACaptureRequest*, const ACameraMetadata* metadata)
    {
        LOGD("Capture completed");

        if(numberOfTimesCaptured++==0)
        {
            LOGD("Skipping frame, retrying a capture");

            CHECK_CAM_CALL(ACameraCaptureSession_capture(captureSession, &captureCallbacks, 1, &captureRequest, nullptr),);
            return;
        }

        const auto filename = FILE_PATH_PREFIX "IMG_"+getFormattedTimeNow()+".settings";
        std::ofstream file(filename);
        if(!file)
        {
            LOGD("Capture callback: failed to open \"%s\"", filename.c_str());
            return;
        }

        ACameraMetadata_const_entry entry{};
        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_COLOR_CORRECTION_TRANSFORM, &entry),)
        else
        {
            file << "Color correction matrix:\n";
            printRationalMatrix(file, entry);
            file << "\n";
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_COLOR_CORRECTION_GAINS, &entry),)
        else
        {
            file << "Color correction gains: " << entry.data.f[0] << ", " << entry.data.f[1]
                                       << ", " << entry.data.f[2] << ", " << entry.data.f[3] << "\n";
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_CONTROL_AF_STATE, &entry),)
        else
        {
            file << "Current state of auto-focus algorithm: ";
            switch(entry.data.u8[0])
            {
            case ACAMERA_CONTROL_AF_STATE_INACTIVE          :  file << "INACTIVE\n"; break;
            case ACAMERA_CONTROL_AF_STATE_PASSIVE_SCAN      :  file << "PASSIVE_SCAN\n"; break;
            case ACAMERA_CONTROL_AF_STATE_PASSIVE_FOCUSED   :  file << "PASSIVE_FOCUSED\n"; break;
            case ACAMERA_CONTROL_AF_STATE_ACTIVE_SCAN       :  file << "ACTIVE_SCAN\n"; break;
            case ACAMERA_CONTROL_AF_STATE_FOCUSED_LOCKED    :  file << "FOCUSED_LOCKED\n"; break;
            case ACAMERA_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED:  file << "NOT_FOCUSED_LOCKED\n"; break;
            case ACAMERA_CONTROL_AF_STATE_PASSIVE_UNFOCUSED :  file << "PASSIVE_UNFOCUSED\n"; break;
            default: file << +entry.data.u8[0] << "\n";
            }
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_APERTURE, &entry),)
        else
        {
            file << "Aperture: f/" << entry.data.f[0] << "\n";
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FOCAL_LENGTH, &entry),)
        else
        {
            file << "Focal length: " << entry.data.f[0] << " mm\n";
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_STATE, &entry),)
        else
        {
            file << "Lens state: ";
            switch(entry.data.u8[0])
            {
            case ACAMERA_LENS_STATE_STATIONARY: file << "stationary\n"; break;
            case ACAMERA_LENS_STATE_MOVING    : file << "moving\n"; break;
            default: file << +entry.data.u8[0] << "\n";
            }
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_DYNAMIC_BLACK_LEVEL, &entry),)
        else
        {
            file << "Dynamic black level: " << entry.data.f[0] << ", " << entry.data.f[1]
                                    << ", " << entry.data.f[2] << ", " << entry.data.f[3] << "\n";
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_DYNAMIC_WHITE_LEVEL, &entry),)
        else
        {
            file << "Dynamic white level: " << entry.data.i32[0] << "\n";
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_EXPOSURE_TIME, &entry),)
        else
        {
            file << "Exposure time: " << entry.data.i64[0] << " ns\n";
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_SENSITIVITY, &entry),)
        else
        {
            file << "ISO sensitivity: " << entry.data.i32[0] << "\n";
        }

        CHECK_CAM_CALL(ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_TIMESTAMP, &entry),)
        else
        {
            file << "Timestamp: " << entry.data.i64[0] << " ns\n";
        }


        file << "\n------------------ Static camera characteristics ------------------\n";
        file << cameraPropsString;
    },
    .onCaptureFailed = [](void*, ACameraCaptureSession*, ACaptureRequest*, ACameraCaptureFailure*)
                       { LOGE("***************** Capture failed! **********************"); },
};

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
            const auto formatIt=formatNames.find(format);
            const auto formatName = formatIt==formatNames.end() ? std::to_string(format) : formatIt->second;
            LOGD("%s: imageCallback()", formatName.c_str());
            using namespace std::chrono_literals;
            auto& future=futures[format];
            if(future.valid() && future.wait_for(0ms) != std::future_status::ready)
            {
                LOGD("%s: Skipping an image due to still running processor of previous frame, returning from imageCallback", formatName.c_str());
                return;
            }

            AImage *image = nullptr;
            const auto status = AImageReader_acquireNextImage(reader, &image);
            if (status != AMEDIA_OK)
            {
                const auto it = mediaStatusNames.find(status);
                const auto name = it==mediaStatusNames.end() ? std::to_string(status) : it->second;
                LOGD("%s: *********** AImageReader_acquireNextImage failed with code %s, returning from imageCallback **************", formatName.c_str(), name.c_str());
                return;
            }

            // Skip first image, since it'll be blurred
            if(numberOfTimesCaptured==0)
            {
                AImage_delete(image);
                return;
            }

            const bool isRaw = format==AIMAGE_FORMAT_RAW16 ||
                               format==AIMAGE_FORMAT_RAW12 ||
                               format==AIMAGE_FORMAT_RAW10;
            const auto filename=FILE_PATH_PREFIX "IMG_"+getFormattedTimeNow()+(isRaw?".raw":".jpg");
            future=std::async(std::launch::async, [image,formatName,filename,isRaw]
                {
                    LOGD("%s: starting data saving thread", formatName.c_str());

                    uint8_t *data = nullptr;
                    int len = 0;
                    AImage_getPlaneData(image, 0, &data, &len);
                    int32_t width, height;
                    AImage_getWidth(image, &width);
                    AImage_getHeight(image, &height);

                    LOGD("%s: Plane data len: %d", formatName.c_str(), len);
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
                            LOGD("%s: Failed to write \"%s\"", formatName.c_str(), filename.c_str());
                        }
                        else
                        {
                            LOGD("%s: File \"%s\" successfully written", formatName.c_str(), filename.c_str());
                            success=true;
                        }
                    }
                    else
                    {
                        LOGD("%s: Failed to open \"%s\"", formatName.c_str(), filename.c_str());
                    }

                    AImage_delete(image);
                    LOGD("%s: returning from data saving thread", formatName.c_str());
                    return success;
                });
            LOGD("%s: returning from imageCallback", formatName.c_str());
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

    unsigned imageWidthCooked, imageHeightCooked;
    unsigned imageWidthRaw, imageHeightRaw;
    {
        std::ostringstream props;
        getCamProps(props, cameraManager, id.c_str(),
                    desiredRawFormat, imageWidthRaw, imageHeightRaw,
                    desiredCookedFormat, imageWidthCooked, imageHeightCooked);
        cameraPropsString=props.str();
    }
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

    ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_STILL_CAPTURE, &captureRequest);

    ACaptureSessionOutputContainer_create(&outputs);

    rawImageReader = createImageReader(desiredRawFormat, imageWidthRaw, imageHeightRaw);
    AImageReader_getWindow(rawImageReader, &rawImageWindow);
    ANativeWindow_acquire(rawImageWindow);
    CHECK_CAM_CALL(ACameraOutputTarget_create(rawImageWindow, &rawImageTarget),);
    CHECK_CAM_CALL(ACaptureRequest_addTarget(captureRequest, rawImageTarget),);
    CHECK_CAM_CALL(ACaptureSessionOutput_create(rawImageWindow, &rawImageOutput),);
    CHECK_CAM_CALL(ACaptureSessionOutputContainer_add(outputs, rawImageOutput),);

    jpegImageReader = createImageReader(desiredCookedFormat, imageWidthCooked, imageHeightCooked);
    AImageReader_getWindow(jpegImageReader, &jpegImageWindow);
    ANativeWindow_acquire(jpegImageWindow);
    CHECK_CAM_CALL(ACameraOutputTarget_create(jpegImageWindow, &jpegImageTarget),);
    CHECK_CAM_CALL(ACaptureRequest_addTarget(captureRequest, jpegImageTarget),);
    CHECK_CAM_CALL(ACaptureSessionOutput_create(jpegImageWindow, &jpegImageOutput),);
    CHECK_CAM_CALL(ACaptureSessionOutputContainer_add(outputs, jpegImageOutput),);

    static const ACameraCaptureSession_stateCallbacks sessionStateCallbacks{};
    CHECK_CAM_CALL(ACameraDevice_createCaptureSession(cameraDevice, outputs, &sessionStateCallbacks, &captureSession),);

    CHECK_CAM_CALL(ACameraCaptureSession_capture(captureSession, &captureCallbacks, 1, &captureRequest, nullptr),);
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
    rawImageReader = nullptr;
    AImageReader_delete(jpegImageReader);
    jpegImageReader = nullptr;

    ACaptureRequest_free(captureRequest);
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
