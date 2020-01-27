#include "data.table.h"
#include <Rdefines.h>
#include <ctype.h>   // for isdigit

SEXP asmatrix(SEXP dt, SEXP rownames)
{
  // Determine rows and colums
  int ncol = length(dt);
  int nrow = length(VECTOR_ELT(dt, 0));
  
  // Extract column types and determine type to coerce to
  int maxType=RAWSXP;
  bool integer64=false; // are we coercing to integer64?
  for (int j=0; j<ncol; ++j) {
    // Extract column type. 
    SEXP thisCol = VECTOR_ELT(dt, j);
    int thisType = TYPEOF(thisCol);

    // Determine the maximum type now that we have inspected this column
    if (maxType==VECSXP) 
      continue; // nothing to do, max type is already list
    // If integer64 defer coercion til after we know maxType of other columns
    else if (INHERITS(thisCol, char_integer64))
      integer64=true;
    // non-atomic non-list types are coerced / wrapped in list, see #4196
    else if (TYPEORDER(thisType)>TYPEORDER(VECSXP)) 
      maxType=VECSXP;
    // otherwise if this column is higher in typeorder list, set this type as maxType
    else if (TYPEORDER(thisType)>TYPEORDER(maxType)) 
      maxType=thisType;
  }
  
  // Determine type to coerce to based on presence of integer64 columns and maxType.
  if (integer64) {
    if (TYPEORDER(maxType)<TYPEORDER(REALSXP))
      // raw, logical, and integer coerced to integer64
      maxType = REALSXP; // integer64 is REALSXP with class integer64
    else if (TYPEORDER(maxType)<TYPEORDER(STRSXP))
      // if numeric or complex, all need to be coerced to STRSXP
      maxType = STRSXP;
    // else maxType is VECSXP, so no coercion needed.
  }
  
  // allocate matrix
  int nprotect=0;
  SEXP ans = PROTECT(allocMatrix(maxType, nrow, ncol)); nprotect++;
  
  // Add dimnames
  SEXP dimnames = PROTECT(allocVector(VECSXP, 2)); nprotect++;
  SET_VECTOR_ELT(dimnames, 0, rownames);
  SET_VECTOR_ELT(dimnames, 1, getAttrib(dt, R_NamesSymbol));
  setAttrib(ans, R_DimNamesSymbol, dimnames);
  
  // Add integer64 class to existing classes ("matrix" in R < 4.0.0 or c("matrix", "array") in R>=4.0.0)
  if (integer64 && maxType == REALSXP) {
    SEXP matClass = getAttrib(ans, R_ClassSymbol);
    SEXP newClass = PROTECT(allocVector(STRSXP, length(matClass)+1)); nprotect++;
    SET_STRING_ELT(newClass, 0, char_integer64);
    for (int i=0; i<length(matClass); ++i) {
      SET_STRING_ELT(newClass, i+1, STRING_ELT(matClass, i));
    }
    setAttrib(ans, R_ClassSymbol, newClass);
  }

  // If any nrow 0 we can now return. ncol == 0 handled in R.
  if (nrow == 0) {
    UNPROTECT(nprotect);
    return(ans);
  }
  
  // Coerce columns (if needed) and fill
  SEXP coerced;
  int ansloc=0; // position in vector to start copying to, filling by column.
  for (int j=0; j<ncol; ++j) {
    SEXP thisCol = VECTOR_ELT(dt, j);
    if (maxType == VECSXP && TYPEOF(thisCol) != VECSXP) { // coercion to list not handled by memrecycle.
      if (isVectorAtomic(thisCol) || TYPEOF(thisCol)==LISTSXP) {
        // Atomic vectors and pairlists can be coerced to list with coerceVector:
        coerced = PROTECT(coerceVector(thisCol, maxType)); nprotect++;
      } else if (TYPEOF(thisCol)==EXPRSXP) {
        // For EXPRSXP each element must be wrapped in a list and re-coerced to EXPRSXP, otherwise column is LANGSXP
        coerced = PROTECT(allocVector(VECSXP, nrow)); nprotect++;
        for (int i=0; i<nrow; ++i) {
          SEXP thisElement = PROTECT(coerceVector(VECTOR_ELT(thisCol, i), EXPRSXP)); nprotect++;
          SET_VECTOR_ELT(coerced, i, thisElement);
        }
      } else if (!isVector(thisCol)) { 
        // Anything not a vector we can assign directly through SET_VECTOR_ELT
        // Although tecnically there should only be one list element for any type met here,
        // the length of the type may be > 1, in which case the other columns in data.table
        // will have been recycled. We therefore in turn have to recycle the list elements
        // to match the number of rows.
        coerced = PROTECT(allocVector(VECSXP, nrow)); nprotect++;
        for (int i=0; i<nrow; ++i) {
          SET_VECTOR_ELT(coerced, i, thisCol);
        }
      } else { // should be unreachable
        error("Internal error: as.matrix cannot coerce type %s to list\n", type2char(TYPEOF(thisCol))); // # nocov
      }
    } else if (integer64 && INHERITS(thisCol, char_integer64)) {
      error("Internal error: coercion of integer64 to character not yet supported\n");
    } else {
      coerced = thisCol; // type coercion handled by memrecycle
    }
    
    // Fill matrix with memrecycle
    const char *ret = memrecycle(ans, R_NilValue, ansloc, nrow, coerced, 0, -1, 0, "V1");
    // Warning when precision is lost after coercion, should not be possible to reach
    if (ret) warning(_("Column %d: %s"), j+1, ret); // # nocov
    // TODO: but maxType should handle that and this should never warn
    ansloc += nrow;
  }
    
  UNPROTECT(nprotect);  // ans, dimnames, coerced, thisElement
  return(ans);
}