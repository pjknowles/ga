/*********************** "private" include file for DRA *****************/
 
#include "elio.h"
#include "macdecls.h"
#include <string.h>
#define MAXDIM 7

#if defined(CRAY) && defined(__crayx1)
#undef CRAY
#endif

#ifdef FALSE
#undef FALSE
#endif
#ifdef TRUE
#undef TRUE
#endif
#ifdef CRAY_YMP
#include <fortran.h>
#define FALSE _btol(0)
#define TRUE  _btol(1)
#else
#define FALSE (logical) 0
#define TRUE  (logical) 1
#endif

#define USE_OLD_BUFFER

/************************** common constants ***********************************/
#define DRA_OFFSET     5000                    /* DRA handle offset            */
#define DRA_BRD_TYPE  30000                    /* msg type for DRA broadcast   */
#define DRA_GOP_TYPE  30001                    /* msg type for DRA sum         */
#define DRA_MAX_NAME     72                    /* max length of array name     */
#define DRA_MAX_FNAME   248                    /* max length of metafile name  */


/************************* common data structures **************************/
typedef struct{                               /* stores basic DRA info */
        Integer ndim;                         /* dimension of array */
        Integer dims[MAXDIM];                 /* array dimensions */
        Integer chunk[MAXDIM];                /* data layout chunking */
        Integer layout;                       /* date layout type */
        int type;                         /* data type */
        int mode;                         /* file/array access permissions */
        char    name[DRA_MAX_NAME+8];         /* array name */
        char    fname[DRA_MAX_FNAME+8];       /* metafile name */
        Integer actv;                         /* is array active ? */ 
        Integer indep;                        /* shared/independent files ? */
        Fd_t      fd;                         /* ELIO meta-file descriptor */
        Integer numfiles;                     /* # files on open file system */
        Integer ioprocs;                      /* number of IO procs per node */
} disk_array_t;

#define MAX_ALGN  1                /* max # aligned subsections   */ 
#define MAX_UNLG  (2*(MAXDIM-1))   /* max # unaligned subsections */

typedef struct{                   /* object describing DRA/GA section */
        Integer handle;
        Integer ndim;
        Integer lo[MAXDIM];
        Integer hi[MAXDIM];
}section_t;


typedef struct{                  /* structure stores arguments for callback f */
        int op;
        int transp;
        Integer ld[MAXDIM];
        section_t gs_a;
        section_t ds_a;
        section_t ds_chunk;
}args_t;


typedef struct{                   /* stores info associated with DRA request */
        Integer  d_a;             /* disk array handle */
#ifdef USE_OLD_BUFFER
        io_request_t id;          /* low level asynch. I/O op. id */
#endif
        int num_pending;          /* number of pending  asynch. I/O ops */ 
        Integer list_algn[MAX_ALGN][2*MAXDIM]; /* coordinates of aligned subsection */
        Integer list_unlgn[MAX_UNLG][2*MAXDIM];/*coordinates of unaligned subsections*/
        Integer list_cover[MAX_UNLG][2*MAXDIM];/* coordinates of "cover" subsections */
        int        ibuf;
        int        nu;            
        int        na;
        int        callback;      /* callback status flag ON/OFF */
#ifdef USE_OLD_BUFFER
        args_t     args;          /* arguments to callback function */
#endif
}request_t;

#ifndef USE_OLD_BUFFER
typedef struct{
  args_t args;
  Integer    req;
  io_request_t id;
  char *buffer;
} buffer_t;
#endif


extern disk_array_t *DRA;
extern logical dra_debug_flag;


/**************************** common macros ********************************/
#define MAX(a,b) (((a) >= (b)) ? (a) : (b))
#define MIN(a,b) (((a) <= (b)) ? (a) : (b))

#define dai_error ga_error

extern int dai_read_param(char* filename, Integer d_a);
extern void dai_write_param(char* filename, Integer d_a);
extern void dai_delete_param(char* filename, Integer d_a);
extern int dai_file_config(char* filename);
extern logical dai_section_intersect(section_t sref, section_t* sadj);
extern int  drai_get_num_serv(void);

extern Integer dra_create(Integer *type, Integer *dim1, Integer *dim2, 
			  char *name, char *filename, Integer *mode, 
			  Integer *reqdim1, Integer *reqdim2, Integer *d_a);

extern Integer ndra_create(Integer *type, Integer *ndim, Integer dims[], 
			   char *name, char *filename, Integer *mode, 
			   Integer reqdims[], Integer *d_a);

extern Integer dra_open(char *filename, Integer *mode, Integer *d_a);

extern Integer dra_inquire(Integer *d_a, Integer *type, Integer *dim1, 
			   Integer *dim2, char *name, char *filename);

extern Integer ndra_inquire(Integer *d_a, Integer *type, Integer *ndim,
			    Integer dims[],char *name,char *filename);

extern Integer ndra_create_config(Integer *type, Integer *ndim, Integer dims[],
				  char *name, char *filename, Integer *mode, 
				  Integer reqdims[], Integer *numfiles, 
				  Integer *numioprocs,   Integer *d_a);

extern Integer FATR dra_terminate_();

extern Integer FATR dra_init_(
		       Integer *max_arrays,              /* input */
		       DoublePrecision *max_array_size,  /* input */
		       DoublePrecision *tot_disk_space,  /* input */
		       DoublePrecision *max_memory);     /* input */

extern Integer FATR dra_probe_(
	  Integer *request,                  /*input*/
	  Integer *status);                   /*output*/
     
extern Integer FATR ndra_write_(
          Integer *g_a,                      /*input:GA handle*/
	  Integer *d_a,                      /*input:DRA handle*/
	  Integer *request);                 /*output: handle to async oper.*/

extern Integer FATR ndra_write_section_(
	  logical *transp,                   /*input:transpose operator*/
	  Integer *g_a,                      /*input:GA handle*/
	  Integer glo[],                     /*input*/
	  Integer ghi[],                     /*input*/
	  Integer *d_a,                      /*input:DRA handle*/
	  Integer dlo[],                     /*input*/
	  Integer dhi[],                     /*input*/
	  Integer *request);                 /*output: async. request id*/

extern Integer FATR ndra_read_(Integer* g_a, Integer* d_a, Integer* request);

extern Integer FATR ndra_read_section_(
          logical *transp,                   /*input:transpose operator*/
	  Integer *g_a,                      /*input:GA handle*/
	  Integer glo[],                     /*input*/
	  Integer ghi[],                     /*input*/
	  Integer *d_a,                      /*input:DRA handle*/
	  Integer dlo[],                     /*input*/
	  Integer dhi[],                     /*input*/
	  Integer *request);                  /*output: request id*/
     
extern void FATR dra_print_internals_(Integer *d_a);

extern void FATR dra_set_debug_(logical *flag);

extern void FATR dra_set_default_config_(Integer *numfiles, 
					 Integer *numioprocs);

extern Integer FATR dra_delete_(Integer* d_a);

extern Integer FATR dra_close_(Integer* d_a);

extern void dra_flick_();

extern Integer FATR dra_wait_(Integer* request);
