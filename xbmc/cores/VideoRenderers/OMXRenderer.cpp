/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#ifdef TARGET_RASPBERRY_PI

#include "Util.h"
#include "OMXRenderer.h"
#include "cores/dvdplayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "filesystem/File.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/Texture.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/MathUtils.h"
#include "utils/SystemInfo.h"
#include "windowing/WindowingFactory.h"
#include "cores/FFmpeg.h"
#include "cores/dvdplayer/DVDCodecs/Video/OpenMaxVideo.h"

#define CLASSNAME "COMXRenderer"

COMXRenderer::YUVBUFFER::YUVBUFFER()
{
  memset(&fields, 0, sizeof(fields));
  memset(&image , 0, sizeof(image));
  flipindex = 0;
  openMaxBuffer = NULL;
}

COMXRenderer::YUVBUFFER::~YUVBUFFER()
{
}


COMXRenderer::COMXRenderer()
{
  m_iYV12RenderBuffer = 0;
  m_NumYV12Buffers = 0;
}

COMXRenderer::~COMXRenderer()
{
  UnInit();
}

void COMXRenderer::AddProcessor(COpenMaxVideoBuffer *openMaxBuffer, int index)
{
  CLog::Log(LOGNOTICE, "%s::%s - %p (%p) %i", CLASSNAME, __func__, openMaxBuffer, openMaxBuffer->mmal_buffer, index);

  YUVBUFFER &buf = m_buffers[index];
  COpenMaxVideoBuffer *pic = openMaxBuffer->Acquire();
  SAFE_RELEASE(buf.openMaxBuffer);
  buf.openMaxBuffer = pic;
}

bool COMXRenderer::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags, ERenderFormat format, unsigned extended_format, unsigned int orientation)
{
  if(m_sourceWidth  != width
  || m_sourceHeight != height)
  {
    m_sourceWidth       = width;
    m_sourceHeight      = height;
  }
  CLog::Log(LOGNOTICE, "%s::%s - %dx%d->%dx%d@%.2f flags:%x format:%d ext:%x orient:%d", CLASSNAME, __func__, width, height, d_width, d_height, fps, flags, format, extended_format, orientation);

  m_fps = fps;
  m_iFlags = flags;
  m_format = format;

  m_RenderUpdateCallBackFn = NULL;
  m_RenderUpdateCallBackCtx = NULL;

  // calculate the input frame aspect ratio
  CalculateFrameAspectRatio(d_width, d_height);
  ChooseBestResolution(fps);
  m_destWidth = g_graphicsContext.GetResInfo(m_resolution).iWidth;
  m_destHeight = g_graphicsContext.GetResInfo(m_resolution).iHeight;
  SetViewMode(CMediaSettings::Get().GetCurrentVideoSettings().m_ViewMode);
  ManageDisplay();

  m_bConfigured = true;

  return true;
}

int COMXRenderer::NextYV12Texture()
{
  return (m_iYV12RenderBuffer + 1) % m_NumYV12Buffers;
}

int COMXRenderer::GetImage(YV12Image *image, int source, bool readonly)
{
  CLog::Log(LOGNOTICE, "%s::%s - %p %d %d", CLASSNAME, __func__, image, source, readonly);
  if (!image) return -1;

  /* take next available buffer */
  if( source == AUTOSOURCE )
   source = NextYV12Texture();

  assert(m_format != RENDER_FMT_BYPASS);
  if (m_format == RENDER_FMT_OMXEGL)
  {
    return source;
  }

  YV12Image &im = m_buffers[source].image;

  // copy the image - should be operator of YV12Image
  for (int p=0;p<MAX_PLANES;p++)
  {
    image->plane[p]  = im.plane[p];
    image->stride[p] = im.stride[p];
  }
  image->width    = im.width;
  image->height   = im.height;
  image->flags    = im.flags;
  image->cshift_x = im.cshift_x;
  image->cshift_y = im.cshift_y;
  image->bpp      = 1;

  return source;
}

void COMXRenderer::ReleaseImage(int source, bool preserve)
{
  CLog::Log(LOGNOTICE, "%s::%s - %d %d", CLASSNAME, __func__, source, preserve);
  // no need to release anything here since we're using system memory
}

void COMXRenderer::Reset()
{
  CLog::Log(LOGNOTICE, "%s::%s", CLASSNAME, __func__);
}

void COMXRenderer::Update()
{
  CLog::Log(LOGNOTICE, "%s::%s", CLASSNAME, __func__);
  if (!m_bConfigured) return;
  ManageDisplay();
}

