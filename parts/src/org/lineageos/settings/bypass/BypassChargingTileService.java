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
        updateTile();
    }

    @Override
    public void onClick() {
        super.onClick();
        if (BypassChargingUtils.isChargingControlEnabled(this)) {
            updateTile();
            return;
        }
        BypassChargingUtils.setEnabled(this, !BypassChargingUtils.isEnabled(this));
        updateTile();
    }

    private void updateTile() {
        Tile tile = getQsTile();
        if (tile == null) {
            return;
        }
        if (BypassChargingUtils.isChargingControlEnabled(this)) {
            tile.setState(Tile.STATE_UNAVAILABLE);
            tile.setSubtitle(getString(R.string.bypass_charging_tile_blocked));
        } else {
            boolean enabled = BypassChargingUtils.isEnabled(this);
            tile.setState(enabled ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
            tile.setSubtitle(getString(enabled
                    ? R.string.bypass_charging_tile_on : R.string.bypass_charging_tile_off));
        }
        tile.updateTile();
    }
}
