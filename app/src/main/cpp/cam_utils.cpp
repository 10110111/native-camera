// Copyright (c) 2018 Roman Sisik

#include "cam_utils.h"
#include "log.h"
#include <media/NdkImageReader.h>
#include <map>

const std::map<int32_t,const char*> formatNames=
{
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

void getCamProps(ACameraManager *cameraManager, const char *id, const AIMAGE_FORMATS formatToFind, unsigned& rawWidth, unsigned& rawHeight)
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

    rawWidth=rawHeight=0;
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
        if(format==formatToFind)
        {
            rawWidth=width;
            rawHeight=height;
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
