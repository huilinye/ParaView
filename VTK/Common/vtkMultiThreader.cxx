#include "vtkMultiThreader.h"

// These are the includes necessary for multithreaded rendering on an SGI
// using the sproc() call
#ifdef VTK_USE_SPROC
#include <sys/resource.h>
#include <sys/prctl.h>
#include <wait.h>
#endif

#ifdef VTK_USE_PTHREADS
#include <pthread.h>
#endif

// Description:
// Constructor. Default all the methods to NULL. Since the
// ThreadInfoArray is static, the ThreadIDs can be initialized here
// and will not change.
vtkMultiThreader::vtkMultiThreader()
{
  int i;

  for ( i = 0; i < VTK_MAX_THREADS; i++ )
    {
    this->ThreadInfoArray[i].ThreadID = i;
    this->MultipleMethod[i] = NULL;
    }

  this->SingleMethod = NULL;

#ifdef VTK_USE_SPROC
  // Default the number of threads to be the number of available
  // processors if we are using sproc()
  this->ThreadCount             = prctl( PR_MAXPPROCS );
#endif

#ifdef VTK_USE_PTHREADS
  // Default the number of threads to be the number of available
  // processors if we are using pthreads()
  this->ThreadCount             = sysconf( _SC_NPROCESSORS_ONLN );
#endif

#ifndef VTK_USE_SPROC
#ifndef VTK_USE_PTHREADS
  // If we are not multithreading, the number of threads should
  // always be 1
  this->ThreadCount             = 1;
#endif  
#endif  

}

// Description:
// Destructor. Nothing allocated so nothing needs to be done here.
vtkMultiThreader::~vtkMultiThreader()
{
}

// Description:
// Set the user defined method that will be run on ThreadCount threads
// when SingleMethodExecute is called.
void vtkMultiThreader::SetSingleMethod( vtkThreadFunctionType (*f)(void *), 
					void *data )
{ 
  this->SingleMethod = f;
  this->SingleData   = data;
}

// Description:
// Set one of the user defined methods that will be run on ThreadCount
// threads when MultipleMethodExecute is called. This method should be
// called with index = 0, 1, ..,  ThreadCount-1 to set up all the
// required user defined methods
void vtkMultiThreader::SetMultipleMethod( int index, 
					  vtkThreadFunctionType (*f)(void *), 
					  void *data )
{ 
  // You can only set the method for 0 through ThreadCount-1
  if ( index >= this->ThreadCount ) {
    vtkErrorMacro( << "Can't set method " << index << 
    " with a thread count of " << this->ThreadCount );
    }
  else
    {
    this->MultipleMethod[index] = f;
    this->MultipleData[index]   = data;
    }
}

