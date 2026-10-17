/*<html><pre>  -<a                             href="qh-geom.htm"
  >-------------------------------</a><a name="TOP">-</a>

   geom.c 
   geometric routines of qhull

   see qh-geom.htm and geom.h

   copyright (c) 1993-2002 The Geometry Center        

   infrequent code goes into geom2.c
*/
   
#include "qhull_a.h"
   
/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="distplane">-</a>
  
  qh_distplane( point, facet, dist )
    return distance from point to facet

  returns:
    dist
    if qh.RANDOMdist, joggles result
  
  notes:  
    dist > 0 if point is above facet (i.e., outside)
    does not error (for sortfacets)
    
  see:
    qh_distnorm in geom2.c
*/
void qh_distplane (pointT *point, facetT *facet, realT *dist) {
  coordT *normal= facet->normal, *coordp, randr;
  int k;
  
  switch(qh hull_dim){
  /* retail 0637fd83..0637fe24 sums the products from the highest index down to 0 (left-associative
     chain n[d-1]p[d-1] + n[d-2]p[d-2] + ... + n[0]p[0]) and adds the offset last (FADD [facet+0x10]) */
  case 2:
    *dist= point[1]*normal[1] + point[0]*normal[0] + facet->offset;
    break;
  case 3:
    *dist= point[2]*normal[2] + point[1]*normal[1] + point[0]*normal[0] + facet->offset;
    break;
  case 4:
    *dist= point[3]*normal[3] + point[2]*normal[2] + point[1]*normal[1] + point[0]*normal[0] + facet->offset;
    break;
  case 5:
    *dist= point[4]*normal[4] + point[3]*normal[3] + point[2]*normal[2] + point[1]*normal[1] + point[0]*normal[0] + facet->offset;
    break;
  case 6:
    *dist= point[5]*normal[5] + point[4]*normal[4] + point[3]*normal[3] + point[2]*normal[2] + point[1]*normal[1] + point[0]*normal[0] + facet->offset;
    break;
  case 7:
    *dist= point[6]*normal[6] + point[5]*normal[5] + point[4]*normal[4] + point[3]*normal[3] + point[2]*normal[2] + point[1]*normal[1] + point[0]*normal[0] + facet->offset;
    break;
  case 8:
    *dist= point[7]*normal[7] + point[6]*normal[6] + point[5]*normal[5] + point[4]*normal[4] + point[3]*normal[3] + point[2]*normal[2] + point[1]*normal[1] + point[0]*normal[0] + facet->offset;
    break;
  default:
    *dist= facet->offset;
    coordp= point;
    for (k= qh hull_dim; k--; )
      *dist += *coordp++ * *normal++;
    break;
  }
  zinc_(Zdistplane);
  if (!qh RANDOMdist && qh IStracing < 4)
    return;
  if (qh RANDOMdist) {
    randr= qh_RANDOMint;
    /* retail 0637fe89: FILD randr; FMUL .rdata double 063a5958 (= 2.0/qh_RANDOMmax); FSUB 1.0 -- a multiply by the folded constant, not 2.0*randr/RANDOMmax */
    /* retail 0637fe99/0637fe9f: FMUL MAXabs_coord (0658b6a8) first, then FMUL RANDOMfactor (0658b1b0) */
    *dist += (randr * (2.0 / qh_RANDOMmax) - 1.0) *
      qh MAXabs_coord * qh RANDOMfactor;
  }
  if (qh IStracing >= 4) {
    FUN_06356960 (/* retail 0637feb8 */ "qh_distplane: ");
    FUN_06356960 (/* retail 0637fecc */ qh_REAL_1, *dist);
    FUN_06356960 (/* retail 0637fee7 */ "from p%d to f%d\n", qh_pointid(point), facet->id);
  }
  return;
} /* distplane */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="findbest">-</a>

  qh_findbest( point, startfacet, bestoutside, isnewfacets, noupper, dist, isoutside, numpart )
    retail 0637ff10 ("ipion 99/07/10", the qhull 2.6 algorithm; see docs in the tracker)
    find facet that is furthest below a point

  mode flags derived from the arguments (retail 0637ff1b..0637ffbf):
    ischeckmax:  bestoutside && !isnewfacets && !isoutside   (qh_check_maxout)
    ispartition: isnewfacets && isoutside                    (qh_partitionpoint)
    isfindfacet: !isnewfacets && isoutside                   (qh_addpoint, qh_partitioncoplanar)
    testhorizon: ispartition && (bestoutside || qh APPROXhull || qh MERGING)

  notes:
    the search keeps qh.coplanarset as a stack of facets that were passed over
    (retail has no qh_findbesthorizon); numpart is only written for a non flipped startfacet
