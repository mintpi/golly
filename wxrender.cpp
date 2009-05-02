                        /*** /

This file is part of Golly, a Game of Life Simulator.
Copyright (C) 2009 Andrew Trevorrow and Tomas Rokicki.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 Web site:  http://sourceforge.net/projects/golly
 Authors:   rokicki@gmail.com  andrew@trevorrow.com

                        / ***/

/* -------------------- Some notes on Golly's display code ---------------------

The rectangular area used to display patterns is called the viewport.
It's represented by a window of class PatternView defined in wxview.h.
The global viewptr points to a PatternView window which is created in
MainFrame::MainFrame() in wxmain.cpp.

Nearly all drawing in the viewport is done in this module.  The only other
place is in wxview.cpp where PatternView::DrawOneCell() is used to draw
cells with the pencil cursor.  This is done for performance reasons --
using Refresh + Update to do the drawing is too slow.

The main rendering routine is DrawView() -- see the end of this module.
DrawView() is called from PatternView::OnPaint(), the update event handler
for the viewport window.  Update events are created automatically by the
wxWidgets event dispatcher, or they can be created manually by calling
Refresh() and Update().

DrawView() does the following tasks:

- Calls currlayer->algo->draw() to draw the current pattern.  It passes
  in renderer, an instance of wx_render (derived from liferender) which
  has these methods:
  - killrect() draws a rectangular area of dead cells.
  - pixblit() draws a pixmap containing at least one live cell.
  - getcolors() provides access to the current layer's color arrays.
  
  Note that currlayer->algo->draw() does all the hard work of figuring
  out which parts of the viewport are dead and building all the pixmaps
  for the live parts.  The pixmaps contain suitably shrunken images
  when the scale is < 1:1 (ie. mag < 0).
  
  Each lifealgo needs to implement its own draw() method; for example,
  hlifealgo::draw() in hlifedraw.cpp.

- Calls DrawGridLines() to overlay grid lines if they are visible.

- Calls DrawSelection() to overlay a translucent selection rectangle
  if a selection exists and any part of it is visible.

- Calls DrawStackedLayers() to overlay multiple layers using the current
  layer's scale and location.

- If the user is doing a paste, CheckPasteImage() creates a temporary
  viewport (tempview) and draws the paste pattern (stored in pastealgo)
  into a masked pixmap which is then used by DrawPasteImage().

Potential optimizations:

- Every time DrawView() is called it draws the entire viewport, so
  one improvement would be to try incremental drawing.  Unfortunately
  this is complicated by the fact that wxMac and wxGTK ignore the
  "erase background" parameter in Refresh(false) calls; ie. they
  *always* paint the viewport background on every update event.
  It should be possible to get around this problem by splitting the
  rendering code into 2 phases: the 1st phase would calculate the
  viewport area that has changed and pass that rectangle into Refresh.
  The 2nd phase (done in OnPaint) would call GetUpdateRegion and only
  render that area.
  
  Another idea worth exploring would be to use OpenGL rather than call
  wxDC::DrawBitmap() which seems to be the main reason rendering is slow
  in the Mac and Linux apps (about 3 times slower than the Win app).

Other points of interest:

- The decision to use buffered drawing is made in PatternView::OnPaint().
  It's never used on Mac OS X or GTK+ 2.0 because windows on those systems
  are automatically buffered.  To avoid flicker on Windows, buffering is
  always used if:
  - the user is doing a paste;
  - the grid lines are visible;
  - the selection is visible;
  - multiple layers are being displayed.

- Change the "#if 0" to "#if 1" in wx_render::killrect() to see randomly
  colored rects.  This gives an insight into how lifealgo::draw() works.

- The viewport window is actually the right-hand pane of a wxSplitWindow.
  The left-hand pane is a directory control (wxGenericDirCtrl) that
  displays the user's preferred pattern or script folder.  This is all
  handled in wxmain.cpp.

----------------------------------------------------------------------------- */

#include "wx/wxprec.h"     // for compilers that support precompilation
#ifndef WX_PRECOMP
   #include "wx/wx.h"      // for all others include the necessary headers
#endif

#include "wx/rawbmp.h"     // for wxAlphaPixelData

#include "bigint.h"
#include "lifealgo.h"
#include "viewport.h"

#include "wxgolly.h"       // for viewptr, bigview, statusptr
#include "wxutils.h"       // for Warning, Fatal, FillRect
#include "wxprefs.h"       // for swapcolors, showgridlines, mingridmag, etc
#include "wxstatus.h"      // for statusptr->...
#include "wxview.h"        // for viewptr->...
#include "wxlayer.h"       // currlayer, GetLayer, etc
#include "wxrender.h"

// -----------------------------------------------------------------------------

// globals used in wx_render routines
wxDC* currdc;                 // current device context for viewport
int currwd, currht;           // current width and height of viewport
wxBitmap* pixmap = NULL;      // 32-bit deep bitmap used in pixblit calls
int pixmapwd = -1;            // width of pixmap
int pixmapht = -1;            // height of pixmap
wxBitmap** iconmaps;          // array of icon bitmaps

// for drawing translucent selection (initialized in InitDrawingData)
int selwd;                    // width of selection bitmap
int selht;                    // height of selection bitmap
wxBitmap* selbitmap = NULL;   // selection bitmap (if NULL then inversion is used)
wxBitmap* graybitmap = NULL;  // for inactive selections when drawing multiple layers

// for drawing paste pattern (initialized in CreatePasteImage)
wxBitmap* pastebitmap;        // paste bitmap
int pimagewd;                 // width of paste image
int pimageht;                 // height of paste image
int prectwd;                  // must match viewptr->pasterect.width
int prectht;                  // must match viewptr->pasterect.height
int pastemag;                 // must match current viewport's scale
int cvwd, cvht;               // must match current viewport's width and height
paste_location pasteloc;      // must match plocation
bool pasteicons;              // must match showicons
lifealgo* pastealgo;          // universe containing paste pattern
wxRect pastebbox;             // bounding box in cell coords (not necessarily minimal)

