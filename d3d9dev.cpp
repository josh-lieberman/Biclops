#include <windows.h>
#include <fstream>
#include <ostream>
#include <iostream>
#include <list>
#include "main.h"
#include "d3d9.h"

using namespace std;

struct VERT
{
    float x, y, z, rhw;
    float tu, tv;       
	const static D3DVERTEXELEMENT9 Decl[4];
};

const D3DVERTEXELEMENT9 VERT::Decl[4] =
{
    { 0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 },
    { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  0 },
    D3DDECL_END()
};
IDirect3DVertexDeclaration9* vertDeclaration;

bool isLeft = true; //Flip-flops between frames to determine if we're drawing the left or right view.
bool showCrossEyed = false; //False for HMDs, true for "cross-eyed" stereo viewing

IDirect3DSurface9* backBuffer = NULL;
IDirect3DTexture9* preWarpBuffer = NULL; //What we send to the warping pixel shader
ID3DXEffect* effect;

float frustumOffset = 0.011696f; //0.145433f; //How far each view frustum is horizontally asymmetric
float eyeOffset = 0.175539f; //1.082709f; //How far each eye moves in the x-axis

bool dirtyRegisters[300]; //Mark which vertex shader registers have been updated by SetVertexShaderConstantF

HRESULT renderToPrewarpBuffer(IDirect3DDevice9* pD3Ddev, DWORD renderTargetIndex)
{
	IDirect3DSurface9* textureSurface;
	preWarpBuffer->GetSurfaceLevel(0, &textureSurface);
	HRESULT hr = pD3Ddev->SetRenderTarget(renderTargetIndex,textureSurface);
	_SAFE_RELEASE(textureSurface);
	return hr;
}

HRESULT APIENTRY hkIDirect3DDevice9::SetRenderTarget(DWORD renderTargetIndex, IDirect3DSurface9* renderTarget) 
{	
	return (renderTarget == backBuffer) ? 
		renderToPrewarpBuffer(m_pD3Ddev, renderTargetIndex) 
		: m_pD3Ddev->SetRenderTarget(renderTargetIndex,renderTarget);
}

D3DVIEWPORT9 buildViewport(UINT width, UINT height)
{
	D3DVIEWPORT9 viewport;
	viewport.Width = width;
	viewport.X = isLeft^showCrossEyed ? 0 : viewport.Width+1;
	viewport.Y = 0;
	viewport.Height = height;
	viewport.MaxZ = 1.0f;
	viewport.MinZ = 0.0f;
	return viewport;
}

void renderBarrelWarp(IDirect3DDevice9* pD3Ddev, VERT* quad)
{
	pD3Ddev->BeginScene();
	effect->SetTechnique(effect->GetTechniqueByName("RenderBarrelWarp"));
	effect->SetTexture("input", preWarpBuffer);
	UINT cPasses;
	effect->Begin(&cPasses, 0);
	for(UINT p = 0; p < cPasses; ++p)
	{
		effect->BeginPass(p);
		pD3Ddev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(VERT));
		effect->EndPass();
	}
	effect->End();
	pD3Ddev->EndScene();
}

void drawWarpedBuffer(IDirect3DDevice9* pD3Ddev)
{
	pD3Ddev->SetRenderTarget(0, backBuffer);
	
	D3DSURFACE_DESC pd3dsdBackBuffer;
	backBuffer->GetDesc(&pd3dsdBackBuffer);
	
	D3DVIEWPORT9 viewport = buildViewport(pd3dsdBackBuffer.Width/2, pd3dsdBackBuffer.Height);
	pD3Ddev->SetViewport(&viewport);
	
	float width = (float)viewport.Width;
	float height = (float)pd3dsdBackBuffer.Height;
	float x = isLeft^showCrossEyed ? 0.0f : width+1.0f;
	float y = 0.0f;

	x -= 0.5f; y -= 0.5f; //correct for tex sampling
    VERT quad[4] =
    {
        { x,		y,			0.5f, 1.0f, 0.0f, 0.0f},
        { x+width,	y,			0.5f, 1.0f, 1.0f, 0.0f},
        { x,		y+height,	0.5f, 1.0f, 0.0f, 1.0f},
        { x+width,	y+height,	0.5f, 1.0f, 1.0f, 1.0f}
	};
	pD3Ddev->SetVertexDeclaration(vertDeclaration);
	
	renderBarrelWarp(pD3Ddev, quad);
}

void update3DConstants() {
	if(GetAsyncKeyState(VK_HOME))
	{
		frustumOffset *= 1.01f;
	}
	if(GetAsyncKeyState(VK_END))
	{
		frustumOffset *= 0.99f;
	}
	if(GetAsyncKeyState(VK_PRIOR))
	{
		eyeOffset *= 1.01f;
	}
	if(GetAsyncKeyState(VK_NEXT))
	{
		eyeOffset *= 0.99f;
	}
	if(GetAsyncKeyState(VK_INSERT))
	{
		add_log("SETTINGS: frustum: %f  eye: %f", frustumOffset, eyeOffset);
	}
	if(GetAsyncKeyState(VK_DELETE))
	{
		showCrossEyed = !showCrossEyed;
	}
}

