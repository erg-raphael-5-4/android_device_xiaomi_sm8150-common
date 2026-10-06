/*
 * SPDX-FileCopyrightText: 2026 ergdevops
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.poweroffalarm;

import android.app.AlarmManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.util.Log;

/**
 * Hands the next alarm clock to the QTI PowerOffAlarm app, which programs the
 * PMIC RTC so the phone powers on shortly before the alarm. Clock apps other
 * than the CAF/Lineage DeskClock (e.g. Google Clock) don't talk to it.
 */
public class PowerOffAlarmReceiver extends BroadcastReceiver {

    private static final String TAG = "XiaomiParts-PowerOffAlarm";

    private static final String POWER_OFF_ALARM_PACKAGE = "com.qualcomm.qti.poweroffalarm";
    private static final String ACTION_SET_ALARM =
            "org.codeaurora.poweroffalarm.action.SET_ALARM";
    private static final String ACTION_CANCEL_ALARM =
            "org.codeaurora.poweroffalarm.action.CANCEL_ALARM";
    private static final String EXTRA_TIME = "time";

    private static final String PREF_TIME = "power_off_alarm_time";

    @Override
    public void onReceive(Context context, Intent intent) {
        AlarmManager.AlarmClockInfo next =
                context.getSystemService(AlarmManager.class).getNextAlarmClock();
        long time = next != null ? next.getTriggerTime() : 0;

        // Device protected storage: this also runs before the first unlock.
        SharedPreferences prefs = context.createDeviceProtectedStorageContext()
                .getSharedPreferences(TAG, Context.MODE_PRIVATE);
        long last = prefs.getLong(PREF_TIME, 0);

        // The RTC alarm is computed from the wall clock when it's set, so set
        // it again after the time changes or a reboot, not only on new alarms.
        boolean resend = !AlarmManager.ACTION_NEXT_ALARM_CLOCK_CHANGED.equals(intent.getAction());
        if (time == last && !resend) {
            return;
        }

        if (last != 0 && last != time) {
            send(context, ACTION_CANCEL_ALARM, last);
        }
        if (time != 0) {
            send(context, ACTION_SET_ALARM, time);
        }
        prefs.edit().putLong(PREF_TIME, time).apply();
    }

    private static void send(Context context, String action, long time) {
        Log.i(TAG, action + " " + time);
        Intent intent = new Intent(action);
        intent.setPackage(POWER_OFF_ALARM_PACKAGE);
        intent.addFlags(Intent.FLAG_RECEIVER_FOREGROUND);
        intent.putExtra(EXTRA_TIME, time);
        context.sendBroadcast(intent);
    }
}