*/
facetT *qh_findbest (pointT *point, facetT *startfacet,
		     boolT bestoutside, boolT isnewfacets, boolT noupper,
		     realT *dist, boolT *isoutside, int *numpart) {
  realT bestdist= -REALmax/2;
  realT minsearch, searchdist, maxfloor= 0.0, maxcoplanar;
  facetT *facet, *neighbor, **neighborp, *bestfacet= NULL, *horizon;
  int oldtrace= qh IStracing, numcoplanar= 0;
  boolT ischeckmax, ispartition, isfindfacet, testhorizon, isjump;

  if (!bestoutside || isnewfacets || isoutside) {
    ischeckmax= False;
    ispartition= (isnewfacets && isoutside) ? True : False;
  }else {
    ischeckmax= True;
    ispartition= False;
  }
  isfindfacet= (!isnewfacets && isoutside) ? True : False;
  testhorizon= (ispartition && (bestoutside || qh APPROXhull || qh MERGING)) ? True : False;
  if (!ischeckmax && !ispartition && !isfindfacet) {
    FUN_06356960 (/* retail 0637ffc4 */ "qhull internal error (qh_findbest): unknown combination of arguments\n");
    qh_errexit (qh_ERRqhull, startfacet, NULL);
  }
  if (qh TRACElevel && qh TRACEpoint >= 0 && qh TRACEpoint == qh_pointid (point)) {
    qh IStracing= qh TRACElevel;
    FUN_06356960 (/* retail 06380029 */ "qh_findbest: point p%d starting at f%d bestoutside? %d newfacets %d\n",
	     qh TRACEpoint, startfacet->id, bestoutside, isnewfacets);
    fprintf (qh ferr, "  ischeckmax %d ispartition %d isfindfacet %d testhorizon %d\n",
	     ischeckmax, ispartition, isfindfacet, testhorizon);
    FUN_06356960 (/* retail 0638005e */ "  Last point added to hull was p%d.", qh furthest_id);
    fprintf (qh ferr, "  Last merge was #%d.\n", zzval_(Ztotmerge));
  }
  if (isoutside)
    *isoutside= True;
  if (!startfacet->flipped) {
    *numpart= 1;
    qh_distplane (point, startfacet, dist);
    if (!startfacet->upperdelaunay || (!noupper && qh MINoutside <= *dist)) {
      bestdist= *dist;
      bestfacet= startfacet;
      if (!bestoutside && qh MINoutside <= *dist) {
	qh IStracing= oldtrace;
	return startfacet;
      }
    }
    if (ischeckmax && (!qh ONLYgood || startfacet->good) && startfacet->maxoutside < *dist)
      startfacet->maxoutside= *dist;
  }
  maxcoplanar= qh MINvisible;
  maximize_(maxcoplanar, qh MAXcoplanar);
  if (!ispartition)
    searchdist= qh DISTround + qh DISTround + maxcoplanar + qh max_outside;
  else
    searchdist= qh DISTround + qh DISTround;
  minsearch= bestdist - searchdist;
  if (ischeckmax) {
    maxfloor= -(qh DISTround - maxcoplanar);
    if (maxfloor < minsearch)
      minsearch= maxfloor;
  }
  facet= startfacet;
  qh visit_id++;
  startfacet->visitid= qh visit_id;
  for (;;) {
  restart:
    isjump= False;
    if (qh IStracing > 3)
      fprintf (qh ferr, "qh_findbest: neighbors of f%d, bestdist %2.2g cutoff %2.2g searchdist %2.2g\n",
	       facet->id, bestdist, minsearch, searchdist);
    FOREACHneighbor_(facet) {
      if (ispartition && !neighbor->newfacet)
	continue;
      if (!neighbor->flipped) {
	if (neighbor->visitid == qh visit_id)
	  continue;
	neighbor->visitid= qh visit_id;
	(*numpart)++;
	qh_distplane (point, neighbor, dist);
	if (!bestoutside && qh MINoutside <= *dist) {
	  if (!noupper) {
	    qh IStracing= oldtrace;
	    return neighbor;
	  }
	  /* retail 063802d1: tests the upperdelaunay flag of the facet being expanded, not of the neighbor */
	  if (!facet->upperdelaunay) {
	    qh IStracing= oldtrace;
	    return neighbor;
	  }
	}
	if (!ischeckmax) {
	  if (bestfacet && *dist < minsearch)
	    continue;
	}else if ((qh ONLYgood && !neighbor->good) || *dist <= neighbor->maxoutside) {
	  if (bestfacet && *dist < minsearch)
	    continue;
	}else
	  neighbor->maxoutside= *dist;
	if (bestdist < *dist
	&& (!neighbor->upperdelaunay || (bestoutside && !noupper && qh MINoutside <= *dist))) {
	  bestfacet= neighbor;
	  if (!ischeckmax) {
	    if (searchdist + bestdist < *dist) {
	      bestdist= *dist;
	      minsearch= *dist - searchdist;
	      numcoplanar= 0;
	      facet= neighbor;
	      if (isjump) {
		qh visit_id++;
		neighbor->visitid= qh visit_id;
	      }
	      goto restart;
	    }
	    bestdist= *dist;
	    minsearch= *dist - searchdist;
	  }else {
	    bestdist= *dist;
	    minsearch= *dist - searchdist;
	    if (maxfloor < minsearch)
	      minsearch= maxfloor;
	  }
	  isjump= True;
	}
      }
      if (!numcoplanar) {
	SETfirst_(qh coplanarset)= neighbor;
	numcoplanar++;
	qh_settruncate (qh coplanarset, 1);
      }else {
	numcoplanar++;
	qh_setappend (&qh coplanarset, neighbor);
      }
    }
    if (!numcoplanar)
      break;
    facet= (facetT *)qh_setdellast (qh coplanarset);
    if (!facet)
      break;
  }
  if (!ischeckmax) {
    if (!bestfacet) {
      FUN_06356960 (/* retail 063804db */ "qh_findbest: point p%d starting at f%d bestoutside? %d newfacets %d\n",
	       qh TRACEpoint, startfacet->id, bestoutside, isnewfacets);
      fprintf (qh ferr, "\nqh_findbest: all neighbors of facet %d are flipped or upper Delaunay.\n\
Please report this error to qhull_bug@geom.umn.edu with the input and all of the output.\n",
	       startfacet->id);
      qh FORCEoutput= True;
      qh_errexit (qh_ERRqhull, startfacet, NULL);
    }
    if (ispartition && !qh findbest_notsharp && bestdist < -qh DISTround) {
      if (qh_sharpnewfacets (point, &bestfacet, &bestdist, numpart))
	qh findbestnew= True;
      else
	qh findbest_notsharp= True;
    }
    if (testhorizon) {
      horizon= SETfirst_(bestfacet->neighbors);
      if (qh IStracing > 3)
	fprintf (qh ferr, "qh_findbest: horizon facet f%d\n", horizon->id);
      (*numpart)++;
      qh_distplane (point, horizon, dist);
      if (bestdist < *dist
      && (!horizon->upperdelaunay || (!noupper && qh MINoutside <= *dist))) {
	bestdist= *dist;
	bestfacet= horizon;
      }
    }
  }
  *dist= bestdist;
  if (isoutside && bestdist < qh MINoutside)
    *isoutside= False;
  qh IStracing= oldtrace;
  return bestfacet;
}  /* findbest */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="findbestnew">-</a>

  qh_findbestnew( point, startfacet, dist, isoutside, numpart )
    retail 06380640 (five parameters; no bestoutside argument, qh BESToutside is read)
    find best newfacet for point
    searches all of qh.newfacet_list starting at startfacet, then the horizon
    neighbors (non-new facets) of the best (or best upperdelaunay) facet
  returns:
    best facet that is not upperdelaunay, unless isoutside and the upperdelaunay
    facet is clearly outside
    early out when dist >= cutoff