HRESULT APIENTRY hkIDirect3DDevice9::Present(CONST RECT *pSourceRect, CONST RECT *pDestRect, HWND hDestWindowOverride, CONST RGNDATA *pDirtyRegion) 
{
	update3DConstants();
	drawWarpedBuffer(m_pD3Ddev);

	isLeft = !isLeft; //Flip frames
	if(!isLeft) return 0; //flip backbuffers only every other frame
	return m_pD3Ddev->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}


struct TargetMatrix {
	UINT startRegister;
	void (*transformationFunc)(IDirect3DDevice9*, int);
	TargetMatrix():startRegister(0),transformationFunc(NULL) {}
};

HRESULT APIENTRY hkIDirect3DDevice9::SetVertexShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount) 
{
	for(UINT i = StartRegister; i < StartRegister + Vector4fCount; i++)
	{
		dirtyRegisters[i] = true;
	}

	return m_pD3Ddev->SetVertexShaderConstantF(StartRegister,pConstantData,Vector4fCount);	
}

void transformIndividualFloat(IDirect3DDevice9 *pD3Ddev, int startRegister) 
{
	float pConstantData[4];
	pD3Ddev->GetVertexShaderConstantF(startRegister, pConstantData, 1);
	pConstantData[0] += isLeft ? eyeOffset : -eyeOffset;
	pD3Ddev->SetVertexShaderConstantF(startRegister, pConstantData, 1);
	memset(&dirtyRegisters[startRegister], 0, sizeof(boolean));
}

const float n = 0.1f;
const float f = 10.0f;
const float l = -0.5f;
const float r = 0.5f;
const float t = 0.5f;
const float b = -0.5f;

const D3DXMATRIX invertProjection(
	1.0f/(2.0f*n),	0.0f,			0.0f,	0.0f,
	0.0f,			1.0f/(2.0f*n),	0.0f,	0.0f,
	0.0f,			0.0f,			0.0f,	(n-f)/(f*n),
	0.0f,			0.0f,			1.0f,	1.0f/n
);

void adjustEyeOffsetAndViewFrustum(D3DXMATRIX &outMatrix, D3DXMATRIX &inMatrix)
{
	D3DXMATRIX transform;
	D3DXMatrixTranslation(&transform, isLeft ? eyeOffset: -eyeOffset, 0, 0);

	float adjustedFrustumOffset = isLeft ? frustumOffset : -frustumOffset;		
	D3DXMATRIX reProject;
	D3DXMatrixPerspectiveOffCenterLH(&reProject, l+adjustedFrustumOffset, r+adjustedFrustumOffset, b, t, n, f);

	outMatrix = inMatrix * invertProjection * transform * reProject;
}

void transformRowMajor4by4(IDirect3DDevice9 *pD3Ddev, int startRegister) 
{
	float pConstantData[16];
	pD3Ddev->GetVertexShaderConstantF(startRegister, pConstantData, 4);
	D3DXMATRIX original = D3DXMATRIX(pConstantData);
	D3DXMATRIX transposed;
	D3DXMatrixTranspose(&transposed, &original);
	adjustEyeOffsetAndViewFrustum(original, transposed);
	D3DXMatrixTranspose(&transposed, &original);
	pD3Ddev->SetVertexShaderConstantF(startRegister, transposed,4);
	memset(&dirtyRegisters[startRegister], 0, sizeof(boolean)*4);
}

void transform4by4(IDirect3DDevice9 *pD3Ddev, int startRegister) 
{
	float pConstantData[16];
	pD3Ddev->GetVertexShaderConstantF(startRegister, pConstantData, 4);
	D3DXMATRIX inMatrix = D3DXMATRIX(pConstantData);
	if(inMatrix._41 == 0.0f) return; //Mirror's Edge compat. hack (otherwise you get weird squares)
	D3DXMATRIX outMatrix;
	adjustEyeOffsetAndViewFrustum(outMatrix, inMatrix);
	pD3Ddev->SetVertexShaderConstantF(startRegister, outMatrix,4);
	memset(&dirtyRegisters[startRegister], 0, sizeof(boolean)*4);
}

list<TargetMatrix> targetMatrices;
void transformDirtyShaderParams(IDirect3DDevice9 *pD3Ddev)
{
	for(list<TargetMatrix>::iterator i = targetMatrices.begin(); i != targetMatrices.end(); i++)
	{
		if(dirtyRegisters[i->startRegister])
		{
			i->transformationFunc(pD3Ddev, i->startRegister);
		}
	}
}

