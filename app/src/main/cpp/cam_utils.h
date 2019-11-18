// Copyright (c) 2018 Roman Sisik

#ifndef NATIVE_CAMERA_CAM_UTILS_H
#define NATIVE_CAMERA_CAM_UTILS_H

#include <camera/NdkCameraManager.h>
#include <media/NdkImage.h>
#include <string>

void getCamProps(ACameraManager *cameraManager, const char *id, AIMAGE_FORMATS formatToFind, unsigned& rawWidth, unsigned& rawHeight);

std::string getBackFacingCamId(ACameraManager *cameraManager);

#endif
