/* $Id: datatypes.c,v 1.4 2002-01-18 19:52:12 vinod Exp $
 * conversion of MA identifiers between C to Fortran data types 
 * Note that ga_type_c2f(MT_F_INT) == MT_F_INT 
 */

#include <macdecls.h>
#include "global.h"
int ga_type_f2c(int type)
{
int ctype;
   switch(type){
   case MT_F_INT: 
#       ifdef EXT_INT 
		ctype = C_INT;
#       else
                ctype = C_LONG;
#       endif
                break;
   case MT_F_REAL: 
#       if defined(CRAY) || defined(NEC) 
                ctype = C_DBL;
#       else
                ctype = C_FLOAT;
#       endif
                break;
   case MT_F_DBL: 
		ctype = C_DBL;
                break;
   case MT_F_DCPL: 
		ctype = C_DCPL;
                break;
   case MT_F_SCPL: 
#       if defined(CRAY) || defined(NEC)
		ctype = C_DCPL;
#       else
		ctype = C_SCPL;
#       endif
   default:     ctype = type;
                break;
   }
   
   return(ctype);
}


int ga_type_c2f(int type)
{
int ftype;
   switch(type){
   case C_INT: 
                ftype = (sizeof(int) != sizeof(Integer))? -1: MT_F_INT;
                break;
   case C_LONG: 
                ftype = (sizeof(long) != sizeof(Integer))? -1: MT_F_INT;
                break;
   case C_FLOAT:
#       if defined(CRAY) || defined(NEC)
                ftype = -1;
#       else
                ftype = MT_F_REAL; 
#       endif
                break;
   case C_DBL: 
                ftype = MT_F_DBL;
                break;
   case C_DCPL:
                ftype = MT_F_DCPL;
                break;
   case C_SCPL:
#       if defined(CRAY) || defined(NEC)
                ftype = -1;
#       else
                ftype = MT_F_SCPL;
#       endif
   default:     ftype = type;
                break;
   }
   
   return(ftype);
}
