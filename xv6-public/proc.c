#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->priority=0;
  p->level=0;
  p->time_q=4;
  p->admin=0;
  p->memory_limit=0;
  p->start_time=ticks;
  p->shmem=0;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  int limit=curproc->memory_limit;

  if(limit!=0 && n+sz>limit) return -1;

  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } 
  else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  np->admin=curproc->admin;
  np->memory_limit=curproc->memory_limit;
  np->shmem=curproc->shmem;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  //cprintf("mypriority is %d\n\n", myproc()->priority);
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

void proc_list()
{
	cprintf("Name | PID | Time | Memory | MemLim\n");

	struct proc *p;

	acquire(&ptable.lock);

	for(p=ptable.proc; p<&ptable.proc[NPROC];p++)
	{
		if(p->state!=UNUSED)
		{
			cprintf("%s  ", p->name);
			cprintf("%d  ", p->pid);
			cprintf("%d  ", (ticks-p->start_time)*10);
			cprintf("%d  ", p->sz);
			cprintf("%d  \n", p->memory_limit);
		}
	}

	release(&ptable.lock);
}

char* getshmem(int pid)
{
	struct proc *p;
	struct proc *pp=0;
	acquire(&ptable.lock);
	for(p=ptable.proc;p<&ptable.proc[NPROC];p++)
	{
		if(p->pid==pid)
		{
			pp=p;
		}		
	}
	release(&ptable.lock);
	int t=pp->shmem;
	return (char*)(t);
}

int setmemorylimit(int pid, int limit)
{
	//cprintf("admin %d\n", myproc()->admin);
	if(myproc()->admin!=1) return -1;	
	//cprintf("int pcall pid: %d limit : %d\n", pid, limit);
	struct proc *p;
	struct proc *pp=0;
	acquire(&ptable.lock);
	for(p=ptable.proc;p<&ptable.proc[NPROC];p++)
	{
		if(p->pid==pid) pp=p;
	}
	release(&ptable.lock);

	if(pp==0||limit<0) return -1;

	if(pp->sz>limit) return -1;

	pp->memory_limit=limit;

	//cprintf("pp->memory_limit : %d\n", pp->memory_limit);

	return 0;
	
}

int getadmin(char *password)
{
	struct proc *p=myproc();

	char *pass="2016024893";
	//cprintf("%d", strcmp(password,pass));
	if(strcmp(password, pass)==0)
	{
		p->admin=1;
		return 0;
	}

	return -1;
}

int getlev(void)
{
//	cprintf("levle::: %d\n", myproc()->level);
	return myproc()->level;
}

int setpriority(int pid, int priority)
{
	struct proc *p;
//	cprintf("entering entering entering");
	if(priority<0||priority>10) return -2;
	acquire(&ptable.lock);
	int change=0;
	for(p=ptable.proc;p<&ptable.proc[NPROC];p++)
	{
		if(p->pid==pid)
		{
			if(p->parent->pid== myproc()->pid)
			{
				p->priority=priority;
				change=1;
			}

			break;
		}		
	}
	release(&ptable.lock);

	if(change==0) return -1;

	return 0;
}

int before_pid[5]={-1,-1,-1,-1,-1};

void q_level_boosting(void)
{
	for(int i=0;i<5;i++) before_pid[i]=-1;
	struct proc *p;
	acquire(&ptable.lock);
	for(p=ptable.proc;p<&ptable.proc[NPROC];p++)
	{
		p->level=0;
		p->time_q=4;
	}
	release(&ptable.lock);		
}

#ifdef MULTILEVEL_SCHED
void
scheduler(void)
{
 // cprintf("%d\n", MLFQ_K);
  struct proc *p;
  struct proc *pl;
  struct cpu *c = mycpu();
  c->proc = 0;
  int no_even=1;
  int minimum_pid=-1; 
  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);   
    
    int flag_even=0;
    int ppid=-1;
    for(pl=ptable.proc;pl<&ptable.proc[NPROC];pl++)
    {
      if(pl->state == RUNNABLE&&(pl->pid)%2==0) flag_even=1;
      if(pl->state == RUNNABLE&&(pl->pid)%2==1) 
      {
		if(ppid<=0) ppid=pl->pid;
		else if(ppid>pl->pid) ppid=pl->pid;
      }
    }    

    if(flag_even==1) no_even=0;
    else no_even=1;
    minimum_pid=ppid;    

    for(p = ptable.proc; p < &ptable.proc[NPROC];p++){
     // cprintf("%d\n", p->pid);
      if(p->state != RUNNABLE)
      {  
		continue;
      }
      if(no_even==0&&(p->pid)%2==1)
      {
		continue;
      }
      if(no_even==1&&p->pid!=minimum_pid)
      {
	//cprintf("%d %d\n", p->pid, minimum_pid);
		continue;
      }
      if(no_even==1&&(p->pid)%2==0) continue;
      // proc_cnt++;
      // cprintf("proc cnt : %d\n", proc_cnt);
      //cprintf("%d\n", no_even);
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }

    release(&ptable.lock);

  }
}

#elif MLFQ_SCHED

//int before_proc_pid=-1;

void
scheduler(void)
{
 // cprintf("%d\n", MLFQ_K);
 // int k=MLFQ_K;
  struct proc *p=0;
  struct proc *pl=0;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    
    acquire(&ptable.lock);         
    int current_max_priority=-1;
    int current_minimum_level=15;
    int isthere=0;
    int already_before_one=0;
    for(pl = ptable.proc; pl < &ptable.proc[NPROC];pl++){
     
      if(pl->state != RUNNABLE) continue;
      if(pl->time_q<=0) continue;
      if(pl->level<current_minimum_level)
      {
      	isthere=1;
      	current_minimum_level=pl->level;
      	current_max_priority=pl->priority;
      	p=pl;
      	already_before_one=0;
      }
      if(pl->level==current_minimum_level)
      {
      	if(already_before_one==1) continue;

      	if(already_before_one==0&&before_pid[current_minimum_level]==pl->pid)
      	{
      		if(pl->time_q>0&&pl->level==current_minimum_level)
      		{
      			p=pl;
      			already_before_one=1;
      		}
      	}

      	if(already_before_one==0&&current_max_priority<pl->priority)
      	{
      		current_max_priority=pl->priority;
      		p=pl;
      	}
      }
    }
    //cprintf("isthere is %d", &isthere);
    if(isthere==1)
    {
      before_pid[p->level]=p->pid;
      //cprintf("I store before datat %d %d\n\n", p->pid, p->level);
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      c->proc = 0;
      release(&ptable.lock);
    }
    else if(isthere==0)
    {
    	release(&ptable.lock);
    	q_level_boosting();
    }
    //release(&ptable.lock);
    //release needs to be in if(isthere==1) and else if(isthere==0)
    //I dont know exact reason but it makes error(acquire)
    //I need to find out the reason why it makes error later
  }
}
#else
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);   
    
    for(p = ptable.proc; p < &ptable.proc[NPROC];p++){
     // cprintf("%d\n", p->pid);
      if(p->state != RUNNABLE)
      {  
		continue;
      }
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }

    release(&ptable.lock);

  }
}
#endif
// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