// for drawing multiple layers
wxBitmap* layerbitmap = NULL;    // layer bitmap
int layerwd = -1;                // width of layer bitmap
int layerht = -1;                // height of layer bitmap

// for drawing tile borders
const wxColor dkgray(96, 96, 96);
const wxColor ltgray(224, 224, 224);
const wxColor brightgreen(0, 255, 0);

// -----------------------------------------------------------------------------

void SetSelectionPixels(wxBitmap* bitmap, const wxColor* color)
{
   // set color and alpha of pixels in given bitmap
   wxAlphaPixelData data(*bitmap, wxPoint(0,0), wxSize(selwd,selht));
   if (data) {
      int alpha = 128;     // 50% opaque
      
      #ifdef __WXMSW__
         // premultiply the RGB values on Windows
         int r = color->Red() * alpha / 255;
         int g = color->Green() * alpha / 255;
         int b = color->Blue() * alpha / 255;
      #else
         int r = color->Red();
         int g = color->Green();
         int b = color->Blue();
      #endif
      
      data.UseAlpha();
      wxAlphaPixelData::Iterator p(data);
      for ( int y = 0; y < selht; y++ ) {
         wxAlphaPixelData::Iterator rowstart = p;
         for ( int x = 0; x < selwd; x++ ) {
            p.Red()   = r;
            p.Green() = g;
            p.Blue()  = b;
            p.Alpha() = alpha;
            p++;
         }
         p = rowstart;
         p.OffsetY(data, 1);
      }
   }
}

// -----------------------------------------------------------------------------

void InitDrawingData()
{
   // create translucent selection bitmap
   viewptr->GetClientSize(&selwd, &selht);
   // selwd or selht might be < 1 on Windows
   if (selwd < 1) selwd = 1;
   if (selht < 1) selht = 1;

   // use depth 32 so bitmap has an alpha channel
   selbitmap = new wxBitmap(selwd, selht, 32);
   if (selbitmap == NULL) {
      Warning(_("Not enough memory for selection bitmap!"));
   } else {
      SetSelectionPixels(selbitmap, selectrgb);
   }
   
   // create translucent gray bitmap for inactive selections
   graybitmap = new wxBitmap(selwd, selht, 32);
   if (graybitmap == NULL) {
      Warning(_("Not enough memory for gray bitmap!"));
   } else {
      SetSelectionPixels(graybitmap, wxLIGHT_GREY);
   }
}

// -----------------------------------------------------------------------------

void DestroyDrawingData()
{
   delete layerbitmap;
   delete selbitmap;
   delete graybitmap;
}

// -----------------------------------------------------------------------------

// called from wx_render::pixblit to magnify given pixmap by pmscale (2, 4, ... 2^MAX_MAG)
void DrawStretchedPixmap(unsigned char* byteptr, int x, int y, int w, int h, int pmscale)
{
   int cellsize = pmscale > 2 ? pmscale - 1 : pmscale;
   bool drawgap = (pmscale > 2 && pmscale < (1 << mingridmag)) ||
                  #ifdef __WXMAC__
                     // wxMac seems to draw lines with semi-transparent pixels at the
                     // top/left ends, so we have to draw gaps even if showing grid lines
                     // otherwise we see annoying dots at the top/left edge of the viewport
                     (pmscale >= (1 << mingridmag));
                  #else
                     (pmscale >= (1 << mingridmag) && !showgridlines);
                  #endif
   unsigned char deadred   = currlayer->cellr[0];
   unsigned char deadgreen = currlayer->cellg[0];
   unsigned char deadblue  = currlayer->cellb[0];

   // might be faster to draw rectangles above certain scales???
   wxAlphaPixelData pxldata(*pixmap);
   if (pxldata) {
      #ifdef __WXGTK__
         pxldata.UseAlpha();
      #endif
      wxAlphaPixelData::Iterator p(pxldata);
      for ( int row = 0; row < h; row++ ) {
         wxAlphaPixelData::Iterator rowstart = p;
         for ( int col = 0; col < w; col++ ) {
            int newx = x + col * pmscale;
            int newy = y + row * pmscale;
            if (newx < 0 || newy < 0 || newx >= currwd || newy >= currht) {
               // clip cell outside viewport
            } else {
               unsigned char state = *byteptr;
               unsigned char r = currlayer->cellr[state];
               unsigned char g = currlayer->cellg[state];
               unsigned char b = currlayer->cellb[state];
               
               // expand byte into cellsize*cellsize pixels
               wxAlphaPixelData::Iterator topleft = p;
               for (int i = 0; i < cellsize; i++) {
                  wxAlphaPixelData::Iterator colstart = p;
                  for (int j = 0; j < cellsize; j++) {
                     p.Red()   = r;
                     p.Green() = g;
                     p.Blue()  = b;
                     #ifdef __WXGTK__
                        p.Alpha() = 255;
                     #endif
                     p++;
                  }
                  if (drawgap) {
                     // draw dead pixels at right edge of cell
                     p.Red()   = deadred;
                     p.Green() = deadgreen;
                     p.Blue()  = deadblue;
                     #ifdef __WXGTK__
                        p.Alpha() = 255;
                     #endif
                  }
                  p = colstart;
                  p.OffsetY(pxldata, 1);
               }
               if (drawgap) {
                  // draw dead pixels at bottom edge of cell
                  for (int j = 0; j <= cellsize; j++) {
                     p.Red()   = deadred;
                     p.Green() = deadgreen;
                     p.Blue()  = deadblue;
                     #ifdef __WXGTK__
                        p.Alpha() = 255;
                     #endif
                     p++;
                  }
               }
               p = topleft;
            }
            p.OffsetX(pxldata, pmscale);
            
            byteptr++;        // move to next byte in pmdata
         }
         p = rowstart;
         p.OffsetY(pxldata, pmscale);
      }
   }
   currdc->DrawBitmap(*pixmap, x, y);
}

// -----------------------------------------------------------------------------

