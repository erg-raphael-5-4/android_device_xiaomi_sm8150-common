/*
 * SPDX-FileCopyrightText: 2026 ergdevops
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.bypass;

import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

import org.lineageos.settings.R;

public class BypassChargingTileService extends TileService {

    @Override
    public void onStartListening() {
        super.onStartListening();
        updateTile(BypassChargingUtils.isEnabled(this));
    }

    @Override
    public void onClick() {
        super.onClick();
        boolean enabled = !BypassChargingUtils.isEnabled(this);
        BypassChargingUtils.setEnabled(this, enabled);
        updateTile(enabled);
    }

    private void updateTile(boolean enabled) {
        Tile tile = getQsTile();
        if (tile == null) {
            return;
        }
        tile.setState(enabled ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.setSubtitle(getString(enabled
                ? R.string.bypass_charging_tile_on : R.string.bypass_charging_tile_off));
        tile.updateTile();
    }
}
