/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "UdfpsHandler.xiaomi_msmnile"

#include "UdfpsHandler.h"

#include <android-base/logging.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <unistd.h>

#define COMMAND_NIT 10
#define PARAM_NIT_FOD 1
#define PARAM_NIT_NONE 0

static const char* kFodUiPaths[] = {
        "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_ui",
        "/sys/devices/platform/soc/soc:qcom,dsi-display/fod_ui",
};

static const char* kFodStatusPaths[] = {
        "/sys/touchpanel/fod_status",
        "/sys/devices/virtual/touch/tp_dev/fod_status",
};

static const char* kBacklightPath =
        "/sys/class/backlight/panel0-backlight/brightness";
// raphael's ea8076 panel reports max_brightness=2047. The goodix optical
// sensor needs the panel at or near its peak to capture a usable image —
// at the framework's default ~33/255 (~265 panel units) every frame fails
// GF_HAL preprocess with errno=1011.
#define FOD_BRIGHTNESS_MAX 2047

static bool readBool(int fd) {
    char c;
    int rc;

    rc = lseek(fd, 0, SEEK_SET);
    if (rc) {
        LOG(ERROR) << "failed to seek fd, err: " << rc;
        return false;
    }

    rc = read(fd, &c, sizeof(char));
    if (rc != 1) {
        LOG(ERROR) << "failed to read bool from fd, err: " << rc;
        return false;
    }

    return c != '0';
}

class XiaomiMsmnileUdfpsHandler : public UdfpsHandler {
  public:
    void init(fingerprint_device_t *device) {
        mDevice = device;
        LOG(INFO) << "UdfpsHandler init: device=" << device
                  << (device ? " (non-null)" : " (NULL!)");
        if (device) {
            LOG(INFO) << "  extCmd=" << reinterpret_cast<void*>(device->extCmd);
        }

        for (auto& path : kFodStatusPaths) {
            mFodStatusFd = open(path, O_WRONLY);
            if (mFodStatusFd >= 0) {
                break;
            }
        }
        if (mFodStatusFd < 0) {
            LOG(ERROR) << "failed to open any fod_status node";
        }

        mBacklightFd = open(kBacklightPath, O_RDWR);
        if (mBacklightFd < 0) {
            LOG(ERROR) << "failed to open backlight node " << kBacklightPath;
        }

        std::thread([this]() {
            int fodUiFd;
            for (auto& path : kFodUiPaths) {
                fodUiFd = open(path, O_RDONLY);
                if (fodUiFd >= 0) {
                    break;
                }
            }

            if (fodUiFd < 0) {
                LOG(ERROR) << "failed to open fd, err: " << fodUiFd;
                return;
            }

            struct pollfd fodUiPoll = {
                    .fd = fodUiFd,
                    .events = POLLERR | POLLPRI,
                    .revents = 0,
            };

            while (true) {
                int rc = poll(&fodUiPoll, 1, -1);
                if (rc < 0) {
                    LOG(ERROR) << "failed to poll fd, err: " << rc;
                    continue;
                }

                bool fodUi = readBool(fodUiFd);
                applyFodState(fodUi);
            }
        }).detach();
    }

    void onFingerDown(uint32_t /*x*/, uint32_t /*y*/, float /*minor*/, float /*major*/) {
        // AIDL SystemUI calls us directly when the FOD circle is pressed;
        // the legacy fod_ui display-driver path doesn't fire on this build,
        // so the poll thread above never enables HBM or fod_status. Drive
        // both ourselves here.
        applyFodState(true);
    }

    void onFingerUp() {
        applyFodState(false);
    }

    void onAcquired(int32_t /*result*/, int32_t /*vendorCode*/) {
        // nothing
    }

    void cancel() {
        applyFodState(false);
    }
  private:
    fingerprint_device_t *mDevice;
    int mFodStatusFd = -1;
    int mBacklightFd = -1;
    int mSavedBrightness = -1;

    void applyFodState(bool on) {
        if (mDevice && mDevice->extCmd) {
            mDevice->extCmd(mDevice, COMMAND_NIT, on ? PARAM_NIT_FOD : PARAM_NIT_NONE);
        } else {
            static bool warned = false;
            if (!warned) {
                LOG(WARNING) << "skipping HBM/NIT notify (mDevice=" << mDevice
                             << " extCmd unavailable)";
                warned = true;
            }
        }
        if (mFodStatusFd >= 0) {
            const char* v = on ? "1" : "0";
            write(mFodStatusFd, v, 1);
        }
        if (mBacklightFd >= 0) {
            if (on) {
                char buf[16] = {0};
                lseek(mBacklightFd, 0, SEEK_SET);
                int r = read(mBacklightFd, buf, sizeof(buf) - 1);
                if (r > 0) {
                    int v = atoi(buf);
                    if (v > 0 && v < FOD_BRIGHTNESS_MAX) {
                        mSavedBrightness = v;
                    }
                }
                char out[16];
                int n = snprintf(out, sizeof(out), "%d\n", FOD_BRIGHTNESS_MAX);
                lseek(mBacklightFd, 0, SEEK_SET);
                write(mBacklightFd, out, n);
            } else if (mSavedBrightness > 0) {
                char out[16];
                int n = snprintf(out, sizeof(out), "%d\n", mSavedBrightness);
                lseek(mBacklightFd, 0, SEEK_SET);
                write(mBacklightFd, out, n);
                mSavedBrightness = -1;
            }
        }
    }
};

static UdfpsHandler* create() {
    return new XiaomiMsmnileUdfpsHandler();
}

static void destroy(UdfpsHandler* handler) {
    delete handler;
}

extern "C" UdfpsHandlerFactory UDFPS_HANDLER_FACTORY = {
    .create = create,
    .destroy = destroy,
};
