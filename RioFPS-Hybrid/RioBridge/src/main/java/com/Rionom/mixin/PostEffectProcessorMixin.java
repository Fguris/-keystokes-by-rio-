package com.Rionom.mixin;

import com.Rionom.bridge.BridgeState;
import net.minecraft.client.gl.PostEffectPipeline;
import net.minecraft.client.gl.PostEffectProcessor;
import net.minecraft.client.gl.UniformValue;
import net.minecraft.util.Identifier;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Mixin(PostEffectProcessor.class)
public abstract class PostEffectProcessorMixin {
    private static final Identifier RIOFPS_OUTLINE_BLUR =
            Identifier.of("minecraft", "post/entity_outline_box_blur");

    @ModifyVariable(
            method = "parsePass(Lnet/minecraft/client/texture/TextureManager;Lnet/minecraft/client/gl/PostEffectPipeline$Pass;Lnet/minecraft/util/Identifier;)Lnet/minecraft/client/gl/PostEffectPass;",
            at = @At("HEAD"),
            argsOnly = true
    )
    private static PostEffectPipeline.Pass riofps$changeOutlineRadius(PostEffectPipeline.Pass pass) {
        if (!RIOFPS_OUTLINE_BLUR.equals(pass.fragmentShaderId())) {
            return pass;
        }

        Map<String, List<UniformValue>> uniforms = new HashMap<>(pass.uniforms());
        List<UniformValue> blurConfig = uniforms.get("BlurConfig");
        if (blurConfig == null || blurConfig.isEmpty()) {
            return pass;
        }

        List<UniformValue> modified = new ArrayList<>(blurConfig);
        float radius = BridgeState.glEnabled() ? BridgeState.glWidth() : 2.0F;

        for (int i = modified.size() - 1; i >= 0; i--) {
            if (modified.get(i) instanceof UniformValue.FloatValue) {
                modified.set(i, new UniformValue.FloatValue(radius));
                break;
            }
        }

        uniforms.put("BlurConfig", modified);
        return new PostEffectPipeline.Pass(
                pass.vertexShaderId(),
                pass.fragmentShaderId(),
                pass.inputs(),
                pass.outputTarget(),
                uniforms
        );
    }
}