void removeExistingMatrices(D3DXCONSTANT_DESC &desc)
{	list<TargetMatrix>::iterator i = targetMatrices.begin();
	while(i != targetMatrices.end())
	{
		if(desc.RegisterIndex <= (*i).startRegister && desc.RegisterIndex+desc.RegisterCount > (*i).startRegister)
		{
			targetMatrices.erase(i++);
		}
		else
		{
			++i;
		}
	}
}

void parse4by4Matrices(D3DXCONSTANT_DESC &desc)
{
	if(desc.Name == NULL) return;
	if(!strstr(desc.Name, "proj") && !strstr(desc.Name, "Proj"))return;
	if(desc.RegisterCount != 4) return;			
	TargetMatrix tm;
	tm.startRegister = desc.RegisterIndex;
	tm.transformationFunc = (desc.Class == 2) ? &transformRowMajor4by4 : &transform4by4;
	targetMatrices.push_back(tm);
}


void parseIndividualFloats(D3DXCONSTANT_DESC &desc)
{
	if(desc.Name == NULL) return;
	if(!strstr(desc.Name, "EyePos")) return;
	if(desc.RegisterCount != 1) return;
	TargetMatrix tm;
	tm.startRegister = desc.RegisterIndex;
	tm.transformationFunc = &transformIndividualFloat;
	targetMatrices.push_back(tm);
}

void findWeirdMirrorsEdgeShader(UINT pSizeOfData)
{	
	if(pSizeOfData != 172) return;
	TargetMatrix tm;
	tm.startRegister = 0;

	tm.transformationFunc = &transform4by4;
	targetMatrices.push_back(tm);
}

list<UINT> seenShaders;

bool hasSeenShader(UINT size)
{
	for(list<UINT>::iterator i = seenShaders.begin(); i != seenShaders.end(); i++)
	{
		if(*i == size) return true;
	}
	return false;
}

HRESULT APIENTRY hkIDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9* pvShader) 
{	
	IDirect3DVertexShader9* pShader = NULL;
	LPD3DXCONSTANTTABLE pConstantTable = NULL;
	BYTE *pData = NULL;

	HRESULT hr = m_pD3Ddev->SetVertexShader(pvShader);
	m_pD3Ddev->GetVertexShader(&pShader);
	UINT pSizeOfData;
	if(NULL == pShader) goto grexit;
	pShader->GetFunction(NULL,&pSizeOfData);
	findWeirdMirrorsEdgeShader(pSizeOfData);
	pData = new BYTE[pSizeOfData];
	pShader->GetFunction(pData,&pSizeOfData);

	bool shaderSeen = hasSeenShader(pSizeOfData);

    D3DXCONSTANT_DESC pConstantDesc[32];
    UINT pConstantNum = 32;

    D3DXGetShaderConstantTable(reinterpret_cast<DWORD*>(pData),&pConstantTable);
	if(pConstantTable == NULL) goto grexit;
    D3DXCONSTANTTABLE_DESC pDesc;
    pConstantTable->GetDesc(&pDesc);
	for(UINT i = 0; i < pDesc.Constants; i++)
	{
		D3DXHANDLE Handle = pConstantTable->GetConstant(NULL,i);
		if(Handle == NULL) continue;
		pConstantTable->GetConstantDesc(Handle,pConstantDesc,&pConstantNum);
		for(UINT j = 0; j < pConstantNum; j++)
		{
			removeExistingMatrices(pConstantDesc[j]);
			parse4by4Matrices(pConstantDesc[j]);
			parseIndividualFloats(pConstantDesc[j]);			
		}
	}
	
#ifdef SHADER_DEBUG
	if(!shaderSeen)
	{
		add_log("---------------");
		add_log("SHADER SIZE: %d", pSizeOfData);
		LPD3DXBUFFER bOut;
		D3DXDisassembleShader(reinterpret_cast<DWORD*>(pData),NULL,NULL,&bOut);
		add_log("%s", bOut->GetBufferPointer());
		ReadConstantTable(pData);
		seenShaders.push_back(pSizeOfData);
	}
#endif
grexit:
	_SAFE_RELEASE(pConstantTable);
	_SAFE_RELEASE(pShader);
	if(pData) delete[] pData;
	return hr;
}

HRESULT CD3DManager::Initialize()
{
	memset(dirtyRegisters,0,sizeof(bool)*300);
	D3DXCreateEffectFromFile(m_pD3Ddev, "Scene.fx", NULL, NULL, NULL, NULL, &effect, NULL);	
	m_pD3Ddev->CreateVertexDeclaration(VERT::Decl, &vertDeclaration);

	m_pD3Ddev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
	D3DSURFACE_DESC desc;
	backBuffer->GetDesc(&desc);
	m_pD3Ddev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &preWarpBuffer, NULL);

	return S_OK;
}

HRESULT CD3DManager::PreReset()
{
	return CD3DManager::Release();
}

