/**********************************************************************\
 ELementary I/O (ELIO) disk operations for Chemio libraries   
\**********************************************************************/


#include "elio.h"
#include <errno.h>
#if defined(PARAGON)
#  include <nx.h>
#endif


#define  MAX_AIO_REQ  4
#define  NULL_AIO    -123456

#define FOPEN_MODE 0644
#define MAX_ATTEMPTS 10


#if  defined(AIX) || defined(DECOSF) || defined(SGITFP) || defined(notKSR)
#   define AIO 1
#   include <aio.h>
#   if defined(KSR)||defined(AIX)
#      define INPROGRESS EINPROG
#   else 
#      define INPROGRESS EINPROGRESS
#   endif
      struct aiocb    cb_fout[MAX_AIO_REQ];
const struct aiocb   *cb_fout_arr[MAX_AIO_REQ];
#else
#   define NO_AIO 1
#   define INPROGRESS 1            /* I wish this didn't have to be here */
#endif


static int            aio_req[MAX_AIO_REQ];
static int            first_elio_init = 1;


#if defined(AIO) || defined(PARAGON)
#  define AIO_LOOKUP \
      while(aio_req[aio_i] != NULL_AIO && aio_i < MAX_AIO_REQ) aio_i++;
#else
#  define AIO_LOOKUP aio_i = MAX_AIO_REQ;
#endif


