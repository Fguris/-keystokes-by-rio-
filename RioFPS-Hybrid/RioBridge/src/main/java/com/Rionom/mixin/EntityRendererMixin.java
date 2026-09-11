package com.Rionom.mixin;

import com.Rionom.core.ModuleManager;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.entity.EntityRenderer;
import net.minecraft.client.render.entity.state.EntityRenderState;
import net.minecraft.entity.Entity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(EntityRenderer.class)
public abstract class EntityRendererMixin {
    @Inject(method = "updateRenderState", at = @At("TAIL"))
    private void riofps$setOutlineColor(Entity entity, EntityRenderState state, float tickProgress, CallbackInfo ci) {
        MinecraftClient client = MinecraftClient.getInstance();
        if (ModuleManager.GL.shouldOutline(client, entity)) {
            state.outlineColor = ModuleManager.GL.getColor();
        }
    }
}
