//------------------------------------------------------------------
// ProjectionPicture.C
// -------------------------------------------------------------------
#include "ProjectionPicture.H"
#include "PltApp.H"
#include "DataServices.H"
#include "Volume.H"
#include "List.H"
#include <math.h>
#include <time.h>

#define VOLUMEBOXES 0

// -------------------------------------------------------------------
ProjectionPicture::ProjectionPicture(PltApp *pltappptr, ViewTransform *vtptr,
                                     Palette *PalettePtr,
                                     Widget da, int w, int h)
{
  int lev;
  pltAppPtr = pltappptr;
  amrPicturePtr = pltAppPtr->GetAmrPicturePtr(XY); 
  minDrawnLevel = pltAppPtr->MinDrawnLevel();
  maxDrawnLevel = pltAppPtr->MaxDrawnLevel();
  drawingArea = da; 
  viewTransformPtr = vtptr;
  daWidth = w;
  daHeight = h;
  palettePtr = PalettePtr;

  volumeBoxColor = (unsigned char) GetBoxColor();
  volumeBoxColor = Max((unsigned char) 0, Min((unsigned char) MaxPaletteIndex(),
			volumeBoxColor));

  showSubCut = false;
  pixCreated = false;
  imageData = NULL;
  maxDataLevel = amrPicturePtr->MaxAllowableLevel();
  theDomain = amrPicturePtr->GetSubDomain();

#ifdef BL_VOLUMERENDER
  volRender = new VolRender(theDomain, minDrawnLevel, maxDrawnLevel, palettePtr);
#endif

  SetDrawingAreaDimensions(daWidth, daHeight);

  const AmrData &amrData = pltAppPtr->GetDataServicesPtr()->AmrDataRef();

  boxReal.resize(maxDataLevel + 1);
  boxTrans.resize(maxDataLevel + 1);
  for(lev = minDrawnLevel; lev <= maxDataLevel; ++lev) {
    int nBoxes(amrData.NIntersectingGrids(lev, theDomain[lev]));
    boxReal[lev].resize(nBoxes);
    boxTrans[lev].resize(nBoxes);
  }
  subCutColor = 160;
  sliceColor = 100;
  boxColors.resize(maxDataLevel + 1);
  for(lev = minDrawnLevel; lev <= maxDataLevel; ++lev) {
    int iBoxIndex(0);
    if(lev == minDrawnLevel) {
      boxColors[lev] = pltAppPtr->GetPalettePtr()->WhiteIndex();
    } else {
      boxColors[lev] = MaxPaletteIndex() - 80 * (lev - 1);
    }
    boxColors[lev] = Max(0, Min(MaxPaletteIndex(), boxColors[lev]));
    for(int iBox = 0; iBox < amrData.boxArray(lev).length(); ++iBox) {
      Box temp(amrData.boxArray(lev)[iBox]);
      if(temp.intersects(theDomain[lev])) {
        temp &= theDomain[lev];
        AddBox(temp.refine(CRRBetweenLevels(lev, maxDataLevel,
		amrData.RefRatio())), iBoxIndex, lev);
	++iBoxIndex;
      }
    }
  }
  Box alignedBox(theDomain[minDrawnLevel]);
  alignedBox.refine(CRRBetweenLevels(minDrawnLevel, maxDataLevel,
		     amrData.RefRatio()));
  realBoundingBox = RealBox(alignedBox);

  LoadSlices(alignedBox);

  xcenter = (theDomain[maxDataLevel].bigEnd(XDIR) -
		theDomain[maxDataLevel].smallEnd(XDIR)) / 2 +
		theDomain[maxDataLevel].smallEnd(XDIR);
  ycenter = (theDomain[maxDataLevel].bigEnd(YDIR) -
		theDomain[maxDataLevel].smallEnd(YDIR)) / 2 +
		theDomain[maxDataLevel].smallEnd(YDIR);
  zcenter = (theDomain[maxDataLevel].bigEnd(ZDIR) -
		theDomain[maxDataLevel].smallEnd(ZDIR)) / 2 +
		theDomain[maxDataLevel].smallEnd(ZDIR);	// center of 3D object

  viewTransformPtr->SetObjCenter(xcenter, ycenter, zcenter);
  viewTransformPtr->SetScreenPosition(daWidth/2, daHeight/2);
  ReadTransferFile("vpramps.dat");
}  // end ProjectionPicture()


