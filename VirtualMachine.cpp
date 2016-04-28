
#include "VirtualMachine.h"
#include "Machine.h"
#include "stdlib.h"
#include "vector"
#include <iostream>

using namespace std;

//@********************************************************************************************
//@*********1. hello.so works for single thread version
//@*********2. sleep.so works for single thread version
//@*********3. file.so works for single thread version
//@*********4. thread.so     MULTITHREADED VERSION
//@*********5. mutex.so      MULTITHREADED VERSION
//@*********6. preempt.s     MULTITHREADED VERSION
//@*********************************************************************************************



/*
 DOING MULTITHREADED VERSION
 */



extern "C"  //all the things inside are linked.It is because of name mangling in C++. This is so that linking can be done between C and C++ object files.
{
    
    TVMMainEntry VMLoadModule(const char *module);
    void VMUnloadModule(void);
    //************************* Thread Control Block **********************************
    
    typedef struct TCB  //Thread Control Block
    {
        int returnFile;                     //use when doing multithread
        void *entry;                         //for thread entry parameter used in ThreadcontectCreate
        uint8_t *base;                      //byte size type pointer for base of stack
        TVMThreadState ThreadState;         //dead, running, ready or waiting
        TVMThreadIDRef ThreadIDref;
        TVMThreadEntry ThreadEntry;
        TVMThreadID ThreadID;               //hold thread iDs
        TVMThreadPriority tPriority;        //thread schedular schedules threads according to priority (preemptive scheduling)
        TVMMemorySize memorySize;
        SMachineContext context;//use to switch to/from the thread
        TVMTick SleepTick;
    }TCB;
    
    
    TCB* currentThread = new TCB; //global current thread
    TCB* idleThread = new TCB;
    vector<TCB*> ThreadsVector; //global threads vector
    
    //ready threads have 3 prioity levels
    vector<TCB*> highPriority;
    vector<TCB*> lowPriority;
    vector<TCB*> normalPriority;
    
    //scheduling queues
    vector<TCB*> readyQueue;
    vector<TCB*> sleepingQueue;
    
    volatile int sleepCount;                //use for single-thread only
    volatile int callBackFlag = false;      //use for single-thread only
    //volatile int returnFile;                //use for single-thread only
    
    
    /*
     Process choosing the next thread from ready threads in priority queues to run
     */
    //************************* Scheduling threads ******************************
    void Scheduler()
    {
        TCB *newThread = new TCB;
        

        if(readyQueue.size() > 0)
        {

            if(currentThread->ThreadState == VM_THREAD_STATE_RUNNING)
                currentThread->ThreadState = VM_THREAD_STATE_READY;

            //choose the appropriate priority queue to put the ready thread in
            if(currentThread->ThreadState == VM_THREAD_STATE_READY)
            {

                if (currentThread->tPriority == VM_THREAD_PRIORITY_HIGH)
                    highPriority.push_back(currentThread);
                
                else if (currentThread->tPriority == VM_THREAD_PRIORITY_NORMAL)
                    normalPriority.push_back(currentThread);
                
                else if (currentThread->tPriority == VM_THREAD_PRIORITY_LOW)
                    lowPriority.push_back(currentThread);
            }
            
            else if(currentThread->ThreadState == VM_THREAD_STATE_WAITING && currentThread->SleepTick != 0)
            {

                sleepingQueue.push_back(currentThread);
            }
            
            /*
             Set the highest priority thread to new thread
             Then would switch content with currentThread
             */
            if(highPriority.size() > 0)
            {

                for(int pos =0; pos < highPriority.size(); pos++)
                {

                    newThread = highPriority[pos];
                    highPriority.erase(highPriority.begin() + pos);
                    TCB *oldThread = currentThread;                                         //put current thread's content to old thread
                    currentThread = newThread;                                              //update currentThread
                    currentThread->ThreadState = VM_THREAD_STATE_RUNNING;                   //make currentThread w/ new content running
                    MachineContextSwitch(&(currentThread->context), &(oldThread->context));

                }
            }
            
            else if(normalPriority.size() > 0)
            {

                for(int pos =0; pos < normalPriority.size(); pos++)
                {

                    newThread = normalPriority[pos];
                    normalPriority.erase(normalPriority.begin() + pos);
                    TCB *oldThread = currentThread;                                         //put current thread's content to old thread
                    currentThread = newThread;                                              //update currentThread
                    currentThread->ThreadState = VM_THREAD_STATE_RUNNING;                   //make currentThread w/ new content running
                    MachineContextSwitch(&(currentThread->context), &(oldThread->context));
                }
            }
            
            else if(lowPriority.size() > 0)
            {

                for(int pos =0; pos < lowPriority.size(); pos++)
                {

                    newThread = lowPriority[pos];
                    lowPriority.erase(lowPriority.begin() + pos);
                    TCB *oldThread = currentThread;                                         //put current thread's content to old thread
                    currentThread = newThread;                                              //update currentThread
                    currentThread->ThreadState = VM_THREAD_STATE_RUNNING;                   //make currentThread w/ new content running
                    MachineContextSwitch(&(currentThread->context), &(oldThread->context));
                    

                }
            }
        }//end of if ready queue is not empty
        
        
        //MAking idle thread
        else
        {

            currentThread = idleThread;
            TCB *oldThread = currentThread;                                         //put current thread's content to old thread
            currentThread = newThread;                                              //update currentThread
            currentThread->ThreadState = VM_THREAD_STATE_RUNNING;                   //make currentThread w/ new content running
            MachineContextSwitch(&(currentThread->context), &(oldThread->context));
            

        }
    }
    
    
    //************************* Call Back functions ******************************
    void FilesCallBack (void *cb, int result) //call back to signal when machine is done
    {

        currentThread -> returnFile = result;
        currentThread -> ThreadState = VM_THREAD_STATE_READY;
        readyQueue.push_back(currentThread);
        Scheduler();
    }
    
    
    /*  It will be called every tick. This is where you will see if threads should wake up,
     or if another thread should be switched in. The alarm is called every period of time
     signifying a "tick" Sleep waits for a certain number of "ticks".
     
     For all sleep threads in sleepQueue, count down their ticks, once a thread's ticks
     reaches 0, put that thread to ready state and schedule it.
     */
    void AlarmCallback(void *calldata)
    {

        if(sleepingQueue.size() >0)
        {

            for(int pos =0; pos < sleepingQueue.size() ; pos++ )
            {
                sleepingQueue[pos]->SleepTick --;

                if(sleepingQueue[pos]->SleepTick == 0) //time for the thread to wake up
                {

                    sleepingQueue[pos]->ThreadState = VM_THREAD_STATE_READY;
                    
                    readyQueue.push_back(sleepingQueue[pos]);
                    sleepingQueue.erase(pos + sleepingQueue.begin());
                    idleThread->ThreadState = VM_THREAD_STATE_WAITING;
                }
            }
        }
        
        Scheduler();
    }
    
    //************************************** Skeleton *************************************
    void Skeleton(void *parameter)
    {
        TCB* ske = (TCB*) parameter;
        MachineEnableSignals();
        
        //call entry of thread
        ske-> ThreadEntry(ske->entry);
        VMThreadTerminate(ske->ThreadID);  //gain back control
    }
    
    
    //****************************** VMStart *************************************
    TVMStatus VMStart( int tickms, int argc, char *argv[])
    {
        //VMStart() starts the virtual machine by loading the module specified by argv[0].
        TVMMainEntry VMMain = VMLoadModule(argv[0]);
        
        //initialize the machine/alarm
        MachineInitialize();
        cout << "start1" <<endl;

        //request machine alarm( it requests that the callback is called at the period specified by the time)
        MachineRequestAlarm(tickms*1000, AlarmCallback, NULL);
        
        cout << "start2" <<endl;
        
        if(VMMain == NULL)
        {
            MachineTerminate();
            return VM_STATUS_FAILURE;
        }
        
        else
        {
            //set up the main thread and push it into the thread vector
            TCB* mainThread = new TCB();
            mainThread->ThreadState = VM_THREAD_STATE_RUNNING;
            mainThread->tPriority = VM_THREAD_PRIORITY_NORMAL;
            mainThread->SleepTick = 0;
            mainThread->ThreadID = ThreadsVector.size();
            ThreadsVector.push_back(mainThread);
            
            //set up the idle thread
            idleThread->ThreadState = VM_THREAD_STATE_DEAD;
            idleThread->tPriority = 0;
            idleThread->SleepTick = 0;
            idleThread->ThreadID = ThreadsVector.size();
            ThreadsVector.push_back(mainThread);
            
            cout << "start3" <<endl;

            MachineContextCreate(&ThreadsVector[0]->context, Skeleton, ThreadsVector[0], ThreadsVector[0]->base, ThreadsVector[0]->memorySize);
            //The argc and argv are passed directly into the VMMain() function that exists in the loaded module.
            VMMain(argc, argv);       //run the module that we just loaded
            MachineTerminate();
            VMUnloadModule();
            return VM_STATUS_SUCCESS;
        }
    }
    
    
    //****************************** VMFileOpen *************************************
    TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);

        if (filename == NULL || filedescriptor == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        
        MachineFileOpen(filename, flags, mode, FilesCallBack, currentThread);

        
        ///thread blocks in a wait state until either suc or unsucessful openng of the file is completed
        currentThread ->ThreadState = VM_THREAD_STATE_WAITING;

        //The file descriptor of newly opened file will be passed in to callback function as the result.
        *filedescriptor = currentThread -> returnFile;
        
        MachineResumeSignals(&sigState);

        return VM_STATUS_SUCCESS;
        
    }
    
    
    //****************************** VMFileClose *************************************
    TVMStatus VMFileClose(int filedescriptor)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        MachineFileClose(filedescriptor, FilesCallBack, currentThread);
        
        ///thread blocks in a wait state until either suc or unsucessful openng of the file is completed
        currentThread ->ThreadState = VM_THREAD_STATE_WAITING;
        
        MachineResumeSignals(&sigState);
        
        if(currentThread -> returnFile >= 0)
            return VM_STATUS_SUCCESS;
        
        else
            return VM_STATUS_FAILURE;
        
    }
    
    
    //****************************** VMFileRead *************************************
    TVMStatus VMFileRead(int filedescriptor, void *data, int *length)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        if(data == NULL || length == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        
        else
        {
            MachineFileRead(filedescriptor, data, *length, FilesCallBack, currentThread);
            
            ///thread blocks in a wait state until either suc or unsucessful openng of the file is completed
            currentThread ->ThreadState = VM_THREAD_STATE_WAITING;
            
            //the actual number of bytes transferred by the write will be updated in length location
            *length = currentThread -> returnFile;
            
            MachineResumeSignals(&sigState);
            
            if(currentThread -> returnFile >= 0)
                return VM_STATUS_SUCCESS;
            
            else
                return VM_STATUS_FAILURE;
            
        }
    }
    
    
    //****************************** VMFileWrite *************************************
    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        //If data or length parameters are NULL, VMFileWrite() returns VM_STATUS_ERROR_INVALID_PARAMETER.
        if(data == NULL || length == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        
        else
            
        {
            MachineFileWrite(filedescriptor, data, *length, FilesCallBack, currentThread);
            
            ///thread blocks in a wait state until either suc or unsucessful openng of the file is completed
            currentThread ->ThreadState = VM_THREAD_STATE_WAITING;
            
            *length = currentThread -> returnFile;
            
            MachineResumeSignals(&sigState);
            
            if(currentThread -> returnFile >= 0)
                return VM_STATUS_SUCCESS;
            
            else
                return VM_STATUS_FAILURE;
        }
    }
    
    
    //****************************** VMFileSeek *************************************
    TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset)  //whence: location
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        MachineFileSeek(filedescriptor, offset, whence, FilesCallBack, currentThread);
        
        ///thread blocks in a wait state until either suc or unsucessful openng of the file is completed
        currentThread ->ThreadState = VM_THREAD_STATE_WAITING;
        Scheduler();
        
        *newoffset = currentThread -> returnFile;
        
        
        if (currentThread -> returnFile >= 0)
        {
            MachineResumeSignals(&sigState);
            return VM_STATUS_SUCCESS;
        }
        
        return VM_STATUS_FAILURE;
    }
    
    
    //****************************** VMThreadSleep *************************************
    TVMStatus VMThreadSleep(TVMTick tick)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        if ( tick == VM_TIMEOUT_INFINITE )
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        
        else
        {
            if( tick != VM_TIMEOUT_IMMEDIATE)
            {
                currentThread->SleepTick = tick;
                currentThread->ThreadState = VM_THREAD_STATE_WAITING;
                readyQueue.push_back(currentThread);
            }
            
            currentThread->ThreadState = VM_THREAD_STATE_READY;
            
            Scheduler();
            
            MachineResumeSignals(&sigState);
            return VM_STATUS_SUCCESS;
        }
        
    }
    
    
    //*************************************VMThreadState************************************
    TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        if(stateref == NULL)
        {
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        else
        {
            for (int i = 0; i < ThreadsVector.size();i++)
            {
                if(ThreadsVector[i]->ThreadID == thread)
                {
                    if(i == ThreadsVector.size())
                    {
                        return VM_STATUS_ERROR_INVALID_ID;
                    }
                    else
                    {
                        *stateref = ThreadsVector[i]-> ThreadState;
                    }
                }
            }
        }
        //*stateref = ThreadsVector.at(thread)->ThreadState;
        MachineResumeSignals(&sigState);
        
        return VM_STATUS_SUCCESS;
    }
    
    
    //**************************************VMThreadCreate*************************************
    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        if (entry == NULL || tid == NULL)
        {
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        
        else
        {
            TCB *newTCB = new TCB();
            newTCB->ThreadEntry = entry;
            //memsize in VMThreadCreate same as stacksize in MachineContextCreate
            newTCB->base = new uint8_t[memsize]; // stacksize use uint8_t.
            newTCB->ThreadState = VM_THREAD_STATE_DEAD;
            newTCB->tPriority = prio;
            newTCB->ThreadIDref = tid;
            newTCB->ThreadID = ThreadsVector.size();
            ThreadsVector.push_back(newTCB);
            
            MachineResumeSignals(&sigState);
            return VM_STATUS_SUCCESS;
        }
    }
    
    
    
    //activates the dead thread specified by thread parameter in the virtual machine.
    //************************************** VMThreadActivate *************************************
    TVMStatus VMThreadActivate(TVMThreadID thread)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        TCB* deadThread = ThreadsVector[thread];
        
        if(thread < 0)
            return VM_STATUS_ERROR_INVALID_ID;
        
        else
        {
            if(deadThread->ThreadState == VM_THREAD_STATE_DEAD)
            {
                MachineContextCreate(&deadThread->context, Skeleton, deadThread->entry, deadThread->base,deadThread->memorySize);
                
                deadThread->ThreadState = VM_THREAD_STATE_READY;
                
                //push into priority queues
                if(deadThread->ThreadState == VM_THREAD_STATE_READY)
                {
                    if (deadThread->tPriority == VM_THREAD_PRIORITY_HIGH)
                        highPriority.push_back(deadThread);
                    
                    else if (deadThread->tPriority == VM_THREAD_PRIORITY_NORMAL)
                        normalPriority.push_back(deadThread);
                    
                    else if (deadThread->tPriority == VM_THREAD_PRIORITY_LOW)
                        lowPriority.push_back(deadThread);
                }
                
                
                if(deadThread->tPriority > ThreadsVector[thread]->tPriority)
                    Scheduler();
            }
            
            else
                return VM_STATUS_ERROR_INVALID_STATE;
            
        }
        
        MachineResumeSignals(&sigState);
        
        return VM_STATUS_SUCCESS;
    }
    
    
    //************************************** VMThreadDelete *************************************
    TVMStatus VMThreadDelete(TVMThreadID thread)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        
        if(ThreadsVector.at(thread)->ThreadState == VM_THREAD_STATE_DEAD)
        {
            delete ThreadsVector.at(thread);
            //Replace the thread's location with NULL to avoid changing the index of each element.
            ThreadsVector.at(thread) = NULL;
        }
        
        else if (ThreadsVector.at(thread)->ThreadState != VM_THREAD_STATE_DEAD)
        {
            return VM_STATUS_ERROR_INVALID_STATE;
        }
        
        else if (ThreadsVector.size() < thread)
        {
            MachineResumeSignals(&sigState);
            return VM_STATUS_ERROR_INVALID_ID;
        }
        
        return VM_STATUS_SUCCESS;
    }
    
    
    //************************************** VMThreadTernimate *************************************
    TVMStatus VMThreadTerminate(TVMThreadID thread)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        if (ThreadsVector.at(thread)->ThreadState == VM_THREAD_STATE_DEAD)
        {
            return VM_STATUS_ERROR_INVALID_STATE;
        }
        else if(ThreadsVector.size() < thread)
        {
            return VM_STATUS_ERROR_INVALID_ID;
        }
        ThreadsVector.at(thread)->ThreadState = VM_THREAD_STATE_DEAD;
        Scheduler();
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    
    
    //************************************** VMThreadID *************************************
    TVMStatus VMThreadID(TVMThreadIDRef threadref)
    {
        TMachineSignalState sigState;
        MachineSuspendSignals(&sigState);
        if(threadref == NULL)
        {
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    
  
}




