// Description:
// Execute the method set as the SingleMethod on ThreadCount threads.
void vtkMultiThreader::SingleMethodExecute()
{

#ifdef VTK_USE_SPROC
  int                thread_loop;
  siginfo_t          info_ptr;
  int                process_id[VTK_MAX_THREADS];
#endif

#ifdef VTK_USE_PTHREADS
  int                thread_loop;
  pthread_t          process_id[VTK_MAX_THREADS];
#endif

  if ( !this->SingleMethod )
    {
    vtkErrorMacro( << "No single method set!" );
    return;
    }

  // We are using sproc (on SGIs), pthreads(on Suns), or a single thread
  // (the default)  

#ifdef VTK_USE_SPROC
  // Using sproc() on an SGI
  //
  // We want to use sproc to start this->ThreadCount - 1 additional
  // threads which will be used to call this->SingleMethod(). The
  // parent thread will also call this routine.  When it is done,
  // it will wait for all the children to finish. 
  //
  // First, start up the this->ThreadCount-1 processes.  Keep track
  // of their process ids for use later in the waitid call
  for ( thread_loop = 0; thread_loop < this->ThreadCount-1; thread_loop++ )
    {
    this->ThreadInfoArray[thread_loop].UserData    = this->SingleData;
    this->ThreadInfoArray[thread_loop].ThreadCount = this->ThreadCount;
    process_id[thread_loop] = 
      sproc( this->SingleMethod, PR_SADDR, 
	     ( (void *)(&this->ThreadInfoArray[thread_loop]) ) );
    }
  
  // Now, the parent thread calls this->SingleMethod() itself
  this->ThreadInfoArray[this->ThreadCount-1].UserData    = this->SingleData;
  this->ThreadInfoArray[this->ThreadCount-1].ThreadCount = this->ThreadCount;
  this->SingleMethod( 
    (void *)(&this->ThreadInfoArray[this->ThreadCount-1]) );

  // The parent thread has finished this->SingleMethod() - so now it
  // waits for each of the other processes to exit
  for ( thread_loop = 0; thread_loop < this->ThreadCount-1; thread_loop++ )
    {
    waitid( P_PID, (id_t) process_id[thread_loop], &info_ptr, WEXITED );
    }
#endif

#ifdef VTK_USE_PTHREADS
  // Using POSIX threads
  //
  // We want to use pthread_create to start this->ThreadCount - 1 additional
  // threads which will be used to call this->SingleMethod(). The
  // parent thread will also call this routine.  When it is done,
  // it will wait for all the children to finish. 
  //
  // First, start up the this->ThreadCount-1 processes.  Keep track
  // of their process ids for use later in the pthread_join call

pthread_attr_t attr;

pthread_attr_init(&attr);
pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  for ( thread_loop = 0; thread_loop < this->ThreadCount-1; thread_loop++ )
    {
    this->ThreadInfoArray[thread_loop].UserData    = this->SingleData;
    this->ThreadInfoArray[thread_loop].ThreadCount = this->ThreadCount;
    pthread_create( &(process_id[thread_loop]),
		      &attr, this->SingleMethod,  
		      ( (void *)(&this->ThreadInfoArray[thread_loop]) ) );
    }
  
  // Now, the parent thread calls this->SingleMethod() itself
  this->ThreadInfoArray[this->ThreadCount-1].UserData    = this->SingleData;
  this->ThreadInfoArray[this->ThreadCount-1].ThreadCount = this->ThreadCount;
  this->SingleMethod( 
    (void *)(&this->ThreadInfoArray[this->ThreadCount - 1]) );

  // The parent thread has finished this->SingleMethod() - so now it
  // waits for each of the other processes to exit
  for ( thread_loop = 0; thread_loop < this->ThreadCount-1; thread_loop++ )
    {
    pthread_join( process_id[thread_loop], NULL );
    }
#endif

#ifndef VTK_USE_SPROC
#ifndef VTK_USE_PTHREADS
  // There is no multi threading, so there is only one thread.
  this->ThreadInfoArray[0].UserData    = this->SingleData;
  this->ThreadInfoArray[0].ThreadCount = this->ThreadCount;
  this->SingleMethod( (void *)(&this->ThreadInfoArray[0]) );
#endif
#endif
}

