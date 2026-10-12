/*
 * SPDX-FileCopyrightText: 2026 ergdevops
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.bypass;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.IBinder;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;

import androidx.preference.PreferenceManager;

import vendor.lineage.health.IChargingControl;

/**
 * Bypass charging: the charger keeps powering the phone but the battery is
 * not charged. The Lineage health HAL writes battery/charging_enabled, which
 * only clears the PM8150B charging enable; the charger input and the battery
 * path stay up, so the battery supplements a load above the input limit and
 * takes over without an interruption when the charger is unplugged.
 */
public final class BypassChargingUtils {

    private static final String TAG = "BypassCharging";
    private static final String KEY_BYPASS_CHARGING = "bypass_charging";
    private static final String CHARGING_CONTROL =
            IChargingControl.DESCRIPTOR + "/default";

    private BypassChargingUtils() {}

    public static boolean isEnabled(Context context) {
        return getPrefs(context).getBoolean(KEY_BYPASS_CHARGING, false);
    }

    public static void setEnabled(Context context, boolean enabled) {
        getPrefs(context).edit().putBoolean(KEY_BYPASS_CHARGING, enabled).apply();
        apply(enabled);
    }

    /** Re-applies the saved state, e.g. after boot. */
    public static void restore(Context context) {
        if (isEnabled(context)) {
            apply(true);
        }
    }

    private static void apply(boolean bypass) {
        IBinder binder = ServiceManager.waitForDeclaredService(CHARGING_CONTROL);
        if (binder == null) {
            Log.e(TAG, CHARGING_CONTROL + " is not available");
            return;
        }
        try {
            IChargingControl.Stub.asInterface(binder).setChargingEnabled(!bypass);
        } catch (RemoteException | RuntimeException e) {
            Log.e(TAG, "Couldn't " + (bypass ? "enable" : "disable") + " bypass charging", e);
        }
    }

    private static SharedPreferences getPrefs(Context context) {
        return PreferenceManager.getDefaultSharedPreferences(context);
    }
}
