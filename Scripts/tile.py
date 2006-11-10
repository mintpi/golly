# Tile current selection with pattern inside selection.
# Author: Andrew Trevorrow (andrew@trevorrow.com), March 2006.
# Updated to use exit command, Nov 2006.

from glife import *
import golly as g

# ------------------------------------------------------------------------------

def clip_left (patt, left):
   clist = list(patt)
   x = 0
   while x < len(clist):
      if clist[x] < left:
         clist[x : x+2] = []     # remove cell from list
      else:
         x += 2
   return pattern(clist)

# ------------------------------------------------------------------------------

def clip_right (patt, right):
   clist = list(patt)
   x = 0
   while x < len(clist):
      if clist[x] > right:
         clist[x : x+2] = []     # remove cell from list
      else:
         x += 2
   return pattern(clist)

# ------------------------------------------------------------------------------

def clip_top (patt, top):
   clist = list(patt)
   y = 1
   while y < len(clist):
      if clist[y] < top:
         clist[y-1 : y+1] = []   # remove cell from list
      else:
         y += 2
   return pattern(clist)

# ------------------------------------------------------------------------------

def clip_bottom (patt, bottom):
   clist = list(patt)
   y = 1
   while y < len(clist):
      if clist[y] > bottom:
         clist[y-1 : y+1] = []   # remove cell from list
      else:
         y += 2
   return pattern(clist)

# ------------------------------------------------------------------------------

selrect = rect( g.getselrect() )
if selrect.empty: g.exit("There is no selection.")

selpatt = pattern( g.getcells(g.getselrect()) )
if len(selpatt) == 0: g.exit("No pattern in selection.")

# find selpatt's minimal bounding box
bbox = getminbox(selpatt)

# first tile selpatt horizontally, clipping where necessary
left = bbox.left
i = 0
while left > selrect.left:
   left -= bbox.width
   i += 1
   if left >= selrect.left:
      selpatt.put(-bbox.width * i, 0)
   else:
      clip_left( selpatt(-bbox.width * i, 0), selrect.left ).put()
right = bbox.right
i = 0
while right < selrect.right:
   right += bbox.width
   i += 1
   if right <= selrect.right:
      selpatt.put(bbox.width * i, 0)
   else:
      clip_right( selpatt(bbox.width * i, 0), selrect.right ).put()

# get new selection pattern and tile vertically, clipping where necessary
selpatt = pattern( g.getcells(g.getselrect()) )
bbox = getminbox(selpatt)
top = bbox.top
i = 0
while top > selrect.top:
   top -= bbox.height
   i += 1
   if top >= selrect.top:
      selpatt.put(0, -bbox.height * i)
   else:
      clip_top( selpatt(0, -bbox.height * i), selrect.top ).put()
bottom = bbox.bottom
i = 0
while bottom < selrect.bottom:
   bottom += bbox.height
   i += 1
   if bottom <= selrect.bottom:
      selpatt.put(0, bbox.height * i)
   else:
      clip_bottom( selpatt(0, bbox.height * i), selrect.bottom ).put()

if not selrect.visible(): g.fitsel()
