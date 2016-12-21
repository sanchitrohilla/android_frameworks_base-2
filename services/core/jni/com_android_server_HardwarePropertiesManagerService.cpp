/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "HardwarePropertiesManagerService-JNI"

#include "JNIHelp.h"
#include "jni.h"

#include <stdlib.h>

#include <android/hardware/thermal/1.0/IThermal.h>
#include <utils/Log.h>
#include <utils/String8.h>

#include "core_jni_helpers.h"

namespace android {

using hardware::hidl_vec;
using hardware::Status;
using hardware::thermal::V1_0::CoolingDevice;
using hardware::thermal::V1_0::CpuUsage;
using hardware::thermal::V1_0::IThermal;
using hardware::thermal::V1_0::Temperature;
using hardware::thermal::V1_0::ThermalStatus;
using hardware::thermal::V1_0::ThermalStatusCode;

// ---------------------------------------------------------------------------

// These values must be kept in sync with the temperature source constants in
// HardwarePropertiesManager.java
enum {
    TEMPERATURE_CURRENT = 0,
    TEMPERATURE_THROTTLING = 1,
    TEMPERATURE_SHUTDOWN = 2,
    TEMPERATURE_THROTTLING_BELOW_VR_MIN = 3
};

static struct {
    jclass clazz;
    jmethodID initMethod;
} gCpuUsageInfoClassInfo;

static sp<IThermal> gThermalModule;

// ----------------------------------------------------------------------------

static void nativeInit(JNIEnv* env, jobject obj) {
    // TODO(b/31632518)
    if (gThermalModule == nullptr) {
        gThermalModule = IThermal::getService("thermal");
    }

    if (gThermalModule == nullptr) {
        ALOGE("Unable to get Thermal service.");
    }
}

static jfloatArray nativeGetFanSpeeds(JNIEnv *env, jclass /* clazz */) {
    if (gThermalModule == nullptr) {
        ALOGE("Couldn't get fan speeds because of HAL error.");
        return env->NewFloatArray(0);
    }

    hidl_vec<CoolingDevice> list;
    Status status = gThermalModule->getCoolingDevices(
            [&list](ThermalStatus status, hidl_vec<CoolingDevice> devices) {
                if (status.code == ThermalStatusCode::SUCCESS) {
                    list = std::move(devices);
                } else {
                    ALOGE("Couldn't get fan speeds because of HAL error: %s",
                          status.debugMessage.c_str());
                }
            }).getStatus();

    if (!status.isOk()) {
        ALOGE("getCoolingDevices failed status: %d", status.exceptionCode());
    }

    float values[list.size()];
    for (size_t i = 0; i < list.size(); ++i) {
        values[i] = list[i].currentValue;
    }
    jfloatArray fanSpeeds = env->NewFloatArray(list.size());
    env->SetFloatArrayRegion(fanSpeeds, 0, list.size(), values);
    return fanSpeeds;
}

static jfloatArray nativeGetDeviceTemperatures(JNIEnv *env, jclass /* clazz */, int type,
                                               int source) {
    if (gThermalModule == nullptr) {
        ALOGE("Couldn't get device temperatures because of HAL error.");
        return env->NewFloatArray(0);
    }
    hidl_vec<Temperature> list;
    Status status = gThermalModule->getTemperatures(
            [&list](ThermalStatus status, hidl_vec<Temperature> temperatures) {
                if (status.code == ThermalStatusCode::SUCCESS) {
                    list = std::move(temperatures);
                } else {
                    ALOGE("Couldn't get temperatures because of HAL error: %s",
                          status.debugMessage.c_str());
                }
            }).getStatus();

    if (!status.isOk()) {
        ALOGE("getDeviceTemperatures failed status: %d", status.exceptionCode());
    }

    jfloat values[list.size()];
    size_t length = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        if (static_cast<int>(list[i].type) == type) {
            switch (source) {
                case TEMPERATURE_CURRENT:
                    values[length++] = list[i].currentValue;
                    break;
                case TEMPERATURE_THROTTLING:
                    values[length++] = list[i].throttlingThreshold;
                    break;
                case TEMPERATURE_SHUTDOWN:
                    values[length++] = list[i].shutdownThreshold;
                    break;
                case TEMPERATURE_THROTTLING_BELOW_VR_MIN:
                    values[length++] = list[i].vrThrottlingThreshold;
                    break;
            }
        }
    }
    jfloatArray deviceTemps = env->NewFloatArray(length);
    env->SetFloatArrayRegion(deviceTemps, 0, length, values);
    return deviceTemps;
}

static jobjectArray nativeGetCpuUsages(JNIEnv *env, jclass /* clazz */) {
    if (gThermalModule == nullptr || !gCpuUsageInfoClassInfo.initMethod) {
        ALOGE("Couldn't get CPU usages because of HAL error.");
        return env->NewObjectArray(0, gCpuUsageInfoClassInfo.clazz, nullptr);
    }
    hidl_vec<CpuUsage> list;
    Status status = gThermalModule->getCpuUsages(
            [&list](ThermalStatus status, hidl_vec<CpuUsage> cpuUsages) {
                if (status.code == ThermalStatusCode::SUCCESS) {
                    list = std::move(cpuUsages);
                } else {
                    ALOGE("Couldn't get CPU usages because of HAL error: %s",
                          status.debugMessage.c_str());
                }
            }).getStatus();

    if (!status.isOk()) {
        ALOGE("getCpuUsages failed status: %d", status.exceptionCode());
    }

    jobjectArray cpuUsages = env->NewObjectArray(list.size(), gCpuUsageInfoClassInfo.clazz,
                                                 nullptr);
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].isOnline) {
            jobject cpuUsage = env->NewObject(gCpuUsageInfoClassInfo.clazz,
                                              gCpuUsageInfoClassInfo.initMethod,
                                              list[i].active,
                                              list[i].total);
            env->SetObjectArrayElement(cpuUsages, i, cpuUsage);
        }
    }
    return cpuUsages;
}

// ----------------------------------------------------------------------------

static const JNINativeMethod gHardwarePropertiesManagerServiceMethods[] = {
    /* name, signature, funcPtr */
    { "nativeInit", "()V",
            (void*) nativeInit },
    { "nativeGetFanSpeeds", "()[F",
            (void*) nativeGetFanSpeeds },
    { "nativeGetDeviceTemperatures", "(II)[F",
            (void*) nativeGetDeviceTemperatures },
    { "nativeGetCpuUsages", "()[Landroid/os/CpuUsageInfo;",
            (void*) nativeGetCpuUsages }
};

int register_android_server_HardwarePropertiesManagerService(JNIEnv* env) {
    gThermalModule = nullptr;
    int res = jniRegisterNativeMethods(env, "com/android/server/HardwarePropertiesManagerService",
                                       gHardwarePropertiesManagerServiceMethods,
                                       NELEM(gHardwarePropertiesManagerServiceMethods));
    jclass clazz = env->FindClass("android/os/CpuUsageInfo");
    gCpuUsageInfoClassInfo.clazz = MakeGlobalRefOrDie(env, clazz);
    gCpuUsageInfoClassInfo.initMethod = GetMethodIDOrDie(env, gCpuUsageInfoClassInfo.clazz,
                                                         "<init>", "(JJ)V");
    return res;
}

} /* namespace android */
