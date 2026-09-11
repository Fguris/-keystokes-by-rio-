package com.Rionom;

import com.Rionom.bridge.BridgeServer;
import com.Rionom.core.ModuleManager;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public final class RioFPSClient implements ClientModInitializer {
    public static final String MOD_ID = "riofps";
    public static final Logger LOGGER = LoggerFactory.getLogger("RioFPS");

    private static final BridgeServer BRIDGE = new BridgeServer();

    @Override
    public void onInitializeClient() {
        BRIDGE.start();
        ClientTickEvents.END_CLIENT_TICK.register(ModuleManager::tick);
        LOGGER.info("RioFPS bridge initialized for Minecraft 1.21.11");
    }
}