*/
facetT *qh_findbestnew (pointT *point, facetT *startfacet,
		   realT *dist, boolT *isoutside, int *numpart) {
  realT bestdist= -REALmax, bestdist_upper= -REALmax, cutoff;
  facetT *bestfacet= NULL, *bestfacet_upper= NULL, *facet, *neighbor, **neighborp;
  int oldtrace= qh IStracing, pass;

  if (!startfacet) {
    if (!qh MERGING)
      fprintf (qh ferr, "qhull internal error (qh_findbestnew): no new facets for point p%d\n",
	       qh furthest_id);
    else
      fprintf (qh ferr, "qhull precision error (qh_findbestnew): merging has formed and deleted an independent cycle of facets.  Can not continue.\n");
    qh_errexit (qh_ERRqhull, NULL, NULL);
  }
  if (!qh BESToutside && isoutside) {
    if (!qh MERGING)
      cutoff= qh MINoutside;
    else {
      cutoff= qh MINoutside * 4.0;
      if (cutoff < qh max_outside + qh max_outside)
	cutoff= qh max_outside + qh max_outside;
    }
  }else
    cutoff= REALmax;
  if (qh TRACElevel && qh TRACEpoint >= 0 && qh TRACEpoint == qh_pointid (point)) {
    qh IStracing= qh TRACElevel;
    fprintf (qh ferr, "qh_findbestnew: point p%d facet f%d. Stop if dist > %2.2g\n",
	     qh TRACEpoint, startfacet->id, cutoff);
    fprintf (qh ferr, "  Last point added to hull was p%d.", qh furthest_id);
    fprintf (qh ferr, "  Last merge was #%d.\n", zzval_(Ztotmerge));
  }
  if (isoutside)
    *isoutside= True;
  *numpart= 0;
  for (pass= 0, facet= startfacet; pass < 2; pass++, facet= qh newfacet_list) {
    for (; facet && facet->next && (facet != startfacet || !pass); facet= facet->next) {
      qh_distplane (point, facet, dist);
      (*numpart)++;
      if (!facet->upperdelaunay) {
	if (bestdist < *dist) {
	  bestdist= *dist;
	  bestfacet= facet;
	  if (cutoff <= *dist) {
	    qh IStracing= oldtrace;
	    return facet;
	  }
	}
      }else if (bestdist_upper < *dist) {
	bestdist_upper= *dist;
	bestfacet_upper= facet;
	if (cutoff <= *dist) {
	  qh IStracing= oldtrace;
	  return facet;
	}
      }
    }
  }
  facet= bestfacet ? bestfacet : bestfacet_upper;
  FOREACHneighbor_(facet) {
    if (!neighbor->newfacet) {
      qh_distplane (point, neighbor, dist);
      (*numpart)++;
      if (!neighbor->upperdelaunay) {
	if (bestdist < *dist) {
	  bestdist= *dist;
	  bestfacet= neighbor;
	}
      }else if (bestdist_upper < *dist) {
	bestdist_upper= *dist;
	bestfacet_upper= neighbor;
      }
    }
  }
  if (!bestfacet
  || (isoutside && qh MINoutside <= bestdist_upper && bestdist < bestdist_upper)) {
    *dist= bestdist_upper;
    bestfacet= bestfacet_upper;
  }else
    *dist= bestdist;
  if (isoutside && *dist < qh MINoutside)
    *isoutside= False;
  qh IStracing= oldtrace;
  return bestfacet;
}  /* findbestnew */



/* ============ hyperplane functions -- keep code together [?] ============ */

/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="backnormal">-</a>
  
  qh_backnormal( rows, numrow, numcol, sign, normal, nearzero )
    given an upper-triangular rows array and a sign,
    solve for normal equation x using back substitution over rows U

  returns:
     normal= x
      
     if will not be able to divzero() when normalized (qh.MINdenom_2 and qh.MINdenom_1_2),
       if fails on last row
         this means that the hyperplane intersects [0,..,1]
         sets last coordinate of normal to sign
       otherwise
         sets tail of normal to [...,sign,0,...], i.e., solves for b= [0...0]
         sets nearzero

  notes:
     assumes numrow == numcol-1

     see Golub & van Loan 4.4-9 for back substitution

     solves Ux=b where Ax=b and PA=LU
     b= [0,...,0,sign or 0]  (sign is either -1 or +1)
     last row of A= [0,...,0,1]

     1) Ly=Pb == y=b since P only permutes the 0's of   b
     
  design:
    for each row from end
      perform back substitution
      if near zero
        use qh_divzero for division
        if zero divide and not last row
          set tail of normal to 0