void DrawIcons(unsigned char* byteptr, int x, int y, int w, int h, int pmscale)
{
   // called from wx_render::pixblit to draw icons for each live cell;
   // assume pmscale > 2 (should be 8 or 16)
   int cellsize = pmscale - 1;
   bool drawgap = (pmscale < (1 << mingridmag)) ||
                  (pmscale >= (1 << mingridmag) && !showgridlines);
   unsigned char deadred   = currlayer->cellr[0];
   unsigned char deadgreen = currlayer->cellg[0];
   unsigned char deadblue  = currlayer->cellb[0];

   wxAlphaPixelData pxldata(*pixmap);
   if (pxldata) {
      #ifdef __WXGTK__
         pxldata.UseAlpha();
      #endif
      wxAlphaPixelData::Iterator p(pxldata);
      for ( int row = 0; row < h; row++ ) {
         wxAlphaPixelData::Iterator rowstart = p;
         for ( int col = 0; col < w; col++ ) {
            int newx = x + col * pmscale;
            int newy = y + row * pmscale;
            if (newx < 0 || newy < 0 || newx >= currwd || newy >= currht) {
               // clip cell outside viewport
            } else {
               unsigned char state = *byteptr;
               
               wxAlphaPixelData::Iterator topleft = p;
               if (state && iconmaps[state]) {
                  // copy cellsize*cellsize pixels from iconmaps[state]
                  // but convert black pixels to dead cell color;
                  #ifdef __WXMSW__
                     // must use wxNativePixelData for bitmaps with no alpha channel
                     wxNativePixelData icondata(*iconmaps[state]);
                  #else
                     wxAlphaPixelData icondata(*iconmaps[state]);
                  #endif
                  if (icondata) {
                     #ifdef __WXMSW__
                        wxNativePixelData::Iterator iconpxl(icondata);
                     #else
                        wxAlphaPixelData::Iterator iconpxl(icondata);
                     #endif
                     for (int i = 0; i < cellsize; i++) {
                        wxAlphaPixelData::Iterator colstart = p;
                        #ifdef __WXMSW__
                           wxNativePixelData::Iterator iconrow = iconpxl;
                        #else
                           wxAlphaPixelData::Iterator iconrow = iconpxl;
                        #endif
                        for (int j = 0; j < cellsize; j++) {
                           if (iconpxl.Red() || iconpxl.Green() || iconpxl.Blue()) {
                              // replace non-black pixel with current cell color
                              p.Red()   = currlayer->cellr[state];
                              p.Green() = currlayer->cellg[state];
                              p.Blue()  = currlayer->cellb[state];
                           } else {
                              // replace black pixel with dead cell color
                              p.Red()   = deadred;
                              p.Green() = deadgreen;
                              p.Blue()  = deadblue;
                           }
                           #ifdef __WXGTK__
                              p.Alpha() = 255;
                           #endif
                           p++;
                           iconpxl++;
                        }
                        if (drawgap) {
                           // draw dead pixels at right edge of cell
                           p.Red()   = deadred;
                           p.Green() = deadgreen;
                           p.Blue()  = deadblue;
                           #ifdef __WXGTK__
                              p.Alpha() = 255;
                           #endif
                        }
                        p = colstart;
                        p.OffsetY(pxldata, 1);
                        // move to next row of icon bitmap
                        iconpxl = iconrow;
                        iconpxl.OffsetY(icondata, 1);
                     }
                     if (drawgap) {
                        // draw dead pixels at bottom edge of cell
                        for (int j = 0; j <= cellsize; j++) {
                           p.Red()   = deadred;
                           p.Green() = deadgreen;
                           p.Blue()  = deadblue;
                           #ifdef __WXGTK__
                              p.Alpha() = 255;
                           #endif
                           p++;
                        }
                     }
                  }
               } else {
                  // draw dead cell
                  for (int i = 0; i < cellsize; i++) {
                     wxAlphaPixelData::Iterator colstart = p;
                     for (int j = 0; j < cellsize; j++) {
                        p.Red()   = deadred;
                        p.Green() = deadgreen;
                        p.Blue()  = deadblue;
                        #ifdef __WXGTK__
                           p.Alpha() = 255;
                        #endif
                        p++;
                     }
                     if (drawgap) {
                        // draw dead pixels at right edge of cell
                        p.Red()   = deadred;
                        p.Green() = deadgreen;
                        p.Blue()  = deadblue;
                        #ifdef __WXGTK__
                           p.Alpha() = 255;
                        #endif
                     }
                     p = colstart;
                     p.OffsetY(pxldata, 1);
                  }
                  if (drawgap) {
                     // draw dead pixels at bottom edge of cell
                     for (int j = 0; j <= cellsize; j++) {
                        p.Red()   = deadred;
                        p.Green() = deadgreen;
                        p.Blue()  = deadblue;
                        #ifdef __WXGTK__
                           p.Alpha() = 255;
                        #endif
                        p++;
                     }
                  }
               }
               p = topleft;
               
            }
            p.OffsetX(pxldata, pmscale);
            
            byteptr++;        // move to next byte in pmdata
         }
         p = rowstart;
         p.OffsetY(pxldata, pmscale);
      }
   }
   currdc->DrawBitmap(*pixmap, x, y);   
}

// -----------------------------------------------------------------------------