// -------------------------------------------------------------------
ProjectionPicture::~ProjectionPicture() {
  delete [] imageData;
  if(pixCreated) {
    XFreePixmap(XtDisplay(drawingArea), pixMap);
  }
#ifdef BL_VOLUMERENDER
  delete volRender;
#endif
}


// -------------------------------------------------------------------
void ProjectionPicture::AddBox(const Box &theBox, int index, int level) {
    RealBox newBox(theBox);
    boxReal[level][index] = newBox;
}


// -------------------------------------------------------------------
void ProjectionPicture::TransformBoxPoints(int iLevel, int iBoxIndex) {
  Real px, py, pz;

  for(int i = 0; i < NVERTICIES; ++i) {
    viewTransformPtr->
	   TransformPoint(boxReal[iLevel][iBoxIndex].vertices[i].component[0],
			  boxReal[iLevel][iBoxIndex].vertices[i].component[1],
			  boxReal[iLevel][iBoxIndex].vertices[i].component[2],
			  px, py, pz);
    boxTrans[iLevel][iBoxIndex].vertices[i] =
	     TransPoint((int)(px+.5), daHeight-(int)(py+.5));
  }
}


// -------------------------------------------------------------------
void ProjectionPicture::MakeBoxes() {
  minDrawnLevel = pltAppPtr->MinDrawnLevel();
  maxDrawnLevel = pltAppPtr->MaxDrawnLevel();

  if(amrPicturePtr->ShowingBoxes()) {
    for(int iLevel = minDrawnLevel; iLevel <= maxDrawnLevel; ++iLevel) {
      int nBoxes(boxTrans[iLevel].length());
      for(int iBox = 0; iBox < nBoxes; ++iBox) {
        TransformBoxPoints(iLevel, iBox);
      }
    }
  }

  MakeAuxiliaries(); // make subcut, boundingbox, slices

}


// -------------------------------------------------------------------
void ProjectionPicture::MakeAuxiliaries() {
    if(showSubCut) {
        MakeSubCutBox();
    }
    MakeBoundingBox();
    MakeSlices();
}

// -------------------------------------------------------------------
void ProjectionPicture::MakeBoundingBox() {
    Real px, py, pz;
    for(int i = 0; i < NVERTICIES; ++i) {
        viewTransformPtr->
            TransformPoint(realBoundingBox.vertices[i].component[0],
                           realBoundingBox.vertices[i].component[1],
                           realBoundingBox.vertices[i].component[2],
                           px, py, pz);
        transBoundingBox.vertices[i] = TransPoint((int)(px+.5), 
                                                  daHeight-(int)(py+.5));
    }
} 


// -------------------------------------------------------------------
void ProjectionPicture::MakeSubCutBox() {
    Real px, py, pz;
    minDrawnLevel = pltAppPtr->MinDrawnLevel();
    maxDrawnLevel = pltAppPtr->MaxDrawnLevel();
    
    for(int i = 0; i < NVERTICIES; ++i) {
        viewTransformPtr->
            TransformPoint(realSubCutBox.vertices[i].component[0],
                           realSubCutBox.vertices[i].component[1],
                           realSubCutBox.vertices[i].component[2],
                           px, py, pz);
        transSubCutBox.vertices[i] = TransPoint((int)(px+.5), 
                                                daHeight-(int)(py+.5));
   
    }
}   


// -------------------------------------------------------------------
void ProjectionPicture::MakeSlices() {
    Real px, py, pz;
    for(int j = 0; j< 3 ; j++) {
        for(int i = 0; i < 4; ++i) {
            viewTransformPtr->
                TransformPoint(realSlice[j].edges[i].component[0],
                               realSlice[j].edges[i].component[1],
                               realSlice[j].edges[i].component[2],
                               px, py, pz);
            transSlice[j].edges[i] = TransPoint((int)(px+.5), 
                                                daHeight-(int)(py+.5));
        }
    }
}

