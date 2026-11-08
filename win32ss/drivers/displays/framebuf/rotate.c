/*
 * PROJECT:     ReactOS Framebuffer Display Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Desktop turned by 90 or 270 degrees on a panel that scans out the other way
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include "framebuf.h"

/* GDI draws the desktop into a shadow bitmap, every hooked call copies what it changed onto the panel */

static
BOOL
FbIsShadow(
   IN SURFOBJ *pso)
{
   PPDEV ppdev;

   if (!pso || !pso->dhpdev)
      return FALSE;
   ppdev = (PPDEV)pso->dhpdev;
   return ppdev->Rotation && pso->hsurf == ppdev->hSurfEng;
}

static
VOID
FbClip(
   IN OUT RECTL *prcl,
   IN CLIPOBJ *pco)
{
   if (pco && pco->iDComplexity != DC_TRIVIAL)
   {
      prcl->left = max(prcl->left, pco->rclBounds.left);
      prcl->top = max(prcl->top, pco->rclBounds.top);
      prcl->right = min(prcl->right, pco->rclBounds.right);
      prcl->bottom = min(prcl->bottom, pco->rclBounds.bottom);
   }
}

/* copies a rectangle of the shadow, in desktop coordinates with the end exclusive, onto the panel */
VOID
FbRotateRect(
   IN PPDEV ppdev,
   IN RECTL *prcl)
{
   LONG Left = max(prcl->left, 0), Top = max(prcl->top, 0);
   LONG Right = min(prcl->right, (LONG)ppdev->ScreenWidth);
   LONG Bottom = min(prcl->bottom, (LONG)ppdev->ScreenHeight);
   LONG PanelWidth = (LONG)ppdev->ScreenHeight, PanelHeight = (LONG)ppdev->ScreenWidth;
   LONG ShadowPixels = ppdev->ShadowDelta / sizeof(ULONG);
   PULONG Shadow = (PULONG)ppdev->ShadowBits, Line, Source;
   LONG x, y;

   if (Left >= Right || Top >= Bottom)
      return;

   /* panel rows in order, the shadow is cached and the panel often is not */
   if (ppdev->Rotation == 270)
   {
      /* desktop (X, Y) lands at panel (PanelWidth - 1 - Y, X) */
      for (y = Left; y < Right; y++)
      {
         Line = (PULONG)((PUCHAR)ppdev->ScreenPtr + y * ppdev->ScreenDelta);
         Source = Shadow + (PanelWidth - 1 - (PanelWidth - Bottom)) * ShadowPixels + y;
         for (x = PanelWidth - Bottom; x < PanelWidth - Top; x++)
         {
            Line[x] = *Source;
            Source -= ShadowPixels;
         }
      }
   }
   else
   {
      /* desktop (X, Y) lands at panel (Y, PanelHeight - 1 - X) */
      for (y = PanelHeight - Right; y < PanelHeight - Left; y++)
      {
         Line = (PULONG)((PUCHAR)ppdev->ScreenPtr + y * ppdev->ScreenDelta);
         Source = Shadow + Top * ShadowPixels + (PanelHeight - 1 - y);
         for (x = Top; x < Bottom; x++)
         {
            Line[x] = *Source;
            Source += ShadowPixels;
         }
      }
   }
}

static
VOID
FbRotateClipped(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN CLIPOBJ *pco)
{
   RECTL Rect;

   if (!FbIsShadow(pso) || !prcl)
      return;

   /* stretched targets may come inverted */
   Rect.left = min(prcl->left, prcl->right);
   Rect.right = max(prcl->left, prcl->right);
   Rect.top = min(prcl->top, prcl->bottom);
   Rect.bottom = max(prcl->top, prcl->bottom);
   FbClip(&Rect, pco);
   FbRotateRect((PPDEV)pso->dhpdev, &Rect);
}

/* a path's bounds in whole pixels, with room for the pen */
static
VOID
FbRotatePath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN LONG Margin)
{
   RECTFX Bounds;
   RECTL Rect;

   if (!FbIsShadow(pso))
      return;

   PATHOBJ_vGetBounds(ppo, &Bounds);
   Rect.left = (Bounds.xLeft >> 4) - Margin;
   Rect.top = (Bounds.yTop >> 4) - Margin;
   Rect.right = ((Bounds.xRight + 15) >> 4) + Margin + 1;
   Rect.bottom = ((Bounds.yBottom + 15) >> 4) + Margin + 1;
   FbClip(&Rect, pco);
   FbRotateRect((PPDEV)pso->dhpdev, &Rect);
}

