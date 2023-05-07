#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "file.h"

//hardcoding: convert nice to weight value
int nice_to_weight[40] = {
	88818, 71054, 56843, 45475, 36380,
	29104, 23283, 18626, 14901, 11921,
	9537, 7629, 6104, 4883, 3906, 
	3125, 2500, 2000, 1600, 1280,
	1024, 819, 655, 524, 419,
	336, 268, 214, 172, 137,
	110, 88, 70, 56, 45,
	36, 29, 23, 18, 15
};

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

struct mmap_area {
  struct file *f;
  uint addr;
  int length;
  int offset;
  int prot;
  int flags;
  struct proc *p;
};

struct mmap_area mmap_areas[64];

// 0: empty
// 1: private mapping with MAP_POPULATE
// -1: private mapping without MAP_POPULATE
int mmap_status_flag[64] = {0,};

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// Calculate total weight of RUNNABLE process
int
totalweight(struct proc *firstp, struct proc *lastp) {
	struct proc *p;
	int total_weight = 0;
	
	for(p = firstp; p < lastp; p++) {
    		if(p->state != UNUSED)
			total_weight += p->weight;
	}
   	return total_weight;
}

// Update vruntime and handle the overflow situation
void
vruntimeupdate(struct proc *curproc) {
	// If vruntime is greater than max int value, vruntime becomes 0 
	if (curproc->vruntime +
	(1024 * ((double) (curproc->actual_runtime - curproc->scheduled_time) / curproc->weight)) > 2147483647) {
  		curproc->int_overflow++;
  		curproc->vruntime += 1024 * (double) (curproc->actual_runtime - curproc->scheduled_time)/curproc->weight - 2147483647;
	}
	else curproc->vruntime += (1024 * ((double) (curproc->actual_runtime - curproc->scheduled_time) / curproc->weight));
}

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
  //setting when the process is created
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->nice = 20;
  p->weight = nice_to_weight[p->nice];
  p->actual_runtime = 0;
  p->vruntime = 0;
  p->scheduled_time = 0;
  p->time_slice = 0;
  p->int_overflow = 0;
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
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
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
  // Inherits nice and vruntime of parent process
  np->nice = curproc->nice;
  np->weight = nice_to_weight[np->nice];
  np->vruntime = curproc->vruntime;
  np->int_overflow = curproc->int_overflow;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  for (i = 0; i < 64; i++) {
    if (curproc == mmap_areas[i].p && (mmap_status_flag[i] != 0)) {

      int mmap_arr_idx = 0;
      for (mmap_arr_idx = 0; mmap_arr_idx < 64; mmap_arr_idx++) {
        if (mmap_status_flag[i] == 0) {
          break;
        }
      }
      mmap_areas[mmap_arr_idx].addr = mmap_areas[i].addr;
      mmap_areas[mmap_arr_idx].f = mmap_areas[i].f;
      mmap_areas[mmap_arr_idx].length = mmap_areas[i].length;
      mmap_areas[mmap_arr_idx].offset = mmap_areas[i].offset;
      mmap_areas[mmap_arr_idx].prot = mmap_areas[i].prot;
      mmap_areas[mmap_arr_idx].flags = mmap_areas[i].flags;
      mmap_areas[mmap_arr_idx].p = np;
      mmap_status_flag[mmap_arr_idx] = mmap_status_flag[i];

      int length = mmap_areas[i].length;
      int flags = mmap_areas[i].flags;
      int offset = mmap_areas[i].offset;
      uint addr = mmap_areas[i].addr;
      struct file *pfile = mmap_areas[i].f;
      int prot = mmap_areas[i].prot;

      if ((flags == 0 && mmap_status_flag[i] == 1) || flags == MAP_POPULATE) { // private file mapping with MAP_POPULATE
        pfile->off = offset;
        char *mem = 0;
        int i = 0;
        for (i = 0; i < length; i += PGSIZE) {
          mem = kalloc();
          if (mem == 0) return 0;
          memset(mem, 0, PGSIZE);

          fileread(pfile, mem, PGSIZE);
          
          // Create PTEs for virtual addresses starting at va that refer to
          // physical addresses starting at pa. va and size might not
          // be page-aligned.
          // mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm) 
          mappages(curproc->pgdir, (void *) (addr + i), PGSIZE, V2P(mem), prot | PTE_U);
        
        }
        mmap_status_flag[mmap_arr_idx] = 1;
      }
      else if ((flags == MAP_ANONYMOUS && mmap_status_flag[i] == 1) || flags == (MAP_ANONYMOUS | MAP_POPULATE)) { // private anonymous mapping with MAP_POPULATE
          
          int i = 0;
          char *mem = 0;
          for (i = 0; i < length; i += PGSIZE) {
            mem = kalloc();
            if (mem == 0) return 0;
            memset(mem, 0, PGSIZE);

            fileread(pfile, mem, PGSIZE);
            
            // Create PTEs for virtual addresses starting at va that refer to
            // physical addresses starting at pa. va and size might not
            // be page-aligned.
            // mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm) 
            mappages(curproc->pgdir, (void *) (addr + i), PGSIZE, V2P(mem), prot | PTE_U);
          
        }
        mmap_status_flag[mmap_arr_idx] = 1;
      }
    }
  }

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
  
  // Calculate vruntime
  vruntimeupdate(curproc);
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
void
scheduler(void)
{
  struct proc *p;
  struct proc *shortestjob = 0;
  struct cpu *c = mycpu();
  uint total_weight = 0;
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    // Gain a total weight value
    total_weight = totalweight(ptable.proc, &ptable.proc[NPROC]);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state != RUNNABLE) 
      	continue;

      // Find the shortest runtime process 
      shortestjob = p;
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      	if (p->state == RUNNABLE) {
	  if (p->int_overflow < shortestjob->int_overflow)
      	    shortestjob = p;
	  else if (p->int_overflow == shortestjob->int_overflow && p->vruntime < shortestjob->vruntime)
            shortestjob = p;
      	}
      }
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      shortestjob->time_slice = 1000 * (int) ((double) shortestjob->weight / total_weight + 0.5) * 10;
      shortestjob->scheduled_time = shortestjob->actual_runtime;
      c->proc = shortestjob;
      switchuvm(shortestjob);
      shortestjob->state = RUNNING;

      swtch(&(c->scheduler), shortestjob->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

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
   
  vruntimeupdate(myproc()); // Update vruntime 
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
  vruntimeupdate(p); 
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
  struct proc *shortest = 0;
  // Looking for RUNNABLE process
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state == RUNNABLE) 
        continue;
      shortest = p;
  }
  // Looking for shortest runtime RUNNABLE process
  // considering overflow
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == RUNNABLE) {
	if(p->int_overflow < shortest->int_overflow)
    	  shortest = p;
	else if (p->int_overflow == shortest->int_overflow && p->vruntime < shortest->vruntime) 
	  shortest = p;
    }
  }
  // Wake up
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      if (shortest->int_overflow != 0 || shortest->vruntime != 0) {
        p->vruntime = (shortest->vruntime - (int) (1024 / (double) p->weight + 0.5)) * 1000;
        p->int_overflow = shortest->int_overflow;
      }
      else {
	p->vruntime = shortest->vruntime;
	p->int_overflow = 0;
      }
    }
  }
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
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
 	vruntimeupdate(p);       
      }
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

