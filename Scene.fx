texture input;

sampler2D inputSampler =
sampler_state
{
    Texture = <input>;
    AddressU = Border;
    AddressV = Border;
    MinFilter = Linear;
    MagFilter = Linear;
    MipFilter = Linear;
};

//Determines how strong the barrel distortion is,
//positive values will pincushion instead.
//The current value is derived from John Carmack D3BFG screenshots.
float barrelFactor = (-81.0/10.0); 

float4 BarrelWarp( float2 Tex : TEXCOORD0) : COLOR
{
    float2 v = float2(Tex);
    v *= 2.0; //[0,1]-based -> [-1,1]
    v -= 1.0;
    float2 warped = float2(
        barrelFactor*v.x/(v.y*v.y + barrelFactor), 
        barrelFactor*v.y/(v.x*v.x + barrelFactor));
    warped += 1.0; //[-1,1] back into [0,1]
    warped *= 0.5;

    return tex2D(inputSampler, warped);
}

technique RenderBarrelWarp
{
    pass p0
    {
        VertexShader = null;
        PixelShader = compile ps_2_0 BarrelWarp();
        ZEnable = false;
    }
}

//-----
texture distortionMap;
sampler2D distortionMapSampler =
sampler_state
{
    Texture = <distortionMap>;
    AddressU = Wrap;
    AddressV = Wrap;
    MinFilter = Point;
    MagFilter = Linear;
    MipFilter = Linear;
};

float4 DistortionMap(float2 Tex : TEXCOORD0) : COLOR
{
    float2 offset = tex2D(distortionMapSampler, Tex).xy;
    offset *= 2.0; //Scale from [0,1]->[0,2]
    offset -= 1.0; //[0,2]->[-1,1]
    return tex2D(inputSampler, Tex+offset);
}

technique RenderDistortionMap
{
    pass p0
    {
        VertexShader = null;
        PixelShader = compile ps_2_0 DistortionMap();
        ZEnable = false;
    }
}