*/
void qh_backnormal (realT **rows, int numrow, int numcol, boolT sign,
  	coordT *normal, boolT *nearzero) {
  int i, j;
  coordT *normalp, *normal_tail, *ai, *ak;
  realT diagonal;
  boolT waszero;
  int zerocol= -1;
  
  normalp= normal + numcol - 1;
  *normalp--= (sign ? -1.0 : 1.0);
  for(i= numrow; i--; ) {
    *normalp= 0.0;
    ai= rows[i] + i + 1;
    ak= normalp+1;
    for(j= i+1; j < numcol; j++)
      *normalp -= *ai++ * *ak++;
    diagonal= (rows[i])[i];
    if (fabs_(diagonal) > qh MINdenom_2)
      *(normalp--) /= diagonal;
    else {
      waszero= False;
      *normalp= qh_divzero (*normalp, diagonal, qh MINdenom_1_2, &waszero);
      if (waszero) {
        zerocol= i;
	*(normalp--)= (sign ? -1.0 : 1.0);
	for (normal_tail= normalp+2; normal_tail < normal + numcol; normal_tail++)
	  *normal_tail= 0.0;
      }else
	normalp--;
    }
  }
  if (zerocol != -1) {
    zzinc_(Zback0);
    *nearzero= True;
    trace4((qh ferr, "qh_backnormal: zero diagonal at column %d.\n", i));
    qh_precision ("zero diagonal on back substitution");
  }
} /* backnormal */

/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="gausselim">-</a>
  
  qh_gausselim( rows, numrow, numcol, sign )
    Gaussian elimination with partial pivoting

  returns:
    rows is upper triangular (includes row exchanges)
    flips sign for each row exchange
    sets nearzero if pivot[k] < qh.NEARzero[k], else clears it

  notes:
    if nearzero, the determinant's sign may be incorrect.
    assumes numrow <= numcol

  design:
    for each row
      determine pivot and exchange rows if necessary
      test for near zero
      perform gaussian elimination step
*/
void qh_gausselim(realT **rows, int numrow, int numcol, boolT *sign, boolT *nearzero) {
  realT *ai, *ak, *rowp, *pivotrow;
  realT n, pivot, pivot_abs= 0.0, temp;
  int i, j, k, pivoti, flip=0;
  
  *nearzero= False;
  for(k= 0; k < numrow; k++) {
    pivot_abs= fabs_((rows[k])[k]);
    pivoti= k;
    for(i= k+1; i < numrow; i++) {
      if ((temp= fabs_((rows[i])[k])) > pivot_abs) {
	pivot_abs= temp;
	pivoti= i;
      }
    }
    if (pivoti != k) {
      rowp= rows[pivoti]; 
      rows[pivoti]= rows[k]; 
      rows[k]= rowp; 
      *sign ^= 1;
      flip ^= 1;
    }
    if (pivot_abs <= qh NEARzero[k]) {
      *nearzero= True;
      if (pivot_abs == 0.0) {   /* remainder of column == 0 */
	if (qh IStracing >= 4) {
	  FUN_06356960 (/* retail 06380b1e */ "qh_gausselim: 0 pivot at column %d. (%2.2g < %2.2g)\n", k, pivot_abs, qh DISTround);
	  qh_printmatrix (qh ferr, "Matrix:", rows, numrow, numcol);
	}
	zzinc_(Zgauss0);
        qh_precision ("zero pivot for Gaussian elimination");
	goto LABELnextcol;
      }
    }
    pivotrow= rows[k] + k;
    pivot= *pivotrow++;  /* signed value of pivot, and remainder of row */
    for(i= k+1; i < numrow; i++) {
      ai= rows[i] + k;
      ak= pivotrow;
      n= (*ai++)/pivot;   /* divzero() not needed since |pivot| >= |*ai| */
      for(j= numcol - (k+1); j--; )
	*ai++ -= n * *ak++;
    }
  LABELnextcol:
    ;
  }
  wmin_(Wmindenom, pivot_abs);  /* last pivot element */
  if (qh IStracing >= 5)
    qh_printmatrix (qh ferr, "qh_gausselem: result", rows, numrow, numcol);
} /* gausselim */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="getangle">-</a>
  
  qh_getangle( vect1, vect2 )
    returns the dot product of two vectors
    if qh.RANDOMdist, joggles result

  notes:
    the angle may be > 1.0 or < -1.0 because of roundoff errors

*/
realT qh_getangle(pointT *vect1, pointT *vect2) {
  realT angle= 0, randr;
  int k;

  for(k= qh hull_dim; k--; )
    angle += *vect1++ * *vect2++;
  if (qh RANDOMdist) {
    randr= qh_RANDOMint;
    /* retail 06380ca0: FILD randr; FMUL .rdata double 063a5958 (= 2.0/qh_RANDOMmax); FSUB 1.0 */
    angle += (randr * (2.0 / qh_RANDOMmax) - 1.0) *
      qh RANDOMfactor;
  }
  trace4((qh ferr, "qh_getangle: %2.2g\n", angle));
  return(angle);
} /* getangle */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="getcenter">-</a>
  
  qh_getcenter( vertices )
    returns arithmetic center of a set of vertices as a new point

  notes:
    allocates point array for center