int
getnice(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      int nice = p->nice;
      release(&ptable.lock);	
      return nice;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
setnice(int pid, int value)
{
  struct proc *p;
  
  if(value > 39 || value < 0)
	  return -1;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->nice = value;
      p->weight = nice_to_weight[p->nice];
      break;
    }
  }
  release(&ptable.lock);
  return 0;
}

void
ps(int pid)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("name\tpid\tstate\t\tpriority\truntime/weight   runtime   \tvruntime\ttick %d\n", 1000*ticks);
  if(pid == 0){
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == 0)
	      continue;
      cprintf("%s\t%d\t", p->name, p->pid);
      switch(p->state){
      case 0:
	      cprintf("UNUSED  \t");
	      break;
      case 1:
	      cprintf("EMBRYO  \t");
	      break;
      case 2:
	      cprintf("SLEEPING\t");
	      break;
      case 3:
	      cprintf("RUNNABLE\t");
	      break;
      case 4:
	      cprintf("RUNNING \t");
	      break;
      case 5:
	      cprintf("ZOMBIE  \t");
	      break;
      }

      if (p->actual_runtime == 0)	
	cprintf("%d\t        %d\t\t %d   \t\t%d\n", p->nice, p->actual_runtime/p->weight, p->actual_runtime, p->vruntime);
      else 
	cprintf("%d\t        %d\t\t %d   \t%d\n", p->nice, p->actual_runtime/p->weight, p->actual_runtime, p->vruntime);
    }
    release(&ptable.lock);
    return;
  }
  
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == 0)
	    continue;
    if(p->pid == pid){
      cprintf("%s\t%d\t", p->name, p->pid);
      switch(p->state){
      case 0:
	      cprintf("UNUSED\t");
	      break;
      case 1:
	      cprintf("EMBRYO\t");
	      break;
      case 2:
	      cprintf("SLEEPING\t");
	      break;
      case 3:
	      cprintf("RUNNABLE\t");
	      break;
      case 4:
	      cprintf("RUNNING\t");
	      break;
      case 5:
	      cprintf("ZOMBIE\t");
	      break;
      }
	
      if (p->actual_runtime == 0)	
	cprintf("%d\t        %d\t\t %d   \t\t%d\n", p->nice, p->actual_runtime/p->weight, p->actual_runtime, p->vruntime);
      else 
	cprintf("%d\t        %d\t\t %d   \t%d\n", p->nice, p->actual_runtime/p->weight, p->actual_runtime, p->vruntime);
      release(&ptable.lock);
      return;
    }
  }

  release(&ptable.lock);
  return;
}

