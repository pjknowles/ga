/* $Id: winshmem.c,v 1.12 2002-06-20 23:34:17 vinod Exp $ */
/* WIN32 & Posix SysV-like shared memory allocation and management
 * 
 *
 * Interface:
 * ~~~~~~~~~
 *  char *Create_Shared_Region(long *idlist, long *size, long *offset)
 *       . to be called by just one process. 
 *       . calls shmalloc, a modified by Robert Harrison version of malloc-like
 *         memory allocator from K&R.shmalloc inturn calls armci_allocate() that
 *         does shmget() and shmat(). 
 *       . idlist might be just a pointer to integer or a true array in the
 *         MULTIPLE_REGIONS versions (calling routine has to take cere of it) 
 *  char *Attach_Shared_Region(long *idlist, long *size, long *offset)
 *       . called by any other process to attach to existing shmem region or
 *         if attached just calculate the address based on the offset
 *       . has to be called after shmem region was created
 *  void  Free_Shmem_Ptr(long id, long size, char* addr)
 *       . called ONLY by the process that created shmem region (id) to return
 *         pointer to shmalloc (shmem is not destroyed)
 *  long  Delete_All_Regions()
 *       . destroys all shared memory regions
 *       . can be called by any process assuming that all processes attached
 *         to alllocated shmem regions 
 *       . needs to by called by cleanup procedure(s)
 */


#define DEBUG 0
#define DEBUG0 0

#include <stdio.h>

#ifdef WIN32
#  include <windows.h>
#  include <process.h>
#  define  GETPID _getpid
#elif defined(NEC)
#  include <unistd.h>
#  include <sys/mppg.h>
   typedef void* HANDLE;
   typedef void* LPVOID;
#  define  GETPID getpid
#elif defined(HITACHI)
#  include <unistd.h>
#  define PAGE_SIZE       0x1000
#  define ROUND_UP_PAGE(size)  ((size + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1))
#  include <strings.h>
#  include <stdlib.h>
#  include <hxb/combuf.h>
#  include <hxb/combuf_returns.h>
   typedef long HANDLE;
   typedef char* LPVOID;
#  define  GETPID getpid
   static long cb_key=1961;
   static long _hitachi_reg_size;
#elif defined(MMAP)
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
#  include <sys/mman.h>
   typedef int HANDLE;
   typedef void* LPVOID;
#  define  GETPID getpid
#else
#  ifndef _POSIX_C_SOURCE
#    define  _POSIX_C_SOURCE 199309L
#  endif
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
#  include <sys/mman.h>
   typedef int HANDLE; 
   typedef void* LPVOID; 
#  define  GETPID getpid
#endif

#include <assert.h>
#include "shmalloc.h"
#include "shmem.h"

#define SHM_UNIT (1024)

/* default unit for shared memory allocation in KB! */
#if defined(WIN32)
#  define _SHMMAX  32678      
#elif defined(MACX)
#  define _SHMMAX  64*1024
#else
#  define _SHMMAX  2*32678      
#endif

#define SET_MAPNAME(id) sprintf(map_fname,"/tmp/ARMCIshmem.%d.%d",parent_pid,(id))

/*********************** global data structures ****************************/

/* Terminology
 *   region - actual piece of shared memory allocated from OS
 *   block  - a part of allocated shmem that is given to the requesting process
 */


/* array holds handles and addreses for each shmem region*/ 
static struct shm_region_list{
   char     *addr;
   HANDLE   id;
   long      size;
}region_list[MAX_REGIONS];

static char map_fname[64];
static  int alloc_regions=0;   /* counter to identify mapping handle */
static  int last_allocated=0; /* counter trailing alloc_regions by 0/1 */

/* Min and Max amount of aggregate memory that can be allocated */
static  unsigned long MinShmem = _SHMMAX;  
static  unsigned long MaxShmem = MAX_REGIONS*_SHMMAX;
static  int parent_pid=-1;  /* process id of process 0 "parent" */

extern void armci_die(char*,int);
extern int armci_me;

/* not done here yet */
void armci_shmem_init() {}

unsigned long armci_max_region()
{
  return MinShmem;
}

/*\ application can reset the upper limit for memory allocation
\*/
void armci_set_shmem_limit(unsigned long shmemlimit) /* comes in bytes */ 
{
     unsigned long kbytes;
     kbytes = (shmemlimit + SHM_UNIT -1)/SHM_UNIT;
     if(MaxShmem > kbytes) MaxShmem = kbytes;
     if(MinShmem > kbytes) MinShmem = kbytes;
}


void Delete_All_Regions()
{
int reg;
long code=0;

  for(reg = 0; reg < alloc_regions; reg++){
    if(region_list[reg].addr != (char*)0){
#       if defined(WIN32)
          UnmapViewOfFile(region_list[reg].addr);
          CloseHandle(region_list[reg].id);
#       elif defined(NEC)
          (int)dp_xmfree(region_list[reg].addr);
#       else
          munmap(region_list[reg].addr, region_list[reg].size);
          SET_MAPNAME(reg);
          (void*)unlink(map_fname);
#       endif
        region_list[reg].addr = (char*)0;
    }
  }
}


