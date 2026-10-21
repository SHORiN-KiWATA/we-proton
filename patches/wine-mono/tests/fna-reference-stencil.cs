using System;
using System.Reflection;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

// Run against both baseline and patched WineMono.FNA on SDL_GPU and D3D11.
// No game assets are required.
internal sealed class StencilOrderTest : Game
{
    private readonly GraphicsDeviceManager manager;

    private StencilOrderTest()
    {
        manager = new GraphicsDeviceManager(this);
        manager.PreferredBackBufferWidth = 64;
        manager.PreferredBackBufferHeight = 64;
        manager.PreferredDepthStencilFormat = DepthFormat.Depth24Stencil8;
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
            throw new Exception(message);
    }

    // Exercise the effect-state merge without needing a compiled game shader.
    private static void MergeEffectDepthState(GraphicsDevice device, int? reference)
    {
        var field = typeof(GraphicsDevice).GetField("PipelineCache",
            BindingFlags.Instance | BindingFlags.NonPublic);
        Check(field != null, "FNA PipelineCache field not found");
        object cache = field.GetValue(device);
        Type type = cache.GetType();
        type.GetMethod("BeginApplyDepthStencil").Invoke(cache, null);
        type.GetField("DepthBufferWriteEnable").SetValue(cache, true);
        if (reference.HasValue)
            type.GetField("ReferenceStencil").SetValue(cache, reference.Value);
        type.GetMethod("EndApplyDepthStencil").Invoke(cache, null);
    }

    protected override void LoadContent()
    {
        GraphicsDevice device = GraphicsDevice;
        using (var target = new RenderTarget2D(device, 16, 16, false,
            SurfaceFormat.Color, DepthFormat.Depth24Stencil8))
        using (var effect = new BasicEffect(device))
        using (var state = new DepthStencilState
        {
            DepthBufferEnable = false,
            StencilEnable = true,
            StencilFunction = CompareFunction.NotEqual,
            StencilPass = StencilOperation.Replace,
            ReferenceStencil = 0
        })
        using (var other = new DepthStencilState { ReferenceStencil = 3 })
        {
            var vertices = new[]
            {
                new VertexPositionColor(new Vector3(-1, -1, 0), Color.White),
                new VertexPositionColor(new Vector3(-1, 1, 0), Color.White),
                new VertexPositionColor(new Vector3(1, -1, 0), Color.White)
            };
            var pixels = new Color[16 * 16];
            effect.VertexColorEnabled = true;
            device.SetRenderTarget(target);
            device.Clear(ClearOptions.Target | ClearOptions.DepthBuffer |
                ClearOptions.Stencil, Color.Black, 1, 0);
            device.BlendState = BlendState.Opaque;
            device.RasterizerState = RasterizerState.CullNone;

            // SunBurn sequence: queue a state, override reference, then draw.
            // Do not read ReferenceStencil before drawing: that could mask a bug.
            device.DepthStencilState = state;
            device.ReferenceStencil = 1;
            effect.CurrentTechnique.Passes[0].Apply();
            device.DrawUserPrimitives(PrimitiveType.TriangleList, vertices, 0, 1);
            Check(device.ReferenceStencil == 1, "Draw overwrote explicit reference");
            Check(state.ReferenceStencil == 0, "Mutated the caller's state object");
            device.SetRenderTarget(null);
            target.GetData(pixels);
            Check(pixels[8 * 16 + 4].R > 200, "Stencil test rejected lit pixels");

            // Dynamic changes must also survive draws without a state switch.
            device.SetRenderTarget(target);
            device.Clear(ClearOptions.Target | ClearOptions.Stencil, Color.Black, 1, 1);
            device.ReferenceStencil = 2;
            effect.CurrentTechnique.Passes[0].Apply();
            device.DrawUserPrimitives(PrimitiveType.TriangleList, vertices, 0, 1);
            Check(device.ReferenceStencil == 2, "Repeated draw reset the reference");
            device.SetRenderTarget(null);
            target.GetData(pixels);
            Check(pixels[8 * 16 + 4].R > 200, "Dynamic reference did not reach GPU");

            // Effect states that only change depth writes must inherit the
            // device override, not the original state's zero reference.
            foreach (int reference in new[] { 220, 221, 220 })
            {
                device.SetRenderTarget(target);
                device.Clear(ClearOptions.Target | ClearOptions.Stencil, Color.Black, 1, 0);
                device.DepthStencilState = state;
                device.ReferenceStencil = reference;
                MergeEffectDepthState(device, null);
                effect.CurrentTechnique.Passes[0].Apply();
                device.DrawUserPrimitives(PrimitiveType.TriangleList, vertices, 0, 1);
                Check(device.ReferenceStencil == reference, "Effect reset dynamic reference");
                Check(state.ReferenceStencil == 0, "Effect mutated caller's state");
                device.SetRenderTarget(null);
                target.GetData(pixels);
                Check(pixels[8 * 16 + 4].R > 200, "Effect merge rejected lit pixels");
            }

            // An explicit effect reference, including zero, still wins.
            foreach (int reference in new[] { 0, 3 })
            {
                device.SetRenderTarget(target);
                device.Clear(ClearOptions.Target | ClearOptions.Stencil, Color.Black, 1, 0);
                device.DepthStencilState = state;
                device.ReferenceStencil = 220;
                MergeEffectDepthState(device, reference);
                effect.CurrentTechnique.Passes[0].Apply();
                device.DrawUserPrimitives(PrimitiveType.TriangleList, vertices, 0, 1);
                Check(device.ReferenceStencil == reference, "Ignored explicit effect reference");
                device.SetRenderTarget(null);
                target.GetData(pixels);
                Check(reference == 0 ? pixels[8 * 16 + 4].R == 0 : pixels[8 * 16 + 4].R > 200,
                    "Explicit effect reference produced wrong pixels");
            }

            device.DepthStencilState = other;
            MergeEffectDepthState(device, null);
            Check(device.ReferenceStencil == 3, "Effect ignored pending state's reference");

            // Reverse order: assigning a different state replaces the reference.
            device.ReferenceStencil = 9;
            device.DepthStencilState = other;
            Check(device.ReferenceStencil == 3, "Getter ignored pending state");
            device.DepthStencilState = state;
            Check(device.ReferenceStencil == 0, "Rebinding did not restore state reference");
        }
        Console.WriteLine("PASS: stencil reference ordering, effect merging and rendered pixels");
        Exit();
    }

    public static int Main()
    {
        try
        {
            using (var game = new StencilOrderTest())
                game.Run();
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