void COMXRenderer::RenderUpdate(bool clear, DWORD flags, DWORD alpha)
{
  CLog::Log(LOGNOTICE, "%s::%s - %d %x %d", CLASSNAME, __func__, clear, flags, alpha);

  if (!m_bConfigured) return;

  CSingleLock lock(g_graphicsContext);

  ManageDisplay();
  if (m_RenderUpdateCallBackFn)
    (*m_RenderUpdateCallBackFn)(m_RenderUpdateCallBackCtx, m_sourceRect, m_destRect);

  CRect old = g_graphicsContext.GetScissors();

  g_graphicsContext.BeginPaint();
  g_graphicsContext.SetScissors(m_destRect);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  g_graphicsContext.SetScissors(old);
  g_graphicsContext.EndPaint();

  if (m_format == RENDER_FMT_BYPASS)
    return;

  if (m_buffers[m_iYV12RenderBuffer].openMaxBuffer)
  {
    m_buffers[m_iYV12RenderBuffer].openMaxBuffer->Render();
  }
  else
  {
    //assert(0);
    printf("Render %dx%d %p,%p,%p\n", m_buffers[m_iYV12RenderBuffer].image.width, m_buffers[m_iYV12RenderBuffer].image.height, m_buffers[m_iYV12RenderBuffer].image.plane[0], m_buffers[m_iYV12RenderBuffer].image.plane[1], m_buffers[m_iYV12RenderBuffer].image.plane[2]);
  }
}

void COMXRenderer::FlipPage(int source)
{
  CLog::Log(LOGNOTICE, "%s::%s - %d", CLASSNAME, __func__, source);
  if( source >= 0 && source < m_NumYV12Buffers )
    m_iYV12RenderBuffer = source;
  else
    m_iYV12RenderBuffer = NextYV12Texture();
}

unsigned int COMXRenderer::PreInit()
{
  CSingleLock lock(g_graphicsContext);
  m_bConfigured = false;
  UnInit();
  m_resolution = CDisplaySettings::Get().GetCurrentResolution();
  if ( m_resolution == RES_WINDOW )
    m_resolution = RES_DESKTOP;

  CLog::Log(LOGNOTICE, "%s::%s", CLASSNAME, __func__);

  m_formats.clear();
  m_formats.push_back(RENDER_FMT_YUV420P);
  m_formats.push_back(RENDER_FMT_OMXEGL);
  m_formats.push_back(RENDER_FMT_BYPASS);

  m_iYV12RenderBuffer = 0;
  m_NumYV12Buffers = 2;


  return 0;
}

void COMXRenderer::UnInit()
{
  CSingleLock lock(g_graphicsContext);
  CLog::Log(LOGNOTICE, "%s::%s", CLASSNAME, __func__);

  m_bConfigured = false;
}

bool COMXRenderer::RenderCapture(CRenderCapture* capture)
{
  if (!m_bConfigured)
    return false;

  bool succeeded = false;

  CLog::Log(LOGNOTICE, "%s::%s - %p", CLASSNAME, __func__, capture);

  return succeeded;
}

//********************************************************************************************************
// YV12 Texture creation, deletion, copying + clearing
//********************************************************************************************************

bool COMXRenderer::Supports(EDEINTERLACEMODE mode)
{
  if(mode == VS_DEINTERLACEMODE_OFF
  || mode == VS_DEINTERLACEMODE_AUTO
  || mode == VS_DEINTERLACEMODE_FORCE)
    return true;

  return false;
}

bool COMXRenderer::Supports(EINTERLACEMETHOD method)
{
  if (method == VS_INTERLACEMETHOD_DEINTERLACE_HALF)
    return true;

  return false;
}

bool COMXRenderer::Supports(ERENDERFEATURE feature)
{
  if (feature == RENDERFEATURE_STRETCH         ||
      feature == RENDERFEATURE_CROP            ||
      feature == RENDERFEATURE_ZOOM            ||
      feature == RENDERFEATURE_VERTICAL_SHIFT  ||
      feature == RENDERFEATURE_PIXEL_RATIO)
    return true;

  return false;
}

bool COMXRenderer::Supports(ESCALINGMETHOD method)
{
  return false;
}

EINTERLACEMETHOD COMXRenderer::AutoInterlaceMethod()
{
  return VS_INTERLACEMETHOD_DEINTERLACE_HALF;
}

unsigned int COMXRenderer::GetProcessorSize()
{
  if (m_format == RENDER_FMT_OMXEGL)
    return 1;
  else
    return 0;
}

#endif