void DrawOneIcon(wxDC& dc, int x, int y, wxBitmap* icon,
                 unsigned char r, unsigned char g, unsigned char b)
{
   // copy pixels from icon but convert black pixels to dead cell color
   // and convert non-black pixels to given live cell color
   int wd = icon->GetWidth();
   int ht = icon->GetHeight();
   wxBitmap pixmap(wd, ht, 32);

   wxAlphaPixelData pxldata(pixmap);
   if (pxldata) {
      #ifdef __WXGTK__
         pxldata.UseAlpha();
      #endif
      wxAlphaPixelData::Iterator p(pxldata);

      #ifdef __WXMSW__
         // must use wxNativePixelData for bitmaps with no alpha channel
         wxNativePixelData icondata(*icon);
      #else
         wxAlphaPixelData icondata(*icon);
      #endif

      if (icondata) {
         #ifdef __WXMSW__
            wxNativePixelData::Iterator iconpxl(icondata);
         #else
            wxAlphaPixelData::Iterator iconpxl(icondata);
         #endif

         unsigned char deadred   = deadrgb->Red();
         unsigned char deadgreen = deadrgb->Green();
         unsigned char deadblue  = deadrgb->Blue();

         for (int i = 0; i < ht; i++) {
            wxAlphaPixelData::Iterator pixmaprow = p;
            #ifdef __WXMSW__
               wxNativePixelData::Iterator iconrow = iconpxl;
            #else
               wxAlphaPixelData::Iterator iconrow = iconpxl;
            #endif
            for (int j = 0; j < wd; j++) {
               if (iconpxl.Red() || iconpxl.Green() || iconpxl.Blue()) {
                  // replace non-black pixel with given cell color
                  p.Red()   = r;
                  p.Green() = g;
                  p.Blue()  = b;
               } else {
                  // replace black pixel with dead cell color
                  p.Red()   = deadred;
                  p.Green() = deadgreen;
                  p.Blue()  = deadblue;
               }
               #ifdef __WXGTK__
                  p.Alpha() = 255;
               #endif
               p++;
               iconpxl++;
            }
            // move to next row of pixmap
            p = pixmaprow;
            p.OffsetY(pxldata, 1);
            // move to next row of icon bitmap
            iconpxl = iconrow;
            iconpxl.OffsetY(icondata, 1);
         }
      }
   }
   dc.DrawBitmap(pixmap, x, y);
}

// -----------------------------------------------------------------------------

class wx_render : public liferender
{
public:
   wx_render() {}
   virtual ~wx_render() {}
   virtual void killrect(int x, int y, int w, int h);
   virtual void pixblit(int x, int y, int w, int h, char* pm, int pmscale);
   virtual void getcolors(unsigned char** r, unsigned char** g, unsigned char** b);
};

wx_render renderer;     // create instance

// -----------------------------------------------------------------------------

void wx_render::killrect(int x, int y, int w, int h)
{
   //!!! is Tom's hashdraw code doing unnecessary work???
   if (x >= currwd || y >= currht) return;
   if (x + w <= 0 || y + h <= 0) return;
      
   if (w <= 0 || h <= 0) return;

   // clip given rect so it's within viewport
   int clipx = x < 0 ? 0 : x;
   int clipy = y < 0 ? 0 : y;
   int clipr = x + w;
   int clipb = y + h;
   if (clipr > currwd) clipr = currwd;
   if (clipb > currht) clipb = currht;
   int clipwd = clipr - clipx;
   int clipht = clipb - clipy;
   wxRect rect(clipx, clipy, clipwd, clipht);

   #if 0
      // use a different pale color each time to see any probs
      wxBrush randbrush(wxColor((rand()&127)+128,
                                (rand()&127)+128,
                                (rand()&127)+128));
      FillRect(*currdc, rect, randbrush);
   #else
      FillRect(*currdc, rect, *deadbrush);
   #endif
}

// -----------------------------------------------------------------------------

void wx_render::pixblit(int x, int y, int w, int h, char* pmdata, int pmscale)
{
   //!!! is Tom's hashdraw code doing unnecessary work???
   if (x >= currwd || y >= currht) return;
   if (x + w <= 0 || y + h <= 0) return;

   // faster to create new pixmap only when size changes
   if (pixmapwd != w || pixmapht != h) {
      delete pixmap;
      pixmap = new wxBitmap(w, h, 32);
      pixmapwd = w;
      pixmapht = h;
   }

   if (pmscale == 1) {
      // pmdata contains 3 bytes (the rgb values) for each pixel
      wxAlphaPixelData pxldata(*pixmap);
      if (pxldata) {
         #ifdef __WXGTK__
            pxldata.UseAlpha();
         #endif
         wxAlphaPixelData::Iterator p(pxldata);
         unsigned char* byteptr = (unsigned char*) pmdata;
         for ( int row = 0; row < h; row++ ) {
            wxAlphaPixelData::Iterator rowstart = p;
            for ( int col = 0; col < w; col++ ) {
               p.Red()   = *byteptr++;
               p.Green() = *byteptr++;
               p.Blue()  = *byteptr++;
               #ifdef __WXGTK__
                  p.Alpha() = 255;
               #endif
               p++;
            }
            p = rowstart;
            p.OffsetY(pxldata, 1);
         }
      }
      currdc->DrawBitmap(*pixmap, x, y);

   } else if (showicons && pmscale > 4 && iconmaps) {
      // draw icons only at scales 1:8 or 1:16
      DrawIcons((unsigned char*) pmdata, x, y, w/pmscale, h/pmscale, pmscale);

   } else {
      // stretch pixmap by pmscale, assuming pmdata contains (w/pmscale)*(h/pmscale) bytes
      // where each byte contains a cell state
      DrawStretchedPixmap((unsigned char*) pmdata, x, y, w/pmscale, h/pmscale, pmscale);
   }
}

// -----------------------------------------------------------------------------

void wx_render::getcolors(unsigned char** r, unsigned char** g, unsigned char** b)
{
   *r = currlayer->cellr;
   *g = currlayer->cellg;
   *b = currlayer->cellb;
}

// -----------------------------------------------------------------------------

void CheckSelectionSize(int viewwd, int viewht)
{
   if (viewwd != selwd || viewht != selht) {
      // resize selbitmap and graybitmap
      selwd = viewwd;
      selht = viewht;
      delete selbitmap;
      delete graybitmap;
      // use depth 32 so bitmaps have an alpha channel
      selbitmap = new wxBitmap(selwd, selht, 32);
      graybitmap = new wxBitmap(selwd, selht, 32);
      if (selbitmap) SetSelectionPixels(selbitmap, selectrgb);
      if (graybitmap) SetSelectionPixels(graybitmap, wxLIGHT_GREY);
   }
}

// -----------------------------------------------------------------------------

void SetSelectionColor()
{
   // selectrgb has changed
   if (selbitmap) SetSelectionPixels(selbitmap, selectrgb);
}

// -----------------------------------------------------------------------------