HRESULT CD3DManager::PostReset()
{
	return CD3DManager::Initialize();
}

HRESULT CD3DManager::Release()
{
	_SAFE_RELEASE(preWarpBuffer);
	_SAFE_RELEASE(backBuffer);
	_SAFE_RELEASE(effect);
	_SAFE_RELEASE(vertDeclaration);
	return S_OK;
}

HRESULT APIENTRY hkIDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9 *pViewport) 
{
	return 0; //We're handling our own viewports internally, so suppress target calls.
}

HRESULT APIENTRY hkIDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE Type,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount)
{
	transformDirtyShaderParams(m_pD3Ddev);
	return m_pD3Ddev->DrawIndexedPrimitive(Type,BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinIndex, UINT NumVertices, UINT PrimitiveCount, CONST void *pIndexData, D3DFORMAT IndexDataFormat, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride) 
{	
	transformDirtyShaderParams(m_pD3Ddev);
	return m_pD3Ddev->DrawIndexedPrimitiveUP(PrimitiveType, MinIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
}

HRESULT APIENTRY hkIDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) 
{
	transformDirtyShaderParams(m_pD3Ddev);
	return m_pD3Ddev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride) 
{
	transformDirtyShaderParams(m_pD3Ddev);
	return m_pD3Ddev->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}

HRESULT APIENTRY hkIDirect3DDevice9::DrawRectPatch(UINT Handle, CONST float *pNumSegs, CONST D3DRECTPATCH_INFO *pRectPatchInfo) 
{
	transformDirtyShaderParams(m_pD3Ddev);
	return m_pD3Ddev->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
}

HRESULT APIENTRY hkIDirect3DDevice9::DrawTriPatch(UINT Handle, CONST float *pNumSegs, CONST D3DTRIPATCH_INFO *pTriPatchInfo)
{	
	transformDirtyShaderParams(m_pD3Ddev);
	return m_pD3Ddev->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
}

//Courtesy of http://www.gamedeception.net/threads/16538-World-to-Screen -- thanks ntKid/Azorbix.
void ReadConstantTable(BYTE* pData)
{
    LPD3DXCONSTANTTABLE pConstantTable;
    D3DXCONSTANT_DESC pConstantDesc[32];
    UINT pConstantNum = 32;

    D3DXGetShaderConstantTable(reinterpret_cast<DWORD*>(pData),&pConstantTable);
	if(pConstantTable == NULL) return;
    D3DXCONSTANTTABLE_DESC pDesc;
    pConstantTable->GetDesc(&pDesc);
    for(UINT StartRegister = 0; StartRegister < pDesc.Constants; StartRegister++)
    {
        D3DXHANDLE Handle = pConstantTable->GetConstant(NULL,StartRegister);
        char* bClass[6] = {"D3DXPC_SCALAR","D3DXPC_VECTOR","D3DXPC_MATRIX_ROWS","D3DXPC_MATRIX_COLUMNS","D3DXPC_OBJECT","D3DXPC_STRUCT"};
        char* bRegisterSet[4] = {"D3DXRS_BOOL","D3DXRS_INT4","D3DXRS_FLOAT4","D3DXRS_SAMPLER"};
        char* bType[19] = {"D3DXPT_VOID","D3DXPT_BOOL","D3DXPT_INT","D3DXPT_FLOAT","D3DXPT_STRING","D3DXPT_TEXTURE","D3DXPT_TEXTURE1D","D3DXPT_TEXTURE2D","D3DXPT_TEXTURE3D","D3DXPT_TEXTURECUBE","D3DXPT_SAMPLER","D3DXPT_SAMPLER1D","D3DXPT_SAMPLER2D","D3DXPT_SAMPLER3D","D3DXPT_SAMPLERCUBE","D3DXPT_PIXELSHADER","D3DXPT_VERTEXSHADER","D3DXPT_PIXELFRAGMENT","D3DXPT_VERTEXFRAGMENT"};
        if(Handle != NULL)
        {
            pConstantTable->GetConstantDesc(Handle,pConstantDesc,&pConstantNum);
            for(UINT i =0; i < pConstantNum; i++)
            {
                add_log("Name=[%s]\nRegisterSet=[%s]\nRegisterIndex=[%d]\nRegisterCount=[%d]\nClass=[%s]\nType=[%s]\nRows=[%d]\nColumns=[%d]\nElements=[%d]\nStructMembers=[%d]\nBytes=[%d]\nDefaultValue=[0x%X]\nStartRegister=[%d]\nEND\n",
                    pConstantDesc[i].Name,
                    bRegisterSet[pConstantDesc[i].RegisterSet],
                    pConstantDesc[i].RegisterIndex,
                    pConstantDesc[i].RegisterCount,
                    bClass[pConstantDesc[i].Class],
                    bType[pConstantDesc[i].Type],
                    pConstantDesc[i].Rows,
                    pConstantDesc[i].Columns,
                    pConstantDesc[i].Elements,
                    pConstantDesc[i].StructMembers,
                    pConstantDesc[i].Bytes,
                    (DWORD)pConstantDesc[i].DefaultValue,
                    StartRegister);
            }
        }
	}
    pConstantTable->Release();
}

//Everything below here ought to be a dumb passthrough
//-----------------------------------------------------------------------------
#pragma region passthroughs
HRESULT APIENTRY hkIDirect3DDevice9::GetBackBuffer(UINT iSwapChain,UINT iBackBuffer,D3DBACKBUFFER_TYPE Type,IDirect3DSurface9** ppBackBuffer) 
{
	return m_pD3Ddev->GetBackBuffer(iSwapChain,iBackBuffer, Type, ppBackBuffer);
}

HRESULT APIENTRY hkIDirect3DDevice9::EndScene()
{	
	return m_pD3Ddev->EndScene();
}

HRESULT APIENTRY hkIDirect3DDevice9::QueryInterface(REFIID riid, LPVOID *ppvObj) 
{
	return m_pD3Ddev->QueryInterface(riid, ppvObj);
}

ULONG APIENTRY hkIDirect3DDevice9::AddRef() 
{
	m_refCount++;
	return m_pD3Ddev->AddRef();
}

HRESULT APIENTRY hkIDirect3DDevice9::BeginScene() 
{	
	return m_pD3Ddev->BeginScene();
}

HRESULT APIENTRY hkIDirect3DDevice9::BeginStateBlock() 
{
	return m_pD3Ddev->BeginStateBlock();
}

HRESULT APIENTRY hkIDirect3DDevice9::Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) 
{
 	return m_pD3Ddev->Clear(Count, pRects, Flags, Color, Z, Stencil);
}