BOOL APIENTRY
DrvBitBlt(
   IN SURFOBJ *psoTrg,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclTrg,
   IN POINTL *pptlSrc,
   IN POINTL *pptlMask,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrush,
   IN ROP4 rop4)
{
   BOOL Result = EngBitBlt(psoTrg, psoSrc, psoMask, pco, pxlo, prclTrg,
                           pptlSrc, pptlMask, pbo, pptlBrush, rop4);
   FbRotateClipped(psoTrg, prclTrg, pco);
   return Result;
}

BOOL APIENTRY
DrvCopyBits(
   OUT SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN POINTL *pptlSrc)
{
   BOOL Result = EngCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc);
   FbRotateClipped(psoDest, prclDest, pco);
   return Result;
}

BOOL APIENTRY
DrvStretchBlt(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN COLORADJUSTMENT *pca,
   IN POINTL *pptlHTOrg,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN POINTL *pptlMask,
   IN ULONG iMode)
{
   BOOL Result = EngStretchBlt(psoDest, psoSrc, psoMask, pco, pxlo, pca,
                               pptlHTOrg, prclDest, prclSrc, pptlMask, iMode);
   FbRotateClipped(psoDest, prclDest, pco);
   return Result;
}

BOOL APIENTRY
DrvTransparentBlt(
   IN SURFOBJ *psoDst,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDst,
   IN RECTL *prclSrc,
   IN ULONG iTransColor,
   IN ULONG ulReserved)
{
   BOOL Result = EngTransparentBlt(psoDst, psoSrc, pco, pxlo, prclDst, prclSrc,
                                   iTransColor, ulReserved);
   FbRotateClipped(psoDst, prclDst, pco);
   return Result;
}

BOOL APIENTRY
DrvAlphaBlend(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN BLENDOBJ *pBlendObj)
{
   BOOL Result = EngAlphaBlend(psoDest, psoSrc, pco, pxlo, prclDest, prclSrc, pBlendObj);
   FbRotateClipped(psoDest, prclDest, pco);
   return Result;
}

BOOL APIENTRY
DrvGradientFill(
   IN SURFOBJ *psoDest,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN TRIVERTEX *pVertex,
   IN ULONG nVertex,
   IN PVOID pMesh,
   IN ULONG nMesh,
   IN RECTL *prclExtents,
   IN POINTL *pptlDitherOrg,
   IN ULONG ulMode)
{
   BOOL Result = EngGradientFill(psoDest, pco, pxlo, pVertex, nVertex, pMesh, nMesh,
                                 prclExtents, pptlDitherOrg, ulMode);
   FbRotateClipped(psoDest, prclExtents, pco);
   return Result;
}

BOOL APIENTRY
DrvTextOut(
   IN SURFOBJ *pso,
   IN STROBJ *pstro,
   IN FONTOBJ *pfo,
   IN CLIPOBJ *pco,
   IN RECTL *prclExtra,
   IN RECTL *prclOpaque,
   IN BRUSHOBJ *pboFore,
   IN BRUSHOBJ *pboOpaque,
   IN POINTL *pptlOrg,
   IN MIX mix)
{
   BOOL Result = EngTextOut(pso, pstro, pfo, pco, prclExtra, prclOpaque,
                            pboFore, pboOpaque, pptlOrg, mix);
   FbRotateClipped(pso, prclOpaque ? prclOpaque : &pstro->rclBkGround, pco);
   return Result;
}

BOOL APIENTRY
DrvStrokePath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN XFORMOBJ *pxo,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN LINEATTRS *plineattrs,
   IN MIX mix)
{
   BOOL Result = EngStrokePath(pso, ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix);
   LONG Margin = 2;

   /* geometric pens carry their width, cosmetic ones are a pixel or so */
   if (plineattrs && (plineattrs->fl & LA_GEOMETRIC))
      Margin += 64;
   FbRotatePath(pso, ppo, pco, Margin);
   return Result;
}

BOOL APIENTRY
DrvFillPath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN MIX mix,
   IN FLONG flOptions)
{
   BOOL Result = EngFillPath(pso, ppo, pco, pbo, pptlBrushOrg, mix, flOptions);
   FbRotatePath(pso, ppo, pco, 1);
   return Result;
}

BOOL APIENTRY
DrvLineTo(
   IN SURFOBJ *pso,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN LONG x1,
   IN LONG y1,
   IN LONG x2,
   IN LONG y2,
   IN RECTL *prclBounds,
   IN MIX mix)
{
   BOOL Result = EngLineTo(pso, pco, pbo, x1, y1, x2, y2, prclBounds, mix);
   RECTL Rect;

   Rect.left = min(x1, x2) - 1;
   Rect.top = min(y1, y2) - 1;
   Rect.right = max(x1, x2) + 2;
   Rect.bottom = max(y1, y2) + 2;
   FbRotateClipped(pso, &Rect, pco);
   return Result;
}