// -------------------------------------------------------------------
void ProjectionPicture::MakePicture() {
#ifdef BL_VOLUMERENDER
  clock_t time0 = clock();

  scale[XDIR] = scale[YDIR] = scale[ZDIR] = viewTransformPtr->GetScale();

  Real mvmat[4][4];

  Real tempLenRatio = longestWindowLength/
      (scale[longestBoxSideDir]*longestBoxSide);
  Real tempLen = 0.5*tempLenRatio;
  viewTransformPtr->SetAdjustments(tempLen, daWidth, daHeight);
  viewTransformPtr->MakeTransform();

  viewTransformPtr->GetRenderRotationMat(mvmat);
  volRender->MakePicture(mvmat, tempLen,
                         daWidth, daHeight);

  // map imageData colors to colormap range
  Palette *palPtr = pltAppPtr->GetPalettePtr();
  if(palPtr->ColorSlots() != palPtr->PaletteSize()) {
    const unsigned char *remapTable = palPtr->RemapTable();
    int idat;
    for(idat=0; idat<daWidth*daHeight; idat++) {
      imageData[idat] = remapTable[(unsigned char)imageData[idat]];
    }
  }

  
  unsigned char iMin, iMax;
  iMin = 255;  iMax = 0;
  for(int ii = 0; ii < daWidth * daHeight; ++ii) {
      iMin = Min(iMin, imageData[ii]);
      iMax = Max(iMax, imageData[ii]);
  }

  XPutImage(XtDisplay(drawingArea), pixMap, XtScreen(drawingArea)->
        default_gc, PPXImage, 0, 0, 0, 0, daWidth, daHeight);

  cout << "----- make picture time = " << ((clock()-time0)/1000000.0) << endl;

#endif

}  // end MakePicture()


// -------------------------------------------------------------------
void ProjectionPicture::DrawBoxes(int iFromLevel, int iToLevel) {
  DrawBoxesIntoDrawable(XtWindow(drawingArea), iFromLevel, iToLevel);
}

 
// -------------------------------------------------------------------
void ProjectionPicture::DrawBoxesIntoDrawable(const Drawable &drawable,
					      int iFromLevel, int iToLevel)
{
  minDrawnLevel = pltAppPtr->MinDrawnLevel();
  maxDrawnLevel = pltAppPtr->MaxDrawnLevel();
  
  if(amrPicturePtr->ShowingBoxes()) {
    for(int iLevel = iFromLevel; iLevel <= iToLevel; ++iLevel) {
      XSetForeground(XtDisplay(drawingArea), XtScreen(drawingArea)->default_gc,
                     boxColors[iLevel]);
      int nBoxes(boxTrans[iLevel].length());
      for(int iBox = 0; iBox < nBoxes; ++iBox) {
        boxTrans[iLevel][iBox].Draw(XtDisplay(drawingArea), drawable,
                                    XtScreen(drawingArea)->default_gc);
        
      }
    }
  }
  DrawAuxiliaries(drawable);
  XSetForeground(XtDisplay(drawingArea), XtScreen(drawingArea)->default_gc,
                 boxColors[minDrawnLevel]);
}


// -------------------------------------------------------------------
void ProjectionPicture::DrawAuxiliaries(const Drawable &drawable) {
  if (showSubCut) {
    XSetForeground(XtDisplay(drawingArea), XtScreen(drawingArea)->default_gc,
                   subCutColor);
    transSubCutBox.Draw(XtDisplay(drawingArea),drawable,
                        XtScreen(drawingArea)->default_gc);
  }    
  //bounding box
  XSetForeground(XtDisplay(drawingArea), XtScreen(drawingArea)->default_gc,
                 boxColors[minDrawnLevel]);
  transBoundingBox.Draw(XtDisplay(drawingArea), drawable,
                        XtScreen(drawingArea)->default_gc);
  DrawSlices(drawable);
}


// -------------------------------------------------------------------
void ProjectionPicture::DrawSlices(const Drawable &drawable) {
  XSetForeground(XtDisplay(drawingArea), XtScreen(drawingArea)->default_gc,
                 sliceColor);
  for(int k = 0; k < 3; ++k) {
    transSlice[k].Draw(XtDisplay(drawingArea), drawable,
                       XtScreen(drawingArea)->default_gc);
  }
}    