*/
pointT *qh_getcenter(setT *vertices) {
  int k;
  pointT *center, *coord;
  vertexT *vertex, **vertexp;
  int count= qh_setsize(vertices);

  if (count < 2) {
    FUN_06356960 (/* retail 06380d0e */ "qhull internal error (qh_getcenter): not defined for %d points\n", count);
    qh_errexit (qh_ERRqhull, NULL, NULL);
  }
  center= (pointT *)qh_memalloc(qh normal_size);
  for (k=0; k < qh hull_dim; k++) {
    coord= center+k;
    *coord= 0.0;
    FOREACHvertex_(vertices)
      *coord += vertex->point[k];
    *coord /= count;
  }
  return(center);
} /* getcenter */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="getcentrum">-</a>
  
  qh_getcentrum( facet )
    returns the centrum for a facet as a new point

  notes:
    allocates the centrum
*/
pointT *qh_getcentrum(facetT *facet) {
  realT dist;
  pointT *centrum, *point;

  point= qh_getcenter(facet->vertices);
  zzinc_(Zcentrumtests);
  qh_distplane (point, facet, &dist);
  centrum= qh_projectpoint(point, facet, dist);
  qh_memfree(point, qh normal_size);
  trace4((qh ferr, "qh_getcentrum: for f%d, %d vertices dist= %2.2g\n",
	  facet->id, qh_setsize(facet->vertices), dist));
  return centrum;
} /* getcentrum */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="getdistance">-</a>
  
  qh_getdistance( facet, neighbor, mindist, maxdist )
    returns the maxdist and mindist distance of any vertex from neighbor

  returns:
    the max absolute value

  design:
    for each vertex of facet that is not in neighbor
      test the distance from vertex to neighbor
*/
realT qh_getdistance(facetT *facet, facetT *neighbor, realT *mindist, realT *maxdist) {
  vertexT *vertex, **vertexp;
  realT dist, maxd, mind;
  
  FOREACHvertex_(facet->vertices)
    vertex->seen= False;
  FOREACHvertex_(neighbor->vertices)
    vertex->seen= True;
  mind= 0.0;
  maxd= 0.0;
  FOREACHvertex_(facet->vertices) {
    if (!vertex->seen) {
      zzinc_(Zbestdist);
      qh_distplane(vertex->point, neighbor, &dist);
      if (dist < mind)
	mind= dist;
      else if (dist > maxd)
	maxd= dist;
    }
  }
  *mindist= mind;
  *maxdist= maxd;
  mind= -mind;
  if (maxd > mind)
    return maxd;
  else
    return mind;
} /* getdistance */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="normalize">-</a>

  qh_normalize( normal, dim, toporient )
    normalize a vector and report if too small
    does not use min norm
  
  see:
    qh_normalize2
*/
void qh_normalize (coordT *normal, int dim, boolT toporient) {
  qh_normalize2( normal, dim, toporient, NULL, NULL);
} /* normalize */

/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="normalize2">-</a>
  
  qh_normalize2( normal, dim, toporient, minnorm, ismin )
    normalize a vector and report if too small
    qh.MINdenom/MINdenom1 are the upper limits for divide overflow

  returns:
    normalized vector
    flips sign if !toporient
    if minnorm non-NULL, 
      sets ismin if normal < minnorm

  notes:
    if zero norm
       sets all elements to sqrt(1.0/dim)
    if divide by zero (divzero ())
       sets largest element to   +/-1
       bumps Znearlysingular
      
  design:
    computes norm
    test for minnorm
    if not near zero
      normalizes normal
    else if zero norm
      sets normal to standard value
    else
      uses qh_divzero to normalize
      if nearzero
        sets norm to direction of maximum value
*/
void qh_normalize2 (coordT *normal, int dim, boolT toporient, 
            realT *minnorm, boolT *ismin) {
  int k;
  realT *colp, *maxp, norm= 0, temp, *norm1, *norm2, *norm3;
  boolT zerodiv;

  norm1= normal+1;
  norm2= normal+2;
  norm3= normal+3;
  if (dim == 2)
    norm= sqrt((*normal)*(*normal) + (*norm1)*(*norm1));
  else if (dim == 3)
    /* retail 06380fa8-06380fc0 accumulates the squares from the last component down */
    norm= sqrt(((*norm2)*(*norm2) + (*norm1)*(*norm1)) + (*normal)*(*normal));
  else if (dim == 4) {
    /* retail 06380fd8-06380ff9 */
    norm= sqrt((((*norm3)*(*norm3) + (*norm2)*(*norm2)) + (*norm1)*(*norm1))
               + (*normal)*(*normal));
  }else if (dim > 4) {
    /* retail 06381010-06381039: same four-term chain, then ascending += colp^2 */
    norm= (((*norm3)*(*norm3) + (*norm2)*(*norm2)) + (*norm1)*(*norm1))
               + (*normal)*(*normal);
    for (k= dim-4, colp= normal+4; k--; colp++)
      norm += (*colp) * (*colp);
    norm= sqrt(norm);
  }
  if (minnorm) {
    if (norm < *minnorm) 
      *ismin= True;
    else
      *ismin= False;
  }
  wmin_(Wmindenom, norm);
  if (norm > qh MINdenom) {
    if (!toporient)
      norm= -norm;
    *normal /= norm;
    *norm1 /= norm;
    if (dim == 2)
      ; /* all done */
    else if (dim == 3)
      *norm2 /= norm;
    else if (dim == 4) {
      *norm2 /= norm;
      *norm3 /= norm;
    }else if (dim >4) {
      *norm2 /= norm;
      *norm3 /= norm;
      for (k= dim-4, colp= normal+4; k--; )
        *colp++ /= norm;
    }
  }else if (norm == 0.0) {
    temp= sqrt (1.0/dim);
    for (k= dim, colp= normal; k--; )
      *colp++ = temp;
  }else {
    if (!toporient)
      norm= -norm;
    for (k= dim, colp= normal; k--; colp++) { /* k used below */
      temp= qh_divzero (*colp, norm, qh MINdenom_1, &zerodiv);
      if (!zerodiv)
	*colp= temp;
      else {
	maxp= qh_maxabsval(normal, dim);
	temp= ((*maxp * norm >= 0.0) ? 1.0 : -1.0);
	for (k= dim, colp= normal; k--; colp++)
	  *colp= 0.0;
	*maxp= temp;
	zzinc_(Znearlysingular);
	trace0((qh ferr, "qh_normalize: norm=%2.2g too small during p%d\n", 
	       norm, qh furthest_id));
	return;
      }
    }
  }
} /* normalize */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="projectpoint">-</a>
  
  qh_projectpoint( point, facet, dist )
    project point onto a facet by dist

  returns:
    returns a new point
    
  notes:
    if dist= distplane(point,facet)
      this projects point to hyperplane
    assumes qh_memfree_() is valid for normal_size
