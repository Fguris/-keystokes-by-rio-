package com.Rionom.core;

import com.Rionom.module.GLModule;
import com.Rionom.module.TGModule;
import net.minecraft.client.MinecraftClient;

public final class ModuleManager {
    public static final TGModule TG = new TGModule();
    public static final GLModule GL = new GLModule();

    private ModuleManager() {}

    public static void tick(MinecraftClient client) {
        TG.tick(client);
        GL.tickRenderer(client);
    }
}