// Succeed: return the start address of mapping area
// Failed: return 0

// length is also a multiple of page size
// - MMAPBASE + addr + length is the end address of mapping

// prot can be PROT_READ or {RPT_READ|PROT_WRITE}
// - prot should be match with file's open flag

// flags can be given with the combinations
// if MAP_ANONYMOUS is given, anonymous mapping
// else it is file mapping
// if MAP_POPULATE is give, allocate physical page & make page table for whole mapping area
// else just record its mapping area.

// fd is for file mappings, if not, it should be -1
// offset is given for file mappings, if not, it should be 0
uint
mmap(uint addr, int length, int prot, int flags, int fd, int offset)
{
  // MMAPBASE(0x40000000) + addr is the start address of mapping
  addr = addr + MMAPBASE;
  struct proc *curproc = myproc();

  // Fails if fd is stdin or stdout, or length is 0, or offset is greater than PGSIZE
  // Also, length is a multiple of page size
  if (fd == 0 || fd == 1 || length == 0 || length%PGSIZE == 0 || offset < 0 || offset > PGSIZE) {
    return 0;
  }
  // Prot can be PROT_READ or PROT_READ|PROT_WRITE
  if (prot != PROT_READ || prot != (PROT_READ | PROT_WRITE)) {
    return 0;
  }

  // pfil allocation
  struct file *pfile = 0;
  // ANONYMOUS
  if (fd == -1) { 
    pfile = 0;
  }
  // POPULATE
  else { 
    if (fd < 0) return 0;
    pfile = curproc->ofile[fd];
  }

  // FLAGS
  //    MAP_ANONYMOUS: anonymous mapping
  // no MAP_ANONYMOUS: file mapping
  //     MAP_POPULATE: allocate physical page & make page table for whole mapping area
  //  no MAP_POPULATE: just record its mapping area

  // anonymous mapping, but offset is not 0 and fd is not -1 => INVALID
  if ((flags & MAP_ANONYMOUS) && (offset != 0 || fd != -1)) return 0;

  // not anonymous, but the fd is -1
  if ((flags & MAP_ANONYMOUS) == 0 && fd == -1) return 0;

  // file mapping, but file type is not FD_INODE,
  // PROT_READ but not readable
  // PROT_WRITE but not writable      
  // => INVALID
  if (!(flags & MAP_ANONYMOUS)) {
    if (pfile->type != FD_INODE) return 0;
    if ((prot & PROT_READ) && !pfile->readable) return 0;
    if ((prot & PROT_WRITE) && !pfile->writable) return 0;
  }

  // Find the empty mmap_area_arr idx
  int mmap_arr_idx = 0;
  for (mmap_arr_idx = 0; mmap_arr_idx < 64; mmap_arr_idx++) {
    if (mmap_status_flag[mmap_arr_idx] == 0) {
      break;
    }
  }
  // If Not Found
  if (mmap_arr_idx == 64) return 0; 

  // If ANONYMOUS
  if (pfile == 0) mmap_areas[mmap_arr_idx].f = 0;
  // If Not ANONYMOUS => File mapping
  else mmap_areas[mmap_arr_idx].f = filedup(pfile);

  // Store the mmap_area information to array
  mmap_areas[mmap_arr_idx].length = length;
  mmap_areas[mmap_arr_idx].offset = offset;
  mmap_areas[mmap_arr_idx].prot = prot;
  mmap_areas[mmap_arr_idx].flags = flags;
  mmap_areas[mmap_arr_idx].p = curproc;
  // -1 means mapping without MAP_POPULATE
  mmap_status_flag[mmap_arr_idx] = -1;

  if (flags == 0) {
    // page fault
    return addr;
  }
  else if (flags == MAP_ANONYMOUS) { // private file mapping without MAP_POPULATE
    // page fault
    return addr;
  }
  else if (flags == MAP_POPULATE) { // private file mapping with MAP_POPULATE
    pfile->off = offset;
    char *mem = 0;
    int i = 0;

    // For example, if length is 8192, there will be 2 pages
    for (i = 0; i < length; i += PGSIZE) {
      // allocate
      if ((mem = kalloc()) == 0) return 0;  
      memset(mem, 0, PGSIZE);

      // read the file
      fileread(pfile, mem, PGSIZE);
      
      // Create PTEs for virtual addresses starting at va that refer to
      // physical addresses starting at pa. va and size might not
      // be page-aligned.
      // mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm) 
      mappages(curproc->pgdir, (void *) (addr + i), PGSIZE, V2P(mem), prot | PTE_U);
    }
    // flag 1: private mapping with MAP_POPULATE
    mmap_status_flag[mmap_arr_idx] = 1;
  }
  else if (flags == (MAP_ANONYMOUS | MAP_POPULATE)) { // private anonymous mapping with MAP_POPULATE
      
      char *mem = 0;
      int i = 0;
      for (i = 0; i < length; i += PGSIZE) {
        if ((mem = kalloc()) == 0) return 0;
        memset(mem, 0, PGSIZE);

        fileread(pfile, mem, PGSIZE);
        
        // Create PTEs for virtual addresses starting at va that refer to
        // physical addresses starting at pa. va and size might not
        // be page-aligned.
        // mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm) 
        mappages(curproc->pgdir, (void *) (addr + i), PGSIZE, V2P(mem), prot | PTE_U);
      
    }
    // 1: private mapping with MAP_POPULATE
    mmap_status_flag[mmap_arr_idx] = 1;
  }

  return addr;
}