*/
pointT *qh_projectpoint(pointT *point, facetT *facet, realT dist) {
  pointT *newpoint, *np, *normal;
  int normsize= qh normal_size,k;
  void **freelistp; /* used !qh_NOmem */
  
  qh_memalloc_(normsize, freelistp, newpoint, pointT);
  np= newpoint;
  normal= facet->normal;
  for(k= qh hull_dim; k--; )
    *(np++)= *point++ - dist * *normal++;
  return(newpoint);
} /* projectpoint */

  
/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="setfacetplane">-</a>
  
  qh_setfacetplane( facet )
    sets the hyperplane for a facet
    if qh.RANDOMdist, joggles hyperplane

  notes:
    uses global buffers qh.gm_matrix and qh.gm_row
    overwrites facet->normal if already defined
    updates Wnewvertex if PRINTstatistics
    sets facet->upperdelaunay if upper envelope of Delaunay triangulation

  design:
    copy vertex coordinates to qh.gm_matrix/gm_row
    compute determinate
    if nearzero
      recompute determinate with gaussian elimination
      if nearzero
        force outside orientation by testing interior point
*/
void qh_setfacetplane(facetT *facet) {
  pointT *point;
  vertexT *vertex, **vertexp;
  int k,i, normsize= qh normal_size, oldtrace= 0;
  realT dist;
  void **freelistp; /* used !qh_NOmem */
  coordT *coord, *gmcoord;
  pointT *point0= SETfirstt_(facet->vertices, vertexT)->point;
  boolT nearzero= False;

  zzinc_(Zsetplane);
  if (!facet->normal)
    qh_memalloc_(normsize, freelistp, facet->normal, coordT);
  if (facet == qh tracefacet) {
    oldtrace= qh IStracing;
    qh IStracing= 5;
    FUN_06356960 (/* retail 06381376 */ "qh_setfacetplane: facet f%d created.\n", facet->id);
    FUN_06356960 (/* retail 06381389 */ "  Last point added to hull was p%d.", qh furthest_id);
    if (zzval_(Ztotmerge))
      fprintf(qh ferr, "  Last merge was #%d.", zzval_(Ztotmerge)); /* retail 063813a7 keeps fprintf */
    FUN_06356960 (/* retail 063813b5 */ "\n\nCurrent summary is:\n");
      qh_printsummary (qh ferr);
  }
  if (qh hull_dim <= 4) {
    i= 0;
    if (qh RANDOMdist) {
      gmcoord= qh gm_matrix;
      FOREACHvertex_(facet->vertices) {
        qh gm_row[i++]= gmcoord;
	coord= vertex->point;
	for (k= qh hull_dim; k--; )
	  *(gmcoord++)= *coord++ * qh_randomfactor();
      }	  
    }else {
      FOREACHvertex_(facet->vertices)
       qh gm_row[i++]= vertex->point;
    }
    qh_sethyperplane_det(qh hull_dim, qh gm_row, point0, facet->toporient,
                facet->normal, &facet->offset, &nearzero);
  }
  if (qh hull_dim > 4 || nearzero) {
    i= 0;
    gmcoord= qh gm_matrix;
    FOREACHvertex_(facet->vertices) {
      if (vertex->point != point0) {
	qh gm_row[i++]= gmcoord;
	coord= vertex->point;
	point= point0;
	for(k= qh hull_dim; k--; )
	  *(gmcoord++)= *coord++ - *point++;
      }
    }
    qh gm_row[i]= gmcoord;  /* for areasimplex */
    if (qh RANDOMdist) {
      gmcoord= qh gm_matrix;
      for (i= qh hull_dim-1; i--; ) {
	for (k= qh hull_dim; k--; )
	  *(gmcoord++) *= qh_randomfactor();
      }
    }
    qh_sethyperplane_gauss(qh hull_dim, qh gm_row, point0, facet->toporient,
           	facet->normal, &facet->offset, &nearzero);
    if (nearzero) { 
      if (qh_orientoutside (facet)) {
	trace0((qh ferr, "qh_setfacetplane: flipped orientation after testing interior_point during p%d\n", qh furthest_id));
      /* this is part of using Gaussian Elimination.  For example in 5-d
	   1 1 1 1 0
	   1 1 1 1 1
	   0 0 0 1 0
	   0 1 0 0 0
	   1 0 0 0 0
	   norm= 0.38 0.38 -0.76 0.38 0
	 has a determinate of 1, but g.e. after subtracting pt. 0 has
	 0's in the diagonal, even with full pivoting.  It does work
	 if you subtract pt. 4 instead. */
      }
    }
  }
  facet->upperdelaunay= False;
  if (qh DELAUNAY) {
    if (qh UPPERdelaunay) {     /* matches qh.lower_threshold in qh_initbuild */
      if (facet->normal[qh hull_dim -1] >= qh ANGLEround * qh_ZEROdelaunay)
        facet->upperdelaunay= True;
    }else {
      if (facet->normal[qh hull_dim -1] > -qh ANGLEround * qh_ZEROdelaunay)
        facet->upperdelaunay= True;
    }
  }
  if (qh PRINTstatistics || qh IStracing || qh TRACElevel || qh JOGGLEmax < REALmax) {
    qh old_randomdist= qh RANDOMdist;
    qh RANDOMdist= False;
    FOREACHvertex_(facet->vertices) {
      if (vertex->point != point0) {
	boolT istrace= False;
	zinc_(Zdiststat);
        qh_distplane(vertex->point, facet, &dist);
        dist= fabs_(dist);
        zinc_(Znewvertex);
        wadd_(Wnewvertex, dist);
        if (dist > wwval_(Wnewvertexmax)) {
          wwval_(Wnewvertexmax)= dist;
	  if (dist > qh max_outside) {
	    qh max_outside= dist;  /* used by qh_maxouter() */
	    if (dist > qh TRACEdist) 
	      istrace= True;
	  }
	}else if (-dist > qh TRACEdist)
	  istrace= True;
	if (istrace) {
	  FUN_06356960 (/* retail 063817c9 */ "qh_setfacetplane: ====== vertex p%d (v%d) increases max_outside to %2.2g for new facet f%d last p%d\n",
	        qh_pointid(vertex->point), vertex->id, dist, facet->id, qh furthest_id);
	  qh_errprint ("DISTANT", facet, NULL, NULL, NULL);
	}
      }
    }
    qh RANDOMdist= qh old_randomdist;
  }
  if (qh IStracing >= 3) {
    FUN_06356960 (/* retail 06381813 */ "qh_setfacetplane: f%d offset %2.2g normal: ",
	     facet->id, facet->offset);
    for (k=0; k < qh hull_dim; k++)
      FUN_06356960 (/* retail 06381837 */ "%2.2g ", facet->normal[k]);
    FUN_06356960 (/* retail 0638184e */ "\n");
  }
  if (facet == qh tracefacet)
    qh IStracing= oldtrace;
} /* setfacetplane */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="sethyperplane_det">-</a>
  
  qh_sethyperplane_det( dim, rows, point0, toporient, normal, offset, nearzero )
    given dim X dim array indexed by rows[], one row per point, 
        toporient (flips all signs),
        and point0 (any row)
    set normalized hyperplane equation from oriented simplex

  returns:
    normal (normalized)
    offset (places point0 on the hyperplane)
    sets nearzero if hyperplane not through points

  notes:
    only defined for dim == 2..4
    rows[] is not modified
    solves det(P-V_0, V_n-V_0, ..., V_1-V_0)=0, i.e. every point is on hyperplane
    see Bower & Woodworth, A programmer's geometry, Butterworths 1983.

  derivation of 3-d minnorm
    Goal: all vertices V_i within qh.one_merge of hyperplane
    Plan: exactly translate the facet so that V_0 is the origin
          exactly rotate the facet so that V_1 is on the x-axis and y_2=0.
          exactly rotate the effective perturbation to only effect n_0
	     this introduces a factor of sqrt(3)
    n_0 = ((y_2-y_0)*(z_1-z_0) - (z_2-z_0)*(y_1-y_0)) / norm
    Let M_d be the max coordinate difference
    Let M_a be the greater of M_d and the max abs. coordinate
    Let u be machine roundoff and distround be max error for distance computation
    The max error for n_0 is sqrt(3) u M_a M_d / norm.  n_1 is approx. 1 and n_2 is approx. 0
    The max error for distance of V_1 is sqrt(3) u M_a M_d M_d / norm.  Offset=0 at origin
    Then minnorm = 1.8 u M_a M_d M_d / qh.ONEmerge
    Note that qh.one_merge is approx. 45.5 u M_a and norm is usually about M_d M_d

  derivation of 4-d minnorm
    same as above except rotate the facet so that V_1 on x-axis and w_2, y_3, w_3=0
     [if two vertices fixed on x-axis, can rotate the other two in yzw.]
    n_0 = det3_(...) = y_2 det2_(z_1, w_1, z_3, w_3) = - y_2 w_1 z_3
     [all other terms contain at least two factors nearly zero.]
    The max error for n_0 is sqrt(4) u M_a M_d M_d / norm
    Then minnorm = 2 u M_a M_d M_d M_d / qh.ONEmerge
    Note that qh.one_merge is approx. 82 u M_a and norm is usually about M_d M_d M_d