#define SYNC_EMMULATE(op) \
  if( elio_ ## op (fd, offset, buf, bytes) != bytes ) \
    { \
        fprintf(stderr, "sync_emmulate: stat=%d  bytes=%d\n", stat, bytes); \
	*req_id = ELIO_DONE; \
	stat   = ELIO_FAIL;  \
    } \
  else \
    { \
       *req_id = ELIO_DONE; \
       stat    = 0; \
    }

static int _elio_Errors_Fatal=1;


/****************************************************************/


void elio_errors_fatal(onoff)
int onoff;
{
    _elio_Errors_Fatal = onoff;
}
 

/*\ Blocking Write - returns number of bytes written or -1 if failed
\*/
Size_t elio_write(fd, offset, buf, bytes)
     Fd_t        *fd;
     off_t        offset;
     Void        *buf;
     Size_t       bytes;
{
Size_t stat, bytes_to_write = bytes;
int    attempt=0;

      /* lseek error is always fatal */
      if(offset != lseek(fd->fd, offset, SEEK_SET))
                         ELIO_ABORT("elio_write: seek broken:",0);

      /* interrupted write should be restarted */
      do {
             if(attempt == MAX_ATTEMPTS) 
                ELIO_ABORT("elio_write: num max attempts exceeded", attempt);

             stat = write(fd->fd, buf, bytes_to_write);
             if(stat < bytes_to_write && stat >= -1){
                bytes_to_write -= stat;
                buf += stat;   /* advance pointer by # bytes written */
             }else
                bytes_to_write = 0;
             attempt++;
      }while(bytes_to_write && (errno == EINTR || errno == EAGAIN));

      if(stat != -1) stat = bytes -  bytes_to_write;

      if(_elio_Errors_Fatal && stat != bytes) 
	ELIO_ABORT("elio_write: failed", 0);
      return(stat);
}



void elio_set_cb(fd, offset, reqn, buf, bytes)
     Fd_t  *fd;
     off_t  offset;
     int    reqn;
     Void  *buf;
     Size_t bytes;
{
#if defined(PARAGON) && defined(DEBUG)
  fprintf(stderr, "elio_set_cb: No control block to set on Paragon\n");
#endif

#if defined(AIO)
  cb_fout[reqn].aio_offset = offset;
  cb_fout_arr[reqn] = cb_fout+reqn;
# if defined(KSR)
    cb_fout[reqn].aio_whence = SEEK_SET;
# else
    cb_fout[reqn].aio_buf    = buf;
    cb_fout[reqn].aio_nbytes = bytes;
#   if defined(AIX)
      cb_fout[reqn].aio_whence = SEEK_SET;
#   else
      cb_fout[reqn].aio_sigevent.sigev_notify = SIGEV_NONE;
      cb_fout[reqn].aio_fildes    = fd->fd;
#   endif
# endif
#else
#  if !defined(AIO) && !defined(PARAGON)
  ELIO_ABORT("elio_set_cb: Should not be called without AIO service.\n",1);
#  endif
#endif
}




/*\ Asynchronous Write: returns 0 if succeded or -1 if failed
\*/
int elio_awrite(fd, offset, buf, bytes, req_id)
     Fd_t         *fd;
     off_t         offset;
     Void         *buf;
     Size_t        bytes;
     io_request_t *req_id;
{
  Size_t stat;
  int    aio_i = 0;

  *req_id = ELIO_DONE;
  AIO_LOOKUP;

  if(aio_i >= MAX_AIO_REQ)
    {
#if defined(DEBUG)
      fprintf(stderr, "elio_awrite: Warning- asynch overflow\n");
#endif
      SYNC_EMMULATE(write);
    }
  else
    {
      *req_id = (io_request_t) aio_i;
      elio_set_cb(fd, offset, aio_i, buf, bytes);
#if defined(PARAGON)
      if(offset != lseek(fd->fd, offset, SEEK_SET))
        ELIO_ABORT("elio_awrite: seek broken:",0);
      *req_id = _iwrite(fd->fd, buf, bytes);
      stat = (*req_id == (io_request_t)-1) ? (Size_t)-1: (Size_t)0;
#elif defined(KSR) && defined(AIO)
      stat = awrite(fd->fd, buf, bytes, cb_fout+aio_i);
#elif defined(AIX) && defined(AIO)
      stat = aio_write(fd->fd, cb_fout+aio_i);
#elif defined(AIO)
      stat = aio_write(cb_fout+aio_i);
#else
      SYNC_EMMULATE(write);
#endif
      aio_req[aio_i] = (int) *req_id;
    };
  if(_elio_Errors_Fatal && stat ==-1) 
    ELIO_ABORT("elio_awrite: failed", 0);
  return(stat);
}



/*\ Blocking Read - returns number of bytes read or -1 if failed
\*/
Size_t elio_read(fd, offset, buf, bytes)
Fd_t        *fd;
off_t        offset;
Void        *buf;
Size_t       bytes;
{
Size_t stat, bytes_to_read = bytes;
int    attempt=0;

      /* lseek error is always fatal */
      if(offset != lseek(fd->fd, offset, SEEK_SET))
                         ELIO_ABORT("elio_read: seek broken:",0);

      /* interrupted read should be restarted */
      do {
             if(attempt == MAX_ATTEMPTS) 
                ELIO_ABORT("elio_read: num max attempts exceeded", attempt);

             stat = read(fd->fd, buf, bytes_to_read);
             if(stat < bytes_to_read && stat >= -1){
                bytes_to_read -= stat;
                buf += stat;   /* advance pointer by # bytes read */
             }else
                bytes_to_read = 0;
             attempt++;
      }while(bytes_to_read && (errno == EINTR || errno == EAGAIN));

      if(stat != -1) stat = bytes -  bytes_to_read;

      if(_elio_Errors_Fatal && stat != bytes) 
	ELIO_ABORT("elio_read: failed", 0);
      return(stat);
}



/*\ Asynchronous Read: returns 0 if succeded or -1 if failed
\*/
int elio_aread(fd, offset, buf, bytes, req_id)
Fd_t         *fd;
off_t         offset;
Void         *buf;
Size_t        bytes;
io_request_t *req_id;
{
  Size_t stat;
  int    aio_i = 0;

  *req_id = ELIO_DONE;
  AIO_LOOKUP;
  if( aio_i >= MAX_AIO_REQ )
    {
#if defined(DEBUG)
      fprintf(stderr, "elio_aread: Warning- asynch overflow\n");
#endif
      SYNC_EMMULATE(read);
    }
  else
    {
     *req_id = (io_request_t) aio_i;
      elio_set_cb(fd, offset, aio_i, buf, bytes);
#if defined(PARAGON)
      if(offset != lseek(fd->fd, offset, SEEK_SET))
	ELIO_ABORT("elio_aread: seek broken:",0);
      *req_id = _iread(fd->fd, buf, bytes);
      stat = (*req_id == (io_request_t)-1) ? (Size_t)-1: (Size_t)0;
#elif defined(KSR) && defined(AIO)
      stat = aread(fd->fd, buf, bytes, cb_fout+aio_i);
#elif defined(AIX) && defined(AIO)
      stat = aio_read(fd->fd, cb_fout+aio_i);
#elif defined(AIO)
      stat = aio_read(cb_fout+aio_i);
#else
     SYNC_EMMULATE(read);
#endif
     aio_req[aio_i] = *req_id;
    };
  if(_elio_Errors_Fatal && stat ==-1) 
    ELIO_ABORT("elio_aread: failed", 0);
  return(stat);
}


/*\ Wait for asynchronous I/O operation to complete. Invalidate id.
\*/
int elio_wait(req_id)
io_request_t *req_id;
{
  int  aio_i=0;
  int  rc;
  
  if(*req_id != ELIO_DONE )
    { 
#if defined(PARAGON)
      iowait(*req_id);
#elif defined(KSR)
      if(iosuspend(1, cb_fout_arr+(int)*req_id) ==-1)
	ELIO_ABORT("elio_wait:",0);
#elif defined(AIX)
      do {
           rc =aio_suspend(1, cb_fout_arr+(int)*req_id);
	   /* wouldn't this be a good place to usleep(100000);? */
      } while(rc == -1 && errno == EINTR); 
      if(rc  == -1)
        ELIO_ABORT("elio_wait: can't probe",0);
#elif !defined(NO_AIO)
      if(aio_suspend(cb_fout_arr+(int)*req_id, 1, NULL) == -1)
	ELIO_ABORT("elio_wait:",0);
#endif
      while(aio_req[aio_i] != *req_id && aio_i < MAX_AIO_REQ) aio_i++;
      if(aio_i >= MAX_AIO_REQ)
	ELIO_ABORT("elio_wait: Handle %d, is not in aio_req table", 1);
      aio_req[aio_i] = NULL_AIO;
      *req_id = ELIO_DONE;
   }
   return ELIO_OK;
}



/*\ Check if asynchronous I/O operation completed. If yes, invalidate id.
\*/
int elio_probe(req_id, status)
io_request_t *req_id;
int          *status;
{
  int    errval;
  int    aio_i = 0;
     
   if(*req_id != ELIO_DONE)
     {
#if defined(PARAGON)
       if( iodone(*req_id)== (long) 0) errval = INPROGRESS;
       else errval = 0;
#elif defined(KSR)
       errval = cb_fout[(int)*req_id].aio_errno;
#elif defined(AIX)
       errval = aio_error(cb_fout[(int)*req_id].aio_handle);
#elif !defined(NO_AIO)
       errval = aio_error(cb_fout+(int)*req_id);
#endif
       switch (errval) {
       case 0:           while(aio_req[aio_i] != *req_id && aio_i < MAX_AIO_REQ) aio_i++;
			 if(aio_i >= MAX_AIO_REQ)
			   ELIO_ABORT("elio_probe: id %d not in table", 1);
			 *req_id = ELIO_DONE; 
	                 *status = ELIO_DONE;
			 aio_req[aio_i] = NULL_AIO;
			 break;
       case INPROGRESS:  *status = ELIO_PENDING; 
			 break;
       default:          ELIO_ABORT("problem in elio_probe",errval);
			 return ELIO_FAIL;
       };
     }
   return ELIO_OK;
}




/*\ Stat a file (or path) to determine it's filesystem type
\*/
int  elio_stat(fname)
char *fname;
{
  struct  stat     ufs_stat;
  char             tmp_pathname[EAF_FILENAME_MAX];
  int              i;
  int              ret_fs = -1;
#if defined(PARAGON)
  struct statpfs  *statpfsbuf;
  struct estatfs   estatbuf;
  int              bufsz;
#endif
#if defined(AIX)
  piofs_statfs_t piofs_stat;
#endif
  

  if(fname[0] == '/')
     {
        i = strlen(fname);
        while(fname[i] != '/') i--;
        tmp_pathname[i+1] = (char) 0;
        while(i >= 0)
          {
             tmp_pathname[i] = fname[i];
             i--;
          };
      }
   else
     strcpy(tmp_pathname, "./");
#if defined(DEBUG)
   fprintf(stderr, "tmp_pathname =%s|    fname=%s|\n", tmp_pathname, fname);
#endif

#if defined(PARAGON)
   bufsz = sizeof(struct statpfs) + SDIRS_INIT_SIZE;
   if( (statpfsbuf = (struct statpfs *) malloc(bufsz)) == NULL)
     ELIO_ABORT("elio_open: Unable to malloc struct statpfs\n", 1);
   if(statpfs(tmp_pathname, &estatbuf, statpfsbuf, bufsz) == 0)
     {
       if(estatbuf.f_type == MOUNT_PFS)
         ret_fs = FS_PFS;
       else if(estatbuf.f_type == MOUNT_UFS || estatbuf.f_type == MOUNT_NFS)
         ret_fs = FS_UFS;
       else
          ELIO_ABORT("elio_open: Abel to stat, Unable to determine filesystem type\n", 1);
     }
   else
     ELIO_ABORT("elio_open: Unable to to stat path.\n",1);
   free(statpfsbuf);
#else
#  if defined(AIX)
   strcpy(piofs_stat.name, tmp_pathname);
   if(piofsioctl(i, PIOFS_STATFS, &piofs_stat) == 0)
     ret_fs = FS_PIOFS;
#  endif
   if(ret_fs == -1)
     {
       if(stat(tmp_pathname, &ufs_stat) != 0)
	 ELIO_ABORT("elio_open: Not able to stat UFS filesystem\n", 1)
	   else
	     ret_fs = FS_UFS;
     };
#endif
#if defined(DEBUG)
   fprintf(stderr, "Determined filesystem: %d\n", ret_fs);
#endif
   return(ret_fs);
}




/*\ Noncollective File Open
\*/
Fd_t * elio_open(fname, type)
char* fname;
int   type;
{
  Fd_t *fd;
  int ptype;
  
  if(first_elio_init) elio_init();
  
   switch(type){
     case ELIO_W:  ptype = O_CREAT | O_TRUNC | O_WRONLY;     
                   break;
     case ELIO_R:  ptype = O_RDONLY;     
                   break;
     case ELIO_RW: ptype = O_CREAT | O_RDWR;     
                   break;
     default:      ELIO_ABORT("elio_open: mode incorrect", 0);
   }


   if( (fd = (Fd_t *) malloc(sizeof(Fd_t)) ) == NULL)
     ELIO_ABORT("elio_open: Unable to malloc Fd_t structure\n", 1);

   fd->fs = elio_stat(fname);

   fd->fd = open(fname, ptype, FOPEN_MODE );
   if(_elio_Errors_Fatal && (int)fd->fd == -1)
     ELIO_ABORT("elio_open failed",0);
   return(fd);
}




/*\ Collective File Open
\*/
Fd_t * elio_gopen(fname, type)
char* fname;
int   type;
{
  Fd_t *fd;

  if(first_elio_init) elio_init();
  
#  if defined(PARAGON)
   {
      int ptype;

      switch(type){
        case ELIO_W:  ptype = O_CREAT | O_TRUNC | O_WRONLY;
                      break;
        case ELIO_R:  ptype = O_RDONLY;
                      break;
        case ELIO_RW: ptype = O_CREAT | O_RDWR;
                      break;
        default:      ELIO_ABORT("elio_open: mode incorrect", 0);
      }

     if( (fd = (Fd_t *) malloc(sizeof(Fd_t)) ) == NULL)
       ELIO_ABORT("elio_open: Unable to malloc Fd_t structure\n", 1);

      fd->fs = FS_PFS;
      fd->fd = gopen(fname, ptype, M_ASYNC, FOPEN_MODE );
   }
#  else
      ELIO_ABORT("elio_gopen: Collective open only supported on Paragon",0);
#  endif

   if(_elio_Errors_Fatal && (int)fd->fd == -1)
     ELIO_ABORT("elio_gopen failed",0);
   return(fd);
}


/*\ Close File
\*/
void elio_close(fd)
Fd_t *fd;
{
   /* if close fails, it must be a fatal error */
   if(close(fd->fd)==-1) 
     ELIO_ABORT("elio_close failed:",0);
   free(fd);
}


/*\ Delete File
\*/
int elio_delete(filename)
char  *filename;
{
int rc;

    rc = unlink(filename);
    if(_elio_Errors_Fatal && rc ==-1) 
      ELIO_ABORT("elio_delete failed",0);
    return(rc);
}



/*\ Initialize ELIO
\*/
void elio_init()
{
  int i;

  if(first_elio_init)
    {
      first_elio_init = 0;
#if defined(AIO) || defined(PARAGON)
      for(i=0; i < MAX_AIO_REQ; i++)
	aio_req[i] = NULL_AIO;
#endif
    };
}


/*\ Terminate ELIO
\*/
void elio_terminate()
{

}


