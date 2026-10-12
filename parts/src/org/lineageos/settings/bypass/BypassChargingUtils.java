/*
 * SPDX-FileCopyrightText: 2026 ergdevops
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.bypass;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
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
 *
 * LineageOS's Charging control (Settings > Battery) drives the same HAL
 * switch from its own battery-level logic and re-applies it on every battery
 * change, so bypass is unavailable while that feature is enabled.
 */
public final class BypassChargingUtils {

    private static final String TAG = "BypassCharging";
    private static final String KEY_BYPASS_CHARGING = "bypass_charging";
    private static final String CHARGING_CONTROL =
            IChargingControl.DESCRIPTOR + "/default";
    /* LineageSettings.System.CHARGING_CONTROL_ENABLED, read through its provider */
    private static final Uri LINEAGE_SETTINGS_SYSTEM =
            Uri.parse("content://lineagesettings/system");
    private static final String CHARGING_CONTROL_ENABLED = "charging_control_enabled";

    private BypassChargingUtils() {}

    public static boolean isEnabled(Context context) {
        return getPrefs(context).getBoolean(KEY_BYPASS_CHARGING, false);
    }

    public static void setEnabled(Context context, boolean enabled) {
        if (enabled && isChargingControlEnabled(context)) {
            Log.w(TAG, "Charging control is enabled, not enabling bypass charging");
            return;
        }
        getPrefs(context).edit().putBoolean(KEY_BYPASS_CHARGING, enabled).apply();
        apply(enabled);
    }

    /** Re-applies the saved state, e.g. after boot. */
    public static void restore(Context context) {
        if (isEnabled(context) && !isChargingControlEnabled(context)) {
            apply(true);
        }
    }

    /** Whether LineageOS's Charging control owns the charging switch. */
    public static boolean isChargingControlEnabled(Context context) {
        try (Cursor c = context.getContentResolver().query(LINEAGE_SETTINGS_SYSTEM,
                new String[] {"value"}, "name=?", new String[] {CHARGING_CONTROL_ENABLED},
                null)) {
            return c != null && c.moveToFirst() && "1".equals(c.getString(0));
        } catch (RuntimeException e) {
            Log.e(TAG, "Couldn't read " + CHARGING_CONTROL_ENABLED, e);
            return false;
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