*/
void qh_sethyperplane_det (int dim, coordT **rows, coordT *point0, 
          boolT toporient, coordT *normal, realT *offset, boolT *nearzero) {
  realT maxround, dist;
  int i;
  pointT *point;


  if (dim == 2) {
    normal[0]= dY(1,0);
    normal[1]= dX(0,1);
    qh_normalize2 (normal, dim, toporient, NULL, NULL);
    *offset= -(point0[0]*normal[0]+point0[1]*normal[1]);
    *nearzero= False;  /* since nearzero norm => incident points */
  }else if (dim == 3) {
    normal[0]= det2_(dY(2,0), dZ(2,0),
		     dY(1,0), dZ(1,0));
    normal[1]= det2_(dX(1,0), dZ(1,0),
		     dX(2,0), dZ(2,0));
    normal[2]= det2_(dX(2,0), dY(2,0),
		     dX(1,0), dY(1,0));
    qh_normalize2 (normal, dim, toporient, NULL, NULL);
    /* retail 06381976-06381996 sums the products from the last component down */
    *offset= -((point0[2]*normal[2] + point0[1]*normal[1])
	       + point0[0]*normal[0]);
    maxround= qh DISTround;
    for (i=dim; i--; ) {
      point= rows[i];
      if (point != point0) {
        /* retail 063819a8-063819bc: ((p2n2 + p1n1) + p0n0) + offset */
        dist= ((point[2]*normal[2] + point[1]*normal[1])
	       + point[0]*normal[0]) + *offset;
        if (dist > maxround || dist < -maxround) {
  	  *nearzero= True;
	  break;
	}
      }
    }
  }else if (dim == 4) {
    normal[0]= - det3_(dY(2,0), dZ(2,0), dW(2,0),
			dY(1,0), dZ(1,0), dW(1,0),
			dY(3,0), dZ(3,0), dW(3,0));
    normal[1]=   det3_(dX(2,0), dZ(2,0), dW(2,0),
		        dX(1,0), dZ(1,0), dW(1,0),
		        dX(3,0), dZ(3,0), dW(3,0));
    normal[2]= - det3_(dX(2,0), dY(2,0), dW(2,0),
			dX(1,0), dY(1,0), dW(1,0),
			dX(3,0), dY(3,0), dW(3,0));
    normal[3]=   det3_(dX(2,0), dY(2,0), dZ(2,0),
		        dX(1,0), dY(1,0), dZ(1,0),
		        dX(3,0), dY(3,0), dZ(3,0));
    qh_normalize2 (normal, dim, toporient, NULL, NULL);
    /* retail 06381c03-06381c26 */
    *offset= -(((point0[3]*normal[3] + point0[2]*normal[2])
	       + point0[1]*normal[1]) + point0[0]*normal[0]);
    maxround= qh DISTround;
    for (i=dim; i--; ) {
      point= rows[i];
      if (point != point0) {
        /* retail 06381c3d-06381c59 */
        dist= (((point[3]*normal[3] + point[2]*normal[2])
	       + point[1]*normal[1]) + point[0]*normal[0]) + *offset;
        if (dist > maxround || dist < -maxround) {
  	  *nearzero= True;
	  break;
	}
      }
    }
  }
  if (*nearzero) {
    zzinc_(Zminnorm);
    trace0((qh ferr, "qh_sethyperplane_det: degenerate norm during p%d.\n", qh furthest_id));
    zzinc_(Znearlysingular);
  }
} /* sethyperplane_det */