void vtkMultiThreader::MultipleMethodExecute()
{
  int                thread_loop;

#ifdef VTK_USE_SPROC
  siginfo_t          info_ptr;
  int                process_id[VTK_MAX_THREADS];
#endif

#ifdef VTK_USE_PTHREADS
  pthread_t          process_id[VTK_MAX_THREADS];
#endif


  for ( thread_loop = 0; thread_loop < this->ThreadCount; thread_loop++ )
    if ( this->MultipleMethod[thread_loop] == (void *)NULL)
      {
      vtkErrorMacro( << "No multiple method set for: " << thread_loop );
      return;
      }

  // We are using sproc (on SGIs), pthreads(on Suns), or a single thread
  // (the default)  

#ifdef VTK_USE_SPROC
  // Using sproc() on an SGI
  //
  // We want to use sproc to start this->ThreadCount - 1 additional
  // threads which will be used to call the ThreadCount-1 methods
  // defined in this->MultipleMethods[](). The parent thread
  // will call this->MultipleMethods[ThreadCount-1]().  When it is done,
  // it will wait for all the children to finish. 
  //
  // First, start up the this->ThreadCount-1 processes.  Keep track
  // of their process ids for use later in the waitid call
  for ( thread_loop = 0; thread_loop < this->ThreadCount-1; thread_loop++ )
    {
    this->ThreadInfoArray[thread_loop].UserData = 
      this->MultipleData[thread_loop];
    this->ThreadInfoArray[thread_loop].ThreadCount = this->ThreadCount;
    process_id[thread_loop] = 
      sproc( this->MultipleMethod[thread_loop], PR_SADDR, 
	     ( (void *)(&this->ThreadInfoArray[thread_loop]) ) );
    }
  
  // Now, the parent thread calls the last method itself
  this->ThreadInfoArray[this->ThreadCount-1].UserData = 
    this->MultipleData[this->ThreadCount-1];
  this->ThreadInfoArray[this->ThreadCount-1].ThreadCount = this->ThreadCount;
  (this->MultipleMethod[this->ThreadCount-1])( 
    (void *)(&this->ThreadInfoArray[this->ThreadCount-1]) );

  // The parent thread has finished its method - so now it
  // waits for each of the other processes (created with sproc) to
  // exit
  for ( thread_loop = 0; thread_loop < this->ThreadCount-1; thread_loop++ )
    {
    waitid( P_PID, (id_t) process_id[thread_loop], &info_ptr, WEXITED );
    }
#endif

#ifdef VTK_USE_PTHREADS
  // Using POSIX threads
  //
  // We want to use pthread_create to start this->ThreadCount - 1 additional
  // threads which will be used to call the ThreadCount-1 methods
  // defined in this->MultipleMethods[](). The parent thread
  // will call this->MultipleMethods[ThreadCount-1]().  When it is done,
  // it will wait for all the children to finish. 
  //
  // First, start up the this->ThreadCount-1 processes.  Keep track
  // of their process ids for use later in the pthread_join call
  for ( thread_loop = 0; thread_loop < this->ThreadCount-1; thread_loop++ )
    {
    this->ThreadInfoArray[thread_loop].UserData = 
      this->MultipleData[thread_loop];
    this->ThreadInfoArray[thread_loop].ThreadCount = this->ThreadCount;
    pthread_create( &(process_id[thread_loop]),
		      NULL, this->MultipleMethod[thread_loop],  
		      ( (void *)(&this->ThreadInfoArray[thread_loop]) ) );
    }
  
  // Now, the parent thread calls the last method itself
  this->ThreadInfoArray[this->ThreadCount-1].UserData = 
    this->MultipleData[this->ThreadCount-1];
  this->ThreadInfoArray[this->ThreadCount-1].ThreadCount = this->ThreadCount;
  (this->MultipleMethod[this->ThreadCount-1])( 
    (void *)(&this->ThreadInfoArray[this->ThreadCount - 1]) );

  // The parent thread has finished its method - so now it
  // waits for each of the other processes to exit
  for ( thread_loop = 0; thread_loop < this->ThreadCount-1; thread_loop++ )
    {
    pthread_join( process_id[thread_loop], NULL );
    }
#endif

#ifndef VTK_USE_SPROC
#ifndef VTK_USE_PTHREADS
  // There is no multi threading, so there is only one thread.
  this->ThreadInfoArray[0].UserData    = this->MultipleData[0];
  this->ThreadInfoArray[0].ThreadCount = this->ThreadCount;
  (this->MultipleMethod[0])( (void *)(&this->ThreadInfoArray[0]) );
#endif
#endif
}

// Description:
// Print method for the multithreader
void vtkMultiThreader::PrintSelf(ostream& os, vtkIndent indent)
{
  
  os << indent << "Thread Count: " << this->ThreadCount << "\n";

}