int page_fault_handler(uint error) {
  uint va;

  // The page fault virtual address
  if ((va = rcr2()) < 0) {
    return -1;
  }
  
  int mmap_arr_idx = 0;
  for (mmap_arr_idx = 0; mmap_arr_idx < 64; mmap_arr_idx++) {
    // If virtual address is in the range of certain mmap_area, and the process is the same
    // break the loop
    if (mmap_areas[mmap_arr_idx].addr <= va 
      && va < mmap_areas[mmap_arr_idx].addr + mmap_areas[mmap_arr_idx].length 
      && mmap_areas[mmap_arr_idx].p == myproc()) {
      break;
    }
  }
  // faulted address has no corresponding mmap_area
  if (mmap_arr_idx == 64) return -1;

  // cannot write, but tried to write
  if ((mmap_areas[mmap_arr_idx].prot & PROT_WRITE) != 2 && (error & 2) == 1) { 
    return -1;
  }

  // 0: empty
  // 1: private mapping with MAP_POPULATE
  // -1: private mapping without MAP_POPULATE
  if (mmap_status_flag[mmap_arr_idx] == -1) {
    cprintf("Cannot access the page\n");
    return -1;
  }
  if (mmap_status_flag[mmap_arr_idx] == 1) {
    cprintf("Already using page\n");
    return -1;
  }
  if (mmap_status_flag[mmap_arr_idx] != 0) return -1;

  // if status flag is 0(empty), it is possible to allocate mmap_area
  char *mem = 0;  
  if ((mem = kalloc()) == 0) return 0;
  memset(mem, 0, PGSIZE);

  fileread(mmap_areas[mmap_arr_idx].f, mem, PGSIZE);
        
  // Create PTEs for virtual addresses starting at va that refer to
  // physical addresses starting at pa. va and size might not
  // be page-aligned.
  // mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm) 
  mappages(myproc()->pgdir, (void *) va, PGSIZE, V2P(mem), mmap_areas[mmap_arr_idx].prot | PTE_U);
  mmap_status_flag[mmap_arr_idx] = 1;
}

// Succeed: 1
// Failed: -1
int
munmap(uint addr) {
  struct proc *curproc = myproc();
  uint va = addr + 0x40000000;
  // Find the corresponding mmap_area_arr idx
  int mmap_arr_idx = 0;
  for (mmap_arr_idx = 0; mmap_arr_idx < 64; mmap_arr_idx++) {
    if (mmap_areas[mmap_arr_idx].addr <= va 
      && va < mmap_areas[mmap_arr_idx].addr + mmap_areas[mmap_arr_idx].length 
      && mmap_areas[mmap_arr_idx].p == curproc) {
      break;
    }
  }
  // If the corresponding mmap_area doesn't exist, it fails
  if (mmap_arr_idx == 64) return -1;

  pde_t *pte = 0;
  int length = mmap_areas[mmap_arr_idx].length;

  // Free the pages and page tables
  for(int i = 0; i < length; i += PGSIZE) {
    // If there are no pte, it fails
    if ((pte = walkpgdir(curproc->pgdir, (char *)(addr + i), 0)) == 0) {
      return -1;
    }

    if((*pte & PTE_P)){     // If PTE is present
      uint pa = P2V(*pte);  // Get the physical address
      kfree(P2V(pa));       // Free the address
      *pte = 0;             // Make the pte empty
    }
  }
  
  mmap_status_flag[mmap_arr_idx] = 0;
  return 1;
}

// Return the free memory pages
int
freemem() {
  return freemem_count();
}