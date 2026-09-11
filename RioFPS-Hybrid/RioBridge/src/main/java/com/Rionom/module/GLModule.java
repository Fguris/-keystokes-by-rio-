package com.Rionom.module;

import com.Rionom.bridge.BridgeState;
import net.minecraft.client.MinecraftClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.PlayerEntity;

public final class GLModule {
    private boolean lastEnabled;
    private int lastThickness = -1;

    public boolean shouldOutline(MinecraftClient client, Entity entity) {
        if (!BridgeState.glEnabled() || client.player == null || !(entity instanceof PlayerEntity target)) {
            return false;
        }

        if (target == client.player || !target.isAlive() || target.isSpectator()) {
            return false;
        }

        if (BridgeState.glMode() == BridgeState.GLMode.VISIBLE) {
            return !target.isInvisible() && client.player.canSee(target);
        }

        return true;
    }

    public int getColor() {
        return switch (BridgeState.glColor()) {
            case RED -> 0xFF5555;
            case WHITE -> 0xFFFFFF;
            case BLUE -> 0x5599FF;
        };
    }

    public int getThickness() {
        return BridgeState.glWidth();
    }

    public void tickRenderer(MinecraftClient client) {
        boolean enabled = BridgeState.glEnabled();
        int thickness = BridgeState.glWidth();

        if (enabled == lastEnabled && thickness == lastThickness) {
            return;
        }

        lastEnabled = enabled;
        lastThickness = thickness;

        if (client.worldRenderer != null) {
            client.worldRenderer.loadEntityOutlinePostProcessor();
        }
    }
}
