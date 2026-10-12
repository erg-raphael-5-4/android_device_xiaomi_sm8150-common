/*
 * SPDX-FileCopyrightText: 2026 ergdevops
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.bypass;

import android.os.Bundle;

import com.android.settingslib.collapsingtoolbar.CollapsingToolbarBaseActivity;

public class BypassChargingSettingsActivity extends CollapsingToolbarBaseActivity {

    private static final String TAG_BYPASS_CHARGING = "bypass_charging";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getFragmentManager()
                .beginTransaction()
                .replace(com.android.settingslib.collapsingtoolbar.R.id.content_frame,
                        new BypassChargingSettingsFragment(), TAG_BYPASS_CHARGING)
                .commit();
    }
}