HRESULT APIENTRY hkIDirect3DDevice9::ColorFill(IDirect3DSurface9* pSurface,CONST RECT* pRect, D3DCOLOR color) 
{	
	return m_pD3Ddev->ColorFill(pSurface,pRect,color);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DSwapChain9 **ppSwapChain) 
{
	return m_pD3Ddev->CreateAdditionalSwapChain(pPresentationParameters, ppSwapChain);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateCubeTexture(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture9** ppCubeTexture,HANDLE* pSharedHandle) 
{
	return m_pD3Ddev->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture,pSharedHandle);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateDepthStencilSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle) 
{
	return m_pD3Ddev->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality,Discard,ppSurface, pSharedHandle);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,HANDLE* pSharedHandle) 
{
	return m_pD3Ddev->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer,pSharedHandle);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateOffscreenPlainSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle) 
{
	return m_pD3Ddev->CreateOffscreenPlainSurface(Width,Height,Format,Pool,ppSurface,pSharedHandle);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreatePixelShader(CONST DWORD* pFunction,IDirect3DPixelShader9** ppShader) 
{
	return m_pD3Ddev->CreatePixelShader(pFunction, ppShader);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateQuery(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery) 
{
	return m_pD3Ddev->CreateQuery(Type,ppQuery);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateRenderTarget(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle) 
{
	return m_pD3Ddev->CreateRenderTarget(Width, Height, Format, MultiSample,MultisampleQuality, Lockable, ppSurface,pSharedHandle);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateStateBlock(D3DSTATEBLOCKTYPE Type,IDirect3DStateBlock9** ppSB) 
{
	return m_pD3Ddev->CreateStateBlock(Type, ppSB);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,HANDLE* pSharedHandle) 
{
	return m_pD3Ddev->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateVertexBuffer(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer9** VERTexBuffer,HANDLE* pSharedHandle) 
{
	return m_pD3Ddev->CreateVertexBuffer(Length, Usage, FVF, Pool, VERTexBuffer,pSharedHandle);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl) 
{
	return m_pD3Ddev->CreateVertexDeclaration(pVertexElements,ppDecl);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateVertexShader(CONST DWORD* pFunction,IDirect3DVertexShader9** ppShader) 
{
	return m_pD3Ddev->CreateVertexShader(pFunction, ppShader);
}

HRESULT APIENTRY hkIDirect3DDevice9::CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,HANDLE* pSharedHandle) 
{
	return m_pD3Ddev->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture,pSharedHandle);
}

HRESULT APIENTRY hkIDirect3DDevice9::DeletePatch(UINT Handle) 
{
	return m_pD3Ddev->DeletePatch(Handle);
}

HRESULT APIENTRY hkIDirect3DDevice9::EndStateBlock(IDirect3DStateBlock9** ppSB) 
{
	return m_pD3Ddev->EndStateBlock(ppSB);
}

HRESULT APIENTRY hkIDirect3DDevice9::EvictManagedResources() 
{
	return m_pD3Ddev->EvictManagedResources();
}

UINT APIENTRY hkIDirect3DDevice9::GetAvailableTextureMem() 
{
	return m_pD3Ddev->GetAvailableTextureMem();
}

HRESULT APIENTRY hkIDirect3DDevice9::GetClipPlane(DWORD Index, float *pPlane) 
{
	return m_pD3Ddev->GetClipPlane(Index, pPlane);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetClipStatus(D3DCLIPSTATUS9 *pClipStatus) 
{
	return m_pD3Ddev->GetClipStatus(pClipStatus);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) 
{
	return m_pD3Ddev->GetCreationParameters(pParameters);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetCurrentTexturePalette(UINT *pPaletteNumber)
{
	return m_pD3Ddev->GetCurrentTexturePalette(pPaletteNumber);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9 **ppZStencilSurface) 
{
	return m_pD3Ddev->GetDepthStencilSurface(ppZStencilSurface);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetDeviceCaps(D3DCAPS9 *pCaps) 
{
	return m_pD3Ddev->GetDeviceCaps(pCaps);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetDirect3D(IDirect3D9 **ppD3D9) 
{
	HRESULT hRet = m_pD3Ddev->GetDirect3D(ppD3D9);
	if( SUCCEEDED(hRet) )
		*ppD3D9 = m_pD3Dint;
	return hRet;
}

HRESULT APIENTRY hkIDirect3DDevice9::GetDisplayMode(UINT iSwapChain,D3DDISPLAYMODE* pMode) 
{
	return m_pD3Ddev->GetDisplayMode(iSwapChain,pMode);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetFrontBufferData(UINT iSwapChain,IDirect3DSurface9* pDestSurface) 
{
	return m_pD3Ddev->GetFrontBufferData(iSwapChain,pDestSurface);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetFVF(DWORD* pFVF) 
{
	return m_pD3Ddev->GetFVF(pFVF);
}

void APIENTRY hkIDirect3DDevice9::GetGammaRamp(UINT iSwapChain,D3DGAMMARAMP* pRamp) 
{
	m_pD3Ddev->GetGammaRamp(iSwapChain,pRamp);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetIndices(IDirect3DIndexBuffer9** ppIndexData) 
{
	return m_pD3Ddev->GetIndices(ppIndexData);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetLight(DWORD Index, D3DLIGHT9 *pLight) 
{
	return m_pD3Ddev->GetLight(Index, pLight);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetLightEnable(DWORD Index, BOOL *pEnable) 
{
	return m_pD3Ddev->GetLightEnable(Index, pEnable);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetMaterial(D3DMATERIAL9 *pMaterial) 
{
	return m_pD3Ddev->GetMaterial(pMaterial);
}

float APIENTRY hkIDirect3DDevice9::GetNPatchMode() 
{
	return m_pD3Ddev->GetNPatchMode();
}

unsigned int APIENTRY hkIDirect3DDevice9::GetNumberOfSwapChains() 
{
	return m_pD3Ddev->GetNumberOfSwapChains();
}

HRESULT APIENTRY hkIDirect3DDevice9::GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY *pEntries)
{
	return m_pD3Ddev->GetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetPixelShader(IDirect3DPixelShader9** ppShader) 
{
	return m_pD3Ddev->GetPixelShader(ppShader);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetPixelShaderConstantB(UINT StartRegister,BOOL* pConstantData,UINT BoolCount) 
{
	return m_pD3Ddev->GetPixelShaderConstantB(StartRegister,pConstantData,BoolCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetPixelShaderConstantF(UINT StartRegister,float* pConstantData,UINT Vector4fCount) 
{
	return m_pD3Ddev->GetPixelShaderConstantF(StartRegister,pConstantData,Vector4fCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetPixelShaderConstantI(UINT StartRegister,int* pConstantData,UINT Vector4iCount)
{
	return m_pD3Ddev->GetPixelShaderConstantI(StartRegister,pConstantData,Vector4iCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetRasterStatus(UINT iSwapChain,D3DRASTER_STATUS* pRasterStatus) 
{
	return m_pD3Ddev->GetRasterStatus(iSwapChain,pRasterStatus);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) 
{
	return m_pD3Ddev->GetRenderState(State, pValue);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetRenderTarget(DWORD renderTargetIndex,IDirect3DSurface9** ppRenderTarget) 
{
	return m_pD3Ddev->GetRenderTarget(renderTargetIndex,ppRenderTarget);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetRenderTargetData(IDirect3DSurface9* renderTarget,IDirect3DSurface9* pDestSurface) 
{
	return m_pD3Ddev->GetRenderTargetData(renderTarget,pDestSurface);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetSamplerState(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD* pValue) 
{
	return m_pD3Ddev->GetSamplerState(Sampler,Type,pValue);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetScissorRect(RECT* pRect) 
{
	return m_pD3Ddev->GetScissorRect(pRect);
}

BOOL APIENTRY hkIDirect3DDevice9::GetSoftwareVertexProcessing() 
{
	return m_pD3Ddev->GetSoftwareVertexProcessing();
}

HRESULT APIENTRY hkIDirect3DDevice9::GetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer9** ppStreamData,UINT* OffsetInBytes,UINT* pStride) 
{
	return m_pD3Ddev->GetStreamSource(StreamNumber, ppStreamData,OffsetInBytes, pStride);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetStreamSourceFreq(UINT StreamNumber,UINT* Divider) 
{
	return m_pD3Ddev->GetStreamSourceFreq(StreamNumber,Divider);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetSwapChain(UINT iSwapChain,IDirect3DSwapChain9** pSwapChain)
{
	return m_pD3Ddev->GetSwapChain(iSwapChain,pSwapChain);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetTexture(DWORD Stage, IDirect3DBaseTexture9 **ppTexture) 
{
	return m_pD3Ddev->GetTexture(Stage, ppTexture);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) 
{
	return m_pD3Ddev->GetTextureStageState(Stage, Type, pValue);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) 
{
	return m_pD3Ddev->GetTransform(State, pMatrix);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) 
{
	return m_pD3Ddev->GetVertexDeclaration(ppDecl);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetVertexShader(IDirect3DVertexShader9** ppShader) 
{
	return m_pD3Ddev->GetVertexShader(ppShader);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetVertexShaderConstantB(UINT StartRegister,BOOL* pConstantData,UINT BoolCount)
{
	return m_pD3Ddev->GetVertexShaderConstantB(StartRegister,pConstantData,BoolCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetVertexShaderConstantF(UINT StartRegister,float* pConstantData,UINT Vector4fCount) 
{
	return m_pD3Ddev->GetVertexShaderConstantF(StartRegister,pConstantData,Vector4fCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetVertexShaderConstantI(UINT StartRegister,int* pConstantData,UINT Vector4iCount)
{
	return m_pD3Ddev->GetVertexShaderConstantI(StartRegister,pConstantData,Vector4iCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::GetViewport(D3DVIEWPORT9 *pViewport) 
{
	return m_pD3Ddev->GetViewport(pViewport);
}

HRESULT APIENTRY hkIDirect3DDevice9::LightEnable(DWORD LightIndex, BOOL bEnable) 
{
	return m_pD3Ddev->LightEnable(LightIndex, bEnable);
}

HRESULT APIENTRY hkIDirect3DDevice9::MultiplyTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix) 
{
	return m_pD3Ddev->MultiplyTransform(State, pMatrix);
}

void APIENTRY WINAPI D3DPERF_SetOptions(DWORD options)
{
    //MessageBox(NULL, "D3DPERF_SetOptions", "D3D9Wrapper", MB_OK);
}

HRESULT APIENTRY hkIDirect3DDevice9::ProcessVertices(UINT SrcStartIndex,UINT DestIndex,UINT VertexCount,IDirect3DVertexBuffer9* pDestBuffer,IDirect3DVertexDeclaration9* pVertexDecl,DWORD Flags) 
{
	return m_pD3Ddev->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer,pVertexDecl, Flags);
}

ULONG APIENTRY hkIDirect3DDevice9::Release() 
{
	if( --m_refCount == 0 )
		m_pManager->Release();

	return m_pD3Ddev->Release();
}

HRESULT APIENTRY hkIDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) 
{
	m_pManager->PreReset();

	HRESULT hRet = m_pD3Ddev->Reset(pPresentationParameters);

	if( SUCCEEDED(hRet) )
	{
		m_PresentParam = *pPresentationParameters;
		m_pManager->PostReset();
	}

	return hRet;
}

HRESULT APIENTRY hkIDirect3DDevice9::SetClipPlane(DWORD Index, CONST float *pPlane) 
{
	return m_pD3Ddev->SetClipPlane(Index, pPlane);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetClipStatus(CONST D3DCLIPSTATUS9 *pClipStatus) 
{
	return m_pD3Ddev->SetClipStatus(pClipStatus);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetCurrentTexturePalette(UINT PaletteNumber) 
{
	return m_pD3Ddev->SetCurrentTexturePalette(PaletteNumber);
}

void APIENTRY hkIDirect3DDevice9::SetCursorPosition(int X, int Y, DWORD Flags) 
{
	m_pD3Ddev->SetCursorPosition(X, Y, Flags);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9 *pCursorBitmap) 
{
	return m_pD3Ddev->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) 
{
	return m_pD3Ddev->SetDepthStencilSurface(pNewZStencil);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetDialogBoxMode(BOOL bEnableDialogs) 
{
	return m_pD3Ddev->SetDialogBoxMode(bEnableDialogs);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetFVF(DWORD FVF) 
{
	return m_pD3Ddev->SetFVF(FVF);
}

void APIENTRY hkIDirect3DDevice9::SetGammaRamp(UINT iSwapChain,DWORD Flags,CONST D3DGAMMARAMP* pRamp)
{
	m_pD3Ddev->SetGammaRamp(iSwapChain,Flags, pRamp);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9* pIndexData) 
{
	return m_pD3Ddev->SetIndices(pIndexData);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetLight(DWORD Index, CONST D3DLIGHT9 *pLight) 
{
	return m_pD3Ddev->SetLight(Index, pLight);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetMaterial(CONST D3DMATERIAL9 *pMaterial) 
{	
	return m_pD3Ddev->SetMaterial(pMaterial);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetNPatchMode(float nSegments) 
{	
	return m_pD3Ddev->SetNPatchMode(nSegments);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY *pEntries) 
{
	return m_pD3Ddev->SetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9* pShader) 
{
	return m_pD3Ddev->SetPixelShader(pShader);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetPixelShaderConstantB(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount) 
{
	return m_pD3Ddev->SetPixelShaderConstantB(StartRegister,pConstantData,BoolCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetPixelShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount) 
{
	return m_pD3Ddev->SetPixelShaderConstantF(StartRegister,pConstantData,Vector4fCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetPixelShaderConstantI(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount) 
{
	return m_pD3Ddev->SetPixelShaderConstantI(StartRegister,pConstantData,Vector4iCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) 
{
	return m_pD3Ddev->SetRenderState(State, Value);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetSamplerState(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD Value) 
{
	return m_pD3Ddev->SetSamplerState(Sampler,Type,Value);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetScissorRect(CONST RECT* pRect) 
{
	return m_pD3Ddev->SetScissorRect(pRect);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetSoftwareVertexProcessing(BOOL bSoftware) 
{
	return m_pD3Ddev->SetSoftwareVertexProcessing(bSoftware);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride) 
{
	return m_pD3Ddev->SetStreamSource(StreamNumber, pStreamData,OffsetInBytes, Stride);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetStreamSourceFreq(UINT StreamNumber,UINT Divider)
{	
	return m_pD3Ddev->SetStreamSourceFreq(StreamNumber,Divider);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetTexture(DWORD Stage, IDirect3DBaseTexture9 *pTexture) 
{
#ifdef D3DHOOK_TEXTURES
	IDirect3DDevice9 *dev = NULL;
	if(pTexture != NULL && ((hkIDirect3DTexture9*)(pTexture))->GetDevice(&dev) == D3D_OK)
	{
		if(dev == this)
			return m_pD3Ddev->SetTexture(Stage, ((hkIDirect3DTexture9*)(pTexture))->m_D3Dtex);
	}
#endif
	
	return m_pD3Ddev->SetTexture(Stage, pTexture);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) 
{
	return m_pD3Ddev->SetTextureStageState(Stage, Type, Value);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix) 
{
	return m_pD3Ddev->SetTransform(State, pMatrix);	
}

HRESULT APIENTRY hkIDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) 
{
	return m_pD3Ddev->SetVertexDeclaration(pDecl);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetVertexShaderConstantB(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount) 
{
	return m_pD3Ddev->SetVertexShaderConstantB(StartRegister,pConstantData,BoolCount);
}

HRESULT APIENTRY hkIDirect3DDevice9::SetVertexShaderConstantI(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount) 
{
	return m_pD3Ddev->SetVertexShaderConstantI(StartRegister,pConstantData,Vector4iCount);
}

BOOL APIENTRY hkIDirect3DDevice9::ShowCursor(BOOL bShow)	
{
	return m_pD3Ddev->ShowCursor(bShow);
}

HRESULT APIENTRY hkIDirect3DDevice9::StretchRect(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestSurface,CONST RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter) 
{
	return m_pD3Ddev->StretchRect(pSourceSurface,pSourceRect,pDestSurface,pDestRect,Filter);
}

HRESULT APIENTRY hkIDirect3DDevice9::TestCooperativeLevel() 
{
	return m_pD3Ddev->TestCooperativeLevel();
}

HRESULT APIENTRY hkIDirect3DDevice9::UpdateSurface(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestinationSurface,CONST POINT* pDestPoint) 
{
	return m_pD3Ddev->UpdateSurface(pSourceSurface,pSourceRect,pDestinationSurface,pDestPoint);
}

HRESULT APIENTRY hkIDirect3DDevice9::UpdateTexture(IDirect3DBaseTexture9 *pSourceTexture, IDirect3DBaseTexture9 *pDestinationTexture) 
{
	return m_pD3Ddev->UpdateTexture(pSourceTexture, pDestinationTexture);
}

HRESULT APIENTRY hkIDirect3DDevice9::ValidateDevice(DWORD *pNumPasses) 
{
	return m_pD3Ddev->ValidateDevice(pNumPasses);
}
#pragma endregion