void DrawSelection(wxDC& dc, wxRect& rect)
{
   if (selbitmap) {
      #ifdef __WXGTK__
         // wxGTK Blit doesn't support alpha channel
         if (selectrgb->Red() == 255 && selectrgb->Green() == 255 && selectrgb->Blue() == 255) {
            // use inversion for speed
            dc.Blit(rect.x, rect.y, rect.width, rect.height, &dc, rect.x, rect.y, wxINVERT);
         } else {
            dc.DrawBitmap(selbitmap->GetSubBitmap(rect), rect.x, rect.y, true);
         }
      #else
         // Blit seems to be about 10% faster (on Mac at least)
         wxMemoryDC memdc;
         memdc.SelectObject(*selbitmap);
         dc.Blit(rect.x, rect.y, rect.width, rect.height, &memdc, 0, 0, wxCOPY, true);
      #endif
   } else {
      // no alpha channel so just invert rect
      dc.Blit(rect.x, rect.y, rect.width, rect.height, &dc, rect.x, rect.y, wxINVERT);
   }
}

// -----------------------------------------------------------------------------

void DrawInactiveSelection(wxDC& dc, wxRect& rect)
{
   if (graybitmap) {
      #ifdef __WXGTK__
         // wxGTK Blit doesn't support alpha channel
         if (selectrgb->Red() == 255 && selectrgb->Green() == 255 && selectrgb->Blue() == 255) {
            // use inversion for speed
            dc.Blit(rect.x, rect.y, rect.width, rect.height, &dc, rect.x, rect.y, wxINVERT);
         } else {
            dc.DrawBitmap(graybitmap->GetSubBitmap(rect), rect.x, rect.y, true);
         }
      #else
         // Blit seems to be about 10% faster (on Mac at least)
         wxMemoryDC memdc;
         memdc.SelectObject(*graybitmap);
         dc.Blit(rect.x, rect.y, rect.width, rect.height, &memdc, 0, 0, wxCOPY, true);
      #endif
   } else {
      // no alpha channel so just invert rect
      dc.Blit(rect.x, rect.y, rect.width, rect.height, &dc, rect.x, rect.y, wxINVERT);
   }
}

// -----------------------------------------------------------------------------

void CreatePasteImage(lifealgo* palgo, wxRect& bbox)
{
   pastealgo = palgo;      // save for use in CheckPasteImage
   pastebbox = bbox;       // ditto
   pastebitmap = NULL;
   prectwd = -1;           // force CheckPasteImage to update paste image
   prectht = -1;
   pimagewd = -1;          // force CheckPasteImage to rescale paste image
   pimageht = -1;
   pastemag = currlayer->view->getmag();
   pasteicons = showicons;
}

// -----------------------------------------------------------------------------

void DestroyPasteImage()
{
   if (pastebitmap) {
      delete pastebitmap;
      pastebitmap = NULL;
   }
}

// -----------------------------------------------------------------------------

void MaskDeadPixels(wxBitmap* bitmap, int wd, int ht, int livealpha)
{
   // access pixels in given bitmap and make all dead pixels 100% transparent
   // and use given alpha value for all live pixels
   wxAlphaPixelData data(*bitmap, wxPoint(0,0), wxSize(wd,ht));
   if (data) {
      int deadr = deadrgb->Red();
      int deadg = deadrgb->Green();
      int deadb = deadrgb->Blue();

      data.UseAlpha();
      wxAlphaPixelData::Iterator p(data);
      for ( int y = 0; y < ht; y++ ) {
         wxAlphaPixelData::Iterator rowstart = p;
         for ( int x = 0; x < wd; x++ ) {
            // get pixel color
            int r = p.Red();
            int g = p.Green();
            int b = p.Blue();

            // set alpha value depending on whether pixel is live or dead
            if (r == deadr && g == deadg && b == deadb) {
               // make dead pixel 100% transparent
               p.Red()   = 0;
               p.Green() = 0;
               p.Blue()  = 0;
               p.Alpha() = 0;
            } else {
               // live pixel
               #ifdef __WXMSW__
                  // premultiply the RGB values on Windows
                  p.Red()   = r * livealpha / 255;
                  p.Green() = g * livealpha / 255;
                  p.Blue()  = b * livealpha / 255;
               #else
                  // no change needed
                  // p.Red()   = r;
                  // p.Green() = g;
                  // p.Blue()  = b;
               #endif
               p.Alpha() = livealpha;
            }

            p++;
         }
         p = rowstart;
         p.OffsetY(data, 1);
      }
   }
}

// -----------------------------------------------------------------------------

int PixelsToCells(int pixels) {
   // convert given # of screen pixels to corresponding # of cells
   if (pastemag >= 0) {
      int cellsize = 1 << pastemag;
      return (pixels + cellsize - 1) / cellsize;
   } else {
      // pastemag < 0; no need to worry about overflow
      return pixels << -pastemag;
   }
}

// -----------------------------------------------------------------------------

