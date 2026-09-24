// D3DX9 subset — implementações reais (math não-inline + DDS/BMP + sprite + buffer).
#include "win32_extras.h"
#include "wingdi.h"

#include <d3d9.h>
#include "d3dx9.h"
#include <cmath>
#include "d3dx9math.h"
#include "d3dx9tex.h"
#include "d3dx9core.h"
#include "d3dx9mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <new>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdlib>


namespace
{
	void MatIdentity(D3DXMATRIX* m)
	{
		std::memset(m, 0, sizeof(*m));
		m->_11 = m->_22 = m->_33 = m->_44 = 1.f;
	}

	float MatDet3(float a, float b, float c, float d, float e, float f, float g, float h, float i)
	{
		return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	}

	struct DxBuffer final : public ID3DXBuffer {
		ULONG refs { 1 };
		std::vector<unsigned char> data;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override
		{
			if (!ppv)
				return E_POINTER;
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG r = --refs;
			if (!r)
				delete this;
			return r;
		}
		LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return data.data(); }
		DWORD STDMETHODCALLTYPE GetBufferSize() override { return static_cast<DWORD>(data.size()); }
	};

	struct SpriteVert {
		float x, y, z, rhw;
		D3DCOLOR color;
		float u, v;
	};

	struct DxSprite final : public ID3DXSprite {
		ULONG refs { 1 };
		LPDIRECT3DDEVICE9 device { nullptr };
		bool begun { false };

		explicit DxSprite(LPDIRECT3DDEVICE9 dev) : device(dev)
		{
			if (device)
				device->AddRef();
		}
		~DxSprite()
		{
			if (device)
				device->Release();
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override
		{
			if (!ppv)
				return E_POINTER;
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG r = --refs;
			if (!r)
				delete this;
			return r;
		}
		HRESULT STDMETHODCALLTYPE GetDevice(LPDIRECT3DDEVICE9* ppDevice) override
		{
			if (!ppDevice)
				return E_POINTER;
			*ppDevice = device;
			if (device)
				device->AddRef();
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE Begin() override
		{
			if (!device)
				return E_FAIL;
			begun = true;
			device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			device->SetRenderState(D3DRS_ZENABLE, FALSE);
			device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			device->SetRenderState(D3DRS_LIGHTING, FALSE);
			device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
			device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE Draw(LPDIRECT3DTEXTURE9 pSrcTexture, CONST RECT* pSrcRect,
			CONST D3DXVECTOR2* pScaling, CONST D3DXVECTOR2* pRotationCenter, FLOAT Rotation,
			CONST D3DXVECTOR2* pTranslation, D3DCOLOR Color) override
		{
			if (!begun || !device || !pSrcTexture)
				return E_FAIL;

			D3DSURFACE_DESC desc {};
			pSrcTexture->GetLevelDesc(0, &desc);
			RECT src { 0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height) };
			if (pSrcRect)
				src = *pSrcRect;

			const float sw = static_cast<float>(src.right - src.left);
			const float sh = static_cast<float>(src.bottom - src.top);
			const float sx = pScaling ? pScaling->x : 1.f;
			const float sy = pScaling ? pScaling->y : 1.f;
			const float tx = pTranslation ? pTranslation->x : 0.f;
			const float ty = pTranslation ? pTranslation->y : 0.f;
			const float rcx = pRotationCenter ? pRotationCenter->x : 0.f;
			const float rcy = pRotationCenter ? pRotationCenter->y : 0.f;

			const float u0 = src.left / static_cast<float>(desc.Width);
			const float v0 = src.top / static_cast<float>(desc.Height);
			const float u1 = src.right / static_cast<float>(desc.Width);
			const float v1 = src.bottom / static_cast<float>(desc.Height);

			// Cantos locais (antes de rotação): em torno da origem da sprite.
			D3DXVECTOR2 local[4] = {
				{ 0.f, 0.f },
				{ sw * sx, 0.f },
				{ sw * sx, sh * sy },
				{ 0.f, sh * sy },
			};
			// D3DX8/9 Sprite: Scale → rotate around RotationCenter → Translate.
			// RotationCenter is relative to the source rect's top-left; when
			// Rotation==0 the center offset must cancel so Translation is the
			// sprite's top-left (not its center). Omitting the +cx/+cy restore
			// shifted every UI panel by half its size while fonts (DrawPrimitiveUP)
			// stayed correct — empty frames with floating text/buttons.
			const float cx = (rcx - static_cast<float>(src.left)) * sx;
			const float cy = (rcy - static_cast<float>(src.top)) * sy;
			const float c = std::cos(Rotation), s = std::sin(Rotation);
			SpriteVert verts[4];
			for (int i = 0; i < 4; ++i) {
				const float dx = local[i].x - cx;
				const float dy = local[i].y - cy;
				const float rx = dx * c - dy * s;
				const float ry = dx * s + dy * c;
				verts[i].x = rx + cx + tx + 0.5f;
				verts[i].y = ry + cy + ty + 0.5f;
				verts[i].z = 0.f;
				verts[i].rhw = 1.f;
				verts[i].color = Color;
			}
			verts[0].u = u0; verts[0].v = v0;
			verts[1].u = u1; verts[1].v = v0;
			verts[2].u = u1; verts[2].v = v1;
			verts[3].u = u0; verts[3].v = v1;

			device->SetTexture(0, pSrcTexture);
			return device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, verts, sizeof(SpriteVert));
		}
		HRESULT STDMETHODCALLTYPE DrawTransform(LPDIRECT3DTEXTURE9 pSrcTexture, CONST RECT* pSrcRect,
			CONST D3DXMATRIX* pTransform, D3DCOLOR Color) override
		{
			D3DXVECTOR2 scale(pTransform ? pTransform->_11 : 1.f, pTransform ? pTransform->_22 : 1.f);
			D3DXVECTOR2 trans(pTransform ? pTransform->_41 : 0.f, pTransform ? pTransform->_42 : 0.f);
			return Draw(pSrcTexture, pSrcRect, &scale, nullptr, 0.f, &trans, Color);
		}
		HRESULT STDMETHODCALLTYPE End() override
		{
			begun = false;
			if (device) {
				device->SetRenderState(D3DRS_ZENABLE, TRUE);
				device->SetTexture(0, nullptr);
				device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
				device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
				device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
				device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
				device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE OnLostDevice() override { return S_OK; }
		HRESULT STDMETHODCALLTYPE OnResetDevice() override { return S_OK; }
	};
}

extern "C" {

D3DXMATRIX* WINAPI D3DXMatrixMultiply(D3DXMATRIX* pOut, const D3DXMATRIX* pA, const D3DXMATRIX* pB)
{
	D3DXMATRIX r;
	for (int row = 0; row < 4; ++row)
		for (int col = 0; col < 4; ++col)
			r.m[row][col] =
				pA->m[row][0] * pB->m[0][col] +
				pA->m[row][1] * pB->m[1][col] +
				pA->m[row][2] * pB->m[2][col] +
				pA->m[row][3] * pB->m[3][col];
	*pOut = r;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixMultiplyTranspose(D3DXMATRIX* pOut, const D3DXMATRIX* pA, const D3DXMATRIX* pB)
{
	D3DXMATRIX tmp;
	D3DXMatrixMultiply(&tmp, pA, pB);
	return D3DXMatrixTranspose(pOut, &tmp);
}

D3DXMATRIX* WINAPI D3DXMatrixTranspose(D3DXMATRIX* pOut, const D3DXMATRIX* pM)
{
	D3DXMATRIX r;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			r.m[i][j] = pM->m[j][i];
	*pOut = r;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixInverse(D3DXMATRIX* pOut, FLOAT* pDet, const D3DXMATRIX* pM)
{
	const float* m = &pM->_11;
	float inv[16];
	inv[0] = MatDet3(m[5], m[6], m[7], m[9], m[10], m[11], m[13], m[14], m[15]);
	inv[4] = -MatDet3(m[4], m[6], m[7], m[8], m[10], m[11], m[12], m[14], m[15]);
	inv[8] = MatDet3(m[4], m[5], m[7], m[8], m[9], m[11], m[12], m[13], m[15]);
	inv[12] = -MatDet3(m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14]);
	inv[1] = -MatDet3(m[1], m[2], m[3], m[9], m[10], m[11], m[13], m[14], m[15]);
	inv[5] = MatDet3(m[0], m[2], m[3], m[8], m[10], m[11], m[12], m[14], m[15]);
	inv[9] = -MatDet3(m[0], m[1], m[3], m[8], m[9], m[11], m[12], m[13], m[15]);
	inv[13] = MatDet3(m[0], m[1], m[2], m[8], m[9], m[10], m[12], m[13], m[14]);
	inv[2] = MatDet3(m[1], m[2], m[3], m[5], m[6], m[7], m[13], m[14], m[15]);
	inv[6] = -MatDet3(m[0], m[2], m[3], m[4], m[6], m[7], m[12], m[14], m[15]);
	inv[10] = MatDet3(m[0], m[1], m[3], m[4], m[5], m[7], m[12], m[13], m[15]);
	inv[14] = -MatDet3(m[0], m[1], m[2], m[4], m[5], m[6], m[12], m[13], m[14]);
	inv[3] = -MatDet3(m[1], m[2], m[3], m[5], m[6], m[7], m[9], m[10], m[11]);
	inv[7] = MatDet3(m[0], m[2], m[3], m[4], m[6], m[7], m[8], m[10], m[11]);
	inv[11] = -MatDet3(m[0], m[1], m[3], m[4], m[5], m[7], m[8], m[9], m[11]);
	inv[15] = MatDet3(m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]);

	const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (pDet)
		*pDet = det;
	if (std::fabs(det) < 1e-12f) {
		MatIdentity(pOut);
		return nullptr;
	}
	const float invDet = 1.f / det;
	for (int i = 0; i < 16; ++i)
		(&pOut->_11)[i] = inv[i] * invDet;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixLookAtLH(D3DXMATRIX* pOut, const D3DXVECTOR3* pEye,
	const D3DXVECTOR3* pAt, const D3DXVECTOR3* pUp)
{
	D3DXVECTOR3 z = *pAt - *pEye;
	D3DXVec3Normalize(&z, &z);
	D3DXVECTOR3 x;
	D3DXVec3Cross(&x, pUp, &z);
	D3DXVec3Normalize(&x, &x);
	D3DXVECTOR3 y;
	D3DXVec3Cross(&y, &z, &x);
	MatIdentity(pOut);
	pOut->_11 = x.x; pOut->_12 = y.x; pOut->_13 = z.x;
	pOut->_21 = x.y; pOut->_22 = y.y; pOut->_23 = z.y;
	pOut->_31 = x.z; pOut->_32 = y.z; pOut->_33 = z.z;
	pOut->_41 = -D3DXVec3Dot(&x, pEye);
	pOut->_42 = -D3DXVec3Dot(&y, pEye);
	pOut->_43 = -D3DXVec3Dot(&z, pEye);
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixPerspectiveFovLH(D3DXMATRIX* pOut, FLOAT fovy, FLOAT aspect,
	FLOAT zn, FLOAT zf)
{
	const float yScale = 1.f / std::tan(fovy * 0.5f);
	const float xScale = yScale / aspect;
	MatIdentity(pOut);
	pOut->_11 = xScale;
	pOut->_22 = yScale;
	pOut->_33 = zf / (zf - zn);
	pOut->_34 = 1.f;
	pOut->_43 = (-zn * zf) / (zf - zn);
	pOut->_44 = 0.f;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixTranslation(D3DXMATRIX* pOut, FLOAT x, FLOAT y, FLOAT z)
{
	MatIdentity(pOut);
	pOut->_41 = x; pOut->_42 = y; pOut->_43 = z;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixScaling(D3DXMATRIX* pOut, FLOAT x, FLOAT y, FLOAT z)
{
	MatIdentity(pOut);
	pOut->_11 = x; pOut->_22 = y; pOut->_33 = z;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationX(D3DXMATRIX* pOut, FLOAT a)
{
	MatIdentity(pOut);
	const float c = std::cos(a), s = std::sin(a);
	pOut->_22 = c; pOut->_23 = s;
	pOut->_32 = -s; pOut->_33 = c;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationY(D3DXMATRIX* pOut, FLOAT a)
{
	MatIdentity(pOut);
	const float c = std::cos(a), s = std::sin(a);
	pOut->_11 = c; pOut->_13 = -s;
	pOut->_31 = s; pOut->_33 = c;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationZ(D3DXMATRIX* pOut, FLOAT a)
{
	MatIdentity(pOut);
	const float c = std::cos(a), s = std::sin(a);
	pOut->_11 = c; pOut->_12 = s;
	pOut->_21 = -s; pOut->_22 = c;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationYawPitchRoll(D3DXMATRIX* pOut, FLOAT yaw, FLOAT pitch, FLOAT roll)
{
	D3DXMATRIX x, y, z, t;
	D3DXMatrixRotationY(&y, yaw);
	D3DXMatrixRotationX(&x, pitch);
	D3DXMatrixRotationZ(&z, roll);
	D3DXMatrixMultiply(&t, &x, &y);
	return D3DXMatrixMultiply(pOut, &z, &t);
}

D3DXMATRIX* WINAPI D3DXMatrixRotationAxis(D3DXMATRIX* pOut, const D3DXVECTOR3* pV, FLOAT a)
{
	D3DXVECTOR3 axis = *pV;
	D3DXVec3Normalize(&axis, &axis);
	const float c = std::cos(a), s = std::sin(a), t = 1.f - c;
	const float x = axis.x, y = axis.y, z = axis.z;
	MatIdentity(pOut);
	pOut->_11 = t * x * x + c;
	pOut->_12 = t * x * y + s * z;
	pOut->_13 = t * x * z - s * y;
	pOut->_21 = t * x * y - s * z;
	pOut->_22 = t * y * y + c;
	pOut->_23 = t * y * z + s * x;
	pOut->_31 = t * x * z + s * y;
	pOut->_32 = t * y * z - s * x;
	pOut->_33 = t * z * z + c;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationQuaternion(D3DXMATRIX* pOut, const D3DXQUATERNION* pQ)
{
	const float xx = pQ->x * pQ->x, yy = pQ->y * pQ->y, zz = pQ->z * pQ->z;
	const float xy = pQ->x * pQ->y, xz = pQ->x * pQ->z, yz = pQ->y * pQ->z;
	const float wx = pQ->w * pQ->x, wy = pQ->w * pQ->y, wz = pQ->w * pQ->z;
	MatIdentity(pOut);
	pOut->_11 = 1.f - 2.f * (yy + zz);
	pOut->_12 = 2.f * (xy + wz);
	pOut->_13 = 2.f * (xz - wy);
	pOut->_21 = 2.f * (xy - wz);
	pOut->_22 = 1.f - 2.f * (xx + zz);
	pOut->_23 = 2.f * (yz + wx);
	pOut->_31 = 2.f * (xz + wy);
	pOut->_32 = 2.f * (yz - wx);
	pOut->_33 = 1.f - 2.f * (xx + yy);
	return pOut;
}

D3DXVECTOR3* WINAPI D3DXVec3Normalize(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV)
{
	const float len = D3DXVec3Length(pV);
	if (len > 1e-8f) {
		pOut->x = pV->x / len;
		pOut->y = pV->y / len;
		pOut->z = pV->z / len;
	} else {
		pOut->x = 0;
		pOut->y = 1;
		pOut->z = 0;
	}
	return pOut;
}

D3DXVECTOR2* WINAPI D3DXVec2Normalize(D3DXVECTOR2* pOut, const D3DXVECTOR2* pV)
{
	const float len = D3DXVec2Length(pV);
	if (len > 1e-8f) {
		pOut->x = pV->x / len;
		pOut->y = pV->y / len;
	} else {
		pOut->x = 1;
		pOut->y = 0;
	}
	return pOut;
}

D3DXVECTOR3* WINAPI D3DXVec3TransformCoord(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM)
{
	D3DXVECTOR4 t;
	D3DXVec3Transform(&t, pV, pM);
	const float w = (t.w != 0.f) ? t.w : 1.f;
	pOut->x = t.x / w;
	pOut->y = t.y / w;
	pOut->z = t.z / w;
	return pOut;
}

D3DXVECTOR4* WINAPI D3DXVec3Transform(D3DXVECTOR4* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM)
{
	pOut->x = pV->x * pM->_11 + pV->y * pM->_21 + pV->z * pM->_31 + pM->_41;
	pOut->y = pV->x * pM->_12 + pV->y * pM->_22 + pV->z * pM->_32 + pM->_42;
	pOut->z = pV->x * pM->_13 + pV->y * pM->_23 + pV->z * pM->_33 + pM->_43;
	pOut->w = pV->x * pM->_14 + pV->y * pM->_24 + pV->z * pM->_34 + pM->_44;
	return pOut;
}

D3DXVECTOR3* WINAPI D3DXVec3Project(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DVIEWPORT9* pViewport,
	const D3DXMATRIX* pProjection, const D3DXMATRIX* pView, const D3DXMATRIX* pWorld)
{
	D3DXMATRIX wvp, tmp;
	if (pWorld && pView)
		D3DXMatrixMultiply(&tmp, pWorld, pView);
	else if (pView)
		tmp = *pView;
	else if (pWorld)
		tmp = *pWorld;
	else
		MatIdentity(&tmp);
	if (pProjection)
		D3DXMatrixMultiply(&wvp, &tmp, pProjection);
	else
		wvp = tmp;

	D3DXVECTOR3 clip;
	D3DXVec3TransformCoord(&clip, pV, &wvp);
	if (pViewport) {
		pOut->x = pViewport->X + (1.f + clip.x) * pViewport->Width * 0.5f;
		pOut->y = pViewport->Y + (1.f - clip.y) * pViewport->Height * 0.5f;
		pOut->z = pViewport->MinZ + clip.z * (pViewport->MaxZ - pViewport->MinZ);
	} else {
		*pOut = clip;
	}
	return pOut;
}

D3DXQUATERNION* WINAPI D3DXQuaternionSlerp(D3DXQUATERNION* pOut, const D3DXQUATERNION* a,
	const D3DXQUATERNION* b, FLOAT t)
{
	float cosTheta = a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w;
	D3DXQUATERNION bb = *b;
	if (cosTheta < 0.f) {
		cosTheta = -cosTheta;
		bb.x = -bb.x;
		bb.y = -bb.y;
		bb.z = -bb.z;
		bb.w = -bb.w;
	}
	float scale0, scale1;
	if (1.f - cosTheta > 1e-5f) {
		const float theta = std::acos(cosTheta);
		const float sinTheta = std::sin(theta);
		scale0 = std::sin((1.f - t) * theta) / sinTheta;
		scale1 = std::sin(t * theta) / sinTheta;
	} else {
		scale0 = 1.f - t;
		scale1 = t;
	}
	pOut->x = scale0 * a->x + scale1 * bb.x;
	pOut->y = scale0 * a->y + scale1 * bb.y;
	pOut->z = scale0 * a->z + scale1 * bb.z;
	pOut->w = scale0 * a->w + scale1 * bb.w;
	return pOut;
}

D3DXQUATERNION* WINAPI D3DXQuaternionRotationMatrix(D3DXQUATERNION* pOut, const D3DXMATRIX* pM)
{
	const float tr = pM->_11 + pM->_22 + pM->_33;
	if (tr > 0.f) {
		const float s = std::sqrt(tr + 1.f) * 2.f;
		pOut->w = 0.25f * s;
		pOut->x = (pM->_23 - pM->_32) / s;
		pOut->y = (pM->_31 - pM->_13) / s;
		pOut->z = (pM->_12 - pM->_21) / s;
	} else if (pM->_11 > pM->_22 && pM->_11 > pM->_33) {
		const float s = std::sqrt(1.f + pM->_11 - pM->_22 - pM->_33) * 2.f;
		pOut->w = (pM->_23 - pM->_32) / s;
		pOut->x = 0.25f * s;
		pOut->y = (pM->_12 + pM->_21) / s;
		pOut->z = (pM->_31 + pM->_13) / s;
	} else if (pM->_22 > pM->_33) {
		const float s = std::sqrt(1.f + pM->_22 - pM->_11 - pM->_33) * 2.f;
		pOut->w = (pM->_31 - pM->_13) / s;
		pOut->x = (pM->_12 + pM->_21) / s;
		pOut->y = 0.25f * s;
		pOut->z = (pM->_23 + pM->_32) / s;
	} else {
		const float s = std::sqrt(1.f + pM->_33 - pM->_11 - pM->_22) * 2.f;
		pOut->w = (pM->_12 - pM->_21) / s;
		pOut->x = (pM->_31 + pM->_13) / s;
		pOut->y = (pM->_23 + pM->_32) / s;
		pOut->z = 0.25f * s;
	}
	return pOut;
}

BOOL WINAPI D3DXIntersectTri(const D3DXVECTOR3* p0, const D3DXVECTOR3* p1, const D3DXVECTOR3* p2,
	const D3DXVECTOR3* pRayPos, const D3DXVECTOR3* pRayDir, FLOAT* pU, FLOAT* pV, FLOAT* pDist)
{
	// Möller–Trumbore
	const D3DXVECTOR3 e1 = *p1 - *p0;
	const D3DXVECTOR3 e2 = *p2 - *p0;
	D3DXVECTOR3 pvec;
	D3DXVec3Cross(&pvec, pRayDir, &e2);
	const float det = D3DXVec3Dot(&e1, &pvec);
	if (std::fabs(det) < 1e-8f)
		return FALSE;
	const float invDet = 1.f / det;
	const D3DXVECTOR3 tvec = *pRayPos - *p0;
	const float u = D3DXVec3Dot(&tvec, &pvec) * invDet;
	if (u < 0.f || u > 1.f)
		return FALSE;
	D3DXVECTOR3 qvec;
	D3DXVec3Cross(&qvec, &tvec, &e1);
	const float v = D3DXVec3Dot(pRayDir, &qvec) * invDet;
	if (v < 0.f || u + v > 1.f)
		return FALSE;
	const float t = D3DXVec3Dot(&e2, &qvec) * invDet;
	if (t < 0.f)
		return FALSE;
	if (pU)
		*pU = u;
	if (pV)
		*pV = v;
	if (pDist)
		*pDist = t;
	return TRUE;
}

HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER* ppBuffer)
{
	if (!ppBuffer || !NumBytes)
		return E_INVALIDARG;
	auto* buf = new (std::nothrow) DxBuffer();
	if (!buf)
		return E_OUTOFMEMORY;
	buf->data.assign(NumBytes, 0);
	*ppBuffer = buf;
	return S_OK;
}

HRESULT WINAPI D3DXCreateSprite(LPDIRECT3DDEVICE9 pDevice, LPD3DXSPRITE* pp)
{
	if (!pDevice || !pp)
		return E_INVALIDARG;
	auto* spr = new (std::nothrow) DxSprite(pDevice);
	if (!spr)
		return E_OUTOFMEMORY;
	*pp = spr;
	return S_OK;
}

HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE9 pDevice, UINT Width, UINT Height, UINT Levels,
	DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, LPDIRECT3DTEXTURE9* ppTexture)
{
	if (!pDevice || !ppTexture)
		return E_INVALIDARG;
	return pDevice->CreateTexture(Width, Height, Levels ? Levels : 1, Usage, Format, Pool, ppTexture, nullptr);
}

namespace {

void Decode565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b)
{
	r = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
	g = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
	b = static_cast<uint8_t>((c & 31) * 255 / 31);
}

bool DecodeDxt1ToRgba(const uint8_t* src, size_t srcSize, uint32_t w, uint32_t h, std::vector<uint8_t>& rgba)
{
	const size_t need = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 8u;
	if (srcSize < need)
		return false;
	rgba.assign(static_cast<size_t>(w) * h * 4u, 0);
	size_t off = 0;
	for (uint32_t by = 0; by < h; by += 4) {
		for (uint32_t bx = 0; bx < w; bx += 4) {
			const uint16_t c0 = static_cast<uint16_t>(src[off] | (src[off + 1] << 8));
			const uint16_t c1 = static_cast<uint16_t>(src[off + 2] | (src[off + 3] << 8));
			const uint32_t bits = static_cast<uint32_t>(src[off + 4]) |
				(static_cast<uint32_t>(src[off + 5]) << 8) |
				(static_cast<uint32_t>(src[off + 6]) << 16) |
				(static_cast<uint32_t>(src[off + 7]) << 24);
			off += 8;
			uint8_t cr[4], cg[4], cb[4], ca[4];
			Decode565(c0, cr[0], cg[0], cb[0]);
			Decode565(c1, cr[1], cg[1], cb[1]);
			if (c0 > c1) {
				cr[2] = static_cast<uint8_t>((2 * cr[0] + cr[1]) / 3);
				cg[2] = static_cast<uint8_t>((2 * cg[0] + cg[1]) / 3);
				cb[2] = static_cast<uint8_t>((2 * cb[0] + cb[1]) / 3);
				cr[3] = static_cast<uint8_t>((cr[0] + 2 * cr[1]) / 3);
				cg[3] = static_cast<uint8_t>((cg[0] + 2 * cg[1]) / 3);
				cb[3] = static_cast<uint8_t>((cb[0] + 2 * cb[1]) / 3);
				ca[0] = ca[1] = ca[2] = ca[3] = 255;
			} else {
				cr[2] = static_cast<uint8_t>((cr[0] + cr[1]) / 2);
				cg[2] = static_cast<uint8_t>((cg[0] + cg[1]) / 2);
				cb[2] = static_cast<uint8_t>((cb[0] + cb[1]) / 2);
				cr[3] = cg[3] = cb[3] = 0;
				ca[0] = ca[1] = ca[2] = 255;
				ca[3] = 0;
			}
			for (uint32_t py = 0; py < 4; ++py) {
				for (uint32_t px = 0; px < 4; ++px) {
					const uint32_t x = bx + px, y = by + py;
					if (x >= w || y >= h)
						continue;
					const uint32_t idx = (bits >> (2 * (py * 4 + px))) & 3u;
					const size_t di = (static_cast<size_t>(y) * w + x) * 4u;
					rgba[di] = cr[idx];
					rgba[di + 1] = cg[idx];
					rgba[di + 2] = cb[idx];
					rgba[di + 3] = ca[idx];
				}
			}
		}
	}
	return true;
}

bool DecodeDxt3ToRgba(const uint8_t* src, size_t srcSize, uint32_t w, uint32_t h, std::vector<uint8_t>& rgba)
{
	const size_t need = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 16u;
	if (srcSize < need)
		return false;
	rgba.assign(static_cast<size_t>(w) * h * 4u, 0);
	size_t off = 0;
	for (uint32_t by = 0; by < h; by += 4) {
		for (uint32_t bx = 0; bx < w; bx += 4) {
			uint8_t a[16];
			for (int i = 0; i < 8; ++i) {
				const uint8_t byte = src[off + static_cast<size_t>(i)];
				a[i * 2] = static_cast<uint8_t>((byte & 0x0F) * 17);
				a[i * 2 + 1] = static_cast<uint8_t>(((byte >> 4) & 0x0F) * 17);
			}
			off += 8;
			const uint16_t c0 = static_cast<uint16_t>(src[off] | (src[off + 1] << 8));
			const uint16_t c1 = static_cast<uint16_t>(src[off + 2] | (src[off + 3] << 8));
			const uint32_t bits = static_cast<uint32_t>(src[off + 4]) |
				(static_cast<uint32_t>(src[off + 5]) << 8) |
				(static_cast<uint32_t>(src[off + 6]) << 16) |
				(static_cast<uint32_t>(src[off + 7]) << 24);
			off += 8;
			uint8_t cr[4], cg[4], cb[4];
			Decode565(c0, cr[0], cg[0], cb[0]);
			Decode565(c1, cr[1], cg[1], cb[1]);
			cr[2] = static_cast<uint8_t>((2 * cr[0] + cr[1]) / 3);
			cg[2] = static_cast<uint8_t>((2 * cg[0] + cg[1]) / 3);
			cb[2] = static_cast<uint8_t>((2 * cb[0] + cb[1]) / 3);
			cr[3] = static_cast<uint8_t>((cr[0] + 2 * cr[1]) / 3);
			cg[3] = static_cast<uint8_t>((cg[0] + 2 * cg[1]) / 3);
			cb[3] = static_cast<uint8_t>((cb[0] + 2 * cb[1]) / 3);
			for (uint32_t py = 0; py < 4; ++py) {
				for (uint32_t px = 0; px < 4; ++px) {
					const uint32_t x = bx + px, y = by + py;
					if (x >= w || y >= h)
						continue;
					const uint32_t idx = (bits >> (2 * (py * 4 + px))) & 3u;
					const size_t di = (static_cast<size_t>(y) * w + x) * 4u;
					rgba[di] = cr[idx];
					rgba[di + 1] = cg[idx];
					rgba[di + 2] = cb[idx];
					rgba[di + 3] = a[py * 4 + px];
				}
			}
		}
	}
	return true;
}

bool DecodeDxt5ToRgba(const uint8_t* src, size_t srcSize, uint32_t w, uint32_t h, std::vector<uint8_t>& rgba)
{
	const size_t need = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 16u;
	if (srcSize < need)
		return false;
	rgba.assign(static_cast<size_t>(w) * h * 4u, 0);
	size_t off = 0;
	for (uint32_t by = 0; by < h; by += 4) {
		for (uint32_t bx = 0; bx < w; bx += 4) {
			const uint8_t a0 = src[off], a1 = src[off + 1];
			uint64_t abits = 0;
			for (int i = 0; i < 6; ++i)
				abits |= static_cast<uint64_t>(src[off + 2 + i]) << (8 * i);
			off += 8;
			uint8_t av[8];
			av[0] = a0;
			av[1] = a1;
			if (a0 > a1) {
				for (int i = 1; i <= 6; ++i)
					av[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
			} else {
				for (int i = 1; i <= 4; ++i)
					av[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
				av[6] = 0;
				av[7] = 255;
			}
			const uint16_t c0 = static_cast<uint16_t>(src[off] | (src[off + 1] << 8));
			const uint16_t c1 = static_cast<uint16_t>(src[off + 2] | (src[off + 3] << 8));
			const uint32_t bits = static_cast<uint32_t>(src[off + 4]) |
				(static_cast<uint32_t>(src[off + 5]) << 8) |
				(static_cast<uint32_t>(src[off + 6]) << 16) |
				(static_cast<uint32_t>(src[off + 7]) << 24);
			off += 8;
			uint8_t cr[4], cg[4], cb[4];
			Decode565(c0, cr[0], cg[0], cb[0]);
			Decode565(c1, cr[1], cg[1], cb[1]);
			cr[2] = static_cast<uint8_t>((2 * cr[0] + cr[1]) / 3);
			cg[2] = static_cast<uint8_t>((2 * cg[0] + cg[1]) / 3);
			cb[2] = static_cast<uint8_t>((2 * cb[0] + cb[1]) / 3);
			cr[3] = static_cast<uint8_t>((cr[0] + 2 * cr[1]) / 3);
			cg[3] = static_cast<uint8_t>((cg[0] + 2 * cg[1]) / 3);
			cb[3] = static_cast<uint8_t>((cb[0] + 2 * cb[1]) / 3);
			for (uint32_t py = 0; py < 4; ++py) {
				for (uint32_t px = 0; px < 4; ++px) {
					const uint32_t x = bx + px, y = by + py;
					if (x >= w || y >= h)
						continue;
					const uint32_t cidx = (bits >> (2 * (py * 4 + px))) & 3u;
					const uint32_t aidx = static_cast<uint32_t>((abits >> (3 * (py * 4 + px))) & 7u);
					const size_t di = (static_cast<size_t>(y) * w + x) * 4u;
					rgba[di] = cr[cidx];
					rgba[di + 1] = cg[cidx];
					rgba[di + 2] = cb[cidx];
					rgba[di + 3] = av[aidx];
				}
			}
		}
	}
	return true;
}

void ApplyColorKey(std::vector<uint8_t>& rgba, D3DCOLOR key)
{
	if (key == 0)
		return;
	const uint8_t kr = static_cast<uint8_t>((key >> 16) & 0xFF);
	const uint8_t kg = static_cast<uint8_t>((key >> 8) & 0xFF);
	const uint8_t kb = static_cast<uint8_t>(key & 0xFF);
	for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
		if (rgba[i] == kr && rgba[i + 1] == kg && rgba[i + 2] == kb)
			rgba[i + 3] = 0;
	}
}

HRESULT UploadRgbaTexture(LPDIRECT3DDEVICE9 device, UINT tw, UINT th, DWORD Usage, D3DFORMAT Format,
	D3DPOOL Pool, const std::vector<uint8_t>& rgba, D3DXIMAGE_INFO* info, D3DXIMAGE_FILEFORMAT fileFmt,
	LPDIRECT3DTEXTURE9* outTex)
{
	D3DFORMAT fmt = Format;
	if (fmt == D3DFMT_UNKNOWN)
		fmt = D3DFMT_A8R8G8B8;

	LPDIRECT3DTEXTURE9 tex = nullptr;
	HRESULT hr = device->CreateTexture(tw, th, 1, Usage, fmt, Pool, &tex, nullptr);
	if (FAILED(hr) || !tex)
		return hr;

	D3DLOCKED_RECT lr {};
	hr = tex->LockRect(0, &lr, nullptr, 0);
	if (FAILED(hr)) {
		tex->Release();
		return hr;
	}

	for (UINT y = 0; y < th; ++y) {
		auto* dst = static_cast<unsigned char*>(lr.pBits) + y * lr.Pitch;
		const uint8_t* src = rgba.data() + static_cast<size_t>(y) * tw * 4u;
		if (fmt == D3DFMT_A8R8G8B8 || fmt == D3DFMT_X8R8G8B8) {
			for (UINT x = 0; x < tw; ++x) {
				dst[x * 4 + 0] = src[x * 4 + 2];
				dst[x * 4 + 1] = src[x * 4 + 1];
				dst[x * 4 + 2] = src[x * 4 + 0];
				dst[x * 4 + 3] = (fmt == D3DFMT_X8R8G8B8) ? 255 : src[x * 4 + 3];
			}
		} else if (fmt == D3DFMT_A4R4G4B4) {
			auto* d16 = reinterpret_cast<uint16_t*>(dst);
			for (UINT x = 0; x < tw; ++x) {
				const uint16_t a = static_cast<uint16_t>(src[x * 4 + 3] >> 4);
				const uint16_t r = static_cast<uint16_t>(src[x * 4 + 0] >> 4);
				const uint16_t g = static_cast<uint16_t>(src[x * 4 + 1] >> 4);
				const uint16_t b = static_cast<uint16_t>(src[x * 4 + 2] >> 4);
				d16[x] = static_cast<uint16_t>((a << 12) | (r << 8) | (g << 4) | b);
			}
		} else if (fmt == D3DFMT_A1R5G5B5 || fmt == D3DFMT_X1R5G5B5) {
			auto* d16 = reinterpret_cast<uint16_t*>(dst);
			for (UINT x = 0; x < tw; ++x) {
				const uint16_t a = (fmt == D3DFMT_X1R5G5B5 || src[x * 4 + 3] >= 128) ? 1u : 0u;
				const uint16_t r = static_cast<uint16_t>(src[x * 4 + 0] >> 3);
				const uint16_t g = static_cast<uint16_t>(src[x * 4 + 1] >> 3);
				const uint16_t b = static_cast<uint16_t>(src[x * 4 + 2] >> 3);
				d16[x] = static_cast<uint16_t>((a << 15) | (r << 10) | (g << 5) | b);
			}
		} else {
			// Fallback: try as A8R8G8B8 layout
			for (UINT x = 0; x < tw; ++x) {
				dst[x * 4 + 0] = src[x * 4 + 2];
				dst[x * 4 + 1] = src[x * 4 + 1];
				dst[x * 4 + 2] = src[x * 4 + 0];
				dst[x * 4 + 3] = src[x * 4 + 3];
			}
		}
	}
	tex->UnlockRect(0);

	if (info) {
		std::memset(info, 0, sizeof(*info));
		info->Width = tw;
		info->Height = th;
		info->Depth = 1;
		info->MipLevels = 1;
		info->Format = fmt;
		info->ResourceType = D3DRTYPE_TEXTURE;
		info->ImageFileFormat = fileFmt;
	}
	*outTex = tex;
	return S_OK;
}

bool DecodeTgaToRgba(const uint8_t* bytes, UINT size, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h)
{
	if (!bytes || size < 18)
		return false;
	const uint8_t idLen = bytes[0];
	const uint8_t cmapType = bytes[1];
	const uint8_t imageType = bytes[2];
	if (cmapType != 0)
		return false;
	if (imageType != 2 && imageType != 10)
		return false;

	w = static_cast<uint32_t>(bytes[12] | (bytes[13] << 8));
	h = static_cast<uint32_t>(bytes[14] | (bytes[15] << 8));
	const uint8_t bpp = bytes[16];
	const uint8_t desc = bytes[17];
	if (w == 0 || h == 0 || (bpp != 16 && bpp != 24 && bpp != 32))
		return false;

	const size_t headerSkip = 18u + idLen;
	if (size < headerSkip)
		return false;
	const uint8_t* pixels = bytes + headerSkip;
	const size_t avail = size - headerSkip;
	const bool originTop = (desc & 0x20) != 0;
	rgba.assign(static_cast<size_t>(w) * h * 4u, 255);

	auto writePx = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		const uint32_t dy = originTop ? y : (h - 1u - y);
		const size_t di = (static_cast<size_t>(dy) * w + x) * 4u;
		rgba[di] = r;
		rgba[di + 1] = g;
		rgba[di + 2] = b;
		rgba[di + 3] = a;
	};

	if (imageType == 2) {
		const size_t bppBytes = bpp / 8u;
		const size_t need = static_cast<size_t>(w) * h * bppBytes;
		if (avail < need)
			return false;
		for (uint32_t y = 0; y < h; ++y) {
			for (uint32_t x = 0; x < w; ++x) {
				const size_t si = (static_cast<size_t>(y) * w + x) * bppBytes;
				uint8_t r = 0, g = 0, b = 0, a = 255;
				if (bpp == 24) {
					b = pixels[si];
					g = pixels[si + 1];
					r = pixels[si + 2];
				} else if (bpp == 32) {
					b = pixels[si];
					g = pixels[si + 1];
					r = pixels[si + 2];
					a = pixels[si + 3];
				} else {
					const uint16_t v = static_cast<uint16_t>(pixels[si] | (pixels[si + 1] << 8));
					if ((desc & 0x0F) == 0) {
						r = static_cast<uint8_t>(((v >> 11) & 31) * 255 / 31);
						g = static_cast<uint8_t>(((v >> 5) & 63) * 255 / 63);
						b = static_cast<uint8_t>((v & 31) * 255 / 31);
						a = 255;
					} else {
						r = static_cast<uint8_t>(((v >> 10) & 31) * 255 / 31);
						g = static_cast<uint8_t>(((v >> 5) & 31) * 255 / 31);
						b = static_cast<uint8_t>((v & 31) * 255 / 31);
						a = (v & 0x8000) ? 255 : 0;
					}
				}
				writePx(x, y, r, g, b, a);
			}
		}
		return true;
	}

	// TGA RLE type 10
	size_t off = 0;
	uint32_t px = 0;
	const uint32_t total = w * h;
	const size_t bppBytes = bpp / 8u;
	while (px < total && off < avail) {
		const uint8_t packet = pixels[off++];
		const uint32_t count = (packet & 0x7F) + 1u;
		if (packet & 0x80) {
			if (off + bppBytes > avail)
				return false;
			uint8_t r = 0, g = 0, b = 0, a = 255;
			if (bpp == 24) {
				b = pixels[off];
				g = pixels[off + 1];
				r = pixels[off + 2];
			} else if (bpp == 32) {
				b = pixels[off];
				g = pixels[off + 1];
				r = pixels[off + 2];
				a = pixels[off + 3];
			} else {
				const uint16_t v = static_cast<uint16_t>(pixels[off] | (pixels[off + 1] << 8));
				r = static_cast<uint8_t>(((v >> 10) & 31) * 255 / 31);
				g = static_cast<uint8_t>(((v >> 5) & 31) * 255 / 31);
				b = static_cast<uint8_t>((v & 31) * 255 / 31);
				a = (v & 0x8000) ? 255 : 0;
			}
			off += bppBytes;
			for (uint32_t i = 0; i < count && px < total; ++i, ++px)
				writePx(px % w, px / w, r, g, b, a);
		} else {
			for (uint32_t i = 0; i < count && px < total; ++i, ++px) {
				if (off + bppBytes > avail)
					return false;
				uint8_t r = 0, g = 0, b = 0, a = 255;
				if (bpp == 24) {
					b = pixels[off];
					g = pixels[off + 1];
					r = pixels[off + 2];
				} else if (bpp == 32) {
					b = pixels[off];
					g = pixels[off + 1];
					r = pixels[off + 2];
					a = pixels[off + 3];
				} else {
					const uint16_t v = static_cast<uint16_t>(pixels[off] | (pixels[off + 1] << 8));
					r = static_cast<uint8_t>(((v >> 10) & 31) * 255 / 31);
					g = static_cast<uint8_t>(((v >> 5) & 31) * 255 / 31);
					b = static_cast<uint8_t>((v & 31) * 255 / 31);
					a = (v & 0x8000) ? 255 : 0;
				}
				off += bppBytes;
				writePx(px % w, px / w, r, g, b, a);
			}
		}
	}
	return px == total;
}

void CopyDxtBlocks(void* dstBits, INT pitch, const uint8_t* src, UINT tw, UINT th, UINT blockBytes)
{
	const UINT blocksW = (tw + 3) / 4;
	const UINT blocksH = (th + 3) / 4;
	const size_t rowBytes = static_cast<size_t>(blocksW) * blockBytes;
	for (UINT by = 0; by < blocksH; ++by) {
		std::memcpy(static_cast<uint8_t*>(dstBits) + static_cast<size_t>(by) * pitch,
			src + static_cast<size_t>(by) * rowBytes, rowBytes);
	}
}

} // namespace

HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData, UINT SrcDataSize,
	UINT Width, UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD, DWORD, D3DCOLOR ColorKey,
	D3DXIMAGE_INFO* pSrcInfo, PALETTEENTRY*, LPDIRECT3DTEXTURE9* ppTexture)
{
	if (!pDevice || !pSrcData || !SrcDataSize || !ppTexture)
		return E_INVALIDARG;
	*ppTexture = nullptr;

	const auto* bytes = static_cast<const unsigned char*>(pSrcData);

	// DDS (incl. reconstruído pelo TextureManager a partir de .wys)
	if (SrcDataSize >= 128 && bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ') {
		const DWORD hdrSize = *reinterpret_cast<const DWORD*>(bytes + 4);
		if (hdrSize < 124)
			return E_FAIL;
		const DWORD h = *reinterpret_cast<const DWORD*>(bytes + 12);
		const DWORD w = *reinterpret_cast<const DWORD*>(bytes + 16);
		const DWORD pfFlags = *reinterpret_cast<const DWORD*>(bytes + 80);
		const DWORD fourCC = *reinterpret_cast<const DWORD*>(bytes + 84);
		const DWORD rgbBitCount = *reinterpret_cast<const DWORD*>(bytes + 88);

		D3DFORMAT srcFmt = D3DFMT_UNKNOWN;
		if (pfFlags & 0x4) {
			if (fourCC == 0x31545844)
				srcFmt = D3DFMT_DXT1;
			else if (fourCC == 0x33545844)
				srcFmt = D3DFMT_DXT3;
			else if (fourCC == 0x35545844)
				srcFmt = D3DFMT_DXT5;
			else
				return E_FAIL;
		} else if (rgbBitCount == 32)
			srcFmt = D3DFMT_A8R8G8B8;
		else if (rgbBitCount == 16)
			srcFmt = D3DFMT_A4R4G4B4;
		else
			return E_FAIL;

		const UINT tw = Width == static_cast<UINT>(-1) || Width == 0 ? w : Width;
		const UINT th = Height == static_cast<UINT>(-1) || Height == 0 ? h : Height;
		const unsigned char* src = bytes + 4 + hdrSize;
		const size_t payload = SrcDataSize > (4 + hdrSize) ? SrcDataSize - (4 + hdrSize) : 0;

		const bool srcIsDxt = (srcFmt == D3DFMT_DXT1 || srcFmt == D3DFMT_DXT3 || srcFmt == D3DFMT_DXT5);
		const bool wantNativeDxt = (Format == D3DFMT_UNKNOWN || Format == srcFmt) && srcIsDxt
			&& tw == w && th == h;

		if (wantNativeDxt) {
			// Only mip0 is filled below. Requesting MipLevels>1 left undefined
			// higher mips → sampler holes / black patches on DXVK.
			const UINT levels = 1;
			LPDIRECT3DTEXTURE9 tex = nullptr;
			HRESULT hr = pDevice->CreateTexture(tw, th, levels, Usage, srcFmt, Pool, &tex, nullptr);
			if (FAILED(hr) || !tex)
				return hr;
			D3DLOCKED_RECT lr {};
			hr = tex->LockRect(0, &lr, nullptr, 0);
			if (FAILED(hr)) {
				tex->Release();
				return hr;
			}
			const UINT blockBytes = (srcFmt == D3DFMT_DXT1) ? 8u : 16u;
			const size_t need = static_cast<size_t>((tw + 3) / 4) * ((th + 3) / 4) * blockBytes;
			if (payload < need) {
				tex->UnlockRect(0);
				tex->Release();
				return E_FAIL;
			}
			CopyDxtBlocks(lr.pBits, lr.Pitch, src, tw, th, blockBytes);
			tex->UnlockRect(0);
			if (pSrcInfo) {
				std::memset(pSrcInfo, 0, sizeof(*pSrcInfo));
				pSrcInfo->Width = tw;
				pSrcInfo->Height = th;
				pSrcInfo->Depth = 1;
				pSrcInfo->MipLevels = levels;
				pSrcInfo->Format = srcFmt;
				pSrcInfo->ResourceType = D3DRTYPE_TEXTURE;
				pSrcInfo->ImageFileFormat = D3DXIFF_DDS;
			}
			*ppTexture = tex;
			return S_OK;
		}

		std::vector<uint8_t> rgba;
		bool ok = false;
		if (srcFmt == D3DFMT_DXT1)
			ok = DecodeDxt1ToRgba(src, payload, w, h, rgba);
		else if (srcFmt == D3DFMT_DXT3)
			ok = DecodeDxt3ToRgba(src, payload, w, h, rgba);
		else if (srcFmt == D3DFMT_DXT5)
			ok = DecodeDxt5ToRgba(src, payload, w, h, rgba);
		else if (srcFmt == D3DFMT_A8R8G8B8) {
			rgba.resize(static_cast<size_t>(w) * h * 4u);
			for (UINT y = 0; y < h; ++y) {
				const unsigned char* row = src + static_cast<size_t>(y) * w * 4u;
				for (UINT x = 0; x < w; ++x) {
					const size_t di = (static_cast<size_t>(y) * w + x) * 4u;
					rgba[di] = row[x * 4 + 2];
					rgba[di + 1] = row[x * 4 + 1];
					rgba[di + 2] = row[x * 4 + 0];
					rgba[di + 3] = row[x * 4 + 3];
				}
			}
			ok = payload >= static_cast<size_t>(w) * h * 4u;
		}
		if (!ok)
			return E_FAIL;
		ApplyColorKey(rgba, ColorKey);

		if (tw == w && th == h)
			return UploadRgbaTexture(pDevice, tw, th, Usage, Format, Pool, rgba, pSrcInfo, D3DXIFF_DDS, ppTexture);

		std::vector<uint8_t> scaled(static_cast<size_t>(tw) * th * 4u, 0);
		for (UINT y = 0; y < th; ++y) {
			const uint32_t sy = h ? (y * h / th) : 0;
			for (UINT x = 0; x < tw; ++x) {
				const uint32_t sx = w ? (x * w / tw) : 0;
				const size_t di = (static_cast<size_t>(y) * tw + x) * 4u;
				const size_t si = (static_cast<size_t>(sy) * w + sx) * 4u;
				scaled[di] = rgba[si];
				scaled[di + 1] = rgba[si + 1];
				scaled[di + 2] = rgba[si + 2];
				scaled[di + 3] = rgba[si + 3];
			}
		}
		return UploadRgbaTexture(pDevice, tw, th, Usage, Format, Pool, scaled, pSrcInfo, D3DXIFF_DDS, ppTexture);
	}

	// TGA (UI .wyt: TextureManager remove WT10 e anexa footer)
	{
		std::vector<uint8_t> rgba;
		uint32_t w = 0, h = 0;
		if (DecodeTgaToRgba(bytes, SrcDataSize, rgba, w, h)) {
			const UINT tw = (Width == static_cast<UINT>(-1) || Width == 0) ? w : Width;
			const UINT th = (Height == static_cast<UINT>(-1) || Height == 0) ? h : Height;
			ApplyColorKey(rgba, ColorKey);

			// D3DX real: Width/Height pedidos redimensionam. TMFont2 usa isso como
			// hack para alocar 512x64 A4R4G4B4 a partir de minimap.wyt (128x128) e
			// sobrescreve os pixels via LockRect — o conteúdo inicial pouco importa.
			if (tw == w && th == h)
				return UploadRgbaTexture(pDevice, tw, th, Usage, Format, Pool, rgba, pSrcInfo, D3DXIFF_TGA, ppTexture);

			std::vector<uint8_t> scaled(static_cast<size_t>(tw) * th * 4u, 0);
			for (UINT y = 0; y < th; ++y) {
				const uint32_t sy = (h > 0) ? (y * h / th) : 0;
				for (UINT x = 0; x < tw; ++x) {
					const uint32_t sx = (w > 0) ? (x * w / tw) : 0;
					const size_t di = (static_cast<size_t>(y) * tw + x) * 4u;
					const size_t si = (static_cast<size_t>(sy) * w + sx) * 4u;
					scaled[di] = rgba[si];
					scaled[di + 1] = rgba[si + 1];
					scaled[di + 2] = rgba[si + 2];
					scaled[di + 3] = rgba[si + 3];
				}
			}
			return UploadRgbaTexture(pDevice, tw, th, Usage, Format, Pool, scaled, pSrcInfo, D3DXIFF_TGA, ppTexture);
		}
	}

	// BMP (BITMAPFILEHEADER + BITMAPINFOHEADER)
	if (SrcDataSize >= 54 && bytes[0] == 'B' && bytes[1] == 'M') {
		const DWORD pixelOffset = *reinterpret_cast<const DWORD*>(bytes + 10);
		const int w = *reinterpret_cast<const int*>(bytes + 18);
		const int hAbs = *reinterpret_cast<const int*>(bytes + 22);
		const int h = hAbs < 0 ? -hAbs : hAbs;
		const WORD bpp = *reinterpret_cast<const WORD*>(bytes + 28);
		if (w <= 0 || h <= 0 || (bpp != 24 && bpp != 32) || pixelOffset >= SrcDataSize)
			return E_FAIL;

		std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4u);
		const int rowBytes = ((w * (bpp / 8) + 3) & ~3);
		for (int y = 0; y < h; ++y) {
			const int sy = hAbs < 0 ? y : (h - 1 - y);
			const unsigned char* srcRow = bytes + pixelOffset + sy * rowBytes;
			for (int x = 0; x < w; ++x) {
				const unsigned char* p = srcRow + x * (bpp / 8);
				const size_t di = (static_cast<size_t>(y) * w + x) * 4u;
				rgba[di] = p[2];
				rgba[di + 1] = p[1];
				rgba[di + 2] = p[0];
				rgba[di + 3] = (bpp == 32) ? p[3] : 255;
			}
		}
		ApplyColorKey(rgba, ColorKey);
		return UploadRgbaTexture(pDevice, static_cast<UINT>(w), static_cast<UINT>(h), Usage, Format, Pool,
			rgba, pSrcInfo, D3DXIFF_BMP, ppTexture);
	}

	return E_FAIL;
}


HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR pDestFile, D3DXIMAGE_FILEFORMAT DestFormat,
	LPDIRECT3DSURFACE9 pSrcSurface, const PALETTEENTRY*, const RECT* pSrcRect)
{
	if (!pDestFile || !pSrcSurface)
		return E_INVALIDARG;

	D3DSURFACE_DESC desc {};
	pSrcSurface->GetDesc(&desc);
	RECT rect { 0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height) };
	if (pSrcRect)
		rect = *pSrcRect;

	D3DLOCKED_RECT lr {};
	HRESULT hr = pSrcSurface->LockRect(&lr, &rect, D3DLOCK_READONLY);
	if (FAILED(hr))
		return hr;

	const int w = rect.right - rect.left;
	const int h = rect.bottom - rect.top;
	std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
	for (int y = 0; y < h; ++y) {
		const auto* src = static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch;
		unsigned char* dst = rgba.data() + static_cast<size_t>(y) * w * 4;
		for (int x = 0; x < w; ++x) {
			// Assume A8R8G8B8 / X8R8G8B8 layout in memory (B,G,R,A little-endian).
			dst[x * 4 + 0] = src[x * 4 + 2];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + 0];
			dst[x * 4 + 3] = src[x * 4 + 3];
		}
	}
	pSrcSurface->UnlockRect();

	int ok = 0;
	if (DestFormat == D3DXIFF_BMP)
		ok = stbi_write_bmp(pDestFile, w, h, 4, rgba.data());
	else if (DestFormat == D3DXIFF_PNG)
		ok = stbi_write_png(pDestFile, w, h, 4, rgba.data(), w * 4);
	else if (DestFormat == D3DXIFF_TGA)
		ok = stbi_write_tga(pDestFile, w, h, 4, rgba.data());
	else // JPG / default
		ok = stbi_write_jpg(pDestFile, w, h, 4, rgba.data(), 90);

	return ok ? S_OK : E_FAIL;
}

} // extern "C"