/*\ only process that created shared region returns the pointer to shmalloc 
\*/
void Free_Shmem_Ptr(long id, long size, char* addr)
{  
  shfree(addr);
}


char *armci_get_core_from_map_file(int exists, long size)
{
    LPVOID  ptr;

#if defined(HITACHI)

    Cb_object_t oid;
    int desc;

    region_list[alloc_regions].addr = (char*)0;
    if(exists){ 
      int rc,nsize=_hitachi_reg_size;
      if(size < MinShmem*SHM_UNIT) size = MinShmem*SHM_UNIT;
      nsize = ROUND_UP_PAGE(nsize);
 
      if((rc=combuf_object_get(region_list[alloc_regions].id,(Cb_size_t)nsize,0, &oid))
                       != COMBUF_SUCCESS) armci_die("attaching combufget fail",0);
      if((rc=combuf_map(oid, 0, (Cb_size_t)nsize, COMBUF_COMMON_USE, &ptr)) 
                       != COMBUF_SUCCESS) armci_die("combuf map failed",0);
       
    }else{
      int rc;
      size = ROUND_UP_PAGE(size);

      if((rc=combuf_object_get(cb_key,(Cb_size_t)size,COMBUF_OBJECT_CREATE,&oid))
                       != COMBUF_SUCCESS) armci_die("creat combufget fail",0);
      if((rc=combuf_map(oid, 0, (Cb_size_t)size, COMBUF_COMMON_USE, &ptr)) 
                       != COMBUF_SUCCESS) armci_die("combuf map failed",0);

      /* make the region suitable for communication */
      if(combuf_create_field(oid, ptr, (Cb_size_t)size, FIELD_NUM, 0, 0, &desc)
                       != COMBUF_SUCCESS) armci_die("create field failed",0);
    
      region_list[alloc_regions].id = cb_key;
      _hitachi_reg_size=size;
      cb_key++; /* increment for next combuf create call */
     
    }

#elif defined(NEC)

    region_list[alloc_regions].addr = (char*)0;
    if(exists)
       ptr = dp_xmatt(parent_pid, region_list[alloc_regions].id, (void*)0);  
    else {
       ptr = dp_xmalloc((void*)0, (long long) size);
       region_list[alloc_regions].id = ptr;
    }

    if(ptr == (void*)-1) return ((char*)0); 
       
#else
    HANDLE  h_shm_map;
    SET_MAPNAME(alloc_regions);
    region_list[alloc_regions].addr = (char*)0;

#if defined(WIN32)
    h_shm_map = CreateFileMapping(INVALID_HANDLE_VALUE,
                NULL, PAGE_READWRITE, 0, (int)size, map_fname);
    if(h_shm_map == NULL) return NULL;

    if(exists){
       /* get an error code when mapping should exist */
       if (GetLastError() != ERROR_ALREADY_EXISTS){
          CloseHandle(h_shm_map);
          fprintf(stderr,"map handle does not exist (attach)\n");
          return NULL;
       }else { /* OK */ }
    } else {
        /* problem if mapping it should not be there */
        if (GetLastError() == ERROR_ALREADY_EXISTS){
          CloseHandle(h_shm_map);
          fprintf(stderr,"map handle already exists (create)\n");
          return NULL;
        }
    }
     /* now map file into process address space */
    ptr = MapViewOfFile(h_shm_map, 
                        FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, 0);
    if((char*)ptr == NULL){
       CloseHandle(h_shm_map);
       h_shm_map = INVALID_HANDLE_VALUE;
    }
#elif defined(MMAP)&&!defined(HITACHI) && !defined(MACX)

    if(exists){
       if(size < MinShmem*SHM_UNIT) size = MinShmem*SHM_UNIT;
       h_shm_map = open(map_fname, O_RDWR, S_IRWXU);
       if(h_shm_map <0) return NULL;
    }else{
       (void*)unlink(map_fname); /* sanity cleanup */
       h_shm_map = open(map_fname, O_CREAT|O_RDWR, S_IRWXU);
       if(h_shm_map <0) return NULL;
       if(ftruncate(h_shm_map,size) < 0) return NULL;
    }

    ptr = mmap((caddr_t)0, (size_t)size, PROT_READ|PROT_WRITE,
                                         MAP_SHARED, h_shm_map, 0);

    close(h_shm_map);
    h_shm_map = -1;

#elif defined(MACX)

    if(exists){
       if(size < MinShmem*SHM_UNIT) size = MinShmem*SHM_UNIT;
       h_shm_map = shm_open(map_fname, O_RDWR, S_IRWXU);
       if(h_shm_map == -1) return NULL;
    }else{
       (void*)shm_unlink(map_fname); /* sanity cleanup */
       h_shm_map = shm_open(map_fname, O_CREAT|O_RDWR, S_IRWXU);
       if(h_shm_map<0) perror("open");
       if(h_shm_map == -1) return NULL;
       if(ftruncate(h_shm_map,size) < 0){
            perror("ftruncate");
            return NULL;
       }
    }

    ptr = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, h_shm_map, 0L);
    if((long)ptr <0){ perror("mmap"); return NULL; }

    close(h_shm_map);
    h_shm_map = -1;
    