void CheckPasteImage()
{
   // paste image needs to be updated if pasterect size changed
   // or viewport size changed or plocation changed or showicons changed
   if ( prectwd != viewptr->pasterect.width ||
        prectht != viewptr->pasterect.height ||
        cvwd != currlayer->view->getwidth() ||
        cvht != currlayer->view->getheight() ||
        pasteloc != plocation ||
        pasteicons != showicons
      ) {
      prectwd = viewptr->pasterect.width;
      prectht = viewptr->pasterect.height;
      pastemag = currlayer->view->getmag();
      cvwd = currlayer->view->getwidth();
      cvht = currlayer->view->getheight();
      pasteloc = plocation;
      pasteicons = showicons;

      // calculate size of paste image; we could just set it to pasterect size
      // but that would be slow and wasteful for large pasterects, so we use
      // the following code (the only tricky bit is when plocation = Middle)
      int pastewd = prectwd;
      int pasteht = prectht;
      
      if (pastewd <= 2 || pasteht <= 2) {
         // no need to draw paste image because border lines will cover it
         delete pastebitmap;
         pastebitmap = NULL;
         if (pimagewd != 1 || pimageht != 1) {
            pimagewd = 1;
            pimageht = 1;
         }
         return;
      }
      
      wxRect cellbox = pastebbox;
      if (pastewd > currlayer->view->getwidth() || pasteht > currlayer->view->getheight()) {
         if (plocation == Middle) {
            // temporary viewport may need to be TWICE size of current viewport
            if (pastewd > 2 * currlayer->view->getwidth())
               pastewd = 2 * currlayer->view->getwidth();
            if (pasteht > 2 * currlayer->view->getheight())
               pasteht = 2 * currlayer->view->getheight();
            if (pastemag > 0) {
               // make sure pastewd/ht don't have partial cells
               int cellsize = 1 << pastemag;
               if ((pastewd + 1) % cellsize > 0)
                  pastewd += cellsize - ((pastewd + 1) % cellsize);
               if ((pasteht + 1) % cellsize != 0)
                  pasteht += cellsize - ((pasteht + 1) % cellsize);
            }
            if (prectwd > pastewd) {
               // make sure prectwd - pastewd is an even number of *cells*
               // to simplify shifting in DrawPasteImage
               if (pastemag > 0) {
                  int cellsize = 1 << pastemag;
                  int celldiff = (prectwd - pastewd) / cellsize;
                  if (celldiff & 1) pastewd += cellsize;
               } else {
                  if ((prectwd - pastewd) & 1) pastewd++;
               }
            }
            if (prectht > pasteht) {
               // make sure prectht - pasteht is an even number of *cells*
               // to simplify shifting in DrawPasteImage
               if (pastemag > 0) {
                  int cellsize = 1 << pastemag;
                  int celldiff = (prectht - pasteht) / cellsize;
                  if (celldiff & 1) pasteht += cellsize;
               } else {
                  if ((prectht - pasteht) & 1) pasteht++;
               }
            }
         } else {
            // plocation is at a corner of pasterect so temporary viewport
            // may need to be size of current viewport
            if (pastewd > currlayer->view->getwidth())
               pastewd = currlayer->view->getwidth();
            if (pasteht > currlayer->view->getheight())
               pasteht = currlayer->view->getheight();
            if (pastemag > 0) {
               // make sure pastewd/ht don't have partial cells
               int cellsize = 1 << pastemag;
               int gap = 1;                        // gap between cells
               if (pastemag == 1) gap = 0;         // no gap at scale 1:2
               if ((pastewd + gap) % cellsize > 0)
                  pastewd += cellsize - ((pastewd + gap) % cellsize);
               if ((pasteht + gap) % cellsize != 0)
                  pasteht += cellsize - ((pasteht + gap) % cellsize);
            }
            cellbox.width = PixelsToCells(pastewd);
            cellbox.height = PixelsToCells(pasteht);
            if (plocation == TopLeft) {
               // show top left corner of pasterect
               cellbox.x = pastebbox.x;
               cellbox.y = pastebbox.y;
            } else if (plocation == TopRight) {
               // show top right corner of pasterect
               cellbox.x = pastebbox.x + pastebbox.width - cellbox.width;
               cellbox.y = pastebbox.y;
            } else if (plocation == BottomRight) {
               // show bottom right corner of pasterect
               cellbox.x = pastebbox.x + pastebbox.width - cellbox.width;
               cellbox.y = pastebbox.y + pastebbox.height - cellbox.height;
            } else { // plocation == BottomLeft
               // show bottom left corner of pasterect
               cellbox.x = pastebbox.x;
               cellbox.y = pastebbox.y + pastebbox.height - cellbox.height;
            }
         }
      }
      
      // delete old bitmap even if size hasn't changed
      delete pastebitmap;
      pimagewd = pastewd;
      pimageht = pasteht;
      // create a bitmap with depth 32 so it has an alpha channel
      pastebitmap = new wxBitmap(pimagewd, pimageht, 32);
      
      if (pastebitmap) {
         // create temporary viewport and draw pattern into pastebitmap
         // for later use in DrawPasteImage
         viewport tempview(pimagewd, pimageht);
         int midx, midy;
         if (pastemag > 0) {
            midx = cellbox.x + cellbox.width / 2;
            midy = cellbox.y + cellbox.height / 2;
         } else {
            midx = cellbox.x + (cellbox.width - 1) / 2;
            midy = cellbox.y + (cellbox.height - 1) / 2;
         }
         tempview.setpositionmag(midx, midy, pastemag);

         // set rgb values for dead cells in pixblit calls
         currlayer->cellr[0] = deadrgb->Red();
         currlayer->cellg[0] = deadrgb->Green();
         currlayer->cellb[0] = deadrgb->Blue();
         
         // temporarily turn off grid lines
         bool saveshow = showgridlines;
         showgridlines = false;
         
         wxMemoryDC pattdc;
         pattdc.SelectObject(*pastebitmap);
         
         currdc = &pattdc;
         currwd = tempview.getwidth();
         currht = tempview.getheight();
         pastealgo->draw(tempview, renderer);
         
         showgridlines = saveshow;
         
         // make dead pixels 100% transparent and live pixels 100% opaque
         MaskDeadPixels(pastebitmap, pimagewd, pimageht, 255);
      }
   }
}

// -----------------------------------------------------------------------------

