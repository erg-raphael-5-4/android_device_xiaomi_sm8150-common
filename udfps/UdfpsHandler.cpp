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

// New sysfs added by the kernel patch — writing "1" triggers the panel's
// DSI HBM-FOD command sequence (qcom,mdss-dsi-dispparam-hbm-fod-on-command).
// This brightens only the FOD circle area at the panel level instead of
// cranking the whole AMOLED via the backlight node. Falls back gracefully
// if the kernel doesn't have the patch (open() returns -1).
static const char* kFodHbmPath =
        "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_hbm";

// DRM connector exposes the panel power state: 0 = LCD_MODE_ON,
// non-zero values = sleep / doze / off variants. We poll this to arm
// fod_status before the touch IC enters suspend, so the IC enters
// FOD-gesture mode (TP_GESTURE_DBCLK_FOD) instead of plain
// double-tap-wake. Without arming, BTN_INFO never fires for screen-off
// FOD touches and the FP sensor is never woken.
static const char* kPanelPowerStatePath =
        "/sys/class/drm/sde-conn-1-DSI-1/panel_power_state";

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

        mFodHbmFd = open(kFodHbmPath, O_WRONLY);
        if (mFodHbmFd < 0) {
            LOG(ERROR) << "failed to open fod_hbm node " << kFodHbmPath
                       << " (kernel lacks the dsi_display fod_hbm patch?)";
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

        // Screen-state watcher: arms fod_status when the panel leaves
        // LCD_MODE_ON (any non-zero state) so the touch IC enters
        // FOD-gesture mode at suspend, and clears it when the panel
        // returns to LCD_MODE_ON so screen-on flows can manage fod_status
        // via onFingerDown/onFingerUp without interference.
        std::thread([this]() {
            int fd = open(kPanelPowerStatePath, O_RDONLY);
            if (fd < 0) {
                LOG(ERROR) << "failed to open panel_power_state: " << kPanelPowerStatePath;
                return;
            }
            int lastState = -1;
            while (true) {
                char buf[8] = {0};
                lseek(fd, 0, SEEK_SET);
                int r = read(fd, buf, sizeof(buf) - 1);
                if (r > 0) {
                    int state = atoi(buf);
                    if (state != lastState) {
                        bool screenOff = (state != 0);
                        LOG(INFO) << "panel_power_state " << lastState
                                  << " -> " << state
                                  << ", fod_status=" << (screenOff ? 1 : 0);
                        if (mFodStatusFd >= 0) {
                            const char* v = screenOff ? "1" : "0";
                            lseek(mFodStatusFd, 0, SEEK_SET);
                            write(mFodStatusFd, v, 1);
                        }
                        lastState = state;
                    }
                }
                usleep(500 * 1000);
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

    void onAuthenticationSucceeded() {
        // SystemUI's onPointerUp arrives AFTER the framework tears down the
        // auth session (logged as "onPointerUp received during client: null"),
        // so onFingerUp never reaches us and HBM/brightness stays on. Drop
        // the FOD state here from the auth-complete path instead.
        applyFodState(false);
    }

    void onAuthenticationFailed() {
        applyFodState(false);
    }

    void cancel() {
        applyFodState(false);
    }
  private:
    fingerprint_device_t *mDevice;
    int mFodStatusFd = -1;
    int mFodHbmFd = -1;

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
        // fod_hbm is intentionally NOT written here. With FOD_ZPOS enabled,
        // the SurfaceFlinger -> HWComposer -> DRM -> sde_plane PLANE_PROP_FOD
        // path automatically marks the UDFPS overlay as the FOD plane; the
        // kernel's sde_connector_update_fod_hbm() hook then fires HBM-FOD
        // on/off per frame with the proper kickoff-context locking. Writing
        // fod_hbm from here would race that hook.
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
