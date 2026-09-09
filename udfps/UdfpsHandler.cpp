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

/*
 * Panel-side HBM-FOD trigger. Normally driven in-kernel by
 * sde_connector_update_fod_hbm() once PLANE_PROP_FOD lands on a plane, but on
 * this build fod_ui never leaves 0, so the optical sensor gets no illumination
 * and the HAL's onBeforeEnrollCapture times out (error 1143 -> ERROR_TIMEOUT).
 * Drive it directly from the pointer callbacks instead.
 */
static const char* kFodHbmPaths[] = {
        "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_hbm",
        "/sys/devices/platform/soc/soc:qcom,dsi-display/fod_hbm",
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

static void writeBool(int fd, bool value) {
    if (fd < 0) {
        return;
    }

    lseek(fd, 0, SEEK_SET);
    if (write(fd, value ? "1" : "0", 1) < 0) {
        LOG(ERROR) << "failed to write " << value << " to fd " << fd;
    }
}

class XiaomiMsmnileUdfpsHandler : public UdfpsHandler {
  public:
    void init(fingerprint_device_t *device) {
        mDevice = device;

        for (auto& path : kFodStatusPaths) {
            mFodStatusFd = open(path, O_WRONLY);
            if (mFodStatusFd >= 0) {
                break;
            }
        }

        for (auto& path : kFodHbmPaths) {
            mFodHbmFd = open(path, O_WRONLY);
            if (mFodHbmFd >= 0) {
                break;
            }
        }

        /*
         * Arm the touch IC so it reports BTN_INFO on FOD-area presses.
         * init.xiaomi.rc already does this at boot, but the fod_ui poll below
         * used to write it straight back to 0: sysfs signals POLLPRI on the
         * very first poll(), the thread read fod_ui == 0 and disarmed it. That
         * cancelled the init.rc workaround on every boot.
         */
        writeBool(mFodStatusFd, true);

        std::thread([this]() {
            int fodUiFd = -1;
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
                LOG(INFO) << "fod_ui changed to " << fodUi;

                /*
                 * Only ever act on the asserted edge. A de-assert here must not
                 * disarm fod_status, and must not cancel illumination that the
                 * pointer callbacks are driving.
                 */
                if (fodUi) {
                    setFodState(true);
                }
            }
        }).detach();
    }

    void onFingerDown(uint32_t /*x*/, uint32_t /*y*/, float /*minor*/, float /*major*/) {
        LOG(INFO) << "onFingerDown";
        setFodState(true);
    }

    void onFingerUp() {
        LOG(INFO) << "onFingerUp";
        setFodState(false);
    }

    void onAcquired(int32_t /*result*/, int32_t /*vendorCode*/) {
        // nothing
    }

    void cancel() {
        LOG(INFO) << "cancel";
        setFodState(false);
    }

  private:
    void setFodState(bool enabled) {
        if (mFodActive == enabled) {
            return;
        }
        mFodActive = enabled;

        LOG(INFO) << "setFodState(" << enabled << ")";

        // Illuminate before telling the HAL, so the sensor has light to capture with.
        writeBool(mFodHbmFd, enabled);

        if (mDevice && mDevice->extCmd) {
            mDevice->extCmd(mDevice, COMMAND_NIT, enabled ? PARAM_NIT_FOD : PARAM_NIT_NONE);
        }

        // Keep the touch IC armed; never disarm it.
        if (enabled) {
            writeBool(mFodStatusFd, true);
        }
    }

    fingerprint_device_t *mDevice = nullptr;
    int mFodStatusFd = -1;
    int mFodHbmFd = -1;
    bool mFodActive = false;
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