#else

    if(exists){
       h_shm_map = shm_open(map_fname, O_RDWR, S_IRWXU);
       if(h_shm_map == -1) return NULL;
    }else{
       (void*)shm_unlink(map_fname); /* sanity cleanup */
       h_shm_map = shm_open(map_fname, O_CREAT|O_RDWR, S_IRWXU);
       if(h_shm_map) perror("shm_open");
       if(h_shm_map == -1) return NULL;
       if(ftruncate(h_shm_map,size) < 0) return NULL;
    }

    ptr = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, h_shm_map, 0L);

    close(h_shm_map);
    h_shm_map = -1;

#endif

    /*     save file handle in the array to close it in the future */
    region_list[alloc_regions].id   = h_shm_map;
    
#endif

    if(DEBUG0){printf("%d: got ptr=%p bytes=%d mmap\n",armci_me,ptr,size); fflush(stdout); }
    region_list[alloc_regions].addr = (char*)ptr;
    region_list[alloc_regions].size = size;

    return((char*)ptr);
}


/*\ function called by shared memory allocator (shmalloc)
\*/
char *armci_allocate(size_t size)
{
    char *ptr;

    if(alloc_regions>= MAX_REGIONS)
        armci_die("max alloc regions exceeded", alloc_regions);

    ptr = armci_get_core_from_map_file( 0, (long)size);
    if(ptr !=NULL) alloc_regions++;
    if(DEBUG)fprintf(stderr,"allocate got %lx %ld\n",ptr, size);

    return ptr;
}


char* Create_Shared_Region(long idlist[], long size, long *offset)
{
    char *temp;

    /*initialization */
    if(!alloc_regions){
          int reg;
          for(reg=0;reg<MAX_REGIONS;reg++){
            region_list[reg].addr=(char*)0;
            region_list[reg].id=0;
            parent_pid = GETPID();
          }
          shmalloc_request((unsigned)MinShmem, (unsigned)MaxShmem);
     }

     temp = armci_shmalloc((unsigned)size);
     if(temp == (char*)0 )
           armci_die("Create_Shared_Region: shmalloc failed ",0);
    
     /* find if shmalloc allocated a new shmem region */
     if(last_allocated == alloc_regions){
         *offset = (long) (temp - region_list[last_allocated-1].addr);
     } else if(last_allocated == alloc_regions -1){
         *offset = (long) (temp - region_list[last_allocated].addr);
         last_allocated++;
     }else{
         armci_die(" Create_Shared_Region:inconsitency in counters",
             last_allocated - alloc_regions);
     }

     idlist[0] = alloc_regions;
     idlist[1] = parent_pid;
#if  defined(HITACHI) || defined(NEC)
     idlist[2] = (long) region_list[alloc_regions-1].id;
#if  defined(HITACHI)
     idlist[SHMIDLEN-2]=_hitachi_reg_size;
#endif
     if(DEBUG)fprintf(stderr,"created %lx %ld id=%ld\n",temp, size,idlist[2]);
#endif
     return (temp);
}

#ifdef HITACHI
void server_reset_memory_variables()
{
	alloc_regions=0;
	parent_pid=-1;
}
#endif

char *Attach_Shared_Region(long id[], long size, long offset)
{
    char *temp;
    /*initialization */
    if(!alloc_regions){
          int reg;
          for(reg=0;reg<MAX_REGIONS;reg++){
            region_list[reg].addr=(char*)0;
            region_list[reg].id=0;
            parent_pid= (int) id[1];
          }
     }

     if(DEBUG)printf("%d:alloc_regions=%d size=%d\n",armci_me,alloc_regions,size);
     /* find out if a new shmem region was allocated */
     if(alloc_regions == id[0] -1){
#if      defined(HITACHI) || defined(NEC)
#if        defined(HITACHI)
               _hitachi_reg_size=id[SHMIDLEN-2];
#          endif

               region_list[alloc_regions].id = (HANDLE) id[2];
#        endif
         if(DEBUG)printf("alloc_regions=%d size=%d\n",alloc_regions,size);
         temp = armci_get_core_from_map_file(1,size);
         if(temp != NULL)alloc_regions++;
         else return NULL;
     }

     if( alloc_regions == id[0]){
         temp = region_list[alloc_regions-1].addr + offset; 
     }else armci_die("Attach_Shared_Region:iconsistency in counters",
         alloc_regions - (int) id[0]);

      if(DEBUG)fprintf(stderr,"\n%d:attach succesful off=%ld ptr=%p\n",armci_me,offset,temp);
     return(temp);
}



long armci_shmem_reg_size(int i, long id)
{
     if(i<0 || i>= MAX_REGIONS)armci_die("armci_shmem_reg_size: bad i",i); 
     if(region_list[i].id !=(HANDLE)id)armci_die("armci_shmem_reg_size id",(int)id);
     return region_list[i].size;
}

char* armci_shmem_reg_ptr(int i)
{
     if(i<0 || i>= MAX_REGIONS)armci_die("armci_shmem_reg_ptr: bad i",i); 
     return region_list[i].addr;
}
