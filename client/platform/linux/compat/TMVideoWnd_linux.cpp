// TMVideoWnd — intro AVI. No 7.48 o DirectShow já estava inativo no fonte;
// mantemos a API e ponteiros COM válidos; reprodução SDL quando houver asset.
#include "pch.h"
#include "TMVideoWnd.h"

namespace {
IVideoWindow g_videoWindow;
IBasicVideo g_basicVideo;
IMediaControl g_mediaControl;
IMediaEventEx g_mediaEvent;
IGraphBuilder g_graphBuilder;
}

TMVideoWnd::TMVideoWnd(int bFull)
{
	m_bFullscreen = bFull;
	m_psCurrent = PLAYSTATE::Init;
	m_szFileName[0] = 0;
	m_pGB = &g_graphBuilder;
	m_pMC = &g_mediaControl;
	m_pME = &g_mediaEvent;
	m_pVW = &g_videoWindow;
	m_pBV = &g_basicVideo;
}

TMVideoWnd::~TMVideoWnd()
{
	CloseInterfaces();
}

HRESULT TMVideoWnd::PlayMovieInWindow(char* szFile)
{
	if (!szFile || !szFile[0])
		return E_INVALIDARG;
	return OpenClip(szFile) ? S_OK : E_FAIL;
}

HRESULT TMVideoWnd::InitVideoWindow() { return S_OK; }
void TMVideoWnd::MoveVideoWindow() {}
void TMVideoWnd::CheckVisibility() {}

int TMVideoWnd::OpenClip(const char* szFilename)
{
	if (!szFilename)
		return 0;
	std::strncpy(m_szFileName, szFilename, sizeof(m_szFileName) - 1);
	m_szFileName[sizeof(m_szFileName) - 1] = 0;
	FILE* fp = fopen(szFilename, "rb");
	if (!fp)
		return 0;
	std::fclose(fp);
	m_psCurrent = PLAYSTATE::Running;
	return 1;
}

void TMVideoWnd::CloseClip() { m_psCurrent = PLAYSTATE::Stopped; }
void TMVideoWnd::CloseInterfaces() { m_psCurrent = PLAYSTATE::Stopped; }
HRESULT TMVideoWnd::ToggleFullScreen() { return S_OK; }
int TMVideoWnd::HandleGraphEvent() { return 1; }