// -------------------------------------------------------------------
void ProjectionPicture::LoadSlices(const Box &surroundingBox) {
  for(int j = 0; j < 3 ; ++j) {
    int slice(pltAppPtr->GetAmrPicturePtr(j)->GetSlice());
    realSlice[j] = RealSlice(j,slice,surroundingBox);
  }
}


// -------------------------------------------------------------------
void ProjectionPicture::ChangeSlice(int Dir, int newSlice) {
  for(int j = 0; j < 3; ++j) {
    if (Dir == j) {
      realSlice[j].ChangeSlice(newSlice);
    }
  }
}


// -------------------------------------------------------------------
XImage *ProjectionPicture::DrawBoxesIntoPixmap(int iFromLevel, int iToLevel) {
  XSetForeground(XtDisplay(drawingArea), XtScreen(drawingArea)->default_gc,
                 palettePtr->BlackIndex());

  XFillRectangle(XtDisplay(drawingArea),  pixMap,
		 XtScreen(drawingArea)->default_gc, 0, 0, daWidth, daHeight);
  DrawBoxesIntoDrawable(pixMap, iFromLevel, iToLevel);
  
  return XGetImage(XtDisplay(drawingArea), pixMap, 0, 0,
                   daWidth, daHeight, AllPlanes, ZPixmap);
}


// -------------------------------------------------------------------
void ProjectionPicture::DrawPicture() {
  XCopyArea(XtDisplay(drawingArea), pixMap, XtWindow(drawingArea),
            XtScreen(drawingArea)->default_gc, 0, 0, daWidth, daHeight, 0, 0);
}


// -------------------------------------------------------------------
void ProjectionPicture::LabelAxes() {
  int xHere(1);	// Where to label axes with X, Y, and Z
  int yHere(3);
  int zHere(4);
  minDrawnLevel = pltAppPtr->MinDrawnLevel();
  maxDrawnLevel = pltAppPtr->MaxDrawnLevel();
  XDrawString(XtDisplay(drawingArea), XtWindow(drawingArea),
              XtScreen(drawingArea)->default_gc,
              transBoundingBox.vertices[xHere].x,
              transBoundingBox.vertices[xHere].y,
              "X", 1);
  XDrawString(XtDisplay(drawingArea), XtWindow(drawingArea),
              XtScreen(drawingArea)->default_gc,
              transBoundingBox.vertices[yHere].x,
              transBoundingBox.vertices[yHere].y,
              "Y", 1);
  XDrawString(XtDisplay(drawingArea), XtWindow(drawingArea),
              XtScreen(drawingArea)->default_gc,
              transBoundingBox.vertices[zHere].x,
              transBoundingBox.vertices[zHere].y,
              "Z", 1);
  
}


// -------------------------------------------------------------------
void ProjectionPicture::ToggleShowSubCut() {
  showSubCut = (showSubCut ? false : true);
}


// -------------------------------------------------------------------
void ProjectionPicture::SetSubCut(const Box &newbox) {
  realSubCutBox = RealBox(newbox);
  MakeSubCutBox();
}


