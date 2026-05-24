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

            int fodStatusFd;
            for (auto& path : kFodStatusPaths) {
                fodStatusFd = open(path, O_WRONLY);
                if (fodStatusFd >= 0) {
                    break;
                }
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

                // mDevice may be NULL (init called with a null device) and
                // extCmd is an Xiaomi-extended function pointer that some
                // vendor fingerprint HAL blobs don't populate. Either of
                // those would SIGSEGV at offset 0xd0; check both.
                if (mDevice && mDevice->extCmd) {
                    mDevice->extCmd(mDevice, COMMAND_NIT,
                                    fodUi ? PARAM_NIT_FOD : PARAM_NIT_NONE);
                } else {
                    static bool warned = false;
                    if (!warned) {
                        LOG(WARNING) << "skipping HBM/NIT notify (mDevice="
                                     << mDevice << " extCmd unavailable)";
                        warned = true;
                    }
                }
                if (fodStatusFd >= 0) {
                    write(fodStatusFd, fodUi ? "1" : "0", 1);
                }
            }
        }).detach();
    }

    void onFingerDown(uint32_t /*x*/, uint32_t /*y*/, float /*minor*/, float /*major*/) {
        // nothing
    }

    void onFingerUp() {
        // nothing
    }

    void onAcquired(int32_t /*result*/, int32_t /*vendorCode*/) {
        // nothing
    }

    void cancel() {
        // nothing
    }
  private:
    fingerprint_device_t *mDevice;
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
