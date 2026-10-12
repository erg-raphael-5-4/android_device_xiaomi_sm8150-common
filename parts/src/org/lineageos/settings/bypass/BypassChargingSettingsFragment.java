/*
 * SPDX-FileCopyrightText: 2026 ergdevops
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.bypass;

import android.os.Bundle;

import androidx.preference.Preference;
import androidx.preference.Preference.OnPreferenceChangeListener;
import androidx.preference.PreferenceFragment;
import androidx.preference.SwitchPreferenceCompat;

import org.lineageos.settings.R;

public class BypassChargingSettingsFragment
        extends PreferenceFragment implements OnPreferenceChangeListener {

    private static final String KEY_BYPASS_CHARGING = "bypass_charging";

    private SwitchPreferenceCompat mBypassCharging;

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        addPreferencesFromResource(R.xml.bypass_charging_settings);
        mBypassCharging = findPreference(KEY_BYPASS_CHARGING);
        mBypassCharging.setOnPreferenceChangeListener(this);
    }

    @Override
    public void onResume() {
        super.onResume();
        // The quick settings tile can change the state too.
        mBypassCharging.setChecked(BypassChargingUtils.isEnabled(getContext()));
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        BypassChargingUtils.setEnabled(getContext(), (Boolean) newValue);
        return true;
    }
}