// -------------------------------------------------------------------
void ProjectionPicture::SetDrawingAreaDimensions(int w, int h) {
  
  minDrawnLevel = pltAppPtr->MinDrawnLevel();
  maxDrawnLevel = pltAppPtr->MaxDrawnLevel();
  
  daWidth = w;
  daHeight = h;
  if(imageData != NULL) {
    delete [] imageData;
  }
  imageData = new unsigned char[daWidth*daHeight];
  if(imageData == NULL) {
    cout << " SetDrawingAreaDimensions::imageData : new failed" << endl;
    ParallelDescriptor::Abort("Exiting.");
  }
  
  viewTransformPtr->SetScreenPosition(daWidth/2, daHeight/2);
  
  PPXImage = XCreateImage(XtDisplay(drawingArea),
                       XDefaultVisual(XtDisplay(drawingArea),
                                      DefaultScreen(XtDisplay(drawingArea))),
                       DefaultDepthOfScreen(XtScreen(drawingArea)), ZPixmap, 0,
                       (char *) imageData, daWidth, daHeight,
		XBitmapPad(XtDisplay(drawingArea)), daWidth);
  
  if(pixCreated) {
    XFreePixmap(XtDisplay(drawingArea), pixMap);
  }
  pixMap = XCreatePixmap(XtDisplay(drawingArea), XtWindow(drawingArea),
			daWidth, daHeight,
			DefaultDepthOfScreen(XtScreen(drawingArea)));
  pixCreated = true;

#ifdef BL_VOLUMERENDER
  // --- set the image buffer
  volRender->SetImage( (unsigned char *)imageData, daWidth, daHeight,
                       VP_LUMINANCE);
#endif

  longestWindowLength  = (Real) Max(daWidth, daHeight);
  shortestWindowLength = (Real) Min(daWidth, daHeight);
#ifdef BL_VOLUMERENDER
  volRender->SetAspect(shortestWindowLength/longestWindowLength);
#endif
  viewTransformPtr->SetAspect(shortestWindowLength/longestWindowLength);

  Box alignedBox(theDomain[minDrawnLevel]);
  const AmrData &amrData = pltAppPtr->GetDataServicesPtr()->AmrDataRef();
  alignedBox.refine(CRRBetweenLevels(minDrawnLevel, maxDataLevel,
		     amrData.RefRatio()));
  longestBoxSide = (Real) alignedBox.longside(longestBoxSideDir);
}


// -------------------------------------------------------------------
void ProjectionPicture::ReadTransferFile(const aString &rampFileName) {
#ifdef BL_VOLUMERENDER
  volRender->ReadTransferFile(rampFileName);
  pltAppPtr->GetPalettePtr()->SetTransfers(volRender->DensityRampX(),
					   volRender->DensityRampY());
#endif
}  // end ReadTransferFile
// -------------------------------------------------------------------
// -------------------------------------------------------------------


//--------------------------------------------------------------------
//----- BOX AND POINT STUFF-------------------------------------------
//--------------------------------------------------------------------

//--------------------------------------------------------------------
RealBox::RealBox() {
    RealPoint zero(0., 0., 0.);
    for (int i = 0; i < 8 ; i++)
        vertices[i] = zero;
}

//--------------------------------------------------------------------
RealBox::RealBox(RealPoint p1, RealPoint p2, RealPoint p3, RealPoint p4, 
                 RealPoint p5, RealPoint p6, RealPoint p7, RealPoint p8)
{
    vertices[0] = p1; vertices[1] = p2; vertices[2] = p3; vertices[3] = p4;
    vertices[4] = p5; vertices[5] = p6; vertices[6] = p7; vertices[7] = p8;
}


//--------------------------------------------------------------------
RealBox::RealBox(const Box& theBox) {
    Real dimensions[6] = { theBox.smallEnd(XDIR), theBox.smallEnd(YDIR),
                           theBox.smallEnd(ZDIR), theBox.bigEnd(XDIR)+1,
                           theBox.bigEnd(YDIR)+1, theBox.bigEnd(ZDIR)+1 };
    vertices[0] = RealPoint(dimensions[0], dimensions[1], dimensions[2]);
    vertices[1] = RealPoint(dimensions[3], dimensions[1], dimensions[2]);
    vertices[2] = RealPoint(dimensions[3], dimensions[4], dimensions[2]);
    vertices[3] = RealPoint(dimensions[0], dimensions[4], dimensions[2]);
    vertices[4] = RealPoint(dimensions[0], dimensions[1], dimensions[5]);
    vertices[5] = RealPoint(dimensions[3], dimensions[1], dimensions[5]);
    vertices[6] = RealPoint(dimensions[3], dimensions[4], dimensions[5]);
    vertices[7] = RealPoint(dimensions[0], dimensions[4], dimensions[5]);
}


//--------------------------------------------------------------------
TransBox::TransBox() {
    TransPoint zero(0,0);
    for(int i = 0; i < 8 ; ++i) {
      vertices[i] = zero;
    }
}


//--------------------------------------------------------------------
TransBox::TransBox(TransPoint p1, TransPoint p2, TransPoint p3, TransPoint p4, 
                   TransPoint p5, TransPoint p6, TransPoint p7, TransPoint p8)
{
    vertices[0] = p1; vertices[1] = p2; vertices[2] = p3; vertices[3] = p4;
    vertices[4] = p5; vertices[5] = p6; vertices[6] = p7; vertices[7] = p8;
}