void DrawPasteImage(wxDC& dc)
{
   if (pastebitmap) {
      // draw paste image
      wxRect r = viewptr->pasterect;
      if (r.width > pimagewd || r.height > pimageht) {
         // paste image is smaller than pasterect (which can't fit in viewport)
         // so shift image depending on plocation
         switch (plocation) {
            case TopLeft:
               // no need to do any shifting
               break;
            case TopRight:
               // shift image to top right corner of pasterect
               r.x += r.width - pimagewd;
               break;
            case BottomRight:
               // shift image to bottom right corner of pasterect
               r.x += r.width - pimagewd;
               r.y += r.height - pimageht;
               break;
            case BottomLeft:
               // shift image to bottom left corner of pasterect
               r.y += r.height - pimageht;
               break;
            case Middle:
               // shift image to middle of pasterect; note that CheckPasteImage
               // has ensured (r.width - pimagewd) and (r.height - pimageht)
               // are an even number of *cells* if pastemag > 0
               r.x += (r.width - pimagewd) / 2;
               r.y += (r.height - pimageht) / 2;
               break;
         }
      }
      #ifdef __WXGTK__
         // wxGTK Blit doesn't support alpha channel
         dc.DrawBitmap(*pastebitmap, r.x, r.y, true);
      #else
         wxMemoryDC memdc;
         memdc.SelectObject(*pastebitmap);
         dc.Blit(r.x, r.y, pimagewd, pimageht, &memdc, 0, 0, wxCOPY, true);
      #endif
   }

   // now overlay border rectangle
   dc.SetPen(*pastepen);
   dc.SetBrush(*wxTRANSPARENT_BRUSH);

   // if large rect then we need to avoid overflow because DrawRectangle
   // has problems on Mac if given a size that exceeds 32K
   wxRect r = viewptr->pasterect;
   if (r.x < 0) { int diff = -1 - r.x;  r.x = -1;  r.width -= diff; }
   if (r.y < 0) { int diff = -1 - r.y;  r.y = -1;  r.height -= diff; }
   if (r.width > currlayer->view->getwidth())
      r.width = currlayer->view->getwidth() + 2;
   if (r.height > currlayer->view->getheight())
      r.height = currlayer->view->getheight() + 2;
   dc.DrawRectangle(r);
   
   if (r.y > 0) {
      dc.SetFont(*statusptr->GetStatusFont());
      //dc.SetBackgroundMode(wxSOLID);
      //dc.SetTextBackground(*wxWHITE);
      dc.SetBackgroundMode(wxTRANSPARENT);   // better in case pastergb is white
      dc.SetTextForeground(*pastergb);
      wxString pmodestr = wxString(GetPasteMode(),wxConvLocal);
      int pmodex = r.x + 2;
      int pmodey = r.y - 4;
      dc.DrawText(pmodestr, pmodex, pmodey - statusptr->GetTextAscent());
   }
   
   dc.SetBrush(wxNullBrush);
   dc.SetPen(wxNullPen);
}

// -----------------------------------------------------------------------------

void DrawGridLines(wxDC& dc, wxRect& r)
{
   int cellsize = 1 << currlayer->view->getmag();
   int h, v, i, topbold, leftbold;

   if (showboldlines) {
      // ensure that origin cell stays next to bold lines;
      // ie. bold lines scroll when pattern is scrolled
      pair<bigint, bigint> lefttop = currlayer->view->at(0, 0);
      leftbold = lefttop.first.mod_smallint(boldspacing);
      topbold = lefttop.second.mod_smallint(boldspacing);
      if (currlayer->originx != bigint::zero) {
         leftbold -= currlayer->originx.mod_smallint(boldspacing);
      }
      if (currlayer->originy != bigint::zero) {
         topbold -= currlayer->originy.mod_smallint(boldspacing);
      }
      if (mathcoords) topbold--;   // show origin cell above bold line
   } else {
      // avoid spurious gcc warning
      topbold = leftbold = 0;
   }

   // draw all plain lines first
   dc.SetPen(*gridpen);
   
   i = showboldlines ? topbold : 1;
   v = -1;
   while (true) {
      v += cellsize;
      if (v >= r.height) break;
      if (showboldlines) i++;
      if (i % boldspacing != 0 && v >= r.y && v < r.y + r.height)
         dc.DrawLine(r.x, v, r.GetRight() + 1, v);
   }
   i = showboldlines ? leftbold : 1;
   h = -1;
   while (true) {
      h += cellsize;
      if (h >= r.width) break;
      if (showboldlines) i++;
      if (i % boldspacing != 0 && h >= r.x && h < r.x + r.width)
         dc.DrawLine(h, r.y, h, r.GetBottom() + 1);
   }

   if (showboldlines) {
      // overlay bold lines
      dc.SetPen(*boldpen);
      i = topbold;
      v = -1;
      while (true) {
         v += cellsize;
         if (v >= r.height) break;
         i++;
         if (i % boldspacing == 0 && v >= r.y && v < r.y + r.height)
            dc.DrawLine(r.x, v, r.GetRight() + 1, v);
      }
      i = leftbold;
      h = -1;
      while (true) {
         h += cellsize;
         if (h >= r.width) break;
         i++;
         if (i % boldspacing == 0 && h >= r.x && h < r.x + r.width)
            dc.DrawLine(h, r.y, h, r.GetBottom() + 1);
      }
   }
   
   dc.SetPen(wxNullPen);
}

// -----------------------------------------------------------------------------

void DrawOneLayer(wxDC& dc)
{
   wxMemoryDC layerdc;
   layerdc.SelectObject(*layerbitmap);
   
   currdc = &layerdc;
   currlayer->algo->draw(*currlayer->view, renderer);
   
   // make dead pixels 100% transparent; live pixels use opacity setting
   MaskDeadPixels(layerbitmap, layerwd, layerht, int(2.55 * opacity));
   
   // draw result
   dc.DrawBitmap(*layerbitmap, 0, 0, true);
}

// -----------------------------------------------------------------------------

void DrawStackedLayers(wxDC& dc)
{
   // check if layerbitmap needs to be created or resized
   if ( layerwd != currlayer->view->getwidth() ||
        layerht != currlayer->view->getheight() ) {
      layerwd = currlayer->view->getwidth();
      layerht = currlayer->view->getheight();
      delete layerbitmap;
      // create a bitmap with depth 32 so it has an alpha channel
      layerbitmap = new wxBitmap(layerwd, layerht, 32);
      if (!layerbitmap) {
         Fatal(_("Not enough memory for layer bitmap!"));
         return;
      }
   }

   // temporarily turn off grid lines
   bool saveshow = showgridlines;
   showgridlines = false;
   
   // draw patterns in layers 1..numlayers-1
   for ( int i = 1; i < numlayers; i++ ) {
      Layer* savelayer = currlayer;
      currlayer = GetLayer(i);
   
      // set rgb values for dead cells in pixblit calls
      currlayer->cellr[0] = deadrgb->Red();
      currlayer->cellg[0] = deadrgb->Green();
      currlayer->cellb[0] = deadrgb->Blue();
      
      // use real current layer's viewport
      viewport* saveview = currlayer->view;
      currlayer->view = savelayer->view;
      
      // avoid drawing a cloned layer more than once??? draw first or last clone???

      if ( !currlayer->algo->isEmpty() ) {
         DrawOneLayer(dc);
      }
      
      // draw this layer's selection if necessary
      wxRect r;
      if ( currlayer->currsel.Visible(&r) ) {
         CheckSelectionSize(layerwd, layerht);
         if (i == currindex)
            DrawSelection(dc, r);
         else
            DrawInactiveSelection(dc, r);
      }
      
      // restore viewport and currlayer
      currlayer->view = saveview;
      currlayer = savelayer;
   }
   
   showgridlines = saveshow;
}

