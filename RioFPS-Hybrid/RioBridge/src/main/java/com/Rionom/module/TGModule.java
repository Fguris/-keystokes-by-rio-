package com.Rionom.module;

import com.Rionom.bridge.BridgeState;
import com.Rionom.mixin.MinecraftClientInvoker;
import com.Rionom.mixin.PlayerEntityInvoker;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayNetworkHandler;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.network.packet.c2s.play.ClientCommandC2SPacket;

public final class TGModule {
    private static final float COMBA_COOLDOWN = 0.99F;
    private static final float KRIT_COOLDOWN = 0.90F;

    public void tick(MinecraftClient client) {
        if (!BridgeState.tgEnabled() || client.player == null || client.interactionManager == null) {
            return;
        }

        if (!(client.targetedEntity instanceof PlayerEntity target)) {
            return;
        }

        if (target == client.player || !target.isAlive() || target.isSpectator()) {
            return;
        }

        float cooldown = client.player.getAttackCooldownProgress(0.5F);

        if (BridgeState.tgMode() == BridgeState.TGMode.COMBA) {
            if (cooldown < COMBA_COOLDOWN) {
                return;
            }

            // COMBA intentionally avoids a critical-hit window.
            if (((PlayerEntityInvoker) client.player).riofps$isCriticalHit(target)) {
                return;
            }

            attack(client, target);
            return;
        }

        if (cooldown < KRIT_COOLDOWN) {
            return;
        }

        // Fast pre-filter for an actual falling crit window. The private
        // vanilla predicate below remains the final authority.
        if (client.player.fallDistance <= 0.0D
                || client.player.isOnGround()
                || client.player.isTouchingWater()
                || client.player.hasVehicle()) {
            return;
        }

        boolean restoreSprint = client.player.isSprinting();
        if (restoreSprint) {
            setSprintState(client, false);
        }

        boolean critical = ((PlayerEntityInvoker) client.player).riofps$isCriticalHit(target);
        if (!critical) {
            if (restoreSprint) {
                setSprintState(client, true);
            }
            return;
        }

        attack(client, target);

        if (restoreSprint) {
            setSprintState(client, true);
        }
    }

    private static void attack(MinecraftClient client, PlayerEntity target) {
        if (client.targetedEntity != target) {
            return;
        }

        // Use Minecraft's normal left-click attack handler rather than
        // calling interactionManager.attackEntity directly.
        ((MinecraftClientInvoker) client).riofps$doAttack();
    }

    private static void setSprintState(MinecraftClient client, boolean sprinting) {
        if (client.player == null) {
            return;
        }

        client.player.setSprinting(sprinting);

        ClientPlayNetworkHandler networkHandler = client.getNetworkHandler();
        if (networkHandler != null) {
            networkHandler.sendPacket(new ClientCommandC2SPacket(
                    client.player,
                    sprinting
                            ? ClientCommandC2SPacket.Mode.START_SPRINTING
                            : ClientCommandC2SPacket.Mode.STOP_SPRINTING
            ));
        }
    }
}