//--------------------------------------------------------------------
void TransBox::Draw(Display *display, Window window, GC gc)
{
    for(int j = 0; j < 2; ++j) {
        for(int i = 0; i < 3; ++i){
            XDrawLine(display, window, gc,
                      vertices[j*4+i].x, vertices[j*4+i].y,
                      vertices[j*4+i+1].x, vertices[j*4+i+1].y);
        }
        XDrawLine(display, window, gc, vertices[j*4].x, vertices[j*4].y, 
                  vertices[j*4+3].x, vertices[j*4+3].y);
    }
    for(int k = 0; k < 4; ++k) {
        XDrawLine(display, window, gc,
                  vertices[k].x, vertices[k].y,
                  vertices[k+4].x, vertices[k+4].y);
    }
            
}


//--------------------------------------------------------------------
RealSlice::RealSlice() {
    RealPoint zero(0,0,0);
    for(int i = 0; i < 4; ++i) {
      edges[i] = zero;
    }
}


//--------------------------------------------------------------------
RealSlice::RealSlice(RealPoint p1, RealPoint p2, RealPoint p3, RealPoint p4) {
  edges[0] = p1;
  edges[1] = p2;
  edges[2] = p3;
  edges[3] = p4;
}


//--------------------------------------------------------------------
RealSlice::RealSlice(int count, int slice, const Box &worldBox) {
    Real dimensions[6] = { worldBox.smallEnd(XDIR), worldBox.smallEnd(YDIR),
                           worldBox.smallEnd(ZDIR), worldBox.bigEnd(XDIR)+1,
                           worldBox.bigEnd(YDIR)+1, worldBox.bigEnd(ZDIR)+1 };
    dirOfFreedom = count;
    if(dirOfFreedom == 0) {
        edges[0] = RealPoint(dimensions[0], dimensions[1], slice);
        edges[1] = RealPoint(dimensions[3], dimensions[1], slice);
        edges[2] = RealPoint(dimensions[3], dimensions[4], slice);
        edges[3] = RealPoint(dimensions[0], dimensions[4], slice);
    }
    if(dirOfFreedom == 1) {
        edges[0] = RealPoint(dimensions[0], slice, dimensions[2]);
        edges[1] = RealPoint(dimensions[3], slice, dimensions[2]);
        edges[2] = RealPoint(dimensions[3], slice, dimensions[5]);
        edges[3] = RealPoint(dimensions[0], slice, dimensions[5]);
    }
    if(dirOfFreedom == 2) {
        edges[0] = RealPoint(slice, dimensions[1], dimensions[2]);
        edges[1] = RealPoint(slice, dimensions[4], dimensions[2]);
        edges[2] = RealPoint(slice, dimensions[4], dimensions[5]);
        edges[3] = RealPoint(slice, dimensions[1], dimensions[5]);
    }
}


//--------------------------------------------------------------------
void RealSlice::ChangeSlice(int NewSlice) {
    for(int k = 0; k < 4; ++k) {
      edges[k].component[2-dirOfFreedom] = NewSlice;
    }
}


//--------------------------------------------------------------------
TransSlice::TransSlice() {
    TransPoint zero(0,0);
    for(int i = 0; i < 4; ++i) {
      edges[i] = zero;
    }
}


//--------------------------------------------------------------------
TransSlice::TransSlice(TransPoint p1, TransPoint p2, TransPoint p3, TransPoint p4) {
    edges[0] = p1;
    edges[1] = p2;
    edges[2] = p3;
    edges[3] = p4;
}


//--------------------------------------------------------------------
void TransSlice::Draw(Display *display, Window window, GC gc) {
    for(int j = 0; j < 3; ++j) {
        XDrawLine(display, window, gc,
                  edges[j].x, edges[j].y,
                  edges[j+1].x,edges[j+1].y);
    }
    XDrawLine(display, window, gc, edges[0].x, edges[0].y, 
              edges[3].x, edges[3].y);
}
//--------------------------------------------------------------------
//--------------------------------------------------------------------