// -----------------------------------------------------------------------------

void DrawTileFrame(wxDC& dc, wxRect& trect, wxBrush& brush, int wd)
{
   trect.Inflate(wd);
   wxRect r = trect;

   r.height = wd;
   FillRect(dc, r, brush);       // top edge
   
   r.y += trect.height - wd;
   FillRect(dc, r, brush);       // bottom edge
   
   r = trect;
   r.width = wd;
   FillRect(dc, r, brush);       // left edge
   
   r.x += trect.width - wd;
   FillRect(dc, r, brush);       // right edge
}

// -----------------------------------------------------------------------------

void DrawTileBorders(wxDC& dc)
{
   if (tileborder <= 0) return;    // no borders
   
   // draw tile borders in bigview window
   int wd, ht;
   bigview->GetClientSize(&wd, &ht);
   if (wd < 1 || ht < 1) return;
   
   wxBrush brush;
   int gray = (int) ((deadrgb->Red() + deadrgb->Green() + deadrgb->Blue()) / 3.0);
   if (gray > 127) {
      // deadrgb is light
      brush.SetColour(dkgray);
   } else {
      // deadrgb is dark
      brush.SetColour(ltgray);
   }
   wxRect trect;
   for ( int i = 0; i < numlayers; i++ ) {
      trect = GetLayer(i)->tilerect;
      DrawTileFrame(dc, trect, brush, tileborder);
   }

   // draw different colored border to indicate tile for current layer
   trect = GetLayer(currindex)->tilerect;
   brush.SetColour(brightgreen);
   DrawTileFrame(dc, trect, brush, tileborder);
}

// -----------------------------------------------------------------------------

void DrawView(wxDC& dc, int tileindex)
{
   wxRect r;
   Layer* savelayer = NULL;
   viewport* saveview0 = NULL;
   int colorindex;
   
   if ( viewptr->nopattupdate ) {
      // don't draw incomplete pattern, just fill background
      r = wxRect(0, 0, currlayer->view->getwidth(), currlayer->view->getheight());
      FillRect(dc, r, *deadbrush);
      // might as well draw grid lines
      if ( viewptr->GridVisible() ) DrawGridLines(dc, r);
      return;
   }

   if ( numlayers > 1 && tilelayers ) {
      if ( tileindex < 0 ) {
         DrawTileBorders(dc);
         return;
      }
      // tileindex >= 0 so temporarily change some globals to draw this tile
      if ( syncviews && tileindex != currindex ) {
         // make sure this layer uses same location and scale as current layer
         GetLayer(tileindex)->view->setpositionmag(currlayer->view->x,
                                                   currlayer->view->y,
                                                   currlayer->view->getmag());
      }
      savelayer = currlayer;
      currlayer = GetLayer(tileindex);
      viewptr = currlayer->tilewin;
      colorindex = tileindex;
   } else if ( numlayers > 1 && stacklayers ) {
      // draw all layers starting with layer 0 but using current layer's viewport
      savelayer = currlayer;
      if ( currindex != 0 ) {
         // change currlayer to layer 0
         currlayer = GetLayer(0);
         saveview0 = currlayer->view;
         currlayer->view = savelayer->view;
      }
      colorindex = 0;
   } else {
      // just draw the current layer
      colorindex = currindex;
   }
   
   // set rgb values for dead cells in pixblit calls
   currlayer->cellr[0] = deadrgb->Red();
   currlayer->cellg[0] = deadrgb->Green();
   currlayer->cellb[0] = deadrgb->Blue();

   // only show icons at scales 1:8 and 1:16
   if (showicons && currlayer->view->getmag() > 2) {
      if (currlayer->view->getmag() == 3) {
         iconmaps = currlayer->icons7x7;
      } else {
         iconmaps = currlayer->icons15x15;
      }
   }

   // draw pattern using a sequence of pixblit and killrect calls
   currdc = &dc;
   currwd = currlayer->view->getwidth();
   currht = currlayer->view->getheight();
   currlayer->algo->draw(*currlayer->view, renderer);

   if ( viewptr->GridVisible() ) {
      r = wxRect(0, 0, currlayer->view->getwidth(), currlayer->view->getheight());
      DrawGridLines(dc, r);
   }
   
   if ( currlayer->currsel.Visible(&r) ) {
      CheckSelectionSize(currlayer->view->getwidth(), currlayer->view->getheight());
      if (colorindex == currindex)
         DrawSelection(dc, r);
      else
         DrawInactiveSelection(dc, r);
   }
   
   if ( numlayers > 1 && stacklayers ) {
      // must restore currlayer before we call DrawStackedLayers
      currlayer = savelayer;
      if ( saveview0 ) {
         // restore layer 0's viewport
         GetLayer(0)->view = saveview0;
      }
      // draw layers 1, 2, ... numlayers-1
      DrawStackedLayers(dc);
   }
   
   if ( viewptr->waitingforclick && viewptr->pasterect.width > 0 ) {
      // this test is not really necessary, but it avoids unnecessary
      // drawing of the paste image when user changes scale
      if ( pastemag != currlayer->view->getmag() &&
           prectwd == viewptr->pasterect.width && prectwd > 1 &&
           prectht == viewptr->pasterect.height && prectht > 1 ) {
         // don't draw old paste image, a new one is coming very soon
      } else {
         CheckPasteImage();
         DrawPasteImage(dc);
      }
   }

   if ( numlayers > 1 && tilelayers ) {
      // restore globals changed above
      currlayer = savelayer;
      viewptr = currlayer->tilewin;
   }
}