/*-<a                             href="qh-geom.htm#TOC"
  >-------------------------------</a><a name="sethyperplane_gauss">-</a>
  
  qh_sethyperplane_gauss( dim, rows, point0, toporient, normal, offset, nearzero )
    given (dim-1) X dim array of rows[i]= V_{i+1} - V_0 (point0)
    set normalized hyperplane equation from oriented simplex

  returns:
    normal (normalized)
    offset (places point0 on the hyperplane)

  notes:
    if nearzero
      orientation may be incorrect because of incorrect sign flips in gausselim
    solves [V_n-V_0,...,V_1-V_0, 0 .. 0 1] * N == [0 .. 0 1] 
        or [V_n-V_0,...,V_1-V_0, 0 .. 0 1] * N == [0] 
    i.e., N is normal to the hyperplane, and the unnormalized
        distance to [0 .. 1] is either 1 or   0

  design:
    perform gaussian elimination
    flip sign for negative values
    perform back substitution 
    normalize result
    compute offset
*/
void qh_sethyperplane_gauss (int dim, coordT **rows, pointT *point0, 
		boolT toporient, coordT *normal, coordT *offset, boolT *nearzero) {
  coordT *pointcoord, *normalcoef;
  int k;
  boolT sign= toporient, nearzero2= False;
  
  qh_gausselim(rows, dim-1, dim, &sign, nearzero);
  for(k= dim-1; k--; ) {
    if ((rows[k])[k] < 0)
      sign ^= 1;
  }
  if (*nearzero) {
    zzinc_(Znearlysingular);
    trace0((qh ferr, "qh_sethyperplane_gauss: nearly singular or axis parallel hyperplane during p%d.\n", qh furthest_id));
    qh_backnormal(rows, dim-1, dim, sign, normal, &nearzero2);
  }else {
    qh_backnormal(rows, dim-1, dim, sign, normal, &nearzero2);
    if (nearzero2) {
      zzinc_(Znearlysingular);
      trace0((qh ferr, "qh_sethyperplane_gauss: singular or axis parallel hyperplane at normalization during p%d.\n", qh furthest_id));
    }
  }
  if (nearzero2)
    *nearzero= True;
  qh_normalize2(normal, dim, True, NULL, NULL);
  pointcoord= point0;
  normalcoef= normal;
  *offset= -(*pointcoord++ * *normalcoef++);
  for(k= dim-1; k--; )
    *offset -= *pointcoord++ * *normalcoef++;
} /* sethyperplane_gauss */

  

